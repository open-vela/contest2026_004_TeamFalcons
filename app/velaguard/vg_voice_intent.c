/****************************************************************************
 * app/velaguard/vg_voice_intent.c
 ****************************************************************************/

#include "vg_voice_intent.h"

#include <stdio.h>
#include <string.h>

/* ── byte helpers ───────────────────────────────────────────────── */

static bool has(const char *text, const char *needle)
{
  return text != NULL && needle != NULL && needle[0] != '\0' &&
         strstr(text, needle) != NULL;
}

static bool ends_with(const char *text, const char *suffix)
{
  size_t tl;
  size_t sl;

  if (text == NULL || suffix == NULL)
    {
      return false;
    }

  tl = strlen(text);
  sl = strlen(suffix);
  return tl >= sl && strcmp(text + tl - sl, suffix) == 0;
}

static bool starts_with(const char *text, const char *prefix)
{
  return text != NULL && prefix != NULL &&
         strncmp(text, prefix, strlen(prefix)) == 0;
}

static void trim(char *s)
{
  size_t n;
  size_t start = 0;
  size_t i;

  if (s == NULL)
    {
      return;
    }

  n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                   s[n - 1] == '\r' || s[n - 1] == '\n'))
    {
      s[--n] = '\0';
    }

  while (start < n && (s[start] == ' ' || s[start] == '\t'))
    {
      start++;
    }

  if (start > 0)
    {
      for (i = 0; i + start <= n; i++)
        {
          s[i] = s[i + start];
        }
    }
}

static void remove_tail(char *s, size_t cut)
{
  size_t n = strlen(s);

  if (cut >= n)
    {
      s[0] = '\0';
      return;
    }

  s[n - cut] = '\0';
}

/* Fillers are stripped from the ends only.  A middle substring may be part of
 * a point name, and cutting it would silently ask about a different point. */

static const char *const k_prefix[] =
{
  "请问", "麻烦", "帮我看看", "帮我查", "帮我", "帮忙", "我想知道", "我想问",
  "查一下", "查查", "看一下", "看看", "告诉我", "现在", "当前", "目前",
  NULL
};

static const char *const k_suffix[] =
{
  "是多少", "是多少呢", "多少", "是几", "读数", "数值", "现在的值", "当前的值",
  "的值", "值", "现在", "当前", "怎么样", "如何",
  "恢复了吗", "恢复没有", "恢复了没有", "恢复", "解除了吗", "解除",
  "消除了吗", "消除", "正常了吗", "正常了没", "正常没有", "正常么",
  "好了吗", "好了", "还报吗", "还在报吗", "还报警吗", "还在报警吗",
  "报警吗", "告警吗", "异常吗", "还在吗",
  "的告警", "的报警", "告警", "报警", "呢", "吗", "了", "呀", "啊", "的",
  NULL
};

static bool strip_once(char *s)
{
  int i;

  for (i = 0; k_prefix[i] != NULL; i++)
    {
      if (starts_with(s, k_prefix[i]))
        {
          size_t cut = strlen(k_prefix[i]);

          memmove(s, s + cut, strlen(s + cut) + 1);
          trim(s);
          return true;
        }
    }

  for (i = 0; k_suffix[i] != NULL; i++)
    {
      if (ends_with(s, k_suffix[i]))
        {
          remove_tail(s, strlen(k_suffix[i]));
          trim(s);
          return true;
        }
    }

  return false;
}

int vg_vi_point_token(const char *text, char *out, size_t cap)
{
  char buf[128];
  int guard;

  if (out == NULL || cap == 0)
    {
      return 0;
    }

  out[0] = '\0';

  if (text == NULL || text[0] == '\0')
    {
      return 0;
    }

  snprintf(buf, sizeof(buf), "%s", text);
  trim(buf);

  /* Bounded: each pass removes at least one byte, and the guard keeps a
   * pathological suffix list from spinning. */

  for (guard = 0; guard < 24 && buf[0] != '\0'; guard++)
    {
      if (!strip_once(buf))
        {
          break;
        }
    }

  if (strlen(buf) >= cap)
    {
      return 0;
    }

  snprintf(out, cap, "%s", buf);
  return (int)strlen(out);
}

int vg_vi_normalize_digits(const char *in, char *out, size_t cap)
{
  static const struct
  {
    const char *from;
    char        to;
  } map[] =
  {
    { "零", '0' }, { "〇", '0' }, { "幺", '1' }, { "一", '1' },
    { "二", '2' }, { "两", '2' }, { "三", '3' }, { "四", '4' },
    { "五", '5' }, { "六", '6' }, { "七", '7' }, { "八", '8' },
    { "九", '9' }
  };

  size_t o = 0;
  size_t i = 0;

  if (out == NULL || cap == 0)
    {
      return 0;
    }

  out[0] = '\0';

  if (in == NULL)
    {
      return 0;
    }

  while (in[i] != '\0')
    {
      size_t k;
      bool replaced = false;

      for (k = 0; k < sizeof(map) / sizeof(map[0]); k++)
        {
          size_t fl = strlen(map[k].from);

          if (strncmp(in + i, map[k].from, fl) == 0)
            {
              if (o + 1 >= cap)
                {
                  out[0] = '\0';
                  return 0;
                }

              out[o++] = map[k].to;
              i += fl;
              replaced = true;
              break;
            }
        }

      if (replaced)
        {
          continue;
        }

      if (o + 1 >= cap)
        {
          out[0] = '\0';
          return 0;
        }

      out[o++] = in[i++];
    }

  out[o] = '\0';
  return (int)o;
}

/* ── classification ─────────────────────────────────────────────── */

/* Words that ask for a change.  The voice path cannot perform any of them, so
 * the answer is a refusal rather than a point reading that happens to share a
 * word.  Erring toward refusal is safe: it never changes device state. */

static const char *const k_control[] =
{
  "设置", "修改", "改成", "改到", "改为", "写入", "写值", "清除", "清掉",
  "删除", "删掉", "关闭", "打开", "开启", "启动", "停止", "控制", "重启",
  "配置", "添加", "新增", "应用", "下发", "设为", "调到", "切换", "调节",
  "调整", "重置", "复位", "设为", "输出", "打开", "关掉", "开一下",
  NULL
};

static bool is_control(const char *text)
{
  int i;

  for (i = 0; k_control[i] != NULL; i++)
    {
      if (has(text, k_control[i]))
        {
          return true;
        }
    }

  return false;
}

static bool is_recovery(const char *text)
{
  return has(text, "恢复") || has(text, "解除") || has(text, "好了") ||
         has(text, "还报") || has(text, "还在报") || has(text, "仍然报") ||
         has(text, "还报警") || has(text, "没恢复") || has(text, "恢复了没") ||
         has(text, "消除") || has(text, "消了") || has(text, "还在吗") ||
         has(text, "正常了吗") || has(text, "正常了没") ||
         has(text, "正常没有") || has(text, "正常么");
}

static bool is_anomaly(const char *text)
{
  return has(text, "异常") || has(text, "告警") || has(text, "报警") ||
         has(text, "故障") || has(text, "问题") || has(text, "哪些点") ||
         has(text, "报警点");
}

enum vg_voice_intent vg_vi_classify(const char *text,
                                    struct vg_voice_intent_result *out)
{
  enum vg_voice_intent intent = VG_VI_UNKNOWN;

  if (out != NULL)
    {
      memset(out, 0, sizeof(*out));
    }

  if (text == NULL || text[0] == '\0')
    {
      return VG_VI_UNKNOWN;
    }

  if (is_control(text))
    {
      intent = VG_VI_REFUSED;
    }
  else if (is_recovery(text))
    {
      intent = VG_VI_RECOVERY;
    }
  else if (is_anomaly(text))
    {
      intent = VG_VI_ANOMALY;
    }
  else
    {
      /* A bare point name is a read request in this UI.  Whether the name
       * exists is decided by the table lookup, not here. */

      intent = VG_VI_POINT;
    }

  if (out != NULL)
    {
      out->intent = intent;

      if (intent == VG_VI_POINT || intent == VG_VI_RECOVERY)
        {
          out->has_token =
              vg_vi_point_token(text, out->token, sizeof(out->token)) > 0;
        }
    }

  return intent;
}

/* ── point resolution ───────────────────────────────────────────── */

static bool id_matches_normalized(const struct vg_agent_point *points, int n,
                                  const char *token,
                                  struct vg_agent_query_result *out)
{
  char norm[VG_AGENT_QUERY_STR_MAX];
  int i;

  if (vg_vi_normalize_digits(token, norm, sizeof(norm)) <= 0)
    {
      return false;
    }

  /* Only ids: normalizing digits inside a name would let 一 become 1 and
   * change which name the user said. */

  for (i = 0; i < n; i++)
    {
      if (strcmp(points[i].id, norm) == 0)
        {
          memset(out, 0, sizeof(*out));
          out->mode = VG_AGENT_MATCH_ID;
          out->index = i;
          return true;
        }
    }

  return false;
}

enum vg_agent_query_rc
vg_vi_resolve_point(const char *text, const struct vg_agent_point *points,
                    int n, struct vg_agent_query_result *out,
                    char *token, size_t token_cap)
{
  char buf[VG_AGENT_QUERY_STR_MAX];
  enum vg_agent_query_rc rc;

  if (token != NULL && token_cap > 0)
    {
      token[0] = '\0';
    }

  if (text == NULL || points == NULL || out == NULL || n <= 0)
    {
      return VG_AGENT_QUERY_ARG;
    }

  if (vg_vi_point_token(text, buf, sizeof(buf)) <= 0)
    {
      return VG_AGENT_QUERY_NOT_FOUND;
    }

  rc = vg_agent_query_match(points, n, buf, out);
  if (rc == VG_AGENT_QUERY_OK || rc == VG_AGENT_QUERY_AMBIG)
    {
      if (token != NULL && token_cap > 0)
        {
          snprintf(token, token_cap, "%s", buf);
        }

      return rc;
    }

  if (id_matches_normalized(points, n, buf, out))
    {
      if (token != NULL && token_cap > 0)
        {
          snprintf(token, token_cap, "%s", buf);
        }

      return VG_AGENT_QUERY_OK;
    }

  /* The filler stripper may have cut a name that itself ends in one of the
   * suffix words.  Retry with the untouched sentence before saying the point
   * does not exist. */

  if (strcmp(buf, text) != 0)
    {
      rc = vg_agent_query_match(points, n, text, out);
      if (rc == VG_AGENT_QUERY_OK || rc == VG_AGENT_QUERY_AMBIG)
        {
          if (token != NULL && token_cap > 0)
            {
              snprintf(token, token_cap, "%s", text);
            }

          return rc;
        }
    }

  return VG_AGENT_QUERY_NOT_FOUND;
}
