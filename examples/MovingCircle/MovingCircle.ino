// MovingCircle — partial-refresh benchmark for SSD1683-fast-partial-lut.
//
// A filled circle does a random walk around the 400x300 GDEY042T81 (SSD1683)
// panel, 10 px per step. Each step is a fast PARTIAL refresh over just the
// union of the old and new circle; the BUSY time is measured and printed to
// Serial. Every 50 steps a full refresh clears accumulated ghosting.
//
// Expected: ~510 ms per partial with the custom LUT (vs ~1050 ms if you swap
// the type back to the stock GxEPD2_420_GDEY042T81 for comparison).
//
// Example wiring (Waveshare ESP32-S3 Nano; adapt to your board):
//   CS=D10  DC=D2  RST=D3  BUSY=D4 ; SCK=D13  MOSI=D11 ; VCC=3V3  GND=GND

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_420_FAST_LUT_GDEY042T81.h>

#define EPD_CS   10
#define EPD_DC    2
#define EPD_RST   3
#define EPD_BUSY  4

// Swap GxEPD2_420_FAST_LUT_GDEY042T81 <-> GxEPD2_420_GDEY042T81 to A/B the
// custom LUT against the stock ~1050 ms OTP baseline.
GxEPD2_BW<GxEPD2_420_FAST_LUT_GDEY042T81, GxEPD2_420_FAST_LUT_GDEY042T81::HEIGHT>
  display(GxEPD2_420_FAST_LUT_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static const int      RADIUS        = 20;
static const int      STEP          = 10;
static const uint32_t FULL_EVERY    = 50;   // full refresh every N steps
static const uint32_t MOVE_DELAY_MS = 200;

static const int8_t DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static int      cx, cy;
static uint32_t moveCount = 0;

// Align a partial window to an 8-pixel boundary (SSD1683 addresses RAM in byte
// columns): round x down and the right edge up so the rectangle is covered.
static void alignWindow(int &x, int &w) {
  int right = x + w;
  x &= ~7;
  w = (right - x + 7) & ~7;
}

static uint32_t drawMove(int ox, int oy) {
  int left   = max(min(ox, cx) - RADIUS, 0);
  int top    = max(min(oy, cy) - RADIUS, 0);
  int right  = min(max(ox, cx) + RADIUS + 1, (int)display.width());
  int bottom = min(max(oy, cy) + RADIUS + 1, (int)display.height());

  int wx = left, ww = right - left;
  alignWindow(wx, ww);
  if (wx + ww > (int)display.width()) ww = (int)display.width() - wx;
  int wh = bottom - top;

  uint32_t t0 = millis();
  display.setPartialWindow(wx, top, ww, wh);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);                 // erase old circle
    display.fillCircle(cx, cy, RADIUS, GxEPD_BLACK); // draw new circle
  } while (display.nextPage());
  return millis() - t0;
}

static void pickMove(int &nx, int &ny) {
  const int minX = RADIUS, maxX = display.width()  - 1 - RADIUS;
  const int minY = RADIUS, maxY = display.height() - 1 - RADIUS;
  for (int tries = 0; tries < 16; tries++) {
    int d = random(8);
    nx = cx + DX[d] * STEP;
    ny = cy + DY[d] * STEP;
    if (nx >= minX && nx <= maxX && ny >= minY && ny <= maxY) return;
  }
  nx = constrain(cx, minX, maxX);
  ny = constrain(cy, minY, maxY);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== SSD1683 fast-partial moving-circle benchmark ===");

  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);   // park CS HIGH before SPI init
  SPI.begin();

  display.init(115200, true, 2, false);
  display.setRotation(0);
  randomSeed(esp_random());

  cx = display.width()  / 2;
  cy = display.height() / 2;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillCircle(cx, cy, RADIUS, GxEPD_BLACK);
  } while (display.nextPage());

  Serial.printf("Start at (%d, %d). r=%d, step=%d px.\n", cx, cy, RADIUS, STEP);
}

void loop() {
  int ox = cx, oy = cy;
  pickMove(cx, cy);
  moveCount++;

  if (moveCount % FULL_EVERY == 0) {
    uint32_t t0 = millis();
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.fillCircle(cx, cy, RADIUS, GxEPD_BLACK);
    } while (display.nextPage());
    Serial.printf("Move %lu: FULL refresh %lu ms\n",
                  (unsigned long)moveCount, (unsigned long)(millis() - t0));
  } else {
    uint32_t dt = drawMove(ox, oy);
    Serial.printf("Move %lu: partial %lu ms -> (%d, %d)\n",
                  (unsigned long)moveCount, (unsigned long)dt, cx, cy);
  }

  if (MOVE_DELAY_MS) delay(MOVE_DELAY_MS);
}
