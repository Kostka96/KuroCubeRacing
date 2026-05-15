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
volatile int sharedSpeed = 0;
volatile int sharedGear = 0;
volatile int sharedGas = 0;   // 0-100
volatile int sharedBrake = 0; // 0-100
volatile int sharedClutch = 0; // 0-100

float sharedAngle = 0; 
float fps = 0;

void setup() {
  Serial.begin(115200);

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
    
    int r = sharedRpm;
    int s = sharedSpeed;
    int g = sharedGear;

    canvas.setTextColor(TFT_WHITE);

    // 1. Шкала RPM (верхняя дуга)
    String rStr = String(r);
    int twr = canvas.textWidth(rStr, 4);
    canvas.drawString(rStr, S_CENTER - twr / 2, S_CENTER - 14, 4);

    // Фоновая дуга: от 5ч до 7ч через верх
    canvas.drawSmoothArc(S_CENTER, S_CENTER, 82, 78, 30, 330, TFT_DARKGREY, TFT_BLACK);

    // Активная дуга RPM
    int rpmAngle = map(r, 0, 8000, 30, 330);
    canvas.drawSmoothArc(S_CENTER, S_CENTER, 82, 74, 30, rpmAngle, TFT_WHITE, TFT_BLACK);

    
    // 2. Передача (Крупно в центре)
    String gStr = (g == 0) ? "N" : String(g);
    int tgw = canvas.textWidth(gStr, 4);
    canvas.drawString(gStr, S_CENTER - tgw / 2, S_CENTER + 10, 4);

    // 3. Скорость 
    // Автоцентровка через textWidth
    String sStr = String(s);
    int tw = canvas.textWidth(sStr, 6);
    canvas.drawString(sStr, S_CENTER - tw / 2, S_CENTER - 55, 6);
    //canvas.drawString("km/h", S_CENTER - 20, S_CENTER - 14, 4);

    // 4. Педали (3 вертикальных ползунка внизу)
    // Газ (Зеленый)
    int p_y = S_CENTER + 40; // Начало по Y
    canvas.drawRect(S_CENTER - 30, p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER - 30, p_y + (30 - map(sharedGas, 0, 100, 0, 30)), 10, map(sharedGas, 0, 100, 0, 30), TFT_GREEN);
    
    // Тормоз (Красный)
    canvas.drawRect(S_CENTER - 5, p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER - 5, p_y + (30 - map(sharedBrake, 0, 100, 0, 30)), 10, map(sharedBrake, 0, 100, 0, 30), TFT_RED);

    // Сцепление (Синий/Голубой)
    canvas.drawRect(S_CENTER + 20, p_y, 10, 30, TFT_DARKGREY);
    canvas.fillRect(S_CENTER + 20, p_y + (30 - map(sharedClutch, 0, 100, 0, 30)), 10, map(sharedClutch, 0, 100, 0, 30), TFT_SKYBLUE);

    // Только передний план, фон не трогается — по сути прозрачность
    canvas.drawBitmap(S_CENTER - 70, S_CENTER - 16, epd_bitmap_Dice6_32x32, 32, 32, TFT_WHITE);
    canvas.pushImage(S_CENTER + 35, S_CENTER - 17, 40, 34, epd_bitmap_cat_40x34);

    canvas.pushRotated(sharedAngle);
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}
// --- ЯДРО 1: ТОЛЬКО СЕТЬ ---
void loop() {
  // 1. Обработка UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = 0;
      // Парсим CSV: rpm,speed,gear,gas,brake,clutch
      char* ptr = strtok(incomingPacket, ",");
      if (ptr) sharedRpm = atoi(ptr);
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
  // Даем время фоновым процессам Wi-Fi
  yield();
  delay(5); 
}
