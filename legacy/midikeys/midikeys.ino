#include "nrf.h"
#include "nrf_gzll.h"

const uint8_t PIPE = 7;
const bool DEBUG = 0;

/////// RADIO

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t channel_table[6] = { 4, 25, 42, 63, 77, 33 };

void initRadio() {
  nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
  nrf_gzll_set_max_tx_attempts(100);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_channel_table(channel_table, 6);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_4_DBM);
  nrf_gzll_enable();
}

const uint16_t max_resends = 1000;
volatile uint16_t resends = 0;
volatile uint16_t outstanding_packets = 0;
volatile bool force_resend = false;

uint8_t key_down_count = 0;
uint8_t key_packet[34] = { 0 }; // Last two bytes always zero.

inline void transmit() {
  if (nrf_gzll_get_tx_fifo_packet_count(PIPE) > 0) {
    force_resend = true;
    return;
  }
  outstanding_packets++;
  if (!nrf_gzll_add_packet_to_tx_fifo(PIPE, key_packet, key_down_count * 2)) {
    force_resend = true;
    outstanding_packets--;
  }
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack) {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
  resends = 0;
  outstanding_packets--;
}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  outstanding_packets--;
  if (DEBUG) Serial.println("X");
  if (outstanding_packets > 0) {
    // A later packet will catch up with this info.
    return;
  }
  if (resends < max_resends) {
    resends++;
    force_resend = true;
    return;
  }
  if (DEBUG) Serial.println("!!!");
}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

/////// XMIT QUEUE

bool key_changed = false;

uint8_t last_vel = 1;

void keyDown(uint8_t note, uint32_t time) {
  uint8_t vel = last_vel = time > 0 ? timeToVelocity(time) : last_vel;
  for (int i = 0; i < 32; i += 2) {
    if (key_packet[i] == 0) {
      key_packet[i] = note;
      key_packet[i + 1] = vel;
      key_down_count++;
      key_changed = true;
      return;
    }
  }
}

void keyUp(uint8_t note, uint32_t time) {
  for (int i = 0; i < 32; i += 2) {
    if (key_packet[i] == 0) {
      return;
    }
    if (key_packet[i] == note) {
      key_changed = true;
      key_down_count--;
      for (; i < 32 && key_packet[i] != 0; i += 2) {
        key_packet[i] = key_packet[i + 2];
        key_packet[i + 1] = key_packet[i + 3];
      }
      return;
    }
  }
}

/////// MATRIX

const uint8_t rows[8] = { 16, 14, 17, 13, 18, 12, 19, 11 };
const uint8_t cols1[8] = { 31, 30, 29, 28, 25, 24, 20 };
const uint8_t cols2[8] = { 2, 3, 4, 5, 8, 9, 10 };

const char name[][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

uint32_t last1[8], last2[8];
unsigned long time1[64], time2[64];
uint32_t rowsScan[8], cols1Scan[7], cols2Scan[7];
uint32_t rowsMask = 0, cols1Mask = 0, cols2Mask = 0;

// 20000 ~= 0x10 velocity
// 2000 = 0x7f velocity
// 120000 ~= 0x01 velocity

static inline uint8_t timeToVelocity(int32_t t) {
  int vel = 150 * exp(t * -0.00005f);
  if (vel < 1) return 1;
  if (vel > 127) return 127;
  return vel;

  /*
    const int VMIN = 3000;
  const int VMAX = 50000;
  t = (t - VMIN) * 127 / VMAX;
  t = 127 - t;
  if (t < 1) return 1;
  if (t > 127) return 127;
  return t;*/
}

inline uint8_t note(uint8_t i, uint8_t j) {
  return j * 8 + i + 32;
}

void initMatrix() {
  memset(last1, 255, 8 * 4);
  memset(last2, 255, 8 * 4);
  memset(time1, 0, 64 * 4);
  memset(time2, 0, 64 * 4);
  for (int i = 0; i < 7; ++i) {
    pinMode(cols1[i], INPUT_PULLUP);
    pinMode(cols2[i], INPUT_PULLUP);
    cols1Scan[i] = (1UL << cols1[i]);
    cols2Scan[i] = (1UL << cols2[i]);
    cols1Mask |= cols1Scan[i];
    cols2Mask |= cols2Scan[i];
  }
  for (int i = 0; i < 8; ++i) {
    pinMode(rows[i], OUTPUT);
    rowsMask |= (rowsScan[i] = (1UL << rows[i]));
  }
  NRF_GPIO->OUTSET = rowsMask;
  NRF_GPIO->OUTCLR = rowsScan[0];
}

inline bool updateScan(unsigned long t, uint8_t i, uint32_t scan) {
  bool key_changed = false;
  uint32_t scan1 = scan & cols1Mask;
  if (scan1 != last1[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint32_t b = cols1Scan[j];
      uint8_t k = 8 * i + j;
      if ((scan1 & b) && !(last1[i] & b)) { // released
        if (time2[k] != 0 && time1[k] == 0) {
          keyUp(note(i, j), time2[k]);
          key_changed = true;
        }
        time1[k] = time2[k] = 0;
      }
      if (!(scan1 & b) && (last1[i] & b)) { // pressed
        time1[k] = t;
      }
    }
    last1[i] = scan1;
  }
  uint32_t scan2 = scan & cols2Mask;
  if (scan2 != last2[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint32_t b = cols2Scan[j];
      uint8_t k = 8 * i + j;
      if ((scan2 & b) && !(last2[i] & b)) { // released
        time2[k] = t;
      }
      if (!(scan2 & b) && (last2[i] & b)) { // pressed
        if (time1[k] != 0 && time2[k] == 0) {
          keyDown(note(i, j), t - time1[k]);
          key_changed = true;
        }
        time1[k] = time2[k] = 0;
      }
    }
    last2[i] = scan2;
  }
  return key_changed;
}

inline bool scanMatrix() {
  bool key_changed = false;
  for (uint8_t i = 0; i < 8; ++i) {
    uint32_t scan = NRF_GPIO->IN;
    NRF_GPIO->OUTSET = rowsMask;
    NRF_GPIO->OUTCLR = rowsScan[(i + 1) % 8];
    key_changed |= updateScan(micros(), i, scan);
  }
  return key_changed;
}

/////// MISC

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

void ledOff() {
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
}

/////// CORE LOOP

void setup() {
  if (DEBUG) {
    Serial.begin(9600);
    Serial.println("online");
  } else {
    Serial.end();
  }
  nfcAsGpio();
  initMatrix();
  initRadio();
  ledOff();
  delay(100);
}

bool has_outstanding = false;

void loop() {
  if (scanMatrix() || force_resend) {
    force_resend = false;
    transmit();
  }
  delay(1);
}
