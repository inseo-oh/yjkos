#pragma once

#ifdef __cplusplus
#ifdef __GNUC__
#define RESTRICT    __restrict__
#else
#define RESTRICT
#endif // __GNUC__
#else // __cplusplus
#define RESTRICT    restrict
#endif // __cplusplus
