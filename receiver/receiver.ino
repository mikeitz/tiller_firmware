#include "nrf_gzll.h"
#include "Wire.h"

const uint8_t num_cols = 7;
const uint8_t num_rows = 8; // If num_rows > 8, need to use bigger types for matrix.
const uint8_t CHECK_BYTE = 0x55;
uint8_t matrix[num_rows + 1];

#define I2C_ADDRESS 2

static uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
extern nrf_gzll_error_code_t nrf_gzll_error_code;
static uint8_t channel_table[6] = {4, 25, 42, 63, 77, 33};

unsigned long lastPacket = 0;

static uint8_t new_data[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];

static uint8_t per_pipe_data[8][NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
static uint32_t per_pipe_length[8];
static unsigned long per_pipe_millis[8];

#define halfBit(r, c) (((uint32_t)1) << (c + r * num_cols))
#define getHalf(half, r, c) (half & halfBit(r, c) ? 1 : 0)
#define packet_t_size 4
struct packet_t {
  uint32_t state = 0;
};

#define OFFLINE_TIME 2000

///////////////////////////////////////// MATRIX

bool getKey(uint8_t r, uint8_t c) {
  return (matrix[r] & (1 << c)) != 0;
}

void printMatrix() {
  Serial.println("");
  for (uint8_t r = 0; r < num_rows; r++) {
    for (uint8_t c = 0; c < num_cols; c++) {
      Serial.print(getKey(r, c));
    }
    Serial.println("");
  }
  Serial.println("");
}

///////////////////////////////////////// RADIO

void initRadio() {
  memset(per_pipe_data, 0, NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH * 8);
  memset(per_pipe_length, 0, 8 * 4);

  nrf_gzll_init(NRF_GZLL_MODE_HOST);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);

  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);

  ack_payload[0] = 0x55;
  nrf_gzll_add_packet_to_tx_fifo(0, ack_payload, 1);
  nrf_gzll_add_packet_to_tx_fifo(1, ack_payload, 1);
  nrf_gzll_add_packet_to_tx_fifo(2, ack_payload, 1);
  nrf_gzll_add_packet_to_tx_fifo(3, ack_payload, 1);
  nrf_gzll_add_packet_to_tx_fifo(4, ack_payload, 1);
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_4_DBM);
  nrf_gzll_enable();
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {
  uint32_t new_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (nrf_gzll_fetch_packet_from_rx_fifo(pipe, new_data, &new_length)) {
    per_pipe_length[pipe] = new_length;
    memcpy(per_pipe_data[pipe], new_data, new_length);
    nrf_gzll_add_packet_to_tx_fifo(pipe, ack_payload, 1);
    per_pipe_millis[pipe] = millis();
  }
  nrf_gzll_flush_rx_fifo(pipe);
}

void mergeLeft(packet_t packet, int pipe) {
  uint32_t half = packet.state;
  matrix[0] |= (half >> 0) & 0x3f;
  matrix[1] |= (half >> 7) & 0x3f;
  matrix[2] |= (half >> 14) & 0x3f;
  matrix[3] |= (getHalf(half, 0, 6) << 4) | (getHalf(half, 1, 6) << 5) | (getHalf(half, 2, 6) << 6);
}

void mergeRight(packet_t packet, int pipe) {
  uint32_t half = packet.state;
  matrix[4] |= (half >> 0) & 0x7e;
  matrix[5] |= (half >> 7) & 0x7e;
  matrix[6] |= (half >> 14) & 0x7e;
  matrix[7] |= (getHalf(half, 0, 0) << 2) | (getHalf(half, 1, 0) << 1) | (getHalf(half, 2, 0) << 0);
}

bool loadMatrixFromPayload() {

  return true;
}

///////////////////////////////////////// WIRE

void initWire() {
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(requestEvent);
}

void requestEvent() {  memset(matrix, 0, num_rows);
  memset(matrix, 0, num_rows);
  matrix[num_rows] = CHECK_BYTE;
  if (per_pipe_length[0] > 0) {
    for (int i = 0; i < num_rows; i++) {
      matrix[i] = per_pipe_data[0][i];
    }
  }
  if (per_pipe_length[1] > 0) {
    mergeLeft(*(packet_t*)(per_pipe_data[1]), 1);
  }
  if (per_pipe_length[2] > 0) {
    mergeRight(*(packet_t*)(per_pipe_data[2]), 2);
  }
  if (per_pipe_length[3] > 0) {
    mergeLeft(*(packet_t*)(per_pipe_data[3]), 3);
  }
  if (per_pipe_length[4] > 0) {
    mergeRight(*(packet_t*)(per_pipe_data[4]), 4);
  }
  Wire.write(matrix, num_rows + 1);
}

///////////////////////////////////////// MAIN

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  initRadio();
  initWire();
  Serial.print("ok");
  suspendLoop();
}

void loop() {}
