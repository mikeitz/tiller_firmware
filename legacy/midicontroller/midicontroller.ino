#include "nrf.h"
#include "nrf_gzll.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads1115;

#define PIPE 6
#define DEBUG 0

///////////////////////////////////////////////////// CONTROLLERS

class Controller {
 public:
  Controller(int pin, int cc_num, int min_val, int max_val)
    : cc_num_(cc_num), pin_(pin), min_val_(min_val), max_val_(max_val) {}
  void Update(uint32_t data[], uint8_t* n) {
    int32_t sense = ads1115.readADC_SingleEnded(pin_);
    if (sense < min_val_) sense = min_val_;
    if (sense > max_val_) sense = max_val_;
      if (sense == last_sense_) {
      return;
    }
    uint32_t cc = (sense - min_val_) * 127 / (max_val_ - min_val_);
  
    if (cc != last_cc_ && abs((int)last_sense_ - (int)sense) > 7) {
      last_sense_ = sense;
      last_cc_ = cc;

      uint32_t msg = (3 /*cc*/ << 28) | (cc_num_ << 16) | last_cc_;
      
      data[*n] = msg;
      *n += 1;

      #if DEBUG
      Serial.print(msg, HEX);
      Serial.print(" ");
      Serial.print(cc_num_);
      Serial.print(" ");
      Serial.println(sense);
      #endif
    }
  }
 private:
  uint8_t pin_;
  uint8_t cc_num_;
  uint32_t last_sense_ = 0;
  uint32_t last_cc_ = 0;
  int32_t min_val_ = 0, max_val_ = 0;
};

auto fader_1 = Controller(0, 0, 0, 17650);
auto fader_2 = Controller(1, 1, 0, 17650);
auto joy_x = Controller(2, 3, 650, 16000);
auto joy_y = Controller(3, 2, 650, 16000);

///////////////////////////////////////////////////// KEYS

const uint8_t num_rows = 4;
const uint8_t num_cols = 4;
const uint8_t num_keys = num_rows * num_cols;
const uint8_t keys[num_keys] = {
  PIN_A1, PIN_A2, PIN_A3, PIN_A4,
  13, 12, 11, 5,
  PIN_A5, 10, 9, 6,
  PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SERIAL1_RX, PIN_SERIAL1_TX
};
const uint8_t delay_per_tick = 2;
const uint8_t debounce_ticks = 0;

const uint64_t ONE = 1;
uint64_t key_mask; // Effectively const;

uint64_t stable, lastStable;
uint64_t debounce;
uint8_t debounce_count = 0;

void computeColMask() {
  key_mask = 0;
  for (int i = 0; i < num_keys; ++i) {
    key_mask |= ONE << g_ADigitalPinMap[keys[i]];
  }
}

void initMatrix() {
  // Since we woke up, assume we're bouncing against idle.
  debounce_count = debounce_ticks;
  debounce = 0;
  stable = 0;
  lastStable = 0;
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
  lastStable = stable;
  stable = debounce;
  bool keys_down = stable != 0;
  return (scan_to_stable_diff ? CHANGED : 0) | (keys_down ? ACTIVE : 0);
}

void initKeys() {
  computeColMask();
  for (int i = 0; i < num_keys; ++i) {
    pinMode(keys[i], INPUT_PULLUP_SENSE);
  }
}

///////////////////////////////////////////////////// RADIO
    
uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint8_t channel_table[3] = {4, 42, 77};

uint32_t data[100];

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

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

///////////////////////////////////////////////////// CORE

void setup() {
  #if DEBUG
    Serial.begin(9600);
  #endif
  ads1115.begin();
  initKeys();
  delay(100);
  initRadio();
  delay(100);
}

int updatePhase = 0;
const int updatePhases = 16;
const int keyOffset = 0;

void loop() {
  uint8_t num = 0;

  updatePhase++;
  if (updatePhase == updatePhases) {
    updatePhase = 0;
  }
  switch(updatePhase) {
    case 0:
      fader_1.Update(data, &num);
      break;
    case 4:
      fader_2.Update(data, &num);
      break;
    case 8:
      joy_x.Update(data, &num);
      break;
    case 12:
      joy_y.Update(data, &num);
      break;
    default:
      delay(delay_per_tick);
      break;
  }

  const uint8_t verdict = scanWithDebounce();
  if (verdict & BOUNCING) {
    // No-op.
  } else if (verdict & CHANGED) {
    for (int i = 0; i < num_keys; ++i) {
      uint64_t mask = ONE << g_ADigitalPinMap[keys[i]];
      if ((stable & mask) && !(lastStable & mask)) {
        // Key down.
        data[num++] = (1 << 28) | ((i + keyOffset) << 16) | 0x7F;
      }
      if (!(stable & mask) && (lastStable & mask)) {
        // Key up.
        data[num++] = (2 << 28) | ((i + keyOffset) << 16) | 0x00;
      }
    }
  } else if (verdict & ACTIVE) {
    // Keys held down.  TODO: resends?
  }
  
  if (num) {
    nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)data, num * 4);
  }
}
