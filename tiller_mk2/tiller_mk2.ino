#include "nrf_gzll.h"

#define num_cols 7
#define num_rows 3

const uint8_t rows[num_rows] = {13, 12, 11};
const uint8_t cols[num_cols] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};

uint8_t matrix[num_rows + 1];
uint8_t lastMatrix[num_rows];
int scanTimeMs = 100;

///////////////////////////////////////////////////// MATRIX

inline void clearMatrix(uint8_t *m) {
  memset(m, 0, num_rows);
}

inline void copyMatrixTo(uint8_t *other) {
  memcpy(other, matrix, num_rows);
}

inline bool different(uint8_t *a, uint8_t *b) {
  return memcmp(a, b, num_rows);
}

inline void setKey(uint8_t r, uint8_t c, bool set) {
  matrix[r] = matrix[r] | (set << c);
}

inline bool getKey(uint8_t r, uint8_t c) {
  return (matrix[r] & (1 << c)) != 0;
}

bool anyKeyDown() {
  for (int r = 0; r < num_rows; ++r) {
    if (matrix[r]) {
      return true;
    }
  }
  return false;
}

void printMatrix() {
  if (!Serial) {
    return;
  }
  Serial.println("");
  for (uint8_t r = 0; r < num_rows; r++) {
    for (uint8_t c = 0; c < num_cols; c++) {
      Serial.print(getKey(r, c));
    }
    Serial.println("");
  }
  Serial.println("");
}

void scanMatrix() {
  clearMatrix(matrix);
  for (uint8_t c = 0; c < num_cols; ++c) {
    digitalWrite(cols[c], HIGH);
    for (uint8_t r = 0; r < num_rows; ++r) {
      bool bit = digitalRead(rows[r]);
      setKey(r, c, bit ? 1 : 0);
    }
    digitalWrite(cols[c], LOW);
  }
}

void initMatrix() {
  for (uint8_t c = 0; c < num_cols; ++c) {
    pinMode(cols[c], OUTPUT);
    digitalWrite(cols[c], LOW);
  }
  for (uint8_t r = 0; r < num_rows; ++r) {
    pinMode(rows[r], INPUT_PULLDOWN);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("boot"); 
  initMatrix();
  nrf_gzll_init(NRF_GZLL_MODE_HOST);
}

void loop() {
  scanMatrix();
  printMatrix();
  delay(scanTimeMs);
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}
