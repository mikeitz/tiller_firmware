#include "nrf.h"
#include "nrf_gzll.h"

#define SIDE 0
#define MARK 1
const uint8_t debug = 0;

const uint8_t num_rows = 3;
const uint8_t num_cols = 7;
const uint8_t keys = num_rows * num_cols;

#if MARK == 1
  #if SIDE == 0
    const uint8_t pins[keys] = {
      PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0, 11,
      PIN_NFC2, PIN_SERIAL_TX, PIN_SERIAL_RX, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SCK, 12,
      PIN_WIRE_SDA, PIN_WIRE_SCL, 5, 6, 9, 10, 13,
    };
    const uint8_t pipe = 3;
  #else
    const uint8_t pins[keys] = {
      PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0, 11,
      PIN_NFC2, PIN_SERIAL_TX, PIN_SERIAL_RX, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SCK, 12,
      PIN_WIRE_SDA, PIN_WIRE_SCL, 5, 6, 9, 10, 13,
    };
    const uint8_t pipe = 4;
  #endif
#endif

const uint8_t delayPerTick = 2;
const uint8_t debounceTicks = 3;

const uint16_t sleepAfterIdleTicks = 500/delayPerTick;
const uint16_t repeatTransmitTicks = 200/delayPerTick;

///////////////////////////////////////////////////// MATRIX

const uint64_t ONE = 1;
uint64_t colMask; // Effectively const

uint64_t stablePins;
uint64_t debouncePins;
int debounceCount = 0;
bool keysDown = false;

void computeColMask() {
  colMask = 0;
  for (int p = 0; p < keys; ++p) {
    colMask |= ONE << g_ADigitalPinMap[pins[p]];
  }
}

uint32_t getState() {
  uint32_t state = 0;
  for (int p = 0; p < keys; ++p) {
      if (stablePins & (ONE << g_ADigitalPinMap[pins[p]])) {
        state |= ONE << (p);
      }
  }
  return state;
}

void initMatrix() {
  for (int p = 0; p < keys; ++p) {
    pinMode(pins[p], INPUT_PULLUP);
  }
  debouncePins = 0;
  stablePins = 0;
  debounceCount = 0;
  keysDown = false;
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

bool scanWithDebounce() {
  uint64_t scan;
  ((uint32_t*)&scan)[0] = NRF_P0->IN;
  ((uint32_t*)&scan)[1] = NRF_P1->IN;
  scan = ~scan & colMask;
  bool scanToDebounceDiff = scanToDebounceDiff || (scan != debouncePins);
  bool scanToStableDiff = scanToStableDiff || (scan != stablePins);
  debouncePins = scan;
  if (scanToDebounceDiff) {
    // Start/restart debouncing.
    debounceCount = debounceTicks;
    return false;
  }
  if (debounceCount > 0) {
    // Holding steady, no new bounces.
    debounceCount--;
    return false;
  }
  if (!scanToStableDiff) {
    // Finished debouncing but no keys changed.
    return false;
  }
  // Finished debouncing and something changed.
  stablePins = debouncePins;
  keysDown = stablePins != 0;
  return true;
}

///////////////////////////////////////////////////// CORE

volatile bool sleeping = false;
volatile bool waking = true;

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
  for (int p = 0; p < keys; ++p) {
    pinModeDetect(pins[p]);
  }
  attachOneShotPortEventHandler(wake);
  suspendLoop();
}

void wake() {
  if (!sleeping) return;
  sleeping = false;
  waking = true;
  resumeLoop();
}

void initCore() {
  sleeping = false;
  waking = true;
  nfcAsGpio();
}

///////////////////////////////////////////////////// RADIO

#if SIDE == 0
  uint8_t channel_table[3] = {25, 63, 33};
#else
  uint8_t channel_table[3] = {4, 42, 77};
#endif

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;

uint16_t outstanding_packets = 0;

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
  outstanding_packets++;
  uint32_t state = getState();
  if (!nrf_gzll_add_packet_to_tx_fifo(pipe, (uint8_t*)&state, 4)) {
    outstanding_packets--;
  }
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
  outstanding_packets--;
}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  outstanding_packets--;
}
void nrf_gzll_disabled() {
  outstanding_packets = 0;
}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

///////////////////////////////////////////////////// MAIN

uint16_t ticksSinceActivity = 0;
uint16_t ticksSinceTransmit = 0;

void setup() {
  if (debug) {
    Serial.begin(115200);
  }
  initCore();
  initRadio();
  computeColMask();
}

void loop() {
  delay(delayPerTick);
  if (sleeping) {
    return;
  }

  if (waking) {
    if (debug) { Serial.println("wake"); Serial.flush(); }
    waking = false;
    if (!debug && Serial) {
      Serial.end();
    }
    initMatrix();
    ticksSinceActivity = 0;
    ticksSinceTransmit = 0;
  }

  if (scanWithDebounce()) {
    if (debug) { show(getState()); Serial.flush(); }
    transmit();
    ticksSinceTransmit = 0;
  } else if (ticksSinceTransmit > repeatTransmitTicks) {
    if (debug) { Serial.println("xmit"); Serial.flush(); }
    transmit();
    ticksSinceTransmit = 0;
  } else {
    ticksSinceTransmit++;
  }

  if (keysDown || debounceCount > 0) {
    ticksSinceActivity = 0;
  } else if (ticksSinceActivity > sleepAfterIdleTicks) {
    if (debug) { Serial.println("sleep"); Serial.flush(); }
    sleep();
    return;
  } else {
    ticksSinceActivity++;
  }
}