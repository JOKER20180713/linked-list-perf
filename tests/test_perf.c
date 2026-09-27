#define _GNU_SOURCE
#include "perf_metrics.h"
#include <assert.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>

static int opens, closes, fail_open, fail_control, read_mode, reads;
static size_t members;
static const PerfEventSpec events[] = {
    {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
    {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES}
};

long __wrap_syscall(long number, ...)
{
    assert(number == SYS_perf_event_open);
    va_list args;
    va_start(args, number);
    const struct perf_event_attr *attr = va_arg(args, const struct perf_event_attr *);
    assert(va_arg(args, int) == 0);
    assert(va_arg(args, int) == -1);
    int leader = va_arg(args, int);
    assert(leader == (opens == 0 ? -1 : 0));
    assert(va_arg(args, unsigned long) == PERF_FLAG_FD_CLOEXEC);
    va_end(args);
    assert(attr->exclude_kernel == 1 && attr->exclude_hv == 1);
    assert(attr->disabled == (opens == 0));
    assert((attr->read_format & PERF_FORMAT_ID) != 0);
    ++opens;
    if (fail_open == opens) {
        errno = EACCES;
        return -1;
    }
    return opens - 1; /* fd 0 is valid and must be closed. */
}

int __wrap_close(int fd)
{
    assert(fd >= 0 && fd <= 1);
    ++closes;
    return 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    assert(fd >= 0 && fd <= 1);
    va_list args;
    va_start(args, request);
    if (request == PERF_EVENT_IOC_ID) {
        uint64_t *id = va_arg(args, uint64_t *);
        *id = (uint64_t)(100 + fd);
    } else {
        assert(va_arg(args, unsigned long) == PERF_IOC_FLAG_GROUP);
    }
    va_end(args);
    if ((fail_control == 1 && request == PERF_EVENT_IOC_ENABLE) ||
        (fail_control == 2 && request == PERF_EVENT_IOC_DISABLE)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

ssize_t __wrap_read(int fd, void *data, size_t size)
{
    assert(fd == 0 && size == (3 + members * 2) * sizeof(uint64_t));
    ++reads;
    if (read_mode == 1 && reads == 1) {
        errno = EINTR;
        return -1;
    }
    if (read_mode == 2)
        return 0;
    uint64_t buffer[7] = {members, 200, 100, 20, 100, 40, 101};
    if (members == 2) { /* Kernel order is decoded using IDs, not assumptions. */
        buffer[3] = 40; buffer[4] = 101;
        buffer[5] = 20; buffer[6] = 100;
    }
    if (read_mode == 3)
        buffer[2] = 0;
    if (read_mode == 4)
        buffer[4] = 999;
    if (read_mode == 5)
        buffer[0] = 9;
    if (read_mode == 6)
        buffer[2] = 201;
    if (read_mode == 7)
        buffer[6] = buffer[4];
    if (read_mode == 8) {
        errno = EIO;
        return -1;
    }
    memcpy(data, buffer, size);
    return (ssize_t)size;
}

static void reset(size_t count)
{
    opens = closes = fail_open = fail_control = read_mode = reads = 0;
    members = count;
}

int main(void)
{
    PerfGroup group;
    double rate;
    assert(PerfHitRate(100, 25, &rate) == 0 && rate == 75);
    assert(PerfHitRate(0, 0, &rate) == -1);
    assert(PerfHitRate(100, 101, &rate) == -1);
    assert(PerfHitRate(NAN, 1, &rate) == -1);
    assert(PerfHitRate(100, -1, &rate) == -1);

    for (size_t n = 1; n <= 2; ++n) {
        reset(n);
        assert(PerfOpen(&group, events, n) == 0);
        assert(PerfStart(&group) == 0);
        assert(PerfStop(&group) == 0 && group.valid);
        assert(group.raw[0] == 20 && group.value[0] == 40);
        if (n == 2)
            assert(group.raw[1] == 40 && group.value[1] == 80);
        PerfClose(&group);
        assert(closes == (int)n);
        PerfClose(&group);
        assert(closes == (int)n);
    }
    for (int fail = 1; fail <= 2; ++fail) {
        reset(2);
        fail_open = fail;
        assert(PerfOpen(&group, events, 2) == -1);
        assert(strstr(group.error, "errno=") != NULL);
        assert(closes == fail - 1 && !group.valid);
    }
    for (int mode = 1; mode <= 8; ++mode) {
        reset(2);
        read_mode = mode;
        assert(PerfOpen(&group, events, 2) == 0);
        assert(PerfStart(&group) == 0);
        assert(PerfStop(&group) == (mode == 1 ? 0 : -1));
        assert(group.valid == (mode == 1));
        PerfClose(&group);
        assert(closes == 2);
    }
    for (int fail = 1; fail <= 2; ++fail) {
        reset(2);
        fail_control = fail;
        assert(PerfOpen(&group, events, 2) == 0);
        if (fail == 1)
            assert(PerfStart(&group) == -1);
        else {
            assert(PerfStart(&group) == 0);
            assert(PerfStop(&group) == -1);
        }
        PerfClose(&group);
        assert(closes == 2 && !group.valid);
    }
    puts("PASS: perf grouping, scaling, IDs, permissions, EINTR, short reads, zero runtime, cleanup");
    return 0;
}
