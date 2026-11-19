This folder contains the digital content of the proposal.

*****  QR-UOV  *****,

submitted to the NIST Post-Quantum Cryptography Standardization Process

by

Hiroki Furue,
Yasuhiko Ikematsu,
Fumitaka Hoshino,
Tsuyoshi Takagi,
Renma Sugai,
Hiroshi Amagasa,
Naofumi Homma,
Rei Ueno,
Kosuke Sakata,
Haruhisa Kosuge,
Kimihiro Yamakoshi,
Koha Kinjo,
Rika Akiyama,
Satoshi Nakamura,
Shingo Orihara and
Yuto Kawahara.


This folder contains the subfolders.

・IP_Statement
・KAT
・Optimized_Implementation
・Reference_Implementation
・Supporting_Documentation

--------------------------------------------------------------------
IP_Statement
This folder contains IP statements by each submitter.
--------------------------------------------------------------------
KAT
This folder contains the Known Answer Test values of our implementation.
--------------------------------------------------------------------
Optimized_Implementation
This folder contains the C code for two implementations of QR-UOV: our optimized portable implementation and our AVX2/BMI2 implementation for the x86-64 architecture. More information on the content of this folder as well as an explanation of how to use the code can be found in Optimized_Implementation/README_optimized.txt.
--------------------------------------------------------------------
Reference_Implementation
This folder contains the C code of our reference implementation of QR-UOV. More information on the content of this folder as well as an explanation of how to use the code, can be found in Reference_Implementation/README_reference.txt.
--------------------------------------------------------------------
Supporting Documentation
This folder contains the file qruov_Spec-v3.0.pdf, containing the algorithm specification, implementation details, performance data, security analysis as well as advantages and limitations of our scheme. This folder also contains intermediate values, which can be used to check the implementation.
--------------------------------------------------------------------

Package Contents

QR_UOV
├─ IP_Statement
│   └── qruov-ip-statements-round3.pdf
│
├─ KAT                             -- One set of KATs per parameter set
│   ├── kat_aes.zip                     -- KATs for AES option
│   └── kat_shake.zip                   -- KATs for SHAKE option
│
├─ Optimized_Implementation        -- Optimized implementations (portable and AVX2)
│   ├── avx2
│   ├── opt
│   └── README_optimized.txt
│
├─ Reference_Implementation        -- Unoptimized reference implementation
│   ├── ref
│   └── README_reference.txt
│
├─ Supporting_Documentation
│   ├── changelog.pdf                   -- Change log
│   ├── Intermediate_Values             -- Intermediate values per parameter set
│   ├── qruov_Cover_Sheet.pdf           -- Cover Sheet
│   └── qruov_Spec-v3.0.pdf             -- Specification document
│
└── README.txt   -- This file