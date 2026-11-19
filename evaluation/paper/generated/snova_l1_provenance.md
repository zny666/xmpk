# SNOVA-L1 原始三阶段结果恢复

缺项来自归档/导表遗漏，不是仿真未完成。
此前 `e2e_before_refinements.csv` 仅保存整轮
120392609 / 104727096 cycles，没有收录三阶段。
通用收集脚本使用 `snova1-xmpk` 标签；最优运行实际是
另一台服务器、另一目录中的 `snova1-batch2`。
普通 QGEMM 和未批处理 LINXFM 是另外两个较慢版本，不应替换已选最优数据。

原始服务器：REDACTED_IP。

- baseline：
  `/reviewer/riscv/xmpk-final-runs/sim-final-20261108T201758/e2e/results/baseline/snova1-baseline.*`
- optimized：
  `/reviewer/riscv/xmpk-snova-batch-20261109/e2e/results/optimized/snova1-batch2.*`

两组 guest 输出均包含 `SNOVA / SNOVA_I_B_AES`、
完整三个 `E2E_METRIC` 和 `E2E_SUCCESS`；主日志均确认 `tohost=0`。
baseline implementation 为 `cva6-ref-portable`，
optimized 为 `cva6-xmpk-gf16-linxfm-batch`。

| API | baseline 十六进制 | baseline cycles | batch2 十六进制 | batch2 cycles | baseline/batch2 |
|---|---|---:|---|---:|---:|
| KeyGen | 0x00f52508 | 16065800 | 0x00c82bdf | 13118431 | 1.224674× |
| Sign | 0x0495c24e | 76923470 | 0x03d5d853 | 64346195 | 1.195463× |
| Verify | 0x01a2244b | 27403339 | 0x019ffe06 | 27262470 | 1.005167× |
| API sum | — | 120392609 | — | 104727096 | 1.149584× |

API sums 与此前登记的整轮值逐 cycle 相同。
主日志的退出计数284981262 / 131993179包含初始化、额外负向验证和输出等待，
不用于 API 周期，亦不替代上述求和。
优化运行 wrapper 记录开始于2026-11-09 14:39:31、
PASS于2026-11-09 20:09:32。

现存运行ELF时间为2026-11-09 14:37:21，早于启动；
其SHA-256为
`98c012aca5bc5e177f0fcf85d81c22b90b26b367cad852c06b6030e9f055e0b9`。
源码复核发现运行启动后15:02修改过 xmpk_asm.c 和 matmul test；
这两份当前源码不再匹配 BATCH2_SOURCE_SHA256SUMS。
因此记录的源码指纹、已编译ELF与历史日志分别保存，
不把当前源码目录宣称为该运行的完整可复现快照。

原始日志、ELF、wrapper、BATCH2_SOURCE_SHA256SUMS 已归档：
`/artifact/xmpk-arm64-native/formal-results-secondary/snova1-recovered-20261113/`。
恢复数据通过 `tools/update_e2e_refinements.py --arrivals`
导入原始周期层；恢复时 SNOVA-L1 尚无已确认额外因子，
当时累计乘积保持1，未套用 SNOVA-V 的新增倍率。

实际记录的 L1 最优后端是 LINXFM batch2。
先前关于“L1默认QGEMM”的描述指20261112归档构建脚本的默认选项，
不能当成这份已选历史结果的后端。这是两个不同版本的来源。
本次恢复不构成新的 MACC 后端性能验证。

## 2026-11-13 用户确认新增优化实测

用户随后报告“我实际测试了一下，和你预估的完全一致”，并授权更新表格与论文。
本轮确认上一答复的圆整 API 追加倍率：KeyGen 1.49×、Sign 1.32×、
Verify 2.40×；报告整轮为1.52×。未收到新原始 cycle 计数。
因此保留上述原始 batch2 日志及周期，新增
`snova-l1-shape-followup-confirmed-20261113` 因子轮次；换算后周期明确
标记为 `externally-validated-factor-derived-cycles`，论文表格用 dagger。
整轮由更新后的三阶段求和，1.52×只作交叉校验，不重复乘入。
具体换算及用户确认口径见 `snova_l1_followup_confirmation.md`。
