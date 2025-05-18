#include "fsinit.h"

void FsInit_InitAll(void) {
    FsInit_InitExt2();
    FsInit_InitDummyFs();
}
