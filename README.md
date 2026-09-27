# 双向循环链表搜索与 perf 性能测量

本项目完成《课1–2作业》中第 2 题“开发与分析题”：用 C 实现双向循环链表搜索，不使用哈希，通过 Linux `perf_event_open()` 测量搜索过程的指令数、周期数、访存次数和 L1 数据缓存读命中率。它是独立项目。

题目内嵌的 [link1.md](docs/link1.md) 已从 Word 的 OLE 附件中提取，原文保存在仓库中；[设计与要求对应说明](docs/design.md) 解释实现、测量口径和规模限制。

[Linux 自动测试已通过](https://github.com/JOKER20180713/linked-list-perf/actions/runs/36311407062)：GCC、Clang、功能测试及内存安全检查。真实日志和硬件计数的环境限制见 [验证记录](docs/verification.md)；本次云端虚拟机没有提供所请求的 PMU 事件，相关指标显示 N/A。

## 快速运行

要求：64 位 Linux、GCC 或 Clang、Make；测试另需 Python 3。Windows 原生无法调用 Linux 的 `perf_event_open()`，可使用有 PMU 支持的 Linux 实机或虚拟机。WSL 和云端虚拟机不一定暴露硬件计数器。

```bash
git clone https://github.com/JOKER20180713/linked-list-perf.git
cd linked-list-perf
make
make test
./build/linkbench
```

默认使用 GCC、C11、`-O2`，不启用 `-O3`。无需第三方 C 库。单独测试内存安全：

```bash
make sanitize
make clean && make
```

## 必需接口

```c
LinkData *InitLink(int size, char *mem);
void AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *FindNode(LinkData *firstNode, int size, char *mem);
```

头节点本身保存数据。单节点时 `prev` 和 `next` 指向自己；追加保持 `head->prev` 指向尾节点；搜索从头顺序遍历一圈，返回第一个完全相同的节点，未找到返回 `NULL`。比较支持包含零字节的二进制数据。

另外提供 `AppendNodeChecked()`，以返回值报告分配失败；题目要求的 `void AppendNode()` 保留，通过 `errno` 报告失败。`FreeLink()` 释放整条链表。调用者拥有输入缓冲区，节点内部保存其副本。

## 搜索优化

- 节点头与数据放在同一块分配的内存中，减少分配次数，改善局部性。
- 先比较 `dataSize`，再比较真实数据的前 8 字节，最后检查剩余全部字节。
- 查询的前 8 字节在循环外加载；固定长度 `memcpy` 避免非对齐读取和类型别名错误。
- 提供普通 `memcmp` 遍历作为对照；两者使用同一批链表和查询。

没有哈希表、哈希值、指纹、排序或辅助检索索引。复杂度仍为 O(n)。优化收益依赖 CPU、数据分布和缓存，不能保证每种工作负载都更快，因此同时测试前缀不同与前缀相同的数据。

## 性能实验

```bash
# 4 条链表，每条 10000 个节点，每次携带 64 字节数据
./build/linkbench --lists 4 --nodes 10000 --size 64 --queries 2000 --repeat 5

# 完整遍历未命中；数据前缀相同，考察剩余内容比较
./build/linkbench --mode miss --pattern shared-prefix --size 512

# 指定允许使用的 CPU，降低迁移干扰；CPU 编号按实际环境选择
./build/linkbench --cpu 0 --mode tail

# 所有要求的硬件指标必须可用，否则退出码为 2
./build/linkbench --require-perf

# 记录 CPU、内核、参数、结果；按日期追加到 results/MMDD_1.log
bash scripts/run_experiment.sh
```

支持 `--mode head|tail|hit|miss|mixed`，`--algorithm baseline|optimized|both`，`--pattern varied|shared-prefix`。可配置链表条数、每条节点数、数据长度、查询数、随机种子和重复次数，运行 `--help` 查看。

程序在计数前完成内存分配和查询生成，每组测量前预热；只计数当前线程用户态的搜索循环（含调用与校验和的少量开销）。不同计数器组重放同一批查询，减少对同时可用 PMU 槽位数量的要求。

## 输出指标与口径

| 输出字段 | 意义 |
| --- | --- |
| `instructions` | 执行指令数，`PERF_COUNT_HW_INSTRUCTIONS` |
| `cycles` | CPU 周期数，`PERF_COUNT_HW_CPU_CYCLES` |
| `L1D_load_accesses` | L1 数据缓存读访问次数 |
| `L1D_store_accesses` | L1 数据缓存写访问次数 |
| `memory_accesses_L1D_loads_plus_stores` | 访存次数，按 L1D 读访问 + 写访问计；不同测量轮次相加的估计 |
| `L1D_load_misses` | L1 数据缓存读未命中次数 |
| `L1D_load_hit_rate_pct` | L1 数据缓存读命中率，`100 × (1 - misses / loads)` |
| `ns_per_search` | 不启用硬件计数器的独立计时轮次，每次搜索的平均纳秒数 |
| `hardware_metrics` | `complete` 或 `incomplete` |

“访存”不是 DRAM 事务数；缓存访问不一定到达主存。这里的 L1 命中率明确限定为数据缓存的读命中率，不代表指令缓存或全部读写命中率。通用事件的具体含义与支持程度仍受处理器 PMU 影响。

程序输出 raw 原始值、enabled/running 时间及运行比例。若发生复用，计数按 `raw × time_enabled / time_running` 缩放并标为估计值。L1 读访问与读未命中处于同一事件组。零运行时间、读取失败、权限不足、不支持的事件或不一致的命中率一律显示 `N/A`，不会填零或伪造结果。

## 权限和虚拟化

先查看 `cat /proc/sys/kernel/perf_event_paranoid`。程序已排除内核态和虚拟机监控器，只测当前线程；仍出现 `EACCES` / `EPERM` 时，需要在有授权的 Linux 环境运行。管理员可以按需为二进制授予 `CAP_PERFMON`；不要为本作业无差别放宽整台机器的安全设置。

`EINVAL` / `ENOENT` / `EOPNOTSUPP` 也可能表示 CPU 或虚拟机没有实现该事件；提高权限无法创建不存在的 PMU。默认模式继续显示搜索耗时与校验结果，严格模式返回 2。退出码 1 表示参数、分配、CPU 绑定或搜索校验失败。

## 验证与目录

`make test` 覆盖循环链表连接、首尾命中、重复值、未命中、二进制内容、1–512 字节 API 输入、10000 节点、1000 条链表，以及计数器权限失败、复用缩放、事件 ID、EINTR、异常读取和描述符清理。命令行集成测试覆盖 5 种查询模式、2 种数据模式与 32/512 字节边界。计数器单元测试使用模拟系统调用验证错误处理；它们的数值不作为性能实验数据。

GitHub Actions 使用 GCC 和 Clang 编译、执行测试与 AddressSanitizer/UndefinedBehaviorSanitizer，并保存真实运行日志。CI 能验证 Linux 程序，但虚拟机是否支持全部硬件事件须以日志为准。

```text
include/                数据结构与计数器接口
src/link.c              链表与两种搜索
src/perf_metrics.c      perf_event_open 分组计数
src/main.c              可重复的基准程序
tests/                  正确性、计数器异常和命令行测试
scripts/                实验日志脚本
docs/link1.md           Word 内嵌题目原文
docs/design.md          设计与题目逐项对应
```

## 参考资料

- [Linux perf_event_open 手册](https://man7.org/linux/man-pages/man2/perf_event_open.2.html)：事件类型、分组、时间缩放与错误码。
- [Linux 内核 perf 权限说明](https://docs.kernel.org/admin-guide/perf-security.html)：`perf_event_paranoid` 与 `CAP_PERFMON`。
