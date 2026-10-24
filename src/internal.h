/*
 * libterm - internal shared declarations.
 * Not installed. Do not expose lt__ (double-underscore) symbols publicly.
 */
#ifndef LIBTERM_INTERNAL_H
#define LIBTERM_INTERNAL_H

#include "lib/arena.h"
#include "libterm/libterm.h"
#include <stdbool.h>

#if defined(LIBTERM_ENABLE_RENDER_STATS)
#include <stdint.h>
#define LT_RENDER_STATS 1
#else
#define LT_RENDER_STATS 0
#endif

#if LT_RENDER_STATS
struct lt__render_stats {
  uint64_t present_calls;
  uint64_t dirty_rows;
  uint64_t diff_runs;
  uint64_t cursor_moves;
  uint64_t cells_rendered;
  uint64_t bytes_buffered;
  uint64_t flushes;
};
#endif

/* ---- fg/bg bit layout (see LT_* macros in libterm.h) ---- */
/* Wide characters: the second cell of a 2-column character carries this
 * sentinel in `ch`. lt_set_cell() writes it when a wide code point is placed
 * (and clears a stale head/tail when either half is overwritten); the
 * renderer emits NOTHING for it and never starts or ends a diff run between
 * a head and its tail, so the terminal cursor and the cached cursor (cur_x)
 * stay in step. Before this, the tail cell was left untouched and emitted as
 * its own character, which pushed the rest of the row one column to the
 * right whenever the tail cell differed from the previous frame. */
#define LT__WIDE_TAIL 0xFFFFFFFFu

#define LT__COLOR_MASK 0x00FFFFFFu /* bits 0-23: color value */
#define LT__ATTR_MASK 0x7F000000u  /* bits 24-30: the 7 attribute bits */

/* ---- global state (single terminal instance) ---- */

struct lt__state {
  bool initialized;
  bool *dirty_rows;
  bool force_repaint; /* lt_invalidate: next present repaints every cell */
  bool kitty_active; /* kitty keyboard protocol was negotiated (push emitted) */
  int last_errno; /* errno captured at the last failing syscall (lt_last_errno)
                   */
  int width;
  int height;
  int input_mode;
  int output_mode;
  int cur_x;
  int cur_y;
  /* User cursor (lt_set_cursor / lt_hide_cursor): -1/-1 = hidden — the init
   * default, termbox2 parity. Distinct from cur_x/cur_y above, which cache
   * where the terminal's physical cursor sits after the last emission (the
   * paint cache). When visible, lt_present reparks the physical cursor here
   * after painting. */
  int cursor_x;
  int cursor_y;
  lt_attr cur_fg; /* SGR cache: last-emitted fg (color bits 0-23 + HI_BLACK) */
  lt_attr cur_bg; /* SGR cache: last-emitted bg (color bits 0-23 + HI_BLACK) */
  lt_attr cur_attrs; /* SGR cache: last-emitted attr bits (LT__ATTR_MASK) */
  lt_attr clear_fg;
  lt_attr clear_bg;
  struct lt_cell *back;  /* back buffer  (w*h cells) */
  struct lt_cell *front; /* front buffer (w*h cells) */
#if LT_RENDER_STATS
  struct lt__render_stats stats;
#endif
  struct lt_arena *arena; /* arena for back/front buffers */
};

extern struct lt__state lt__g;

/* ---- buffer ops (shared/buffer.c) ---- */
/* (Re)allocate the back/front buffers to w*h cells (one arena allocation holds
 * both, reset on each call), clear them to the current clear attrs, mark every
 * row dirty, and update lt__g.width/height. No-op returning LT_OK if the size
 * is unchanged. Returns LT_ERR for a non-positive dimension, LT_ERR_MEM on
 * allocation failure (or a w*h that would overflow the size computation). */
int lt__buffer_resize(int w, int h);
/* Release the cell buffers and their arena and zero the size; safe to call when
 * nothing is allocated. */
void lt__buffer_free(void);
/* Fill `count` cells of `buf` with a blank glyph in fg/bg (dispatches to the
 * SIMD fill backend). */
void lt__buffer_clear(struct lt_cell *buf, int count, lt_attr fg, lt_attr bg);
/* Apply a size change: resize buffers, reset the cursor cache, force a full
 * repaint (resize ghosts otherwise — see buffer.c), and fill *ev as
 * LT_EVENT_RESIZE. LT_ERR_NO_EVENT for non-positive/unchanged sizes;
 * LT_ERR_MEM etc. from the buffer resize; LT_OK with *ev filled otherwise. */
int lt__handle_resize(int new_w, int new_h, struct lt_event *ev);

/* ---- utf8 (shared/utf8.c) ---- */
/* Bytes in the UTF-8 sequence lead byte `c` begins: 1-4, or 0 for an invalid
 * lead byte. Inspects only `c`. */
int lt__utf8_char_length(char c);
/* Decode up to `len` bytes of `s` into *out, rejecting overlong encodings,
 * surrogates, and codepoints above U+10FFFF. Returns the number of bytes
 * consumed (1-4), or 0 on a NULL argument, len 0, or a malformed/incomplete
 * sequence. */
int lt__utf8_decode(const char *s, size_t len, lt_uchar *out);
/* Encode codepoint `ch` into `out` (up to 4 bytes, not NUL-terminated). Returns
 * the byte count (1-4), or 0 for a surrogate or out-of-range codepoint. */
int lt__utf8_encode(lt_uchar ch, char out[4]);

/* ---- character width (shared/wcwidth.c) ----
 * Columns a codepoint occupies: 0 (combining/zero-width), 2 (wide CJK/emoji),
 * 1 (normal), or -1 (non-printable). Markus Kuhn reference ranges. */
int lt__wcwidth(lt_uchar ch);

/* ---- grapheme-cluster table (shared/egc.c) ----
 * Interns a cluster (base + trailing codepoints) and returns a nonzero id to
 * store in lt_cell._reserved; identical clusters share one id (content dedup),
 * which keeps the SIMD byte-equality diff invariant valid. id 0 = no cluster.
 * lt__egc_intern returns 0 on a single-codepoint cluster (caller stores the
 * base in ch directly) or on allocation failure (*ok reports which). */
uint32_t lt__egc_intern(const lt_uchar *chs, size_t nch, bool *ok);
/* Look up an interned cluster by id: returns its codepoint array and sets
 * *nch. Returns NULL for id 0 or an unknown id. */
const lt_uchar *lt__egc_get(uint32_t id, size_t *nch);
/* Drop all interned clusters (called from lt_shutdown so a fresh session
 * starts with an empty table). */
void lt__egc_reset(void);

/* ---- SGR / run emission (shared/sgr.c) ----
 * Platform-independent VT byte construction; calls lt__plat_reserve/commit
 * (platform.h) to reach the per-platform output buffer. */
int lt__write_uint(char *buf, int v); /* int -> decimal ASCII; returns digits */
/* Emit a CSI ... m sequence to move the terminal's SGR state to fg/bg/attrs,
 * diffing against the cached cur_fg/cur_bg/cur_attrs so an unchanged style
 * emits nothing. Negative attr deltas force a reset-then-reapply. Returns
 * LT_OK, or LT_ERR if the output buffer can't be reserved. */
int lt__emit_sgr(lt_attr fg, lt_attr bg, lt_attr attrs);
/* Emit `count` consecutive cells: split into same-style spans (each prefixed by
 * lt__emit_sgr) and write their glyph bytes, falling back to per-cell emission
 * for spans containing a grapheme cluster. Returns LT_OK or a write error. */
int lt__render_run(const struct lt_cell *cells, int count);

/* ---- key sequence decoding (shared/keymap.c) ----
 * Pure: no globals, no I/O, no syscalls. Interprets a byte sequence already
 * read from the terminal into an lt_event. `input_mode` is the LT_INPUT_*
 * bitmask (the COMPAT bit changes control-byte and modifier semantics). */
enum lt__key_match {
  LT__KEY_NOMATCH = 0, /* not a recognized sequence */
  LT__KEY_PARTIAL = 1, /* valid prefix of a known sequence; read more */
  LT__KEY_MATCH = 2,   /* out filled; *consumed = bytes used */
  LT__KEY_DISCARD = 3  /* *consumed bytes recognized but yield no event
                          (e.g. a stray OSC color-query reply) */
};

enum lt__key_match lt__key_decode(const unsigned char *seq, size_t len,
                                  int input_mode, struct lt_event *out,
                                  size_t *consumed);

/* ---- color-query reply parsing (shared/colorq.c) ----
 * Parse an OSC color-reply payload — the bytes between "ESC ]" and the BEL/ST
 * terminator: "10;rgb:...", "11;rgb:...", or "4;<idx>;rgb:...". The URxvt
 * "rgba:" form (R/G/B/A, alpha last) is also accepted, with alpha ignored.
 * Channels are 1-4 hex digits per the X11 XParseColor forms, scaled to 8 bits
 * with rounding. Returns LT_OK and fills *rgb (0x00RRGGBB) only if the reply
 * matches `what` (LT_COLOR_DEFAULT_FG / LT_COLOR_DEFAULT_BG / palette index
 * 0..255) and parses cleanly; LT_ERR otherwise. */
int lt__color_parse_osc_reply(const char *payload, size_t len, int what,
                              uint32_t *rgb);

#endif /* LIBTERM_INTERNAL_H */
