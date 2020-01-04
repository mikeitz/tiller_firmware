#include "nrf_gzll.h"
#include "Wire.h"
#include "MD_CirQueue.h"


const uint8_t num_cols = 7;
const uint8_t num_rows = 8; // If num_rows > 8, need to use bigger types for matrix.

const uint8_t CHECK_BYTE = 0x55;
#define I2C_ADDRESS 2

struct host_packet_t {
  uint8_t matrix[num_rows];
  uint32_t midi_note;
  uint8_t check_byte;
};
host_packet_t host_packet;

static uint8_t channel_table[6] = {4, 25, 42, 63, 77, 33};
uint8_t new_data[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint8_t per_pipe_data[8][NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t per_pipe_length[8];

#define halfBit(r, c) (((uint32_t)1) << (c + r * num_cols))
#define getHalf(half, r, c) (half & halfBit(r, c) ? 1 : 0)
#define half_packet_t_size 4
struct half_packet_t {
  uint32_t state = 0;
};

// Ring buffer for midi notes.
const uint16_t buffer_size = 1024;
volatile uint32_t midi_keys[buffer_size];
volatile uint32_t index_written = 0;
volatile uint32_t index_read = 0;

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

  nrf_gzll_enable();
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}

inline void handle_midi_packet(uint8_t* data, uint8_t len) {
  for (int i = 0; i < len; i += 4) {
    uint32_t msg = *(uint32_t*)&data[i];
    if (msg) {
      midi_keys[index_written % buffer_size] = msg;
      index_written++;
    }
  }
}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {
  uint32_t new_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (nrf_gzll_fetch_packet_from_rx_fifo(pipe, new_data, &new_length)) {
    if (pipe >= 5) {
      handle_midi_packet(new_data, new_length);
    } else {
      per_pipe_length[pipe] = new_length;
      memcpy(per_pipe_data[pipe], new_data, new_length);
    }
  }
}

inline void mergeLeft(half_packet_t packet) {
  uint32_t half = packet.state;
  host_packet.matrix[0] |= (half >> 0) & 0x3f;
  host_packet.matrix[1] |= (half >> 7) & 0x3f;
  host_packet.matrix[2] |= (half >> 14) & 0x3f;
  host_packet.matrix[3] |= (getHalf(half, 0, 6) << 4) | (getHalf(half, 1, 6) << 5) | (getHalf(half, 2, 6) << 6);
}

inline void mergeRight(half_packet_t packet) {
  uint32_t half = packet.state;
  host_packet.matrix[4] |= (half >> 0) & 0x7e;
  host_packet.matrix[5] |= (half >> 7) & 0x7e;
  host_packet.matrix[6] |= (half >> 14) & 0x7e;
  host_packet.matrix[7] |= (getHalf(half, 0, 0) << 2) | (getHalf(half, 1, 0) << 1) | (getHalf(half, 2, 0) << 0);
}

///////////////////////////////////////// WIRE

void initWire() {
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(requestEvent);
}

void requestEvent() {
  memset(&host_packet, 0, sizeof(host_packet_t));
  host_packet.check_byte = CHECK_BYTE;

  if (per_pipe_length[1] > 0) {
    mergeLeft(*(half_packet_t*)(per_pipe_data[1]));
  }
  if (per_pipe_length[2] > 0) {
    mergeRight(*(half_packet_t*)(per_pipe_data[2]));
  }

  if (per_pipe_length[3] > 0) {
    mergeLeft(*(half_packet_t*)(per_pipe_data[3]));
  }
  if (per_pipe_length[4] > 0) {
    mergeRight(*(half_packet_t*)(per_pipe_data[4]));
  }

  if (index_read < index_written) {
    host_packet.midi_note = midi_keys[index_read % buffer_size];
    index_read++;
  }

  Wire.write((uint8_t*)&host_packet, sizeof(host_packet_t));
}

///////////////////////////////////////// MAIN

void setup() {
  //memset(keybed, 0, 49);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  initRadio();
  initWire();
  Serial.println("ok");
  Serial.flush();
  suspendLoop();
}

void loop() {}
