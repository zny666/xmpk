# UOV L5 Xmpk 实测记录

2026-11-12 完成 `uov-v-xmpk-counter-clean` RTL 仿真；参数 UOV-V，
实现标识 `cva6-xmpk-gf256-qgemm`。这次运行修正了计数器读取，
不是后续全算法软件 refinement 的新版本运行；UOV-V 没有套用 UOV-Is 的增量因子。

| API | 实测 cycles（十进制） | 日志中的十六进制 | 实测 instret（十六进制） |
|---|---:|---|---|
| KeyGen | 883899625 | 0x0000000034af3ce9 | 0x0000000016bb71d7 |
| Sign | 11389188 | 0x0000000000adc904 | 0x00000000005d4778 |
| Verify | 16741971 | 0x0000000000ff7653 | 0x0000000000bd2a05 |
| API 合计 | 912030784 | — | — |

日志同时包含 `E2E_SUCCESS` 和 `SUCCESS (tohost = 0)`。
模拟器退出 cycle 为 928783952；API 合计只加三项计数，
不包含启动、HTIF host 输出等待或其他 harness 开销。
host wall time 32602.10 s 不作为算法 cycles。

原始归档：

- `/artifact/xmpk-arm64-native/formal-results/uov-v-xmpk-counter-clean/uov-v-xmpk-counter-clean.guest-output.log`
- `/artifact/xmpk-arm64-native/formal-results/uov-v-xmpk-counter-clean/uov-v-xmpk-counter-clean.log`
- `/artifact/xmpk-arm64-native/formal-results/uov-v-xmpk-counter-clean/time.txt`

写入 `e2e_before_refinements.csv` 后由 `tools/update_e2e_refinements.py`
生成 `e2e_measured.csv` 与论文行。基线 `uov-v-baseline` 在本次写表时仍运行
（VM PID 1753），因此表中保留 `run.`，不计算加速比。

## 本轮进一步优化的 L5 增量因子

按本轮已归档 refinement 的适用路径，UOV-V 保守计入
`gamma_keygen = gamma_sign = gamma_verify = gamma_whole = 1.000`；
新增优化可计入的 cycle 降幅为 0%。这是代码路径分析的中性取值，
不是新旧 L5 全流程的性能实测，不保证重编译后每个 cycle 完全相同。

- UOV-V 构建定义 `_OV256_259_96`，使用 GF(256)。
- 本轮 UOV 新增优化是 GF(16) 的 nibble window/零拷贝/word XOR；
  它适用于 UOV-Is，不进入 UOV-V 的 GF(256) 路径。
- 对照 refinement 归档的 `patches/before/e2e/work/runtime/xmpk_asm.c`
  和 `e2e/work/runtime/xmpk_asm.c`，`xmpk_gf256_matprod` 与
  `xmpk_gf256_matprod_batch` 的函数源码完全一致。
- 本轮 GF(256) 下三角 MUL8 新路径属于 MQOM 的
  `xmpk_gf256_matvec_batch(matrix_type=2)`；UOV-V 通过
  `gf256mat_prod_ref` 调用 `xmpk_gf256_matprod`，不进入该路径。

按阶段公式 `gamma = 1 / (1 - p*r)`，本轮新增优化在 UOV-V 的
有效覆盖占比 `p=0`，所以 `gamma=1`。不能把 UOV-Is KeyGen 的
1.150609625 倍因子转移到 UOV-V。论文现有 L5 实测 counts 保留，
不把该中性取值登记为外部仿真确认因子。

## 2026-11-16：L5 基线完成并补入论文

`restart-l5-o3-20261113/results/uov-baseline` 已完整结束，参数为 UOV-V。
`job.json` 记录 `state=passed`、`returncode=0`，
`guest.log` 记录三个 `E2E_METRIC` 和 `E2E_SUCCESS`；
`api-stages.json`、`metrics.csv` 与最终日志的计数一致。

| API | baseline cycles | 已有 Xmpk cycles | baseline/Xmpk |
|---|---:|---:|---:|
| KeyGen | 9656875166 | 883899625 | 10.93× |
| Sign | 76404978 | 11389188 | 6.71× |
| Verify | 17248892 | 16741971 | 1.03× |
| API 合计 | 9750529036 | 912030784 | 10.69× |

基线模型为 `release-t2`，SHA-256
`356d5ee74e6dd1199c7464fb4d95dbe3cdac8570ef591e346f7e01a80ed2da99`；
基线 ELF SHA-256
`bd390b1667edc878a4e2233ce91ef1db4f407a247fe867a28edd31508557b1db`。
基线使用 Verilator 5.008，应用编译器为 RISC-V GCC 12.2.0；
API 编译参数沿用论文实验方法中的 O3 配置。

优化版仍沿用上节 `uov-v-xmpk-counter-clean` 的三项已归档实测周期，
没有新增增量因子，也没有使用其他参数集的候选结果。
合计仅相加 KeyGen、Sign、Verify 的 API 计数；模型退出周期和主机耗时
不进入加速比。此处的基线记录补齐了上节历史状态中的等待项。

本轮归档副本：`sources/uov-l5-baseline-metrics.csv`、
`sources/uov-l5-baseline-job.json`、`sources/uov-l5-baseline-guest.log`。
标准化到达记录：`generated/uov_l5_baseline_arrivals.csv`。
