#include "nrf.h"
#include "nrf_gzll.h"

const uint8_t PIPE = 7;
const bool DEBUG = 0;

/////// RADIO

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t channel_table[3] = {4, 42, 77};

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
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
  if (DEBUG) Serial.println("X");
}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

/////// XMIT QUEUE

const uint16_t buffer_size = 1024;
volatile uint32_t midi_keys[buffer_size];
volatile uint32_t index_written = 0;
volatile uint32_t index_read = 0;

inline void transmitFromQueue() {
  while (index_read < index_written) {
    int num = min(8, index_written - index_read);
    num = min(num, buffer_size - index_read % buffer_size);
    if (nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)&midi_keys[index_read % buffer_size], 4 * num)) {
      index_read += num;
    } else {
      break;
    }
  }
}

inline void enqueue(uint8_t n, unsigned long t, bool down) {
  uint32_t msg = n;
  msg <<= 24;
  msg |= min(t/100, 0xffff) | (down ? 0x010000 : 0);
  midi_keys[index_written % buffer_size] = msg;
  index_written++;
}

/////// MATRIX

const uint8_t rows[8] = {16, 14, 17, 13, 18, 12, 19, 11};
const uint8_t cols1[8] = {31, 30, 29, 28, 25, 24, 20};
const uint8_t cols2[8] = {2, 3, 4, 5, 8, 9, 10};

const char name[][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

uint32_t last1[8], last2[8];
unsigned long time1[64], time2[64];
uint32_t rowsScan[8], cols1Scan[7], cols2Scan[7];
uint32_t rowsMask = 0, cols1Mask = 0, cols2Mask = 0;

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

inline void updateScan(unsigned long t, uint8_t i, uint32_t scan) {
  uint32_t scan1 = scan & cols1Mask;
  if (scan1 != last1[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint32_t b = cols1Scan[j];
      uint8_t k = 8 * i + j;
      if ((scan1 & b) && !(last1[i] & b)) { // released
        if (time2[k] != 0 && time1[k] == 0) {
          enqueue(note(i, j), t - time2[k], false);
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
          enqueue(note(i, j), t - time1[k], true);
        }
        time1[k] = time2[k] = 0;
      }
    }
    last2[i] = scan2;
  }
}

inline void scanMatrix() {
  for(uint8_t i = 0; i < 8; ++i) {
    uint32_t scan = NRF_GPIO->IN;
    NRF_GPIO->OUTSET = rowsMask;
    NRF_GPIO->OUTCLR = rowsScan[(i + 1) % 8];
    updateScan(micros(), i, scan);
  }
}

/////// MISC

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

void ledOff() {
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
}

/////// CORE LOOP

void setup() {
  if (DEBUG) {
    Serial.begin(115200);
    Serial.println("online");
  } else {
    Serial.end();
  }
  nfcAsGpio();
  initMatrix();
  initRadio();
  ledOff();
  delay(1);
}

void loop() {
  scanMatrix();
  transmitFromQueue();
  delay(1);
}
