/* Bounds and grammar tests for the board-side AI text contract.
 *
 * The parser is the only door between model output and the screen, so every
 * rejection path and every cap gets a case here. */

#include <stdio.h>
#include <string.h>

#include "../vg_ai_contract.h"

static int expect_true(int cond, const char *msg)
{
  if (!cond)
    {
      fprintf(stderr, "FAIL: %s\n", msg);
      return 1;
    }

  return 0;
}

static int expect_rc(int got, int want, const char *msg)
{
  if (got != want)
    {
      fprintf(stderr, "FAIL: %s (got %d, want %d)\n", msg, got, want);
      return 1;
    }

  return 0;
}

static const char DOC3[] =
  "VGADV1\n"
  "boot=0a1b2c3d\n"
  "req=7\n"
  "n=3\n"
  "[1]\n"
  "id=water_1\n"
  "epoch=2\n"
  "sev=crit\n"
  "unres=0\n"
  "sum=水浸触发，先确认现场是否积水\n"
  "ev=当前值超过阈值\n"
  "att=检查地漏与排水泵\n"
  "[2]\n"
  "id=temp_1\n"
  "epoch=5\n"
  "sev=warn\n"
  "unres=0\n"
  "sum=温度接近上限\n"
  "ev=当前值 41.2 阈值 40.0\n"
  "att=检查散热\n"
  "[3]\n"
  "id=slave2_hr\n"
  "epoch=1\n"
  "sev=offline\n"
  "unres=1\n"
  "sum=通信离线，信息不足\n"
  "ev=\n"
  "att=\n"
  "END\n";

/* Build an n-entry document; only the first three carry text, the rest get
 * an empty-but-legal body.  Returns the length. */

static size_t build_doc_n(char *out, size_t cap, int n)
{
  size_t len = 0;
  int i;

  len += (size_t)snprintf(out + len, cap - len,
                          "VGADV1\nboot=0a1b2c3d\nreq=7\nn=%d\n", n);

  for (i = 0; i < n; i++)
    {
      len += (size_t)snprintf(out + len, cap - len,
                              "[%d]\nid=p%d\nepoch=%d\nsev=warn\nunres=0\n"
                              "sum=x\nev=y\natt=z\n",
                              i + 1, i, i + 1);
    }

  len += (size_t)snprintf(out + len, cap - len, "END\n");
  return len;
}

static int test_ids_and_utf8(void)
{
  int fails = 0;

  fails += expect_true(vg_ai_id_valid("water_1") == 1, "id water_1");
  fails += expect_true(vg_ai_id_valid("A1_") == 1, "id A1_");
  fails += expect_true(vg_ai_id_valid("") == 0, "id empty");
  fails += expect_true(vg_ai_id_valid(NULL) == 0, "id null");
  fails += expect_true(vg_ai_id_valid("a b") == 0, "id space");
  fails += expect_true(vg_ai_id_valid("a/b") == 0, "id slash");
  fails += expect_true(vg_ai_id_valid("..") == 0, "id dotdot");
  fails += expect_true(vg_ai_id_valid("abcdefghijklmnopqrstuvwx") == 0,
                       "id 24 chars");

  fails += expect_true(vg_ai_utf8_valid("正常中文", 12, '\n') == 1,
                       "utf8 chinese");
  fails += expect_true(vg_ai_utf8_valid("a\nb", 3, '\n') == 1,
                       "utf8 newline allowed");
  fails += expect_true(vg_ai_utf8_valid("a\nb", 3, 0) == 0,
                       "utf8 newline rejected when not allowed");
  fails += expect_true(vg_ai_utf8_valid("a\x01", 2, '\n') == 0,
                       "utf8 control byte");
  fails += expect_true(vg_ai_utf8_valid("\xc0\x80", 2, '\n') == 0,
                       "utf8 overlong");
  fails += expect_true(vg_ai_utf8_valid("\xed\xa0\x80", 3, '\n') == 0,
                       "utf8 surrogate");
  fails += expect_true(vg_ai_utf8_valid("\xf5\x80\x80\x80", 4, '\n') == 0,
                       "utf8 above U+10FFFF");
  fails += expect_true(vg_ai_utf8_valid("\xe4\xb8", 2, '\n') == 0,
                       "utf8 truncated sequence");
  fails += expect_true(vg_ai_utf8_valid(NULL, 0, '\n') == 1,
                       "utf8 empty ok");

  return fails;
}

static int test_parse_ok(void)
{
  vg_ai_advice_doc_t doc;
  const vg_ai_advice_entry_t *e;
  int fails = 0;

  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(DOC3, sizeof(DOC3) - 1,
                                        0x0a1b2c3d, 7, &doc),
                     VG_AI_OK, "parse DOC3");
  fails += expect_true(doc.n == 3, "DOC3 n");
  fails += expect_true(doc.boot == 0x0a1b2c3du, "DOC3 boot");
  fails += expect_true(doc.req == 7, "DOC3 req");
  fails += expect_true(strcmp(doc.e[0].id, "water_1") == 0, "DOC3 e0 id");
  fails += expect_true(doc.e[0].epoch == 2, "DOC3 e0 epoch");
  fails += expect_true(doc.e[0].sev == VG_AI_SEV_CRIT, "DOC3 e0 sev");
  fails += expect_true(doc.e[0].unresolved == false, "DOC3 e0 unres");
  fails += expect_true(strcmp(doc.e[0].sum, "水浸触发，先确认现场是否积水") == 0,
                       "DOC3 e0 sum");
  fails += expect_true(doc.e[2].sev == VG_AI_SEV_OFFLINE, "DOC3 e2 offline");
  fails += expect_true(doc.e[2].unresolved == true, "DOC3 e2 unresolved");
  fails += expect_true(doc.e[2].ev[0] == '\0', "DOC3 e2 empty ev");

  e = vg_ai_advice_find(&doc, "temp_1", 5);
  fails += expect_true(e != NULL && e->sev == VG_AI_SEV_WARN, "find hit");

  /* Same point, next alarm episode: the advice must not carry over. */

  e = vg_ai_advice_find(&doc, "temp_1", 6);
  fails += expect_true(e == NULL, "find epoch mismatch");
  e = vg_ai_advice_find(&doc, "temp_9", 5);
  fails += expect_true(e == NULL, "find id mismatch");
  e = vg_ai_advice_find(&doc, "temp_1", 5);
  fails += expect_true(e != NULL, "find hit again");

  return fails;
}

static int test_entry_count(void)
{
  static char buf[4096];
  vg_ai_advice_doc_t doc;
  size_t len;
  int fails = 0;

  len = build_doc_n(buf, sizeof(buf), VG_AI_ADV_MAX);
  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0x0a1b2c3du, 7, &doc),
                     VG_AI_OK, "8 entries");
  fails += expect_true(doc.n == VG_AI_ADV_MAX, "8 entries n");

  len = build_doc_n(buf, sizeof(buf), VG_AI_ADV_MAX + 1);
  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0x0a1b2c3du, 7, &doc),
                     VG_AI_ERR_RANGE, "9 entries rejected");

  /* n=0 is legal: a round that found nothing to advise on. */

  len = (size_t)snprintf(buf, sizeof(buf),
                         "VGADV1\nboot=0a1b2c3d\nreq=7\nn=0\nEND\n");
  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0x0a1b2c3du, 7, &doc),
                     VG_AI_OK, "n=0 ok");
  fails += expect_true(doc.n == 0, "n=0 count");

  return fails;
}

static int test_doc_size_cap(void)
{
  static char buf[8192];
  vg_ai_advice_doc_t doc;
  size_t len;
  int fails = 0;

  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse("", 0, 0, 0, &doc),
                     VG_AI_ERR_RANGE, "len 0 rejected");
  fails += expect_rc(vg_ai_advice_parse(NULL, 10, 0, 0, &doc),
                     VG_AI_ERR_ARG, "NULL buffer rejected");
  fails += expect_rc(vg_ai_advice_parse(DOC3, sizeof(DOC3) - 1, 0, 0, NULL),
                     VG_AI_ERR_ARG, "NULL out rejected");

  len = VG_AI_DOC_MAX + 1;
  memset(buf, 'a', sizeof(buf));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0, 0, &doc),
                     VG_AI_ERR_RANGE, "over doc cap rejected");

  return fails;
}

static int test_structure_failures(void)
{
  vg_ai_advice_doc_t doc;
  int fails = 0;

  /* Missing END. */

  fails += expect_rc(vg_ai_advice_parse("VGADV1\nboot=0a1b2c3d\nreq=7\nn=0\n",
                                        30, 0x0a1b2c3du, 7, &doc),
                     VG_AI_ERR_FORMAT, "missing END");

  /* Trailing garbage after END. */

  {
    static const char bad[] = "VGADV1\nboot=0a1b2c3d\nreq=7\nn=0\nEND\nx\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_FORMAT, "trailing garbage");
  }

  /* Wrong marker. */

  {
    static const char bad[] = "VGADV2\nboot=0a1b2c3d\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_FORMAT, "wrong marker");
  }

  /* Unknown key where boot belongs. */

  {
    static const char bad[] = "VGADV1\nfoo=0a1b2c3d\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_KEY, "unknown key");
  }

  /* Right shape, wrong round number. */

  {
    static const char bad[] = "VGADV1\nboot=0a1b2c3d\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 8, &doc),
                       VG_AI_ERR_STALE, "req mismatch");
  }

  /* Entry index must run [1]..[n] in order. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[2]\nid=p0\nepoch=1\nsev=warn\n"
      "unres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_COUNT, "entry index out of order");
  }

  /* Truncated entry: n promises 2 but only 1 is present. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=2\n[1]\nid=p0\nepoch=1\nsev=warn\n"
      "unres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_COUNT, "missing second entry");
  }

  return fails;
}

static int test_boot_req_identity(void)
{
  vg_ai_advice_doc_t doc;
  int fails = 0;

  fails += expect_rc(vg_ai_advice_parse(DOC3, sizeof(DOC3) - 1,
                                        0x0a1b2c3du, 8, &doc),
                     VG_AI_ERR_STALE, "req mismatch");
  fails += expect_rc(vg_ai_advice_parse(DOC3, sizeof(DOC3) - 1,
                                        0x11111111u, 7, &doc),
                     VG_AI_ERR_STALE, "boot mismatch");
  fails += expect_rc(vg_ai_advice_parse(DOC3, sizeof(DOC3) - 1,
                                        0x0a1b2c3du, 7, &doc),
                     VG_AI_OK, "matching identity");

  /* Bad boot literal. */

  {
    static const char bad[] = "VGADV1\nboot=zzzzzzzz\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "bad boot hex");
  }

  {
    static const char bad[] = "VGADV1\nboot=0a1b2c3\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "short boot hex");
  }

  /* Upper case is the same stamp.  A model that echoes the request's boot
   * back in capitals must not lose the whole round over it. */

  {
    static const char upper[] = "VGADV1\nboot=0A1B2C3D\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(upper, sizeof(upper) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_OK, "upper case boot hex");
    fails += expect_true(doc.boot == 0x0a1b2c3du, "upper case value");
  }

  return fails;
}

static int test_entry_field_failures(void)
{
  vg_ai_advice_doc_t doc;
  int fails = 0;

  /* Illegal ids. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=a/b\nepoch=1\nsev=warn\n"
      "unres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "id with slash");
  }

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=abcdefghijklmnopqrstuvwx\n"
      "epoch=1\nsev=warn\nunres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "id too long");
  }

  /* Same point twice in one round. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=2\n"
      "[1]\nid=p0\nepoch=1\nsev=warn\nunres=0\nsum=x\nev=y\natt=z\n"
      "[2]\nid=p0\nepoch=2\nsev=crit\nunres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_DUP, "duplicate id");
  }

  /* Bad severity word. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=p0\nepoch=1\nsev=urgent\n"
      "unres=0\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "bad severity");
  }

  /* Bad unres flag. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=p0\nepoch=1\nsev=warn\n"
      "unres=2\nsum=x\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "bad unres");
  }

  /* Empty sum is not an advice. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=p0\nepoch=1\nsev=warn\n"
      "unres=0\nsum=\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_RANGE, "empty sum");
  }

  /* Carriage return anywhere. */

  {
    static const char bad[] =
      "VGADV1\r\nboot=0a1b2c3d\nreq=7\nn=0\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_UTF8, "CR rejected");
  }

  /* Control byte inside a text field. */

  {
    static const char bad[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=p0\nepoch=1\nsev=warn\n"
      "unres=0\nsum=a\x01b\nev=y\natt=z\nEND\n";

    fails += expect_rc(vg_ai_advice_parse(bad, sizeof(bad) - 1,
                                          0x0a1b2c3du, 7, &doc),
                       VG_AI_ERR_UTF8, "control byte rejected");
  }

  return fails;
}

static int test_text_truncation(void)
{
  static char buf[4096];
  vg_ai_advice_doc_t doc;
  size_t len;
  int fails = 0;

  /* 79 ASCII bytes then a 3-byte character: the character cannot fit in the
   * 80-byte cap and must be dropped whole, never split. */

  len = 0;
  len += (size_t)snprintf(buf + len, sizeof(buf) - len,
                          "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n"
                          "[1]\nid=p0\nepoch=1\nsev=warn\nunres=0\nsum=");
  memset(buf + len, 'a', 79);
  len += 79;
  memcpy(buf + len, "\xe4\xb8\xad", 3);
  len += 3;
  len += (size_t)snprintf(buf + len, sizeof(buf) - len, "\nev=y\natt=z\nEND\n");

  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0x0a1b2c3du, 7, &doc),
                     VG_AI_OK, "long sum still parses");
  fails += expect_true(strlen(doc.e[0].sum) == 79, "sum cut to 79 bytes");
  fails += expect_true(vg_ai_utf8_valid(doc.e[0].sum,
                                        strlen(doc.e[0].sum), 0) == 1,
                       "truncated sum stays valid UTF-8");

  /* A value that fits exactly is kept whole. */

  len = 0;
  len += (size_t)snprintf(buf + len, sizeof(buf) - len,
                          "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n"
                          "[1]\nid=p0\nepoch=1\nsev=warn\nunres=0\nsum=");
  memset(buf + len, 'a', VG_AI_SUM_MAX);
  len += VG_AI_SUM_MAX;
  len += (size_t)snprintf(buf + len, sizeof(buf) - len, "\nev=y\natt=z\nEND\n");

  memset(&doc, 0, sizeof(doc));
  fails += expect_rc(vg_ai_advice_parse(buf, len, 0x0a1b2c3du, 7, &doc),
                     VG_AI_OK, "sum at cap parses");
  fails += expect_true(strlen(doc.e[0].sum) == VG_AI_SUM_MAX, "sum kept whole");

  return fails;
}

static int test_request_builder(void)
{
  char req[1536];
  vg_ai_alarm_in_t in[VG_AI_ADV_MAX];
  int fails = 0;
  int n;
  int i;
  int rc;

  memset(in, 0, sizeof(in));

  for (i = 0; i < VG_AI_ADV_MAX; i++)
    {
      in[i].id = "water_1";
      in[i].name = "水浸传感器";
      in[i].epoch = (uint32_t)(i + 1);
      in[i].sev = VG_AI_SEV_CRIT;
      in[i].value = 1.0f;
      in[i].threshold = 0.5f;
      in[i].dur_s = 42;
    }

  rc = vg_ai_advice_build_request(req, sizeof(req), 0x0a1b2c3du, 7, in,
                                  VG_AI_ADV_MAX);
  fails += expect_true(rc > 0, "build request 8 alarms");
  fails += expect_true(rc < (int)sizeof(req), "request fits cap");
  fails += expect_true(strstr(req, "boot=0a1b2c3d") != NULL, "request boot");
  fails += expect_true(strstr(req, "req=7") != NULL, "request req");
  fails += expect_true(strstr(req, "alarm_advice.txt") != NULL,
                       "request output path");
  fails += expect_true(strstr(req, "水浸传感器") != NULL, "request name");
  fails += expect_true(strstr(req, "sev=crit") != NULL, "request sev");
  fails += expect_true(strstr(req, "nan") == NULL, "no nan in request");
  fails += expect_true(strstr(req, "inf") == NULL, "no inf in request");

  /* NaN / Inf must degrade to a dash, not leak through %f. */

  in[0].value = 0.0f / 0.0f;
  in[1].threshold = 1.0f / 0.0f;
  rc = vg_ai_advice_build_request(req, sizeof(req), 0x0a1b2c3du, 7, in, 2);
  fails += expect_true(rc > 0, "build request with non-finite floats");
  fails += expect_true(strstr(req, "nan") == NULL, "NaN as dash");
  fails += expect_true(strstr(req, "inf") == NULL, "Inf as dash");
  fails += expect_true(strstr(req, "值=-") != NULL, "NaN rendered as dash");

  /* Too small a buffer must be reported, not silently cut. */

  rc = vg_ai_advice_build_request(req, 64, 0x0a1b2c3du, 7, in, 2);
  fails += expect_rc(rc, VG_AI_ERR_RANGE, "small cap reported");

  fails += expect_rc(vg_ai_advice_build_request(NULL, 16, 0, 0, in, 1),
                     VG_AI_ERR_ARG, "null out");
  fails += expect_true(
    vg_ai_advice_build_request(req, sizeof(req), 0, 0, NULL, 0) > 0,
    "zero alarms ok");
  fails += expect_rc(vg_ai_advice_build_request(req, sizeof(req), 0, 0, in,
                                                VG_AI_ADV_MAX + 1),
                     VG_AI_ERR_ARG, "too many alarms");

  /* The builder must emit a document the parser accepts back, which is the
   * shape the agent is asked to echo. */

  /* The board's buffer is the same 1536 bytes, and the request also has to
   * hold eight rows with realistic point names.  A header that grows past
   * this makes vg_ai_advice_build_request() reject the whole round, so the
   * worst case is asserted rather than assumed. */

  for (i = 0; i < VG_AI_ADV_MAX; i++)
    {
      in[i].id = "acu_return_temp";
      in[i].name = "回风温度传感器";
    }

  rc = vg_ai_advice_build_request(req, sizeof(req), 0xffffffffu, 999999u,
                                  in, VG_AI_ADV_MAX);
  fails += expect_true(rc > 0, "request with long names builds");
  fails += expect_true(rc < 1536, "request with long names fits the board cap");

  n = 0;
  {
    static const char doc[] =
      "VGADV1\nboot=0a1b2c3d\nreq=7\nn=1\n[1]\nid=water_1\nepoch=1\n"
      "sev=crit\nunres=0\nsum=先确认现场\nev=y\natt=z\nEND\n";
    vg_ai_advice_doc_t parsed;

    memset(&parsed, 0, sizeof(parsed));
    fails += expect_rc(vg_ai_advice_parse(doc, sizeof(doc) - 1,
                                          0x0a1b2c3du, 7, &parsed),
                       VG_AI_OK, "echoed doc parses");
    n = parsed.n;
  }

  fails += expect_true(n == 1, "echoed doc count");

  return fails;
}

static int test_report_validate(void)
{
  static const char good[] =
    "AI-DAILY v1\n"
    "date=2026-09-16\n"
    "source=agent\n"
    "---\n"
    "通信质量：总线正常率 99.2%，超时 3 次。\n"
    "点位在线：6/6 全在线。\n"
    "异常时间线：14:02 水浸恢复。\n";

  static const char no_marker[] =
    "AI-DAILY v2\ndate=2026-09-16\nsource=agent\n---\nbody\n";
  static const char no_source[] =
    "AI-DAILY v1\ndate=2026-09-16\nsource=firmware\n---\nbody\n";
  static const char no_sep[] =
    "AI-DAILY v1\ndate=2026-09-16\nsource=agent\nbody\n";
  static const char no_body[] =
    "AI-DAILY v1\ndate=2026-09-16\nsource=agent\n---\n";
  static const char bad_date[] =
    "AI-DAILY v1\ndate=2026-09-15\nsource=agent\n---\nbody\n";
  int fails = 0;

  fails += expect_rc(vg_ai_report_validate(good, sizeof(good) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_OK, "report ok");
  fails += expect_rc(vg_ai_report_validate(good, sizeof(good) - 1,
                                           "2026-09-16", 1000, 1000),
                     VG_AI_OK, "report fresh ok");
  fails += expect_rc(vg_ai_report_validate(no_marker, sizeof(no_marker) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_ERR_FORMAT, "report marker");
  fails += expect_rc(vg_ai_report_validate(no_source, sizeof(no_source) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_ERR_FORMAT, "report source");
  fails += expect_rc(vg_ai_report_validate(no_sep, sizeof(no_sep) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_ERR_FORMAT, "report separator");
  fails += expect_rc(vg_ai_report_validate(no_body, sizeof(no_body) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_ERR_FORMAT, "report empty body");
  fails += expect_rc(vg_ai_report_validate(bad_date, sizeof(bad_date) - 1,
                                           "2026-09-16", 0, 0),
                     VG_AI_ERR_STALE, "report wrong date");
  fails += expect_rc(vg_ai_report_validate(good, sizeof(good) - 1,
                                           "2026-09-16", 1000, 1000 + 27 * 3600),
                     VG_AI_ERR_STALE, "report too old");
  fails += expect_rc(vg_ai_report_validate(good, sizeof(good) - 1,
                                           "2026-09-16", 1000 + 600, 1000),
                     VG_AI_ERR_RANGE, "report from the future");
  fails += expect_rc(vg_ai_report_validate(good, 0, "2026-09-16", 0, 0),
                     VG_AI_ERR_RANGE, "report len 0");
  fails += expect_rc(vg_ai_report_validate(NULL, 10, "2026-09-16", 0, 0),
                     VG_AI_ERR_ARG, "report null");
  fails += expect_rc(vg_ai_report_validate(good, sizeof(good) - 1,
                                           "2026-9-16", 0, 0),
                     VG_AI_ERR_ARG, "report bad expect_date");

  {
    static char big[VG_AI_REPORT_MAX + 64];

    memset(big, 'a', sizeof(big));
    fails += expect_rc(vg_ai_report_validate(big, sizeof(big),
                                             "2026-09-16", 0, 0),
                       VG_AI_ERR_RANGE, "report over cap");
  }

  return fails;
}

int main(void)
{
  int fails = 0;

  fails += test_ids_and_utf8();
  fails += test_parse_ok();
  fails += test_entry_count();
  fails += test_doc_size_cap();
  fails += test_structure_failures();
  fails += test_boot_req_identity();
  fails += test_entry_field_failures();
  fails += test_text_truncation();
  fails += test_request_builder();
  fails += test_report_validate();

  if (fails)
    {
      fprintf(stderr, "%d fail(s)\n", fails);
      return 1;
    }

  printf("test_ai_contract: ok\n");
  return 0;
}
