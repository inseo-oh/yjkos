#ifndef _YJK_INTERNAL_TYPES_H
#define _YJK_INTERNAL_TYPES_H    1

#ifndef __YJK_USE_INTERNAL_TYPES_H
#error Do not include <internal/types.h> directly. Include <sys/types.h> instead.
#endif

#ifdef __i386__

typedef __SIZE_TYPE__ __YJK_SIZE_TYPE;
// There's no compiler builtin for ssize_t, as far as I know.
typedef long __YJK_SSIZE_TYPE;

#else // __i386__
#error Unknown arch
#endif

#endif
