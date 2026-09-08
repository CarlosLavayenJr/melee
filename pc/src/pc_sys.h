/* pc_sys.h — the host services the port layer needs, named once.
 *
 * Two builds exist. A hosted build links the platform's C library normally. A
 * freestanding build (-nostdlib) links none of it, because the 32-bit target
 * the decomp requires may have no 32-bit libc installed even where the
 * compiler can emit 32-bit code. Both provide these three.
 */
#ifndef PC_SYS_H
#define PC_SYS_H

/* Map size bytes of zeroed, writable memory at exactly `at`. Returns 0 on
   failure. Nothing here can relocate: the game hardcodes console addresses. */
int pc_sys_map_fixed(unsigned long at, unsigned long size);

/* Write to the diagnostic stream. */
void pc_sys_log(const char* s);

void pc_sys_exit(int code);

/* Nanoseconds from a monotonic source. Only differences are meaningful. */
unsigned long long pc_sys_mono_ns(void);

/* Read-only file access, for the disc image. Returns a handle, or -1.
   Reads are positional so the caller keeps no file offset of its own -- which
   suits DVDLowRead, whose every request carries an absolute disc offset. */
int pc_sys_open_ro(const char* path);
long pc_sys_pread(int fd, void* buf, unsigned long len,
                  unsigned long long offset);
void pc_sys_close(int fd);

#endif
