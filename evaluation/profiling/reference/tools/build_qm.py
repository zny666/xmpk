"""Build only the six QRUOV and three MQOM signed-message reference profiles.

All compiler/linker commands go through run(argv, label); this module never
executes a signature algorithm, invokes make, or writes into the source tree.
The caller supplies native/bench_api.c and native/system_random.c, whose RNG
ABI must be int randombytes(unsigned char *, unsigned long long).
"""

import os
from pathlib import Path
import re


QR_SOURCES = (
    "sign.c", "qruov_common.c", "qrop.c", "prg.c", "prim.c", "linsys.c", "qruov.c",
)

# The complete C object groups from the actual per-variant Makefile. Inactive
# OT/memopt/hardware translation units retain their upstream preprocessor guards.
MQ_GROUPS = {
    "RIJNDAEL_SRC_FILES": (
        "rijndael/rijndael_ref.c", "rijndael/rijndael_table.c",
        "rijndael/rijndael_aes_ni.c", "rijndael/rijndael_vaes.c",
        "rijndael/rijndael_ct64.c", "rijndael/rijndael_external.c",
        "rijndael/rijndael_arm_aes.c",
    ),
    "GGM_SRC_FILES": (
        "ggm_tree/ggm_tree_small.c", "ggm_tree/ggm_tree_small_incr.c",
        "ggm_tree/ggm_tree_small_incr_batch.c",
        "ggm_tree/ggm_tree_large_common_impl.c", "ggm_tree/ggm_tree_large_dfs.c",
        "ggm_tree/ggm_tree_large_incr.c", "ggm_tree/ggm_tree_large_incr_batch.c",
        "ggm_tree/ggm_tree_large_bfs.c",
    ),
    "BLC_SRC_FILES": (
        "blc/blc_ot_default.c", "blc/blc_ot_memopt.c",
        "blc/blc_ct_default.c", "blc/blc_ct_memopt.c",
    ),
    "PIOP_SRC_FILES": (
        "piop/piop_default.c", "piop/piop_memopt.c", "piop/piop_bitslice.c",
    ),
    "MQOM3_SRC_FILES": (
        "xof.c", "prg.c", "seed_expand.c", "expand_mq.c", "keygen.c",
        "sign.c", "sign_memopt.c", "sign_pre.c", "crypto_sign.c",
        "verify_stream_ot.c", "verify_stream_ct.c",
    ),
}
SHA3_COMMON = (
    "KeccakHash.c", "KeccakSponge.c", "KeccakSpongetimes4.c", "KeccakHashtimes4.c",
)
SHA3_OPT64 = (
    "opt64/KeccakP-1600-opt64.c", "opt64/KeccakP-1600-times4-on1.c",
)
MQ_DEFINES = (
    "FIELDS_REF", "RIJNDAEL_BITSLICE", "NO_GFNI",
    "NO_RIJNDAEL_VAES_GFNI_KEYSCHED", "NO_MISALIGNED_ACCESSES",
    "KECCAK_PLATFORM=opt64", "NO_ALLOC_PROBE", "NDEBUG",
    # Preserve the Makefile's portable performance defaults. x4 Keccak is four
    # serial C permutations; ct64/x8 encryption is C bitslicing, not an ISA SIMD.
    "USE_PIOP_CACHE", "USE_XOF_X4", "AES_GRINDING_X4", "USE_ENC_X8",
    "LARGE_TREE_BFS", "LARGE_TREE_BFS_X8",  # OT-only, inactive for these CT sets.
)


def _relative(path, root):
    return Path(os.path.relpath(path, root)).as_posix()


def _make_words(text, variable, replacements=None):
    """Read only known literal source assignments, without executing make."""
    values = re.findall(r"^[ \t]*" + re.escape(variable) +
                        r"[ \t]*(?::=|\+=|=)[ \t]*(.*)$", text, re.MULTILINE)
    if not values:
        raise ValueError("Missing upstream source assignment: " + variable)
    words = []
    for value in values:
        for key, replacement in (replacements or {}).items():
            value = value.replace("$(" + key + ")", replacement)
        for word in value.split():
            if "$" in word or "\\" in word or word.startswith("#"):
                raise ValueError("Unrecognized upstream source expression: " + word)
            words.append(Path(word).as_posix())
    return tuple(words)


def _check_qr_make(source_dir):
    text = (source_dir / "GNUmakefile").read_text()
    expected = QR_SOURCES[:3] + ("rng.c",) + QR_SOURCES[3:] + ("PQCgenKAT_sign.c",)
    if _make_words(text, "SRCS") != expected:
        raise ValueError("QRUOV Makefile source list changed; review selection")


def _check_mq_make(source_dir, category):
    text = (source_dir / "Makefile").read_text()
    if _make_words(text, "MQOM3_VARIANT") != (f"cat{category}_gf16_fast_ct",):
        raise ValueError("MQOM source directory and Makefile variant disagree")
    replacements = {
        "RIJNDAEL_DIR": "rijndael", "GGM_DIR": "ggm_tree", "BLC_DIR": "blc",
        "PIOP_DIR": "piop", "MQOM3_DIR": ".",
    }
    for group, expected in MQ_GROUPS.items():
        # These two assembly files occur only inside RIJNDAEL_OPT_ARMV7M=1.
        if group == "RIJNDAEL_SRC_FILES":
            expected += ("rijndael/aes128_table_arvmv7m.s",
                         "rijndael/aes128_fixsliced_arvmv7m.s")
        if _make_words(text, group, replacements) != expected:
            raise ValueError("MQOM Makefile source list changed: " + group)
    sha3 = (source_dir / "sha3/Makefile").read_text()
    common = _make_words(sha3, "COMMON_SOURCES")
    # times8 is added only inside the avx512 branch, which is not selected.
    if common != SHA3_COMMON + ("KeccakSpongetimes8.c", "KeccakHashtimes8.c"):
        raise ValueError("MQOM SHA3 common source list changed")
    block = re.search(r"ifeq \(\$\(KECCAK_PLATFORM\),opt64\)\n(.*?)\nendif",
                      sha3, re.DOTALL)
    if block is None or _make_words(block.group(1), "SPECIFIC_SOURCES") != SHA3_OPT64:
        raise ValueError("MQOM SHA3 opt64 source selection changed")


def _validate_flags(flags):
    if "-O3" not in flags or "-std=c11" not in flags:
        raise ValueError("The common flags must explicitly include -std=c11 -O3")
    owned_macros = (
        "QRUOV_", "PRG_IS_AES", "PRIM_", "MQOM", "FIELDS_", "RIJNDAEL_",
        "KECCAK_", "Keccak", "XKCP_", "REF_", "APPLY_", "SUPERCOP", "randombytes",
        "USE_", "NO_", "SIGN_", "VERIFY_", "MEMORY_", "BLC_", "PIOP_", "BENCHMARK",
        "EXTERNAL_", "AES_GRINDING", "LARGE_TREE", "SMALL_GGM", "SEED_COMMIT",
    )
    for flag in flags:
        if (flag.startswith("-O") and flag != "-O3") or flag == "-s":
            raise ValueError("Non-profile optimization/strip flag: " + flag)
        if any(token in flag.lower() for token in
               ("native", "avx", "neon", "rvv", "-maes", "-mvaes", "-mgfni",
                "+crypto", "+sha3", "-flto", "strip-all", "strip-debug")):
            raise ValueError("Non-portable or stripped profile flag: " + flag)
        if flag.startswith("-march=") and flag != "-march=rv64gc":
            raise ValueError("Only the supplied rv64gc target is supported: " + flag)
        if flag.startswith(("-D", "-U")) and flag[2:].startswith(owned_macros):
            raise ValueError("Algorithm selection belongs to build_qm: " + flag)


def _build_one(root, out, compiler, flags, run, algorithm, parameter,
               implementation, source_dir, upstream_sources, adapter, defines,
               include_dirs, libraries, system_libraries, selection, extra=None):
    sources = [source_dir / name for name in upstream_sources]
    sources += [root / "native" / name for name in
                (adapter, "bench_api.c", "system_random.c")]
    for source in sources:
        if not source.is_file():
            raise FileNotFoundError(source)
    unit = out / (algorithm.lower() + "-" + parameter)
    # Fail on a reused parameter directory: never bless stale .o/.d/.su files.
    unit.mkdir(exist_ok=False)
    objects, dependencies, stack_usage = [], [], []
    metadata_defines = [f'REF_PARAMETER="{parameter}"',
                        f'REF_IMPLEMENTATION="{implementation}"']
    cflags = flags + ["-D" + value for value in list(defines) + metadata_defines]
    cflags += ["-I" + str(path) for path in [root / "native"] + include_dirs]
    for index, source in enumerate(sources):
        obj = unit / f"{index:02d}-{source.stem}.o"
        dep, stack = obj.with_suffix(".d"), obj.with_suffix(".su")
        local_flags = (extra or {}).get(_relative(source, root), [])
        argv = compiler + cflags + local_flags + [
            "-MD", "-MF", str(dep), "-MT", str(obj), "-fstack-usage",
            "-c", str(source), "-o", str(obj),
        ]
        run(argv, f"{unit.name}-cc-{index:02d}-{source.stem}")
        for artifact in (obj, dep, stack):
            if not artifact.is_file():
                raise RuntimeError("Compiler did not produce " + str(artifact))
        objects.append(obj)
        dependencies.append(dep)
        stack_usage.append(stack)
    binary = unit / "bench"
    argv = compiler + flags + [str(obj) for obj in objects]
    argv += [str(library) for library in libraries] + system_libraries
    argv += ["-Wl,--build-id", "-o", str(binary)]
    run(argv, unit.name + "-link")
    if not binary.is_file():
        raise RuntimeError("Linker did not produce " + str(binary))
    selection = dict(selection, defines=list(defines),
                     adapter_defines=metadata_defines,
                     per_source_flags=extra or {},
                     includes=[_relative(path, root) for path in include_dirs],
                     common_flags=list(flags),
                     rng="native/system_random.c: getrandom; int randombytes(unsigned char *, unsigned long long)")
    return {
        "algorithm": algorithm, "parameter": parameter,
        "implementation": implementation, "binary": _relative(binary, root),
        "objects": [_relative(path, root) for path in objects],
        "sources": [_relative(path, root) for path in sources],
        "libraries": [_relative(path, root) for path in libraries],
        "library_dependencies": [_relative(path, root) for path in libraries],
        "system_libraries": system_libraries,
        "dependency_files": [_relative(path, root) for path in dependencies],
        "stack_usage": [_relative(path, root) for path in stack_usage],
        "api": "signed-message", "reference_selection": selection,
    }


def build(root, out, cc, prefix, flags, run):
    """Return nine entries after compiling/linking; never execute the binaries.

    root is R20, out is an existing fresh build-run directory, and prefix is the
    caller-provided portable OpenSSL installation (headers and libcrypto).
    The caller audits that library's ISA/provenance as well as these objects.
    """
    root = Path(root).resolve()
    out = (root / out).resolve()
    prefix = (root / prefix).resolve()
    out.relative_to(root / "build")
    if not out.is_dir():
        raise NotADirectoryError(out)
    compiler = [str(cc)] if isinstance(cc, (str, os.PathLike)) else list(cc)
    flags = list(flags)
    _validate_flags(flags)
    if not (prefix / "include/openssl/evp.h").is_file():
        raise FileNotFoundError(prefix / "include/openssl/evp.h")
    crypto = next((prefix / directory / filename
                   for filename in ("libcrypto.so.3", "libcrypto.so", "libcrypto.a")
                   for directory in ("lib/riscv64-linux-gnu", "lib", "lib64")
                   if (prefix / directory / filename).is_file()), None)
    if crypto is None:
        raise FileNotFoundError("Private portable libcrypto is required under " + str(prefix))
    crypto_link = ["-ldl", "-pthread", "-lm"]
    if crypto.suffix == ".so":
        crypto_link.append("-Wl,-rpath," + str(crypto.parent))

    qr_dir = root / "upstream/qruov/Reference_Implementation/ref"
    _check_qr_make(qr_dir)
    mq_dirs = {category: root / "upstream/mqom/Reference_Implementation" /
               f"mqom3_cat{category}_gf16_fast_ct" for category in (1, 3, 5)}
    for category, source_dir in mq_dirs.items():
        _check_mq_make(source_dir, category)

    entries = []
    for category in (1, 3, 5):
        for extension in (3, 10):
            parameter = f"{category}q127L{extension}"
            entries.append(_build_one(
                root, out, compiler, flags, run, "QRUOV", parameter,
                "ref-original-aes", qr_dir, QR_SOURCES, "qruov_adapter.c",
                [f"QRUOV_PARAM_{parameter}", "PRG_IS_AES=1"],
                [qr_dir, prefix / "include/riscv64-linux-gnu", prefix / "include"], [crypto], crypto_link,
                {
                    "source_directory": _relative(qr_dir, root),
                    "makefiles": [_relative(qr_dir / "GNUmakefile", root)],
                    "selection": "PARAM=" + parameter + "; PRG=aes; IMPL=ref",
                    "primitive": "Unmodified prim.c OpenSSL EVP AES/SHAKE; no PRIM overrides",
                    "excluded": ["rng.c", "PQCgenKAT_sign.c"],
                    "rng_bridge": "Upstream void RNG calls use qruov_ref_randombytes; adapter checks production int result",
                    "custom_optimizations": "none (no Full/cache/EMI/opt/RVV sources)",
                },
                {_relative(qr_dir / "sign.c", root): ["-Drandombytes=qruov_ref_randombytes"]},
            ))

    mq_sources = tuple(name for group in MQ_GROUPS.values() for name in group)
    mq_sources += tuple("sha3/" + name for name in SHA3_COMMON + SHA3_OPT64)
    for category, security in ((1, 128), (3, 192), (5, 256)):
        source_dir = mq_dirs[category]
        parameter = f"mqom3_cat{category}_gf16_fast_ct"
        defines = list(MQ_DEFINES) + [
            f"MQOM3_PARAM_SECURITY={security}", "MQOM3_PARAM_BASE_FIELD=4",
            "MQOM3_PARAM_TRADEOFF=0", "MQOM3_PARAM_OT_VARIANT=0",
        ]
        entries.append(_build_one(
            root, out, compiler, flags, run, "MQOM", parameter,
            "reference-C-portable", source_dir, mq_sources, "mqom_adapter.c", defines,
            [source_dir / path for path in
             (".", "sha3", "sha3/opt64", "rijndael", "ggm_tree", "blc", "piop",
              "fields", "fields_bitsliced")], [], ["-lm"],
            {
                "source_directory": _relative(source_dir, root),
                "makefiles": [_relative(source_dir / path, root) for path in
                              ("Makefile", "sha3/Makefile")],
                "selection": "Official Reference_Implementation C tree; shared reference/optimized source, explicitly portable backends",
                "make_equivalent": "FORCE_PLATFORM_REF=1 NO_NATIVE_TUNE=1 KECCAK_PLATFORM=opt64; portable default options retained; no make autodetection",
                "fields": "FIELDS_REF, upstream SWAR enabled (scalar integer C)",
                "rijndael": "RIJNDAEL_BITSLICE private ct64; upstream public table C backend; default key schedule",
                "xof": "Internal SHAKE, opt64 C with times4-on1 serial fallback; full rounds",
                "ct_meaning": "correlated-tree, not a constant-time claim",
                "platform_interface": "ordinary int RNG API; no LIBOQS/MUPQ/SUPERCOP branch",
                "excluded": ["generator/rng.c", "generator/PQCgenKAT*.c",
                             "benchmark/*.c", "tests/*", "hardware/assembly backends (guarded off)"],
            },
        ))
    return entries
