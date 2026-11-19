# 五算法完整路径增量优化归档

日期：2026-11-12。按“只需实现并归档，不用仿真”完成。
本目录为独立源码快照，以 `xmpk-final/e2e` 的 989 个文件为基线。
完整算法源代码、运行时和构建入口都位于本目录；外部依赖为已有
RISC-V GCC/picolibc。必要的 CVA6 linker、encoding/util header 已收入
`build-support`，无需访问原算法目录即可构建。

## 集成范围

| 算法/参数 | 完整路径接入点 | 本轮优化与边界 |
|---|---|---|
| MAYO-1/5 | `upstream/mayo/src/generic/generic_arithmetic.h` 的 `mul_add_mat_trans_x_m_mat`、`mul_add_mat_x_m_mat`、`P1_times_O` → runtime `xmpk_gf16_packed_gemm_add` | A 按完整 word 构造，支持转置和普通 scalar stride；连续且对齐的 B 直接引用，否则拷贝；省去 C clear；保留 XOR 累加和输出 stride |
| MQOM L1/L5 | Cat1/Cat5 `piop/piop_default.c` → runtime `xmpk_gf256_matvec_batch` | 下三角 TRI_INF=2 用 MUL8 dot；固定公开边界、尾部 mask、横向 XOR；N 为 8 的倍数且 ≤128、batch≤3；dense/upper 路径保留 |
| SNOVA L1/L5 | `work/source/snova/snova_ref.c` 的 KeyGen/Sign prepared LINXFM 调用 | right/transform 按完整低 nibble word 构造；每列输出一次展开，去除 C clear；保留 add/overwrite、byte stride 和复用语义 |
| UOV-Is（GF16） | `upstream/uov/src/parallel_matrix_op.c` 的 KeyGen/Sign batch matvec 调用 | nibble window shift/mask，奇数偏移第九字节有界装载；对齐连续 B 直接引用；输出按 word XOR |
| UOV-V（GF256） | 原有完整 GF256 QGEMM/batching 路径 | GF16 nibble 优化不适用于该字段；保留已有加速实现，并纳入完整构建归档 |
| QR-UOV L1/L5（q127、扩域 L=3） | `crypto_sign_keypair` → `qruov_keygen` → `qruov_keygen_refined`；Sign → `sign_build_system`；Verify → `qruov_verify` | KeyGen 使用 mask13 的单方程 direct/triangle/fused 思路；Sign/Verify 使用 mask7 的 striped MxV、coefficient0 dot、固定乘积 contraction；遍历全部方程，无 trial 计时或 trial 方程限制 |

`upstream` 的根为
`e2e/source/r20/results/received/upstream`；共享运行时为
`e2e/work/runtime/xmpk_asm.c/.h`。QR 新实现为
`qruov/Reference_Implementation/ref/xmpk_refined.c/.h`。
既有 scalar、尺寸不支持时的回退，以及未使用这些 helper 的算法阶段仍保留。
新增优化没有将所有标量操作替换为硬件操作，也不代表所有阶段都获得加速。

## QR-UOV 语义和清理

- KeyGen 全程只准备一次 secret A；复用单方程的 Q/R workspace。
  二次项只生成 M(M+1)/2 个输出；融合 `-T + R + Rᵀ`。
  7-bit 写入使用完整公钥的全局 coefficient index，保持方程边界共用
  字节时的正确布局。失败后清除部分输出与工作区，重新走原 KeyGen。
- Sign 的 sdt 只 pack 一次；P2 直接形成转置 striped 输入。
  保留 PRG 方程顺序、doubling、permutation、线性求解、hash/seed、
  盐与签名生成及 canonicalization。
- Verify 在全部方程外生成 vinegar×oil 与加权上三角 oil×oil。
  保留 checked P3 解码、消息 hash、canonical 校验和目标比较；
  对角项一次，非对角项两次。
- 新 QR 路径仅适用于 q127、L=3、fc=fc0=fe=1、M/V≤128；
  compile-time assertion 约束 dot 累加不溢出。
  L=10 或其他字段/多项式参数保留原路径。
- EXT 工作区 high-water 记录在生产构建中也开启；新 KeyGen 清除
  caller-owned Q/R/T 和 prepared A。Sign 清除新增 striped secret
  缓冲区；prepared API 的固定静态 workspace 仍沿用单线程使用约定。
  旧实验用 `qruov_keygen_xmpk_chunk` 保留，完整 optimized 入口已转入
  新完整 KeyGen 路径。

为使独立快照可构建，补齐了两处裸机适配：UOV 的 config 在
`XMPK_BAREMETAL` 下使用随源码提供的 FIPS202，而不默认启用 OpenSSL；
QR 的 prim.h 补回既有裸机 backend 使用的 Keccak state/AES key schedule
声明，普通构建保留原 OpenSSL backend。这些是构建适配，不计作性能优化。

## 验证与复现

本轮只允许静态、编译与链接验证；不执行算法，不运行 native 模拟、
Spike、QEMU、Verilator 或 RTL。原有长仿真没有重启或替换输入。
详细结果见 `validation`。全算法输出等价与实际性能未在本轮测量；
十个完整 ACCEL=1 ELF 全部编译链接通过，静态检查和补丁 dry-run 通过。
旧短测报告描述的是历史实验，不能视为这份完整集成代码已经实测。
论文和原有 cycle 数据未修改。

```bash
# 配置已有编译器；脚本不会安装依赖，也不会执行生成的 ELF。
export TOOLCHAIN_BIN=/path/to/existing/toolchain/bin
export CC="$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc"
export NM="$TOOLCHAIN_BIN/riscv64-unknown-elf-nm"
export SIZE="$TOOLCHAIN_BIN/riscv64-unknown-elf-size"
bash static_check.sh
bash build_only.sh
sha256sum -c SHA256SUMS
```

`build_only.sh` 启用 ACCEL=1，顺序编译五个算法的 L1/L5 共十个 ELF，
默认使用归档中的 build-support。QR 构建脚本已链接新实现。
`static_check.sh` 还覆盖 QR L1/L5 的 ACCEL=0/1 和 L=10 回退。
重新构建会改变 build/log 文件，校验 SHA256SUMS 应在重新构建前执行。

## 归档内容

- `BASELINE_MANIFEST.json`：修改前的完整快照哈希。
- `SOURCE_MANIFEST.json`、`CHANGES.json`：集成后源码哈希与变更清单。
- `patches/full-integration.patch`：相对原基线的完整增量补丁，
  可在同结构快照根目录 `patch -p1`；新增文件也包含在内。
- `patches/before/`：修改文件的原版副本，便于逐文件比较。
- `e2e/build/`、`validation/`：本轮完整编译 ELF 和静态/链接日志。
- `SHA256SUMS`：所有归档文件（清单自身除外）的 SHA-256。
- 原历史短测代码和文档保留在 `e2e/work/tests` 与 `e2e/work`，
  仅作优化出处记录，不由本轮两个验证脚本执行。
