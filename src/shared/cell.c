#include "internal.h"
#include <stdarg.h>
#include <stdio.h>

int lt_set_cell(int x, int y, lt_uchar ch, lt_attr fg, lt_attr bg) {
  if (!lt__g.initialized)
    return LT_ERR_NOT_INIT;

  if (!lt__g.back || x < 0 || y < 0 || x >= lt__g.width || y >= lt__g.height)
    return LT_ERR_OUT_OF_BOUNDS;

  lt__g.dirty_rows[y] = true;

  const size_t idx = (size_t)(y * lt__g.width + x);

  /* Keep head/tail pairs of wide characters consistent (LT__WIDE_TAIL). */
  if (lt__g.back[idx].ch == LT__WIDE_TAIL && x > 0 && ch != LT__WIDE_TAIL) {
    /* Overwriting a tail: the head to the left loses its second column. */
    lt__g.back[idx - 1].ch = (lt_uchar)' ';
    lt__g.back[idx - 1]._reserved = 0;
  } else if (x + 1 < lt__g.width && lt__g.back[idx + 1].ch == LT__WIDE_TAIL &&
             lt__wcwidth(ch) != 2) {
    /* Overwriting a head with a narrow character: drop the stale tail. */
    lt__g.back[idx + 1].ch = (lt_uchar)' ';
    lt__g.back[idx + 1].fg = fg;
    lt__g.back[idx + 1].bg = bg;
    lt__g.back[idx + 1]._reserved = 0;
  }

  lt__g.back[idx].ch = ch;
  lt__g.back[idx].fg = fg;
  lt__g.back[idx].bg = bg;
  lt__g.back[idx]._reserved = 0;

  if (ch != LT__WIDE_TAIL && lt__wcwidth(ch) == 2 && x + 1 < lt__g.width) {
    lt__g.back[idx + 1].ch = LT__WIDE_TAIL;
    lt__g.back[idx + 1].fg = fg;
    lt__g.back[idx + 1].bg = bg;
    lt__g.back[idx + 1]._reserved = 0;
  }

  return LT_OK;
}

int lt_get_cell(int x, int y, struct lt_cell *out) {
  if (!lt__g.initialized)
    return LT_ERR_NOT_INIT;
  if (!out)
    return LT_ERR;

  if (!lt__g.back || x < 0 || y < 0 || x >= lt__g.width || y >= lt__g.height)
    return LT_ERR_OUT_OF_BOUNDS;

  *out = lt__g.back[(size_t)(y * lt__g.width + x)];
  return LT_OK;
}

int lt_set_cell_ex(int x, int y, const lt_uchar *chs, size_t nch, lt_attr fg,
                   lt_attr bg) {
  if (!lt__g.initialized)
    return LT_ERR_NOT_INIT;
  if (!chs || nch == 0)
    return LT_ERR;

  if (!lt__g.back || x < 0 || y < 0 || x >= lt__g.width || y >= lt__g.height)
    return LT_ERR_OUT_OF_BOUNDS;

  /* Intern the trailing codepoints; a single-codepoint cluster gets id 0 and
   * behaves exactly like lt_set_cell. */
  bool ok = true;
  uint32_t id = lt__egc_intern(chs, nch, &ok);
  if (!ok)
    return LT_ERR_MEM;

  lt__g.dirty_rows[y] = true;

  const size_t idx = (size_t)(y * lt__g.width + x);
  lt__g.back[idx].ch = chs[0]; /* base codepoint stays in ch */
  lt__g.back[idx].fg = fg;
  lt__g.back[idx].bg = bg;
  lt__g.back[idx]._reserved = id;

  return LT_OK;
}

int lt_extend_cell(int x, int y, lt_uchar ch) {
  if (!lt__g.initialized)
    return LT_ERR_NOT_INIT;

  if (!lt__g.back || x < 0 || y < 0 || x >= lt__g.width || y >= lt__g.height)
    return LT_ERR_OUT_OF_BOUNDS;

  const size_t idx = (size_t)(y * lt__g.width + x);
  const struct lt_cell *c = &lt__g.back[idx];

  /* Rebuild the cluster = existing (base + any prior cluster codepoints) + ch,
   * then re-intern. Cap the assembled length to a sane bound. */
  lt_uchar buf[16];
  size_t n = 0;

  size_t prev_n = 0;
  const lt_uchar *prev = lt__egc_get(c->_reserved, &prev_n);
  if (prev) {
    for (; n < prev_n && n < sizeof(buf) / sizeof(buf[0]) - 1; n++)
      buf[n] = prev[n];
  } else {
    buf[n++] = c->ch; /* current cell is a plain codepoint; that is the base */
  }

  if (n >= sizeof(buf) / sizeof(buf[0]))
    return LT_ERR; /* cluster too long to extend further */
  buf[n++] = ch;

  return lt_set_cell_ex(x, y, buf, n, c->fg, c->bg);
}

int lt_has_egc(void) { return 1; }

int lt_print_ex(int x, int y, lt_attr fg, lt_attr bg, size_t *out_w,
                const char *str) {
  if (!lt__g.initialized)
    return LT_ERR_NOT_INIT;
  if (!str)
    return LT_ERR;

  /* The starting cell must be in bounds; cells we walk off the edge are clipped
   * (skipped) rather than erroring, matching termbox2's tb_print_ex. */
  if (!lt__g.back || x < 0 || y < 0 || x >= lt__g.width || y >= lt__g.height)
    return LT_ERR_OUT_OF_BOUNDS;

  const int start_x = x;
  size_t line_w = 0; /* columns advanced on the current line */
  size_t max_w = 0;  /* widest line seen */

  while (*str) {
    int need = lt__utf8_char_length(*str);
    lt_uchar cp;

    if (need <= 0 || need > 4) {
      /* Invalid lead byte: emit U+FFFD and resync by one byte. */
      cp = 0xFFFD;
      str += 1;
    } else {
      /* Stop the decode at the NUL terminator if the sequence is truncated, so
       * we never read past the string. */
      size_t avail = 0;
      while (avail < (size_t)need && str[avail])
        avail++;

      int dec = lt__utf8_decode(str, avail, &cp);
      if (dec <= 0) {
        cp = 0xFFFD;
        str += 1; /* resync one byte on a malformed/truncated sequence */
      } else {
        str += dec;
      }
    }

    if (cp == '\n') {
      if (line_w > max_w)
        max_w = line_w;
      line_w = 0;
      x = start_x;
      y += 1;
      continue;
    }

    int w = lt__wcwidth(cp);
    if (w < 0) {
      /* Non-printable (control char etc.): show a replacement glyph, width 1.
       */
      cp = 0xFFFD;
      w = 1;
    }

    if (w == 0) {
      /* Zero-width (combining mark): no cell of its own, no column advance. A
       * full grapheme renderer would merge it into the previous cell; libterm
       * has no extend-cell yet, so it is simply not placed. */
      continue;
    }

    /* Clip off-buffer cells but keep advancing, so a later '\n' still resets to
     * start_x and trailing lines render. A 2-column character occupies the
     * starting cell; the second column is left for the renderer (no cell of its
     * own, matching how termbox2 advances without writing a spacer). */
    (void)lt_set_cell(x, y, cp, fg, bg);
    x += w;
    line_w += (size_t)w;
  }

  if (line_w > max_w)
    max_w = line_w;
  if (out_w)
    *out_w = max_w;

  return LT_OK;
}

int lt_print(int x, int y, lt_attr fg, lt_attr bg, const char *str) {
  return lt_print_ex(x, y, fg, bg, NULL, str);
}

/* Shared printf core: format into a fixed buffer, then print via lt_print_ex
 * (out_w may be NULL). vsnprintf truncates into buf and NUL-terminates, so an
 * over-long result is printed truncated rather than rejected. */
static int lt__printf_into(int x, int y, lt_attr fg, lt_attr bg, size_t *out_w,
                           const char *fmt, va_list vl) {
  char buf[1024];
  int n = vsnprintf(buf, sizeof(buf), fmt, vl);
  if (n < 0)
    return LT_ERR;
  return lt_print_ex(x, y, fg, bg, out_w, buf);
}

int lt_printf(int x, int y, lt_attr fg, lt_attr bg, const char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  int rc = lt__printf_into(x, y, fg, bg, NULL, fmt, vl);
  va_end(vl);
  return rc;
}

int lt_printf_ex(int x, int y, lt_attr fg, lt_attr bg, size_t *out_w,
                 const char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  int rc = lt__printf_into(x, y, fg, bg, out_w, fmt, vl);
  va_end(vl);
  return rc;
}
