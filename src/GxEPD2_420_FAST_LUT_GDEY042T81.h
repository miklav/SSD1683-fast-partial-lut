// GxEPD2_420_FAST_LUT_GDEY042T81.h
//
// Fast (~510 ms) black/white partial-refresh for the WeAct 4.2" e-paper
// (Good Display GDEY042T81 / Solomon Systech SSD1683), via a fully custom
// 227-byte waveform LUT. Drop-in subclass of GxEPD2's stock driver.
//
// Part of SSD1683-fast-partial-lut: https://github.com/miklav/SSD1683-fast-partial-lut
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 SSD1683-fast-partial-lut contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. Distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; see the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.
//
// Builds on GxEPD2 by Jean-Marc Zingg (GPL-3.0): the panel driver this
// subclasses, and the source of the panel's drive-voltage / soft-start bytes
// (from its GxEPD2_4G 4-grayscale LUT for the same panel).
//
// ---------------------------------------------------------------------------
// WHAT THIS DOES
//
// Stock GxEPD2 partial refresh on this panel runs the controller's built-in
// OTP waveform (~1050 ms). This preloads a custom 227-byte waveform and gets a
// clean partial in ~510 ms. The result is a FIXED ~510 ms (about half the OTP
// path); see "limits" below.
//
// THE RECIPE (all three are required -- any one missing => no drive or wrong
// image; this took ~19 iterations to pin down, see the project README):
//   1. A COMPLETE 227-byte SSD1683 B/W waveform LUT via cmd 0x32 (NOT the
//      70-byte SSD1675 nor 153-byte SSD1680 formats -- those are too short and
//      leave the rest of the LUT register at stale OTP/garbage values). Plus
//      companion registers 0x3F (EOPT), 0x03 (VGH), 0x04 (VSH1/VSH2/VSL),
//      0x2C (VCOM).
//   2. Booster soft-start 0x0C -- a custom LUT does NOT carry the OTP's
//      charge-pump config, so without it the pump never ramps and the partial
//      is a silent ~150 ms no-op.
//   3. Update byte 0x22 = 0xDC = load_LUT + black/white + display, load_temp
//      OFF. load_LUT transfers OUR register into the waveform sequencer;
//      load_temp (set in stock 0xFC) MUST stay clear or it reloads the OTP
//      waveform over our register.
//
// LUT FORMAT (SSD1683 datasheet Rev 1.0, Fig 6-7): 5 LUTs x 6 groups x 7 bytes
// = 210, then [224]=FR, [225..226]=XON. Each group: RP_n, (VS0A<<6|TP0A),
// 0B, 0C, 0D, SR_nAB, SR_nCD. VS levels: 0=VSS, 1=VSH1(black), 2=VSL(white),
// 3=VSH2 (Table 6-6). This LUT is a DIRECT drive (non-differential): each pixel
// is driven to its CURRENT value regardless of the previous frame, so
// LUTBB=LUTBW=black and LUTWW=LUTWB=white; no dependence on the 0x26
// previous-frame RAM.
//
// LIMITS / CAVEATS
//   * The ~510 ms is a FIXED duration on this load-LUT path: per-phase frame
//     count (TP) and the frame-rate (FR) byte do NOT change it (all tested).
//     The controller honors our voltage levels but not the LUT's own timing.
//     Good Display's 0.35 s spec is not reached this way.
//   * Drive voltages are lifted from GxEPD2_4G's working LUT for this panel,
//     not independently tuned. They render cleanly at room temperature; this
//     LUT has NO temperature compensation, so behaviour at temperature extremes
//     is uncharacterised.
//   * The single drive phase is DC-unbalanced for simplicity. Do an occasional
//     full refresh (e.g. every ~50 partials) to clear accumulated ghosting.
//   * Verified on a Waveshare ESP32-S3 Nano + WeAct 4.2" GDEY042T81. Other
//     SSD1683 panels likely work but may need different voltages/MUX.
//
// USAGE: include this header and use the class in place of the stock driver:
//
//   #include <GxEPD2_420_FAST_LUT_GDEY042T81.h>
//   GxEPD2_BW<GxEPD2_420_FAST_LUT_GDEY042T81, GxEPD2_420_FAST_LUT_GDEY042T81::HEIGHT>
//     display(GxEPD2_420_FAST_LUT_GDEY042T81(CS, DC, RST, BUSY));
//
// then drive partials exactly as with stock GxEPD2 (setPartialWindow +
// firstPage/nextPage, or drawImage). Full refreshes use the stock OTP path.

#ifndef GxEPD2_420_FAST_LUT_GDEY042T81_H
#define GxEPD2_420_FAST_LUT_GDEY042T81_H

#include <GxEPD2_BW.h>

class GxEPD2_420_FAST_LUT_GDEY042T81 : public GxEPD2_420_GDEY042T81 {
public:
  using GxEPD2_420_GDEY042T81::GxEPD2_420_GDEY042T81;

  // 0x22 partial update byte: 0xDC = load_LUT + bw + display, NO load_temp.
  uint8_t modeByte = 0xDC;

  // Full refresh stays on the stock OTP path; it overwrites our LUT, so mark
  // dirty to force a reload before the next partial.
  void refresh(bool partial_update_mode = false) override {
    if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
    else {
      GxEPD2_420_GDEY042T81::refresh(false);
      _lut_loaded = false;
    }
  }

  void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override {
    if (_initial_refresh) {                 // first frame must be a full refresh
      GxEPD2_420_GDEY042T81::refresh(false);
      _lut_loaded = false;
      return;
    }
    if (!_lut_loaded) { _loadFastPartialLUT(); _lut_loaded = true; }

    // Clip to screen + byte-align x/w (mirrors the parent's refresh()).
    int16_t w1 = x < 0 ? w + x : w;
    int16_t h1 = y < 0 ? h + y : h;
    int16_t x1 = x < 0 ? 0 : x;
    int16_t y1 = y < 0 ? 0 : y;
    w1 = x1 + w1 < int16_t(WIDTH)  ? w1 : int16_t(WIDTH)  - x1;
    h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;
    if ((w1 <= 0) || (h1 <= 0)) return;
    w1 += x1 % 8;
    if (w1 % 8 > 0) w1 += 8 - w1 % 8;
    x1 -= x1 % 8;

    _setPartialRamAreaFL(x1, y1, w1, h1);

    _writeCommand(0x21); _writeData(0x00); _writeData(0x00); // RED normal, single chip
    _writeCommand(0x22); _writeData(modeByte);               // 0xDC: load our LUT + display
    _writeCommand(0x20);
    _waitWhileBusy("_Update_Part_FastLUT", partial_refresh_time);
    _power_is_on = true;
  }

#if defined(ESP32) || defined(ESP8266)
  // ---- Bulk-SPI image writes (ESP only) ----------------------------------
  // Stock GxEPD2 ships image data to RAM one byte per SPI.transfer(). These
  // assemble each row and push it as a single SPI.writeBytes() block write,
  // cutting the per-partial data-transfer overhead (most visible on large
  // update regions; e.g. a full-screen-ish partial ~557 -> ~528 ms at 20 MHz).
  // Windowing mirrors the parent's _writeImage exactly; only the inner per-byte
  // loop is bulked. The 0x26 "previous" bank is still kept in sync (clean image).
  // Covers the common firstPage/nextPage single-page path; writeImagePart
  // (displayWindow / low-RAM multi-page) falls back to the stock per-byte path.
  void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                  bool invert = false, bool mirror_y = false, bool pgm = false) override {
    _writeImageBulk(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
  void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert = false, bool mirror_y = false, bool pgm = false) override {
    _writeImageBulk(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm); // previous
    _writeImageBulk(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm); // current
  }
#endif

private:
  bool _lut_loaded = false;

#if defined(ESP32) || defined(ESP8266)
  // Bulk version of the parent's _writeImage: same clipping/index math, but one
  // SPI.writeBytes() per row instead of per-byte. Assumes the panel is already
  // initialised (true once the first full refresh has run, which our refresh()
  // forces), so it omits the parent's init/initial-clear guards.
  void _writeImageBulk(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y,
                       int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm) {
    int16_t wb = (w + 7) / 8;
    x -= x % 8;
    w = wb * 8;
    int16_t x1 = x < 0 ? 0 : x;
    int16_t y1 = y < 0 ? 0 : y;
    int16_t w1 = x + w < int16_t(WIDTH)  ? w : int16_t(WIDTH)  - x;
    int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
    int16_t dx = x1 - x, dy = y1 - y;
    w1 -= dx; h1 -= dy;
    if ((w1 <= 0) || (h1 <= 0)) return;
    _setPartialRamAreaFL(x1, y1, w1, h1);
    _writeCommand(command);
    _startTransfer();                          // beginTransaction (writeBytes uses block path) + CS low
    const int16_t nb = w1 / 8;
    uint8_t row[(WIDTH + 7) / 8];              // <= 50 bytes
    for (int16_t i = 0; i < h1; i++) {
      for (int16_t j = 0; j < nb; j++) {
        int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb
                               : j + dx / 8 + (i + dy) * wb;
        uint8_t data = pgm ? pgm_read_byte(&bitmap[idx]) : bitmap[idx];
        row[j] = invert ? ~data : data;
      }
      _pSPIx->writeBytes(row, nb);             // one block burst per row
    }
    _endTransfer();                            // CS high + endTransaction
  }
#endif

  static const uint8_t FRAMES   = 24;   // per-phase TP (inert: does not change timing here)
  static const uint8_t GROUP_RP = 1;    // group-0 repeat
  static const uint8_t FR_BYTE  = 0x02; // frame-rate code at LUT[224] (also inert here)

  // Parent's _setPartialRamArea is private; replicate it verbatim.
  void _setPartialRamAreaFL(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    _writeCommand(0x11); _writeData(0x03);
    _writeCommand(0x44); _writeData(x / 8); _writeData((x + w - 1) / 8);
    _writeCommand(0x45); _writeData(y % 256); _writeData(y / 256);
                         _writeData((y + h - 1) % 256); _writeData((y + h - 1) / 256);
    _writeCommand(0x4e); _writeData(x / 8);
    _writeCommand(0x4f); _writeData(y % 256); _writeData(y / 256);
  }

  void _loadFastPartialLUT() {
    uint8_t lut[227];
    for (size_t i = 0; i < sizeof(lut); i++) lut[i] = 0;

    // Booster soft-start (values from GxEPD2_4G for this panel).
    _writeCommand(0x0C); _writeData(0x8B); _writeData(0x9C); _writeData(0xA4); _writeData(0x0F);

    const uint8_t VS_VCOM = 0, VS_BLACK = 1, VS_WHITE = 2;
    auto drive = [&](int off, uint8_t vs) {                  // group 0, phase A
      lut[off]     = GROUP_RP;
      lut[off + 1] = (uint8_t)((vs << 6) | (FRAMES & 0x3F)); // VS | TP
    };
    drive(0,   VS_VCOM);   // LUTC
    drive(42,  VS_WHITE);  // LUTWW : current White -> drive white
    drive(84,  VS_BLACK);  // LUTBW : current Black -> drive black
    drive(126, VS_WHITE);  // LUTWB : current White -> drive white
    drive(168, VS_BLACK);  // LUTBB : current Black -> drive black
    lut[224] = FR_BYTE;    // frame rate

    _writeCommand(0x32);
    for (size_t i = 0; i < sizeof(lut); i++) _writeData(lut[i]);

    // Companion waveform registers (datasheet 6.7: bytes 227..232), the panel's
    // real drive voltages from GxEPD2_4G.
    _writeCommand(0x3F); _writeData(0x22);                       // EOPT: 2-frame discharge
    _writeCommand(0x03); _writeData(0x17);                       // VGH
    _writeCommand(0x04); _writeData(0x41); _writeData(0xA8); _writeData(0x32); // VSH1,VSH2,VSL
    _writeCommand(0x2C); _writeData(0x30);                       // VCOM
  }
};

#endif // GxEPD2_420_FAST_LUT_GDEY042T81_H
