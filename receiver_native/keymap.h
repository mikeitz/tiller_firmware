#define ___ 0
#define XXX 0xff00u
#define MO(layer) (0x0100u | layer)
#define LM(mod, layer) mod
#define S(key) key
#define C(key) key
#define US(key) key

#define LAYER_BASE 0
#define LAYER_TAB 1
#define LAYER_GAME 2
#define LAYER_NUM 3
#define LAYER_SYM 4
#define LAYER_FN 5

const uint32_t leftPipeMap[num_layers][num_keys_per_pipe] = {
  [LAYER_BASE] = {
    HID_KEY_TAB, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, HID_KEY_GUI_LEFT,
    LM(HID_KEY_CONTROL_LEFT, LAYER_TAB), HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_SHIFT_LEFT,
    LM(HID_KEY_ALT_LEFT, LAYER_TAB), HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, MO(LAYER_NUM),
  },
  [LAYER_TAB] = {
    HID_KEY_F4, ___, ___, ___, ___, ___, S(HID_KEY_TAB),
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, HID_KEY_TAB,
  },
  [LAYER_GAME] = {
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
  },
  [LAYER_NUM] = {
    HID_KEY_ESCAPE, HID_KEY_MINUS, HID_KEY_7, HID_KEY_8, HID_KEY_9, S(HID_KEY_5), ___,
    ___, HID_KEY_0, HID_KEY_4, HID_KEY_5, HID_KEY_6, S(HID_KEY_4), ___,
    ___, HID_KEY_PERIOD, HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_SPACE, ___,
  },
  [LAYER_SYM] = {
    ___, S(HID_KEY_1), S(HID_KEY_2), S(HID_KEY_BRACKET_LEFT), S(HID_KEY_BRACKET_LEFT), S(HID_KEY_APOSTROPHE), ___,
    ___, S(HID_KEY_3), S(HID_KEY_8), S(HID_KEY_9), S(HID_KEY_0), US(HID_KEY_APOSTROPHE), ___,
    ___, S(HID_KEY_7), S(HID_KEY_BACKSLASH), US(HID_KEY_BRACKET_LEFT), US(HID_KEY_BRACKET_RIGHT), US(HID_KEY_GRAVE), XXX,
  },
  [LAYER_FN] = {
    ___, XXX, XXX, XXX, XXX, XXX, XXX,
    ___, XXX, XXX, XXX, XXX, XXX, ___,
    ___, XXX, XXX, XXX, XXX, XXX, ___,
  },
};

const uint32_t rightPipeMap[num_layers][num_keys_per_pipe] = {
  [LAYER_BASE] = {
     MO(LAYER_SYM), HID_KEY_Y, HID_KEY_U, HID_KEY_I, HID_KEY_O, HID_KEY_P, HID_KEY_BACKSPACE,
     HID_KEY_SPACE, HID_KEY_H, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_SEMICOLON, HID_KEY_ENTER,
     MO(LAYER_FN), HID_KEY_N, HID_KEY_M, HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH, HID_KEY_DELETE,
    },
  [LAYER_TAB] = {
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
  },
  [LAYER_GAME] = {
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
    ___, ___, ___, ___, ___, ___, ___,
  },
  [LAYER_NUM] = {
    ___, HID_KEY_PAGE_UP, HID_KEY_F7, HID_KEY_F8, HID_KEY_F9, HID_KEY_F11, ___,
    ___, HID_KEY_PAGE_DOWN, HID_KEY_F4, HID_KEY_F5, HID_KEY_F6, HID_KEY_F10, ___,
    ___, HID_KEY_INSERT, HID_KEY_F1, HID_KEY_F2, HID_KEY_F3, HID_KEY_F12, ___,
  },
  [LAYER_SYM] = {
    ___, S(HID_KEY_6), C(HID_KEY_ARROW_LEFT), HID_KEY_ARROW_UP, C(HID_KEY_ARROW_LEFT), S(HID_KEY_MINUS), ___,
    ___, HID_KEY_HOME, HID_KEY_ARROW_LEFT, HID_KEY_ARROW_DOWN, HID_KEY_ARROW_RIGHT, HID_KEY_END, ___,
    ___, S(HID_KEY_GRAVE), US(HID_KEY_MINUS), US(HID_KEY_EQUAL), S(HID_KEY_EQUAL), US(HID_KEY_BACKSLASH), ___,
  },
  [LAYER_FN] = {
    ___, XXX, XXX, XXX, XXX, XXX, ___,
    ___, XXX, XXX, XXX, XXX, XXX, ___,
    ___, XXX, XXX, XXX, XXX, XXX, ___,
  },
};

const uint32_t(*keymap[num_pipes])[num_keys_per_pipe] = {
  nullptr,
  leftPipeMap,
  rightPipeMap,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
};