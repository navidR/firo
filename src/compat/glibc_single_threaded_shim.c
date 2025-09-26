#include <features.h>
// Strong (not weak) so the linker certainly satisfies the undefined ref when old glibc.
// Real glibc (>=2.34) provides a strong one that will override at dynamic link.
#if defined(__GLIBC__)
int __libc_single_threaded = 0;
#endif