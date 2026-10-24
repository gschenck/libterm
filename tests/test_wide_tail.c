/* Wide characters (CJK, emoji) occupy TWO terminal columns but ONE cell in
 * the back buffer's sense of "advance by one per emitted cell". If the
 * renderer emits the cell behind a wide glyph as a separate character, the
 * terminal cursor ends up one column ahead of the cached cursor and the rest
 * of the row shifts right -- visible as a space pushed in behind every CJK
 * character whenever the tail cell differs from the previous frame.
 *
 * This test replays the emitted bytes through a minimal VT column model
 * (CUP + printable characters advanced by lt_wcwidth) and asserts the row the
 * terminal would show. POSIX-only; returns 77 (CTest "skip") without a pty. */
#define _DEFAULT_SOURCE
#include "libterm/libterm.h"

#include <assert.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_master = -1;
static unsigned char cap[65536];
static size_t caplen;

static void drain(void) {
  caplen = 0;
  for (;;) {
    unsigned char tmp[4096];
    ssize_t n = read(g_master, tmp, sizeof tmp);
    if (n <= 0)
      break;
    if (caplen + (size_t)n > sizeof cap)
      n = (ssize_t)(sizeof cap - caplen);
    if (n <= 0)
      break;
    memcpy(cap + caplen, tmp, (size_t)n);
    caplen += (size_t)n;
  }
}

/* Minimal terminal model: one row of code points (0 = never written).
 * Understands CSI ... H (cursor position, 1-based), ignores every other CSI
 * and the DEC private sequences, places printable code points at the cursor
 * and advances by their column width. Wide glyphs clear the cell they cover,
 * like a real terminal does. */
#define TERM_W 80
static uint32_t term_row0[TERM_W];
static int term_x, term_y;

static void term_reset(void) {
  memset(term_row0, 0, sizeof term_row0);
  term_x = term_y = 0;
}

static size_t utf8_decode(const unsigned char *p, size_t n, uint32_t *cp) {
  if (p[0] < 0x80) {
    *cp = p[0];
    return 1;
  }
  if ((p[0] & 0xE0) == 0xC0 && n >= 2) {
    *cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    return 2;
  }
  if ((p[0] & 0xF0) == 0xE0 && n >= 3) {
    *cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
          (p[2] & 0x3F);
    return 3;
  }
  if ((p[0] & 0xF8) == 0xF0 && n >= 4) {
    *cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
          ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 4;
  }
  *cp = 0xFFFD;
  return 1;
}

static void term_feed(const unsigned char *b, size_t n) {
  size_t i = 0;
  while (i < n) {
    if (b[i] == 0x1b && i + 1 < n && b[i + 1] == '[') {
      size_t j = i + 2;
      int p1 = 0, p2 = 0, np = 0, cur = 0, have = 0;
      while (j < n && !(b[j] >= 0x40 && b[j] <= 0x7e)) {
        if (b[j] >= '0' && b[j] <= '9') {
          cur = cur * 10 + (b[j] - '0');
          have = 1;
        } else if (b[j] == ';') {
          if (np == 0)
            p1 = have ? cur : 1;
          else if (np == 1)
            p2 = have ? cur : 1;
          np++;
          cur = 0;
          have = 0;
        }
        j++;
      }
      if (np == 0)
        p1 = have ? cur : 1;
      else if (np == 1)
        p2 = have ? cur : 1;
      if (j < n && b[j] == 'H') {
        term_y = (p1 > 0 ? p1 : 1) - 1;
        term_x = (p2 > 0 ? p2 : 1) - 1;
      }
      i = j + 1;
      continue;
    }
    if (b[i] == 0x1b) { /* other escapes: skip ESC + one byte */
      i += 2;
      continue;
    }
    if (b[i] == '\r') {
      term_x = 0;
      i++;
      continue;
    }
    if (b[i] == '\n') {
      term_y++;
      i++;
      continue;
    }
    if (b[i] < 0x20) {
      i++;
      continue;
    }
    uint32_t cp;
    i += utf8_decode(b + i, n - i, &cp);
    int w = lt_wcwidth(cp);
    if (w <= 0)
      continue;
    if (term_y == 0 && term_x >= 0 && term_x < TERM_W) {
      term_row0[term_x] = cp;
      if (w == 2 && term_x + 1 < TERM_W)
        term_row0[term_x + 1] = 0xFFFFFFFFu; /* covered by the wide glyph */
    }
    term_x += w;
  }
}

static void expect_row(const uint32_t *want, int n) {
  for (int k = 0; k < n; k++) {
    if (term_row0[k] != want[k]) {
      fprintf(stderr, "col %d: got U+%04X want U+%04X\n", k, term_row0[k],
              want[k]);
      assert(!"row 0 differs from the expected terminal content");
    }
  }
}

int main(void) {
  int slave = -1;
  struct winsize ws;
  memset(&ws, 0, sizeof ws);
  ws.ws_row = 24;
  ws.ws_col = TERM_W;
  if (openpty(&g_master, &slave, NULL, NULL, &ws) != 0)
    return 77; /* CTest skip */
  int fl = fcntl(g_master, F_GETFL, 0);
  fcntl(g_master, F_SETFL, fl | O_NONBLOCK);

  assert(lt_init_fd(slave) == LT_OK);
  assert(lt_clear() == LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_reset();

  /* Frame 1: "abcd". */
  assert(lt_print(0, 0, LT_DEFAULT, LT_DEFAULT, "abcd") == LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_feed(cap, caplen);
  {
    const uint32_t want[] = {'a', 'b', 'c', 'd'};
    expect_row(want, 4);
  }

  /* Frame 2: clear, then a wide character at column 0 and "cd" behind it.
   * Back buffer: [日][tail][c][d]; front: [a][b][c][d]. The diff run must
   * cover head+tail as ONE glyph: if the tail is emitted as a space, the
   * terminal shows 日 followed by a space over 'c' and the cached cursor is
   * one column behind the real one. */
  assert(lt_clear() == LT_OK);
  assert(lt_print(0, 0, LT_DEFAULT, LT_DEFAULT, "\xe6\x97\xa5") == LT_OK);
  assert(lt_print(2, 0, LT_DEFAULT, LT_DEFAULT, "cd") == LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_feed(cap, caplen);
  {
    const uint32_t want[] = {0x65E5, 0xFFFFFFFFu, 'c', 'd'};
    expect_row(want, 4);
  }

  /* Frame 3: overwrite the tail column with a narrow character. The head
   * must lose its second column (becomes a space) instead of a stale wide
   * glyph bleeding over the new character. */
  assert(lt_print(1, 0, LT_DEFAULT, LT_DEFAULT, "X") == LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_feed(cap, caplen);
  {
    const uint32_t want[] = {' ', 'X', 'c', 'd'};
    expect_row(want, 4);
  }

  /* Frame 4: two wide characters in a row followed by text, after a change
   * in the middle -- the run end must not fall between head and tail. */
  assert(lt_clear() == LT_OK);
  assert(lt_print(0, 0, LT_DEFAULT, LT_DEFAULT, "\xe6\x97\xa5\xe6\x9c\xacZ") ==
         LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_feed(cap, caplen);
  assert(lt_print(2, 0, LT_DEFAULT, LT_DEFAULT, "\xe8\xaa\x9e") == LT_OK);
  assert(lt_present() == LT_OK);
  drain();
  term_feed(cap, caplen);
  {
    const uint32_t want[] = {0x65E5, 0xFFFFFFFFu, 0x8A9E, 0xFFFFFFFFu, 'Z'};
    expect_row(want, 5);
  }

  assert(lt_shutdown() == LT_OK);
  close(slave);
  close(g_master);
  return 0;
}
