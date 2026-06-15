#include <Keypad.h>
#include <Joystick.h>
#include <Keyboard.h>
#include <HX711.h>

// === ПИНЫ УСТРОЙСТВ ===
#define BRAKE_DATA  3
#define BRAKE_CLOCK 2
#define HANDBRAKE_DATA  9
#define HANDBRAKE_CLOCK 8

#define PIN_THROTTLE A0
#define PIN_CLUTCH   A1

// === КАЛИБРОВКИ ТЕНЗОДАТЧИКОВ ===
#define BRAKE_MAX      490000   
#define BRAKE_DEADZONE  15000   
#define HANDBRAKE_MAX      180000 // Сюда впишешь калибровку ручника 200000
#define HANDBRAKE_DEADZONE  10000

#define THROTTLE_MIN 566
#define THROTTLE_MAX 720
#define CLUTCH_MIN 566
#define CLUTCH_MAX 860

// === НАСТРОЙКА МАТРИЦЫ КЕЙПАДА (5 столбцов х 3 строки) ===
const byte ROWS = 3; 
const byte COLS = 5; 

// ID 6-14 (первые 3 столбца) — это кнопки геймпада (в Windows будут как Button 7 - 15)
// ID 100-105 (последние 2 столбца) — это маркеры клавиатуры
char hexaKeys[ROWS][COLS] = {
  {6, 7, 8,   100, 101},
  {9, 10, 11, 102, 103},
  {12, 13, 14, 104, 105}
};

byte rowPins[ROWS] = {10, 11, 12};     // Пины строк кейпада
byte colPins[COLS] = {A2, A3, A4, A5, 13}; // Пины столбцов кейпада

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

HX711 brakeSensor;
HX711 handbrakeSensor;
// === ИНИЦИАЛИЗАЦИЯ ИГРОВОГО УСТРОЙСТВА ===
// Всего кнопок геймпада теперь 15: 
// 0-5 (это 6 передач шифтера) + 6-14 (это 9 кнопок кейпада)
Joystick_ GameController(JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_GAMEPAD,
  15, 0,                 
  true, false, false,    // X-Axis под ручник
  true, true, true,      // Rx, Ry, Rz под педали
  false, false,          
  false, false, false);

// Для хранения состояния шифтера
bool shifterButtonState[6] = {0,0,0,0,0,0};

int throttleVal = 0;
int clutchVal = 0;
int brakeVal = 0;
int prevBrakeVal = 0;
int handbrakeVal = 0;
int prevHandbrakeVal = 0;

void setup() {
  GameController.begin();
  Keyboard.begin();
  
  // Пины шифтера
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);

  // Диапазоны осей
  GameController.setRxAxisRange(0, 1023); // Газ
  GameController.setRyAxisRange(0, 1023); // Тормоз
  GameController.setRzAxisRange(0, 1023); // Сцепление
  GameController.setXAxisRange(0, 1023);  // Ручник

  // Датчики веса
  brakeSensor.begin(BRAKE_DATA, BRAKE_CLOCK);
  brakeSensor.set_scale(1.0);
  handbrakeSensor.begin(HANDBRAKE_DATA, HANDBRAKE_CLOCK);
  handbrakeSensor.set_scale(1.0);
  
  delay(500);
  if (brakeSensor.is_ready()) brakeSensor.tare();
  if (handbrakeSensor.is_ready()) handbrakeSensor.tare();
}

// Плавное аналоговое чтение (с защитой от cross-talk)
int readAnalogAxis(int pin, int minVal, int maxVal) {
  analogRead(pin);
  delayMicroseconds(50);
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(pin);
    delayMicroseconds(10);
  }
  int raw = sum / 16;
  float percentage = (float)(raw - minVal) / (float)(maxVal - minVal);
  if (percentage < 0.0f) percentage = 0.0f;
  if (percentage > 1.0f) percentage = 1.0f;
  return (int)(percentage * 1023.0f);
}

// Функции кликов реальной клавиатуры Windows
void handleKeyboardPress(byte id) {
  switch(id) {
    case 100: Keyboard.press(KEY_ESC); break;
    case 101: Keyboard.press(' ');     break; // Пробел
    case 102: Keyboard.press(KEY_F1);  break;
    case 103: Keyboard.press(KEY_F2);  break;
    case 104: Keyboard.press(KEY_LEFT_ARROW);  break;
    case 105: Keyboard.press(KEY_RIGHT_ARROW); break;
  }
}

void handleKeyboardRelease(byte id) {
  switch(id) {
    case 100: Keyboard.release(KEY_ESC); break;
    case 101: Keyboard.release(' ');     break;
    case 102: Keyboard.release(KEY_F1);  break;
    case 103: Keyboard.release(KEY_F2);  break;
    case 104: Keyboard.release(KEY_LEFT_ARROW);  break;
    case 105: Keyboard.release(KEY_RIGHT_ARROW); break;
  }
}

void loop() {
  // 1. ОПРОС ПЕДАЛЕЙ И РУЧНИКА
  GameController.setRxAxis(readAnalogAxis(PIN_THROTTLE, THROTTLE_MIN, THROTTLE_MAX));
  GameController.setRzAxis(readAnalogAxis(PIN_CLUTCH, CLUTCH_MIN, CLUTCH_MAX));

  if (brakeSensor.is_ready()) {
    long rawBrake = -brakeSensor.get_value(1);
    if (rawBrake < 0) rawBrake = 0;
    if (rawBrake < BRAKE_DEADZONE) brakeVal = 0;
    else if (rawBrake >= BRAKE_MAX) brakeVal = 1023;
    else brakeVal = (int)(((float)(rawBrake - BRAKE_DEADZONE) / (float)(BRAKE_MAX - BRAKE_DEADZONE)) * 1023.0f);
    
    if (brakeVal != prevBrakeVal) { GameController.setRyAxis(brakeVal); prevBrakeVal = brakeVal; }
  }

  if (handbrakeSensor.is_ready()) {
    long rawHB = -handbrakeSensor.get_value(1);
    if (rawHB < 0) rawHB = 0;
    if (rawHB < HANDBRAKE_DEADZONE) handbrakeVal = 0;
    else if (rawHB >= HANDBRAKE_MAX) handbrakeVal = 1023;
    else handbrakeVal = (int)(((float)(rawHB - HANDBRAKE_DEADZONE) / (float)(HANDBRAKE_MAX - HANDBRAKE_DEADZONE)) * 1023.0f);
    
    if (handbrakeVal != prevHandbrakeVal) { GameController.setXAxis(handbrakeVal); prevHandbrakeVal = handbrakeVal; }
  }

  // 2. ОПРОС ШИФТЕРА КПП (Кнопки геймпада 0 - 5)
  bool B4 = (digitalRead(4) == LOW);
  bool B5 = (digitalRead(5) == LOW);
  bool B6 = (digitalRead(6) == LOW);
  bool B7 = (digitalRead(7) == LOW);

  bool newShifterState[6] = {0,0,0,0,0,0};
  bool comb4_used = false;
  bool comb7_used = false;

  if (B4 && B5) { newShifterState[0] = 1; comb4_used = true; }
  if (B5 && B7) { newShifterState[1] = 1; comb7_used = true; }
  if (B4 && B6) { newShifterState[4] = 1; comb4_used = true; }
  if (B6 && B7) { newShifterState[5] = 1; comb7_used = true; }
  if (B4 && !comb4_used) newShifterState[2] = 1;
  if (B7 && !comb7_used) newShifterState[3] = 1;

  for (int i = 0; i < 6; i++) {
    if (newShifterState[i] != shifterButtonState[i]) {
      GameController.setButton(i, newShifterState[i]); // Кнопки 1,2,3,4,5,6 в joy.cpl
      shifterButtonState[i] = newShifterState[i];
    }
  }

  // 3. ОПРОС КЕЙПАДА 5х3 (Кнопки геймпада 6 - 14 + Кнопки клавиатуры)
  if (customKeypad.getKeys()) {
    for (int i = 0; i < LIST_MAX; i++) {
      if (customKeypad.key[i].stateChanged) {
        byte keyID = customKeypad.key[i].kchar;

        if (customKeypad.key[i].kstate == PRESSED) {
          if (keyID < 50) {
            GameController.setButton(keyID, true); // Кнопки 7 - 15 в joy.cpl
          } else {
            handleKeyboardPress(keyID);
          }
        } 
        else if (customKeypad.key[i].kstate == RELEASED) {
          if (keyID < 50) {
            GameController.setButton(keyID, false);
          } else {
            handleKeyboardRelease(keyID);
          }
        }
      }
    }
  }

  delay(10);
}
