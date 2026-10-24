#include "libterm/libterm.h"
#include "win_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>

WORD lt__win_vk_to_lt_key(WORD vk) {
  // clang-format off
  switch(vk){
  /* control */
  case VK_RETURN: return LT_KEY_ENTER;
  case VK_ESCAPE: return LT_KEY_ESC;
  case VK_BACK:   return LT_KEY_BACKSPACE;
  case VK_TAB:    return LT_KEY_TAB;

  /* arrows */
  case VK_UP:     return LT_KEY_ARROW_UP;
  case VK_DOWN:   return LT_KEY_ARROW_DOWN;
  case VK_LEFT:   return LT_KEY_ARROW_LEFT;
  case VK_RIGHT:  return LT_KEY_ARROW_RIGHT;

  /* navigation */
  case VK_INSERT: return LT_KEY_INSERT;
  case VK_DELETE: return LT_KEY_DELETE;
  case VK_HOME:   return LT_KEY_HOME;
  case VK_END:    return LT_KEY_END;
  case VK_PRIOR:  return LT_KEY_PGUP;
  case VK_NEXT:   return LT_KEY_PGDN;

  /* function keys F1..F12 */
  case VK_F1:  return LT_KEY_F1;
  case VK_F2:  return LT_KEY_F2;
  case VK_F3:  return LT_KEY_F3;
  case VK_F4:  return LT_KEY_F4;
  case VK_F5:  return LT_KEY_F5;
  case VK_F6:  return LT_KEY_F6;
  case VK_F7:  return LT_KEY_F7;
  case VK_F8:  return LT_KEY_F8;
  case VK_F9:  return LT_KEY_F9;
  case VK_F10: return LT_KEY_F10;
  case VK_F11: return LT_KEY_F11;
  case VK_F12: return LT_KEY_F12;

  default: return 0;
  }
  // clang-format on
}

uint16_t lt__win_bare_modifier_key(const KEY_EVENT_RECORD *k) {
  /* ReadConsoleInputW reports the generic VK_SHIFT/VK_CONTROL/VK_MENU for
   * modifier keys (the explicit VK_LSHIFT/VK_LCONTROL/... codes come only from
   * GetKeyState, never from console key events). Left/right is recovered from
   * the scan code (shift) or the ENHANCED_KEY flag (ctrl/alt). VK_LWIN/VK_RWIN
   * and VK_CAPITAL are reported directly. */
  bool enhanced = (k->dwControlKeyState & ENHANCED_KEY) != 0;
  switch (k->wVirtualKeyCode) {
  case VK_SHIFT:
    /* scan code 0x36 is the right shift, 0x2A the left. */
    return (k->wVirtualScanCode == 0x36) ? LT_KEY_RIGHT_SHIFT
                                         : LT_KEY_LEFT_SHIFT;
  case VK_CONTROL:
    return enhanced ? LT_KEY_RIGHT_CTRL : LT_KEY_LEFT_CTRL;
  case VK_MENU:
    return enhanced ? LT_KEY_RIGHT_ALT : LT_KEY_LEFT_ALT;
  case VK_LWIN:
    return LT_KEY_LEFT_SUPER;
  case VK_RWIN:
    return LT_KEY_RIGHT_SUPER;
  case VK_CAPITAL:
    return LT_KEY_CAPS_LOCK;
  default:
    return 0;
  }
}

int lt__win_key_event(const KEY_EVENT_RECORD *k, lt_uchar cp, int input_mode,
                      struct lt_event *ev) {
  bool compat = (input_mode & LT_INPUT_COMPAT) != 0;
  bool down = k->bKeyDown != 0;

  /* Key-up: only the modern model reports a RELEASE; compat drops it (legacy
   * terminals cannot report releases). */
  if (!down && compat)
    return 0;

  memset(ev, 0, sizeof(*ev));
  ev->type = LT_EVENT_KEY;

  /* action: RELEASE on key-up, REPEAT on a coalesced auto-repeat, else PRESS.
   * Compat mode is always PRESS (matches POSIX legacy behavior). */
  if (!down)
    ev->action = LT_KEY_RELEASE;
  else if (!compat && k->wRepeatCount > 1)
    ev->action = LT_KEY_REPEAT;
  else
    ev->action = LT_KEY_PRESS;

  /* modifiers from the console control-key state (parity with POSIX, which
   * carries these on every key in the modern model). */
  DWORD st = k->dwControlKeyState;
  if (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
    ev->mod |= LT_MOD_CTRL;
  if (st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
    ev->mod |= LT_MOD_ALT;
  if (st & SHIFT_PRESSED)
    ev->mod |= LT_MOD_SHIFT;

  /* Bare modifier / lock key (modern model only). */
  uint16_t bare = lt__win_bare_modifier_key(k);
  if (bare) {
    if (compat)
      return 0;
    ev->key = bare;
    ev->ch = 0;
    return 1;
  }

  /* Back-tab: Shift+Tab (mirrors POSIX CSI Z). */
  if (k->wVirtualKeyCode == VK_TAB && (st & SHIFT_PRESSED)) {
    ev->key = LT_KEY_BACK_TAB;
    ev->ch = 0;
    return 1;
  }

  /* Named keys (arrows, F-keys, nav, Enter/Tab/Backspace/Esc). */
  WORD mapped = lt__win_vk_to_lt_key(k->wVirtualKeyCode);
  if (mapped) {
    ev->key = mapped;
    ev->ch = 0;
    return 1;
  }

  /* Ctrl+letter, modern model: the console keeps the letter virtual-key
   * (VK_I is distinct from VK_TAB) even though uChar collapsed to a control
   * byte, so report it disambiguated as ch=lowercase + the held modifiers,
   * matching POSIX+kitty - instead of the ambiguous control byte (Ctrl+I vs
   * Tab, Ctrl+M vs Enter). Gated on cp being the Ctrl+letter control char
   * (0x01-0x1A) AND a letter VK, which is AltGr-safe: AltGr+letter yields a
   * printable char, not a control byte, so it never reaches here. The real
   * Tab/Enter/Backspace keys have their own VKs and were handled by
   * lt__win_vk_to_lt_key above. Compat mode keeps the termbox2 control-byte
   * collapse below. */
  if (!compat && cp >= 0x01 && cp <= 0x1A && k->wVirtualKeyCode >= 'A' &&
      k->wVirtualKeyCode <= 'Z') {
    ev->ch =
        (lt_uchar)(k->wVirtualKeyCode + 0x20); /* VK 'A'..'Z' -> 'a'..'z' */
    ev->key = 0;
    /* ev->mod already carries CTRL (+ SHIFT/ALT) from the control-key state. */
    return 1;
  }

  /* Ctrl+digit and Ctrl+Shift+digit, modern model: the console delivers
   * uChar = 0 for most digits and a control byte for a few (Ctrl+2 = NUL,
   * Ctrl+6 = 0x1E), so the digit was lost (Windows Terminal, bookmarks /
   * favourites bound to Ctrl+1..9). Report the key's digit (VK '0'..'9',
   * independent of the layout -- German Shift+1 is '!', the key is still
   * '1') with the held modifiers, like POSIX with kitty (CSI 49;5u / ;6u).
   * Ctrl+Alt+digit is included on purpose: AltGr+digit yields a printable
   * character (cp >= 0x20) and never reaches this branch, so an Alt check
   * would only block real Ctrl+Alt+digit. Numpad digits (VK_NUMPAD0-9) are
   * left alone. */
  if (!compat && cp < 0x20 && (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) &&
      k->wVirtualKeyCode >= '0' && k->wVirtualKeyCode <= '9') {
    ev->ch = (lt_uchar)k->wVirtualKeyCode;
    ev->key = 0;
    return 1;
  }

  /* Standalone control byte (Ctrl+<non-letter>, or any Ctrl+letter in compat
   * mode): termbox2 model -> key = byte, ch = 0, mod = 0, matching POSIX. */
  if (cp != 0 && (cp < 0x20 || cp == 0x7F)) {
    ev->mod = 0;
    ev->key = (uint16_t)cp;
    ev->ch = 0;
    return 1;
  }

  /* Printable character. */
  if (cp != 0) {
    ev->ch = cp;
    return 1;
  }

  /* Nothing reportable (e.g. a key-up of a key with no character in the modern
   * model, or a dead record). */
  return 0;
}

/* Pick the button key for a press/drag from the held-button bitmask, or
 * LT_KEY_MOUSE_RELEASE if no button is down. */
static uint16_t lt__win_mouse_button(DWORD buttons) {
  if (buttons & FROM_LEFT_1ST_BUTTON_PRESSED)
    return LT_KEY_MOUSE_LEFT;
  if (buttons & RIGHTMOST_BUTTON_PRESSED)
    return LT_KEY_MOUSE_RIGHT;
  if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED)
    return LT_KEY_MOUSE_MIDDLE;
  return LT_KEY_MOUSE_RELEASE;
}

int lt__win_mouse_event(const MOUSE_EVENT_RECORD *m, SHORT viewport_left,
                        SHORT viewport_top, struct lt_event *ev) {
  DWORD flags = m->dwEventFlags;

  /* libterm has no horizontal-wheel key; drop those records. */
  if (flags & MOUSE_HWHEELED)
    return 0;

  memset(ev, 0, sizeof(*ev));
  ev->type = LT_EVENT_MOUSE;
  ev->action = LT_KEY_PRESS;

  if (flags & MOUSE_WHEELED) {
    /* High word of dwButtonState is a signed wheel delta: positive is up. */
    SHORT delta = (SHORT)HIWORD(m->dwButtonState);
    ev->key = (delta > 0) ? LT_KEY_MOUSE_WHEEL_UP : LT_KEY_MOUSE_WHEEL_DOWN;
  } else {
    ev->key = lt__win_mouse_button(m->dwButtonState);
    if (flags & MOUSE_MOVED)
      ev->mod |= LT_MOD_MOTION; /* drag carries the held button in ev->key */
  }

  DWORD st = m->dwControlKeyState;
  if (st & SHIFT_PRESSED)
    ev->mod |= LT_MOD_SHIFT;
  if (st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
    ev->mod |= LT_MOD_ALT;
  if (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
    ev->mod |= LT_MOD_CTRL;

  int x = (int)m->dwMousePosition.X - (int)viewport_left;
  int y = (int)m->dwMousePosition.Y - (int)viewport_top;
  ev->x = x > 0 ? x : 0;
  ev->y = y > 0 ? y : 0;
  return 1;
}
