#pragma once
#include <stdint.h>
// Per-frame input snapshot (ADR-0005, ARCHITECTURE §4.4). Data-oriented: one flat
// sample per frame; edge detection (pressed-this-frame) is computed engine-side by
// diffing snapshots. M0.3 uses basic WM_KEY*/mouse; Raw Input + the full KeyCode
// disambiguation (E0/E1, L/R modifiers) land in M3.

// OS-independent key codes. Contiguous runs (A-Z, 0-9, F1-F12) so the backend can
// map ranges. KEY_UNKNOWN=0 is the catch-all; KEY_COUNT bounds the down[] array.
typedef enum KeyCode {
    KEY_UNKNOWN = 0,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K,
    KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V,
    KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_SPACE, KEY_ESCAPE, KEY_ENTER, KEY_TAB, KEY_BACKSPACE,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_RALT,
    KEY_COUNT
} KeyCode;

typedef struct KeyboardState {
    uint8_t  down[KEY_COUNT];   // 1 = currently held
    uint32_t transitions;       // press+release count this frame
} KeyboardState;

typedef struct MouseState {
    int32_t dx, dy;             // movement since last frame (client px)
    int32_t x, y;               // latest client-space position
    int32_t wheel;              // wheel notches this frame
    uint8_t buttons;            // bit0=L bit1=R bit2=M
} MouseState;

typedef struct PlatformFrameInput {
    KeyboardState keyboard;
    MouseState    mouse;
    bool          window_resized;
    bool          window_minimized;
    bool          window_focused;
    int32_t       fb_width, fb_height;
    uint32_t      text_utf32[16];   // tiny text ring for this frame
    uint32_t      text_count;
} PlatformFrameInput;
