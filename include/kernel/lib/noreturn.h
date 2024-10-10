#pragma once

#if __STDC_VERSION__ < 202311
// Pre-C23
#define NORETURN       _Noreturn
#else
// C23
#define NORETURN       [[noreturn]]
#endif

