#include "nrf.h"
#include "nrf_gzll.h"

#define SIDE 0
#define MARK 1
const uint8_t debug = 0;
const uint8_t num_keys = 21;

#if MARK == 1
  #if SIDE == 0
    const uint8_t pins[num_keys] = {
      PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0, 11,
      PIN_NFC2, PIN_SERIAL_TX, PIN_SERIAL_RX, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SCK, 12,
      PIN_WIRE_SDA, PIN_WIRE_SCL, 5, 6, 9, 10, 13,
    };
    const uint8_t pipe = 3;
  #else
    const uint8_t pins[num_keys] = {
      PIN_A2, 13, 10, 6, PIN_A5, PIN_SPI_MISO, PIN_NFC2,
      PIN_A1, 11, 9, 5, PIN_A4, PIN_SPI_MOSI, PIN_SERIAL_TX,
      PIN_A0, 12, PIN_WIRE_SDA, PIN_WIRE_SCL, PIN_A3, PIN_SPI_SCK, PIN_SERIAL_RX,
    };
    const uint8_t pipe = 4;
  #endif
#endif

const uint8_t delayPerTick = 2;
const uint8_t debounceTicks = 2;

///////////////////////////////////////////////////// MATRIX

const uint64_t ONE = 1;
uint64_t colMask; // Effectively const

volatile uint64_t stablePins = 0;
uint64_t debouncePins = 0;
int debounceCount = 0;

void computeColMask() {
  colMask = 0;
  for (int p = 0; p < num_keys; ++p) {
    colMask |= ONE << g_ADigitalPinMap[pins[p]];
  }
}

uint32_t getState() {
  uint32_t state = 0;
  for (int p = 0; p < num_keys; ++p) {
      if (stablePins & (ONE << g_ADigitalPinMap[pins[p]])) {
        state |= ONE << (p);
      }
  }
  return state;
}

void show(uint64_t x) {
  for (int i = 0; i < 64; i++) {
    Serial.print(x & (ONE<<i) ? 1 : 0);
  }
  Serial.println();
}

void show(uint32_t x) {
  for (int i = 0; i < 32; i++) {
    Serial.print(x & (1UL<<i) ? 1 : 0);
  }
  Serial.println();
}

#define CONTINUE 0
#define TRANSMIT 1
#define SLEEP 2

uint8_t scanWithDebounce() {
  uint64_t scan;
  ((uint32_t*)&scan)[0] = NRF_P0->IN;
  ((uint32_t*)&scan)[1] = NRF_P1->IN;
  scan = ~scan & colMask;
  bool scanToDebounceDiff = (scan != debouncePins);
  bool scanToStableDiff = (scan != stablePins);
  debouncePins = scan;
  if (scanToDebounceDiff) {
    // Start/restart debouncing.
    debounceCount = debounceTicks;
    return CONTINUE;
  }
  if (debounceCount > 0) {
    // Holding steady, no new bounces.
    debounceCount--;
    return CONTINUE;
  }
  if (!scanToStableDiff) {
    // Finished debouncing but no keys changed.
    return SLEEP;
  }
  // Finished debouncing and something changed.
  stablePins = debouncePins;
  return TRANSMIT;
}

///////////////////////////////////////////////////// CORE

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

const uint32_t SENSE_HIGH =
  ((uint32_t)GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos)
   | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)
   | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos)
   | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)
   | ((uint32_t)GPIO_PIN_CNF_SENSE_High << GPIO_PIN_CNF_SENSE_Pos);
const uint32_t SENSE_LOW =
  ((uint32_t)GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos)
   | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)
   | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos)
   | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)
   | ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);

inline void pinModeDetect(uint32_t pin) {
  pin = g_ADigitalPinMap[pin];
  NRF_GPIO_Type *port = nrf_gpio_pin_port_decode(&pin);
  port->PIN_CNF[pin] = (stablePins & (ONE << pin)) ? SENSE_HIGH : SENSE_LOW;
}

///////////////////////////////////////////////////// CALLBACKS

bool sleeping = true;

void scheduleNextWake() {
  if (debug) { Serial.println("schedule"); Serial.flush(); }
  delay(2);
  for (int p = 0; p < num_keys; ++p) {
    pinModeDetect(pins[p]);
  }
  debounceCount = debounceTicks;
  attachOneShotPortEventHandler(wake);
}

void wake() {
  if (debug) { Serial.println("wake"); Serial.flush(); }
  debounceCount = debounceTicks;
  if (sleeping) {
    sleeping = false;
    ada_callback(NULL, 0, doScan);
  }
}

void doScan() {
  delay(delayPerTick);
  switch(scanWithDebounce()) {
    case CONTINUE:
      ada_callback(NULL, 0, doScan);
      return;
    case TRANSMIT:
      if (debug) {
        Serial.println("transmit");
        Serial.flush();
      }
      transmit();
      return;
    case SLEEP:
      if (debug) {
        Serial.println("sleep");
        Serial.flush();
      }
      sleeping = true;
      ada_callback(NULL, 0, scheduleNextWake);
      return;
  }
}

void allPacketsHandled() {
  if (debug) {
    Serial.println("packets sent");
    Serial.flush();
  }
  sleeping = true;
  ada_callback(NULL, 0, scheduleNextWake);
}

void forceResend() {
  if (debug) {
    Serial.println("resend");
    Serial.flush();
  }
  transmit();
}

///////////////////////////////////////////////////// RADIO

#if SIDE == 0
  uint8_t channel_table[3] = {25, 63, 33};
#else
  uint8_t channel_table[3] = {4, 42, 77};
#endif
const uint16_t max_retries = 16;

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;

// Network state.
uint16_t retries = 0;
volatile uint16_t outstanding_packets = 0;

// Debug counters.
volatile uint32_t failed = 0;
volatile uint32_t sent = 0;
volatile uint32_t retried = 0;

void initRadio() {
  nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
  nrf_gzll_set_max_tx_attempts(100);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_0_DBM);
  nrf_gzll_enable();
}

void transmit() {
  outstanding_packets++;
  uint32_t state = getState();
  if (!nrf_gzll_add_packet_to_tx_fifo(pipe, (uint8_t*)&state, 4)) {
    outstanding_packets--;
  }
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  outstanding_packets--;
  sent++;
  retries = 0;
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
  if (outstanding_packets == 0) {
    ada_callback(NULL, 0, allPacketsHandled);
  }
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  outstanding_packets--;
  failed++;
  if (outstanding_packets == 0 && retries < max_retries) {
    retried++;
    retries++;
    // Have to call via callback because we're in an interrupt.
    ada_callback(NULL, 0, forceResend);
  }
}

void nrf_gzll_disabled() {
  outstanding_packets = 0;
}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

///////////////////////////////////////////////////// MAIN

void other1() {
  Serial.println("a");
  Serial.flush();
}

void other2() {
  Serial.println("b");
  Serial.flush();
}

void setup() {
  if (debug) {
    Serial.begin(115200);
  }
  setDebug(other1, other2);
  nfcAsGpio();
  initRadio();
  computeColMask();
  ada_callback(NULL, 0, scheduleNextWake);
}

void loop() {
  if (!debug && Serial) {
    Serial.end();
  }
  suspendLoop();
}