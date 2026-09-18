/****************************************************************************
 * app/velaguard/vg_ai_contract.c
 *
 * Board-side parsing, building and validation of text produced by the
 * on-board AI agent.  Plain C, standard headers only, so the bounds below
 * can be exercised by app/velaguard/host_tests.
 ****************************************************************************/

#include "vg_ai_contract.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/****************************************************************************
 * Character helpers
 ****************************************************************************/

int vg_ai_id_valid(const char *id)
{
  size_t n;
  size_t i;

  if (id == NULL)
    {
      return 0;
    }

  n = strlen(id);
  if (n < 1 || n > (VG_AI_ID_MAX - 1))
    {
      return 0;
    }

  for (i = 0; i < n; i++)
    {
      unsigned char c = (unsigned char)id[i];

      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_'))
        {
          return 0;
        }
    }

  return 1;
}

/* Length of the UTF-8 sequence introduced by lead byte c, or 0 when c is
 * not a legal lead byte.  Continuation and range checks happen in
 * utf8_step(), which also rejects overlong forms, surrogates and >U+10FFFF. */

static size_t utf8_lead_len(unsigned char c)
{
  if (c < 0x80)
    {
      return 1;
    }

  if (c >= 0xc2 && c <= 0xdf)
    {
      return 2;
    }

  if (c >= 0xe0 && c <= 0xef)
    {
      return 3;
    }

  if (c >= 0xf0 && c <= 0xf4)
    {
      return 4;
    }

  return 0;
}

/* Validate one sequence starting at s with at least len bytes left.
 * Returns the sequence length, or 0 when it is malformed. */

static size_t utf8_step(const char *s, size_t len)
{
  unsigned char c0 = (unsigned char)s[0];
  size_t need = utf8_lead_len(c0);
  unsigned char c1;
  size_t i;

  if (need == 0 || need > len)
    {
      return 0;
    }

  for (i = 1; i < need; i++)
    {
      if (((unsigned char)s[i] & 0xc0) != 0x80)
        {
          return 0;
        }
    }

  if (need == 1)
    {
      return 1;
    }

  c1 = (unsigned char)s[1];

  /* Reject overlong encodings, UTF-16 surrogates and code points above
   * U+10FFFF: a decoder would render those as garbage or not at all. */

  if (need == 2)
    {
      return 2;   /* 0xc2..0xdf guarantees >= U+0080 */
    }

  if (need == 3)
    {
      if (c0 == 0xe0 && c1 < 0xa0)
        {
          return 0;
        }

      if (c0 == 0xed && c1 > 0x9f)
        {
          return 0;   /* U+D800..U+DFFF */
        }

      return 3;
    }

  if (c0 == 0xf0 && c1 < 0x90)
    {
      return 0;
    }

  if (c0 == 0xf4 && c1 > 0x8f)
    {
      return 0;   /* above U+10FFFF */
    }

  return 4;
}

int vg_ai_utf8_valid(const char *buf, size_t len, char allow)
{
  size_t i = 0;

  if (buf == NULL && len > 0)
    {
      return 0;
    }

  while (i < len)
    {
      unsigned char c = (unsigned char)buf[i];
      size_t step;

      if (c < 0x20 || c == 0x7f)
        {
          if (c != (unsigned char)allow)
            {
              return 0;
            }

          i++;
          continue;
        }

      step = utf8_step(buf + i, len - i);
      if (step == 0)
        {
          return 0;
        }

      i += step;
    }

  return 1;
}

/* Copy a value into a fixed field.  Control bytes and malformed UTF-8 are
 * rejected outright; an over-long value is cut back to a character boundary
 * so a chatty model costs a word rather than the whole round. */

static int copy_field(const char *v, size_t vlen, char *dst, size_t cap)
{
  size_t i = 0;
  size_t out = 0;

  if (dst == NULL || cap == 0)
    {
      return VG_AI_ERR_ARG;
    }

  while (i < vlen)
    {
      unsigned char c = (unsigned char)v[i];
      size_t step;

      if (c < 0x20 || c == 0x7f)
        {
          return VG_AI_ERR_UTF8;
        }

      step = utf8_step(v + i, vlen - i);
      if (step == 0)
        {
          return VG_AI_ERR_UTF8;
        }

      if (out + step > cap)
        {
          break;
        }

      memcpy(dst + out, v + i, step);
      out += step;
      i += step;
    }

  dst[out] = '\0';
  return VG_AI_OK;
}

/****************************************************************************
 * Line grammar helpers
 ****************************************************************************/

static int next_line(const char **cur, const char *end,
                     const char **ls, size_t *llen)
{
  const char *p = *cur;
  const char *nl;

  if (p >= end)
    {
      return 1;
    }

  nl = memchr(p, '\n', (size_t)(end - p));
  if (nl == NULL)
    {
      *ls = p;
      *llen = (size_t)(end - p);
      *cur = end;
      return 0;
    }

  *ls = p;
  *llen = (size_t)(nl - p);
  *cur = nl + 1;
  return 0;
}

/* Split "key=value".  A line without '=' is a structural error; a line whose
 * key is simply not the one we expect is reported separately so a host test
 * can tell a stray key from a broken document. */

static int expect_key(const char *ls, size_t llen, const char *key,
                      const char **val, size_t *vlen)
{
  size_t klen = strlen(key);
  const char *eq = memchr(ls, '=', llen);

  if (eq == NULL)
    {
      return VG_AI_ERR_FORMAT;
    }

  if ((size_t)(eq - ls) != klen || memcmp(ls, key, klen) != 0)
    {
      return VG_AI_ERR_KEY;
    }

  *val = eq + 1;
  *vlen = llen - (klen + 1);
  return VG_AI_OK;
}

static int parse_dec(const char *v, size_t vlen, uint32_t *out)
{
  uint32_t acc = 0;
  size_t i;

  if (vlen == 0)
    {
      return VG_AI_ERR_RANGE;
    }

  for (i = 0; i < vlen; i++)
    {
      unsigned char c = (unsigned char)v[i];

      if (c < '0' || c > '9')
        {
          return VG_AI_ERR_RANGE;
        }

      if (acc > (0xffffffffu - (uint32_t)(c - '0')) / 10u)
        {
          return VG_AI_ERR_RANGE;
        }

      acc = acc * 10u + (uint32_t)(c - '0');
    }

  *out = acc;
  return VG_AI_OK;
}

static int parse_hex8(const char *v, size_t vlen, uint32_t *out)
{
  uint32_t acc = 0;
  size_t i;

  if (vlen != VG_AI_BOOT_HEX)
    {
      return VG_AI_ERR_RANGE;
    }

  for (i = 0; i < vlen; i++)
    {
      unsigned char c = (unsigned char)v[i];
      uint32_t d;

      if (c >= '0' && c <= '9')
        {
          d = (uint32_t)(c - '0');
        }
      else if (c >= 'a' && c <= 'f')
        {
          d = (uint32_t)(c - 'a') + 10u;
        }
      else if (c >= 'A' && c <= 'F')
        {
          /* Models echo the stamp back in either case; rejecting upper case
           * would throw away a whole round over nothing. */

          d = (uint32_t)(c - 'A') + 10u;
        }
      else
        {
          return VG_AI_ERR_RANGE;
        }

      acc = (acc << 4) | d;
    }

  *out = acc;
  return VG_AI_OK;
}

static int parse_sev(const char *v, size_t vlen, uint8_t *out)
{
  if (vlen == 4 && memcmp(v, "warn", 4) == 0)
    {
      *out = VG_AI_SEV_WARN;
      return VG_AI_OK;
    }

  if (vlen == 4 && memcmp(v, "crit", 4) == 0)
    {
      *out = VG_AI_SEV_CRIT;
      return VG_AI_OK;
    }

  if (vlen == 7 && memcmp(v, "offline", 7) == 0)
    {
      *out = VG_AI_SEV_OFFLINE;
      return VG_AI_OK;
    }

  return VG_AI_ERR_RANGE;
}

/****************************************************************************
 * VGADV1 parsing
 ****************************************************************************/

/* Scratch for the "nothing reaches *out unless the whole document passes"
 * rule.  Module-static rather than a local: one document is about 4 KB, and
 * the board calls this from the HMI file worker, whose stack is 8 KB.  The
 * only callers are that worker and the host tests, so a single scratch
 * instance is enough. */

static vg_ai_advice_doc_t s_parse_scratch;

/****************************************************************************
 * Identity header only.
 *
 * Why this exists: a document is accepted on its boot stamp and its entries'
 * (id, epoch), and the req it was written for says nothing about whether it
 * still applies.  A round that times out while the model is still working
 * leaves its file behind one round later; comparing that file's req against
 * the live counter rejected the whole document, and the page went to the rule
 * summary even though the advice matched the alarms on screen exactly.  The
 * board reads the header first, checks the boot stamp itself, and lets the
 * per-point epochs decide what may be shown.
 ****************************************************************************/

int vg_ai_advice_head(const char *buf, size_t len, uint32_t *boot,
                      uint32_t *req)
{
  const char *cur;
  const char *end;
  const char *ls;
  const char *v;
  size_t llen;
  size_t vlen;
  uint32_t u32;
  int rc;

  if (buf == NULL || len == 0 || len > VG_AI_DOC_MAX)
    {
      return VG_AI_ERR_ARG;
    }

  cur = buf;
  end = buf + len;

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      llen != 6 || memcmp(ls, "VGADV1", 6) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  rc = expect_key(ls, llen, "boot", &v, &vlen);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  rc = parse_hex8(v, vlen, &u32);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (boot != NULL)
    {
      *boot = u32;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  rc = expect_key(ls, llen, "req", &v, &vlen);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  rc = parse_dec(v, vlen, &u32);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (req != NULL)
    {
      *req = u32;
    }

  return VG_AI_OK;
}

int vg_ai_advice_parse(const char *buf, size_t len,
                       uint32_t expect_boot, uint32_t expect_req,
                       vg_ai_advice_doc_t *out)
{
  vg_ai_advice_doc_t *tmp = &s_parse_scratch;
  const char *cur;
  const char *end;
  const char *ls;
  const char *v;
  size_t llen;
  size_t vlen;
  uint32_t u32;
  int rc;
  int i;

  if (buf == NULL || out == NULL)
    {
      return VG_AI_ERR_ARG;
    }

  if (len == 0 || len > VG_AI_DOC_MAX)
    {
      return VG_AI_ERR_RANGE;
    }

  /* \r would silently change the line grammar and NUL would hide a suffix
   * from every strlen-based consumer, so both fail the whole document. */

  for (i = 0; i < (int)len; i++)
    {
      if (buf[i] == '\r' || buf[i] == '\0')
        {
          return VG_AI_ERR_UTF8;
        }
    }

  if (!vg_ai_utf8_valid(buf, len, '\n'))
    {
      return VG_AI_ERR_UTF8;
    }

  memset(tmp, 0, sizeof(*tmp));
  cur = buf;
  end = buf + len;

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      llen != 6 || memcmp(ls, "VGADV1", 6) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  rc = expect_key(ls, llen, "boot", &v, &vlen);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  rc = parse_hex8(v, vlen, &tmp->boot);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  rc = expect_key(ls, llen, "req", &v, &vlen);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  rc = parse_dec(v, vlen, &tmp->req);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  rc = expect_key(ls, llen, "n", &v, &vlen);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  rc = parse_dec(v, vlen, &u32);
  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (u32 > VG_AI_ADV_MAX)
    {
      return VG_AI_ERR_RANGE;
    }

  tmp->n = (int)u32;

  for (i = 0; i < tmp->n; i++)
    {
      vg_ai_advice_entry_t *e = &tmp->e[i];
      char want[16];
      int wlen;

      wlen = snprintf(want, sizeof(want), "[%d]", i + 1);
      if (next_line(&cur, end, &ls, &llen) != 0 ||
          llen != (size_t)wlen || memcmp(ls, want, (size_t)wlen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "id", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (vlen >= sizeof(e->id))
        {
          return VG_AI_ERR_RANGE;
        }

      memcpy(e->id, v, vlen);
      e->id[vlen] = '\0';

      if (!vg_ai_id_valid(e->id))
        {
          return VG_AI_ERR_RANGE;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "epoch", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      rc = parse_dec(v, vlen, &e->epoch);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "sev", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      rc = parse_sev(v, vlen, &e->sev);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "unres", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (vlen != 1 || (v[0] != '0' && v[0] != '1'))
        {
          return VG_AI_ERR_RANGE;
        }

      e->unresolved = (v[0] == '1');

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "sum", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      rc = copy_field(v, vlen, e->sum, VG_AI_SUM_MAX);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (e->sum[0] == '\0')
        {
          return VG_AI_ERR_RANGE;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "ev", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      rc = copy_field(v, vlen, e->ev, VG_AI_TEXT_MAX);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          return VG_AI_ERR_COUNT;
        }

      rc = expect_key(ls, llen, "att", &v, &vlen);
      if (rc != VG_AI_OK)
        {
          return rc;
        }

      rc = copy_field(v, vlen, e->att, VG_AI_TEXT_MAX);
      if (rc != VG_AI_OK)
        {
          return rc;
        }
    }

  for (i = 0; i < tmp->n; i++)
    {
      int j;

      for (j = 0; j < i; j++)
        {
          if (strcmp(tmp->e[i].id, tmp->e[j].id) == 0)
            {
              return VG_AI_ERR_DUP;
            }
        }
    }

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      llen != 3 || memcmp(ls, "END", 3) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  /* Nothing may follow END except the single newline that terminated it. */

  if (next_line(&cur, end, &ls, &llen) == 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  if (tmp->boot != expect_boot || tmp->req != expect_req)
    {
      return VG_AI_ERR_STALE;
    }

  *out = *tmp;
  return VG_AI_OK;
}

const vg_ai_advice_entry_t *vg_ai_advice_find(const vg_ai_advice_doc_t *doc,
                                              const char *id, uint32_t epoch)
{
  int i;

  if (doc == NULL || id == NULL)
    {
      return NULL;
    }

  for (i = 0; i < doc->n && i < VG_AI_ADV_MAX; i++)
    {
      if (doc->e[i].epoch == epoch && strcmp(doc->e[i].id, id) == 0)
        {
          return &doc->e[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Request building
 ****************************************************************************/

typedef struct
{
  char  *buf;
  size_t cap;
  size_t len;
  int    over;
} sb_t;

static void sb_init(sb_t *sb, char *buf, size_t cap)
{
  sb->buf = buf;
  sb->cap = cap;
  sb->len = 0;
  sb->over = 0;

  if (buf != NULL && cap > 0)
    {
      buf[0] = '\0';
    }
  else
    {
      sb->over = 1;
    }
}

static void sb_printf(sb_t *sb, const char *fmt, ...)
{
  va_list ap;
  int w;

  if (sb->over || sb->cap == 0 || sb->len >= sb->cap - 1)
    {
      sb->over = 1;
      return;
    }

  va_start(ap, fmt);
  w = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
  va_end(ap);

  if (w < 0)
    {
      sb->over = 1;
      return;
    }

  if ((size_t)w >= sb->cap - sb->len)
    {
      /* Cut short: keep what fits but flag the request as unusable, so the
       * caller shortens the alarm list instead of sending a broken prompt. */

      sb->len = sb->cap - 1;
      sb->over = 1;
      return;
    }

  sb->len += (size_t)w;
}

/* Append at most max_bytes of an optionally long UTF-8 string, never
 * splitting a sequence.  Used for point names, which are informational. */

static void sb_text(sb_t *sb, const char *s, size_t max_bytes)
{
  size_t i = 0;
  size_t n;

  if (s == NULL)
    {
      return;
    }

  n = strlen(s);

  while (i < n && i < max_bytes)
    {
      size_t step = utf8_step(s + i, n - i);

      if (step == 0)
        {
          return;   /* not UTF-8: drop the rest of the name */
        }

      if (i + step > max_bytes)
        {
          return;
        }

      if (sb->over || sb->cap == 0 || sb->len + step >= sb->cap)
        {
          sb->over = 1;
          return;
        }

      memcpy(sb->buf + sb->len, s + i, step);
      sb->len += step;
      sb->buf[sb->len] = '\0';
      i += step;
    }
}

/* One decimal place without floating-point printf, and "-" for NaN/Inf so a
 * broken reading cannot leak "nan" into the prompt. */

static void fmt_num(char *out, size_t n, float v)
{
  int vi;
  int vf;

  if (!(v >= -1.0e9f && v <= 1.0e9f))
    {
      snprintf(out, n, "-");
      return;
    }

  vi = (int)v;
  vf = (int)((v - (float)vi) * 10.0f);
  if (vf < 0)
    {
      vf = -vf;
    }

  snprintf(out, n, "%d.%d", vi, vf);
}

static const char *sev_word(uint8_t sev)
{
  switch (sev)
    {
      case VG_AI_SEV_WARN:
        return "warn";
      case VG_AI_SEV_CRIT:
        return "crit";
      case VG_AI_SEV_OFFLINE:
        return "offline";
      default:
        return "warn";
    }
}

int vg_ai_advice_build_request(char *out, size_t cap,
                               uint32_t boot, uint32_t req,
                               const vg_ai_alarm_in_t *a, int n)
{
  sb_t sb;
  int i;

  if (out == NULL || cap == 0)
    {
      return VG_AI_ERR_ARG;
    }

  if (n < 0 || n > VG_AI_ADV_MAX || (n > 0 && a == NULL))
    {
      return VG_AI_ERR_ARG;
    }

  sb_init(&sb, out, cap);

  /* Kept terse on purpose: the request shares a 1536-byte budget with up to
   * eight alarm rows, and the long form of this header pushed a bench run
   * with verbose point names over the cap, which the builder rejects whole.
   * The field meanings live in the skill. */

  sb_printf(&sb,
    "按 /data/agent/skills/alarm_interpretation.md 为下面的活动告警各写一条建议。\n"
    "只读查询，不要写寄存器、不要改配置、不要清除告警。\n"
    "必须用 write_file 写 /data/velaguard/reports/alarm_advice.txt，"
    "只回文字不算完成。\n"
    "格式（逐行，顺序不可变；id/epoch/sev 原样抄，boot/req 原样回填）：\n"
    "VGADV1\n"
    "boot=%08x\n"
    "req=%u\n"
    "n=%d\n"
    "[1]\n"
    "id=...\n"
    "epoch=...\n"
    "sev=...\n"
    "unres=0|1\n"
    "sum=<一句话，<=24 汉字>\n"
    "ev=<依据，<=60 汉字>\n"
    "att=<关注，<=60 汉字>\n"
    "（第 2 条起同格式，序号递增）\n"
    "END\n"
    "全部中文常用字，不要 Markdown；信息不足写 unres=1，不要编造。\n"
    "当前活动告警 %d 条：\n",
    (unsigned)boot, (unsigned)req, n, n);

  for (i = 0; i < n; i++)
    {
      char val[24];
      char thr[24];
      const char *id = (a[i].id != NULL) ? a[i].id : "";

      fmt_num(val, sizeof(val), a[i].value);
      fmt_num(thr, sizeof(thr), a[i].threshold);

      sb_printf(&sb, "%d) id=", i + 1);
      sb_text(&sb, id, VG_AI_ID_MAX - 1);
      sb_printf(&sb, " epoch=%u sev=%s 值=%s 阈值=%s 持续=%lds 点名=",
                (unsigned)a[i].epoch, sev_word(a[i].sev), val, thr,
                (long)a[i].dur_s);
      sb_text(&sb, a[i].name, 24);
      sb_printf(&sb, "\n");
    }

  if (sb.over)
    {
      out[cap - 1] = '\0';
      return VG_AI_ERR_RANGE;
    }

  return (int)sb.len;
}

/****************************************************************************
 * Daily report validation
 ****************************************************************************/

static int line_is(const char *ls, size_t llen, const char *want)
{
  size_t n = strlen(want);

  return (llen == n && memcmp(ls, want, n) == 0);
}

int vg_ai_report_validate(const char *buf, size_t len,
                          const char *expect_date,
                          long mtime_s, long now_s)
{
  const char *cur;
  const char *end;
  const char *ls;
  size_t llen;
  size_t dlen;
  int i;
  int rc;

  if (buf == NULL || expect_date == NULL)
    {
      return VG_AI_ERR_ARG;
    }

  dlen = strlen(expect_date);
  if (dlen != 10)
    {
      return VG_AI_ERR_ARG;
    }

  if (len == 0 || len > VG_AI_REPORT_MAX)
    {
      return VG_AI_ERR_RANGE;
    }

  for (i = 0; i < (int)len; i++)
    {
      if (buf[i] == '\r' || buf[i] == '\0')
        {
          return VG_AI_ERR_UTF8;
        }
    }

  if (!vg_ai_utf8_valid(buf, len, '\n'))
    {
      return VG_AI_ERR_UTF8;
    }

  cur = buf;
  end = buf + len;

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      !line_is(ls, llen, "AI-DAILY v1"))
    {
      return VG_AI_ERR_FORMAT;
    }

  if (next_line(&cur, end, &ls, &llen) != 0)
    {
      return VG_AI_ERR_FORMAT;
    }

  {
    const char *v;
    size_t vlen;

    rc = expect_key(ls, llen, "date", &v, &vlen);
    if (rc != VG_AI_OK)
      {
        return rc;
      }

    if (vlen != dlen || memcmp(v, expect_date, dlen) != 0)
      {
        return VG_AI_ERR_STALE;
      }
  }

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      !line_is(ls, llen, "source=agent"))
    {
      return VG_AI_ERR_FORMAT;
    }

  if (next_line(&cur, end, &ls, &llen) != 0 ||
      !line_is(ls, llen, "---"))
    {
      return VG_AI_ERR_FORMAT;
    }

  /* At least one more non-empty line: an empty body is not a report. */

  rc = VG_AI_ERR_FORMAT;
  for (;;)
    {
      if (next_line(&cur, end, &ls, &llen) != 0)
        {
          break;
        }

      if (llen > 0)
        {
          rc = VG_AI_OK;
        }
    }

  if (rc != VG_AI_OK)
    {
      return rc;
    }

  if (mtime_s > 0 && now_s > 0)
    {
      if (mtime_s > now_s + 300)
        {
          return VG_AI_ERR_RANGE;   /* file from the future: clock moved */
        }

      if (now_s - mtime_s > 26 * 3600)
        {
          return VG_AI_ERR_STALE;
        }
    }

  return VG_AI_OK;
}
