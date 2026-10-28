/****************************************************************************
 * Restricted register formula compiler and evaluator (no I/O).
 *
 * Language v1:
 *   numbers    decimal integer, fraction or scientific notation
 *              (at most 31 source bytes per literal)
 *   registers  R0..R3, relative to the sampled block
 *   operators  + - * /, unary +/-, parentheses
 *   functions  S16(Rn), F32_ABCD/BADC/CDAB/DCBA(Rn,Rn+1)
 *
 * At least one register must be referenced; constant-only expressions are
 * rejected even when they would evaluate successfully.
 *
 * Compilation turns source text into a bounded instruction program owned by
 * the caller.  Evaluation runs a fixed stack and never parses text.  Source
 * offsets are 0-based byte positions; runtime errors use offset -1.
 ****************************************************************************/

#ifndef __APP_VELAGUARD_VG_FORMULA_H
#define __APP_VELAGUARD_VG_FORMULA_H

#include <stddef.h>
#include <stdint.h>

#define VG_FORMULA_LANG_VERSION 1u
#define VG_FORMULA_EXPR_MAX     127u
#define VG_FORMULA_REG_MAX      4u
#define VG_FORMULA_INSNS_MAX    64u
#define VG_FORMULA_STACK_MAX    16u
#define VG_FORMULA_DEPTH_MAX    8u

enum vg_formula_status
{
  VG_FORMULA_OK = 0,

  VG_FORMULA_ERR_ARGUMENT = 1,
  VG_FORMULA_ERR_VERSION,
  VG_FORMULA_ERR_LENGTH,
  VG_FORMULA_ERR_CHAR,
  VG_FORMULA_ERR_NUMBER,
  VG_FORMULA_ERR_SYNTAX,
  VG_FORMULA_ERR_UNKNOWN_NAME,
  VG_FORMULA_ERR_REGISTER,
  VG_FORMULA_ERR_ARG_COUNT,
  VG_FORMULA_ERR_ARG_KIND,
  VG_FORMULA_ERR_NOT_ADJACENT,
  VG_FORMULA_ERR_TOO_DEEP,
  VG_FORMULA_ERR_TOO_COMPLEX,
  VG_FORMULA_ERR_NO_REGISTER,
  VG_FORMULA_ERR_PROGRAM,
  VG_FORMULA_ERR_INPUT,
  VG_FORMULA_ERR_DIV_ZERO,
  VG_FORMULA_ERR_NONFINITE,
  VG_FORMULA_ERR_UNDERFLOW
};

enum vg_formula_f32_order
{
  VG_FORMULA_F32_ABCD = 0,
  VG_FORMULA_F32_BADC,
  VG_FORMULA_F32_CDAB,
  VG_FORMULA_F32_DCBA
};

struct vg_formula_error
{
  enum vg_formula_status status;
  int32_t offset;
};

struct vg_formula_spec
{
  unsigned int version;
  unsigned int reg_count;  /* 1..4 registers sampled from reg upward */
  const char *expr;
  size_t expr_len;         /* bytes, excluding any NUL */
};

/* Instruction encoding is private to vg_formula.c.  Callers may inspect the
 * metadata below but must treat the program as read-only after compilation.
 */

struct vg_formula_insn
{
  uint8_t op;
  uint8_t a;
  uint8_t b;
  uint8_t reserved;
  union
  {
    float f;
    uint32_t u;
  } imm;
};

struct vg_formula_program
{
  uint8_t ready;
  uint8_t version;
  uint8_t reg_count;
  uint8_t reg_mask;
  uint8_t insn_count;
  uint8_t max_stack;
  uint16_t reserved;
  struct vg_formula_insn insns[VG_FORMULA_INSNS_MAX];
};

/* Returns VG_FORMULA_OK or an error code.  On failure the program is left
 * not ready.  The error argument may be NULL. */
int vg_formula_compile(const struct vg_formula_spec *spec,
                       struct vg_formula_program *program,
                       struct vg_formula_error *error);

/* Evaluates a compiled program against exactly reg_count registers.  On
 * failure *value is not written.  The error argument may be NULL. */
int vg_formula_eval(const struct vg_formula_program *program,
                    const uint16_t *regs, size_t reg_count,
                    float *value, struct vg_formula_error *error);

#endif
