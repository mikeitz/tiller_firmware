#include "nrf.h"
#include "nrf_gzll.h"

#define SIDE 1
#define MARK 4
const uint8_t debug = 0;

const uint8_t num_rows = 3;
const uint8_t num_cols = 7;
const uint8_t keys = num_rows * num_cols;

#if MARK == 4
  #if SIDE == 0
    const uint8_t rows[num_rows] = {PIN_A3, PIN_A4, PIN_A5};
    const uint8_t cols[num_cols] = {PIN_SPI_MISO, PIN_A2, PIN_SPI_MOSI, PIN_A1, PIN_SPI_SCK, PIN_A0, PIN_NFC2};
    const uint8_t pipe = 1;
  #else
    const uint8_t rows[num_rows] = {10, 9, 6};
    const uint8_t cols[num_cols] = {PIN_NFC2, 13, 5, 12, PIN_WIRE_SCL, 11, PIN_WIRE_SDA};
    const uint8_t pipe = 2;
  #endif
#endif

#if MARK == 3
  #if SIDE == 0
    const uint8_t rows[num_rows] = {13, 12, 11};
    const uint8_t cols[num_cols] = {PIN_SPI_SCK, PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0};
    const uint8_t pipe = 3;
  #else
    const uint8_t rows[num_rows] = {13, 12, 11};
    const uint8_t cols[num_cols] = {PIN_SPI_SCK, PIN_A5, PIN_A4, PIN_A3, PIN_A2, PIN_A1, PIN_A0};
    const uint8_t pipe = 4;
  #endif
#endif

#if MARK == 2
  #if SIDE == 0
    const uint8_t rows[num_rows] = {13, 12, 11};
    const uint8_t cols[num_cols] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};
    const uint8_t pipe = 1;
  #else
    const uint8_t rows[num_rows] = {13, 12, 11};
    const uint8_t cols[num_cols] = {PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5, PIN_SPI_SCK};
    const uint8_t pipe = 2;
  #endif
#endif

const uint8_t delayPerTick = 1;
const uint8_t debounceTicks = 4;

const uint16_t sleepAfterIdleTicks = 1000/delayPerTick;
const uint16_t repeatTransmitTicks = 500/delayPerTick;

///////////////////////////////////////////////////// MATRIX

uint64_t stableRows[num_rows];
uint64_t debounceRows[num_rows];
uint64_t colMask;
const uint64_t ONE = 1;
int debounceCount = 0;
bool keysDown = false;

#define NOP __asm__("nop\n\t")

void computeColMask() {
  colMask = 0;
  for (int c = 0; c < num_cols; ++c) {
    colMask |= ONE << g_ADigitalPinMap[cols[c]];
  }
}

uint32_t getState() {
  uint32_t state = 0;
  for (int r = 0; r < num_rows; ++r) {
    for (int c = 0; c < num_cols; ++c) {
      if (stableRows[r] & (ONE << g_ADigitalPinMap[cols[c]])) {
        state |= ONE << (r * num_cols + c);
      }
    }
  }
  return state;
}

void printMatrix(uint32_t state) {
  if (!debug) {
    return;
  }
  Serial.println("");
  for (uint8_t r = 0; r < num_rows; r++) {
    for (uint8_t c = 0; c < num_cols; c++) {
      Serial.print(1 & (state >> (r * num_cols + c)));
    }
    Serial.print(" ");
  }
  Serial.println("");
  Serial.flush();
}

void initMatrix() {
  for (int r = 0; r < num_rows; ++r) {
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], HIGH);
    debounceRows[r] = 0;
    stableRows[r] = 0;
  }
  for (int c = 0; c < num_cols; ++c) {
    pinMode(cols[c], INPUT_PULLUP);
  }
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
  bool scanToDebounceDiff = false;
  bool scanToStableDiff = false;
  for (int r = 0; r < num_rows; ++r) {
    digitalWrite(rows[r], LOW);
    uint64_t scan = NRF_P1->IN;
    scan <<= 32;
    scan |= NRF_P0->IN;
    digitalWrite(rows[r], HIGH);
    //if(!r)show(scan);
    scan = ~scan & colMask;
    scanToDebounceDiff = scanToDebounceDiff || (scan != debounceRows[r]);
    scanToStableDiff = scanToStableDiff || (scan != stableRows[r]);
    debounceRows[r] = scan;
  }
  if (!scanToStableDiff) {
    debounceCount = 0;
    return false;
  }
  if (scanToDebounceDiff) {
    debounceCount = debounceTicks;
    return false;
  }
  if (debounceCount > 0) {
    debounceCount--;
    return false;
  }
  keysDown = false;
  for (int r = 0; r < num_rows; ++r) {
    stableRows[r] = debounceRows[r];
    keysDown = keysDown || (stableRows[r] != 0);
  }
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
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENCLR |= GPIOTE_INTENSET_PORT_Msk;
  if (!sleeping) return;
  sleeping = false;
  waking = true;
  resumeLoop();
}

void initCore() {
  sleeping = false;
  waking = true;
  nfcAsGpio();
  NVIC_DisableIRQ(GPIOTE_IRQn);
  NVIC_ClearPendingIRQ(GPIOTE_IRQn);
  NVIC_SetPriority(GPIOTE_IRQn, 3);
  NVIC_EnableIRQ(GPIOTE_IRQn);
  attachCustomInterruptHandler(wake);
}

///////////////////////////////////////////////////// RADIO

uint8_t channel_table[3] = {4, 42, 77};
uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;

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
  uint32_t state = getState();
  // printMatrix(state);  
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

uint16_t ticksSinceDiff = 0;
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
  if (sleeping) {
    delay(delayPerTick);
  } else if (waking) {
    waking = false;
    if (!debug && Serial) {
      Serial.end();
    }
    initMatrix();
    transmit();
    ticksSinceDiff = 0;
    ticksSinceTransmit = 0;
  } else if (scanWithDebounce()) {
    if (debug) { show(getState()); Serial.flush(); }
    transmit();
    ticksSinceDiff = 0;
    delay(delayPerTick);
  } else if (ticksSinceDiff > sleepAfterIdleTicks && !keysDown && debounceCount == 0) {
    sleep();
  } else if (ticksSinceTransmit > repeatTransmitTicks) {
    transmit();
    ticksSinceDiff++;
    ticksSinceTransmit = 0;
    delay(delayPerTick);
  } else {
    ticksSinceDiff++;
    ticksSinceTransmit++;
    delay(delayPerTick);
  }
}