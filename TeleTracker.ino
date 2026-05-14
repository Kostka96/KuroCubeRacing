#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
Adafruit_MPU6050 mpu;

#define CORRECTION_ANGLE 88 
// Уменьшаем до 180. Это даст огромный прирост FPS.
#define S_SIZE 180 
#define S_CENTER (S_SIZE / 2)

int rpm = 0;
unsigned long lastTime = 0;
float fps = 0;

void setup() {
  Serial.begin(115200);
  
  Wire.begin(21, 22);
  Wire.setClock(400000);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  // Точка поворота на самом экране ВСЕГДА 120, 120 (центр дисплея)
  tft.setPivot(120, 120); 
  
  canvas.setColorDepth(16); 
  if (!canvas.createSprite(S_SIZE, S_SIZE)) {
    while(1);
  }
  // Точка поворота внутри спрайта - его собственный центр
  canvas.setPivot(S_CENTER, S_CENTER);

  if (!mpu.begin()) { Serial.println("No MPU"); }
  mpu.setFilterBandwidth(MPU6050_BAND_94_HZ);
}

void loop() {
  // Считаем FPS
  unsigned long currentTime = millis();
  float currentFrameTime = (currentTime - lastTime);
  lastTime = currentTime;
  if (currentFrameTime > 0) fps = (fps * 0.9) + (1000.0 / currentFrameTime * 0.1);

  // Читаем датчик
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Быстрая математика
  float rawAngle = atan2(a.acceleration.x, a.acceleration.y) * 57.29578; 
  float displayAngle = rawAngle + CORRECTION_ANGLE;
  
  canvas.fillSprite(TFT_BLACK);
  
  // Рисуем дуги чуть меньшего радиуса, чтобы влезли в 180х180
  // drawSmoothArc(x, y, R_out, R_in, start, end, color, back_color)
  canvas.drawSmoothArc(S_CENTER, S_CENTER, 88, 84, 220, 140, TFT_DARKGREY, TFT_BLACK);
  
  int rpmAngle = map(rpm, 0, 8000, 220, 500);
  canvas.drawSmoothArc(S_CENTER, S_CENTER, 88, 80, 220, rpmAngle, TFT_WHITE, TFT_BLACK);
  
  // Линия
  canvas.drawFastHLine(S_CENTER - 50, S_CENTER + 20, 100, TFT_WHITE);
  
  // Данные (используем drawNumber для скорости)
  canvas.setTextColor(TFT_WHITE);
  canvas.drawNumber(rpm, S_CENTER - 25, S_CENTER - 10, 4);
  
  // Вывод FPS
  canvas.setTextColor(TFT_WHITE);
  canvas.drawNumber((int)fps, S_CENTER - 10, 10, 2);
  
  // ПОВОРОТ И ОТПРАВКА
  // Теперь процессор крутит на 25% меньше пикселей!
  canvas.pushRotated(displayAngle);
  
  rpm += 150;
  if (rpm > 8000) rpm = 0;
}
