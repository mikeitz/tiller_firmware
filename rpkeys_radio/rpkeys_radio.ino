#include "Wire.h"
#include "nrf.h"
#include "nrf_gzll.h"

const uint8_t PIPE = 7;
const bool DEBUG = false;

const uint16_t buffer_size = 4096;
volatile uint32_t buffer[buffer_size];
volatile uint32_t index_written = 0;
volatile uint32_t index_read = 0;

void Enqueue(uint32_t msg) {
    if (DEBUG) Serial.println(msg, 16);

    buffer[index_written % buffer_size] = msg;
    index_written++;
}

void TransmitFromQueue() {
    while (index_read < index_written) {
        int num = min(8, index_written - index_read);
        num = min(num, buffer_size - index_read % buffer_size);
        if (nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)&buffer[index_read % buffer_size], 4 * num)) {
            index_read += num;
        } else {
            break;
        }
    }
}

void Receive(int n) {
    if (n % 4 != 0) {
        return;
    }
    for (uint8_t i = 0; i < n; i += 4) {
        uint8_t msg[4];
        msg[0] = Wire.read();
        msg[1] = Wire.read();
        msg[2] = Wire.read();
        msg[3] = Wire.read();
        Enqueue(*(uint32_t*)msg);
    }
}

uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
uint32_t ack_payload_length = 0;
uint8_t channel_table[6] = { 4, 25, 42, 63, 77, 33 };

void initRadio() {
    delay(500);
    nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
    nrf_gzll_set_max_tx_attempts(1000);
    nrf_gzll_set_timeslots_per_channel(4);
    nrf_gzll_set_channel_table(channel_table, 6);
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
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
    if (DEBUG) Serial.println("TX FAIL");
}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

void setup() {
    if (DEBUG) Serial.begin(9600);
    Wire.begin(0);
    Wire.onReceive(Receive);
    initRadio();
    delay(500);
}

void loop() {
    delay(3);
    TransmitFromQueue();
}