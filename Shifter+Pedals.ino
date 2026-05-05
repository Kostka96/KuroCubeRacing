#include <Adafruit_TinyUSB.h>
#include <HX711.h>

// Pin definitions for shifter buttons
#define PIN_BUTTON_4  4   // Button 4 (GP4)
#define PIN_BUTTON_5  5   // Button 5 (GP5)
#define PIN_BUTTON_6  6   // Button 6 (GP6)
#define PIN_BUTTON_7  7   // Button 7 (GP7)

// Pin definitions for pedals
#define PIN_THROTTLE  A0  // Throttle pedal (GP26 - ADC0)
#define PIN_CLUTCH    A1  // Clutch pedal (GP27 - ADC1)

// Подбери эти значения под свой датчик (замерь реальные min/max)
#define THROTTLE_MIN  1200  // АЦП при педали отпущена
#define THROTTLE_MAX  3200  // АЦП при педали в полу
#define DEADZONE      20    // мёртвая зона в единицах АЦП

uint8_t readAxis(int pin, int minVal, int maxVal, int deadzone) {
    int raw = analogRead(pin);
    
    // Мёртвые зоны по краям
    if (raw < minVal + deadzone) return 0;
    if (raw > maxVal - deadzone) return 255;
    
    return (uint8_t) map(raw, minVal + deadzone, maxVal - deadzone, 0, 255);
}

// HX711 pins for brake load cell
#define PIN_HX711_DATA  3   // HX711 data pin (GP3)
#define PIN_HX711_CLOCK 2   // HX711 clock pin (GP2)

// Brake calibration
#define LOADCELL_SCALE 1000   // Adjust this value to calibrate brake sensitivity

// HID Report Descriptor for gamepad with 3 axes and 6 buttons
static const uint8_t desc_hid_report[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    
    // X Axis - Throttle (8 bit)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Y Axis - Brake (8 bit)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Z Axis - Clutch (8 bit)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x32,        //   Usage (Z)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Buttons (6 buttons)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x06,        //   Usage Maximum (Button 6)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Padding (2 bits)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x01,        //   Input (Constant)
    
    0xC0,              // End Collection
};

// HID device setup
Adafruit_USBD_HID usb_hid;

// Load cell sensor
HX711 brakeSensor;

// Button states
bool buttonState[6] = {0, 0, 0, 0, 0, 0};
bool prevButtonState[6] = {0, 0, 0, 0, 0, 0};

// Custom report structure
typedef struct {
    uint8_t throttle;  // X axis (0-255)
    uint8_t brake;     // Y axis (0-255)
    uint8_t clutch;    // Z axis (0-255)
    uint8_t buttons;   // 6 buttons in lower 6 bits
} gamepad_report_t;

gamepad_report_t report;

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    // Initialize buttons with internal pull-ups
    pinMode(PIN_BUTTON_4, INPUT_PULLUP);
    pinMode(PIN_BUTTON_5, INPUT_PULLUP);
    pinMode(PIN_BUTTON_6, INPUT_PULLUP);
    pinMode(PIN_BUTTON_7, INPUT_PULLUP);

    // Initialize analog pins for pedals
    pinMode(PIN_THROTTLE, INPUT);
    pinMode(PIN_CLUTCH, INPUT);

    // Initialize HX711 load cell for brake
    brakeSensor.begin(PIN_HX711_DATA, PIN_HX711_CLOCK);
    brakeSensor.set_scale(LOADCELL_SCALE);
    // Tare только если HX711 отвечает
    if (brakeSensor.is_ready()) {
        brakeSensor.tare();
    }

    // Initialize TinyUSB HID
    usb_hid.setPollInterval(2);  // 2ms polling for fast response
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.begin();
    TinyUSBDevice.setManufacturerDescriptor("KuroCube");
    TinyUSBDevice.setProductDescriptor("KuroShiPed");
    TinyUSBDevice.setSerialDescriptor("KB-002");

    // Wait for USB connection
    while (!TinyUSBDevice.mounted()) {
        delay(100);
    }
}

void loop() {
    // Read and process throttle (analog 0-4095 -> 0-255)
    int throttleRaw = analogRead(PIN_THROTTLE);
    uint8_t throttleVal = readAxis(PIN_THROTTLE, THROTTLE_MIN, THROTTLE_MAX, DEADZONE);
    
    // Read and process clutch (analog 0-4095 -> 0-255)
    int clutchRaw = analogRead(PIN_CLUTCH);
    uint8_t clutchVal   = readAxis(PIN_CLUTCH,   THROTTLE_MIN, THROTTLE_MAX, DEADZONE);

    long brakeVal = 0;
    if (brakeSensor.is_ready()) {
        brakeVal = brakeSensor.get_units(1);
    }
    report.brake = constrain(map(brakeVal, 0, 1000, 0, 255), 0, 255);

    // Read buttons (active LOW with pull-ups)
    bool B4 = (digitalRead(PIN_BUTTON_4) == LOW);
    bool B5 = (digitalRead(PIN_BUTTON_5) == LOW);
    bool B6 = (digitalRead(PIN_BUTTON_6) == LOW);
    bool B7 = (digitalRead(PIN_BUTTON_7) == LOW);

    // Calculate new button states
    bool newState[6] = {0, 0, 0, 0, 0, 0};
    
    bool comb4_used = false;
    bool comb7_used = false;

    if (B4 && B5) { newState[0] = 1; comb4_used = true; }
    if (B5 && B7) { newState[1] = 1; comb7_used = true; }
    if (B4 && B6) { newState[4] = 1; comb4_used = true; }
    if (B6 && B7) { newState[5] = 1; comb7_used = true; }

    if (B4 && !comb4_used) newState[2] = 1;
    if (B7 && !comb7_used) newState[3] = 1;

    // Check button changes
    bool stateChanged = false;
    for (int i = 0; i < 6; i++) {
        if (newState[i] != prevButtonState[i]) {
            stateChanged = true;
            prevButtonState[i] = newState[i];
        }
    }

    // Check axis changes
    bool axisChanged = (throttleVal != report.throttle || brakeVal != report.brake || clutchVal != report.clutch);

    // Update report
    report.throttle = throttleVal;
    report.brake = brakeVal;
    report.clutch = clutchVal;

    // Send if anything changed
    if ((stateChanged || axisChanged) && TinyUSBDevice.mounted()) {
        if (stateChanged) {
            report.buttons = 0;
            for (int i = 0; i < 6; i++) {
                if (newState[i]) report.buttons |= (1 << i);
            }
        }
        usb_hid.sendReport(0, &report, sizeof(report));
    }
    Serial.print("T: "); Serial.print(analogRead(PIN_THROTTLE));
    Serial.print("  C: "); Serial.println(analogRead(PIN_CLUTCH));
    delay(20);
}
