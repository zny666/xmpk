# 本轮实现验证

本轮未执行任何算法、native instruction emulation 或仿真。

## 已完成静态检查

使用既有远端 `riscv64-unknown-elf-gcc`，版本
`12.2.0-14+deb12u1+11+b2`，RV64GC、lp64d，GNU11。
`static_check.sh` 的 C 检查启用 `-fsyntax-only -Wall -Wextra -Werror`。

| 检查 | 结果 |
|---|---|
| 修改后的完整 xmpk_asm.c | PASS，无 warning |
| QR 1q127L3，ACCEL=0/1 | 两项 PASS，无 warning |
| QR 5q127L3，ACCEL=0/1 | 两项 PASS，无 warning |
| QR 1q127L10，ACCEL=0/1 | 两项 PASS，无 warning，保留回退 |
| 本轮验证脚本与原十个完整构建脚本 bash -n | PASS |
| 原基线 989 文件 SHA-256 | 全部与修改前 manifest 相同 |
| full-integration.patch 在原基线 patch --dry-run -p1 | 十个修改/新增文件全部可应用 |

## 完整编译链接

最终结果和链接大小见 `build_*.log`。编译使用 `-O3 -fno-builtin`
`-fno-lto -fno-strict-aliasing -fno-tree-vectorize -fno-tree-slp-vectorize`
`-march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d -mcmodel=medany`。
CRT、picolibc、链接脚本按完整 API 构建，每项检查 `nm -u` 无未解析符号。

| 完整 ACCEL=1 ELF | 编译/链接 |
|---|---|
| MAYO-1、MAYO-5 | 两项 PASS |
| MQOM-L1、MQOM-L5 | 两项 PASS |
| SNOVA L1、SNOVA L5 | 两项 PASS |
| UOV-Is、UOV-V | 两项 PASS |
| QR-UOV L1、QR-UOV L5 | 两项 PASS |

`symbols.log` 再次核对十个 ELF 无未解析符号，两个 QR ELF 均包含
`qruov_keygen_refined`。`static-check.log` 记录最终代码的静态检查输出；
两个 QR ELF 已在补齐签名生成末尾的 EXT workspace 清理后重新编译。

第一次构建发现 UOV 默认 OpenSSL 和 QR 缺失 primitive 裸机声明；
已补齐归档内适配。最终日志来自修复后的完整源码。
链接脚本若报告 RWX LOAD segment，属于既有裸机链接布局，不影响
本轮 compile/link 成功的判定。

## 本轮未验证

未运行 KeyGen/Sign/Verify，未比较公钥/签名/验签的全算法输出，
未测量 cycle、instructions、端到端加速或完整算法新增内存开销。
历史热点短测通过不等同于本次完整集成已通过功能仿真。
本归档应标为 **implemented, compile/link checked, not executed**。
