#include <Adafruit_TinyUSB.h>

#include "nrf.h"
#include "nrf_gzll.h"

#define PIPE 6
#define DEBUG 0

#define SMOOTHING 5

class Controller {
public:
    Controller(int pin, int cc_num, int min_val, int max_val)
        : cc_num_(cc_num), pin_(pin),
        min_val_(min_val* SMOOTHING),
        max_val_(max_val* SMOOTHING),
        range_(max_val_ - min_val_) {}
    void Update(uint8_t* msg, uint8_t& n) {
        sense_ -= smooth_buffer_[smooth_offset_];
        sense_ += smooth_buffer_[smooth_offset_++] = analogRead(pin_);
        if (smooth_offset_ >= SMOOTHING) {
            smooth_offset_ = 0;
        }

        if (sense_ < min_val_) sense_ = min_val_;
        if (sense_ > max_val_) sense_ = max_val_;
        if (sense_ == last_sense_) {
            return;
        }
        int32_t cc = (sense_ - min_val_) * 128 / range_;
        if (cc == last_cc_ || cc == last_cc_ + 1) {
            return;
        }
        if (cc > last_cc_) {
            --cc;
        }

        last_sense_ = sense_;
        last_cc_ = cc;

        msg[n++] = 0xb0;
        msg[n++] = cc_num_;
        msg[n++] = cc;

#if DEBUG
        Serial.print(cc_num_);
        Serial.print(" ");
        Serial.println(sense_);
        Serial.flush();
#endif
    }
private:
    int32_t sense_ = 0;
    int32_t smooth_buffer_[SMOOTHING] = { 0 };
    uint8_t smooth_offset_ = 0;

    uint8_t pin_;
    uint8_t cc_num_;
    uint32_t last_sense_ = 0;
    uint32_t last_cc_ = 0;
    int32_t min_val_ = 0, max_val_ = 0, range_ = 0;
};

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint8_t channel_table[3] = { 4, 42, 77 };

uint32_t data[100];

void initRadio() {
    delay(500);
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
    delay(500);
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

auto left = Controller(PIN_A3, 21, 1, 931);
auto middle = Controller(PIN_A2, 11, 1, 931);
auto right = Controller(PIN_A1, 1, 1, 931);

void setup() {
    pinMode(PIN_VCC_ON, OUTPUT);
    digitalWrite(PIN_VCC_ON, 0);
    initRadio();
    digitalWrite(PIN_LED1, 1);
}

void loop() {
    uint8_t n = 0;
    static uint8_t msg[256];

    digitalWrite(PIN_VCC_ON, 1);
    delay(1);
    left.Update(msg, n);
    middle.Update(msg, n);
    right.Update(msg, n);
    digitalWrite(PIN_VCC_ON, 0);
    if (n) {
        nrf_gzll_add_packet_to_tx_fifo(PIPE, msg, n);
    }

    delay(5);
}