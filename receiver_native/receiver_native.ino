#include "nrf_gzll.h"
#include <Adafruit_TinyUSB.h>

#define DEBUG 1
const uint64_t ONE = 1;

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

void hid_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {}

uint32_t reports_generated = 0, reports_sent = 0;
const int report_buffer_size = 256;
const int report_size = 7;
uint8_t report_buffer[report_buffer_size * report_size];

void initHid() {
  usb_hid.setReportCallback(NULL, hid_report_callback);
  usb_hid.begin();
  while (!TinyUSBDevice.mounted()) {
    delay(1);
  }
}

int8_t mods = 0, weak_mods = 0;
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

void generateReport() {
  uint8_t* report_ptr = &report_buffer[(reports_generated * report_size) % report_buffer_size];
  report_ptr[0] = mods | weak_mods;
  memcpy(report_ptr + 1, report, 6);
  ++reports_generated;
}

bool sendReports() {
  while (reports_sent < reports_generated) {
    if (!usb_hid.ready()) {
      return false;
    }
    uint8_t* report = &report_buffer[(reports_sent * report_size) % report_buffer_size];
    usb_hid.keyboardReport(0, report[0], report + 1);
    ++reports_sent;
  }
  return true;
}

///////////////////////////////////////// KEYBOARD

#define TG(layer) HID_KEY_SPACE
#define LM(mod, layer) HID_KEY_SPACE

#define LAYER_BASE 0
#define LAYER_TAB 1
#define LAYER_GAME 2
#define LAYER_NUM 3
#define LAYER_SYM 4
#define LAYER_FN 5

const int num_pipes = 8;
const int num_layers = 16;
const int keys_per_pipe = 32;

#define ___ 0
#define XXX -1

const uint32_t keymap[num_pipes][num_layers][keys_per_pipe] = {
  { // PIPE 0
  },
  { // PIPE 1
    { // LAYER_BASE
     HID_KEY_ESCAPE, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, HID_KEY_GUI_LEFT,
     LM(HID_KEY_CONTROL_LEFT, LAYER_TAB), HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_SHIFT_LEFT,
     LM(HID_KEY_ALT_LEFT, LAYER_TAB), HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, TG(LAYER_NUM),
    },
    { // LAYER_TAB
    },
    { // LAYER_GAME
    },
    { // LAYER_NUM
    },
    { // LAYER_SYM
    },
    { // LAYER_FN
    },
  },
  { // PIPE 2
    { // LAYER_BASE
     TG(LAYER_SYM), HID_KEY_Y, HID_KEY_U, HID_KEY_I, HID_KEY_O, HID_KEY_P, HID_KEY_BACKSPACE,
     HID_KEY_SPACE, HID_KEY_H, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_SEMICOLON, HID_KEY_ENTER,
     TG(LAYER_FN), HID_KEY_N, HID_KEY_M, HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH, HID_KEY_DELETE,
    },
    { // LAYER_TAB
    },
    { // LAYER_GAME
    },
    { // LAYER_NUM
    },
    { // LAYER_SYM
    },
    { // LAYER_FN
    },
  },
};

uint32_t release_keymap[num_pipes][keys_per_pipe];
int32_t pipe_state[num_pipes] = { 0, 0, 0, 0, 0, 0, 0, 0 };

void handleKey(uint8_t pipe, uint8_t key, bool pressed) {
  if (pressed) {
    weak_mods = 0;
    uint8_t keycode = (uint8_t)keymap[pipe][LAYER_BASE][key];
    release_keymap[pipe][key] = keycode;
    addToReport(keycode);
  } else {
    uint8_t keycode = (uint8_t)release_keymap[pipe][key];
    clearFromReport(keycode);
  }
}

void updatePipe(uint8_t pipe, uint32_t new_state) {
  int32_t old_state = pipe_state[pipe];
  if (old_state == new_state) {
    return;
  } else {
    for (int i = 0; i < keys_per_pipe; ++i) {
      uint32_t bit = ONE << i;
      if ((old_state & bit) != (new_state & bit)) {
        handleKey(pipe, i, new_state & bit);
        generateReport();
      }
    }
  }
  pipe_state[pipe] = new_state;
}

///////////////////////////////////////// MAIN

void setup() {
  Serial.begin(9600);
  initHid();
  initRadio();
  delay(1000);
}

void loop() {
  if (hasMessage() && TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }

  // Send all existing reports.
  if (!sendReports()) {
    delay(1);
    return;
  };

  // Then generate new reports from radio messages.
  uint8_t pipe, length;
  uint8_t data[256];
  if (dequeueMessage(&pipe, &length, data)) {
    if (pipe < 5 && length == 4) {
      updatePipe(pipe, *(uint32_t*)data);
    }
  }

  // And then we wait for more.
  delay(1);
}