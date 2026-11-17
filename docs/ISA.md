# Xmpk ISA and data layout

The adopted design is RV64 with three GPR read ports. Operand 3 is the **old destination register**; the host uses its existing `MUX_RD_RS3` custom-offload path. `GF16_MACC`, `ROWELIM16`, `BGER16R`, `AGER16R2` and three-address SETUP instructions depend on this mapping. The [encoding table](ENCODINGS.md) specifies all 37 entries and NOP. Enum members absent from that table are legacy internal values and have no released instruction encoding.

`software/xmpk_asm.h` supplies `xmpk_<opcode>(rs1, rs2, old_rd)` for every table entry. Functions with writeback return the new destination value; non-writing instructions return void. These raw interfaces accept configuration words and addresses. They use fixed a0/a1/a2 registers, `.word` encodings and a compiler memory barrier plus `fence rw,rw`. `software/example.c` shows packed multiplication and a one-row QGEMM. Build the example using `scripts/compile_benchmarks.py`.

## Arithmetic

Little-endian packing puts lane 0 in the least significant bits. GF(16) uses sixteen 4-bit lanes and polynomial x⁴+x+1 (`0x13`). AES-GF(256) uses eight byte lanes and x⁸+x⁴+x³+x+1 (`0x11b`). GF(127) inputs are canonical bytes 0–126.

MUL instructions multiply corresponding packed lanes. GF16_MACC XORs the product with old(rd). VBROAD selects nibble `rs2[3:0]` or byte `rs2[2:0]` and broadcasts it. XORFOLD reduces lanes into lane 0. GF256_INV maps zero to zero. FNZ returns the first nonzero lane, or 16/8 for an all-zero word. GF16_DOT returns the XOR of packed GF(16) products. PIVOT16 selects `rs1[4*rs2[3:0]+:4]` and returns `{first_nonzero_index[4:0], selected_value[3:0], inverse[3:0]}` in bits 12:0. ROWELIM16 returns old(rd) XOR rs1 multiplied by a broadcast of `rs2[3:0]`.

## SETUP, RUN and completion

SETUP records addresses and completes without architectural writeback. RUN completes with a workload count on success. The autonomous engines return `0x8000000000000001` for malformed RUN configurations and `0x8000000000000000` for a memory/engine failure. SOLVEQ uses the latter status for a singular system as well. An error can leave a partially written output. Reissue SETUP and RUN to recover, and discard output from the failed command. Issue instructions only when the coprocessor is ready; accept each completion once and match its hart/id/destination tags. The test wrapper checks those tags.

Memory requests are aligned 64-bit transfers, with one response for every accepted request. The CPU adapter translates virtual addresses through the LSU and rejects translation/PMP errors and noncacheable addresses. Memory errors propagate to the command status. The host must provide coherent memory and normal memory-ordering barriers.

| Engine | SETUP rs1/rs2/old rd | RUN rs1 configuration | Success count |
|---|---|---|---|
| QGEMM | A / B / C | M bits 7:0; K 15:8; Nwords 27:16; field mode 30:28 | M×K×Nwords×lanes |
| MQEVAL | Q / X / Y | N 7:0; M 15:8 | M×N×(N+1)/2 |
| SOLVEQ | augmented matrix / solution / unused | N 6:0; bit 7 must be zero; bit 8 selects GF(256), otherwise GF(16) | N |
| LINXFM | T / X / Y | L 4:0; count 15:8 | L×count |
| EXTGEMM | A / B / C | M 7:0; K 15:8; N 23:16; bit 24 selects degree 10, otherwise 3 | M×K×N×L² |

QGEMM field modes are 0 GF(16), 1 AES-GF(256), 2 F7, 3 F31, 4 F127. M and K are nonzero byte dimensions. Nwords is 1–4095. A is row-major, each row padded to `ceil(K/lanes)` words. B is K rows of Nwords packed words; C is M rows of Nwords words, overwritten by the product.

MQEVAL accepts N=1–128 and M=1–255. Q has one upper-triangular matrix per equation, traversed `(0,0),(0,1),…,(1,1),…`. Each equation begins at an 8-byte boundary with `ceil(N(N+1)/32)` words. X and Y pack sixteen nibbles per word; the last Y word is zero padded.

SOLVEQ accepts GF(16) N=1–64 and GF(256) N=1–96. Each row contains N coefficients followed by its RHS, padded to `ceil((N+1)/lanes)` words. The solution is a packed vector. Pivot search and singular-input handling in this adopted implementation are data dependent.

LINXFM accepts L=1–16 and count=1–255. T contains L packed row words. Each input/output vector occupies one 8-byte word. Output nibbles above L are zero.

EXTGEMM stores each element in a 16-byte slot, with L canonical F127 coefficients at increasing byte offsets. Degree 3 writes bytes 0–7 (bytes 3–7 zero) and leaves bytes 8–15 unchanged. Degree 10 writes the full slot with bytes 10–15 zero. Initialize the unused high word before comparing a complete degree-3 slot. A, B and C are row-major. Reduction uses x³=x+1 or x¹⁰=2x³+1. All dimensions are nonzero bytes. Packed 3-byte input slots are outside this adopted interface.

## Tile register and GER instructions

TRF storage is explicitly initialized by software; reset does not erase the RAM banks. The following configuration fields are supplied in rs2. Minus-one fields encode one through eight words/rows, except AGER16X16/R2's four-bit row field.

| Instruction | rs1 / old rd | rs2 fields |
|---|---|---|
| TRF_WRITE | data / unused | bit 3 selects C; bits 2:0 B index or low C index; bits 5:4 high C index in legacy mode |
| TRF_READ | unused / unused | same bank/index mapping as WRITE; autonomous C mapping described below |
| GER16 | packed scalars / unused | lane 3:0; B base 6:4; C base 11:8; words−1 14:12 |
| GER16R | packed row scalars / unused | words−1 2:0; rows−1 6:4; B base 10:8; C base 15:12 |
| BGER16R | B word / packed row scalars | word index 2:0; words per row−1 6:4; rows−1 10:8; C base 15:12 |
| ATRF_LOAD | packed A row word / unused | row 2:0; word index 3 |
| AGER16R | B word / unused | C word 2:0; words per row−1 6:4; rows−1 10:8; C base 15:12; A column 20:16 |
| ATRF_LOAD16 | packed A row word / unused | row 3:0; word index 4 |
| AGER16X16 | B word / unused | C word 2:0; words per row−1 6:4; rows−1 11:8; C base 16:12; A column 21:17 |
| AGER16R2 | first B word / second B word | same fields as AGER16X16; updates a pair of words per row |

The update equation is `C[row,word] ^= gf16_mul(A[row,column], B[word])` across each packed nibble lane. GER16 uses one selected scalar for a contiguous word range; GER16R/BGER16R supply up to eight packed row scalars directly. The AGER paths fetch row scalars from the transposed A storage.

TRF_WRITE takes data in rs1 and the TRF address in rs2. TRF_READ uses rs2 and returns the word. GER variants XOR packed outer products into the selected C tile. ATRF_LOAD/LOAD16 load rows into transposed scalar storage. BGER16R consumes row scalars from old(rd), and AGER16R2 consumes the second B word from old(rd). Initialize every B/C/A location touched by a command, keep the configured tile within its bank, and wait for each completion before reading or updating it.

GER_SETUP takes A/B bases. GER_RUN accepts 16 or 32 and computes an N×N packed GF(16) product into internal C storage, returning N². A/B are row-major with one or two packed words per row. In autonomous mode, TRF_READ selects C with bit 3 and addresses the linear output word through `{rs2[6:4],rs2[2:0]}`. Legacy GER updates return the number of tile words, while ATRF loads return 16. XMEM_STORE takes an address/data pair and returns zero on success. Their detailed schedule is in `xmpk_auto_aggressive` in the same RTL file.
