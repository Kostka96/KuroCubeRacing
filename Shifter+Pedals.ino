#include <Adafruit_TinyUSB.h>
#include <HX711.h>

// ... пины и калибровка остаются прежними ...
#define PIN_BUTTON_4 4
#define PIN_BUTTON_5 5
#define PIN_BUTTON_6 6
#define PIN_BUTTON_7 7
#define PIN_THROTTLE A0
#define PIN_CLUTCH A1
#define PIN_HX711_DATA 3
#define PIN_HX711_CLOCK 2

#define THROTTLE_MIN 1620
#define THROTTLE_MAX 3190
#define CLUTCH_MIN 1620
#define CLUTCH_MAX 3190

#define DEADZONE 500
#define SAMPLES 8
#define LOADCELL_SCALE 1000

// === НОВЫЙ HID ДЕСКРИПТОР (Специально для педалей) ===
static const uint8_t desc_hid_report[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick) - Джойстик лучше подходит для кастомных педалей
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        // REPORT_ID (1) - Добавлен ID для стабильности

    // Три оси по 8 бит: Газ (Rx), Тормоз (Ry), Сцепление (Rz)
    // Использование Rx, Ry, Rz исключает конфликт с "рулем" (X)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x33,        // Usage (Rx) - Accelerator
    0x09, 0x34,        // Usage (Ry) - Brake
    0x09, 0x35,        // Usage (Rz) - Clutch
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0xFF,        // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x03,        // Report Count (3)
    0x81, 0x02,        // Input (Data, Variable, Absolute)

    // Кнопки (6 штук)
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01, 0x29, 0x06, // Buttons 1-6
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x06,
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    
    // Padding (2 bits)
    0x75, 0x01, 0x95, 0x02,
    0x81, 0x03,        // Input (Constant)
    0xC0
};

// Структура должна в точности соответствовать дескриптору!
#pragma pack(push, 1)
typedef struct {
    uint8_t throttle; // Rx
    uint8_t brake;    // Ry
    uint8_t clutch;   // Rz
    uint8_t buttons;  
} gamepad_report_t;
#pragma pack(pop)

gamepad_report_t report;
Adafruit_USBD_HID usb_hid;
HX711 brakeSensor;
bool prevButtonState[6] = {0};

// ... функции readSmooth и readAxis без изменений ...
int readSmooth(int pin) {
    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) sum += analogRead(pin);
    return sum / SAMPLES;
}

uint8_t readAxis(int pin, int minVal, int maxVal, int deadzone) {
    int raw = readSmooth(pin);
    if (raw < minVal + deadzone) return 0;
    if (raw > maxVal - deadzone) return 255;
    return (uint8_t) constrain(map(raw, minVal + deadzone, maxVal - deadzone, 0, 255), 0, 255);
}

void setup() {
    analogReadResolution(12);
    // ... pinMode кнопки и педали ...
    pinMode(PIN_BUTTON_4, INPUT_PULLUP);
    pinMode(PIN_BUTTON_5, INPUT_PULLUP);
    pinMode(PIN_BUTTON_6, INPUT_PULLUP);
    pinMode(PIN_BUTTON_7, INPUT_PULLUP);
    
    brakeSensor.begin(PIN_HX711_DATA, PIN_HX711_CLOCK);
    brakeSensor.set_scale(LOADCELL_SCALE);
    if (brakeSensor.is_ready()) brakeSensor.tare();

    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();
    
    TinyUSBDevice.setManufacturerDescriptor("KuroCube");
    TinyUSBDevice.setProductDescriptor("KuroShiPed");
    TinyUSBDevice.setSerialDescriptor("KB-002");
}

void loop() {
    if (!TinyUSBDevice.ready()) return;

    // 1. Читаем оси
    uint8_t throttleVal = readAxis(PIN_THROTTLE, THROTTLE_MIN, THROTTLE_MAX, DEADZONE);
    uint8_t clutchVal   = readAxis(PIN_CLUTCH, CLUTCH_MIN, CLUTCH_MAX, DEADZONE);
    
    uint8_t brakeVal8 = 0;
    if (brakeSensor.is_ready()) {
        long rawBrake = brakeSensor.get_units(1);
        brakeVal8 = constrain(map(rawBrake, 0, 1000, 0, 255), 0, 255);
    }

    // 2. Читаем кнопки (твоя логика комбинаций)
    bool B[4] = {digitalRead(PIN_BUTTON_4)==LOW, digitalRead(PIN_BUTTON_5)==LOW, 
                 digitalRead(PIN_BUTTON_6)==LOW, digitalRead(PIN_BUTTON_7)==LOW};
    bool newState[6] = {0};
    bool c4 = false, c7 = false;

    if (B[0] && B[1]) { newState[0]=1; c4=1; }
    if (B[1] && B[3]) { newState[1]=1; c7=1; }
    if (B[0] && B[2]) { newState[4]=1; c4=1; }
    if (B[2] && B[3]) { newState[5]=1; c7=1; }
    if (B[0] && !c4) newState[2]=1;
    if (B[3] && !c7) newState[3]=1;

    // 3. Собираем отчет
    report.throttle = throttleVal;
    report.brake = brakeVal8; // Используем 8-битное значение!
    report.clutch = clutchVal;
    
    report.buttons = 0;
    for (int i = 0; i < 6; i++) {
        if (newState[i]) report.buttons |= (1 << i);
        prevButtonState[i] = newState[i];
    }

    // 4. Отправляем (с ID 1, как в дескрипторе)
    usb_hid.sendReport(1, &report, sizeof(report));

    delay(10); // 100Гц достаточно для педалей
}
