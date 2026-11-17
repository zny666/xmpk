# Instruction encodings

Generated from `rtl/include/cvxif_instr_pkg.sv`. There are 37 Xmpk entries and one inherited NOP.
All encodings use custom-3 (`0x7b`). Match `(instruction & mask) == (encoding & mask)`.
Operands 1/2/3 are rs1/rs2/old(rd). Read bits below are listed in that order.
Register fields remain variable, including GF16_MACC's intentionally unmasked high bits.

| Opcode | Base encoding | Mask | Read rs1/rs2/old rd | Write rd |
|---|---|---|---|---|
| NOP | 0x0000007b | 0xfe00707f | 0/0/0 | no |
| GF16_MUL | 0x0800107b | 0xfe00707f | 1/1/0 | yes |
| GF16_MACC | 0x0200107b | 0x0600707f | 1/1/1 | yes |
| GF256_MUL | 0x1000107b | 0xfe00707f | 1/1/0 | yes |
| GF127_MUL | 0x1800107b | 0xfe00707f | 1/1/0 | yes |
| VBROAD16 | 0x2000107b | 0xfe00707f | 1/1/0 | yes |
| VBROAD256 | 0x2800107b | 0xfe00707f | 1/1/0 | yes |
| XORFOLD16 | 0x3000107b | 0xfe00707f | 1/0/0 | yes |
| XORFOLD256 | 0x3800107b | 0xfe00707f | 1/0/0 | yes |
| GF256_INV | 0x4000107b | 0xfe00707f | 1/0/0 | yes |
| FNZ16 | 0x4800107b | 0xfe00707f | 1/0/0 | yes |
| FNZ256 | 0x5000107b | 0xfe00707f | 1/0/0 | yes |
| TRF_WRITE | 0x5800107b | 0xfe00707f | 1/1/0 | no |
| TRF_READ | 0x6000107b | 0xfe00707f | 0/1/0 | yes |
| GER16 | 0x6800107b | 0xfe00707f | 1/1/0 | yes |
| GER16R | 0x7000107b | 0xfe00707f | 1/1/0 | yes |
| BGER16R | 0x7800107b | 0xfe00707f | 1/1/1 | yes |
| ATRF_LOAD | 0x8000107b | 0xfe00707f | 1/1/0 | yes |
| AGER16R | 0x8800107b | 0xfe00707f | 1/1/0 | yes |
| ATRF_LOAD16 | 0x9000107b | 0xfe00707f | 1/1/0 | yes |
| AGER16X16 | 0x9800107b | 0xfe00707f | 1/1/0 | yes |
| AGER16R2 | 0xa000107b | 0xfe00707f | 1/1/1 | yes |
| GF16_DOT | 0xa800107b | 0xfe00707f | 1/1/0 | yes |
| PIVOT16 | 0xb000107b | 0xfe00707f | 1/1/0 | yes |
| ROWELIM16 | 0xb800107b | 0xfe00707f | 1/1/1 | yes |
| GER_SETUP | 0xc000107b | 0xfe00707f | 1/1/0 | no |
| GER_RUN | 0xc800107b | 0xfe00707f | 1/0/0 | yes |
| XMEM_STORE | 0xd000107b | 0xfe00707f | 1/1/0 | yes |
| QGEMM_SETUP | 0xd800107b | 0xfe00707f | 1/1/1 | no |
| QGEMM_RUN | 0xe000107b | 0xfe00707f | 1/0/0 | yes |
| MQEVAL_SETUP | 0xe800107b | 0xfe00707f | 1/1/1 | no |
| MQEVAL_RUN | 0xf000107b | 0xfe00707f | 1/0/0 | yes |
| SOLVEQ_SETUP | 0x0800207b | 0xfe00707f | 1/1/0 | no |
| SOLVEQ_RUN | 0x1000207b | 0xfe00707f | 1/0/0 | yes |
| LINXFM_SETUP | 0x1800207b | 0xfe00707f | 1/1/1 | no |
| LINXFM_RUN | 0x2000207b | 0xfe00707f | 1/0/0 | yes |
| EXTGEMM_SETUP | 0x2800207b | 0xfe00707f | 1/1/1 | no |
| EXTGEMM_RUN | 0x3000207b | 0xfe00707f | 1/0/0 | yes |

See [ISA usage](ISA.md) for configuration words, memory layouts and return values.
