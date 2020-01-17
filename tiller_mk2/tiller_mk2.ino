#include "nrf.h"
#include "nrf_gzll.h"

#define PIPE 3
#define DEBUG 0

#define ROWS 3
#define COLS 7
#define KEYS (ROWS * COLS)
#define matrix_t uint32_t
#define bit(r, c) (((matrix_t)1) << (c + r * COLS))
#define set(s, r, c, v) (s |= v ? bit(r, c) : 0)
#define get(s, r, c) (s & bit(r, c) ? 1 : 0)

const uint8_t rows[ROWS] = {13, 12, 11};
#if (PIPE == 3 || PIPE == 4)
  const uint8_t cols[COLS] = {PIN_SPI_SCK, PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0};
#else
  const uint8_t cols[COLS] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};
#endif

#define packet_t_size 4
matrix_t state = 0;
matrix_t lastScan;

#define delayPerTick 1
#define numDebounce 6
#define sleepAfterIdleTicks (1500/delayPerTick)
#define repeatTransmitTicks (400/delayPerTick)

int ticksSinceDiff = 0;
int ticksSinceTransmit = 0;
int debounceCount = 0;
volatile bool sleeping = false;
volatile bool waking = true;

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
#if (PIPE % 2)
  uint8_t channel_table[3] = {25, 63, 33};
#else
  uint8_t channel_table[3] = {4, 42, 77};
#endif

///////////////////////////////////////////////////// MATRIX

void printMatrix(matrix_t state) {
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      Serial.print(get(state, r, c));
    }
    Serial.print(" ");
  }
}

void initMatrix() {
  for (int r = 0; r < ROWS; ++r) {
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], HIGH);
  }
  for (int c = 0; c < COLS; ++c) {
    pinMode(cols[c], INPUT_PULLUP);
  }
  ticksSinceDiff = 0;
  ticksSinceTransmit = repeatTransmitTicks + 1;
  state = 0;
  lastScan = 0;
  debounceCount = 0;
}

matrix_t scanMatrix() {
  matrix_t scan = 0;
  for (int r = 0; r < ROWS; ++r) {
    digitalWrite(rows[r], LOW);
    for (int c = 0; c < COLS; ++c) {
      set(scan, r, c, !digitalRead(cols[c]));
    }
    digitalWrite(rows[r], HIGH);
  }
  return scan;
}

bool scanWithDebounce() {
  matrix_t scan = scanMatrix();
  if (scan == state || scan != lastScan) {
    debounceCount = 0;
    lastScan = scan;
    return false;
  } else if (debounceCount > numDebounce) {
    debounceCount = 0;
    state = scan;
    return true;
  } else {
    debounceCount++;
    return false;
  }
}

///////////////////////////////////////////////////// POWER

inline void pinModeDetect(uint32_t pin) {
  pin = g_ADigitalPinMap[pin];
  NRF_GPIO_Type *port = nrf_gpio_pin_port_decode(&pin);
  port->PIN_CNF[pin] = ((uint32_t)GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
}

void sleep() {
  if (sleeping) return;
  sleeping = true;
  for (int r = 0; r < ROWS; ++r) {
    digitalWrite(rows[r], LOW);
  }
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENSET |= GPIOTE_INTENSET_PORT_Msk;
  for (int c = 0; c < COLS; ++c) {
    pinModeDetect(cols[c]);
  }
  suspendLoop();
}

void wake() {
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENCLR |= GPIOTE_INTENSET_PORT_Msk;
  if (!sleeping) return;
  sleeping = false;
  waking = true;
  resumeLoop();
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
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_4_DBM);
  nrf_gzll_enable();
}

void transmit() {
  nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)&state, 4);
  #if DEBUG
    static matrix_t printed;
    if (printed != state) {
      Serial.println("");
      printMatrix(state);
      printed = state;
    } else {
      Serial.println(".");
    }
  #endif
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
  #if DEBUG
    Serial.begin(115200);
  #endif
  initCore();
  initRadio();
}

void loop() {
  if (sleeping) {
    delay(1);
    return;
  }
  if (waking) {
    #if DEBUG
      Serial.println("wake");
      Serial.flush();
    #else
      if (Serial) Serial.end();
    #endif
    waking = false;
    initMatrix();
  }
  if (scanWithDebounce()) {
    transmit();
    ticksSinceTransmit = ticksSinceDiff = 0;
  } else if (ticksSinceDiff > sleepAfterIdleTicks && state == 0) {
    #if DEBUG
      Serial.println("sleep");
      Serial.flush();
    #endif
    sleep();
    return;
  } else if (ticksSinceTransmit > repeatTransmitTicks) {
    transmit();
    ticksSinceDiff++;
    ticksSinceTransmit = 0;
  } else {
    ticksSinceDiff++;
    ticksSinceTransmit++;
  }
  delay(delayPerTick);
}
