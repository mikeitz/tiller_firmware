enum Layer {
  LAYER_SYM = 1,
  LAYER_NUM,
};

enum CustomKeycode {
  TAB_OR_F4 = CUSTOM_KEYCODE(0),
  CTRL_OR_STAB,
  NUM_OR_TAB,
  MAC_COMMAND,
  MAC_ALT,
};

uint8_t command_stab_mode = 0;
uint8_t alt_stab_mode = 0;

uint8_t UpdateStab() {
  if (command_stab_mode == 1) {
    command_stab_mode = 2;
    UnregisterKey(HID_KEY_GUI_LEFT);
    RegisterKey(HID_KEY_CONTROL_LEFT);
  }
  if (alt_stab_mode == 1) {
    alt_stab_mode = 2;
    UnregisterKey(HID_KEY_ALT_LEFT);
    RegisterKey(HID_KEY_GUI_LEFT);
  }
  return command_stab_mode || alt_stab_mode;
}

uint32_t RegisterCustom(uint32_t keycode) {
  // Return the key to pass to unregister on keyup.
  switch (keycode) {
  case MAC_COMMAND:
    command_stab_mode = 1;
    RegisterKey(HID_KEY_GUI_LEFT);
    return MAC_COMMAND;
  case MAC_ALT:
    alt_stab_mode = 1;
    RegisterKey(HID_KEY_ALT_LEFT);
    return MAC_ALT;
  case CTRL_OR_STAB:
    if (UpdateStab()) {
      return RegisterKey(SHIFT(HID_KEY_TAB));
    } else {
      return RegisterKey(HID_KEY_CONTROL_LEFT);
    }
  case NUM_OR_TAB:
    if (UpdateStab()) {
      return RegisterKey(HID_KEY_TAB);
    } else {
      return RegisterKey(MOMENTARY(LAYER_NUM));
    }
  default:
    Serial.println("missing keycode");
    Serial.println(keycode, HEX);
    return keycode;
  }
}

void UnregisterCustom(uint32_t keycode) {
  switch (keycode) {
  case MAC_COMMAND:
    if (command_stab_mode == 2) {
      UnregisterKey(HID_KEY_CONTROL_LEFT);
    } else {
      UnregisterKey(HID_KEY_GUI_LEFT);
    }
    command_stab_mode = 0;
    break;
  case MAC_ALT:
    if (alt_stab_mode == 2) {
      UnregisterKey(HID_KEY_GUI_LEFT);
    } else {
      UnregisterKey(HID_KEY_ALT_LEFT);
    }
    alt_stab_mode = 0;
    break;
  default:
    return;
  }
}

// HID_KEY_CONTROL_LEFT
// HID_KEY_ALT_LEFT

const uint32_t left_map[num_layers][num_keys_per_pipe] = {
  [LAYER_BASE] = {
    TAB_OR_F4, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, CTRL_OR_STAB,
    MAC_COMMAND, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_SHIFT_LEFT,
    MAC_ALT, HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, NUM_OR_TAB,
  },
  [LAYER_SYM] = {
    ___, SHIFT(HID_KEY_1), SHIFT(HID_KEY_2), SHIFT(HID_KEY_BRACKET_LEFT), SHIFT(HID_KEY_BRACKET_RIGHT), SHIFT(HID_KEY_APOSTROPHE), ___,
    ___, SHIFT(HID_KEY_3), SHIFT(HID_KEY_8), SHIFT(HID_KEY_9), SHIFT(HID_KEY_0), UNSHIFT(HID_KEY_APOSTROPHE), ___,
    ___, SHIFT(HID_KEY_7), SHIFT(HID_KEY_BACKSLASH), UNSHIFT(HID_KEY_BRACKET_LEFT), UNSHIFT(HID_KEY_BRACKET_RIGHT), UNSHIFT(HID_KEY_GRAVE), ___,
  },
  [LAYER_NUM] = {
    HID_KEY_ESCAPE, UNSHIFT(HID_KEY_MINUS), HID_KEY_7, HID_KEY_8, HID_KEY_9, SHIFT(HID_KEY_5), ___,
    ___, HID_KEY_0, HID_KEY_4, HID_KEY_5, HID_KEY_6, SHIFT(HID_KEY_4), ___,
    ___, UNSHIFT(HID_KEY_PERIOD), HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_SPACE, ___,
  },
};

const uint32_t right_map[num_layers][num_keys_per_pipe] = {
  [LAYER_BASE] = {
     ___, HID_KEY_Y, HID_KEY_U, HID_KEY_I, HID_KEY_O, HID_KEY_P, HID_KEY_BACKSPACE,
     HID_KEY_SPACE, HID_KEY_H, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_SEMICOLON, HID_KEY_ENTER,
     MOMENTARY(LAYER_SYM), HID_KEY_N, HID_KEY_M, HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH, HID_KEY_DELETE,
  },
  [LAYER_SYM] = {
    ___, SHIFT(HID_KEY_6), ALT(HID_KEY_ARROW_LEFT), HID_KEY_ARROW_UP, ALT(HID_KEY_ARROW_RIGHT), SHIFT(HID_KEY_MINUS), ___,
    ___, HID_KEY_HOME, HID_KEY_ARROW_LEFT, HID_KEY_ARROW_DOWN, HID_KEY_ARROW_RIGHT, HID_KEY_END, ___,
    ___, SHIFT(HID_KEY_GRAVE), UNSHIFT(HID_KEY_MINUS), UNSHIFT(HID_KEY_EQUAL), SHIFT(HID_KEY_EQUAL), UNSHIFT(HID_KEY_BACKSLASH), ___,
  },
  [LAYER_NUM] = {
    ___, HID_KEY_PAGE_UP, HID_KEY_F7, HID_KEY_F8, HID_KEY_F9, HID_KEY_F11, ___,
    ___, HID_KEY_PAGE_DOWN, HID_KEY_F4, HID_KEY_F5, HID_KEY_F6, HID_KEY_F10, ___,
    HID_KEY_SHIFT_LEFT, HID_KEY_INSERT, HID_KEY_F1, HID_KEY_F2, HID_KEY_F3, HID_KEY_F12, ___,
  },
};

const uint32_t(*keymap[num_pipes])[num_keys_per_pipe] = {
  empty_map,
  left_map,
  right_map,
  empty_map,
  left_map,
  right_map,
  empty_map,
  empty_map,
};