#include <WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "config.h"

WiFiUDP udp;
char incomingPacket[255];

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
Adafruit_MPU6050 mpu;

// Общие переменные
volatile int sharedRpm = 0;
float sharedAngle = 0; 
float fps = 0;

void setup() {
  Serial.begin(115200);

  // 1. Сначала графика (резервируем память)
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setPivot(120, 120);
  canvas.setColorDepth(16); // Раз 16 бит летает — оставляем их!
  canvas.createSprite(S_SIZE, S_SIZE);
  canvas.setPivot(S_CENTER, S_CENTER);

  // 2. Инициализация I2C для MPU
  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.setTimeOut(10); // Защита от зависания шины

  mpu.begin();
  mpu.setFilterBandwidth(MPU6050_BAND_94_HZ);

  // 3. Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  udp.begin(localUdpPort);

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
    
    // Рисуем дуги
    canvas.drawSmoothArc(S_CENTER, S_CENTER, 88, 84, 220, 140, TFT_DARKGREY, TFT_BLACK);
    int r = sharedRpm;
    int rpmAngle = map(r, 0, 8000, 220, 500);
    canvas.drawSmoothArc(S_CENTER, S_CENTER, 88, 80, 220, rpmAngle, TFT_WHITE, TFT_BLACK);
    
    // Линия горизонта
    canvas.drawFastHLine(S_CENTER - 50, S_CENTER + 20, 100, TFT_WHITE);
    
    // Текст
    canvas.setTextColor(TFT_WHITE);
    canvas.drawNumber(r, S_CENTER - 25, S_CENTER - 10, 4);
    canvas.drawNumber((int)fps, S_CENTER - 10, 10, 2);
    
    // Вывод на экран
    canvas.pushRotated(sharedAngle);
    
    // Важнейшая задержка для стабильности
    vTaskDelay(2 / portTICK_PERIOD_MS); 
  }
}

// --- ЯДРО 1: ТОЛЬКО СЕТЬ ---
void loop() {
  // 1. Обработка UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, 255);
    if (len > 0) incomingPacket[len] = 0;
    // Предполагаем, что пришло просто число "6500"
    sharedRpm = atoi(incomingPacket); 
  }
  // Даем время фоновым процессам Wi-Fi
  yield();
  delay(5); 
}
