#include <Arduino.h>
#include <Wire.h>

const bool DEBUG = false;

const uint8_t NUM_ROW = 8;
const uint8_t NUM_COL = 7;
const uint8_t NUM_KEY = NUM_ROW * NUM_COL;

// PHYSICAL PINS, stripe on left
const uint8_t RIBBON[22] = {
    // TOP ROW, 0-10
    3, 5, 28, 26, 21, 9, 11, 19, 17, 12, 14,
    // BOTTOM ROW, 11-21
    4, 6, 27, 22, 8, 10, 20, 18, 16, 13, 15,
};

// LOGICAL PINS, stripe on left
// TOP: ROW_0, ROW_2, ROW_4, ROW_6, COL_A6, COL_A5, COL_A4, COL_A3, COL_A2, COL_A1, COL_A0,
// BOT: ROW_1, ROW_3, ROW_5, ROW_7, COL_B6, COL_B5, COL_B4, COL_B3, COL_B2, COL_B1, COL_B0,
const uint8_t ROW[NUM_ROW] = {
    RIBBON[0], RIBBON[11], RIBBON[1], RIBBON[12], RIBBON[2], RIBBON[13], RIBBON[3], RIBBON[14] };
const uint8_t COL_A[NUM_COL] = {
    RIBBON[10], RIBBON[9], RIBBON[8], RIBBON[7], RIBBON[6], RIBBON[5], RIBBON[4] };
const uint8_t COL_B[NUM_COL] = {
    RIBBON[21], RIBBON[20], RIBBON[19], RIBBON[18], RIBBON[17], RIBBON[16], RIBBON[15] };

// A columns are the early switches, B columns are the late ones.
uint32_t lastA[NUM_ROW], lastB[NUM_ROW];
uint64_t timeA[NUM_KEY] = { 0 }, timeB[NUM_KEY] = { 0 };
uint32_t bitRow[NUM_ROW], bitColA[NUM_COL], bitColB[NUM_COL];
uint32_t maskRow = 0, maskColA = 0, maskColB = 0;

inline uint8_t note(uint8_t row, uint8_t col) {
    return col * 8 + row + 32;
}

void keyUp(uint8_t note, uint32_t t) {
    uint32_t msg = note;
    msg |= 0x80;
    msg <<= 24;
    msg |= t;
    rp2040.fifo.push(msg);
}

void keyDown(uint8_t note, uint32_t t) {
    uint32_t msg = note;
    msg <<= 24;
    msg |= t;
    rp2040.fifo.push(msg);
}

void allKeysUp() {
    rp2040.fifo.push(0);
}

void initMatrix() {
    memset(lastA, 255, NUM_ROW * 4);
    memset(lastB, 255, NUM_ROW * 4);
    for (int i = 0; i < NUM_COL; ++i) {
        pinMode(COL_A[i], INPUT_PULLUP);
        pinMode(COL_B[i], INPUT_PULLUP);
        bitColA[i] = (1UL << COL_A[i]);
        bitColB[i] = (1UL << COL_B[i]);
        maskColA |= bitColA[i];
        maskColB |= bitColB[i];
    }
    for (int i = 0; i < NUM_ROW; ++i) {
        pinMode(ROW[i], OUTPUT);
        maskRow |= (bitRow[i] = (1UL << ROW[i]));
    }
    gpio_set_mask(maskRow);
}

inline bool updateScan(uint64_t t, uint8_t row, uint32_t scanAll) {
    bool key_changed = false;
    uint32_t scanColA = scanAll & maskColA;
    if (scanColA != lastA[row]) {
        for (uint8_t col = 0; col < NUM_COL; ++col) {
            uint32_t bit = bitColA[col];
            uint8_t k = NUM_COL * row + col;
            if ((scanColA & bit) && !(lastA[row] & bit)) { // released
                if (timeB[k] != 0 && timeA[k] == 0) {
                    keyUp(note(row, col), (uint32_t)min(t - timeB[k], 0xfffff));
                    key_changed = true;
                }
                timeA[k] = timeB[k] = 0;
            }
            if (!(scanColA & bit) && (lastA[row] & bit)) { // pressed
                timeA[k] = t;
            }
        }
        lastA[row] = scanColA;
    }
    uint32_t scanColB = scanAll & maskColB;
    if (scanColB != lastB[row]) {
        for (uint8_t col = 0; col < NUM_COL; ++col) {
            uint32_t bit = bitColB[col];
            uint8_t k = NUM_COL * row + col;
            if ((scanColB & bit) && !(lastB[row] & bit)) { // released
                timeB[k] = t;
            }
            if (!(scanColB & bit) && (lastB[row] & bit)) { // pressed
                if (timeA[k] != 0 && timeB[k] == 0) {
                    keyDown(note(row, col), (uint32_t)min(t - timeA[k], 0xfffff));
                    key_changed = true;
                }
                timeA[k] = timeB[k] = 0;
            }
        }
        lastB[row] = scanColB;
    }

    // At least one switch is down (doesn't mean note is sounding).
    return scanColB != maskColB || scanColA != maskColA;
}


inline void scanMatrix() {
    static bool last_had_switch_down = false;
    bool has_switch_down = false;
    for (uint8_t row = 0; row < NUM_ROW; ++row) {
        gpio_clr_mask(bitRow[row]);
        delayMicroseconds(100);
        uint32_t scan = gpio_get_all();
        gpio_set_mask(bitRow[row]);
        uint64_t t = to_us_since_boot(get_absolute_time());
        has_switch_down |= updateScan(t, row, scan);
    }
    if (!has_switch_down && last_had_switch_down) {
        allKeysUp();
    }
    last_had_switch_down = has_switch_down;
}

void setup1() {
    // Wait for radio to start up to avoid filling the queue on startup.
    delay(4000);
    initMatrix();
}

void loop1() {
    scanMatrix();
}

void setup() {
    Serial.begin(9600);
    Wire.setSDA(0);
    Wire.setSCL(1);
    Wire.begin();

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, 0);

    // Pullup the I2C bus.
    pinMode(7, OUTPUT);
    digitalWrite(7, 1);
}

void loop() {
    uint32_t msg = rp2040.fifo.pop();

    if (DEBUG) Serial.println(msg, 16);

    Wire.beginTransmission(0);
    Wire.write((uint8_t*)&msg, 4);
    uint32_t err = Wire.endTransmission();

    if (err) {
        if (DEBUG) Serial.println(err);
        digitalWrite(PIN_LED, 1);
    } else {
        digitalWrite(PIN_LED, 0);
    }
}