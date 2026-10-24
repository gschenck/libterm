/*
 * libterm - shared SGR / run emission.
 *
 * The bytes written here (CSI ... m colour/attribute sequences and the
 * glyph payload of a run) are interpreted by the terminal, not the OS, so
 * this logic is identical on every platform. It depends only on the shared
 * global SGR cache (lt__g.cur_fg/cur_bg/cur_attrs) and on the per-platform
 * output buffer reached through lt__plat_reserve / lt__plat_commit. The
 * actual I/O (how bytes reach the tty/console) stays in src/platform/.
 */
#include "internal.h"
#include "platform.h"

int lt__write_uint(char *buf, int v) {
  if (v < 0)
    v = 0;

  if (v == 0) {
    buf[0] = '0';
    return 1;
  }

  char tmp[10];
  int n = 0;
  while (v > 0) {
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  }

  for (int i = 0; i < n; i++)
    buf[i] = tmp[n - i - 1];

  return n;
}

static void lt__emit_sep(char *p, size_t *pos, bool *wrote_any) {
  if (*wrote_any)
    p[(*pos)++] = ';';
  *wrote_any = true;
}

/* Resolve the final 0-255 SGR palette index for an indexed output mode,
 * matching termbox2 (termbox2.h:3810-3826): OUTPUT_216 maps 1..216 into the
 * 6x6x6 color cube (16..231); OUTPUT_GRAYSCALE maps 1..24 into the gray ramp
 * (232..255); OUTPUT_256 (and anything else) uses the raw low-byte index.
 * Out-of-range values clamp to the top of the range before the offset. The
 * caller handles the default/HI_BLACK cases, so this only runs for real colors.
 */
static int lt__palette_index(int mode, lt_attr v) {
  int c = (int)(v & 0xFF);
  if (mode == LT_OUTPUT_216) {
    if (c > 216)
      c = 216;
    return c + 0x0F;
  }
  if (mode == LT_OUTPUT_GRAYSCALE) {
    if (c > 24)
      c = 24;
    return c + 0xE7;
  }
  return c;
}

static void lt__emit_sgr_256(char *p, size_t *pos, bool *wrote_any, int is_fg,
                             int idx) {
  lt__emit_sep(p, pos, wrote_any);
  *pos += (size_t)lt__write_uint(p + *pos, is_fg ? 38 : 48);
  p[(*pos)++] = ';';
  p[(*pos)++] = '5';
  p[(*pos)++] = ';';
  *pos += (size_t)lt__write_uint(p + *pos, idx);
}

static void lt__emit_sgr_truecolor(char *p, size_t *pos, bool *wrote_any,
                                   int is_fg, lt_attr v) {
  int r = (int)((v >> 16) & 0xFF);
  int g = (int)((v >> 8) & 0xFF);
  int b = (int)(v & 0xFF);

  lt__emit_sep(p, pos, wrote_any);
  *pos += (size_t)lt__write_uint(p + *pos, is_fg ? 38 : 48);
  p[(*pos)++] = ';';
  p[(*pos)++] = '2';
  p[(*pos)++] = ';';
  *pos += (size_t)lt__write_uint(p + *pos, r);
  p[(*pos)++] = ';';
  *pos += (size_t)lt__write_uint(p + *pos, g);
  p[(*pos)++] = ';';
  *pos += (size_t)lt__write_uint(p + *pos, b);
}

int lt__emit_sgr(lt_attr fg, lt_attr bg, lt_attr attrs) {
  if (fg == lt__g.cur_fg && bg == lt__g.cur_bg && attrs == lt__g.cur_attrs)
    return LT_OK;

  char *p = lt__plat_reserve(64);
  if (!p)
    return LT_ERR;

  int mode = lt__g.output_mode;
  if (mode == LT_OUTPUT_CURRENT)
    mode = LT_OUTPUT_NORMAL;

  /* Depends only on mode, so compute once (after the no-change early return).
   * The per-fg/bg color masks stay inside their branches — those depend on the
   * fg/bg values and are wasted work on a span that re-emits only one of them.
   */
  bool indexed = (mode == LT_OUTPUT_256 || mode == LT_OUTPUT_216 ||
                  mode == LT_OUTPUT_GRAYSCALE);

  size_t pos = 0;
  p[pos++] = '\x1b';
  p[pos++] = '[';

  bool need_reset = false;

  /* If any attr bit was present and now removed, delta codes are messy;
   * reset then re-apply target style for correctness. */
  if ((lt__g.cur_attrs & ~attrs) != 0)
    need_reset = true;

  bool wrote_any = false;
  if (need_reset) {
    p[pos++] = '0';
    wrote_any = true;
  }

  /* fg */
  if (need_reset || fg != lt__g.cur_fg) {
    lt_attr fg_color = fg & LT__COLOR_MASK;
    /* A 0 color with no HI_BLACK sentinel means "terminal default". */
    bool fg_default = (fg_color == 0) && !(fg & LT_HI_BLACK);
    if (fg_default) {
      lt__emit_sep(p, &pos, &wrote_any);
      p[pos++] = '3';
      p[pos++] = '9';
    } else if (indexed) {
      lt__emit_sgr_256(p, &pos, &wrote_any, 1,
                       lt__palette_index(mode, fg_color));
    } else if (mode == LT_OUTPUT_TRUECOLOR) {
      lt__emit_sgr_truecolor(p, &pos, &wrote_any, 1, fg_color);
    } else {
      lt__emit_sep(p, &pos, &wrote_any);
      if (fg_color >= LT_BLACK && fg_color <= LT_WHITE) {
        p[pos++] = '3';
        p[pos++] = (char)('0' + (fg_color - LT_BLACK));
      } else {
        p[pos++] = '3';
        p[pos++] = '9';
      }
    }
  }

  /* bg */
  if (need_reset || bg != lt__g.cur_bg) {
    lt_attr bg_color = bg & LT__COLOR_MASK;
    bool bg_default = (bg_color == 0) && !(bg & LT_HI_BLACK);
    if (bg_default) {
      lt__emit_sep(p, &pos, &wrote_any);
      p[pos++] = '4';
      p[pos++] = '9';
    } else if (indexed) {
      lt__emit_sgr_256(p, &pos, &wrote_any, 0,
                       lt__palette_index(mode, bg_color));
    } else if (mode == LT_OUTPUT_TRUECOLOR) {
      lt__emit_sgr_truecolor(p, &pos, &wrote_any, 0, bg_color);
    } else {
      lt__emit_sep(p, &pos, &wrote_any);
      if (bg_color >= LT_BLACK && bg_color <= LT_WHITE) {
        p[pos++] = '4';
        p[pos++] = (char)('0' + (bg_color - LT_BLACK));
      } else {
        p[pos++] = '4';
        p[pos++] = '9';
      }
    }
  }

  /* attrs: after a reset we re-apply every target attr; otherwise only the
   * bits newly turned on. Both reduce to attrs_to_emit, so spans with no
   * attribute work (the common case) skip the table walk entirely. */
  static const struct {
    lt_attr bit;
    char code;
  } attr_map[] = {
      {LT_BOLD, '1'},  {LT_DIM, '2'},     {LT_ITALIC, '3'}, {LT_UNDERLINE, '4'},
      {LT_BLINK, '5'}, {LT_REVERSE, '7'}, {LT_STRIKE, '9'},
  };

  lt_attr attrs_to_emit = need_reset ? attrs : (attrs & ~lt__g.cur_attrs);
  if (attrs_to_emit) {
    for (size_t i = 0; i < sizeof(attr_map) / sizeof(attr_map[0]); i++) {
      if (!(attrs_to_emit & attr_map[i].bit))
        continue;
      if (wrote_any)
        p[pos++] = ';';
      p[pos++] = attr_map[i].code;
      wrote_any = true;
    }
  }

  /* Safety: if somehow nothing got written, avoid emitting malformed ESC[ m. */
  if (!wrote_any)
    return LT_OK;

  p[pos++] = 'm';
  lt__plat_commit(pos);
  return LT_OK;
}

/* Emit one cell's glyph bytes: the base codepoint, then any trailing cluster
 * codepoints (combining marks / ZWJ joiners) the terminal composes onto it.
 * Used only for spans that contain a cluster; the cluster-free fast path in
 * lt__render_run stays byte-for-byte as it was. Reserves its own buffer per
 * cell since a cluster's byte length is unbounded by the 4-per-cell estimate.
 */
static int lt__render_cluster_cell(const struct lt_cell *c) {
  size_t ncl = 0;
  const lt_uchar *cl = lt__egc_get(c->_reserved, &ncl);

  /* base + up to ncl continuation codepoints, 4 UTF-8 bytes each. */
  size_t need = (size_t)4 * (1 + (cl ? ncl : 0));
  char *p = lt__plat_reserve(need);
  if (!p)
    return LT_ERR;

  size_t pos = 0;
  char tmp[4];

  lt_uchar base = c->ch ? c->ch : (lt_uchar)' ';
  int ub = lt__utf8_encode(base, tmp);
  if (ub <= 0) {
    p[pos++] = ' ';
  } else {
    for (int t = 0; t < ub; t++)
      p[pos++] = tmp[t];
  }

  /* The interned cluster includes the base at index 0 (it is also in c->ch,
   * already emitted above), so the continuation codepoints start at index 1. */
  for (size_t m = 1; cl && m < ncl; m++) {
    ub = lt__utf8_encode(cl[m], tmp);
    if (ub <= 0)
      continue; /* skip an unencodable continuation rather than emit junk */
    for (int t = 0; t < ub; t++)
      p[pos++] = tmp[t];
  }

  lt__plat_commit(pos);
  return LT_OK;
}

int lt__render_run(const struct lt_cell *cells, int count) {
  if (count <= 0)
    return LT_OK;

  lt_attr span_fg = 0;
  lt_attr span_bg = 0;
  lt_attr span_attrs = 0;

  int sgr_rc = 0, span_len = 0, rc = 0, ub = 0;
  size_t max = 0, pos = 0;
  char *p = NULL;

  lt_uchar ch = (lt_uchar)' ';

  int i = 0, j = 0;
  while (i < count) {
    span_fg = cells[i].fg & (LT__COLOR_MASK | LT_HI_BLACK);
    span_bg = cells[i].bg & (LT__COLOR_MASK | LT_HI_BLACK);
    span_attrs = cells[i].fg & LT__ATTR_MASK;

    if (span_fg != lt__g.cur_fg || span_bg != lt__g.cur_bg ||
        span_attrs != lt__g.cur_attrs) {
      sgr_rc = lt__emit_sgr(span_fg, span_bg, span_attrs);
      if (sgr_rc != LT_OK)
        return sgr_rc;

      lt__g.cur_fg = span_fg;
      lt__g.cur_bg = span_bg;
      lt__g.cur_attrs = span_attrs;
    }

    j = i + 1;
    while (j < count) {
      if ((cells[j].fg & (LT__COLOR_MASK | LT_HI_BLACK)) != span_fg ||
          (cells[j].bg & (LT__COLOR_MASK | LT_HI_BLACK)) != span_bg ||
          (cells[j].fg & LT__ATTR_MASK) != span_attrs)
        break;
      j++;
    }

    span_len = j - i;

    /* Does this span contain any grapheme cluster (_reserved != 0)? Clusters
     * are rare, so the common answer is "no" and we take the original
     * single-reserve fast path below. A span with a cluster falls back to
     * per-cell emission, since a cluster's byte length exceeds the 4-per-cell
     * reserve estimate. */
    bool has_cluster = false;
    for (int k = i; k < j; k++)
      if (cells[k]._reserved != 0) {
        has_cluster = true;
        break;
      }

    if (has_cluster) {
      for (int k = i; k < j; k++) {
        if (cells[k].ch == LT__WIDE_TAIL)
          continue; /* wide tail: nothing to emit */
        rc = lt__render_cluster_cell(&cells[k]);
        if (rc != LT_OK)
          return rc;
      }
      i = j;
      continue;
    }

    max = (size_t)span_len * 4;
    p = lt__plat_reserve(max);
    if (!p) {
      for (int k = i; k < j; k++) {
        rc = lt__plat_render_cell(0, 0, &cells[k]);
        if (rc != LT_OK)
          return rc;
      }
      i = j;
      continue;
    }

    pos = 0;
    for (int k = i; k < j; k++) {
      if (cells[k].ch == LT__WIDE_TAIL)
        continue; /* wide tail: the head already advanced two columns */
      ch = cells[k].ch ? cells[k].ch : (lt_uchar)' ';

      if (ch < 0x80) {
        p[pos++] = (char)ch;
        continue;
      }

      char tmp[4];
      ub = lt__utf8_encode(ch, tmp);
      if (ub <= 0) {
        p[pos++] = ' ';
        continue;
      }

      for (int t = 0; t < ub; t++)
        p[pos++] = tmp[t];
    }

    lt__plat_commit(pos);
    i = j;
  }

  return LT_OK;
}
