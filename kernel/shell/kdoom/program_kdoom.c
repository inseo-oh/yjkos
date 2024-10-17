#include "../shell.h"
#include <kernel/io/tty.h>

#ifdef YJKERNEL_ENABLE_KDOOM
#include "thirdparty/PureDOOM.h"
#include <assert.h>
#include <kernel/arch/hcf.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <kernel/raster/fb.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

// It's 90s code time - I mean, time for many diagnostic overrides!
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wenum-compare"
#pragma GCC diagnostic ignored "-Wenum-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define DOOM_IMPLEMENTATION 
#include "thirdparty/PureDOOM.h"
// Leave the 90s world
#pragma GCC diagnostic pop


void __floatsidf(void) {
    // STUB
    assert(0);
}
void __divdf3(void) {
    // STUB
    assert(0);
}
void __muldf3(void) {
    // STUB
    assert(0);
}
void __gedf2(void) {
    // STUB
    assert(0);
}
void __ltdf2(void) {
    // STUB
    assert(0);
}
void __fixdfsi(void) {
    // STUB
    assert(0);
}


////////////////////////////////////////////////////////////////////////////////

static void* dmalloc(int size) {
    size_t finalsize = size * 2;
    void *ptr = heap_alloc(finalsize, 0);
    if (ptr == NULL) {
        tty_printf("[kdoom] not enough memory (Requested %d bytes)\n", size);
    }
    return ptr;
}

static void dfree(void* ptr) {
    return;
    heap_free(ptr);
}

static void dprint(const char* str) {
    tty_printf("%s", str);
}

static void dexit(int exitcode) {
    tty_printf("[kdoom] exited with code %d. Halting system.\n", exitcode);
    arch_hcf();
    while(1) {}
}

static char *dgetenv(char const *env) {
    // STUB
    if (strcmp(env, "HOME") == 0) {
        return "/";
    }
    return NULL;
}

static void* dopen(const char* filename, const char* mode) {
    if (mode[0] == 'w') {
        return NULL;
    }
    struct fd *fd;
    int ret = vfs_openfile(&fd, filename, 0);
    if (ret < 0) {
        tty_printf(
            "[kdoom] failed to open file %s (error %d)\n",
            filename, ret);
        return NULL;
    }
    tty_printf("[kdoom] opened file %s (fd %p)\n", filename, fd);
    (void)mode;
    return fd;
}

static void dclose(void* handle) {
    if (handle == NULL) {
        return;
    }
    vfs_closefile(handle);
}

static int dread(void* handle, void *buf, int count) {
    size_t len = count;
    ssize_t ret = vfs_readfile(handle, buf, len);
    if (ret < 0) {
        tty_printf("[kdoom] failed to read file %p\n", handle);
        // idk if returning -1 is correct behavior
        return -1;
    }
    return ret;
}

static int dwrite(void* handle, const void *buf, int count) {
    assert(!"TODO");
    (void)handle;
    (void)buf;
    (void)count;
    return 0;
}

static int dseek(void* handle, int offset, doom_seek_t origin) {
    int whence;
    switch(origin) {
        case DOOM_SEEK_END:
            whence = SEEK_END;
            break;
        case DOOM_SEEK_CUR:
            whence = SEEK_CUR;
            break;
        case DOOM_SEEK_SET:
            whence = SEEK_SET;
            break;
        default:
            panic("kdoom: unknown origin value");
    }
    int ret = vfs_seekfile(handle, offset, whence);
    if (ret < 0) {
        tty_printf("[kdoom] failed to seek file %p\n", handle);
        // idk if returning -1 is correct behavior
        return -1;
    }
    return offset;
}

static int dtell(void* handle) {
    (void)handle;
    assert(!"TODO");
    return 0;
}

static int deof(void* handle) {
    (void)handle;
    assert(!"TODO");
    return 0;
}

static void dgettime(int* sec, int* usec) {
    ticktime currenttime = g_ticktime;
    *sec = currenttime / 1000;
    *usec = (currenttime % 1000) * 1000;
}

#define MIDIPERIOD (1000 / 140) // 140Hz

static int program_main(int argc, char *argv[]) {
    doom_set_malloc(dmalloc, dfree);
    doom_set_print(dprint);
    doom_set_exit(dexit);
    doom_set_getenv(dgetenv);
    doom_set_gettime(dgettime);
    doom_set_file_io(dopen, dclose, dread, dwrite, dseek, dtell, deof);
    doom_init(argc, argv, 0);
    ticktime starttime = g_ticktime;
    ticktime lastframetime = g_ticktime;
    uint32_t framecount = 0;
    uint32_t fps = 0;
    while (1) {
        if ((g_ticktime - lastframetime) >= 1000) {
            fps = (framecount * 1000) / (g_ticktime - lastframetime);
            framecount = 0;
            lastframetime = g_ticktime;
        }
        if (g_ticktime - starttime >= MIDIPERIOD) {
            starttime = g_ticktime;
            uint32_t midimsg;
            while ((midimsg = doom_tick_midi())) {
                // XXX: The OS does not support MIDI devices(e.g. through Game Port on your sound card or MPU-401),
                //      but it's just stream of bytes so I managed to get MIDI bytes out of QEMU through second serial
                //      port, connected to a remote TCP server on a laptop running OpenBSD. But as of writing this comment,
                //      there is no clean way to access any TTY other than VGA console and serial0, so I had to hack it to
                //      initialize and expose the second serial port as a global variable.
                //
                //      Note that the TCP server on OpenBSD server was just a single nc command that was redirected to rmidi0
                //      device: nc -l 4000 > /dev/rmidi0
                //      (On Linux it seems like /dev/snd/midi~ devices will do the same job, but I haven't tested it)
                //
                //      Anyway, for the record, here's the code I used:
                // status_t status;
                // status = stream_putchar(&g_serial1.stream, midimsg);
                // status = stream_putchar(&g_serial1.stream, midimsg >> 8);
                // status = stream_putchar(&g_serial1.stream, midimsg >> 16);
                // (void)status;
            }
        }
        doom_update();
        uint8_t const *framebuffer = doom_get_framebuffer(4 /* RGBA */);
        static fb_color newfb[SCREENWIDTH * SCREENHEIGHT];
        for (size_t y = 0; y < SCREENHEIGHT; y++) {
            for (size_t x = 0; x < SCREENWIDTH; x++) {
                uint8_t r = framebuffer[y * (SCREENWIDTH * 4) + (x * 4) + 0];
                uint8_t g = framebuffer[y * (SCREENWIDTH * 4) + (x * 4) + 1];
                uint8_t b = framebuffer[y * (SCREENWIDTH * 4) + (x * 4) + 2];
                newfb[y * SCREENWIDTH + x] = makecolor(r, g, b);
            }
        }
        fb_drawimage(newfb, SCREENWIDTH, SCREENHEIGHT, SCREENWIDTH, 0, 0);
        fb_drawrect(188, 16, 0, 0, makecolor(255, 255, 255));
        char textbuf[] = "FPS: xx";
        if (fps < 100) {
            textbuf[5] = fps / 10 + '0';
            textbuf[6] = fps % 10 + '0';
        } else {
            textbuf[5] = '-';
            textbuf[6] = '-';
        }
        fb_drawtext(textbuf, 0, 0, makecolor(0, 0, 0));
        fb_update();
        framecount++;
    }
    return 0;
}

#else

static int program_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    tty_printf("ERROR: YJKERNEL_ENABLE_KDOOM was disabled during compilation\n");
    return 1;
}

#endif

struct shell_program g_shell_program_kdoom = {
    .name = "kdoom",
    .main = program_main,
};

