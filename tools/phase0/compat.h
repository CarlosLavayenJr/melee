/* Phase 0 compatibility shim.
 *
 * The decomp targets MWCC/PowerPC. When compiling with GCC/Clang for a host
 * CPU, three things collide with the host's C library. None of these are
 * bugs in the decomp -- they are header-ordering and configuration issues.
 *
 *   1. platform.h does `typedef signed int ssize_t`, which matches glibc on
 *      32-bit but conflicts on 64-bit. Suppress glibc's own typedef.
 *   2. intptr_t / uintptr_t are used but not always reachable.
 *   3. M_PI / M_PI_2 are not exposed by glibc's math.h under strict modes.
 *
 * Pass with: -include tools/phase0/compat.h
 */
#ifndef PHASE0_COMPAT_H
#define PHASE0_COMPAT_H

/* Suppressing glibc's ssize_t leaves the name undefined for any translation
   unit that does not also pull in Runtime/platform.h -- pc/src/pc_sys_hosted.c
   reaches <unistd.h> without it. Define the same type here so the suppression
   is self-contained. platform.h's identical typedef is a legal redefinition. */
#define __ssize_t_defined
typedef signed int ssize_t;

#define _USE_MATH_DEFINES

#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

#endif /* PHASE0_COMPAT_H */
