#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <XPT2046_Touchscreen.h>

const char* WIFI_SSID = "wifi2";
const char* WIFI_PASSWORD = "justusedata";
const uint16_t UDP_PORT = 9000;
const IPAddress PC_IP(192, 168, 1, 100); // Set this to the PC's LAN IP.
const uint16_t PC_COMMAND_PORT = 9001;
const int TOUCH_CS_PIN = 33;
const int TOUCH_IRQ_PIN = 36;
const int TOUCH_CLK_PIN = 25;
const int TOUCH_MISO_PIN = 39;
const int TOUCH_MOSI_PIN = 32;

const uint16_t DISPLAY_WIDTH = 320;
const uint16_t DISPLAY_HEIGHT = 240;

TFT_eSPI tft = TFT_eSPI();
WiFiUDP udp;
Preferences preferences;
// Keep touch on the other ESP32 SPI host so polling it cannot disturb TFT SPI.
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

struct TouchCalibrationData {
  int32_t xLeft;
  int32_t xRight;
  int32_t yTop;
  int32_t yBottom;
  bool swapXY;
};
TouchCalibrationData touchCalibration{};

struct Telemetry {
  long rpm = 0;
  int gear = 0;
  float speed = 0.0f;
  float lapTime = 0.0f;
  bool valid = false;
};

Telemetry telemetry;
unsigned long lastPacketMs = 0;
bool menuVisible = false;
bool touchWasDown = false;
void renderTelemetry();

const char* COMMANDS[] = {
  "pit_limiter", "headlights", "wipers", "ignition", "starter", "reset_car"
};
const char* COMMAND_LABELS[] = {
  "PIT LIMITER", "HEADLIGHTS", "WIPERS", "IGNITION", "STARTER", "RESET CAR"
};

void connectWifi() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());
}

void drawBackground() {
  tft.fillScreen(0x0000);

  // Compact landscape instrument panel: tachometer, gear, speed and lap.
  tft.drawRoundRect(12, 18, 296, 16, 4, 0x7BEF);
  tft.drawFastHLine(12, 178, 296, 0x7BEF);
  tft.drawFastVLine(151, 46, 131, 0x39E7);

  tft.setTextColor(0x7BEF, 0x0000);
  tft.setTextSize(1);
  tft.setCursor(18, 41);
  tft.print("ENGINE RPM");
  tft.setCursor(18, 193);
  tft.print("LAP TIME");
  tft.setCursor(168, 66);
  tft.print("SPEED");
}

void drawDashboard() {
  menuVisible = false;
  drawBackground();
  renderTelemetry();
}

void drawConnectingSplash() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("Connecting...", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
  tft.setTextDatum(TL_DATUM);
}

void drawMenu() {
  menuVisible = true;
  tft.fillScreen(0x0000);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(2);
  tft.setCursor(12, 9);
  tft.print("CONTROLS");
  tft.drawRoundRect(254, 5, 54, 27, 4, 0x7BEF);
  tft.setTextSize(1);
  tft.setCursor(267, 15);
  tft.print("BACK");

  const int buttonX[] = {10, 165};
  const int buttonY[] = {42, 107, 172};
  for (int i = 0; i < 6; ++i) {
    const int x = buttonX[i % 2];
    const int y = buttonY[i / 2];
    tft.drawRoundRect(x, y, 145, 55, 6, 0x39E7);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(x + 8, y + 21);
    tft.print(COMMAND_LABELS[i]);
  }
}

void sendCommand(const char* command) {
  JsonDocument doc;
  doc["type"] = "simhub_command";
  doc["command"] = command;
  char packet[128];
  const size_t length = serializeJson(doc, packet, sizeof(packet));
  if (udp.beginPacket(PC_IP, PC_COMMAND_PORT)) {
    udp.write(reinterpret_cast<const uint8_t*>(packet), length);
    udp.endPacket();
    Serial.printf("Sent command: %s\n", command);
  } else {
    Serial.printf("Could not send command: %s\n", command);
  }
}

void handleTouch(uint16_t x, uint16_t y) {
  if (!menuVisible) {
    drawMenu();
    return;
  }

  if (x >= 254 && y < 34) {
    drawDashboard();
    return;
  }

  const int column = x < 160 ? 0 : 1;
  const int row = y < 100 ? 0 : y < 165 ? 1 : 2;
  const int index = row * 2 + column;
  const int buttonX = column == 0 ? 10 : 165;
  const int buttonY = 42 + row * 65;
  if (index >= 0 && index < 6 &&
      x >= buttonX && x < buttonX + 145 && y >= buttonY && y < buttonY + 55) {
    sendCommand(COMMANDS[index]);
    delay(150);
    drawDashboard();
  }
}

void collectTouchPoint(int32_t& x, int32_t& y) {
  while (!touch.touched()) delay(10);
  int32_t xTotal = 0;
  int32_t yTotal = 0;
  int samples = 0;
  while (touch.touched() && samples < 20) {
    const TS_Point point = touch.getPoint();
    if (point.z > 200) {
      xTotal += point.x;
      yTotal += point.y;
      ++samples;
    }
    delay(5);
  }
  while (touch.touched()) delay(10);
  x = xTotal / max(samples, 1);
  y = yTotal / max(samples, 1);
  delay(200);
}

void calibrateTouch() {
  const int screenX[] = {20, 300, 300, 20};
  const int screenY[] = {20, 20, 220, 220};
  int32_t rawX[4], rawY[4];
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(28, 12);
  tft.print("TOUCH CALIBRATION");
  tft.setTextSize(1);
  tft.setCursor(23, 34);
  tft.print("Tap and hold each crosshair");

  for (int i = 0; i < 4; ++i) {
    tft.fillRect(0, 52, 320, 20, TFT_BLACK);
    tft.setCursor(23, 58);
    tft.printf("Touch point %d of 4", i + 1);
    tft.drawFastHLine(screenX[i] - 10, screenY[i], 21, TFT_GREEN);
    tft.drawFastVLine(screenX[i], screenY[i] - 10, 21, TFT_GREEN);
    collectTouchPoint(rawX[i], rawY[i]);
    tft.fillRect(screenX[i] - 11, screenY[i] - 11, 23, 23, TFT_BLACK);
    tft.fillCircle(screenX[i], screenY[i], 3, TFT_DARKGREY);
  }

  touchCalibration.swapXY = abs(rawY[1] - rawY[0]) > abs(rawX[1] - rawX[0]);
  int32_t u[4], v[4];
  for (int i = 0; i < 4; ++i) {
    u[i] = touchCalibration.swapXY ? rawY[i] : rawX[i];
    v[i] = touchCalibration.swapXY ? rawX[i] : rawY[i];
  }
  touchCalibration.xLeft = (u[0] + u[3]) / 2;
  touchCalibration.xRight = (u[1] + u[2]) / 2;
  touchCalibration.yTop = (v[0] + v[1]) / 2;
  touchCalibration.yBottom = (v[2] + v[3]) / 2;
  preferences.putBytes("xptcal3", &touchCalibration, sizeof(touchCalibration));
}

void beginTouch() {
  touchSPI.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  // Rotation 1 is the raw coordinate space used by the CYD touch controller.
  touch.setRotation(1);

  preferences.begin("cyd", false);
  const bool saved = preferences.getBytesLength("xptcal3") == sizeof(touchCalibration) &&
                     preferences.getBytes("xptcal3", &touchCalibration, sizeof(touchCalibration)) == sizeof(touchCalibration) &&
                     abs(touchCalibration.xRight - touchCalibration.xLeft) > 300 &&
                     abs(touchCalibration.yBottom - touchCalibration.yTop) > 300;
  if (!saved) {
    calibrateTouch();
  }
  preferences.end();
}

bool readTouch(uint16_t& x, uint16_t& y) {
  if (!touch.touched()) return false;
  const TS_Point point = touch.getPoint();
  const int32_t u = touchCalibration.swapXY ? point.y : point.x;
  const int32_t v = touchCalibration.swapXY ? point.x : point.y;
  const int32_t screenX = (u - touchCalibration.xLeft) * (DISPLAY_WIDTH - 1) /
                          (touchCalibration.xRight - touchCalibration.xLeft);
  const int32_t screenY = (v - touchCalibration.yTop) * (DISPLAY_HEIGHT - 1) /
                          (touchCalibration.yBottom - touchCalibration.yTop);
  x = constrain(screenX, 0, DISPLAY_WIDTH - 1);
  y = constrain(screenY, 0, DISPLAY_HEIGHT - 1);
  return point.z > 200;
}

void drawGauge(long rpm) {
  int gaugePercent = constrain((rpm * 100) / 11000L, 0, 100);
  int gaugeWidth = map(gaugePercent, 0, 100, 0, 292);
  static int previousGaugeWidth = 0;

  if (gaugeWidth > previousGaugeWidth) {
    for (int x = previousGaugeWidth; x < gaugeWidth;) {
      const uint16_t color = x >= 248 ? 0xF800 : x >= 190 ? 0xFBE0 : 0x07E0;
      const int bandEnd = x >= 248 ? gaugeWidth : (x >= 190 ? min(gaugeWidth, 248) : min(gaugeWidth, 190));
      tft.fillRect(14 + x, 20, bandEnd - x, 12, color);
      x = bandEnd;
    }
  } else if (gaugeWidth < previousGaugeWidth) {
    tft.fillRect(14 + gaugeWidth, 20, previousGaugeWidth - gaugeWidth, 12, 0x0000);
  }
  previousGaugeWidth = gaugeWidth;

  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(4);
  tft.setCursor(18, 56);
  tft.printf("%5ld", rpm);
}

void drawGear(int gear) {
  tft.setTextColor(0xFFE0, 0x0000);
  tft.setTextSize(7);
  tft.setCursor(28, 97);
  if (gear < 0) tft.print("R ");
  else if (gear == 0) tft.print("N ");
  else tft.printf("%2d", gear);

  tft.setTextColor(0x7BEF, 0x0000);
  tft.setTextSize(1);
  tft.setCursor(37, 166);
  tft.print("GEAR");
}

void drawSpeed(float speed) {
  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(5);
  tft.setCursor(168, 89);
  tft.printf("%3d", (int)speed);

  tft.setTextSize(1);
  tft.setCursor(260, 144);
  tft.print("km/h");
}

void drawLap(float lapTime) {
  int minutes = (int)(lapTime / 60.0f);
  int seconds = (int)(lapTime - (minutes * 60.0f));
  int hundredths = (int)((lapTime - ((float)minutes * 60.0f + seconds)) * 100.0f);

  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(3);
  tft.setCursor(118, 194);
  if (lapTime > 0.0f) {
    tft.printf("%02d:%02d.%02d", minutes, seconds, hundredths);
  } else {
    tft.print("--:--.--");
  }
}

void renderTelemetry() {
  drawSpeed(telemetry.speed);
  drawGear(telemetry.gear);
  drawLap(telemetry.lapTime);
  drawGauge(telemetry.rpm);
}

void parsePacket(const char* packet) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, packet);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    telemetry.valid = false;
    return;
  }

  telemetry.rpm = doc["rpm"] | 0L;
  telemetry.gear = doc["gear"] | 0;
  telemetry.speed = doc["speed"] | 0.0f;
  telemetry.lapTime = doc["lapTime"] | doc["lap_time"] | 0.0f;
  telemetry.valid = true;
  lastPacketMs = millis();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting SimHub CYD dashboard...");

  tft.init();
  // Rotation 3 keeps landscape layout and flips it 180 degrees.
  tft.setRotation(3);
  beginTouch();
  drawConnectingSplash();
  connectWifi();
  udp.begin(UDP_PORT);
  Serial.print("Listening for SimHub UDP on port ");
  Serial.println(UDP_PORT);
  drawDashboard();
}

void loop() {
  const int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    static char packet[512];
    const int bytesRead = udp.read(packet, sizeof(packet) - 1);
    if (bytesRead > 0) {
      packet[bytesRead] = '\0';
      parsePacket(packet);
      if (!menuVisible) renderTelemetry();
    }
  }

  if (telemetry.valid && millis() - lastPacketMs > 3000) {
    telemetry.valid = false;
    if (!menuVisible) renderTelemetry();
  }

  uint16_t touchX, touchY;
  const bool touched = readTouch(touchX, touchY);
  if (touched && !touchWasDown) handleTouch(touchX, touchY);
  touchWasDown = touched;
}
