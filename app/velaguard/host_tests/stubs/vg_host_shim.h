/****************************************************************************
 * host_tests/stubs/vg_host_shim.h
 *
 * Force-included (-include) when compiling NuttX-facing VelaGuard sources on
 * the host.  NuttX defines OK/ERROR in <sys/types.h>; glibc does not, and
 * agent_compat.h relies on that convention, so the two constants are supplied
 * here under the same values the board uses.
 ****************************************************************************/

#ifndef __HOST_TEST_VG_HOST_SHIM_H
#define __HOST_TEST_VG_HOST_SHIM_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifndef OK
#  define OK 0
#endif

#ifndef ERROR
#  define ERROR (-1)
#endif

#ifndef FAR
#  define FAR
#endif

#endif /* __HOST_TEST_VG_HOST_SHIM_H */
