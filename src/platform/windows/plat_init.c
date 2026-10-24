#include "internal.h"
#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef LIBTERM_BENCH_WIDTH
#define LIBTERM_BENCH_WIDTH 40
#endif

#ifndef LIBTERM_BENCH_HEIGHT
#define LIBTERM_BENCH_HEIGHT 12
#endif

static HANDLE lt__win_in = INVALID_HANDLE_VALUE;
static HANDLE lt__win_out = INVALID_HANDLE_VALUE;
static DWORD lt__win_in_mode_orig = 0;
static DWORD lt__win_out_mode_orig = 0;
static int lt__win_modes_saved = 0;

static int lt__win_is_bad_handle(HANDLE h) {
  return h == NULL || h == INVALID_HANDLE_VALUE;
}

/* Ctrl+C / Ctrl+Break must reach the application as keys, not terminate it.
 * Clearing ENABLE_PROCESSED_INPUT alone is not enough: child processes that
 * share the console (a shell, a filter, a spawned tool) frequently switch
 * processed input back on and leave it on when they exit, after which the
 * next Ctrl+C raises CTRL_C_EVENT and kills the process (0xC000013A). A
 * registered handler that swallows both events keeps POSIX parity (raw mode
 * clears ISIG there). Unlike SetConsoleCtrlHandler(NULL, TRUE), a handler
 * function is not inherited by child processes, so a command started from
 * the application can still be interrupted with Ctrl+C. */
static BOOL WINAPI lt__win_ctrl_handler(DWORD type) {
  return (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) ? TRUE : FALSE;
}

int lt__plat_init(void) {
#if defined(LIBTERM_BENCH_HEADLESS_OUTPUT)
  return LT_OK;
#endif

  lt__win_in = GetStdHandle(STD_INPUT_HANDLE);
  lt__win_out = GetStdHandle(STD_OUTPUT_HANDLE);

  if (lt__win_is_bad_handle(lt__win_in) || lt__win_is_bad_handle(lt__win_out))
    return LT_ERR_INIT_OPEN;

  if (!GetConsoleMode(lt__win_in, &lt__win_in_mode_orig))
    return LT_ERR_INIT_OPEN;

  if (!GetConsoleMode(lt__win_out, &lt__win_out_mode_orig))
    return LT_ERR_INIT_OPEN;

  DWORD in_mode = lt__win_in_mode_orig;
  in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
  in_mode |= ENABLE_WINDOW_INPUT;
  in_mode |= ENABLE_EXTENDED_FLAGS;
  in_mode &= ~ENABLE_QUICK_EDIT_MODE;

  if (!SetConsoleMode(lt__win_in, in_mode))
    return LT_ERR_INIT_OPEN;

  DWORD out_mode = lt__win_out_mode_orig;
  out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
  if (!SetConsoleMode(lt__win_out, out_mode)) {
    (void)SetConsoleMode(lt__win_in, lt__win_in_mode_orig);
    return LT_ERR_INIT_OPEN;
  }

  (void)SetConsoleCP(CP_UTF8);
  (void)SetConsoleOutputCP(CP_UTF8);

  static const char enter_alt[] = "\x1b[?1049h";
  int rc = lt__plat_write(enter_alt, sizeof(enter_alt) - 1);
  if (rc != LT_OK) {
    (void)SetConsoleMode(lt__win_in, lt__win_in_mode_orig);
    (void)SetConsoleMode(lt__win_out, lt__win_out_mode_orig);
    return LT_ERR_INIT_OPEN;
  }

  /* Cursor hidden by default (termbox2 parity); lt_set_cursor shows it. */
  rc = lt__plat_hide_cursor();
  if (rc != LT_OK) {
    (void)SetConsoleMode(lt__win_in, lt__win_in_mode_orig);
    (void)SetConsoleMode(lt__win_out, lt__win_out_mode_orig);
    return LT_ERR_INIT_OPEN;
  }

  rc = lt__plat_flush();
  if (rc != LT_OK) {
    (void)SetConsoleMode(lt__win_in, lt__win_in_mode_orig);
    (void)SetConsoleMode(lt__win_out, lt__win_out_mode_orig);
    return LT_ERR_INIT_OPEN;
  }

  (void)SetConsoleCtrlHandler(lt__win_ctrl_handler, TRUE);

  lt__win_modes_saved = 1;
  return LT_OK;
}

int lt__plat_init_fd(int ttyfd, int owned) {
  (void)ttyfd;
  (void)owned;
  return LT_ERR_INIT_OPEN; /* fd-based init is POSIX-only */
}

int lt__plat_init_file(const char *path) {
  (void)path;
  return LT_ERR_INIT_OPEN; /* path/fd-based init is POSIX-only */
}

int lt__plat_shutdown(void) {
#if defined(LIBTERM_BENCH_HEADLESS_OUTPUT)
  return LT_OK;
#endif

  if (lt__win_modes_saved) {
    (void)lt__plat_show_cursor(); /* restore cursor visibility (in alt) */
    static const char leave_alt[] = "\x1b[?1049l";
    (void)lt__plat_write(leave_alt, sizeof(leave_alt) - 1);
    (void)lt__plat_flush();
    (void)SetConsoleMode(lt__win_in, lt__win_in_mode_orig);
    (void)SetConsoleMode(lt__win_out, lt__win_out_mode_orig);
    (void)SetConsoleCtrlHandler(lt__win_ctrl_handler, FALSE);
  }

  lt__win_in = INVALID_HANDLE_VALUE;
  lt__win_out = INVALID_HANDLE_VALUE;
  lt__win_in_mode_orig = 0;
  lt__win_out_mode_orig = 0;
  lt__win_modes_saved = 0;
  return LT_OK;
}

int lt__plat_get_size(int *w, int *h) {
  if (!w || !h)
    return LT_ERR;

#if defined(LIBTERM_BENCH_HEADLESS_OUTPUT)
  *w = LIBTERM_BENCH_WIDTH;
  *h = LIBTERM_BENCH_HEIGHT;
  return LT_OK;
#endif

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(lt__win_out, &csbi))
    return LT_ERR;

  *w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  *h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  return LT_OK;
}

/* Windows console input is not escape-sequence-negotiated; modern key
 * semantics on Windows are out of scope. Hooks exist so shared code links. */
int lt__plat_kitty_enable(void) { return LT_OK; }
int lt__plat_kitty_disable(void) { return LT_OK; }
