/****************************************************************************
 * app/velaguard/vg_fw_identity.c
 *
 * The build script writes vg_fw_build.h from the repository VERSION file.
 * The linker script marks the image slot as absolute symbols so the same
 * objects can be linked for either slot without recompiling.
 ****************************************************************************/

#include "vg_fw_identity.h"

#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include("vg_fw_build.h")
#    include "vg_fw_build.h"
#  endif
#endif

#ifndef VG_FW_VERSION
#  define VG_FW_VERSION "0.0.0-dev"
#endif

#ifndef VG_FW_BUILD_ID
#  define VG_FW_BUILD_ID "unknown"
#endif

#ifndef VG_FW_TEST_SLOT_MARKER
extern const char __vg_slot_letter;
#endif

#ifndef VG_FW_TEST_LINK_BASE
extern const char __vg_link_base;
#endif

static uintptr_t slot_marker(void)
{
#ifdef VG_FW_TEST_SLOT_MARKER
  return (uintptr_t)VG_FW_TEST_SLOT_MARKER;
#else
  return (uintptr_t)&__vg_slot_letter;
#endif
}

static uintptr_t link_base(void)
{
#ifdef VG_FW_TEST_LINK_BASE
  return (uintptr_t)VG_FW_TEST_LINK_BASE;
#else
  return (uintptr_t)&__vg_link_base;
#endif
}

const char *vg_fw_version(void)
{
  return VG_FW_VERSION;
}

const char *vg_fw_build_id(void)
{
  return VG_FW_BUILD_ID;
}

char vg_fw_slot_from_marker(uintptr_t marker, uintptr_t base)
{
  if (marker == (uintptr_t)'A' || marker == (uintptr_t)'B')
    {
      return (char)marker;
    }

  if (base >= VG_FW_SLOT_B_BASE &&
      base < VG_FW_SLOT_B_BASE + VG_FW_SLOT_SIZE)
    {
      return 'B';
    }

  return 'A';
}

char vg_fw_slot(void)
{
  return vg_fw_slot_from_marker(slot_marker(), link_base());
}

const char *vg_fw_slot_name(void)
{
  return vg_fw_slot() == 'B' ? "B" : "A";
}

uint32_t vg_fw_link_base(void)
{
  return (uint32_t)link_base();
}

int vg_fw_version_valid(const char *version)
{
  const char *p = version;
  int part;

  if (p == NULL)
    {
      return 0;
    }

  for (part = 0; part < 3; part++)
    {
      const char *start = p;

      while (*p >= '0' && *p <= '9')
        {
          p++;
        }

      if (p == start)
        {
          return 0;
        }

      if (part < 2)
        {
          if (*p != '.')
            {
              return 0;
            }

          p++;
        }
    }

  return *p == '\0';
}

int vg_fw_identity_format(char *out, size_t n)
{
  int w;

  if (out == NULL || n == 0)
    {
      return -1;
    }

  w = snprintf(out, n, "%s %s", vg_fw_version(), vg_fw_slot_name());
  if (w < 0 || (size_t)w >= n)
    {
      return -1;
    }

  return 0;
}
