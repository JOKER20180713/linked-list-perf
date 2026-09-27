#define _GNU_SOURCE
#include "perf_metrics.h"

#include <errno.h>
#include <linux/perf_event.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failure(PerfGroup *group, const char *operation, int code)
{
    group->valid = 0;
    (void)snprintf(group->error, sizeof(group->error), "%s: %s (errno=%d)",
                   operation, strerror(code), code);
    return -1;
}

void PerfClose(PerfGroup *group)
{
    for (size_t i = 0; i < 2; ++i) {
        if (group->fd[i] >= 0) {
            (void)close(group->fd[i]);
            group->fd[i] = -1;
        }
    }
}

int PerfOpen(PerfGroup *group, const PerfEventSpec *events, size_t count)
{
    memset(group, 0, sizeof(*group));
    group->fd[0] = group->fd[1] = -1;
    if (count < 1 || count > 2)
        return failure(group, "invalid event group", EINVAL);
    group->count = count;
    for (size_t i = 0; i < count; ++i) {
        struct perf_event_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.type = events[i].type;
        attr.config = events[i].config;
        attr.disabled = i == 0;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID |
                           PERF_FORMAT_TOTAL_TIME_ENABLED |
                           PERF_FORMAT_TOTAL_TIME_RUNNING;
        /* pid=0: calling thread; cpu=-1: follow this thread across CPUs. */
        group->fd[i] = (int)syscall(SYS_perf_event_open, &attr, 0, -1,
                                    i == 0 ? -1 : group->fd[0],
                                    PERF_FLAG_FD_CLOEXEC);
        if (group->fd[i] < 0) {
            int saved = errno;
            PerfClose(group);
            return failure(group, events[i].name, saved);
        }
        if (ioctl(group->fd[i], PERF_EVENT_IOC_ID, &group->id[i]) < 0) {
            int saved = errno;
            PerfClose(group);
            return failure(group, "PERF_EVENT_IOC_ID", saved);
        }
    }
    return 0;
}

int PerfStart(PerfGroup *group)
{
    if (group->fd[0] < 0)
        return -1;
    if (ioctl(group->fd[0], PERF_EVENT_IOC_RESET, (unsigned long)PERF_IOC_FLAG_GROUP) < 0)
        return failure(group, "PERF_EVENT_IOC_RESET", errno);
    if (ioctl(group->fd[0], PERF_EVENT_IOC_ENABLE, (unsigned long)PERF_IOC_FLAG_GROUP) < 0)
        return failure(group, "PERF_EVENT_IOC_ENABLE", errno);
    return 0;
}

int PerfStop(PerfGroup *group)
{
    if (ioctl(group->fd[0], PERF_EVENT_IOC_DISABLE, (unsigned long)PERF_IOC_FLAG_GROUP) < 0) {
        int saved = errno;
        PerfClose(group);
        return failure(group, "PERF_EVENT_IOC_DISABLE", saved);
    }
    uint64_t buffer[7] = {0};
    size_t wanted = (3 + 2 * group->count) * sizeof(uint64_t);
    ssize_t got;
    do {
        got = read(group->fd[0], buffer, wanted);
    } while (got < 0 && errno == EINTR);
    if (got < 0)
        return failure(group, "read", errno);
    if ((size_t)got != wanted || buffer[0] != group->count)
        return failure(group, "incomplete perf group read", EIO);
    group->enabled_ns = buffer[1];
    group->running_ns = buffer[2];
    if (buffer[2] == 0 || buffer[2] > buffer[1])
        return failure(group, "event group was not scheduled correctly", EAGAIN);
    unsigned seen = 0;
    for (size_t i = 0; i < group->count; ++i) {
        size_t j;
        for (j = 0; j < group->count; ++j)
            if (buffer[4 + 2 * i] == group->id[j])
                break;
        if (j == group->count || (seen & (1U << j)) != 0)
            return failure(group, "unexpected perf event ID", EIO);
        seen |= 1U << j;
        group->raw[j] = buffer[3 + 2 * i];
        group->value[j] = (double)group->raw[j] *
                         ((double)buffer[1] / (double)buffer[2]);
    }
    group->valid = 1;
    return 0;
}

int PerfHitRate(double accesses, double misses, double *percentage)
{
    if (!isfinite(accesses) || !isfinite(misses) || accesses <= 0 ||
        misses < 0 || misses > accesses)
        return -1;
    *percentage = 100.0 * (1.0 - misses / accesses);
    return 0;
}
