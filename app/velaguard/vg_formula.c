/****************************************************************************
 * app/velaguard/vg_formula.c
 *
 * Restricted register formula compiler and evaluator.
 ****************************************************************************/

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "vg_formula.h"

_Static_assert(CHAR_BIT == 8, "vg_formula requires 8-bit bytes");
_Static_assert(sizeof(float) == 4, "vg_formula requires 32-bit float");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "vg_formula requires IEEE 754 binary32");

#define VG_FORMULA_NUMBER_MAX 31u

enum vg_formula_op
{
  VG_FOP_CONST = 1,
  VG_FOP_REG,
  VG_FOP_S16,
  VG_FOP_F32,
  VG_FOP_NEG,
  VG_FOP_ADD,
  VG_FOP_SUB,
  VG_FOP_MUL,
  VG_FOP_DIV
};

struct vg_expr_value
{
  bool is_const;
  float value;
};

struct vg_parser
{
  const char *expr;
  size_t len;
  size_t pos;
  unsigned int reg_count;
  unsigned int depth;
  unsigned int insn_count;
  unsigned int stack;
  unsigned int max_stack;
  unsigned int reg_mask;
  struct vg_formula_program *program;
  struct vg_formula_error *error;
  enum vg_formula_status status;
};

static bool parse_expr(struct vg_parser *p, struct vg_expr_value *out);

static bool ascii_digit(char c)
{
  return c >= '0' && c <= '9';
}

static bool ascii_alpha(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool ascii_ident(char c)
{
  return ascii_alpha(c) || ascii_digit(c) || c == '_';
}

static char ascii_lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool ident_equals(const char *s, size_t len, const char *literal)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      if (literal[i] == '\0' ||
          ascii_lower(s[i]) != ascii_lower(literal[i]))
        {
          return false;
        }
    }

  return literal[len] == '\0';
}

static bool ident_register(const char *s, size_t len, unsigned int *index)
{
  if (len != 2 || (s[0] != 'R' && s[0] != 'r') || !ascii_digit(s[1]))
    {
      return false;
    }

  *index = (unsigned int)(s[1] - '0');
  return true;
}

static bool formula_fail(struct vg_parser *p,
                         enum vg_formula_status status, int32_t offset)
{
  if (p->status == VG_FORMULA_OK)
    {
      p->status = status;
      if (p->error != NULL)
        {
          p->error->status = status;
          p->error->offset = offset;
        }
    }

  return false;
}

static void skip_space(struct vg_parser *p)
{
  while (p->pos < p->len &&
         (p->expr[p->pos] == ' ' || p->expr[p->pos] == '\t'))
    {
      p->pos++;
    }
}

static char peek_char(const struct vg_parser *p)
{
  return p->pos < p->len ? p->expr[p->pos] : '\0';
}

static bool expect_char(struct vg_parser *p, char c,
                        enum vg_formula_status missing)
{
  skip_space(p);
  if (peek_char(p) != c)
    {
      return formula_fail(p, missing, (int32_t)p->pos);
    }

  p->pos++;
  return true;
}

static bool enter_depth(struct vg_parser *p)
{
  if (p->depth >= VG_FORMULA_DEPTH_MAX)
    {
      return formula_fail(p, VG_FORMULA_ERR_TOO_DEEP, (int32_t)p->pos);
    }

  p->depth++;
  return true;
}

static bool emit(struct vg_parser *p, uint8_t op, uint8_t a, uint8_t b,
                 float imm)
{
  struct vg_formula_insn *insn;
  int delta;

  if (p->insn_count >= VG_FORMULA_INSNS_MAX)
    {
      return formula_fail(p, VG_FORMULA_ERR_TOO_COMPLEX, (int32_t)p->pos);
    }

  insn = &p->program->insns[p->insn_count++];
  memset(insn, 0, sizeof(*insn));
  insn->op = op;
  insn->a = a;
  insn->b = b;
  insn->imm.f = imm;

  switch (op)
    {
      case VG_FOP_CONST:
      case VG_FOP_REG:
      case VG_FOP_S16:
      case VG_FOP_F32:
        delta = 1;
        break;

      case VG_FOP_NEG:
        delta = 0;
        break;

      case VG_FOP_ADD:
      case VG_FOP_SUB:
      case VG_FOP_MUL:
      case VG_FOP_DIV:
        delta = -1;
        break;

      default:
        return formula_fail(p, VG_FORMULA_ERR_PROGRAM, -1);
    }

  if (delta > 0)
    {
      p->stack++;
      if (p->stack > p->max_stack)
        {
          p->max_stack = p->stack;
        }

      if (p->stack > VG_FORMULA_STACK_MAX)
        {
          return formula_fail(p, VG_FORMULA_ERR_TOO_COMPLEX,
                              (int32_t)p->pos);
        }
    }
  else if (delta < 0)
    {
      if (p->stack < 2)
        {
          return formula_fail(p, VG_FORMULA_ERR_PROGRAM, -1);
        }

      p->stack--;
    }
  else if (p->stack < 1)
    {
      return formula_fail(p, VG_FORMULA_ERR_PROGRAM, -1);
    }

  return true;
}

static enum vg_formula_status calc_binary(uint8_t op, float a, float b,
                                          float *out)
{
  float result;

  switch (op)
    {
      case VG_FOP_ADD:
        result = a + b;
        break;

      case VG_FOP_SUB:
        result = a - b;
        break;

      case VG_FOP_MUL:
        result = a * b;
        break;

      case VG_FOP_DIV:
        if (b == 0.0f)
          {
            return VG_FORMULA_ERR_DIV_ZERO;
          }

        result = a / b;
        break;

      default:
        return VG_FORMULA_ERR_PROGRAM;
    }

  if (!isfinite(result))
    {
      return VG_FORMULA_ERR_NONFINITE;
    }

  if ((op == VG_FOP_MUL || op == VG_FOP_DIV) &&
      result == 0.0f && a != 0.0f && b != 0.0f)
    {
      return VG_FORMULA_ERR_UNDERFLOW;
    }

  *out = result;
  return VG_FORMULA_OK;
}

static bool parse_number(struct vg_parser *p, struct vg_expr_value *out)
{
  size_t start = p->pos;
  size_t digits_before = 0;
  size_t digits_after = 0;
  size_t token_len;
  bool significand_nonzero = false;
  char buffer[VG_FORMULA_NUMBER_MAX + 1];
  char *end;
  float value;

  while (p->pos < p->len && ascii_digit(p->expr[p->pos]))
    {
      if (p->expr[p->pos] != '0')
        {
          significand_nonzero = true;
        }

      p->pos++;
      digits_before++;
    }

  if (p->pos < p->len && p->expr[p->pos] == '.')
    {
      p->pos++;
      while (p->pos < p->len && ascii_digit(p->expr[p->pos]))
        {
          if (p->expr[p->pos] != '0')
            {
              significand_nonzero = true;
            }

          p->pos++;
          digits_after++;
        }
    }

  if (digits_before + digits_after == 0)
    {
      return formula_fail(p, VG_FORMULA_ERR_NUMBER, (int32_t)start);
    }

  if (p->pos < p->len &&
      (p->expr[p->pos] == 'e' || p->expr[p->pos] == 'E'))
    {
      size_t exponent_digits = 0;

      p->pos++;
      if (p->pos < p->len &&
          (p->expr[p->pos] == '+' || p->expr[p->pos] == '-'))
        {
          p->pos++;
        }

      while (p->pos < p->len && ascii_digit(p->expr[p->pos]))
        {
          p->pos++;
          exponent_digits++;
        }

      if (exponent_digits == 0)
        {
          return formula_fail(p, VG_FORMULA_ERR_NUMBER, (int32_t)start);
        }
    }

  token_len = p->pos - start;
  if (token_len > VG_FORMULA_NUMBER_MAX)
    {
      return formula_fail(p, VG_FORMULA_ERR_NUMBER, (int32_t)start);
    }

  memcpy(buffer, p->expr + start, token_len);
  buffer[token_len] = '\0';

  value = strtof(buffer, &end);
  if (end != buffer + token_len || !isfinite(value))
    {
      return formula_fail(p, VG_FORMULA_ERR_NUMBER, (int32_t)start);
    }

  if (value == 0.0f && significand_nonzero)
    {
      return formula_fail(p, VG_FORMULA_ERR_NUMBER, (int32_t)start);
    }

  if (!emit(p, VG_FOP_CONST, 0, 0, value))
    {
      return false;
    }

  out->is_const = true;
  out->value = value;
  return true;
}

static bool parse_register(struct vg_parser *p, unsigned int *index,
                           size_t *start_out)
{
  size_t start;
  size_t len;

  skip_space(p);
  start = p->pos;
  if (!ascii_alpha(peek_char(p)))
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_KIND, (int32_t)start);
    }

  while (p->pos < p->len && ascii_ident(p->expr[p->pos]))
    {
      p->pos++;
    }

  len = p->pos - start;
  if (!ident_register(p->expr + start, len, index))
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_KIND, (int32_t)start);
    }

  if (*index >= p->reg_count)
    {
      return formula_fail(p, VG_FORMULA_ERR_REGISTER, (int32_t)start);
    }

  p->reg_mask |= 1u << *index;
  if (start_out != NULL)
    {
      *start_out = start;
    }

  return true;
}

static bool parse_s16(struct vg_parser *p, struct vg_expr_value *out)
{
  unsigned int index;

  if (!expect_char(p, '(', VG_FORMULA_ERR_SYNTAX) ||
      !parse_register(p, &index, NULL))
    {
      return false;
    }

  skip_space(p);
  if (peek_char(p) == ',')
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_COUNT, (int32_t)p->pos);
    }

  if (peek_char(p) == '\0')
    {
      return formula_fail(p, VG_FORMULA_ERR_SYNTAX, (int32_t)p->pos);
    }

  if (peek_char(p) != ')')
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_KIND, (int32_t)p->pos);
    }

  p->pos++;
  if (!emit(p, VG_FOP_S16, (uint8_t)index, 0, 0.0f))
    {
      return false;
    }

  out->is_const = false;
  return true;
}

static bool f32_order_name(const char *s, size_t len, unsigned int *order)
{
  static const char *const names[] =
    {
      "F32_ABCD", "F32_BADC", "F32_CDAB", "F32_DCBA"
    };

  unsigned int i;

  for (i = 0; i < 4; i++)
    {
      if (ident_equals(s, len, names[i]))
        {
          *order = i;
          return true;
        }
    }

  return false;
}

static bool parse_f32(struct vg_parser *p, unsigned int order,
                      struct vg_expr_value *out)
{
  unsigned int first;
  unsigned int second;
  size_t second_at = 0;

  if (!expect_char(p, '(', VG_FORMULA_ERR_SYNTAX) ||
      !parse_register(p, &first, NULL))
    {
      return false;
    }

  skip_space(p);
  if (peek_char(p) == ',')
    {
      p->pos++;
    }
  else if (peek_char(p) == '\0')
    {
      return formula_fail(p, VG_FORMULA_ERR_SYNTAX, (int32_t)p->pos);
    }
  else
    {
      return formula_fail(p, peek_char(p) == ')' ?
                          VG_FORMULA_ERR_ARG_COUNT :
                          VG_FORMULA_ERR_ARG_KIND,
                          (int32_t)p->pos);
    }

  if (!parse_register(p, &second, &second_at))
    {
      return false;
    }

  skip_space(p);
  if (peek_char(p) == ',')
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_COUNT, (int32_t)p->pos);
    }

  if (peek_char(p) == '\0')
    {
      return formula_fail(p, VG_FORMULA_ERR_SYNTAX, (int32_t)p->pos);
    }

  if (peek_char(p) != ')')
    {
      return formula_fail(p, VG_FORMULA_ERR_ARG_KIND, (int32_t)p->pos);
    }

  p->pos++;

  if (second != first + 1u)
    {
      return formula_fail(p, VG_FORMULA_ERR_NOT_ADJACENT,
                          (int32_t)second_at);
    }

  if (!emit(p, VG_FOP_F32, (uint8_t)order, (uint8_t)first, 0.0f))
    {
      return false;
    }

  out->is_const = false;
  return true;
}

static bool parse_primary(struct vg_parser *p, struct vg_expr_value *out)
{
  size_t start;
  size_t len;
  unsigned int index;
  unsigned int order;
  bool ok;

  skip_space(p);
  if (p->pos >= p->len)
    {
      return formula_fail(p, VG_FORMULA_ERR_SYNTAX, (int32_t)p->pos);
    }

  if (peek_char(p) == '(')
    {
      if (!enter_depth(p))
        {
          return false;
        }

      p->pos++;
      ok = parse_expr(p, out);
      if (ok)
        {
          skip_space(p);
          if (peek_char(p) != ')')
            {
              ok = formula_fail(p, VG_FORMULA_ERR_SYNTAX,
                                (int32_t)p->pos);
            }
          else
            {
              p->pos++;
            }
        }

      p->depth--;
      return ok;
    }

  if (ascii_digit(peek_char(p)) || peek_char(p) == '.')
    {
      return parse_number(p, out);
    }

  if (!ascii_alpha(peek_char(p)))
    {
      return formula_fail(p, VG_FORMULA_ERR_SYNTAX, (int32_t)p->pos);
    }

  start = p->pos;
  while (p->pos < p->len && ascii_ident(p->expr[p->pos]))
    {
      p->pos++;
    }

  len = p->pos - start;

  if (ident_register(p->expr + start, len, &index))
    {
      if (index >= p->reg_count)
        {
          return formula_fail(p, VG_FORMULA_ERR_REGISTER, (int32_t)start);
        }

      p->reg_mask |= 1u << index;
      if (!emit(p, VG_FOP_REG, (uint8_t)index, 0, 0.0f))
        {
          return false;
        }

      out->is_const = false;
      return true;
    }

  if (ident_equals(p->expr + start, len, "S16"))
    {
      return parse_s16(p, out);
    }

  if (f32_order_name(p->expr + start, len, &order))
    {
      return parse_f32(p, order, out);
    }

  return formula_fail(p, VG_FORMULA_ERR_UNKNOWN_NAME, (int32_t)start);
}

static bool parse_unary(struct vg_parser *p, struct vg_expr_value *out)
{
  bool negate;
  bool ok;

  skip_space(p);
  if (peek_char(p) != '+' && peek_char(p) != '-')
    {
      return parse_primary(p, out);
    }

  negate = peek_char(p) == '-';
  if (!enter_depth(p))
    {
      return false;
    }

  p->pos++;
  ok = parse_unary(p, out);
  p->depth--;
  if (!ok)
    {
      return false;
    }

  if (negate)
    {
      if (out->is_const)
        {
          out->value = -out->value;
        }

      if (!emit(p, VG_FOP_NEG, 0, 0, 0.0f))
        {
          return false;
        }
    }

  return true;
}

static bool parse_product(struct vg_parser *p, struct vg_expr_value *out)
{
  if (!parse_unary(p, out))
    {
      return false;
    }

  for (;;)
    {
      struct vg_expr_value rhs;
      enum vg_formula_status status;
      size_t op_pos;
      char op;

      skip_space(p);
      op = peek_char(p);
      if (op != '*' && op != '/')
        {
          break;
        }

      op_pos = p->pos;
      p->pos++;
      if (!parse_unary(p, &rhs))
        {
          return false;
        }

      if (op == '/' && rhs.is_const && rhs.value == 0.0f)
        {
          return formula_fail(p, VG_FORMULA_ERR_DIV_ZERO,
                              (int32_t)op_pos);
        }

      if (out->is_const && rhs.is_const)
        {
          status = calc_binary(op == '*' ? VG_FOP_MUL : VG_FOP_DIV,
                               out->value, rhs.value, &out->value);
          if (status != VG_FORMULA_OK)
            {
              return formula_fail(p, status, (int32_t)op_pos);
            }
        }
      else
        {
          out->is_const = false;
        }

      if (!emit(p, op == '*' ? VG_FOP_MUL : VG_FOP_DIV, 0, 0, 0.0f))
        {
          return false;
        }
    }

  return true;
}

static bool parse_expr(struct vg_parser *p, struct vg_expr_value *out)
{
  if (!parse_product(p, out))
    {
      return false;
    }

  for (;;)
    {
      struct vg_expr_value rhs;
      enum vg_formula_status status;
      size_t op_pos;
      char op;

      skip_space(p);
      op = peek_char(p);
      if (op != '+' && op != '-')
        {
          break;
        }

      op_pos = p->pos;
      p->pos++;
      if (!parse_product(p, &rhs))
        {
          return false;
        }

      if (out->is_const && rhs.is_const)
        {
          status = calc_binary(op == '+' ? VG_FOP_ADD : VG_FOP_SUB,
                               out->value, rhs.value, &out->value);
          if (status != VG_FORMULA_OK)
            {
              return formula_fail(p, status, (int32_t)op_pos);
            }
        }
      else
        {
          out->is_const = false;
        }

      if (!emit(p, op == '+' ? VG_FOP_ADD : VG_FOP_SUB, 0, 0, 0.0f))
        {
          return false;
        }
    }

  return true;
}

static int compile_fail(struct vg_formula_error *error,
                        enum vg_formula_status status, int32_t offset)
{
  if (error != NULL)
    {
      error->status = status;
      error->offset = offset;
    }

  return status;
}

int vg_formula_compile(const struct vg_formula_spec *spec,
                       struct vg_formula_program *program,
                       struct vg_formula_error *error)
{
  struct vg_parser parser;
  struct vg_expr_value value;
  size_t i;

  if (error != NULL)
    {
      error->status = VG_FORMULA_OK;
      error->offset = -1;
    }

  if (program == NULL)
    {
      return compile_fail(error, VG_FORMULA_ERR_ARGUMENT, -1);
    }

  memset(program, 0, sizeof(*program));

  if (spec == NULL || spec->expr == NULL)
    {
      return compile_fail(error, VG_FORMULA_ERR_ARGUMENT, -1);
    }

  if (spec->version != VG_FORMULA_LANG_VERSION)
    {
      return compile_fail(error, VG_FORMULA_ERR_VERSION, -1);
    }

  if (spec->expr_len == 0 || spec->expr_len > VG_FORMULA_EXPR_MAX)
    {
      return compile_fail(error, VG_FORMULA_ERR_LENGTH, -1);
    }

  if (spec->reg_count == 0 || spec->reg_count > VG_FORMULA_REG_MAX)
    {
      return compile_fail(error, VG_FORMULA_ERR_ARGUMENT, -1);
    }

  for (i = 0; i < spec->expr_len; i++)
    {
      unsigned char c = (unsigned char)spec->expr[i];

      if (c != '\t' && (c < 0x20 || c > 0x7e))
        {
          return compile_fail(error, VG_FORMULA_ERR_CHAR, (int32_t)i);
        }
    }

  memset(&parser, 0, sizeof(parser));
  parser.expr = spec->expr;
  parser.len = spec->expr_len;
  parser.reg_count = spec->reg_count;
  parser.program = program;
  parser.error = error;
  parser.status = VG_FORMULA_OK;

  if (!parse_expr(&parser, &value))
    {
      memset(program, 0, sizeof(*program));
      return parser.status;
    }

  skip_space(&parser);
  if (parser.pos != parser.len)
    {
      formula_fail(&parser, VG_FORMULA_ERR_SYNTAX, (int32_t)parser.pos);
      memset(program, 0, sizeof(*program));
      return parser.status;
    }

  if (parser.reg_mask == 0)
    {
      formula_fail(&parser, VG_FORMULA_ERR_NO_REGISTER, 0);
      memset(program, 0, sizeof(*program));
      return parser.status;
    }

  if (parser.stack != 1)
    {
      formula_fail(&parser, VG_FORMULA_ERR_PROGRAM, -1);
      memset(program, 0, sizeof(*program));
      return parser.status;
    }

  program->ready = 1;
  program->version = VG_FORMULA_LANG_VERSION;
  program->reg_count = (uint8_t)spec->reg_count;
  program->reg_mask = (uint8_t)parser.reg_mask;
  program->insn_count = (uint8_t)parser.insn_count;
  program->max_stack = (uint8_t)parser.max_stack;
  return VG_FORMULA_OK;
}

static uint16_t swap_bytes16(uint16_t value)
{
  return (uint16_t)((uint16_t)(value << 8) | (uint16_t)(value >> 8));
}

static bool decode_f32(unsigned int order, uint16_t first, uint16_t second,
                       float *out)
{
  uint32_t bits;

  switch (order)
    {
      case VG_FORMULA_F32_ABCD:
        bits = ((uint32_t)first << 16) | second;
        break;

      case VG_FORMULA_F32_BADC:
        bits = ((uint32_t)swap_bytes16(first) << 16) |
               swap_bytes16(second);
        break;

      case VG_FORMULA_F32_CDAB:
        bits = ((uint32_t)second << 16) | first;
        break;

      case VG_FORMULA_F32_DCBA:
        bits = ((uint32_t)swap_bytes16(second) << 16) |
               swap_bytes16(first);
        break;

      default:
        return false;
    }

  memcpy(out, &bits, sizeof(bits));
  return true;
}

static bool push_value(float *stack, unsigned int *sp, float value)
{
  if (*sp >= VG_FORMULA_STACK_MAX)
    {
      return false;
    }

  stack[(*sp)++] = value;
  return true;
}

static int eval_fail(struct vg_formula_error *error,
                     enum vg_formula_status status)
{
  if (error != NULL)
    {
      error->status = status;
      error->offset = -1;
    }

  return status;
}

int vg_formula_eval(const struct vg_formula_program *program,
                    const uint16_t *regs, size_t reg_count,
                    float *value, struct vg_formula_error *error)
{
  float stack[VG_FORMULA_STACK_MAX];
  unsigned int sp = 0;
  unsigned int i;

  if (error != NULL)
    {
      error->status = VG_FORMULA_OK;
      error->offset = -1;
    }

  if (program == NULL || regs == NULL || value == NULL)
    {
      return eval_fail(error, VG_FORMULA_ERR_ARGUMENT);
    }

  if (!program->ready ||
      program->version != VG_FORMULA_LANG_VERSION ||
      program->insn_count == 0 ||
      program->insn_count > VG_FORMULA_INSNS_MAX ||
      program->reg_count == 0 ||
      program->reg_count > VG_FORMULA_REG_MAX ||
      program->max_stack == 0 ||
      program->max_stack > VG_FORMULA_STACK_MAX ||
      program->reg_mask == 0 ||
      (program->reg_mask & ~((1u << program->reg_count) - 1u)) != 0)
    {
      return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
    }

  if (reg_count != program->reg_count)
    {
      return eval_fail(error, VG_FORMULA_ERR_INPUT);
    }

  for (i = 0; i < program->insn_count; i++)
    {
      const struct vg_formula_insn *insn = &program->insns[i];
      enum vg_formula_status status;
      float value_a;
      float value_b;
      uint16_t raw;

      switch (insn->op)
        {
          case VG_FOP_CONST:
            if (!isfinite(insn->imm.f) ||
                !push_value(stack, &sp, insn->imm.f))
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }
            break;

          case VG_FOP_REG:
            if (insn->a >= program->reg_count ||
                !push_value(stack, &sp, (float)regs[insn->a]))
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }
            break;

          case VG_FOP_S16:
            if (insn->a >= program->reg_count)
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }

            raw = regs[insn->a];
            value_a = (float)((raw & 0x8000u) ?
                              (int32_t)raw - 65536 : (int32_t)raw);
            if (!push_value(stack, &sp, value_a))
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }
            break;

          case VG_FOP_F32:
            if (insn->b + 1u >= program->reg_count ||
                !decode_f32(insn->a, regs[insn->b],
                            regs[insn->b + 1u], &value_a))
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }

            if (!isfinite(value_a))
              {
                return eval_fail(error, VG_FORMULA_ERR_NONFINITE);
              }

            if (!push_value(stack, &sp, value_a))
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }
            break;

          case VG_FOP_NEG:
            if (sp < 1)
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }

            stack[sp - 1] = -stack[sp - 1];
            break;

          case VG_FOP_ADD:
          case VG_FOP_SUB:
          case VG_FOP_MUL:
          case VG_FOP_DIV:
            if (sp < 2)
              {
                return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
              }

            value_b = stack[--sp];
            value_a = stack[sp - 1];
            status = calc_binary(insn->op, value_a, value_b,
                                 &stack[sp - 1]);
            if (status != VG_FORMULA_OK)
              {
                return eval_fail(error, status);
              }
            break;

          default:
            return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
        }
    }

  if (sp != 1)
    {
      return eval_fail(error, VG_FORMULA_ERR_PROGRAM);
    }

  if (!isfinite(stack[0]))
    {
      return eval_fail(error, VG_FORMULA_ERR_NONFINITE);
    }

  *value = stack[0];
  return VG_FORMULA_OK;
}
