#include "Wire.h"
#include "nrf.h"
#include "nrf_gzll.h"

#define PIPE 5
#define DEBUG 0

#include <Adafruit_DotStar.h>
#define NUMPIXELS 1 
#define DATAPIN    8
#define CLOCKPIN   6
Adafruit_DotStar strip(NUMPIXELS, DATAPIN, CLOCKPIN, DOTSTAR_BRG);

const uint8_t message_size = 3;
const uint16_t buffer_size = 4096;
volatile uint8_t buffer[buffer_size * message_size];
volatile uint32_t index_written = 0;
volatile uint32_t index_read = 0;

void Enqueue(uint8_t count, uint8_t* bytes) {
    if (count != 3) {
        return;
    }
    uint16_t slot = (index_written % buffer_size) * message_size;
    buffer[slot + 0] = bytes[0];
    buffer[slot + 1] = bytes[1];
    buffer[slot + 2] = bytes[2];
    index_written++;
}

void TransmitFromQueue() {
    while (index_read < index_written) {
        int num = min(10, index_written - index_read);
        num = min(num, buffer_size - index_read % buffer_size);
        if (nrf_gzll_add_packet_to_tx_fifo(PIPE, (uint8_t*)&buffer[message_size * (index_read % buffer_size)], message_size * num)) {
            index_read += num;
        } else {
            break;
        }
    }
}

void showColor(uint8_t r, uint8_t g, uint8_t b) {
    strip.begin();
    strip.setBrightness(5);
    strip.setPixelColor(0, r, g, b);
    strip.show();
}

const uint16_t PACKET_SIZE = 256;
uint8_t msg[PACKET_SIZE] = { 0 };

void Receive(int n) {
    if (n == 0) {
        return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        msg[i] = Wire.read();
    }
    if (msg[0] == 0) {
        msg[n] = '\0';
        String s((char*)(msg + 1));
#if DEBUG
        Serial.print(s);
#endif
        if (s[0] == 'F') {
            showColor(0, 255, 0);
        }
        if (s[0] == 'C') {
            showColor(0, 0, 255);
        }
        if (s[0] == 'D') {
            showColor(255, 255, 0);
        }
    } else {
#if DEBUG
        Serial.print("data: ");
        for (int i = 0; i < n; ++i) {
            Serial.print(msg[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
#endif
        if (n == 4) {
            Enqueue(3, msg + 1);
        }
    }
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
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {
#if DEBUG
    Serial.println("TX FAIL");
#endif
}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}

void setup() {
#if DEBUG
    Serial.begin(9600);
#endif
    showColor(255, 255, 255);
    initRadio();
    Wire.begin(0);
    Wire.onReceive(Receive);
}

void loop() {
    delay(1);
    TransmitFromQueue();
}