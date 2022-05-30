#include <Arduino.h>
#include <Wire.h>

// PHYSICAL PINS, stripe on left
const uint8_t RIBBON[22] = {
    // TOP ROW, 0-10
    16, 17, 18, 19, /* cols: */ 20, 21, 22, 26, 27, 3, 2,
    // BOTTOM ROW, 11-21
    15, 14, 13, 12, /* cols: */ 11, 10, 9, 8, 7, 6, 5,
};

const uint8_t NUM_ROW = 8;
const uint8_t NUM_COL = 7;
const uint8_t NUM_KEY = NUM_ROW * NUM_COL;

// LOGICAL PINS, stripe on left
// TOP: ROW_0, ROW_2, ROW_4, ROW_6, COL_A6, COL_A5, COL_A4, COL_A3, COL_A2, COL_A1, COL_A0,
// BOT: ROW_1, ROW_3, ROW_5, ROW_7, COL_B6, COL_B5, COL_B4, COL_B3, COL_B2, COL_B1, COL_B0,
const uint8_t ROW[NUM_ROW] = { RIBBON[0], RIBBON[11], RIBBON[1], RIBBON[12], RIBBON[2], RIBBON[13], RIBBON[3], RIBBON[14] };
const uint8_t COL_A[NUM_COL] = { RIBBON[10], RIBBON[9], RIBBON[8], RIBBON[7], RIBBON[6], RIBBON[5], RIBBON[4] };
const uint8_t COL_B[NUM_COL] = { RIBBON[21], RIBBON[20], RIBBON[19], RIBBON[18], RIBBON[17], RIBBON[16], RIBBON[15] };

uint32_t lastA[NUM_ROW], lastB[NUM_ROW];
uint64_t timeA[NUM_KEY] = { 0 }, timeB[NUM_KEY] = { 0 };
uint32_t bitRow[NUM_ROW], bitColA[NUM_COL], bitColB[NUM_COL];
uint32_t maskRow = 0, maskColA = 0, maskColB = 0;

static inline uint8_t timeToVelocity(uint64_t t) {
    // Tuning notes:
    // https://docs.google.com/spreadsheets/d/1R55Zrt3V2YheBwDRFwp-pJO4Ciedf6vfs9Ag5xaM4PQ/edit
    int vel = 150 * exp(t * -0.00005f);
    if (vel < 1) return 1;
    if (vel > 127) return 127;
    return vel;
}

inline uint8_t note(uint8_t row, uint8_t col) {
    return col * 8 + row + 32;
}

void keyUp(uint8_t note, uint32_t t) {
    /*Serial.print("UP ");
    Serial.print(note);
    Serial.print(" ");
    Serial.println(t);
    Serial.flush();*/
}

void keyDown(uint8_t note, uint32_t t) {
    //Serial.print("DOWN ");
    //Serial.print(note);
    //Serial.print(" ");
    Serial.println(t);
    //Serial.flush();
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
    return key_changed;
}

inline bool scanMatrix() {
    bool key_changed = false;
    for (uint8_t row = 0; row < NUM_ROW; ++row) {
        gpio_clr_mask(bitRow[row]);
        delayMicroseconds(100);
        uint32_t scan = gpio_get_all();
        gpio_set_mask(bitRow[row]);
        uint64_t t = to_us_since_boot(get_absolute_time());
        key_changed |= updateScan(t, row, scan);
    }
    return key_changed;
}


void setup() {
    Serial.begin(9600);
    Wire.setSDA(0);
    Wire.setSCL(1);
    pinMode(PIN_LED, OUTPUT);
    initMatrix();
    digitalWrite(PIN_LED, 1);
}

void loop() {
    scanMatrix();
}