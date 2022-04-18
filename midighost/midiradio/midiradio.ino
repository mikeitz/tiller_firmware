#include "Wire.h"
#include "nrf.h"
#include "nrf_gzll.h"

#define PIPE 7
#define DEBUG 0

#include <Adafruit_DotStar.h>
#define NUMPIXELS 1 
#define DATAPIN    8
#define CLOCKPIN   6
Adafruit_DotStar strip(NUMPIXELS, DATAPIN, CLOCKPIN, DOTSTAR_BRG);

void showColor(uint8_t r, uint8_t g, uint8_t b) {
    strip.begin();
    strip.setBrightness(20);
    strip.setPixelColor(0, r, g, b);
    strip.show();
}

uint8_t msg[4] = { 0 };

void Receive(int n) {
    memset(msg, 0, 4);
    for (uint8_t i = 0; i < n; ++i) {
        msg[i] = Wire.read();
    }
#if DEBUG
    for (uint8_t i = 0; i < n; ++i) {
        Serial.print(msg[i], HEX);
        Serial.print(" ");
    }
    Serial.println("");
#endif
    if (n == 1) {
        switch (msg[0]) {
        case 0: // Awaiting connection.
            showColor(0, 0, 255);
            return;
        case 1: // Connected.
            showColor(255, 0, 0); // R/G seem backwards. This is green.
            return;
        case 0x7f: // Critical error.
            showColor(0, 255, 0); // R/G seem backwards. This is red.
            return;
        default:
            break;
        }
    }

    nrf_gzll_add_packet_to_tx_fifo(PIPE, msg, n);
}

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

void setup() {
#if DEBUG
    Serial.begin(9600);
#endif
    Wire.begin(0);
    Wire.onReceive(Receive);
    showColor(255, 255, 255);
    initRadio();
}

void loop() {
    delay(5000);
}