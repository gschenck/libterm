/* White-box unit tests for the pure Windows key-event mappers
 * (lt__win_key_event / lt__win_bare_modifier_key). No console I/O: we
 * hand-build KEY_EVENT_RECORD structs and assert the resulting lt_event.
 * Mirrors how the POSIX side unit-tests the pure decoder in
 * test_posix_input_parse.c. */
#include "libterm/libterm.h"
#include "platform/windows/win_internal.h"

#include <assert.h>
#include <string.h>

static KEY_EVENT_RECORD mk(WORD vk, WORD scan, WCHAR ch, DWORD ctrl, BOOL down,
                           WORD repeat) {
  KEY_EVENT_RECORD k;
  memset(&k, 0, sizeof k);
  k.bKeyDown = down;
  k.wRepeatCount = repeat;
  k.wVirtualKeyCode = vk;
  k.wVirtualScanCode = scan;
  k.uChar.UnicodeChar = ch;
  k.dwControlKeyState = ctrl;
  return k;
}

int main(void) {
  struct lt_event ev;

  /* W1: action = PRESS on key-down, repeat 1. F1 has no character (cp 0). */
  KEY_EVENT_RECORD f1 = mk(VK_F1, 0, 0, 0, TRUE, 1);
  assert(lt__win_key_event(&f1, 0, 0, &ev) == 1);
  assert(ev.type == LT_EVENT_KEY);
  assert(ev.key == LT_KEY_F1);
  assert(ev.action == LT_KEY_PRESS);

  /* W1: wRepeatCount > 1 -> REPEAT. */
  KEY_EVENT_RECORD f1r = mk(VK_F1, 0, 0, 0, TRUE, 3);
  assert(lt__win_key_event(&f1r, 0, 0, &ev) == 1);
  assert(ev.action == LT_KEY_REPEAT);

  /* W1: key-up in the modern model -> a RELEASE event. */
  KEY_EVENT_RECORD f1u = mk(VK_F1, 0, 0, 0, FALSE, 1);
  assert(lt__win_key_event(&f1u, 0, 0, &ev) == 1);
  assert(ev.action == LT_KEY_RELEASE);
  assert(ev.key == LT_KEY_F1);

  /* W1: key-up in compat mode -> no event (legacy drops key-up). */
  assert(lt__win_key_event(&f1u, 0, LT_INPUT_COMPAT, &ev) == 0);

  /* W2: bare modifiers, left/right disambiguation. */
  assert(lt__win_bare_modifier_key(&(KEY_EVENT_RECORD){
             .wVirtualKeyCode = VK_SHIFT, .wVirtualScanCode = 0x2A}) ==
         LT_KEY_LEFT_SHIFT);
  assert(lt__win_bare_modifier_key(&(KEY_EVENT_RECORD){
             .wVirtualKeyCode = VK_SHIFT, .wVirtualScanCode = 0x36}) ==
         LT_KEY_RIGHT_SHIFT);

  KEY_EVENT_RECORD lsh = mk(VK_SHIFT, 0x2A, 0, SHIFT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&lsh, 0, 0, &ev) == 1);
  assert(ev.key == LT_KEY_LEFT_SHIFT);
  assert(ev.ch == 0);
  assert(ev.mod & LT_MOD_SHIFT);

  KEY_EVENT_RECORD rctrl =
      mk(VK_CONTROL, 0, 0, RIGHT_CTRL_PRESSED | ENHANCED_KEY, TRUE, 1);
  assert(lt__win_key_event(&rctrl, 0, 0, &ev) == 1);
  assert(ev.key == LT_KEY_RIGHT_CTRL);

  KEY_EVENT_RECORD lalt = mk(VK_MENU, 0, 0, LEFT_ALT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&lalt, 0, 0, &ev) == 1);
  assert(ev.key == LT_KEY_LEFT_ALT);

  KEY_EVENT_RECORD caps = mk(VK_CAPITAL, 0, 0, 0, TRUE, 1);
  assert(lt__win_key_event(&caps, 0, 0, &ev) == 1);
  assert(ev.key == LT_KEY_CAPS_LOCK);

  /* W2: bare modifiers are suppressed in compat mode. */
  assert(lt__win_key_event(&lsh, 0, LT_INPUT_COMPAT, &ev) == 0);

  /* W3: Shift+Tab -> BACK_TAB; plain Tab -> TAB. */
  KEY_EVENT_RECORD btab = mk(VK_TAB, 0, 0x09, SHIFT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&btab, 0x09, 0, &ev) == 1);
  assert(ev.key == LT_KEY_BACK_TAB);

  KEY_EVENT_RECORD tab = mk(VK_TAB, 0, 0x09, 0, TRUE, 1);
  assert(lt__win_key_event(&tab, 0x09, 0, &ev) == 1);
  assert(ev.key == LT_KEY_TAB);

  /* Printable character: cp flows to ev.ch. */
  KEY_EVENT_RECORD a = mk('A', 0, L'a', 0, TRUE, 1);
  assert(lt__win_key_event(&a, (lt_uchar)L'a', 0, &ev) == 1);
  assert(ev.ch == (lt_uchar)'a');
  assert(ev.key == 0);

  /* Ctrl+letter, modern model: disambiguated via the virtual-key to
   * ch=lowercase + CTRL (key=0), matching POSIX+kitty. Ctrl+C -> ch='c'+CTRL.
   */
  KEY_EVENT_RECORD ctrlc = mk('C', 0, 0x03, LEFT_CTRL_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&ctrlc, 0x03, 0, &ev) == 1);
  assert(ev.ch == (lt_uchar)'c');
  assert(ev.key == 0);
  assert(ev.mod == LT_MOD_CTRL);

  /* Ctrl+letter, compat model: termbox2 control-byte collapse (key=0x03). */
  assert(lt__win_key_event(&ctrlc, 0x03, LT_INPUT_COMPAT, &ev) == 1);
  assert(ev.key == 0x03);
  assert(ev.ch == 0);
  assert(ev.mod == 0);

  /* Ctrl+I is disambiguated to ch='i'+CTRL (NOT Tab) in the modern model -
   * the console keeps VK_I distinct from VK_TAB. */
  KEY_EVENT_RECORD ctrli = mk('I', 0, 0x09, LEFT_CTRL_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&ctrli, 0x09, 0, &ev) == 1);
  assert(ev.ch == (lt_uchar)'i');
  assert(ev.key == 0);
  assert(ev.mod == LT_MOD_CTRL);

  /* The real Tab key (VK_TAB) is unaffected -> LT_KEY_TAB. */
  KEY_EVENT_RECORD realtab = mk(VK_TAB, 0, 0x09, 0, TRUE, 1);
  assert(lt__win_key_event(&realtab, 0x09, 0, &ev) == 1);
  assert(ev.key == LT_KEY_TAB);
  assert(ev.ch == 0);

  /* Ctrl+Shift+A -> ch='a' (lowercase base) + CTRL|SHIFT. */
  KEY_EVENT_RECORD csa =
      mk('A', 0, 0x01, LEFT_CTRL_PRESSED | SHIFT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&csa, 0x01, 0, &ev) == 1);
  assert(ev.ch == (lt_uchar)'a');
  assert(ev.key == 0);
  assert(ev.mod == (LT_MOD_CTRL | LT_MOD_SHIFT));

  /* Ctrl+digit, modern model: the console delivers uChar = 0 for most digits
   * and a control byte for a few (Ctrl+2 = NUL, Ctrl+6 = 0x1E); the digit was
   * lost. Expected: ch = the digit (layout-independent: German Shift+1 is '!'
   * but the key is still '1') + CTRL, like POSIX with kitty (CSI 49;5u). */
  KEY_EVENT_RECORD c1 = mk('1', 0, 0, LEFT_CTRL_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&c1, 0, 0, &ev) == 1);
  assert(ev.key == 0);
  assert(ev.ch == '1');
  assert(ev.mod == LT_MOD_CTRL);

  /* Ctrl+Shift+1 -> '1' + CTRL|SHIFT. */
  KEY_EVENT_RECORD cs1 =
      mk('1', 0, 0, LEFT_CTRL_PRESSED | SHIFT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&cs1, 0, 0, &ev) == 1);
  assert(ev.ch == '1');
  assert(ev.mod == (LT_MOD_CTRL | LT_MOD_SHIFT));

  /* Ctrl+6 arrives as control byte 0x1E -- still the digit, not key 0x1E. */
  KEY_EVENT_RECORD c6 = mk('6', 0, 0x1E, LEFT_CTRL_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&c6, 0x1E, 0, &ev) == 1);
  assert(ev.ch == '6');
  assert(ev.mod == LT_MOD_CTRL);

  /* Compat model keeps the termbox2 control-byte semantics for Ctrl+6. */
  assert(lt__win_key_event(&c6, 0x1E, LT_INPUT_COMPAT, &ev) == 1);
  assert(ev.key == 0x1E);
  assert(ev.ch == 0);

  /* AltGr+digit (Ctrl+Alt with a printable char, e.g. German AltGr+2 = ²) is
   * a character and must stay one. */
  KEY_EVENT_RECORD altgr2 =
      mk('2', 0, 0xB2, LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED, TRUE, 1);
  assert(lt__win_key_event(&altgr2, 0xB2, 0, &ev) == 1);
  assert(ev.ch == 0xB2);

  return 0;
}
