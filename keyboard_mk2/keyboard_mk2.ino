#include "nrf.h"
#include "nrf_gzll.h"

#define SIDE 0
#define MARK 1
const uint8_t debug = 0;

const uint8_t num_rows = 3;
const uint8_t num_cols = 7;
const uint8_t num_extra = 3;
const uint8_t keys = num_rows * num_cols;

#if MARK == 1
  #if SIDE == 0
    const uint8_t pipe = 1;
    const uint8_t rows[num_rows] = {9, PIN_SERIAL1_TX, PIN_SPI_MOSI};
    const uint8_t cols[num_cols] = {PIN_SPI_MISO, PIN_SPI_SCK, PIN_WIRE_SCL, 10, 12, 13, PIN_A4 /* DUMMY */};
    const uint8_t extra_rows[num_extra] = {0, 1, 2};
    const uint8_t extra_cols[num_extra] = {6, 6, 6};
    const uint8_t extra_pins[num_extra] = {PIN_A0, PIN_A1, PIN_A2};
    const uint8_t extra_ground = PIN_A3;
  #else
    const uint8_t pipe = 2;
    const uint8_t rows[num_rows] = {PIN_SPI_SCK, 7, PIN_SERIAL1_RX};
    const uint8_t cols[num_cols] = {PIN_A4 /* DUMMY */, PIN_A0, PIN_A1, PIN_A2, PIN_A5, PIN_SPI_MOSI, PIN_SERIAL1_TX};
    const uint8_t extra_rows[num_extra] = {0, 1, 2};
    const uint8_t extra_cols[num_extra] = {0, 0, 0};
    const uint8_t extra_pins[num_extra] = {11, 12, 13};
    const uint8_t extra_ground = 10;
  #endif
#endif

const uint8_t delay_per_tick = 2;
const uint8_t debounce_ticks = 2;
const uint16_t repeat_transmit_ticks = 5000/delay_per_tick;
const uint16_t max_resends = 1000/delay_per_tick;

///////////////////////////////////////////////////// MATRIX

const uint64_t ONE = 1;
uint64_t col_mask; // Effectively const
uint64_t extra_mask; // Effectively const;

uint64_t stable_rows[num_rows];
uint64_t debounce_rows[num_rows];
uint64_t stable_extra;
uint64_t debounce_extra;
uint8_t debounce_count = 0;

void computeColMask() {
  col_mask = 0;
  for (int c = 0; c < num_cols; ++c) {
    col_mask |= ONE << g_ADigitalPinMap[cols[c]];
  }
  extra_mask = 0;
  for (int i = 0; i < num_extra; ++i) {
    extra_mask |= ONE << g_ADigitalPinMap[extra_pins[i]];
  }
}

uint32_t getState() {
  uint32_t state = 0;
  for (int r = 0; r < num_rows; ++r) {
    for (int c = 0; c < num_cols; ++c) {
      if (stable_rows[r] & (ONE << g_ADigitalPinMap[cols[c]])) {
        state |= ONE << (r * num_cols + c);
      }
    }
  }
  for (int i = 0; i < num_extra; ++i) {
    if (stable_extra & (ONE << g_ADigitalPinMap[extra_pins[i]])) {
      state |= ONE << (extra_rows[i] * num_cols + extra_cols[i]);
    }
  }
  return state;
}

void initMatrix() {
  for (int r = 0; r < num_rows; ++r) {
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], HIGH);
    debounce_rows[r] = 0;
    stable_rows[r] = 0;
  }
  // Prepare for read of first row.
  digitalWrite(rows[0], LOW);
  // Since we woke up, assume we're bouncing against idle.
  debounce_count = debounce_ticks;
  debounce_extra = 0;
  stable_extra = 0;
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

#define BOUNCING 1
#define CHANGED 2
#define ACTIVE 4

uint8_t scanWithDebounce() {
  bool scan_to_debounce_diff = false;
  bool scan_to_stable_diff = false;
  uint64_t scan;
  uint64_t masked_row;
  for (int r = 0; r < num_rows; ++r) {
    ((uint32_t*)&scan)[0] = NRF_P0->IN;
    ((uint32_t*)&scan)[1] = NRF_P1->IN;
    // Update rows off cycle to give pins time to settle.
    digitalWrite(rows[r], HIGH);
    digitalWrite(rows[(r+1) % num_rows], LOW);
    scan = ~scan;
    masked_row = scan & col_mask;
    scan_to_debounce_diff = scan_to_debounce_diff || (masked_row != debounce_rows[r]);
    scan_to_stable_diff = scan_to_stable_diff || (masked_row != stable_rows[r]);
    debounce_rows[r] = masked_row;
  }
  uint64_t maskedExtra = scan & extra_mask;
  scan_to_debounce_diff = scan_to_debounce_diff || (maskedExtra != debounce_extra);
  scan_to_stable_diff = scan_to_stable_diff || (maskedExtra != stable_extra);
  debounce_extra = maskedExtra;

  if (scan_to_debounce_diff) {
    // Start/restart debouncing.
    debounce_count = debounce_ticks;
    return BOUNCING;
  } else if (debounce_count > 0) {
    // Holding steady, no new bounces.
    debounce_count--;
    return BOUNCING;
  }

  stable_extra = debounce_extra;
  bool keys_down = stable_extra != 0;
  for (int r = 0; r < num_rows; ++r) {
    stable_rows[r] = debounce_rows[r];
    keys_down = keys_down || (stable_rows[r] != 0);
  }

  return (scan_to_stable_diff ? CHANGED : 0) | (keys_down ? ACTIVE : 0);
}

///////////////////////////////////////////////////// CORE

volatile bool sleeping = false;
volatile bool waking = true;
volatile bool force_resend = false;
volatile uint8_t outstanding_packets = 0;

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

void sleep() {
  if (sleeping) return;
  sleeping = true;
  for (int r = 0; r < num_rows; ++r) {
    digitalWrite(rows[r], LOW);
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
  pinMode(extra_ground, OUTPUT);
  digitalWrite(extra_ground, 0);
  nfcAsGpio();
  for (int c = 0; c < num_cols; ++c) {
    pinMode(cols[c], INPUT_PULLUP_SENSE);
  }
  for (int i = 0; i < num_extra; ++i) {
    pinMode(extra_pins[i], INPUT_PULLUP_SENSE);
  }
}

///////////////////////////////////////////////////// RADIO

uint8_t resends = 0;

void initRadio() {
  #if SIDE == 0
    static uint8_t channel_table[3] = {25, 63, 33};
  #else
    static uint8_t channel_table[3] = {4, 42, 77};
  #endif
  delay(1000);
  nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
  nrf_gzll_set_max_tx_attempts(100);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_N8_DBM);
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
  static uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
  resends = 0;
  outstanding_packets--;
}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  if (outstanding_packets == 0 && resends < max_resends) {
    resends++;
    force_resend = true;
  }
  outstanding_packets--;
}
void nrf_gzll_disabled() {
  outstanding_packets = 0;
}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

///////////////////////////////////////////////////// MAIN

void setup() {
  if (debug) {
    Serial.begin(115200);
  }
  initCore();
  initRadio();
  computeColMask();
}

void loop() {

  static uint16_t ticks_since_transmit = 0;

  delay(delay_per_tick);
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
    ticks_since_transmit = 0;
  }

  const uint8_t verdict = scanWithDebounce();

  if (verdict & BOUNCING) {
    return;
  }

  if ((verdict & CHANGED) || force_resend) {
    if (debug) {
      if (force_resend) { Serial.print("! "); }
      show(getState());
      Serial.flush();
    }
    force_resend = false;
    ticks_since_transmit = 0;
    transmit();
  } else if (verdict & ACTIVE) {
    if (ticks_since_transmit > repeat_transmit_ticks) {
      if (debug) { Serial.println("xmit"); Serial.flush(); }
      transmit();
      ticks_since_transmit = 0;
    } else {
      ticks_since_transmit++;
    }
  }

  // If no keys are down and the last packet we sent was acked, sleep.
  if (!(verdict & ACTIVE) && outstanding_packets == 0 && !force_resend) {
    if (debug) { Serial.println("sleep"); Serial.flush(); }
    sleep();
  }
}