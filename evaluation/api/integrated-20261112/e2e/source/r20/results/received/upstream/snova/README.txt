# SNOVA

This folder contains the digital content of the proposal

** SNOVA **

submitted to the NISTPQC: Call for Additional Digital Signature Schemes for the Post-Quantum Cryptography Standardization Process

by

Lih-Chung Wang,
Chun-Yen Chou,
Jintai Ding,
Hao Guo,
Yen-Liang Kuan,
Jan Adriaan Leegwater,
Ming-Siou Li,
Peigen Li,
Bo-Shu Tseng,
Po-En Tseng,
Chia-Chun Wang.

## This folder contains the subfolders
1. Statements
2. Supporting_Documentation
3. KAT
4. Optimized_Implementation
5. Reference_Implementation
6. Additional_Implementations

## Statements
This folder contains the files "Statement by Each Submitter.pdf" and "Statement by Reference and Optimized Implementations Owner(s).pdf" containg the intellectual property statements of each submitters (aim at the requirement in Sec. 2.D).

## Supporting_Documentation
This folder contains the file SNOVA_Round3.pdf, containing the algorithm specification, performance analysis and performance data, parameter settings, security analysis and advantages and limitations (aim at Sec.2.B.1, 2.B.2, 2.B.4, Sec.2.B.5, Sec.2.B.6 of the requirement, respectively) of the SNOVA scheme.

## KAT
This folder contains the Known Answer Test values.

## Reference_Implementation
This folder contains the C-code of our Reference implementation of SNOVA.

## Optimized_Implementation
This folder contains the C-code of our implementation of SNOVA optimized for x64/amd64 architecture.

## Additional_Implementations
This folder contains the source code of other implementations of SNOVA:
1. Optimized for AVX2 architecture;
2. Optimized for GFNI architecture;
3. A version of SNOVA in SageMath;
4. A generic version of SNOVA in C supporting all parameter sets, also all of the odd field orders mentioned in the alternative parameters section.

The AVX2 and GFNI versions (1. and 2.) have been used to create the performance numbers in the Supporting Document
