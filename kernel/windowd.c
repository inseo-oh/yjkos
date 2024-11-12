#include "windowd.h"
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>

static void tmain(void *arg) {
    (void)arg;
    arch_interrupts_enable();
    struct list *devlst = iodev_getlist(IODEV_TYPE_TTY);
    if ((devlst == NULL) || (devlst->front == NULL)) {
        co_printf("windowd: no serial device available\n");
        return;
    }
    struct iodev *clientdev = devlst->front->data;
    struct tty *clienttty = clientdev->data;
    struct stream *client = tty_getstream(clienttty);

    co_printf("windowd: listening X11 commands on serial1\n");
    while (1) {
        co_printf("%x\n", stream_getchar(client));
    }

    while(1) {}
}

void windowd_start(void) {
    bool threadstarted = false;
    struct thread *thread = thread_create(
        THREAD_STACK_SIZE, tmain,
        NULL);
    if (thread == NULL) {
        co_printf("not enough memory to create thread\n");
        goto die;
    }
    int ret = sched_queue(thread);
    if (ret < 0) {
        co_printf("failed to queue thread (error %d)\n", ret);
        goto die;
    }
    threadstarted = true;
    return;
die:
    if (threadstarted) {
        thread->shutdown = true;
    }
}


