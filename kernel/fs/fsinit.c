#include "fsinit.h"

void fsinit_init_all(void) {
    fsinit_init_ext2();
    fsinit_init_dummyfs();
}
