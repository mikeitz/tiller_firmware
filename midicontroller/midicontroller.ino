#include "nrf.h"
#include "nrf_gzll.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads1115;

#define PIPE 6
#define DEBUG 0

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint8_t channel_table[3] = {4, 42, 77};

uint32_t data[4];

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

class Controller {
 public:
  Controller(int pin, int cc_num, int min_val, int max_val)
    : cc_num_(cc_num), pin_(pin), min_val_(min_val), max_val_(max_val) {}
  void Update(uint32_t data[], uint8_t* n) {
    int32_t sense = ads1115.readADC_SingleEnded(pin_);
    if (sense < min_val_) sense = min_val_;
    if (sense > max_val_) sense = max_val_;
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

void setup() {
  Serial.begin(9600);
  ads1115.begin();
  delay(100);
  initRadio();
  delay(100);
}

void loop() {
  uint8_t num = 0;
  fader_1.Update(data, &num);
  fader_2.Update(data, &num);
  joy_x.Update(data, &num);
  joy_y.Update(data, &num);
  if (num) {
    nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)data, num * 4);
  }
  delay(20);
}
