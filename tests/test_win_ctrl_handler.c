/* White-box test for the Windows console control handler.
 *
 * lt__plat_init must register a handler that swallows CTRL_C_EVENT and
 * CTRL_BREAK_EVENT (so Ctrl+C reaches the application as a key even after a
 * child process re-enabled ENABLE_PROCESSED_INPUT on the shared console), and
 * lt__plat_shutdown must remove it again. Every other console event (close,
 * logoff, shutdown) must be left to the default handling.
 *
 * No console is needed: plat_init.c is compiled into this test with the Win32
 * console calls and the output primitives replaced by stubs that record what
 * was registered. */
#include "libterm/libterm.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static PHANDLER_ROUTINE registered = NULL;
static int add_calls = 0, remove_calls = 0;

static BOOL WINAPI stub_set_ctrl_handler(PHANDLER_ROUTINE fn, BOOL add) {
  if (add) {
    registered = fn;
    add_calls++;
  } else {
    assert(fn == registered);
    registered = NULL;
    remove_calls++;
  }
  return TRUE;
}
static HANDLE WINAPI stub_get_std_handle(DWORD which) {
  return (HANDLE)(uintptr_t)(which == STD_INPUT_HANDLE ? 0x10 : 0x20);
}
static BOOL WINAPI stub_get_console_mode(HANDLE h, LPDWORD mode) {
  (void)h;
  *mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
  return TRUE;
}
static BOOL WINAPI stub_set_console_mode(HANDLE h, DWORD mode) {
  (void)h;
  (void)mode;
  return TRUE;
}
static BOOL WINAPI stub_set_cp(UINT cp) {
  (void)cp;
  return TRUE;
}
static int stub_ok_write(const char *b, size_t n) {
  (void)b;
  (void)n;
  return LT_OK;
}
static int stub_ok(void) { return LT_OK; }

#define SetConsoleCtrlHandler stub_set_ctrl_handler
#define GetStdHandle stub_get_std_handle
#define GetConsoleMode stub_get_console_mode
#define SetConsoleMode stub_set_console_mode
#define SetConsoleCP stub_set_cp
#define SetConsoleOutputCP stub_set_cp
#define lt__plat_write stub_ok_write
#define lt__plat_flush stub_ok
#define lt__plat_hide_cursor stub_ok
#define lt__plat_show_cursor stub_ok
#include "platform/windows/plat_init.c"

int main(void) {
  assert(lt__plat_init() == LT_OK);
  assert(add_calls == 1);
  assert(registered != NULL);

  /* The handler consumes Ctrl+C and Ctrl+Break ... */
  assert(registered(CTRL_C_EVENT) == TRUE);
  assert(registered(CTRL_BREAK_EVENT) == TRUE);
  /* ... and nothing else. */
  assert(registered(CTRL_CLOSE_EVENT) == FALSE);
  assert(registered(CTRL_LOGOFF_EVENT) == FALSE);
  assert(registered(CTRL_SHUTDOWN_EVENT) == FALSE);

  assert(lt__plat_shutdown() == LT_OK);
  assert(remove_calls == 1);
  assert(registered == NULL);

  /* A second init/shutdown cycle registers and removes again. */
  assert(lt__plat_init() == LT_OK);
  assert(add_calls == 2);
  assert(lt__plat_shutdown() == LT_OK);
  assert(remove_calls == 2);
  return 0;
}
