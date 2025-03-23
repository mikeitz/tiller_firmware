#include "nrf_gzll.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

#define DEBUG 0

#define BIT64(n) (((uint64_t)1)<<n)
#define BIT32(n) (((uint32_t)1)<<n)

const int num_pipes = 8;
const int num_keys_per_pipe = 32;
const int num_layers = 16;

///////////////////////////////////////// RADIO

class RadioManager {
public:
  void Init() {
    delay(500);
    static uint8_t channel_table[6] = { 4, 25, 42, 63, 77, 33 };
    nrf_gzll_init(NRF_GZLL_MODE_HOST);
    nrf_gzll_set_channel_table(channel_table, 3);
    nrf_gzll_set_timeslots_per_channel(4);
    nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
    nrf_gzll_set_timeslot_period(900);
    nrf_gzll_set_base_address_0(0x01020304);
    nrf_gzll_set_base_address_1(0x05060708);
    nrf_gzll_set_tx_power(NRF_GZLL_TX_POWER_4_DBM);
    nrf_gzll_enable();
    delay(500); // Avoid race condition on radio startup.
  }
  void EnqueueMessage(uint8_t pipe, uint8_t length, uint8_t* data) {
    uint32_t cursor = write_count_;
    queue_buffer_[(cursor++) % queue_size_] = pipe;
    queue_buffer_[(cursor++) % queue_size_] = length;
    for (uint8_t i = 0; i < length; ++i) {
      queue_buffer_[(cursor++) % queue_size_] = data[i];
    }
    write_count_ = cursor;
  }
  bool const HasMessage() {
    return write_count_ > read_count_;
  }
  bool DequeueMessage(uint8_t* pipe, uint8_t* length, uint8_t* data) {
    if (!HasMessage()) {
      *pipe = -1;
      *length = 0;
      return false;
    }
    uint32_t cursor = read_count_;
    *pipe = queue_buffer_[(cursor++) % queue_size_];
    *length = queue_buffer_[(cursor++) % queue_size_];
    for (uint8_t i = 0; i < *length; ++i) {
      data[i] = queue_buffer_[(cursor++) % queue_size_];
    }
    read_count_ = cursor;
    return true;
  }
private:
  static const uint32_t queue_size_ = 4096;
  volatile uint8_t queue_buffer_[queue_size_];
  volatile uint32_t read_count_ = 0;
  volatile uint32_t write_count_ = 0;
} Radio;

void nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_disabled() {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {
  uint32_t length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  static uint8_t new_data[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
  if (nrf_gzll_fetch_packet_from_rx_fifo(pipe, new_data, &length)) {
    Radio.EnqueueMessage(pipe, length, new_data);
  }
}

///////////////////////////////////////// HID

const uint8_t desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

class HidManager {
public:
  void Init() {
    usb_hid.setReportCallback(NULL, ReportCallback);
    usb_hid.begin();
  }
  inline void SetMod(uint32_t keycode) {
    mods_ |= 1 << (keycode & 0xf);
  }
  inline void ClearMod(uint32_t keycode) {
    mods_ &= ~(1 << (keycode & 0xf));
  }
  inline const bool IsModSet(uint32_t keycode) {
    return (1 << (keycode & 0xf)) & mods_;
  }
  void GenerateReport() {
    uint8_t* report_ptr = &report_buffer_[(reports_generated_ * report_size_) % report_buffer_size_];
    uint8_t force_on = per_key_mods_ & 0xf;
    uint8_t force_off = ((per_key_mods_ & 0xf0) >> 4) | (per_key_mods_ & 0xf0);
    report_ptr[0] = (mods_ | force_on) & (~force_off);
    memcpy(report_ptr + 1, report_, 6);
    ++reports_generated_;
  }
  bool SendReports() {
    while (reports_sent_ < reports_generated_) {
      if (!usb_hid.ready()) {
        return false;
      }
      uint8_t* report_ = &report_buffer_[(reports_sent_ * report_size_) % report_buffer_size_];
      usb_hid.keyboardReport(0, report_[0], report_ + 1);
      ++reports_sent_;
    }
    return true;
  }
  void AddToReport(uint8_t keycode) {
    if (IsMod(keycode)) {
      return SetMod(keycode);
    }
    ClearFromReport(keycode);
    for (int i = 0; i < 6; ++i) {
      if (report_[i] == 0) {
        report_[i] = keycode;
        break;
      }
    }
  }
  void ClearFromReport(uint8_t keycode) {
    if (IsMod(keycode)) {
      return ClearMod(keycode);
    }
    for (int i = 0; i < 6; ++i) {
      if (report_[i] == keycode) {
        report_[i] = 0;
      }
    }
  }
  void SetPerKeyMods(uint8_t per_key_mods) {
    per_key_mods_ = per_key_mods;
  }
  void ResetPerKeyMods() {
    if (per_key_mods_) {
      per_key_mods_ = 0;
      GenerateReport();
    }
  }
private:
  inline const bool IsMod(uint32_t keycode) {
    return keycode >= 0xE0 && keycode < 0xF0;
  }
  static const uint32_t report_buffer_size_ = 256;
  static const uint8_t report_size_ = 7;
  static void ReportCallback(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {}
  uint32_t reports_generated_ = 0, reports_sent_ = 0;
  uint8_t report_buffer_[report_buffer_size_ * report_size_];
  int8_t mods_ = 0, per_key_mods_ = 0;
  uint8_t report_[6] = { 0, 0, 0, 0, 0, 0 };
} Hid;

///////////////////////////////////////// MIDI

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// Represents the whole stateless midi system.
class MidiManager {
public:
  void Init() {
    usb_midi.setStringDescriptor("tiller");
    MIDI.begin(MIDI_CHANNEL_OMNI);
  }
} Midi;

// Represents one logical piano keyboard which might be tranposed or reconfigured.
class MidiKeysManager {
public:
  void SetChannel(uint8_t channel) {
    channel_ = channel;
  }

  void SetTranspose(int8_t steps) {
    transpose_ = steps;
  }

  void HandlePacket(uint8_t* data, uint8_t length) {
    if (length % 4 != 0) {
      return;
    }
    for (int i = 0; i < length; i += 4) {
      uint32_t msg = *(uint32_t*)&data[i];
      uint8_t key = (msg >> 24) & 0x7f;
      uint32_t time = msg & 0xfffff;
      bool is_release = msg & 0x80000000;

      if (msg == 0) {
        // Indicates "keyboard is idle, all switches up".  We can spot check
        // to ensure we stay in sync.
        EnsureAllKeysReleased();
        continue;
      } else if (key == 0 || key == 0x7f) {
        // Reserve these two notes.  Not used on even 88 key piano.
        continue;
      }

      if (is_release) {
        ReleaseKey(key, TimeToVelocity(time));
      } else {
        PressKey(key, TimeToVelocity(time));
      }
    }
  }

  void SendControlChange(uint8_t control, uint8_t value, uint8_t channel) {
    MIDI.sendControlChange(control, value, channel);
  }

  void SendProgramChange(uint8_t program, uint8_t channel) {
    MIDI.sendProgramChange(program, channel);
  }

private:
  uint8_t channel_ = 1;
  int8_t transpose_ = 0;

  // Describe the channels and note values that physical keys are sounding.
  uint64_t lower_map_ = 0, upper_map_ = 0;
  uint8_t key_channels_[128] = { 0 };
  uint8_t key_notes_[128] = { 0 };

  void SetBit(uint8_t key) {
    if (key > 63) {
      upper_map_ |= BIT64(key - 64);
    } else {
      lower_map_ |= BIT64(key);
    }
  }

  void ClearBit(uint8_t key) {
    if (key > 63) {
      upper_map_ &= ~BIT64(key - 64);
    } else {
      lower_map_ &= ~BIT64(key);
    }
  }

  bool IsBitSet(uint8_t key) {
    return (key > 63) ? (upper_map_ & BIT64(key - 64)) : (lower_map_ & BIT64(key));
  }

  uint8_t TimeToVelocity(uint32_t t) {
    // Tuning notes:
    // https://docs.google.com/spreadsheets/d/1R55Zrt3V2YheBwDRFwp-pJO4Ciedf6vfs9Ag5xaM4PQ/edit
    int vel = 155 * exp(t * -0.00008f);
    if (vel < 1) return 1;
    if (vel > 127) return 127;
    return vel;
  }

  void PressKey(uint8_t key, uint8_t vel) {
    SetBit(key);
    key_notes_[key] = key + transpose_;
    key_channels_[key] = channel_;
    MIDI.sendNoteOn(key_notes_[key], vel, key_channels_[key]);
  }

  void ReleaseKey(uint8_t key, uint8_t vel) {
    if (!IsBitSet(key)) {
      // We won't have a valid channel and note number, and in theory it's not sounding anyway.
      return;
    }
    ClearBit(key);
    MIDI.sendNoteOff(key_notes_[key], vel, key_channels_[key]);
  }

  void EnsureAllKeysReleased() {
    if (lower_map_ || upper_map_) {
      if (DEBUG) Serial.println("releasing stuck keys");
      for (uint8_t i = 0; i < 128; ++i) {
        if (IsBitSet(i)) {
          ReleaseKey(i, 0);
        }
      }
    }
  }
} MidiKeys;

///////////////////////////////////////// LAYERS

class LayersManager {
public:
  void ActivateLayer(uint8_t layer) {
    if (IsLayerActive(layer)) {
      return;
    }
    ++num_active_layers_;
    active_layer_mask_ |= (1 << layer);
    int i = 0;
    int8_t move = 0;
    for (; i < num_layers && active_layers_[i] > layer; ++i) {}
    move = active_layers_[i];
    active_layers_[i++] = layer;
    for (; i < num_layers && move != 0; ++i) {
      int8_t temp = active_layers_[i];
      active_layers_[i] = move;
      move = temp;
    }
  }
  void DeactivateLayer(uint8_t layer) {
    if (!IsLayerActive(layer) || layer == 0) {
      return;
    }
    --num_active_layers_;
    active_layer_mask_ &= ~(1 << layer);
    int i = 0;
    for (; i < num_layers && active_layers_[i] != layer; ++i) {}
    for (; i < num_layers && active_layers_[i] != 0; ++i) {
      active_layers_[i] = active_layers_[i + 1];
    }
  }
  const bool IsLayerActive(uint8_t layer) {
    return (1 << layer) & active_layer_mask_;
  }
  void ToggleLayer(uint8_t layer) {
    if (IsLayerActive(layer)) {
      DeactivateLayer(layer);
    } else {
      ActivateLayer(layer);
    }
  }
  const uint8_t GetNumActiveLayers() {
    return num_active_layers_;
  }
  const uint8_t GetActiveLayer(uint8_t i) {
    return active_layers_[i];
  }
  const bool IsBase() {
    return active_layer_mask_ == 1;
  }
private:
  int8_t active_layers_[num_layers] = { 0 };
  uint32_t active_layer_mask_ = 1;
  uint8_t num_active_layers_ = 1;
} Layers;

///////////////////////////////////////// KEYBOARD

// Layout of 32 bit keycodes:
// all 0 = transparent to next layer
// all f = no action, opaque to other layers, stop
// 0-8 keycode
// 9-12 per key modifiers; 1=ctrl 2=shift 4=alt 8=gui
// 13-16 per key anti-modifiers; as above
// 17-24 opcode
//     0x0X = momentary layer X
//     0x1X = toggle layer X
// 25-32 > 0, custom keycode

#define ___ 0x00000000u
#define XXX 0xffffffffu
#define LAYER_BASE 0

#define MOD(mod) (1 << (8 + (mod & 0xf)))
#define SHIFT(key) (MOD(HID_KEY_SHIFT_LEFT) | key)
#define CTRL(key) (MOD(HID_KEY_CONTROL_LEFT) | key)
#define ALT(key) (MOD(HID_KEY_ALT_LEFT) | key)
#define GUI(key) (MOD(HID_KEY_GUI_LEFT) | key)

// Right mod key bits repurposed to be anti-per-key mods.
#define UNSHIFT(key) (MOD(HID_KEY_SHIFT_RIGHT) | key)
#define UNGUI(key) (MOD(HID_KEY_GUI_RIGHT) | key)

#define MOMENTARY(layer) (layer << 16)
#define TOGGLE(layer) ((0x1u << 20) | MOMENTARY(layer))
#define CUSTOM_KEYCODE(x) (x + 0x01000000)

const uint32_t empty_map[num_layers][num_keys_per_pipe] = {};

#include "./keymap_mac.h"

uint32_t GetKeyFromMap(uint8_t pipe, uint8_t key) {
  for (int i = 0; i < Layers.GetNumActiveLayers(); ++i) {
    uint32_t keycode = keymap[pipe][Layers.GetActiveLayer(i)][key];
    if (keycode == XXX) {
      return 0;
    }
    if (keycode != ___) {
      return keycode;
    }
  }
  return 0;
}

uint32_t RegisterKey(uint32_t keycode) {
  if (keycode == 0) {
    return 0;
  }
  if (keycode >= CUSTOM_KEYCODE(0)) {
    return RegisterCustom(keycode);
  }
  if (keycode & MOMENTARY(0xf)) {
    uint8_t layer = (keycode >> 16) & 0xf;
    if (keycode & TOGGLE(0)) {
      Layers.ToggleLayer(layer);
    } else {
      Layers.ActivateLayer(layer);
    }
  }
  if (keycode & 0xff) {
    Hid.SetPerKeyMods((keycode & 0xff00) >> 8);
    Hid.AddToReport(keycode & 0xff);
    Hid.GenerateReport();
    Hid.ResetPerKeyMods();
  }
  return keycode;
}

void UnregisterKey(uint32_t keycode) {
  if (keycode == 0) {
    return;
  }
  if (keycode >= CUSTOM_KEYCODE(0)) {
    return UnregisterCustom(keycode);
  }
  if (keycode & MOMENTARY(0xf)) {
    uint8_t layer = (keycode >> 16) & 0xf;
    if (keycode & TOGGLE(0)) {
      // No-op.  Toggle only on register.
    } else {
      Layers.DeactivateLayer((keycode >> 16) & 0xf);
    }
  }
  if (keycode & 0xff) {
    Hid.ClearFromReport(keycode & 0xff);
    Hid.GenerateReport();
  }
}

void UpdatePipe(uint8_t pipe, uint32_t new_state) {
  static uint32_t pipe_state[num_pipes] = { 0 };
  static uint32_t release_keymap[num_pipes][num_keys_per_pipe] = { 0 };
  uint32_t old_state = pipe_state[pipe];
  if (old_state == new_state) {
    return;
  } else {
    for (int i = 0; i < num_keys_per_pipe; ++i) {
      uint32_t bit = 1ul << i;
      if ((old_state & bit) != (new_state & bit)) {
        if (new_state & bit) {
          release_keymap[pipe][i] = RegisterKey(GetKeyFromMap(pipe, i));
        } else {
          UnregisterKey(release_keymap[pipe][i]);
        }
      }
    }
  }
  pipe_state[pipe] = new_state;
}

///////////////////////////////////////// MAIN

void setup() {
#if DEBUG
  Serial.begin(9600);
#endif
  Hid.Init();
  Midi.Init();
  while (!TinyUSBDevice.mounted()) {
    delay(1);
  }
  Radio.Init();
}

void HandleMidi(uint8_t pipe, uint8_t* msg, uint8_t bytes) {
  if (bytes % 3 != 0) {
    return;
  }
  for (int i = 0; i < bytes; i += 3) {
    MIDI.send(
      (midi::MidiType)(msg[i] & 0xf0), // Midi command
      msg[i + 1],
      msg[i + 2],
      (msg[i] & 0x0f) + 1  // Channel
    );
  }
}

void loop() {
  delay(1);
  if (Radio.HasMessage() && TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }
  Hid.SendReports();
  static uint8_t pipe, length;
  static uint8_t data[256];
  while (Radio.DequeueMessage(&pipe, &length, data)) {
    if (pipe <= 5 && length == 4) { // Keyboards.
      UpdatePipe(pipe, *(uint32_t*)data);
    }
    if (pipe == 6) { // 3 faders.
      HandleMidi(pipe, data, length);
    }
    if (pipe == 7) { // Midi keyboard.
      MidiKeys.HandlePacket(data, length);
    }
#if DEBUG
    Serial.print(pipe);
    Serial.print(": ");
    for (int i = 0; i < length; ++i) {
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.println("");
#endif
  }
}
