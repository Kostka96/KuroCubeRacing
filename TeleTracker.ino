#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// --- ВАЖНО: Создаем спрайт ГЛОБАЛЬНО, чтобы избежать фрагментации памяти ---
TFT_eSprite canvas = TFT_eSprite(&tft);

Adafruit_MPU6050 mpu;

#define CORRECTION_ANGLE +88 
int rpm = 0;

void setup() {
  // Настраиваем Serial для диагностики
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Устанавливаем точку поворота в центр экрана
  tft.setPivot(120, 120);
  
  // --- СОЗДАЕМ СПРАЙТ ОДИН РАЗ В SETUP ---
  // Используем 4-битную глубину цвета (16 цветов), чтобы занять в 2 раза меньше памяти!
  canvas.setColorDepth(4);
  
  // Создаем сам объект спрайта в памяти
  if (!canvas.createSprite(240, 240)) {
    // Если память кончилась, печатаем ошибку и входим в бесконечный цикл
    Serial.println("ОШИБКА: Не хватило памяти для спрайта!");
    while(1) { delay(1000); }
  }
  
  canvas.setPivot(120, 120);

  if (!mpu.begin()) { 
    Serial.println("No MPU"); 
  }

  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
  
  Serial.println("Инициализация прошла успешно");
}

void loop() {
  // Проверяем гироскоп
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  float rawAngle = atan2(a.acceleration.x, a.acceleration.y) * 180.0 / PI;
  float displayAngle = rawAngle + CORRECTION_ANGLE;
  
  // Рисуем всё во внутреннем спрайте
  canvas.fillSprite(TFT_BLACK);
  
  // Геометрия
  canvas.drawSmoothArc(120, 120, 118, 114, 220, 140, TFT_DARKGREY, TFT_BLACK);
  int rpmAngle = map(rpm, 0, 8000, 220, 500);
  canvas.drawSmoothArc(120, 120, 118, 110, 220, rpmAngle, TFT_WHITE, TFT_BLACK);
  canvas.drawFastHLine(40, 160, 160, TFT_WHITE);
  
  // Текст 
  canvas.setTextColor(TFT_WHITE, TFT_BLACK); // Указываем цвет фона для текста
  canvas.drawString(String(rpm), 100, 130, 4);
  
  // Самая быстрая отправка на дисплей
  canvas.pushRotated(displayAngle);
  
  // Логика без delay
  rpm += 100;
  if (rpm > 8000) rpm = 0;

  // --- СБРОС СТОРОЖЕВОГО ТАЙМЕРА (очень важно!) ---
  yield(); // Даем системе обработать фоновые задачи и сбрасываем таймер
}
