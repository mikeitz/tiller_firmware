#include "nrf.h"
#include "nrf_gzll.h"
#include "Adafruit_TinyUSB.h"

#define SIDE 1
#define MARK 2
const uint8_t debug = 0;

#if MARK == 1 // Pin-per-key first version of 3d contoured board.
const uint8_t num_keys = 21;
#if SIDE == 0
const uint8_t pipe = 1;
const uint8_t keys[num_keys] = {
  6, 5, 9, PIN_WIRE_SCL, PIN_WIRE_SDA, PIN_NFC2, PIN_A2,
  PIN_SPI_SCK, 10, 13, 12, 11, PIN_SERIAL1_TX, PIN_A1,
  PIN_A4, PIN_A3, PIN_A5, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SERIAL1_RX, PIN_A0
};
#else
const uint8_t pipe = 2;
const uint8_t keys[num_keys] = {
  PIN_A0, PIN_WIRE_SDA, PIN_WIRE_SCL, PIN_NFC2, PIN_SERIAL1_RX, PIN_SPI_MOSI, PIN_SERIAL1_TX,
  12, 6, 5, PIN_SPI_SCK, PIN_A5, PIN_SPI_MISO, PIN_A2,
  11, 10, 9, PIN_A4, PIN_A3, 13, PIN_A1
};
#endif
#endif

#if MARK == 2 // Pin-per-key 4x4 numpad.
const uint8_t num_keys = 16;
const uint8_t pipe = 3;
const uint8_t keys[num_keys] = {
  PIN_017, PIN_002, PIN_115, PIN_029,
  PIN_020, PIN_111, PIN_113, PIN_031,
  PIN_022, PIN_011, PIN_106, PIN_010,
  PIN_024, PIN_100, PIN_104, PIN_009,
};
#endif

const uint8_t delay_per_tick = 2;
const uint8_t debounce_ticks = 2;
const uint16_t repeat_transmit_ticks = 5000 / delay_per_tick;
const uint16_t max_resends = 1000 / delay_per_tick;

///////////////////////////////////////////////////// MATRIX

const uint64_t ONE = 1;
uint64_t key_mask; // Effectively const;

uint64_t stable;
uint64_t debounce;
uint8_t debounce_count = 0;

void computeColMask() {
  key_mask = 0;
  for (int i = 0; i < num_keys; ++i) {
    key_mask |= ONE << g_ADigitalPinMap[keys[i]];
  }
}

uint32_t getState() {
  uint32_t state = 0;
  for (int i = 0; i < num_keys; ++i) {
    if (stable & (ONE << g_ADigitalPinMap[keys[i]])) {
      state |= ONE << i;
    }
  }
  return state;
}

void initMatrix() {
  // Since we woke up, assume we're bouncing against idle.
  debounce_count = debounce_ticks;
  debounce = 0;
  stable = 0;
}

void show(uint64_t x) {
  for (int i = 0; i < 64; i++) {
    Serial.print(x & (ONE << i) ? 1 : 0);
  }
  Serial.println();
}

void show(uint32_t x) {
  for (int i = 0; i < 32; i++) {
    Serial.print(x & (1UL << i) ? 1 : 0);
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

  ((uint32_t*)&scan)[0] = NRF_P0->IN;
  ((uint32_t*)&scan)[1] = NRF_P1->IN;

  scan = ~scan;
  uint64_t masked = scan & key_mask;
  scan_to_debounce_diff = masked != debounce;
  scan_to_stable_diff = masked != stable;
  debounce = masked;

  if (scan_to_debounce_diff) {
    // Start/restart debouncing.
    debounce_count = debounce_ticks;
    return BOUNCING;
  } else if (debounce_count > 0) {
    // Holding steady, no new bounces.
    debounce_count--;
    return BOUNCING;
  }

  stable = debounce;
  bool keys_down = stable != 0;
  return (scan_to_stable_diff ? CHANGED : 0) | (keys_down ? ACTIVE : 0);
}

///////////////////////////////////////////////////// CORE

volatile bool sleeping = false;
volatile bool waking = true;
volatile bool force_resend = false;
volatile uint8_t outstanding_packets = 0;

void nfcAsGpio() {
  if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NVIC_SystemReset();
  }
}

void sleep() {
  if (sleeping) return;
  sleeping = true;
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
  for (int i = 0; i < num_keys; ++i) {
    pinMode(keys[i], INPUT_PULLUP_SENSE);
  }
}

///////////////////////////////////////////////////// RADIO

uint8_t resends = 0;

void initRadio() {
#if SIDE == 0
  static uint8_t channel_table[3] = { 25, 63, 33 };
#else
  static uint8_t channel_table[3] = { 4, 42, 77 };
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
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_4_DBM);
  nrf_gzll_enable();
}

void transmit() {
  outstanding_packets++;
  uint32_t state = getState();
  if (!nrf_gzll_add_packet_to_tx_fifo(pipe, (uint8_t*)&state, 4)) {
    force_resend = true;
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
  outstanding_packets--;
  if (outstanding_packets == 0 && resends < max_resends) {
    resends++;
    force_resend = true;
  }
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
