#include <Adafruit_TinyUSB.h>
#include <HX711.h>

#define PIN_BUTTON_4 4
#define PIN_BUTTON_5 5
#define PIN_BUTTON_6 6
#define PIN_BUTTON_7 7

#define PIN_THROTTLE A0
#define PIN_CLUTCH A1
#define PIN_HX711_DATA 3
#define PIN_HX711_CLOCK 2

// === КАЛИБРОВОЧНЫЕ ЗНАЧЕНИЯ ===
// Если при нажатии Газа/Сцепления значения ПАДАЮТ, просто поменяй MIN и MAX местами!
#define THROTTLE_MIN 2390   // реальный покой ADC
#define THROTTLE_MAX 3190   // реальный полный ход ADC

#define CLUTCH_MIN   2298   // реальный покой
#define CLUTCH_MAX   3200   // реальный полный ход

#define BRAKE_MAX      150000  // из лога максимум ~155000
#define BRAKE_DEADZONE  12000  // чуть выше шума ~9985

// === HID ДЕСКРИПТОР — 16-битные оси со знаком (-32768..32767) ===
// Это единственный формат который Windows joy.cpl показывает корректно 0-100%
static const uint8_t desc_hid_report[] = {
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x04,              // Usage (Joystick)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x01,              //   Report ID (1)

    // Три оси: Газ (Rx), Тормоз (Ry), Сцепление (Rz) — 16 бит со знаком
    0x05, 0x01,              //   Usage Page (Generic Desktop)
    0x09, 0x33,              //   Usage (Rx)
    0x09, 0x34,              //   Usage (Ry)
    0x09, 0x32,              //   Usage (Rz)
    0x16, 0x00, 0x80,        //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,        //   Logical Maximum (32767)
    0x75, 0x10,              //   Report Size (16 бит)
    0x95, 0x03,              //   Report Count (3 оси)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)

    // Кнопки (6 штук)
    0x05, 0x09,              //   Usage Page (Button)
    0x19, 0x01,              //   Usage Minimum (1)
    0x29, 0x06,              //   Usage Maximum (6)
    0x15, 0x00,              //   Logical Minimum (0)
    0x25, 0x01,              //   Logical Maximum (1)
    0x75, 0x01,              //   Report Size (1)
    0x95, 0x06,              //   Report Count (6)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)

    // Padding (2 бита до байта)
    0x75, 0x01,              //   Report Size (1)
    0x95, 0x02,              //   Report Count (2)
    0x81, 0x03,              //   Input (Constant)
    0xC0                     // End Collection
};

#pragma pack(push, 1)
typedef struct {
    int16_t throttle;   // -32768..32767
    int16_t brake;
    int16_t clutch;
    uint8_t buttons;
} gamepad_report_t;
#pragma pack(pop)

gamepad_report_t report;
Adafruit_USBD_HID usb_hid;
HX711 brakeSensor;
long lastBrake = 0;

// Возвращает -32768..32767 из аналогового пина
// Возвращает честные -32768..32767 из аналогового пина без наводок между каналами
int16_t getAnalogAxis(int pin, int minVal, int maxVal) {
    // 1. СБРОС НАВОДОК: делаем холостой замер и даем АЦП RP2040 «переварить» напряжение
    analogRead(pin);
    delayMicroseconds(50); // Короткая пауза для стабилизации емкости АЦП

    // 2. БОЕВОЙ ЗАМЕР: собираем 16 отсчетов для максимальной фильтрации шумов
    long sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogRead(pin);
        delayMicroseconds(10);
    }
    int raw = sum / 16;

    // 3. ПЛАВАЮЩИЙ РАСЧЕТ: переводим в проценты, чтобы избежать ступенчатости
    float percentage = (float)(raw - minVal) / (float)(maxVal - minVal);
    
    // Ограничиваем жесткие рамки 0.0 -- 1.0
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 1.0f) percentage = 1.0f;

    // 4. МАСШТАБИРОВАНИЕ: переводим в диапазон -32768..32767 для Windows со знаком
    long result = -32768L + (long)(percentage * 65535.0f);
    
    return (int16_t)constrain(result, -32768L, 32767L);
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);

    pinMode(PIN_BUTTON_4, INPUT_PULLUP);
    pinMode(PIN_BUTTON_5, INPUT_PULLUP);
    pinMode(PIN_BUTTON_6, INPUT_PULLUP);
    pinMode(PIN_BUTTON_7, INPUT_PULLUP);

    TinyUSBDevice.setManufacturerDescriptor("KuroCube");
    TinyUSBDevice.setProductDescriptor("KuroShiPed");
    TinyUSBDevice.setSerialDescriptor("KB-002");

    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();

    brakeSensor.begin(PIN_HX711_DATA, PIN_HX711_CLOCK);
    brakeSensor.set_scale(1.0);
    delay(500);
    if (brakeSensor.is_ready()) {
        brakeSensor.tare();
    }
}

void loop() {
    #if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED)
    delay(1);
    #endif

    if (!TinyUSBDevice.ready()) return;
    // 1. Газ и сцепление
    int16_t throttleVal = getAnalogAxis(PIN_THROTTLE, THROTTLE_MIN, THROTTLE_MAX);
    int16_t clutchVal   = getAnalogAxis(PIN_CLUTCH,   CLUTCH_MIN,   CLUTCH_MAX);

    // 2. Тормоз — чистый расчет без блокирующих фильтров
    int16_t brakeVal = -32768; // По умолчанию 0% в joy.cpl
    
    if (brakeSensor.is_ready()) {
        long newVal = brakeSensor.get_value(1);
        long brakePressed = -newVal; // Переводим отрицательное сжатие тензодатчика в плюс

        if (brakePressed < 0) brakePressed = 0;

        // Обработка шкалы -32768..32767 с плавающей точностью
        if (brakePressed < BRAKE_DEADZONE) {
            brakeVal = -32768;
        } else if (brakePressed >= BRAKE_MAX) {
            brakeVal = 32767;
        } else {
            float brakePct = (float)(brakePressed - BRAKE_DEADZONE) / (float)(BRAKE_MAX - BRAKE_DEADZONE);
            long res = -32768L + (long)(brakePct * 65535.0f);
            brakeVal = (int16_t)constrain(res, -32768L, 32767L);
        }
    }
    // 3. Кнопки
    bool B[4] = {
        digitalRead(PIN_BUTTON_4) == LOW,
        digitalRead(PIN_BUTTON_5) == LOW,
        digitalRead(PIN_BUTTON_6) == LOW,
        digitalRead(PIN_BUTTON_7) == LOW
    };
    bool newState[6] = {0};
    bool c4 = false, c7 = false;

    if (B[0] && B[1]) { newState[0]=1; c4=1; }
    if (B[1] && B[3]) { newState[1]=1; c7=1; }
    if (B[0] && B[2]) { newState[4]=1; c4=1; }
    if (B[2] && B[3]) { newState[5]=1; c7=1; }
    if (B[0] && !c4)  newState[2]=1;
    if (B[3] && !c7)  newState[3]=1;

    // 4. Упаковка и отправка
    report.throttle = throttleVal;
    report.brake    = brakeVal;
    report.clutch   = clutchVal;

    report.buttons = 0;
    for (int i = 0; i < 6; i++) {
        if (newState[i]) report.buttons |= (1 << i);
    }

    usb_hid.sendReport(1, &report, sizeof(report));
    delay(10);
}
