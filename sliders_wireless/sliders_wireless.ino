#include "nrf.h"
#include "nrf_gzll.h"

#define PIPE 6
#define DEBUG 1

// 5 is joystick
// 6 is sliders

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
#if (PIPE % 2)
  uint8_t channel_table[3] = {25, 63, 33};
#else
  uint8_t channel_table[3] = {4, 42, 77};
#endif

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

class Slider {
 public:
  Slider(int pin, int cc) : cc_num_(cc), pin_(pin) {
    pinMode(pin_, INPUT);
  }
  bool Update() {
    uint32_t sense = analogRead(pin_);
    #if (PIPE == 5)
    uint32_t cc = sense * 128 / 930;
    #else
    uint32_t cc = sense * 128 / 940;
    #endif
    cc = min(127, max(0, cc));
  
    if (cc != last_cc_ && abs((int)last_sense_ - (int)sense) > 7) {
      last_sense_ = sense;
      last_cc_ = cc;
      #if DEBUG
      Serial.print(cc_num_);
      Serial.print(" ");
      Serial.println(sense);
      #endif
      return true;
    }
    return false;
  }
  uint8_t Value() {
    return last_cc_;
  }
 private:
  uint8_t pin_;
  uint8_t cc_num_;
  uint32_t last_sense_ = 0;
  uint32_t last_cc_ = 0;
};

auto mod = Slider(A2, 0);
auto vib = Slider(A4, 1);

void setup() {
  Serial.begin(115200);
  initRadio();
}

void loop() {
  bool transmit = false;
  transmit |= mod.Update();
  transmit |= vib.Update();
  if (transmit) {
    uint16_t data = mod.Value();
    data <<= 8;
    data |= vib.Value();
    nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)&data, 2);
  }
  delay(20);
}