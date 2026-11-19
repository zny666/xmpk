"""Build the eleven pinned MAYO/UOV/SNOVA reference profiles, never run them.

The caller owns command logging, ISA audits, dependency hashing and sanity runs.
All manifest paths except compile_commands paths are relative to root. No Make or
CMake defaults, cached/expanded-key adapters, KAT RNGs or downloaded sources are
used. Importing this module has no filesystem or subprocess side effects.
"""

from pathlib import Path
import os


# Undefine selectors, not architecture identity macros needed by libc headers.
# In particular, preserve upstream scalar/SWAR code rather than replacing it.
_HARDWARE = (
    "ENABLE_AESNI", "ENABLE_AESNEON", "TARGET_AMD64", "TARGET_X86",
    "TARGET_ARM", "TARGET_ARM64", "__AVX__", "__AVX2__", "__AES__",
    "__VAES__", "__GFNI__", "__ARM_NEON", "__ARM_NEON__",
    "__ARM_FEATURE_AES", "__ARM_FEATURE_CRYPTO",
)
_MAYO_RESET = (
    "MAYO_VARIANT", "MAYO_BUILD_TYPE_REF", "MAYO_BUILD_TYPE_OPT",
    "MAYO_BUILD_TYPE_AVX2", "MAYO_BUILD_TYPE_GFNI", "MAYO_BUILD_TYPE_NEON",
    "MAYO_AVX", "MAYO_GFNI", "MAYO_NEON", "ENABLE_PARAMS_DYNAMIC",
    "ENABLE_CT_TESTING", "HAVE_STACKEFFICIENT", "HAVE_RANDOMBYTES_NORETVAL",
    "PQM4", "TARGET_BIG_ENDIAN",
)
_UOV_PARAMS = (
    ("UOV-Is", "_OV16_160_64", "1"),
    ("UOV-Ip", "_OV256_119_45", "3"),
    ("UOV-III", "_OV256_193_72", "4"),
    ("UOV-V", "_OV256_259_96", "5"),
)
_UOV_RESET = (
    "_OV16_160_64", "_OV256_119_45", "_OV256_193_72", "_OV256_259_96",
    "_OV_CLASSIC", "_OV_PKC", "_OV_PKC_SKC", "_4ROUND_AES_", "_USE_GF16",
    "_BLAS_UINT64_", "_BLAS_SSE_", "_BLAS_AVX2_", "_BLAS_GFNI_",
    "_BLAS_NEON_", "_BLAS_M4F_", "_BLAS_UNIT_LEN_", "_MUL_WITH_MULTAB_",
    "_UTILS_AESNI_", "_UTILS_NEONAES_", "_UTILS_NEONBSAES_",
    "_UTILS_SUPERCOP_", "_UTILS_PQM4_", "_UTILS_OQS_", "_UTILS_OPENSSL_",
    "_APPLE_SILICON_", "_NIST_KAT_", "_DEBUG_RANDOMBYTES_", "_VALGRIND_",
    "_SUPERCOP_", "PQM4", "_USE_AVX2_", "_USE_AVX_", "_HASH_SHAKE128_",
    "_GE_CONST_TIME_CADD_EARLY_STOP_",
)
_SNOVA_RESET = (
    "SNOVA_REF", "SNOVA_OPT", "SNOVA_NAME", "SNOVA_v", "SNOVA_o",
    "SNOVA_q", "SNOVA_l", "SNOVA_r", "SNOVA_m1", "SNOVA_alpha",
    "AESCTR", "USE_OPENSSL", "SYMMETRIC", "FIXED_ABQ", "HASH_PK",
    "ROUND2_T12", "ABQ_ALG2", "VALGRIND",
)


def _undef(names):
    return ["-U" + name for name in names]


def _private_crypto(prefix):
    if prefix is None:
        raise ValueError("UOV requires the private OpenSSL prefix")
    prefix = Path(prefix).resolve()
    header = prefix / "include/openssl/evp.h"
    if not header.is_file():
        raise FileNotFoundError(header)
    # Never silently fall back to the host's libcrypto via a bare -lcrypto.
    for filename in ("libcrypto.so.3", "libcrypto.so", "libcrypto.a"):
        for dirname in ("lib/riscv64-linux-gnu", "lib", "lib64"):
            library = prefix / dirname / filename
            if library.is_file():
                options = [str(library), "-pthread", "-ldl"]
                if filename.endswith(".so"):
                    options += ["-Wl,-rpath," + str(library.parent)]
                return prefix / "include", options, str(library)
    raise FileNotFoundError("Private OpenSSL libcrypto not found under " + str(prefix))


def _specs(root, prefix, families):
    native = root / "native"
    specs = []
    if "mayo" in families:
        base = root / "upstream/mayo"
        # This pinned tar uses src/generic, not an old project copy's src/ref.
        # Its CMake ref branch enables dynamic parameters without MAYO_VARIANT.
        core = [base / "src" / name for name in (
            "mayo.c", "params.c", "arithmetic.c", "common/aes_c.c",
            "common/aes128ctr.c", "common/fips202.c", "common/mem.c",
            "common/randombytes_system.c",
        )]
        for number in (1, 2, 3, 5):
            api = base / "src" / ("mayo_" + str(number))
            seed_bytes = {1: 24, 2: 24, 3: 32, 5: 40}[number]
            specs.append({
                "algorithm": "MAYO", "parameter": "MAYO-" + str(number),
                "implementation": "ref-dynamic",
                "sources": [*core, api / "api.c", native / "mayo_adapter.c"],
                "includes": [api, base / "include", base / "src",
                             base / "src/common", base / "src/generic"],
                "flags": [*_undef(_MAYO_RESET), "-DMAYO_BUILD_TYPE_REF",
                          "-DENABLE_PARAMS_DYNAMIC", "-DTARGET_OTHER",
                          "-DTARGET_OS_UNIX"],
                "reference_selection": {
                    "macros": ["MAYO_BUILD_TYPE_REF", "ENABLE_PARAMS_DYNAMIC"],
                    "path": "upstream/mayo/src/generic",
                    "dispatch": "generic_arithmetic.h -> arithmetic_dynamic.h",
                    "parameter_object": "MAYO_" + str(number),
                    "api_source": str((api / "api.c").relative_to(root)),
                    "namespace": "MAYO_VARIANT unset; upstream dynamic names unchanged",
                    "rng": "upstream/mayo/src/common/randombytes_system.c; Linux getrandom (upstream urandom fallback)",
                    "symmetric": "upstream portable aes_c.c/aes128ctr.c and fips202.c",
                    "swar": "upstream generic scalar/SWAR unchanged",
                },
                "kat": {
                    "status": "not run; production binary is not a KAT runner",
                    "driver": "upstream/mayo/apps/PQCgenKAT_sign.c",
                    "response": "upstream/mayo/KAT/PQCsignKAT_%d_MAYO_%d.rsp" % (seed_bytes, number),
                    "separate_test_rng": "upstream/mayo/src/common/randombytes_ctrdrbg.c (replace, never add to production RNG)",
                },
            })
    if "uov" in families:
        base = root / "upstream/uov"
        crypto_include, libraries, crypto_path = _private_crypto(prefix)
        core = [base / name for name in (
            "src/blas_matrix.c", "src/ov.c", "src/ov_keypair.c",
            "src/ov_keypair_computation.c", "src/ov_publicmap.c",
            "src/parallel_matrix_op.c", "src/sign.c",
            "src/ref/blas_matrix_ref.c", "utils/aes128_4r_ffs.c",
            "utils/utils_hash.c", "utils/utils_prng.c",
        )]
        for parameter, macro, kat_param in _UOV_PARAMS:
            specs.append({
                "algorithm": "UOV", "parameter": parameter,
                "implementation": "ref-classic",
                "sources": [*core, native / "uov_adapter.c", native / "system_random.c"],
                "includes": [base / "src", base / "src/ref", base / "utils", crypto_include / "riscv64-linux-gnu", crypto_include],
                "flags": [*_undef(_UOV_RESET), "-D" + macro, "-D_OV_CLASSIC",
                          "-D_UTILS_OPENSSL_"],
                "source_flags": {native / "system_random.c": ["-Drandombytes=ref_system_randombytes"]},
                "libraries": libraries,
                "library_dependencies": [crypto_path],
                "reference_selection": {
                    "macros": [macro, "_OV_CLASSIC", "_UTILS_OPENSSL_"],
                    "path": "upstream/uov/src/ref",
                    "api_source": "upstream/uov/src/sign.c",
                    "keys": "full classic public and secret keys; no external expansion/cache",
                    "swar": "original src/ref/blas_u32.h and gf16/gf256 arithmetic unchanged; _BLAS_UINT64_ unset",
                    "rng": "native/system_random.c getrandom; adapter preserves PQOV_NAMESPACE(randombytes) void ABI; utils_randombytes.c excluded",
                    "symmetric": "original full-round portable aes128_4r_ffs.c entry points (_4ROUND_AES_ unset); upstream default OpenSSL EVP SHAKE256",
                },
                "kat": {
                    "status": "not run; production binary is not a KAT runner",
                    "driver": "upstream/uov/nist-submission-project/PQCgenKAT_sign.c",
                    "configuration": "PROJ=ref PARAM=" + kat_param + " VARIANT=1 KAT=1",
                    "separate_test_rng": "upstream/uov/utils/nistkat/rng.c; requires _NIST_KAT_, exclude production RNG and adapter RNG bridge",
                },
            })
    if "snova" in families:
        for level in ("I", "III", "V"):
            parameter = "SNOVA_" + level + "_B_AES"
            base = root / "upstream/snova/Reference_Implementation" / parameter
            specs.append({
                "algorithm": "SNOVA", "parameter": parameter,
                "implementation": "ref",
                "sources": [*(base / name for name in (
                    "sign.c", "snova_ref.c", "symmetric_ref.c", "aes.c",
                )), native / "snova_adapter.c", native / "system_random.c"],
                "includes": [base],
                "flags": [*_undef(_SNOVA_RESET), "-DSNOVA_REF=1"],
                "reference_selection": {
                    "macros": ["SNOVA_REF=1", "AESCTR (from original snova_params.h)"],
                    "path": str(base.relative_to(root)),
                    "api_source": str((base / "sign.c").relative_to(root)),
                    "parameters": "original per-directory snova_params.h; no command-line dimension overrides",
                    "rng": "native/system_random.c getrandom; int randombytes(unsigned char *, unsigned long long) matches rng.h; rng.c excluded",
                    "symmetric": "original portable aes.c (USE_OPENSSL/__AVX2__/__AES__ unset), symmetric_ref.c and keccak_opt64.h",
                    "swar": "original reference code unchanged",
                },
                "kat": {
                    "status": "not run; production binary is not a KAT runner",
                    "driver": str((base / "PQCgenKAT_sign.c").relative_to(root)),
                    "response": "upstream/snova/KAT/%s/PQCsignKAT_%s.rsp" % (parameter, parameter),
                    "configuration": "SNOVA_REF=1 and the original per-directory snova_params.h",
                    "separate_test_rng": str((base / "rng.c").relative_to(root)) + " (replace, never add to production RNG)",
                },
            })
    return specs


def build(root, out, cc, prefix, flags, run, family="all"):
    """Return manifests for all eleven profiles or selected family/families.

    family accepts 'all', 'mayo', 'uov', 'snova', a comma-separated string, or
    an iterable of these family names (case-insensitive). cc is an executable
    path or argv sequence. flags are the caller's RV64GC/LP64D profiling flags.
    run(argv, label) must record output and raise on failure; numeric and
    CompletedProcess return codes are also checked. Only compile/link commands
    are issued. --metadata and --sanity are left to the owning driver.
    """
    root = Path(root).resolve()
    out = Path(out).resolve()
    out.relative_to(root)
    if not out.is_dir():
        raise NotADirectoryError(out)
    names = family.split(",") if isinstance(family, str) else family
    families = {name.strip().lower() for name in names}
    if "all" in families:
        families = (families - {"all"}) | {"mayo", "uov", "snova"}
    if not families or not families <= {"mayo", "uov", "snova"}:
        raise ValueError("family must select mayo, uov, snova, or all")
    compiler = [os.fspath(cc)] if isinstance(cc, (str, os.PathLike)) else list(cc)
    if not compiler:
        raise ValueError("cc must not be empty")
    flags = list(flags)
    # Fix the architecture after inherited options; never run upstream Make's
    # -march=native or enable hand-written vector/hardware AES backends.
    common = [*flags, "-std=c11", "-march=rv64gc", "-mabi=lp64d",
              "-fno-lto", "-fno-tree-vectorize", "-fno-tree-slp-vectorize",
              *_undef(_HARDWARE), "-Urandombytes", "-Urandombytes_init",
              "-UPROFILE_PARAMETER", "-UPROFILE_IMPLEMENTATION"]
    native = root / "native"
    specs = _specs(root, prefix, families)
    for spec in specs:
        for source in [*spec["sources"], native / "bench_api.c", native / "ref_api.h"]:
            source.resolve().relative_to(root)
            if not source.is_file():
                raise FileNotFoundError(source)
        for include in spec["includes"]:
            if not include.is_dir():
                raise NotADirectoryError(include)

    def relative(path):
        return str(path.relative_to(root))

    def command(argv, label):
        result = run(argv, label)
        code = result if type(result) is int else getattr(result, "returncode", 0)
        if code:
            raise RuntimeError("%s failed with return code %s" % (label, code))

    entries = []
    for spec in specs:
        destination = out / spec["algorithm"].lower() / spec["parameter"]
        destination.mkdir(parents=True, exist_ok=True)
        sources = [*spec["sources"], native / "bench_api.c"]
        options = [*common, *spec["flags"],
                   '-DPROFILE_PARAMETER="%s"' % spec["parameter"],
                   '-DPROFILE_IMPLEMENTATION="%s"' % spec["implementation"],
                   "-I" + str(native), *("-I" + str(p) for p in spec["includes"])]
        objects, dependencies, stack_usage, compile_commands = [], [], [], []
        label = spec["algorithm"] + "/" + spec["parameter"]
        for index, source in enumerate(sources):
            obj = destination / ("%02d_%s.o" % (index, source.stem))
            dep = obj.with_suffix(".d")
            argv = [*compiler, *options, *spec.get("source_flags", {}).get(source, []),
                    "-fstack-usage", "-MD", "-MF", str(dep), "-MT", str(obj),
                    "-c", str(source), "-o", str(obj)]
            command(argv, label + "/compile/" + obj.stem)
            objects.append(obj)
            dependencies.append(relative(dep))
            stack_usage.append(relative(obj.with_suffix(".su")))
            compile_commands.append({"directory": str(root), "file": str(source),
                                     "output": str(obj), "arguments": argv})
        binary = destination / "bench"
        link = [*compiler, *common, *(str(obj) for obj in objects),
                "-Wl,--build-id", "-o", str(binary), *spec.get("libraries", [])]
        command(link, label + "/link")
        entries.append({
            "algorithm": spec["algorithm"], "parameter": spec["parameter"],
            "implementation": spec["implementation"], "binary": relative(binary),
            "objects": [relative(obj) for obj in objects],
            "sources": [relative(source) for source in sources],
            "dependency_files": dependencies, "stack_usage": stack_usage,
            "compile_commands": compile_commands, "link_command": link,
            "library_dependencies": spec.get("library_dependencies", []),
            "api": "signed-message", "reference_selection": spec["reference_selection"],
            "sanity_argv": [relative(binary), "--sanity"], "kat": spec["kat"],
        })
    return entries
