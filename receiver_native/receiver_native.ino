#include "nrf_gzll.h"
#include <Adafruit_TinyUSB.h>

#define DEBUG 1
const uint64_t ONE = 1;

///////////////////////////////////////// INTEROP

volatile uint32_t read_count = 0;
volatile uint32_t write_count = 0;
const uint32_t queue_size = 4096;
volatile uint8_t queue_buffer[queue_size];

void enqueueMessage(uint8_t pipe, uint8_t length, uint8_t* data) {
  uint32_t cursor = write_count;
  queue_buffer[(cursor++) % queue_size] = pipe;
  queue_buffer[(cursor++) % queue_size] = length;
  for (uint8_t i = 0; i < length; ++i) {
    queue_buffer[(cursor++) % queue_size] = data[i];
  }
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

void initRadio() {
  static uint8_t channel_table[6] = { 4, 25, 42, 63, 77, 33 };
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
  static uint8_t new_data[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
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
int8_t mods = 0, per_key_mods = 0;
uint8_t report[6] = { 0, 0, 0, 0, 0, 0 };

void initHid() {
  usb_hid.setReportCallback(NULL, hid_report_callback);
  usb_hid.begin();
  while (!TinyUSBDevice.mounted()) {
    delay(1);
  }
}

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
  uint8_t force_on = per_key_mods & 0xf;
  uint8_t force_off = ((per_key_mods & 0xf0) >> 4) | (per_key_mods & 0xf0);
  report_ptr[0] = (mods | force_on) & (~force_off);
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

///////////////////////////////////////// KEYMAP

const int num_pipes = 8;
const int num_layers = 16;
const int num_keys_per_pipe = 32;

#include "./keymap.h"

uint32_t pipe_state[num_pipes] = { 0 };
uint32_t release_keymap[num_pipes][num_keys_per_pipe] = { 0 };
int8_t active_layers[num_layers] = {
  LAYER_BASE, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
uint32_t active_layer_mask = 1;

void activateLayer(uint8_t layer) {
  if (isLayerActive(layer)) {
    return;
  }
  active_layer_mask |= (1 << layer);
  int i = 0;
  int8_t move = -1;
  for (; i < num_layers && active_layers[i] > layer; ++i) {}
  move = active_layers[i];
  active_layers[i++] = layer;
  for (; i < num_layers && move != -1; ++i) {
    int8_t temp = active_layers[i];
    active_layers[i] = move;
    move = temp;
  }
}

void deactivateLayer(uint8_t layer) {
  if (!isLayerActive(layer)) {
    return;
  }
  active_layer_mask &= ~(1 << layer);
  int i = 0;
  for (; i < num_layers && active_layers[i] != layer; ++i) {}
  for (; i < num_layers && active_layers[i] != -1; ++i) {
    active_layers[i] = active_layers[i + 1];
  }
}

bool isLayerActive(uint8_t layer) {
  return (1 << layer) & active_layer_mask;
}

void toggleLayer(uint8_t layer) {
  if (isLayerActive(layer)) {
    deactivateLayer(layer);
  } else {
    activateLayer(layer);
  }
}

uint32_t getKeyFromMap(uint8_t pipe, uint8_t key) {
  return keymap[pipe][active_layers[0]][key];
}

void registerKey(uint32_t keycode) {
  if (keycode >= MO(0)) {
    activateLayer(keycode & 0xf);
  } else {
    addToReport(keycode);
  }
}

void unregisterKey(uint32_t keycode) {
  if (keycode >= MO(0)) {
    deactivateLayer(keycode & 0xf);
  } else {
    clearFromReport(keycode);
  }
}

void handleKey(uint8_t pipe, uint8_t key, bool pressed) {
  if (pressed) {
    per_key_mods = 0;
    uint32_t keycode = getKeyFromMap(pipe, key);
    release_keymap[pipe][key] = keycode;
    registerKey(keycode);
  } else {
    unregisterKey(release_keymap[pipe][key]);
  }
}

void updatePipe(uint8_t pipe, uint32_t new_state) {
  int32_t old_state = pipe_state[pipe];
  if (old_state == new_state) {
    return;
  } else {
    for (int i = 0; i < num_keys_per_pipe; ++i) {
      uint32_t bit = ONE << i;
      if ((old_state & bit) != (new_state & bit)) {
        handleKey(pipe, i, new_state & bit);
        generateReport();
      }
    }
  }
  if (per_key_mods) {
    per_key_mods = 0;
    generateReport();
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
  delay(1);

  if (hasMessage() && TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }

  if (!sendReports()) {
    return;
  }

  static uint8_t pipe, length;
  static uint8_t data[256];
  while (dequeueMessage(&pipe, &length, data)) {
    if (pipe < 5 && length == 4) {
      updatePipe(pipe, *(uint32_t*)data);
    }
  }
}