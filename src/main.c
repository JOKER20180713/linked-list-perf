#define _GNU_SOURCE
#include "link.h"
#include "perf_metrics.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef LinkData *(*Search)(LinkData *, int, char *);
typedef struct Query {
    size_t list;
    uint64_t expected;
    char data[LINK_MAX_DATA_SIZE];
} Query;

typedef struct Options {
    size_t lists, nodes, queries, repeat;
    int size, cpu, require_perf, shared_prefix;
    uint64_t seed;
    const char *mode, *algorithm;
} Options;

static volatile uint64_t result_sink;

static void usage(const char *program)
{
    printf("Usage: %s [options]\n"
           "  --lists N          number of independent circular lists (default 4)\n"
           "  --nodes N          nodes per list (default 4096)\n"
           "  --size N           payload bytes, 32..512 (default 64)\n"
           "  --queries N        searches per pass (default 1000)\n"
           "  --repeat N         trials per algorithm (default 3)\n"
           "  --seed N           deterministic nonzero seed (default 42)\n"
           "  --mode MODE        head | tail | hit | miss | mixed (default mixed)\n"
           "  --algorithm NAME   baseline | optimized | both (default both)\n"
           "  --pattern NAME     varied | shared-prefix (default varied)\n"
           "  --cpu N            pin benchmark thread to this allowed CPU\n"
           "  --require-perf     exit 2 if any required hardware metric is unavailable\n"
           "  --help             show usage\n", program);
}

static int number(const char *s, uint64_t *out)
{
    if (*s == '\0')
        return -1;
    for (const char *p = s; *p; ++p)
        if (*p < '0' || *p > '9')
            return -1;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0')
        return -1;
    *out = (uint64_t)n;
    return 0;
}

static int parse(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (strcmp(key, "--help") == 0) {
            usage(argv[0]);
            return 1;
        }
        if (strcmp(key, "--require-perf") == 0) {
            o->require_perf = 1;
            continue;
        }
        if (i + 1 >= argc)
            return -1;
        const char *value = argv[++i];
        if (strcmp(key, "--mode") == 0) {
            o->mode = value;
        } else if (strcmp(key, "--algorithm") == 0) {
            o->algorithm = value;
        } else if (strcmp(key, "--pattern") == 0) {
            if (strcmp(value, "varied") == 0)
                o->shared_prefix = 0;
            else if (strcmp(value, "shared-prefix") == 0)
                o->shared_prefix = 1;
            else
                return -1;
        } else {
            uint64_t n;
            if (number(value, &n) < 0 || n > SIZE_MAX)
                return -1;
            if (strcmp(key, "--cpu") == 0) {
                if (n >= CPU_SETSIZE)
                    return -1;
                o->cpu = (int)n;
            } else {
                if (n == 0)
                    return -1;
                if (strcmp(key, "--lists") == 0)
                    o->lists = (size_t)n;
                else if (strcmp(key, "--nodes") == 0)
                    o->nodes = (size_t)n;
                else if (strcmp(key, "--queries") == 0)
                    o->queries = (size_t)n;
                else if (strcmp(key, "--repeat") == 0)
                    o->repeat = (size_t)n;
                else if (strcmp(key, "--seed") == 0)
                    o->seed = n;
                else if (strcmp(key, "--size") == 0 && n >= 32 && n <= 512)
                    o->size = (int)n;
                else
                    return -1;
            }
        }
    }
    if (strcmp(o->mode, "head") && strcmp(o->mode, "tail") &&
        strcmp(o->mode, "hit") && strcmp(o->mode, "miss") &&
        strcmp(o->mode, "mixed"))
        return -1;
    if (strcmp(o->algorithm, "baseline") && strcmp(o->algorithm, "optimized") &&
        strcmp(o->algorithm, "both"))
        return -1;
    if (o->lists > SIZE_MAX / o->nodes ||
        o->lists > SIZE_MAX / sizeof(LinkData *) ||
        o->queries > SIZE_MAX / sizeof(Query))
        return -1;
    size_t total = o->lists * o->nodes;
    if (total > (UINT64_MAX - o->queries) / 2 ||
        total > SIZE_MAX / (sizeof(LinkData) + (size_t)o->size))
        return -1;
    return 0;
}

static uint64_t random_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static void payload(char *data, int size, uint64_t key, int shared_prefix)
{
    memset(data, 0x5a, (size_t)size);
    /* All keys are exact bytes. A shared-prefix workload stresses the tail. */
    size_t offset = shared_prefix ? (size_t)size - sizeof(key) : 0;
    memcpy(data + offset, &key, sizeof(key));
}

static uint64_t run_queries(LinkData **heads, Query *queries,
                            const Options *o, Search search)
{
    uint64_t checksum = 0;
    size_t offset = o->shared_prefix ? (size_t)o->size - sizeof(uint64_t) : 0;
    for (size_t i = 0; i < o->queries; ++i) {
        LinkData *node = search(heads[queries[i].list], o->size, queries[i].data);
        if (node != NULL) {
            uint64_t key;
            memcpy(&key, node->data + offset, sizeof(key));
            checksum += key;
        }
    }
    result_sink = checksum;
    return checksum;
}

static int timed_pass(LinkData **heads, Query *queries, const Options *o,
                      Search search, uint64_t expected, double *ns)
{
    struct timespec before, after;
    if (clock_gettime(CLOCK_MONOTONIC, &before) < 0)
        return -1;
    uint64_t checksum = run_queries(heads, queries, o, search);
    if (clock_gettime(CLOCK_MONOTONIC, &after) < 0)
        return -1;
    *ns = (double)(after.tv_sec - before.tv_sec) * 1e9 +
          (double)(after.tv_nsec - before.tv_nsec);
    return checksum == expected ? 0 : -1;
}

static int measured_pass(PerfGroup *group, const PerfEventSpec *events,
                         size_t count, LinkData **heads, Query *queries,
                         const Options *o, Search search, uint64_t expected)
{
    if (PerfOpen(group, events, count) < 0)
        return 0; /* Preserve the precise unavailable reason. */
    /* Every group gets the same warm-up and the same deterministic queries. */
    if (run_queries(heads, queries, o, search) != expected) {
        PerfClose(group);
        return -1;
    }
    if (PerfStart(group) == 0) {
        uint64_t checksum = run_queries(heads, queries, o, search);
        (void)PerfStop(group);
        if (checksum != expected) {
            PerfClose(group);
            return -1;
        }
    }
    PerfClose(group);
    return 0;
}

static void metric(const char *label, const PerfGroup *group, size_t index,
                   size_t queries)
{
    if (group->valid)
        printf("%s: %.0f (per_search=%.3f, raw=%" PRIu64 ")\n",
               label, group->value[index], group->value[index] / (double)queries,
               group->raw[index]);
    else
        printf("%s: N/A [%s]\n", label, group->error);
}

static void timing(const char *label, const PerfGroup *group)
{
    if (group->valid)
        printf("%s: enabled_ns=%" PRIu64 " running_ns=%" PRIu64
               " running_fraction=%.6f%s\n", label, group->enabled_ns,
               group->running_ns,
               (double)group->running_ns / (double)group->enabled_ns,
               group->running_ns < group->enabled_ns ? " (scaled estimate)" : "");
}

static int benchmark(LinkData **heads, Query *queries, const Options *o,
                     Search search, const char *name, uint64_t expected,
                     size_t trial)
{
    const PerfEventSpec core[] = {
        {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
        {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES}
    };
    const PerfEventSpec loads[] = {
        {"L1D-loads", PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D |
         (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16)},
        {"L1D-load-misses", PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D |
         (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)}
    };
    const PerfEventSpec stores[] = {
        {"L1D-stores", PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D |
         (PERF_COUNT_HW_CACHE_OP_WRITE << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16)}
    };
    PerfGroup core_group, load_group, store_group;
    double elapsed;
    if (run_queries(heads, queries, o, search) != expected ||
        timed_pass(heads, queries, o, search, expected, &elapsed) < 0)
        return -1;
    if (measured_pass(&core_group, core, 2, heads, queries, o, search, expected) < 0 ||
        measured_pass(&load_group, loads, 2, heads, queries, o, search, expected) < 0 ||
        measured_pass(&store_group, stores, 1, heads, queries, o, search, expected) < 0)
        return -1;

    printf("\nalgorithm=%s trial=%zu checksum=%" PRIu64 " verified=yes\n",
           name, trial, expected);
    printf("elapsed_ns: %.0f\nns_per_search: %.3f\n", elapsed, elapsed / (double)o->queries);
    metric("instructions", &core_group, 0, o->queries);
    metric("cycles", &core_group, 1, o->queries);
    metric("L1D_load_accesses", &load_group, 0, o->queries);
    metric("L1D_load_misses", &load_group, 1, o->queries);
    metric("L1D_store_accesses", &store_group, 0, o->queries);
    if (load_group.valid && store_group.valid)
        printf("memory_accesses_L1D_loads_plus_stores: %.0f (separate-pass estimate)\n",
               load_group.value[0] + store_group.value[0]);
    else
        puts("memory_accesses_L1D_loads_plus_stores: N/A [requires both loads and stores]");
    double hit_rate;
    int rate_ok = load_group.valid &&
                  PerfHitRate(load_group.value[0], load_group.value[1], &hit_rate) == 0;
    if (rate_ok)
        printf("L1D_load_hit_rate_pct: %.4f\n", hit_rate);
    else
        puts("L1D_load_hit_rate_pct: N/A [unsupported, zero accesses, or inconsistent counters]");
    timing("core_group", &core_group);
    timing("load_group", &load_group);
    timing("store_group", &store_group);
    int complete = core_group.valid && load_group.valid && store_group.valid && rate_ok;
    printf("hardware_metrics: %s\n", complete ? "complete" : "incomplete");
    return complete ? 0 : 2;
}

int main(int argc, char **argv)
{
    Options o = {4, 4096, 1000, 3, 64, -1, 0, 0, 42, "mixed", "both"};
    int parsed = parse(argc, argv, &o);
    if (parsed > 0)
        return 0;
    if (parsed < 0) {
        fputs("Invalid argument or allocation-size overflow; use --help.\n", stderr);
        return 1;
    }
    if (o.cpu >= 0) {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        CPU_SET(o.cpu, &cpus);
        if (sched_setaffinity(0, sizeof(cpus), &cpus) < 0) {
            perror("sched_setaffinity");
            return 1;
        }
    }
    LinkData **heads = calloc(o.lists, sizeof(*heads));
    Query *queries = calloc(o.queries, sizeof(*queries));
    if (heads == NULL || queries == NULL) {
        perror("allocate dataset");
        free(heads);
        free(queries);
        return 1;
    }
    int status = 0;
    char bytes[LINK_MAX_DATA_SIZE];
    for (size_t l = 0; l < o.lists; ++l) {
        for (size_t n = 0; n < o.nodes; ++n) {
            uint64_t id = (uint64_t)(l * o.nodes + n + 1);
            payload(bytes, o.size, id, o.shared_prefix);
            if (n == 0)
                heads[l] = InitLink(o.size, bytes);
            else if (AppendNodeChecked(heads[l], o.size, bytes) < 0)
                goto allocation_error;
            if (heads[l] == NULL)
                goto allocation_error;
        }
    }
    uint64_t state = o.seed, expected = 0;
    size_t hits = 0;
    for (size_t q = 0; q < o.queries; ++q) {
        size_t l = (size_t)(random_next(&state) % o.lists);
        size_t n = (size_t)(random_next(&state) % o.nodes);
        int hit = strcmp(o.mode, "miss") != 0 &&
                  (strcmp(o.mode, "mixed") != 0 || q % 2 == 0);
        if (strcmp(o.mode, "head") == 0)
            n = 0;
        if (strcmp(o.mode, "tail") == 0)
            n = o.nodes - 1;
        uint64_t id = hit ? (uint64_t)(l * o.nodes + n + 1) :
                           (uint64_t)(o.lists * o.nodes) + q + 1;
        queries[q].list = l;
        queries[q].expected = hit ? id : 0;
        payload(queries[q].data, o.size, id, o.shared_prefix);
        expected += queries[q].expected;
        hits += (size_t)hit;
    }
    printf("Linked-list search benchmark (no hash)\n"
           "lists=%zu nodes_per_list=%zu total_nodes=%zu payload_bytes=%d header_bytes=%zu\n"
           "queries=%zu expected_hits=%zu mode=%s pattern=%s seed=%" PRIu64 " repeat=%zu\n"
           "measurement_scope=user-space search loop; allocation/generation/output excluded\n"
           "Each counter group replays identical warmed queries; N/A is never a zero count.\n",
           o.lists, o.nodes, o.lists * o.nodes, o.size, sizeof(LinkData),
           o.queries, hits, o.mode, o.shared_prefix ? "shared-prefix" : "varied",
           o.seed, o.repeat);
    for (size_t trial = 1; trial <= o.repeat; ++trial) {
        /* Alternate order across trials to reduce fixed-order timing bias. */
        for (size_t pass = 0; pass < 2; ++pass) {
            int optimized = (int)((pass + trial + 1) % 2);
            const char *name = optimized ? "optimized" : "baseline";
            if (strcmp(o.algorithm, "both") && strcmp(o.algorithm, name))
                continue;
            int result = benchmark(heads, queries, &o,
                                   optimized ? FindNode : FindNodeBaseline,
                                   name, expected, trial);
            if (result < 0) {
                fputs("Search verification or monotonic clock failed.\n", stderr);
                status = 1;
                goto cleanup;
            }
            if (result == 2 && o.require_perf)
                status = 2;
        }
        if (trial == SIZE_MAX)
            break;
    }
    goto cleanup;

allocation_error:
    perror("allocate linked-list node");
    status = 1;
cleanup:
    for (size_t l = 0; l < o.lists; ++l)
        FreeLink(heads[l]);
    free(heads);
    free(queries);
    return status;
}
