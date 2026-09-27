# 验证记录

验证日期：2026 年 9 月 27 日。测试通过的源代码提交为
`78edd0dce4d1da3a121950f64bb909a2c7ef1116`。

[GitHub Actions 完整记录](https://github.com/JOKER20180713/linked-list-perf/actions/runs/36311407062)
中 GCC 与 Clang 两个任务均成功。后续提交仅补充本页、README 和运行日志，不修改已测试的程序。

## 已完成的验证

| 验证项 | 结果 |
| --- | --- |
| Linux GCC，C11，O2，警告视为错误 | 通过 |
| Linux Clang，C11，O2，警告视为错误 | 通过 |
| 双向循环链表和精确搜索单元测试 | 通过 |
| 1–512 字节 API 输入、二进制数据、重复值、首尾与未命中 | 通过 |
| 单链表 10000 节点、1000 条独立链表 | 通过 |
| perf 分组、事件 ID、复用缩放、错误读取、EINTR、资源清理测试 | 通过 |
| 5 个 Python 集成测试方法，含 40 组查询模式/数据模式/大小组合 | 通过 |
| 两种编译器的 AddressSanitizer 和 UndefinedBehaviorSanitizer | 通过 |
| 固定种子的普通版本与优化版本结果校验 | 通过 |

另外，在本地 Windows 使用官方 Zig 0.16.0 携带的 C 编译器运行了链表单元测试，并交叉编译出 Linux x86_64 可执行文件。该结果仅用于补充验证，课程要求的 GCC 编译与 Linux 测试以上面的 Actions 记录为准。

## 实验日志

日志由程序真实执行生成，保留原始内容：

- [GCC 常规实验与环境信息](runs/2026-09-27/gcc_0927_1.log)
- [GCC 提高权限后的 PMU 探测](runs/2026-09-27/gcc_privileged-pmu.log)
- [Clang 常规实验与环境信息](runs/2026-09-27/clang_0927_1.log)
- [Clang 提高权限后的 PMU 探测](runs/2026-09-27/clang_privileged-pmu.log)

常规实验覆盖头部、尾部、随机命中、未命中、混合查询，以及普通和相同前缀两类数据。提高权限后的实验使用 4 条链表、每条 10000 节点、64 字节内容、2000 次查询和 3 次重复；每轮两种算法校验和一致。

## 尚需支持 PMU 的实机验证

GitHub 的 Azure 虚拟机没有向本次任务提供所请求的硬件 PMU 事件。普通执行和 sudo 执行均返回 `ENOENT`（errno=2）；硬件计数及命中率显示 `N/A`，严格模式退出码为 2。已经验证程序能正确报告该情况。

因此，本仓库交付的是完成并经过功能测试的计数器实现，**不声称已经取得四项硬件指标的实测数值**。日志中的墙钟耗时和校验和是真实结果；模拟计数器测试的数值不是实测数据。CI 通过表示编译、功能、内存安全和异常处理通过，不表示虚拟机提供了 PMU。

在支持这些事件并具有适当权限的 Linux 实机上，用以下命令补齐硬件结果：

```bash
make clean && make
mkdir -p results
set -o pipefail
./build/linkbench --lists 4 --nodes 10000 --size 64 --queries 2000 --repeat 5 --require-perf \
  | tee "results/$(date +%m%d)_hardware.log"
```

完整日志应显示每轮 `hardware_metrics: complete` 且命令退出码为 0。若只支持部分事件，仍需根据具体处理器 PMU 选择支持的测量环境，不能用耗时或节点访问次数冒充指令数、周期数或硬件访存数。
