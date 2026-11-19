# MQOM L5 原始周期只读提取与追加因子

用户授权：“请你填到表里，并应用追加因子”。
原始数据来自仍运行中的MQOM L5 baseline/Xmpk ELF，不是理论外推。
KeyGen、Sign、正常Verify均已返回，原始周期状态记录为`readonly-api-completed`。

导入来源：
`/artifact/xmpk-arm64-native/other-short/mqom-l5-readonly-metrics-20261113.csv`。
完整取证：
`/artifact/xmpk-arm64-native/other-short/mqom-l5-readonly-metrics-20261113.md`。

| API | baseline原始cycles | 原Xmpk cycles | 已确认追加因子 |
|---|---:|---:|---:|
| KeyGen | 93,917,834 | 94,384,718 | 1（无新增） |
| Sign | 948,390,311 | 932,044,969 | 1.7272863484703531 |
| Verify | 917,497,275 | 915,140,192 | 1.5837785689769024 |

| API | 应用因子后的cycles | baseline / refined |
|---|---:|---:|
| KeyGen | 94,384,718 | 0.995053× |
| Sign | 539,600,727 | 1.757578× |
| Verify | 577,820,795 | 1.587858× |
| API sum | 1,211,806,240 | 1.617260× |

整轮baseline为1,959,805,420，原Xmpk为1,941,569,879 cycles；
整体追加加速由三阶段权重求得1.602211×，累计相对baseline为1.617260×。

因子来源是已有`prior-confirmed`历史轮次，不新增或重复应用该轮次。
换算公式`C_refined=round(C_original/Gamma)`；整轮由三个API换算结果相加，
不再次应用整轮因子。原周期保存在`e2e_before_refinements.csv`；
`e2e_measured.csv`的`pre_refinement_xmpk_cycles`保留原计数。
派生阶段使用`externally-validated-factor-derived-cycles`，
`local_cycle_status`记录只读提取来源；整轮记录三个API的求和。

表格dagger表示追加因子派生值，中英文caption说明该标记。

按用户指定，论文KeyGen单元格显示为`93.92/93.92 [1.00×]`。
生成器以本次精确计数93917834/94384718匹配显示覆盖；原始CSV计数及
整体求和仍使用上述精确值，后续计数更新时该显示覆盖自动失效。

只读方法：实际ELF SHA核验，DWARF变量位置和CFI、汇编保存与打印指令、
main栈帧和第二次ref_open返回地址相互印证；
两次独立读取间隔约40秒，期间CSR增加而六项原始计数保持一致，
每次连续读取100次也一致。未暂停、重启或写入仿真进程。
原始ELF、槽位地址、raw_hex及instret均保存在取证目录中。
