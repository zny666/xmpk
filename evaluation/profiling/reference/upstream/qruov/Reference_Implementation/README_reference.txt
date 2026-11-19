# QR-UOV Round 3 Reference Implementation

This directory contains the straightforward reference C implementation of QR-UOV.

## Requirements

The following software is required:
A C compiler, 
GNU Make, 
OpenSSL libcrypto 3.0 or later. 

OpenSSL 3.3 or later is recommended for EVP_DigestSqueeze.
On macOS or BSD, install GNU Make and use "gmake" instead of "make".

## Building

Change to the ref directory:
cd ref

Then run:
make

The default configuration is:
PARAM=1q127L3
PRG=aes

To select a different parameter set or public-seed expansion method:
make PARAM=<parameter set option> PRG=<PRG option>
e.g. make PARAM=3q31L10 PRG=shake

The generated executable is placed in:
build/<PARAM>-<PRG>/PQCgenKAT_sign

To display the supported parameters, PRG options, and build commands:
make help

To remove generated build files, run:
make clean

On macOS or BSD, replace "make" with "gmake".

## Security Notice

This implementation is intended for reference and interoperability testing. It does not include the timing-attack countermeasures described in Appendix A of the specification.