#include "nrf.h"

/////// MATRIX

#define VMIN 3000
#define VMAX 50000

uint8_t last1[8], last2[8];
unsigned long time1[64], time2[64];

void initMatrix() {
  memset(last1, 255, 8);
  memset(last2, 255, 8);
  memset(time1, 0, 64 * 4);
  memset(time2, 0, 64 * 4);
}

void show(uint8_t b) {
  for (int i = 0; i < 8; ++i) {
    Serial.print((int)(b >> i) & 1);
  }
}

uint8_t note(uint8_t i, uint8_t j) {
  return j * 8 + i + 32;
}

uint8_t vel(long t) {
  t = (t - VMIN) * 127 / VMAX;
  t = 127 - t;
  return min(127, max(1, t));
}

void noteOn(uint8_t i, uint8_t j, unsigned long t) {
  uint8_t n = note(i, j);
  uint8_t v = vel(t);
  Serial.print("ON ");
  Serial.print(n);
  Serial.print(" ");
  Serial.println(vel(t));
  Serial.flush();
}

void noteOff(uint8_t i, uint8_t j, unsigned long t) {
  uint8_t n = note(i, j);
  uint8_t v = vel(t);
  Serial.print("OFF ");
  Serial.print(n);
  Serial.print(" ");
  Serial.println(vel(t));
  Serial.flush();
}

void updateScan(unsigned long t, uint8_t i, uint8_t scan1, uint8_t scan2) {
  if (scan1 != last1[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint8_t b = 1 << j;
      uint8_t k = 8 * i + j;
      if ((scan1 & b) && !(last1[i] & b)) { // released
        if (time2[k] != 0 && time1[k] == 0) {
          noteOff(i, j, t - time2[k]);
        }
        time1[k] = time2[k] = 0;
      }
      if (!(scan1 & b) && (last1[i] & b)) { // pressed
        time1[k] = t;
      }
    }
    last1[i] = scan1;
  }
  if (scan2 != last2[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint8_t b = 1 << j;
      uint8_t k = 8 * i + j;
      if ((scan2 & b) && !(last2[i] & b)) { // released
        time2[k] = t;
      }
      if (!(scan2 & b) && (last2[i] & b)) { // pressed
        if (time1[k] != 0 && time2[k] == 0) {
          noteOn(i, j, t - time1[k]);
        }
        time1[k] = time2[k] = 0;
      }
    }
    last2[i] = scan2;
  }
}

/////// CORE IO

const uint8_t rows[8] = {2, 3, 4, 5, 28, 29, 30, 31};
uint32_t rowScan[8];
uint32_t rowMask = 0;

void nfcAsGpio() {
  if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)){
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NVIC_SystemReset();
  }
}

void setup() {
  nfcAsGpio();
  Serial.begin(115200);
  for (int i = 9; i <= 23; ++i) {
    if (i == 21) continue;
    pinMode(i, INPUT_PULLUP);
  }
  for (int i = 0; i <= 8; ++i) {
    pinMode(rows[i], OUTPUT);
    digitalWrite(rows[i], HIGH);
    rowMask |= (rowScan[i] = (1UL << rows[i]));
  }
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  initMatrix();
}

inline uint8_t scanUpper(uint32_t s) {
  // 16, 17, 18, 19, 20, 22, 23
  return ((s >> 16) & 0x1fUL) | ((s >> 17) & 0x60UL);
}

inline uint8_t scanLower(uint32_t s) {
  // 9, 10, 11, 12, 13, 14, 15
  return (s >> 9) & 0x7fUL;
}

void loop() {
  for(uint8_t i = 0; i < 8; ++i) {
    NRF_GPIO->OUTSET = rowMask;
    NRF_GPIO->OUTCLR = rowScan[i];
    unsigned long t = micros();
    uint32_t scan = NRF_GPIO->IN;
    updateScan(t, i, scanUpper(scan), scanLower(scan));
  }
}
