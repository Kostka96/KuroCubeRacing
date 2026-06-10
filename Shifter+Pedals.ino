#include <Joystick.h>
#include <HX711.h>

#define PIN_DATA 3
#define PIN_CLOCK 2

#define PIN_THROTTLE A0
#define PIN_CLUTCH   A1

// === ТВОИ ПРОВЕРЕННЫЕ КАЛИБРОВОЧНЫЕ ЗНАЧЕНИЯ ===
#define THROTTLE_MIN 566
#define THROTTLE_MAX 720

#define CLUTCH_MIN 566
#define CLUTCH_MAX 860

#define BRAKE_MAX      491564   // Твой пиковый прожатый максимум
#define BRAKE_DEADZONE  15000   // Мертвая зона, чтобы отсечь шум покоя

bool buttonState[6] = {0,0,0,0,0,0};
HX711 brakeSensor;

// Инициализируем игровой контроллер (6 кнопок, оси Rx, Ry, Rz включены)
Joystick_ GameController(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
  6, 0,                  
  false, false, false,   
  true, true, true,      // Rx, Ry, Rz включены
  false, false,          
  false, false, false);

void setup() {
  Serial.begin(115200);
  GameController.begin();
  
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);

  // Жестко задаем диапазоны осей 0..1023 для Windows joy.cpl
  GameController.setRxAxisRange(0, 1023); // Газ
  GameController.setRyAxisRange(0, 1023); // Тормоз
  GameController.setRzAxisRange(0, 1023); // Сцепление

  // Инициализация тензодатчика
  brakeSensor.begin(PIN_DATA, PIN_CLOCK);
  brakeSensor.set_scale(1.0); // Работаем с чистыми значениями get_value()
  delay(500);
  if (brakeSensor.is_ready()) {
    brakeSensor.tare(); // Сбрасываем тормоз в ноль при старте
  }
}

// Плавное чтение аналоговой оси с защитой от наводок и float-масштабированием
int readAnalogAxis(int pin, int minVal, int maxVal) {
  analogRead(pin); // Холостой замер для сброса остаточного заряда АЦП
  delayMicroseconds(50);

  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(pin);
    delayMicroseconds(10);
  }
  int raw = sum / 16;

  // Считаем точный процент нажатия (0.0 -- 1.0)
  float percentage = (float)(raw - minVal) / (float)(maxVal - minVal);
  if (percentage < 0.0f) percentage = 0.0f;
  if (percentage > 1.0f) percentage = 1.0f;

  // Переводим в стандартный диапазон джойстика
  return (int)(percentage * 1023.0f);
}

void loop() {
  // 1. Чтение Газа и Сцепления (0 .. 1023)
  int throttleVal = readAnalogAxis(PIN_THROTTLE, THROTTLE_MIN, THROTTLE_MAX);
  int clutchVal   = readAnalogAxis(PIN_CLUTCH, CLUTCH_MIN, CLUTCH_MAX);

  GameController.setRxAxis(throttleVal);
  GameController.setRzAxis(clutchVal);

  // 2. Чтение Тормоза
  if (brakeSensor.is_ready()) {
    long newVal = brakeSensor.get_value(1);
    long brakePressed = -newVal; // Переводим сжатие датчика в плюс

    if (brakePressed < 0) brakePressed = 0;

    int brakeVal = 0;
    if (brakePressed < BRAKE_DEADZONE) {
      brakeVal = 0;
    } else if (brakePressed >= BRAKE_MAX) {
      brakeVal = 1023;
    } else {
      // Рассчитываем плавный ход тормоза от мертвой зоны до максимума
      float brakePct = (float)(brakePressed - BRAKE_DEADZONE) / (float)(BRAKE_MAX - BRAKE_DEADZONE);
      brakeVal = (int)(brakePct * 1023.0f);
    }
    
    GameController.setRyAxis(brakeVal);
  }

  // 3. Чтение кнопок (Твоя матричная логика)
  bool B4 = (digitalRead(4) == LOW);
  bool B5 = (digitalRead(5) == LOW);
  bool B6 = (digitalRead(6) == LOW);
  bool B7 = (digitalRead(7) == LOW);

  bool newState[6] = {0,0,0,0,0,0};
  bool comb4_used = false;
  bool comb7_used = false;

  if (B4 && B5) { newState[0] = 1; comb4_used = true; }
  if (B5 && B7) { newState[1] = 1; comb7_used = true; }
  if (B4 && B6) { newState[4] = 1; comb4_used = true; }
  if (B6 && B7) { newState[5] = 1; comb7_used = true; }
  
  if (B4 && !comb4_used) newState[2] = 1;
  if (B7 && !comb7_used) newState[3] = 1;

  for (int i = 0; i < 6; i++) {
    if (newState[i] != buttonState[i]) {
      GameController.setButton(i, newState[i]);
      buttonState[i] = newState[i];
    }
  }

  delay(10); // Частота опроса 100 Гц — идеальный баланс отзывчивости
}
