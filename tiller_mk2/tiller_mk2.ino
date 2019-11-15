#include "nrf_gzll.h"

#define debug if(Serial) { Serial.println(""); }

#define num_rows 3
#define num_cols 7
#define keys (num_rows * num_cols)
#define matrix uint32_t
#define bit(r, c) (((matrix)1) << (c + r * num_cols))
#define set(s, r, c, v) (s |= v ? bit(r, c) : 0)
#define get(s, r, c) (s & bit(r, c) ? 1 : 0)
const uint8_t rows[num_rows] = {13, 12, 11};
const uint8_t cols[num_cols] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};

matrix state = 0;

#define delayPerTick 2
#define debounceDownTicks 3
#define debounceUpTicks 6
#define sleepAfterIdleTicks (1000/delayPerTick)
#define repeatTransmitTicks (500/delayPerTick)

int ticksSinceDiff = 0;
int ticksSinceTransmit = 0;
uint8_t debounceTicks[keys];
bool sleeping = false;

///////////////////////////////////////////////////// MATRIX


void printMatrix(matrix state) {
  if (!Serial) {
    return;
  }
  Serial.println("");
  for (uint8_t r = 0; r < num_rows; r++) {
    for (uint8_t c = 0; c < num_cols; c++) {
      Serial.print(get(state, r, c));
    }
    Serial.println("");
  }
  Serial.println("");
}

void initMatrix() {
  for (int r = 0; r < num_rows; ++r) {
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], HIGH);
  }
  for (int c = 0; c < num_cols; ++c) {
    pinMode(cols[c], INPUT_PULLUP);
  }
  ticksSinceDiff = 0;
  ticksSinceTransmit = repeatTransmitTicks;
  state = 0;
}

matrix scanMatrix() {
  matrix scan = 0;
  for (int r = 0; r < num_rows; ++r) {
    digitalWrite(rows[r], LOW);
    for (int c = 0; c < num_cols; ++c) {
      set(scan, r, c, !digitalRead(cols[c]));
    }
    digitalWrite(rows[r], HIGH);
  }
  return scan;
}

void setup() {
  Serial.begin(115200);
  Serial.println("boot"); 
  initMatrix();
  nrf_gzll_init(NRF_GZLL_MODE_HOST);
}

void loop() {
  printMatrix(scanMatrix());
  delay(500);
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}
