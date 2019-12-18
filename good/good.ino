#include "nrf.h"
#include "nrf_gzll.h"

const uint8_t debug = 0;
const uint8_t pipe = 4;

const uint8_t num_rows = 3;
const uint8_t num_cols = 7;
const uint8_t keys = num_rows * num_cols;

const uint8_t rows[num_rows] = {13, 12, 11};
const uint8_t cols_slim[num_cols] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};
const uint8_t cols_thick[num_cols] = {PIN_SPI_SCK, PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0};
const uint8_t* cols = nullptr;

const uint8_t delayPerTick = 2;
const uint8_t debounceDownTicks = 3;
const uint8_t debounceUpTicks = 3;
const uint16_t sleepAfterIdleTicks = 1000/delayPerTick;
const uint16_t repeatTransmitTicks = 500/delayPerTick;

typedef uint32_t matrix_t;
matrix_t state = 0;
uint16_t ticksSinceDiff = 0;
uint16_t ticksSinceTransmit = 0;
uint8_t debounceTicks[keys];
volatile bool sleeping = false;
volatile bool waking = true;

uint8_t channel_table[3] = {4, 42, 77};
uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;

///////////////////////////////////////////////////// MATRIX

inline matrix_t rc(uint8_t r, uint8_t c) {
  return ((matrix_t)1) << (c + r * num_cols);
}

inline void set(matrix_t *s, uint8_t r, uint8_t c, bool v) {
  *s |= v ? rc(r, c) : 0;
}

inline bool get(matrix_t s, uint8_t r, uint8_t c) {
  return s & rc(r, c) ? 1 : 0;
}

void printMatrix(matrix_t state) {
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
  if (pipe == 3 || pipe == 4) {
    cols = cols_thick;
  } else {
    cols = cols_slim;
  }
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
  memset(debounceTicks, 0, sizeof(debounceTicks));
}

matrix_t scanMatrix() {
  matrix_t scan = 0;
  for (int r = 0; r < num_rows; ++r) {
    digitalWrite(rows[r], LOW);
    for (int c = 0; c < num_cols; ++c) {
      set(&scan, r, c, !digitalRead(cols[c]));
    }
    digitalWrite(rows[r], HIGH);
  }
  return scan;
}

bool scanWithDebounce() {
  matrix_t scan = scanMatrix(); 
  bool diff = false;
  for (int i = 0; i < keys; ++i) {
    matrix_t b = ((matrix_t)1) << i;
    if (debounceTicks[i] > 0) {
      debounceTicks[i]--;
    } else if ((scan & b) != (state & b)) {
      state = (state & ~b) | (scan & b);
      debounceTicks[i] = (scan & b) ? debounceDownTicks : debounceUpTicks;
      diff = true;
    }
  }
  return diff;
}

///////////////////////////////////////////////////// POWER

inline void pinModeDetect(uint32_t pin) {
  pin = g_ADigitalPinMap[pin];
  NRF_GPIO_Type * port = nrf_gpio_pin_port_decode(&pin);
  port->PIN_CNF[pin] = ((uint32_t)GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
}

void sleep() {
  if (sleeping) return;
  sleeping = true;
  for (int r = 0; r < num_rows; ++r) {
    digitalWrite(rows[r], LOW);
  }
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENSET |= GPIOTE_INTENSET_PORT_Msk;
  for (int c = 0; c < num_cols; ++c) {
    pinModeDetect(cols[c]);
  }
  suspendLoop();
}

void wake() {
  if (!sleeping) return;
  sleeping = false;
  waking = true;
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENCLR |= GPIOTE_INTENSET_PORT_Msk;
  if (Serial && !debug) {
    Serial.end();
  }
  initMatrix();
  resumeLoop();
}

void killSerial() {
  NRF_UART0->TASKS_STOPTX = 1;
  NRF_UART0->TASKS_STOPRX = 1;
  NRF_UART0->ENABLE = 0;
  NRF_SPI0->ENABLE = 0;
}

void initCore() {
  NVIC_DisableIRQ(GPIOTE_IRQn);
  NVIC_ClearPendingIRQ(GPIOTE_IRQn);
  NVIC_SetPriority(GPIOTE_IRQn, 3);
  NVIC_EnableIRQ(GPIOTE_IRQn);
  attachCustomInterruptHandler(wake);
}

///////////////////////////////////////////////////// RADIO

void initRadio() {
  nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
  nrf_gzll_set_max_tx_attempts(100);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);
  nrf_gzll_enable();
}

void transmit() {
  // printMatrix(state);
  ticksSinceTransmit = 0;
  nrf_gzll_add_packet_to_tx_fifo(pipe, (uint8_t*)&state, 4);
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

///////////////////////////////////////////////////// MAIN

void setup() {
  if (debug) {
    Serial.begin(115200);
  }
  initCore();
  initMatrix();
  initRadio();
}

void loop() {
  if (waking) {
    waking = false;
  }
  delay(delayPerTick);
  ticksSinceDiff++;
  ticksSinceTransmit++;
  if (scanWithDebounce()) {
    transmit();
    ticksSinceDiff = 0;
  } else if (ticksSinceDiff > sleepAfterIdleTicks && state == 0) {
    sleep();
  } else if (ticksSinceTransmit > repeatTransmitTicks) {
    transmit();
  }
}