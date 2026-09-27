#ifndef PERF_METRICS_H
#define PERF_METRICS_H

#include <stddef.h>
#include <stdint.h>

typedef struct PerfEventSpec {
    const char *name;
    uint32_t type;
    uint64_t config;
} PerfEventSpec;

typedef struct PerfGroup {
    int fd[2];
    uint64_t id[2];
    size_t count;
    uint64_t raw[2];
    double value[2];
    uint64_t enabled_ns;
    uint64_t running_ns;
    int valid;
    char error[192];
} PerfGroup;

int PerfOpen(PerfGroup *group, const PerfEventSpec *events, size_t count);
int PerfStart(PerfGroup *group);
int PerfStop(PerfGroup *group);
void PerfClose(PerfGroup *group);
int PerfHitRate(double accesses, double misses, double *percentage);

#endif
