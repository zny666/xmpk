# MAYO L1 baseline 原始结果恢复

2026-11-12 复核发现：论文 CSV 仅保留优化側记录，漏导入已完成的历史 baseline。
并非 baseline 仍在运行。历史日志位于另一台仿真服务器 REDACTED_IP，
当前本地/REDACTED_IP 的结果目录未包含这一份记录。

原始运行：
`/reviewer/riscv/xmpk-final-runs/sim-final-20261108T201758/e2e/results/baseline/mayo1-baseline.*`

运行 wrapper 记录开始时间 2026-11-09 10:54:42 +08:00，
PASS 时间 2026-11-09 15:01:43 +08:00。
guest 输出标识 `MAYO / MAYO-1 / cva6-ref-portable`。

| API | rdcycle 原始十六进制 | cycle | minstret |
|---|---|---:|---:|
| KeyGen | 0x01437eae | 21,200,558 | 19,938,788 |
| Sign | 0x02239a13 | 35,887,635 | 32,927,656 |
| Verify | 0x01282952 | 19,409,234 | 18,117,378 |
| API 合计 | 三阶段求和 | 76,497,427 | — |

原始 guest 日志包含三个 `E2E_METRIC` 和 `E2E_SUCCESS`；
主日志确认 `*** SUCCESS *** (tohost = 0) after 95913144 cycles`。
退出 cycle 95,913,144 含初始化、篡改验签、打印和 HTIF 等，不能当作 API 总周期。
相同 32-byte 消息、确定性 RNG seed `0x243f6a8885a308d3`，
测量范围为一次 API 调用，分配与 fixture 初始化在计时之外。

原始 ELF SHA-256：
`55b057f1a3e8fdd2acea1d098836a53414401cc49e12b8833225f567bb62de4e`。

原始 `mayo.c` 和 `mayo_adapter.c` 与当前冻结归档 SHA-256 相同。
`arithmetic.c` 唯一差异为加速分支条件：
`#ifdef XMPK_ACCEL` 改为 `#if defined(XMPK_ACCEL) && defined(XMPK_MAYO_QGEMM)`。
Baseline 不定义 XMPK_ACCEL，因此该差异不改变 baseline 路径。

日志、wrapper、ELF 和原始 arithmetic.c 已持久归档于：
`/artifact/xmpk-arm64-native/formal-results-secondary/mayo1-baseline-recovered-20261112/`。

本次仅回填 baseline 和原始比例。现有外部确认的 Sign 增量因子保持原值，
由生成器从 pre-refinement 21,769,944 cycle 计算为 20,756,964 cycle，
仍明确标为 factor-derived。更新后旧 Xmpk 总体比例为 1.346521×，
当前含已确认增量的 API 总体比例为 1.370967×。
