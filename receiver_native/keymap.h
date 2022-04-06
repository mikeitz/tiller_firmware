#define ___ 0x00000000u
#define XXX 0xffffffffu

#define MOD(mod) (1 << (8 + (mod & 0xf)))
#define S(key) (MOD(HID_KEY_SHIFT_LEFT) | key)
#define C(key) (MOD(HID_KEY_CONTROL_LEFT) | key)
#define US(key) (MOD(HID_KEY_SHIFT_RIGHT) | key)

#define MO(layer) (layer << 16)
#define TG(layer) ((0x1u << 20) | MO(layer))

// bits:
// all 0 = transparent to next layer
// all f = no action, opaque to other layers, stop
// 0-8 keycode
// 9-12 per key modifiers; 1=ctrl 2=shift 4=alt 8=gui
// 13-16 per key anti-modifiers; as above
// 17-24 opcode
//     0x0X = momentary layer X
//     0x1X = toggle layer X
// 25-32 > 0, custom keycode

#define LAYER_BASE 0
#define LAYER_GAME 1
#define LAYER_NUM 2
#define LAYER_SYM 3
#define LAYER_FN 4

#define CUSTOM_KEYCODE(x) (x + 0x01000000)

#define TAB_OR_F4 HID_KEY_TAB // (CUSTOM_KEYCODE + 1)
#define GUI_OR_STAB HID_KEY_GUI_LEFT // (CUSTOM_KEYCODE + 2)
#define NUM_OR_TAB MO(LAYER_NUM) // (CUSTOM_KEYCODE + 3)

const uint32_t leftPipeMap[num_layers][num_keys_per_pipe] = {
  [LAYER_BASE] = {
    TAB_OR_F4, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_T, GUI_OR_STAB,
    HID_KEY_CONTROL_LEFT, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_G, HID_KEY_SHIFT_LEFT,
    HID_KEY_ALT_LEFT, HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, NUM_OR_TAB,
  },
  [LAYER_GAME] = {
    HID_KEY_TAB, HID_KEY_T, HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R, HID_KEY_ALT_LEFT,
    HID_KEY_G, HID_KEY_SHIFT_LEFT, HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F, HID_KEY_SPACE,
    HID_KEY_Z, HID_KEY_CONTROL_LEFT, HID_KEY_X, HID_KEY_C, HID_KEY_V, HID_KEY_B, ___,
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
    ___, XXX, XXX, XXX, XXX, XXX, ___,
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
  [LAYER_GAME] = {},
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
    ___, XXX, XXX, TG(LAYER_GAME), XXX, XXX, ___,
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