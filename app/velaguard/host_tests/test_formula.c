#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vg_formula.h"

static int s_fails;

static void init_spec_len(struct vg_formula_spec *spec, const char *expr,
                          size_t expr_len, unsigned int reg_count)
{
  memset(spec, 0, sizeof(*spec));
  spec->version = VG_FORMULA_LANG_VERSION;
  spec->reg_count = reg_count;
  spec->expr = expr;
  spec->expr_len = expr_len;
}

static void init_spec(struct vg_formula_spec *spec, const char *expr,
                      unsigned int reg_count)
{
  init_spec_len(spec, expr, strlen(expr), reg_count);
}

static int compile_expr(const char *expr, unsigned int reg_count,
                        struct vg_formula_program *program,
                        struct vg_formula_error *error)
{
  struct vg_formula_spec spec;

  init_spec(&spec, expr, reg_count);
  return vg_formula_compile(&spec, program, error);
}

static void expect_ok(const char *expr, unsigned int reg_count,
                      const uint16_t *regs, size_t reg_len,
                      float expected, float tolerance)
{
  struct vg_formula_program program;
  struct vg_formula_error error;
  float value = -9999.0f;
  int status;

  status = compile_expr(expr, reg_count, &program, &error);
  if (status != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL compile %s: status=%d offset=%d\n",
              expr, status, (int)error.offset);
      s_fails++;
      return;
    }

  status = vg_formula_eval(&program, regs, reg_len, &value, &error);
  if (status != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL eval %s: status=%d\n", expr, status);
      s_fails++;
      return;
    }

  if (!(fabsf(value - expected) <= tolerance))
    {
      fprintf(stderr, "FAIL value %s: got %.9g expected %.9g\n",
              expr, (double)value, (double)expected);
      s_fails++;
    }
}

static float eval_value(const char *expr, unsigned int reg_count,
                        const uint16_t *regs, size_t reg_len, int *ok)
{
  struct vg_formula_program program;
  struct vg_formula_error error;
  float value = 0.0f;

  *ok = 0;
  if (compile_expr(expr, reg_count, &program, &error) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL compile %s\n", expr);
      s_fails++;
      return value;
    }

  if (vg_formula_eval(&program, regs, reg_len, &value, &error) !=
      VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL eval %s\n", expr);
      s_fails++;
      return value;
    }

  *ok = 1;
  return value;
}

static void expect_eval_error(const char *expr, unsigned int reg_count,
                              const uint16_t *regs, size_t reg_len,
                              enum vg_formula_status expected)
{
  struct vg_formula_program program;
  struct vg_formula_error error;
  float value = 4242.0f;
  int status;

  status = compile_expr(expr, reg_count, &program, &error);
  if (status != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL compile %s: status=%d offset=%d\n",
              expr, status, (int)error.offset);
      s_fails++;
      return;
    }

  status = vg_formula_eval(&program, regs, reg_len, &value, &error);
  if (status != (int)expected)
    {
      fprintf(stderr, "FAIL eval %s: got status %d expected %d\n",
              expr, status, expected);
      s_fails++;
    }

  if (value != 4242.0f)
    {
      fprintf(stderr, "FAIL eval %s wrote value on failure\n", expr);
      s_fails++;
    }

  if (error.offset != -1)
    {
      fprintf(stderr, "FAIL eval %s offset %d\n", expr, (int)error.offset);
      s_fails++;
    }
}

static void expect_compile_error(const char *expr, unsigned int reg_count,
                                 enum vg_formula_status expected,
                                 int32_t offset)
{
  struct vg_formula_program program;
  struct vg_formula_error error;
  int status;

  status = compile_expr(expr, reg_count, &program, &error);
  if (status != (int)expected)
    {
      fprintf(stderr, "FAIL compile %s: got status %d expected %d\n",
              expr, status, expected);
      s_fails++;
      return;
    }

  if (error.status != expected)
    {
      fprintf(stderr, "FAIL compile %s error.status %d\n",
              expr, error.status);
      s_fails++;
    }

  if (offset >= 0 && error.offset != offset)
    {
      fprintf(stderr, "FAIL compile %s: offset %d expected %d\n",
              expr, (int)error.offset, (int)offset);
      s_fails++;
    }

  if (program.ready || program.insn_count != 0 || program.reg_count != 0)
    {
      fprintf(stderr, "FAIL compile %s left a usable program\n", expr);
      s_fails++;
    }
}

static void expect_f32(const char *expr, uint16_t first, uint16_t second,
                       float expected)
{
  uint16_t regs[2];

  regs[0] = first;
  regs[1] = second;
  expect_ok(expr, 2, regs, 2, expected, 0.0f);
}

static void expect_f32_sign(const char *expr, uint16_t first,
                            uint16_t second, int negative)
{
  uint16_t regs[2];
  float value;
  int ok;

  regs[0] = first;
  regs[1] = second;
  value = eval_value(expr, 2, regs, 2, &ok);
  if (!ok)
    {
      return;
    }

  if (value != 0.0f || (signbit(value) != 0) != (negative != 0))
    {
      fprintf(stderr, "FAIL zero sign %s: %.9g\n", expr, (double)value);
      s_fails++;
    }
}

static void expect_f32_subnormal(void)
{
  uint16_t regs[2] = {0x0000, 0x0001};
  float value;
  int ok;

  value = eval_value("F32_ABCD(R0,R1)", 2, regs, 2, &ok);
  if (!ok)
    {
      return;
    }

  if (!(value != 0.0f && fabsf(value) < FLT_MIN))
    {
      fprintf(stderr, "FAIL subnormal decode: %.9g\n", (double)value);
      s_fails++;
    }
}

static void expect_compile_null_error(void)
{
  struct vg_formula_spec spec;
  struct vg_formula_program program;
  uint16_t regs[1] = {255};
  float value = 0.0f;

  init_spec(&spec, "R0/10", 1);
  if (vg_formula_compile(&spec, &program, NULL) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL compile with NULL error\n");
      s_fails++;
      return;
    }

  if (vg_formula_eval(&program, regs, 1, &value, NULL) != VG_FORMULA_OK ||
      value != 25.5f)
    {
      fprintf(stderr, "FAIL eval with NULL error\n");
      s_fails++;
    }
}

static void expect_explicit_length(void)
{
  static const char text[6] = {'R', '0', '/', '1', '0', 'X'};
  struct vg_formula_spec spec;
  struct vg_formula_program program;
  struct vg_formula_error error;
  uint16_t regs[1] = {255};
  float value = 0.0f;

  memset(&spec, 0, sizeof(spec));
  spec.version = VG_FORMULA_LANG_VERSION;
  spec.reg_count = 1;
  spec.expr = text;
  spec.expr_len = 5;

  if (vg_formula_compile(&spec, &program, &error) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL explicit expr_len compile\n");
      s_fails++;
      return;
    }

  if (vg_formula_eval(&program, regs, 1, &value, &error) != VG_FORMULA_OK ||
      value != 25.5f)
    {
      fprintf(stderr, "FAIL explicit expr_len eval\n");
      s_fails++;
    }
}

static void expect_argument_checks(void)
{
  struct vg_formula_spec spec;
  struct vg_formula_program program;
  struct vg_formula_error error;
  uint16_t regs[1] = {1};
  float value = 0.0f;

  init_spec(&spec, "R0", 1);

  if (vg_formula_compile(NULL, &program, &error) !=
      VG_FORMULA_ERR_ARGUMENT ||
      vg_formula_compile(&spec, NULL, &error) !=
      VG_FORMULA_ERR_ARGUMENT)
    {
      fprintf(stderr, "FAIL compile NULL argument\n");
      s_fails++;
    }

  spec.version = 2;
  if (vg_formula_compile(&spec, &program, &error) !=
      VG_FORMULA_ERR_VERSION)
    {
      fprintf(stderr, "FAIL version check\n");
      s_fails++;
    }

  spec.version = VG_FORMULA_LANG_VERSION;
  spec.reg_count = 0;
  if (vg_formula_compile(&spec, &program, &error) !=
      VG_FORMULA_ERR_ARGUMENT)
    {
      fprintf(stderr, "FAIL reg_count 0\n");
      s_fails++;
    }

  spec.reg_count = 5;
  if (vg_formula_compile(&spec, &program, &error) !=
      VG_FORMULA_ERR_ARGUMENT)
    {
      fprintf(stderr, "FAIL reg_count 5\n");
      s_fails++;
    }

  spec.reg_count = 1;
  if (vg_formula_compile(&spec, &program, &error) != VG_FORMULA_OK ||
      vg_formula_eval(NULL, regs, 1, &value, &error) !=
      VG_FORMULA_ERR_ARGUMENT ||
      vg_formula_eval(&program, NULL, 1, &value, &error) !=
      VG_FORMULA_ERR_ARGUMENT ||
      vg_formula_eval(&program, regs, 1, NULL, &error) !=
      VG_FORMULA_ERR_ARGUMENT)
    {
      fprintf(stderr, "FAIL eval NULL argument\n");
      s_fails++;
    }
}

static void expect_input_mismatch(void)
{
  struct vg_formula_program program;
  struct vg_formula_error error;
  uint16_t regs[4] = {1, 2, 3, 4};
  float value = 4242.0f;

  if (compile_expr("R0+R1", 2, &program, &error) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL input mismatch compile\n");
      s_fails++;
      return;
    }

  if (vg_formula_eval(&program, regs, 1, &value, &error) !=
      VG_FORMULA_ERR_INPUT || value != 4242.0f)
    {
      fprintf(stderr, "FAIL input mismatch\n");
      s_fails++;
    }
}

static void expect_program_rejection(void)
{
  struct vg_formula_program program;
  struct vg_formula_program copy;
  struct vg_formula_error error;
  uint16_t regs[2] = {1, 2};
  float value = 4242.0f;

  if (compile_expr("R0+R1", 2, &program, &error) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL program rejection compile\n");
      s_fails++;
      return;
    }

  copy = program;
  copy.ready = 0;
  if (vg_formula_eval(&copy, regs, 2, &value, &error) !=
      VG_FORMULA_ERR_PROGRAM || value != 4242.0f)
    {
      fprintf(stderr, "FAIL not-ready program\n");
      s_fails++;
    }

  copy = program;
  copy.insns[0].op = 0xff;
  if (vg_formula_eval(&copy, regs, 2, &value, &error) !=
      VG_FORMULA_ERR_PROGRAM || value != 4242.0f)
    {
      fprintf(stderr, "FAIL unknown opcode\n");
      s_fails++;
    }

  copy = program;
  copy.insns[0].a = 5;
  if (vg_formula_eval(&copy, regs, 2, &value, &error) !=
      VG_FORMULA_ERR_PROGRAM || value != 4242.0f)
    {
      fprintf(stderr, "FAIL register index tamper\n");
      s_fails++;
    }

  copy = program;
  copy.reg_mask = 0x08;
  if (vg_formula_eval(&copy, regs, 2, &value, &error) !=
      VG_FORMULA_ERR_PROGRAM || value != 4242.0f)
    {
      fprintf(stderr, "FAIL register mask tamper\n");
      s_fails++;
    }
}

static void expect_metadata(void)
{
  struct vg_formula_program program;
  struct vg_formula_error error;

  if (compile_expr("R0+R1*2", 2, &program, &error) != VG_FORMULA_OK)
    {
      fprintf(stderr, "FAIL metadata compile\n");
      s_fails++;
      return;
    }

  if (program.reg_mask != 0x03 || program.insn_count != 5 ||
      program.max_stack != 3 || program.reg_count != 2)
    {
      fprintf(stderr, "FAIL metadata: mask=%u insns=%u stack=%u regs=%u\n",
              program.reg_mask, program.insn_count, program.max_stack,
              program.reg_count);
      s_fails++;
    }

  if (compile_expr("R1*2", 3, &program, &error) != VG_FORMULA_OK ||
      program.reg_mask != 0x02)
    {
      fprintf(stderr, "FAIL register mask\n");
      s_fails++;
    }
}

static void expect_depth_limit(void)
{
  char expr[32];
  size_t i;
  size_t pos = 0;

  for (i = 0; i < VG_FORMULA_DEPTH_MAX; i++)
    {
      expr[pos++] = '(';
    }

  expr[pos++] = 'R';
  expr[pos++] = '0';
  for (i = 0; i < VG_FORMULA_DEPTH_MAX; i++)
    {
      expr[pos++] = ')';
    }

  expr[pos] = '\0';
  expect_ok(expr, 1, (const uint16_t[]){7}, 1, 7.0f, 0.0f);

  pos = 0;
  for (i = 0; i < VG_FORMULA_DEPTH_MAX + 1; i++)
    {
      expr[pos++] = '(';
    }

  expr[pos++] = 'R';
  expr[pos++] = '0';
  for (i = 0; i < VG_FORMULA_DEPTH_MAX + 1; i++)
    {
      expr[pos++] = ')';
    }

  expr[pos] = '\0';
  expect_compile_error(expr, 1, VG_FORMULA_ERR_TOO_DEEP,
                       (int32_t)VG_FORMULA_DEPTH_MAX);

  pos = 0;
  for (i = 0; i < VG_FORMULA_DEPTH_MAX; i++)
    {
      expr[pos++] = '-';
    }

  expr[pos++] = 'R';
  expr[pos++] = '0';
  expr[pos] = '\0';
  expect_ok(expr, 1, (const uint16_t[]){7}, 1, 7.0f, 0.0f);

  pos = 0;
  for (i = 0; i < VG_FORMULA_DEPTH_MAX + 1; i++)
    {
      expr[pos++] = '-';
    }

  expr[pos++] = 'R';
  expr[pos++] = '0';
  expr[pos] = '\0';
  expect_compile_error(expr, 1, VG_FORMULA_ERR_TOO_DEEP,
                       (int32_t)VG_FORMULA_DEPTH_MAX);
}

static void expect_instruction_limit(void)
{
  char expr[128];
  size_t i;
  size_t pos = 0;
  uint16_t regs[1] = {1};

  for (i = 0; i < 32; i++)
    {
      if (i != 0)
        {
          expr[pos++] = '+';
        }

      expr[pos++] = 'R';
      expr[pos++] = '0';
    }

  expr[pos] = '\0';
  expect_ok(expr, 1, regs, 1, 32.0f, 0.0f);

  pos = 0;
  for (i = 0; i < 33; i++)
    {
      if (i != 0)
        {
          expr[pos++] = '+';
        }

      expr[pos++] = 'R';
      expr[pos++] = '0';
    }

  expr[pos] = '\0';
  expect_compile_error(expr, 1, VG_FORMULA_ERR_TOO_COMPLEX, -1);
}

static void expect_character_checks(void)
{
  static const char nul_text[5] = {'R', '0', '\0', '+', '1'};
  static const char high_text[5] = {'R', '0', (char)0xc3, (char)0x97, '2'};
  struct vg_formula_spec spec;
  struct vg_formula_program program;
  struct vg_formula_error error;
  char long_expr[VG_FORMULA_EXPR_MAX + 2];

  expect_compile_error("R0\n", 1, VG_FORMULA_ERR_CHAR, 2);
  expect_compile_error("R0; R1", 2, VG_FORMULA_ERR_SYNTAX, 2);

  init_spec_len(&spec, nul_text, sizeof(nul_text), 1);
  if (vg_formula_compile(&spec, &program, &error) !=
      VG_FORMULA_ERR_CHAR || error.offset != 2)
    {
      fprintf(stderr, "FAIL embedded NUL\n");
      s_fails++;
    }

  init_spec_len(&spec, high_text, sizeof(high_text), 1);
  if (vg_formula_compile(&spec, &program, &error) !=
      VG_FORMULA_ERR_CHAR || error.offset != 2)
    {
      fprintf(stderr, "FAIL non-ASCII input\n");
      s_fails++;
    }

  memset(long_expr, '1', sizeof(long_expr) - 1);
  long_expr[sizeof(long_expr) - 1] = '\0';
  expect_compile_error(long_expr, 1, VG_FORMULA_ERR_LENGTH, -1);
}

int main(void)
{
  uint16_t regs2[2];
  uint16_t regs3[3];

  expect_f32("F32_ABCD(R0,R1)", 0x41cc, 0x0000, 25.5f);
  expect_f32("F32_BADC(R0,R1)", 0xcc41, 0x0000, 25.5f);
  expect_f32("F32_CDAB(R0,R1)", 0x0000, 0x41cc, 25.5f);
  expect_f32("F32_DCBA(R0,R1)", 0x0000, 0xcc41, 25.5f);

  expect_f32("F32_ABCD(R0,R1)", 0xbfc0, 0x0000, -1.5f);
  expect_f32("F32_BADC(R0,R1)", 0xc0bf, 0x0000, -1.5f);
  expect_f32("F32_CDAB(R0,R1)", 0x0000, 0xbfc0, -1.5f);
  expect_f32("F32_DCBA(R0,R1)", 0x0000, 0xc0bf, -1.5f);

  expect_f32_sign("F32_ABCD(R0,R1)", 0x0000, 0x0000, 0);
  expect_f32_sign("F32_ABCD(R0,R1)", 0x8000, 0x0000, 1);
  expect_f32_subnormal();

  expect_f32("F32_ABCD(R0,R1)", 0x7f7f, 0xffff, FLT_MAX);
  expect_eval_error("F32_ABCD(R0,R1)", 2,
                    (const uint16_t[]){0x7fc0, 0x0001}, 2,
                    VG_FORMULA_ERR_NONFINITE);
  expect_eval_error("F32_ABCD(R0,R1)", 2,
                    (const uint16_t[]){0x7f80, 0x0000}, 2,
                    VG_FORMULA_ERR_NONFINITE);
  expect_eval_error("F32_ABCD(R0,R1)", 2,
                    (const uint16_t[]){0xff80, 0x0000}, 2,
                    VG_FORMULA_ERR_NONFINITE);

  regs2[0] = 255;
  regs2[1] = 0;
  expect_ok("R0/10", 1, regs2, 1, 25.5f, 0.0f);
  expect_ok("r0/10", 1, regs2, 1, 25.5f, 0.0f);
  expect_ok(" \tR0 \t/\t 10 ", 1, regs2, 1, 25.5f, 0.0f);

  regs2[0] = 100;
  expect_ok("R0*0.1+2", 1, regs2, 1, 12.0f, 1e-5f);
  regs2[0] = 0xff9c;
  expect_ok("S16(R0)*0.1", 1, regs2, 1, -10.0f, 1e-5f);
  expect_ok("S16(R0)", 1, (const uint16_t[]){0x8000}, 1, -32768.0f, 0.0f);
  expect_ok("S16(R0)", 1, (const uint16_t[]){0xffff}, 1, -1.0f, 0.0f);
  expect_ok("S16(R0)", 1, (const uint16_t[]){0x7fff}, 1, 32767.0f, 0.0f);
  expect_ok("s16(r0)", 1, (const uint16_t[]){0xffff}, 1, -1.0f, 0.0f);
  expect_ok("R0", 1, (const uint16_t[]){0xffff}, 1, 65535.0f, 0.0f);

  regs2[0] = 100;
  regs2[1] = 50;
  expect_ok("(R0-R1)/10", 2, regs2, 2, 5.0f, 0.0f);
  expect_ok("R0+3*4", 1, regs2, 1, 112.0f, 0.0f);
  expect_ok("(R0+3)*4", 1, regs2, 1, 412.0f, 0.0f);
  expect_ok("-R0+5", 1, regs2, 1, -95.0f, 0.0f);
  expect_ok("2*-R0", 1, regs2, 1, -200.0f, 0.0f);
  expect_ok("+R0", 1, regs2, 1, 100.0f, 0.0f);
  expect_ok("R0*.5", 1, regs2, 1, 50.0f, 0.0f);
  expect_ok("R0*1.", 1, regs2, 1, 100.0f, 0.0f);
  expect_ok("R0*1e-2", 1, regs2, 1, 1.0f, 1e-6f);
  expect_ok("R0*1.0E+2", 1, regs2, 1, 10000.0f, 0.0f);

  regs2[0] = 1;
  regs2[1] = 2;
  expect_ok("R0*65536+R1", 2, regs2, 2, 65538.0f, 0.0f);

  regs2[0] = 0;
  regs2[1] = 21;
  expect_ok("R1*2", 2, regs2, 2, 42.0f, 0.0f);

  regs3[0] = 0;
  regs3[1] = 0x41cc;
  regs3[2] = 0;
  expect_ok("F32_ABCD(R1,R2)", 3, regs3, 3, 25.5f, 0.0f);
  regs3[1] = 0;
  regs3[2] = 0x41cc;
  expect_ok("f32_cdab(r1,r2)", 3, regs3, 3, 25.5f, 0.0f);

  expect_ok("R0/0.000001", 1, (const uint16_t[]){1}, 1,
            1.0f / 0.000001f, 0.5f);
  expect_ok("1/(R0-1)", 1, (const uint16_t[]){2}, 1, 1.0f, 0.0f);
  expect_eval_error("1/(R0-1)", 1, (const uint16_t[]){1}, 1,
                    VG_FORMULA_ERR_DIV_ZERO);
  expect_eval_error("R0/R1", 2, (const uint16_t[]){1, 0}, 2,
                    VG_FORMULA_ERR_DIV_ZERO);
  expect_eval_error("R0/(R1-R1)", 2, (const uint16_t[]){1, 5}, 2,
                    VG_FORMULA_ERR_DIV_ZERO);
  expect_eval_error("R0*1e-30*1e-30", 1, (const uint16_t[]){1}, 1,
                    VG_FORMULA_ERR_UNDERFLOW);
  expect_eval_error("R0*1e38*1e38", 1, (const uint16_t[]){1}, 1,
                    VG_FORMULA_ERR_NONFINITE);

  expect_compile_error("", 1, VG_FORMULA_ERR_LENGTH, -1);
  expect_compile_error("R0+", 1, VG_FORMULA_ERR_SYNTAX, 3);
  expect_compile_error("R0)", 1, VG_FORMULA_ERR_SYNTAX, 2);
  expect_compile_error("R0 R1", 2, VG_FORMULA_ERR_SYNTAX, 3);
  expect_compile_error("R4", 4, VG_FORMULA_ERR_REGISTER, 0);
  expect_compile_error("R3", 2, VG_FORMULA_ERR_REGISTER, 0);
  expect_compile_error("S16(R4)", 4, VG_FORMULA_ERR_REGISTER, 4);
  expect_compile_error("S16(1)", 1, VG_FORMULA_ERR_ARG_KIND, 4);
  expect_compile_error("S16(R0*1)", 1, VG_FORMULA_ERR_ARG_KIND, 6);
  expect_compile_error("S16(R0,R1)", 2, VG_FORMULA_ERR_ARG_COUNT, 6);
  expect_compile_error("F32_ABCD(R0)", 2, VG_FORMULA_ERR_ARG_COUNT, 11);
  expect_compile_error("F32_ABCD(R0,R2)", 3,
                       VG_FORMULA_ERR_NOT_ADJACENT, 12);
  expect_compile_error("F32_ABCD(R1,R0)", 2,
                       VG_FORMULA_ERR_NOT_ADJACENT, 12);
  expect_compile_error("F32_ABCD(R0,R1,R2)", 3,
                       VG_FORMULA_ERR_ARG_COUNT, 14);
  expect_compile_error("FOO(R0)", 1, VG_FORMULA_ERR_UNKNOWN_NAME, 0);
  expect_compile_error("X", 1, VG_FORMULA_ERR_UNKNOWN_NAME, 0);
  expect_compile_error("R0/0", 1, VG_FORMULA_ERR_DIV_ZERO, 2);
  expect_compile_error("R0/(1-1)", 1, VG_FORMULA_ERR_DIV_ZERO, 2);
  expect_compile_error("1/0+R0", 1, VG_FORMULA_ERR_DIV_ZERO, 1);
  expect_compile_error("1+2", 1, VG_FORMULA_ERR_NO_REGISTER, 0);
  expect_compile_error("0x10+R0", 1, VG_FORMULA_ERR_SYNTAX, 1);
  expect_compile_error("R0*1e", 1, VG_FORMULA_ERR_NUMBER, 3);
  expect_compile_error("R0*1e999", 1, VG_FORMULA_ERR_NUMBER, 3);
  expect_compile_error("R0*1e-999", 1, VG_FORMULA_ERR_NUMBER, 3);
  expect_compile_error("R0*(1e38*1e38)", 1, VG_FORMULA_ERR_NONFINITE, -1);
  expect_compile_error("R0*(1e-30*1e-30)", 1,
                       VG_FORMULA_ERR_UNDERFLOW, -1);

  expect_metadata();
  expect_depth_limit();
  expect_instruction_limit();
  expect_character_checks();
  expect_argument_checks();
  expect_input_mismatch();
  expect_program_rejection();
  expect_compile_null_error();
  expect_explicit_length();

  if (s_fails != 0)
    {
      fprintf(stderr, "%d formula test(s) failed\n", s_fails);
      return 1;
    }

  printf("formula tests passed\n");
  return 0;
}
