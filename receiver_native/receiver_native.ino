#include "nrf_gzll.h"
#include <Adafruit_TinyUSB.h>

///////////////////////////////////////// INTEROP

volatile uint32_t read_count = 0;
volatile uint32_t write_count = 0;
const uint32_t queue_size = 4096;
volatile uint8_t queue_buffer[queue_size];

volatile uint32_t last_size = 0;

void enqueueMessage(uint8_t pipe, uint8_t length, uint8_t* data) {
  uint32_t cursor = write_count;
  queue_buffer[(cursor++) % queue_size] = pipe;
  queue_buffer[(cursor++) % queue_size] = length;
  for (uint8_t i = 0; i < length; ++i) {
    queue_buffer[(cursor++) % queue_size] = data[i];
  }
  last_size = length;
  write_count = cursor;
}

bool hasMessage() {
  return write_count > read_count;
}

bool dequeueMessage(uint8_t* pipe, uint8_t* length, uint8_t* data) {
  if (!hasMessage()) {
    *pipe = -1;
    *length = 0;
    return false;
  }
  uint32_t cursor = read_count;
  *pipe = queue_buffer[(cursor++) % queue_size];
  *length = queue_buffer[(cursor++) % queue_size];
  for (uint8_t i = 0; i < *length; ++i) {
    data[i] = queue_buffer[(cursor++) % queue_size];
  }
  read_count = cursor;
  return true;
}

///////////////////////////////////////// RADIO

static uint8_t channel_table[6] = { 4, 25, 42, 63, 77, 33 };
uint8_t new_data[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];

void initRadio() {
  nrf_gzll_init(NRF_GZLL_MODE_HOST);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060709);
  nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_N8_DBM);
  nrf_gzll_enable();
}

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {
  uint32_t length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (nrf_gzll_fetch_packet_from_rx_fifo(pipe, new_data, &length)) {
    enqueueMessage(pipe, length, new_data);
  }
}

///////////////////////////////////////// HID

uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

// Output report callback for LED indicator such as Caplocks
void hid_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {}

///////////////////////////////////////// MAIN

#define DEBUG 1
const uint64_t ONE = 1;

void show(uint64_t x) {
  for (int i = 0; i < 64; i++) {
    Serial.print(x & (ONE << i) ? 1 : 0);
  }
  Serial.println();
}

void show(uint32_t x) {
  for (int i = 0; i < 32; i++) {
    Serial.print(x & (1UL << i) ? 1 : 0);
  }
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  usb_hid.setReportCallback(NULL, hid_report_callback);
  usb_hid.begin();
  while (!TinyUSBDevice.mounted()) {
    delay(1);
  }
  initRadio();
  delay(1000);
}

bool keyPressedPreviously = false;

uint32_t keymap[] = {
  HID_KEY_SPACE, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, HID_KEY_SPACE,
  HID_KEY_SPACE, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_SPACE,
  HID_KEY_SPACE, HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, HID_KEY_SPACE,
};
uint32_t release_keymap[32];

int32_t pipe_state[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
uint8_t report[6] = { 0, 0, 0, 0, 0, 0 };

void addToReport(uint8_t keycode) {
  clearFromReport(keycode);
  for (int i = 0; i < 6; ++i) {
    if (report[i] == 0) {
      report[i] = keycode;
      break;
    }
  }
}

void clearFromReport(uint8_t keycode) {
  for (int i = 0; i < 6; ++i) {
    if (report[i] == keycode) {
      report[i] = 0;
    }
  }
}

void handleKey(uint8_t pipe, uint8_t key, bool pressed) {
  if (pressed) {
    uint8_t keycode = (uint8_t)keymap[key];
    release_keymap[key] = keycode;
    addToReport(keycode);
  } else {
    uint8_t keycode = (uint8_t)release_keymap[key];
    clearFromReport(keycode);
  }
}

void handle(uint8_t pipe, uint32_t new_state) {
  int32_t old_state = pipe_state[pipe];
  if (old_state == new_state) {
    return;
  } else {
    for (int i = 0; i < 32; ++i) {
      uint32_t bit = ONE << i;
      if ((old_state & bit) != (new_state & bit)) {
        handleKey(pipe, i, new_state & bit);
      }
    }
  }
  pipe_state[pipe] = new_state;
  usb_hid.keyboardReport(0, 0, report);
}

void loop() {
  delay(1);

  if (!usb_hid.ready())
    return;

  uint8_t pipe, length;
  uint8_t data[256];

  if (dequeueMessage(&pipe, &length, data)) {
    if (TinyUSBDevice.suspended()) {
      TinyUSBDevice.remoteWakeup();
    }

    if (pipe == 1 && length == 4) {
      handle(1, *(uint32_t*)data);
    }
  }
}