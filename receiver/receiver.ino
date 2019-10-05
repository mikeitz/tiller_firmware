#include "nrf_gzll.h"
#include "Wire.h"

const uint8_t num_cols = 7;
const uint8_t num_rows = 8; // If num_rows > 8, need to use bigger types for matrix.
const uint8_t CHECK_BYTE = 0x55;
uint8_t matrix[num_rows + 1];

#define I2C_ADDRESS 2

#define TX_PAYLOAD_LENGTH 5acabddddd
static uint8_t data_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
static uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
extern nrf_gzll_error_code_t nrf_gzll_error_code;
static uint8_t channel_table[6] = {4, 42, 77};
volatile int new_data_length = 0;

unsigned long lastPacket = 0;

#define OFFLINE_TIME 5000

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
  memset(data_payload, 0, sizeof(data_payload));

  nrf_gzll_init(NRF_GZLL_MODE_HOST);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);

  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);

  ack_payload[0] = 0x55;
  nrf_gzll_add_packet_to_tx_fifo(0, ack_payload, 1);

  nrf_gzll_enable();
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}

void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info)
{
  uint32_t data_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  nrf_gzll_fetch_packet_from_rx_fifo(pipe, data_payload, &data_payload_length);
  if (data_payload_length > 0) {
    new_data_length = data_payload_length;
  }
  nrf_gzll_flush_rx_fifo(pipe);
  ack_payload[0] =  0x55;
  nrf_gzll_add_packet_to_tx_fifo(pipe, ack_payload, 1);
}

bool loadMatrixFromPayload() {
  if (new_data_length == 0) {
    if (millis() - lastPacket > OFFLINE_TIME) {
      digitalWrite(LED_BUILTIN, LOW);
    }
    return false;
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if (new_data_length != num_rows + 1 || data_payload[num_rows] != CHECK_BYTE) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    new_data_length = 0;
    return false;
  }
  new_data_length = 0;
  for (int i = 0; i < num_rows; i++) {
    matrix[i] = data_payload[i];
  }
  matrix[num_rows] = CHECK_BYTE;
  lastPacket = millis();
  return true;
}

///////////////////////////////////////// WIRE

void initWire() {
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(requestEvent);
}

void requestEvent() {
  Wire.write(matrix, num_rows + 1);
}

///////////////////////////////////////// MAIN

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  initRadio();
  initWire();
}

void loop() {
  if (loadMatrixFromPayload()) {
    // printMatrix();
  }
}
