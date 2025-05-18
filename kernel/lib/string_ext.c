#include <kernel/lib/string_ext.h>
#include <stddef.h>

void MemCopy32(void *restrict s1, const void *restrict s2, size_t n) {
#ifdef YJKERNEL_ARCH_I586
    int dummy[3];
    __asm__ volatile (
        "pushf\n"
        "cld\n"
        "rep movsl\n"
        "popf\n"
        : "=c"(dummy[0]),
          "=S"(dummy[1]),
          "=D"(dummy[2])
        : "c"(n),
          "S"(s2),
          "D"(s1)
    );
#else
    #error TODO
#endif
}
