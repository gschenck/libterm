/* White-box test for Ctrl + <key without a control character> on Windows.
 *
 * On non-US layouts the console reports Ctrl+<OEM key> with uChar = 0 (German
 * Ctrl+Ä / Ctrl+Ö on VK_OEM_7 / VK_OEM_3), or with a control byte that belongs
 * to a different key on the US layout (German Ctrl+Ü on VK_OEM_1 yields 0x1B,
 * the same byte as Escape). lt__win_key_event must report such keys as
 * ch = <the key's base character> + CTRL in the modern model instead of
 * dropping them or mis-reporting them as Escape.
 *
 * MapVirtualKeyW depends on the active keyboard layout, so it is replaced by a
 * stub that models a German layout; plat_keys.c is compiled into this test
 * with the stub in place (no console, no library link). */
#include "libterm/libterm.h"
#include "platform/windows/win_internal.h"

#include <assert.h>
#include <string.h>

/* German layout: VK_OEM_7 -> 'Ä', VK_OEM_3 -> 'Ö', VK_OEM_1 -> 'Ü',
 * VK_OEM_PLUS -> '+'. Anything else is unmapped. */
static UINT test_map_virtual_key(UINT vk, UINT type) {
  if (type != MAPVK_VK_TO_CHAR)
    return 0;
  switch (vk) {
  case VK_OEM_7:
    return 0xC4; /* Ä */
  case VK_OEM_3:
    return 0xD6; /* Ö */
  case VK_OEM_1:
    return 0xDC; /* Ü */
  case VK_OEM_PLUS:
    return '+';
  case VK_OEM_4:
    return 0xDF; /* ß */
  default:
    return 0;
  }
}
#define MapVirtualKeyW test_map_virtual_key
#include "platform/windows/plat_keys.c"
#undef MapVirtualKeyW

static KEY_EVENT_RECORD mk(WORD vk, WCHAR ch, DWORD ctrl) {
  KEY_EVENT_RECORD k;
  memset(&k, 0, sizeof k);
  k.bKeyDown = TRUE;
  k.wRepeatCount = 1;
  k.wVirtualKeyCode = vk;
  k.uChar.UnicodeChar = ch;
  k.dwControlKeyState = ctrl;
  return k;
}

int main(void) {
  struct lt_event ev;

  /* Ctrl+Ä: the console delivers uChar 0. Expect ch 'ä' + CTRL. */
  KEY_EVENT_RECORD ae = mk(VK_OEM_7, 0, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&ae, 0, 0, &ev) == 1);
  assert(ev.type == LT_EVENT_KEY);
  assert(ev.ch == 0xE4);
  assert(ev.key == 0);
  assert(ev.mod == LT_MOD_CTRL);

  /* Ctrl+Shift+Ö: still the lowercase base character, SHIFT kept in mod. */
  KEY_EVENT_RECORD oe = mk(VK_OEM_3, 0, LEFT_CTRL_PRESSED | SHIFT_PRESSED);
  assert(lt__win_key_event(&oe, 0, 0, &ev) == 1);
  assert(ev.ch == 0xF6);
  assert(ev.mod == (LT_MOD_CTRL | LT_MOD_SHIFT));

  /* Ctrl+Ü: the layout maps it to 0x1B. Must not become Escape. */
  KEY_EVENT_RECORD ue = mk(VK_OEM_1, 0x1B, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&ue, 0x1B, 0, &ev) == 1);
  assert(ev.ch == 0xFC);
  assert(ev.key == 0);
  assert(ev.mod == LT_MOD_CTRL);

  /* Ctrl+ß: a lowercase Latin-1 base character is reported unchanged. */
  KEY_EVENT_RECORD sz = mk(VK_OEM_4, 0, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&sz, 0, 0, &ev) == 1);
  assert(ev.ch == 0xDF);
  assert(ev.mod == LT_MOD_CTRL);

  /* Ctrl+'+' (ASCII base character, uChar 0): not touched by the OEM rule,
   * still nothing reportable. */
  KEY_EVENT_RECORD plus = mk(VK_OEM_PLUS, 0, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&plus, 0, 0, &ev) == 0);

  /* US-style Ctrl+[ (VK_OEM_4 with an ASCII base char): keep the control
   * byte. Modelled by an unmapped VK with cp 0x1B. */
  KEY_EVENT_RECORD br = mk(VK_OEM_6, 0x1B, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&br, 0x1B, 0, &ev) == 1);
  assert(ev.key == 0x1B);
  assert(ev.ch == 0);
  assert(ev.mod == 0);

  /* AltGr (Ctrl+Alt) + OEM key with uChar 0: not a Ctrl chord, unchanged. */
  KEY_EVENT_RECORD altgr =
      mk(VK_OEM_7, 0, LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED);
  assert(lt__win_key_event(&altgr, 0, 0, &ev) == 0);

  /* Compat mode keeps the legacy behaviour: Ctrl+Ü stays the ESC byte. */
  assert(lt__win_key_event(&ue, 0x1B, LT_INPUT_COMPAT, &ev) == 1);
  assert(ev.key == 0x1B);
  assert(ev.ch == 0);

  /* Ctrl+letter is unaffected by the OEM rule (handled before it). */
  KEY_EVENT_RECORD ci = mk('I', 0x09, LEFT_CTRL_PRESSED);
  assert(lt__win_key_event(&ci, 0x09, 0, &ev) == 1);
  assert(ev.ch == 'i');
  assert(ev.mod == LT_MOD_CTRL);

  return 0;
}
