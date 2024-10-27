#ifndef _ASSERT_H
#define _ASSERT_H   1

// In C23, _Static_assert keyword and static_assert macro is deprecated, and static_assert itself is a keyword.
#if __STDC_VERSION__ < 202311
#define static_assert       _Static_assert
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);

#ifdef NDEBUG
#define assert(_x)  ((void)0)
#else
#define assert(_x)  (!(_x) ? __assert_fail(#_x, __FILE__, __LINE__, __func__) : (void)(0))
#endif

#endif
