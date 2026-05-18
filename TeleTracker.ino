#include <WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "settings.h"

WiFiUDP udp;
char incomingPacket[255];

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
Adafruit_MPU6050 mpu;

// Общие переменные
volatile int sharedRpm = 0;
volatile int sharedSpeed = 0;
volatile int sharedGear = 0;
volatile int sharedGas = 0;   // 0-100
volatile int sharedBrake = 0; // 0-100
volatile int sharedClutch = 0; // 0-100
volatile int sharedRpmMax = 8000;

float sharedAngle = 0; 
float fps = 0;

void setup() {
  Serial.begin(115200);
  settings_load();

  const int btnPins[] = {BTN_0,BTN_1,BTN_2,BTN_3,BTN_4,BTN_5,BTN_6,BTN_7,BTN_8,BTN_9};
  for (int i = 0; i < 10; i++)
    pinMode(btnPins[i], INPUT_PULLUP);

  // 1. Сначала графика (резервируем память)
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setPivot(120, 120);
  canvas.setColorDepth(16);
  canvas.createSprite(S_SIZE, S_SIZE);
  canvas.setPivot(S_CENTER, S_CENTER);

  // 2. Инициализация I2C для MPU
  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.setTimeOut(10); // Защита от зависания шины

  mpu.begin();
  mpu.setFilterBandwidth(MPU6050_BAND_94_HZ);

  // 3. Wi-Fi
  Serial.printf("Подключаюсь к: %s\n", cfg_ssid);
  WiFi.begin(cfg_ssid, cfg_password);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 10000) {
      Serial.println("\nWiFi timeout!");
      Serial.printf("SSID: '%s'\n", cfg_ssid);
      Serial.printf("Pass: '%s'\n", cfg_password);
      Serial.println("Используй: set ssid / set password / reboot");
      break;
    }
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
    udp.begin(cfg_udp_port);
  }

  // 4. Запуск задачи на Ядре 0
  xTaskCreatePinnedToCore(
    coreTaskZero,   // Функция
    "TaskZero",     // Название
    8192,           // Стек
    NULL,           // Параметры
    1,              // Приоритет
    NULL,           // Хендл
    0               // Ядро 0
  );
}

void handleSerial() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  // Парсим: команда значение
  int sep = line.indexOf(' ');
  String cmd = (sep == -1) ? line : line.substring(0, sep);
  String val = (sep == -1) ? "" : line.substring(sep + 1);
  cmd.toLowerCase();

  if (cmd == "help") {
    Serial.println("=== TeleTracking CLI ===");
    Serial.println("set ssid <name>       — WiFi сеть");
    Serial.println("set password <pass>   — WiFi пароль");
    Serial.println("set ip <x.x.x.x>      — IP компьютера");
    Serial.println("set udpport <port>     — порт приёма (default 4210)");
    Serial.println("set hostport <port>    — порт отправки (default 4211)");
    Serial.println("show                   — текущие настройки");
    Serial.println("reset                  — сброс к дефолтам");
    Serial.println("reboot                 — перезагрузка");

  } else if (cmd == "show") {
    Serial.println("=== Текущие настройки ===");
    Serial.printf("ssid:     %s\n", cfg_ssid);
    Serial.printf("password: %s\n", cfg_password);
    Serial.printf("host_ip:  %s\n", cfg_host_ip);
    Serial.printf("udpport:  %d\n", cfg_udp_port);
    Serial.printf("hostport: %d\n", cfg_host_port);
    Serial.printf("wifi:     %s\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "не подключен");

  } else if (cmd == "set") {
    int sep2 = val.indexOf(' ');
    if (sep2 == -1) { Serial.println("Ошибка: нет значения"); return; }
    String key = val.substring(0, sep2);
    String v   = val.substring(sep2 + 1);
    key.toLowerCase();

    if (key == "ssid")          { settings_save_ssid(v.c_str());     Serial.println("OK — перезагрузи для применения"); }
    else if (key == "password") { settings_save_password(v.c_str()); Serial.println("OK — перезагрузи для применения"); }
    else if (key == "ip")       { settings_save_host_ip(v.c_str());  Serial.println("OK"); }
    else if (key == "udpport")  { settings_save_port("udp_port",  v.toInt(), &cfg_udp_port);  Serial.println("OK"); }
    else if (key == "hostport") { settings_save_port("host_port", v.toInt(), &cfg_host_port); Serial.println("OK"); }
    else Serial.println("Неизвестный параметр. Введи help.");

  } else if (cmd == "reset") {
    settings_reset();
    Serial.println("Сброшено к дефолтам — перезагрузи");

  } else if (cmd == "reboot") {
    Serial.println("Перезагрузка...");
    delay(500);
    ESP.restart();

  } else {
    Serial.println("Неизвестная команда. Введи help.");
  }
}
// --- ЗАДАЧА НА ЯДРЕ 0: ДАТЧИК + ГРАФИКА ---
void coreTaskZero(void * pvParameters) {
  unsigned long lastTime = 0;
  sensors_event_t a, g, temp;

  for(;;) {
    // А) Опрос MPU6050
    if (mpu.getEvent(&a, &g, &temp)) {
      float rawAngle = atan2(a.acceleration.x, a.acceleration.y) * 57.29578;
      sharedAngle = rawAngle + CORRECTION_ANGLE;
    }

    // Б) Отрисовка
    unsigned long currentTime = millis();
    if (currentTime - lastTime > 0) 
      fps = (fps * 0.9) + (1000.0 / (currentTime - lastTime) * 0.1);
    lastTime = currentTime;

    canvas.fillSprite(TFT_BLACK);

    int r  = sharedRpm;
    int rm = sharedRpmMax;
    int s  = sharedSpeed;
    int g  = sharedGear;

    canvas.setTextColor(TFT_WHITE);

    // 1. Шкала RPM — фоновая дуга
    canvas.drawSmoothArc(S_CENTER, S_CENTER, 108, 104, 30, 330, TFT_DARKGREY, TFT_BLACK);

    if (rm > 0) {
        // Отметка shift point на дуге (85% от max) — голубая засечка
        int shiftMarkAngle = map((int)(rm * 0.85f), 0, rm, 30, 330);
        canvas.drawSmoothArc(S_CENTER, S_CENTER, 108, 100, shiftMarkAngle, shiftMarkAngle + 6, TFT_CYAN, TFT_BLACK);
    }

    if (rm > 0 && r > 0) {
        float pct    = (float)r / rm;
        int endAngle = constrain(map(r, 0, rm, 30, 330), 30, 330);

        // Цвет дуги
        uint16_t color;
        if      (pct < 0.70f) color = TFT_WHITE;
        else if (pct < 0.85f) color = TFT_YELLOW;
        else if (pct < 0.95f) color = 0xFBE0;   // оранжевый
        else                  color = TFT_RED;

        // Мигание на redline
        bool draw = true;
        if (pct >= 0.95f) {
            static bool blink = false;
            static unsigned long lastBlink = 0;
            if (millis() - lastBlink > 100) { blink = !blink; lastBlink = millis(); }
            draw = blink;
        }
        if (draw)
            canvas.drawSmoothArc(S_CENTER, S_CENTER, 108, 100, 30, endAngle, color, TFT_BLACK);

        // Световая полоса shift — две дуги по краям когда пора переключать
        if (pct >= 0.85f) {
            static bool shiftBlink = false;
            static unsigned long lastShiftBlink = 0;
            // Мигание ускоряется по мере приближения к redline
            int blinkInterval = (pct >= 0.95f) ? 60 : 150;
            if (millis() - lastShiftBlink > blinkInterval) {
                shiftBlink = !shiftBlink;
                lastShiftBlink = millis();
            }
            if (shiftBlink) {
                // Левая и правая засечки у концов дуги
                canvas.drawSmoothArc(S_CENTER - 5, S_CENTER, 95, 85, 20,  45,  TFT_CYAN, TFT_BLACK);
                canvas.drawSmoothArc(S_CENTER + 3, S_CENTER, 95, 85, 315, 340, TFT_CYAN, TFT_BLACK);
            }
        }
    }

    // 2. RPM цифры
    String rStr = String(r);
    int twr = canvas.textWidth(rStr, 4);
    canvas.drawString(rStr, S_CENTER - twr / 2, S_CENTER + 10, 4);

    // 3. Передача
    String gStr = (g == 0) ? "N" : (g == -1) ? "R" : String(g);
    int tgw = canvas.textWidth(gStr, 4);
    canvas.drawString(gStr, S_CENTER - tgw / 2, S_CENTER + 32, 4);

    // 4. Скорость
    String sStr = String(s);
    int tw = canvas.textWidth(sStr, 8);
    canvas.drawString(sStr, S_CENTER - tw / 2, S_CENTER - 70, 8);

    // 5. Педали
    int p_y = S_CENTER + 60;
    canvas.drawRect(S_CENTER - 30, p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER - 30, p_y + (30 - map(sharedGas,    0, 100, 0, 30)), 10, map(sharedGas,    0, 100, 0, 30), TFT_GREEN);
    canvas.drawRect(S_CENTER - 5,  p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER - 5,  p_y + (30 - map(sharedBrake,  0, 100, 0, 30)), 10, map(sharedBrake,  0, 100, 0, 30), TFT_RED);
    canvas.drawRect(S_CENTER + 20, p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER + 20, p_y + (30 - map(sharedClutch, 0, 100, 0, 30)), 10, map(sharedClutch, 0, 100, 0, 30), TFT_SKYBLUE);

    // 6. Логотипы
    canvas.drawBitmap(S_CENTER - 73, S_CENTER + 18, epd_bitmap_Dice6_32x32, 32, 32, TFT_WHITE);
    canvas.pushImage(S_CENTER + 40, S_CENTER + 17, 40, 34, epd_bitmap_cat_40x34);

    canvas.pushRotated(sharedAngle);
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}
// --- ЯДРО 1: ТОЛЬКО СЕТЬ ---
void loop() {
  handleSerial();
  if (WiFi.status() != WL_CONNECTED) {
    delay(5);
    return; // пропускаем UDP если нет сети
  }
  // 1. Обработка UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = 0;
      // Парсим CSV: rpm,RpmMax,speed,gear,gas,brake,clutch
      char* ptr = strtok(incomingPacket, ",");
      if (ptr) sharedRpm = atoi(ptr);
      ptr = strtok(NULL, ",");
      if (ptr) sharedRpmMax = atoi(ptr);  // ← новое поле
      ptr = strtok(NULL, ",");
      if (ptr) sharedSpeed = atoi(ptr);
      ptr = strtok(NULL, ",");
      if (ptr) sharedGear = atoi(ptr);
      ptr = strtok(NULL, ",");
      if (ptr) sharedGas = atoi(ptr);
      ptr = strtok(NULL, ",");
      if (ptr) sharedBrake = atoi(ptr);
      ptr = strtok(NULL, ",");
      if (ptr) sharedClutch = atoi(ptr);
    }
  }
  struct ButtonPacket {
  uint16_t buttons; // 10 кнопок, запас до 16
  };
  const int btnPins[] = {BTN_0,BTN_1,BTN_2,BTN_3,BTN_4,BTN_5,BTN_6,BTN_7,BTN_8,BTN_9};

  ButtonPacket pkt = {0};

  for (int i = 0; i < 10; i++)
    if (!digitalRead(btnPins[i]))
      pkt.buttons |= (1 << i);

  udp.beginPacket(DEFAULT_HOST_IP, DEFAULT_HOST_PORT);
  udp.write((uint8_t*)&pkt, sizeof(pkt));
  udp.endPacket();
  // Даем время фоновым процессам Wi-Fi
  yield();
  delay(5); 
}
