#!/usr/bin/env python3
"""gen-aten-op-table.py — emit vessel/torch/aten_op_table.h from a PyTorch fork.

The table is data. It carries, for every ATen operator the fork exposes,
the name, the overload, the schema hash the vessel joins on, the arity,
which arguments are tensors, which are tensor lists, whether an argument
writes through an alias, and the CKernelId the taxonomy gives the
operation. It carries no signature: record_kernel.h
derives that by reflection from `at::_ops::<struct>::call`, so the two can
never disagree.

WHERE THE FACTS COME FROM, and why it is not the yaml

The obvious source for this generator is native_functions.yaml. That
file is not the authoritative set. Measured on fork 8be2400cf5c:

    distinct (name, overload) in ATen/ops/*_ops.h   3110
    distinct (name, overload) from yaml `- func:`   2590
    in the headers and not in the yaml               520
    in the yaml and not in the headers                 0

The 520 are the variants torchgen generates from an `autogen:` key, of
which about 92 percent are `out=` forms. They are dispatched at runtime
exactly like any other operator, so a table built from the yaml alone
leaves 520 real operators to the boxed fallback for no reason.

Reproducing them from the yaml means reproducing torchgen's filter, and
that filter has a trap: the yaml declares 533 autogen signatures and only
520 are generated. A CompositeImplicitAutograd operator without `tags:
core` gets no generated variant even though its entry asks for one
(torchgen/native_function_generation.py:424-433). Thirteen declared
signatures name operators that do not exist.

So the headers are the source. The yaml is still read, for two reasons:
its sha256 goes in the emitted header so CI can tell the table apart from
the fork it was built against, and every one of its 2590 `func:` strings
is checked to appear verbatim as a `schema_str`. That check is the third
of three independent agreements this table rests on; the other two are in
record_kernel.h.

Usage:
    gen-aten-op-table.py --torch-dir DIR [--pytorch-src DIR] [-o FILE]
    gen-aten-op-table.py --check      # regenerate and diff, for CI
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

# ---------------------------------------------------------------------
# The schema hash.
#
# This must be the hash crucible_fallback.cpp already computes, because
# the background thread joins the trace entry to the schema table on it.
# The fallback's own words, at crucible_fallback.cpp:146: "Cache miss:
# compute FNV-1a over 'namespace::name.overload'". c10::OperatorName::name
# already carries the `aten::` prefix, and so does the `name` member of
# every generated op struct, so the two spellings agree byte for byte.

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
U64 = 0xFFFFFFFFFFFFFFFF


def schema_hash(qualified_name: str, overload: str) -> int:
    """FNV-1a over "aten::name" plus ".overload" when the overload is named.

    qualified_name is the struct's `name` member, already `aten::`-prefixed.
    """
    text = qualified_name + ("." + overload if overload else "")
    h = FNV_OFFSET
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * FNV_PRIME) & U64
    return h


# Values lifted from crucible_fallback.cpp's own comment block, so a
# change to the hash there fails here rather than silently reshaping
# every key in the table.
_HASH_WITNESSES = {
    ("aten::mm", ""): 0x983B9200D566222D,
    ("aten::mm", "out"): 0xFFEB39DBB60BE1F3,
    ("aten::add", "Tensor"): 0x46A35DEF8D03FCF9,
    ("aten::relu", ""): 0x4E408BC8AB40F455,
}

for (_witness_name, _witness_overload), _expected in _HASH_WITNESSES.items():
    _got = schema_hash(_witness_name, _witness_overload)
    if _got != _expected:
        raise SystemExit(
            f"schema_hash disagrees with crucible_fallback.cpp for "
            f"{_witness_name}.{_witness_overload}: "
            f"got 0x{_got:016x}, expected 0x{_expected:016x}"
        )


# ---------------------------------------------------------------------
# The CKernelId taxonomy.
#
# include/crucible/CKernel.h holds 146 operation identifiers and OPAQUE.
# Its header comment states the rule this map obeys: "Aliases and backend
# variants of one mathematical operation share a single identifier. An
# in-place variant folds into its out-of-place identifier." So the map is
# keyed on the base name with any leading underscore and any trailing
# underscore removed, and every overload of one base name lands on one id.
#
# An operation the map does not name gets OPAQUE, which is the taxonomy's
# own sentinel: CKernel.h says "OPAQUE is 0 so a zero-initialised
# classification field already holds the safe fallback", and
# CKernelTable::classify returns it on a miss. A guess would be worse than
# OPAQUE, because Graph.h:classify_node_kind reads contiguous id ranges —
# POINTWISE is [25,56], REDUCTION [57,64], NOP [73,88] — so a wrong id
# does not mislabel an operation, it changes its NodeKind and with it what
# the graph believes the operation does.

CKERNEL_MAP: dict[str, str] = {
    # GEMM family
    "mm": "GEMM_MM", "bmm": "GEMM_BMM", "matmul": "GEMM_MATMUL",
    "addmm": "GEMM_ADDMM", "linear": "GEMM_LINEAR", "addbmm": "GEMM_ADDBMM",
    "baddbmm": "GEMM_BADDBMM", "einsum": "GEMM_EINSUM",
    # Convolution
    "conv1d": "CONV1D", "conv2d": "CONV2D", "conv3d": "CONV3D",
    "conv_transpose1d": "CONV_TRANSPOSE1D",
    "conv_transpose2d": "CONV_TRANSPOSE2D",
    "conv_transpose3d": "CONV_TRANSPOSE3D",
    # Attention
    "scaled_dot_product_attention": "SDPA",
    # Normalization
    "layer_norm": "LAYER_NORM", "native_layer_norm": "LAYER_NORM",
    "group_norm": "GROUP_NORM", "native_group_norm": "GROUP_NORM",
    "instance_norm": "INSTANCE_NORM",
    "rms_norm": "RMS_NORM",
    # Activations — POINTWISE range [25,56]
    "relu": "ACT_RELU", "gelu": "ACT_GELU", "silu": "ACT_SILU",
    "sigmoid": "ACT_SIGMOID", "tanh": "ACT_TANH", "hardswish": "ACT_HARDSWISH",
    "leaky_relu": "ACT_LEAKY_RELU", "elu": "ACT_ELU",
    "softmax": "ACT_SOFTMAX", "safe_softmax": "ACT_SOFTMAX",
    "log_softmax": "ACT_LOG_SOFTMAX",
    "dropout": "ACT_DROPOUT", "native_dropout": "ACT_DROPOUT",
    "clamp": "ACT_CLAMP", "mish": "ACT_MISH",
    # Elementwise — POINTWISE range
    "add": "EWISE_ADD", "mul": "EWISE_MUL", "sub": "EWISE_SUB",
    "div": "EWISE_DIV", "pow": "EWISE_POW",
    "maximum": "EWISE_MAX", "minimum": "EWISE_MIN",
    "remainder": "EWISE_MOD", "fmod": "EWISE_MOD",
    "where": "EWISE_WHERE", "exp": "EWISE_EXP", "log": "EWISE_LOG",
    "sqrt": "EWISE_SQRT", "rsqrt": "EWISE_RSQRT", "abs": "EWISE_ABS",
    "neg": "EWISE_NEG", "sign": "EWISE_SIGN", "floor": "EWISE_FLOOR",
    "to": "EWISE_CAST", "fill": "EWISE_FILL", "zero": "EWISE_FILL",
    # Reductions — REDUCTION range [57,64]
    "sum": "REDUCE_SUM", "mean": "REDUCE_MEAN", "max": "REDUCE_MAX",
    "min": "REDUCE_MIN", "argmax": "REDUCE_ARGMAX", "argmin": "REDUCE_ARGMIN",
    "cumsum": "REDUCE_CUMSUM", "topk": "REDUCE_TOPK",
    # Pooling
    "max_pool1d": "POOL_MAX1D", "max_pool2d": "POOL_MAX2D",
    "max_pool3d": "POOL_MAX3D", "avg_pool1d": "POOL_AVG1D",
    "avg_pool2d": "POOL_AVG2D", "avg_pool3d": "POOL_AVG3D",
    "adaptive_max_pool2d": "POOL_ADAPTIVE_MAX",
    "adaptive_avg_pool2d": "POOL_ADAPTIVE_AVG",
    # Data movement — NOP range [73,88]
    "view": "VIEW", "reshape": "RESHAPE", "permute": "PERMUTE",
    "transpose": "TRANSPOSE", "contiguous": "CONTIGUOUS",
    "expand": "EXPAND", "squeeze": "SQUEEZE", "unsqueeze": "SQUEEZE",
    "slice": "SLICE", "index_select": "INDEX_SELECT", "index": "INDEX",
    "scatter": "SCATTER", "masked_fill": "MASKED_FILL", "pad": "PAD",
    "cat": "CAT", "stack": "STACK", "unfold": "UNFOLD",
    # Embedding and copies
    "embedding": "EMBEDDING", "embedding_bag": "EMBEDDING_BAG",
    "copy": "COPY_", "clone": "CLONE",
    # Sampling
    "interpolate": "INTERPOLATE", "upsample_nearest2d": "INTERPOLATE",
    "upsample_bilinear2d": "INTERPOLATE", "grid_sampler": "GRID_SAMPLE",
    "im2col": "IM2COL",
    # Linear algebra
    "linalg_svd": "LINALG_SVD", "linalg_cholesky": "LINALG_CHOLESKY",
    "linalg_qr": "LINALG_QR", "linalg_solve": "LINALG_SOLVE",
    "linalg_eigh": "LINALG_EIGH", "linalg_norm": "LINALG_NORM",
    "linalg_cross": "LINALG_CROSS", "cdist": "CDIST",
    # RNG
    "uniform": "RNG_UNIFORM", "rand": "RNG_UNIFORM",
    "normal": "RNG_NORMAL", "randn": "RNG_NORMAL",
}

OPAQUE = "OPAQUE"


def ckernel_for(base_name: str) -> str:
    """The CKernelId for an ATen base name, or OPAQUE when unrecognised.

    The base name arrives `aten::`-prefixed and may carry the leading
    underscore of an internal spelling or the trailing underscore of an
    in-place variant. Both fold away, because the taxonomy gives one
    identifier per mathematical operation.
    """
    stem = base_name.removeprefix("aten::")
    stem = stem.rstrip("_")
    stem = stem.lstrip("_")
    return CKERNEL_MAP.get(stem, OPAQUE)


# ---------------------------------------------------------------------
# Reading the generated op headers.

_STRUCT_RE = re.compile(
    r"struct TORCH_API (?P<struct>\w+) \{(?P<body>.*?)\n\};", re.S
)
_NAME_RE = re.compile(r'static constexpr const char\* name = "(?P<v>[^"]*)";')
_OVERLOAD_RE = re.compile(
    r'static constexpr const char\* overload_name = "(?P<v>[^"]*)";'
)
_SCHEMA_RE = re.compile(
    r'static constexpr const char\* schema_str = "(?P<v>.*?)";\n'
)


@dataclass(frozen=True)
class Op:
    """One ATen operator, as the generated headers describe it."""

    struct: str          # at::_ops::<struct>
    header: str          # ATen/ops/<header>
    name: str            # "aten::add"
    overload: str        # "Tensor", or "" when unnamed
    schema_str: str      # "add.Tensor(Tensor self, ...) -> Tensor"
    arity: int
    tensor_arg_mask: int
    tensor_list_mask: int
    is_mutable: bool
    ckernel: str


def split_top_level(text: str, sep: str = ",") -> list[str]:
    """Split on `sep` at bracket depth zero, honouring quotes and escapes.

    Two things in the schema grammar defeat a plain split. A default value
    may be a bracketed list holding a comma, as in `int[] dim=[-2,-1]`.
    And `_test_string_default` carries backslash-escaped quotes around a
    comma, which desynchronises a splitter that tracks quotes but not
    escapes. Both are real entries in the fork, not hypotheticals.
    """
    parts: list[str] = []
    depth = 0
    quote: str | None = None
    escaped = False
    current: list[str] = []
    for ch in text:
        if escaped:
            current.append(ch)
            escaped = False
            continue
        if ch == "\\":
            current.append(ch)
            escaped = True
            continue
        if quote is not None:
            current.append(ch)
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            current.append(ch)
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(current))
            current = []
            continue
        current.append(ch)
    parts.append("".join(current))
    return [p.strip() for p in parts if p.strip()]


def argument_list_of(schema_str: str) -> list[str]:
    """The arguments of a schema string, as raw `type name[=default]` text.

    The return arrow is not a delimiter. Fourteen entries in the fork
    annotate a parameter `Tensor(a -> *)`, which puts a literal arrow
    inside the argument list, so the arguments are taken by matching the
    opening parenthesis instead of by splitting on the arrow.
    """
    open_at = schema_str.index("(")
    depth = 0
    close_at = -1
    for i in range(open_at, len(schema_str)):
        ch = schema_str[i]
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
            if depth == 0:
                close_at = i
                break
    if close_at < 0:
        raise ValueError(f"unbalanced parentheses in schema: {schema_str!r}")
    inner = schema_str[open_at + 1 : close_at]
    # `*` is the keyword-only separator. It is a bare token with no type
    # and no name, and it occupies no argument position.
    return [a for a in split_top_level(inner) if a != "*"]


_ANNOTATION_RE = re.compile(r"\([^)]*\)")


def classify_argument(arg: str) -> str:
    """"tensor", "tensor_list", or "other" for one schema argument.

    The optional marker binds to whatever precedes it, and both bindings
    occur in the fork: `Tensor?[]` is a list of optional tensors, while
    `float[]?` is an optional list. So the marker is stripped in place
    rather than normalised away, and the brackets decide the answer.
    """
    type_text = arg.split("=", 1)[0].strip()
    # Drop the argument name: it is the last whitespace-separated token.
    parts = type_text.rsplit(" ", 1)
    type_only = parts[0].strip() if len(parts) == 2 else type_text
    # Drop an alias annotation such as (a), (a!) or (a -> *).
    type_only = _ANNOTATION_RE.sub("", type_only).strip()
    type_only = type_only.replace("?", "")
    if not type_only.startswith("Tensor"):
        return "other"
    return "tensor_list" if "[" in type_only else "tensor"


def is_mutable_of(schema_str: str) -> bool:
    """True when some argument of this schema writes through an alias.

    This is the value `c10::FunctionSchema::is_mutable()` computes, which
    the boxed fallback reads to set the IS_MUTABLE op flag. That function
    asks whether any argument carries alias information marked write
    (ATen/core/function_schema.h:383-389), and an argument is marked write
    exactly when its alias annotation carries `!`: `Tensor(a!) self` for an
    in-place operator, `Tensor(a!) out` for an out= variant, `Tensor(a!)[]
    self` for a foreach variant.

    The default value is removed before the search. An annotation only ever
    occurs in the type, so nothing after `=` can carry the marker, and a
    string default that happened to hold `!` would otherwise read as one.

    Verified against the parser itself: over the 3765 `aten::` schemas the
    built fork registers, this function and `FunctionSchema::is_mutable()`
    agree on every one.
    """
    return any("!" in arg.split("=", 1)[0] for arg in argument_list_of(schema_str))


def masks_of(schema_str: str) -> tuple[int, int, int]:
    """(arity, tensor_arg_mask, tensor_list_mask) for one schema string."""
    args = argument_list_of(schema_str)
    tensor_mask = 0
    list_mask = 0
    for index, arg in enumerate(args):
        kind = classify_argument(arg)
        if kind == "tensor":
            tensor_mask |= 1 << index
        elif kind == "tensor_list":
            list_mask |= 1 << index
    return len(args), tensor_mask, list_mask


def read_ops(ops_dir: Path) -> list[Op]:
    """Every operator struct that carries both a call and a redispatch."""
    ops: list[Op] = []
    for header in sorted(ops_dir.glob("*_ops.h")):
        text = header.read_text(encoding="utf-8")
        for match in _STRUCT_RE.finditer(text):
            body = match.group("body")
            if " call(" not in body or " redispatch(" not in body:
                continue
            name_m = _NAME_RE.search(body)
            overload_m = _OVERLOAD_RE.search(body)
            schema_m = _SCHEMA_RE.search(body)
            if not (name_m and overload_m and schema_m):
                raise SystemExit(
                    f"{header.name}: struct {match.group('struct')} is missing "
                    f"a name, overload_name or schema_str member. The generated "
                    f"header shape changed; update _STRUCT_RE and its siblings."
                )
            # schema_str is a C string literal. Only _test_string_default
            # needs the unescaping, but it needs it.
            schema_text = schema_m.group("v").encode().decode("unicode_escape")
            arity, tensor_mask, list_mask = masks_of(schema_text)
            if arity > 32:
                raise SystemExit(
                    f"{name_m.group('v')}.{overload_m.group('v')} has {arity} "
                    f"arguments, past the 32 a uint32_t mask can address. Widen "
                    f"OpEntry's mask fields to uint64_t and this check with them."
                )
            ops.append(
                Op(
                    struct=match.group("struct"),
                    header=header.name,
                    name=name_m.group("v"),
                    overload=overload_m.group("v"),
                    schema_str=schema_text,
                    arity=arity,
                    tensor_arg_mask=tensor_mask,
                    tensor_list_mask=list_mask,
                    is_mutable=is_mutable_of(schema_text),
                    ckernel=ckernel_for(name_m.group("v")),
                )
            )
    if not ops:
        raise SystemExit(f"no operator structs found under {ops_dir}")
    return ops


# ---------------------------------------------------------------------
# The yaml, read for provenance and for the cross-check.

_FUNC_RE = re.compile(r"^- func: (?P<v>.+)$", re.M)


def read_yaml_facts(yaml_path: Path, ops: list[Op]) -> tuple[str, int, int]:
    """(sha256, entry count, matched count), and a hard error on a mismatch.

    Every `func:` string in the yaml must appear verbatim as some
    operator's schema_str. The reverse does not hold, because torchgen
    generates 520 operators the yaml only implies.
    """
    raw = yaml_path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    funcs = [m.group("v").strip() for m in _FUNC_RE.finditer(raw.decode("utf-8"))]
    known = {op.schema_str for op in ops}
    missing = [f for f in funcs if f not in known]
    if missing:
        raise SystemExit(
            f"{len(missing)} of {len(funcs)} yaml func entries do not appear as a "
            f"schema_str in the generated headers. The yaml and the built wheel "
            f"are from different revisions of the fork. First three:\n  "
            + "\n  ".join(missing[:3])
        )
    return digest, len(funcs), len(funcs) - len(missing)


# ---------------------------------------------------------------------
# Emitting.

HEADER_TEMPLATE = '''#pragma once

// aten_op_table.h — GENERATED by scripts/gen-aten-op-table.py. Do not edit.
//
// One entry per ATen operator the fork exposes. The entry carries what a
// recording kernel needs before it has seen a tensor: the schema hash the
// background thread joins on, how many arguments there are, which of them
// are tensors, which are tensor lists, whether an argument writes through
// an alias, and what the CKernel taxonomy calls the operation.
//
// It carries no signature. record_kernel.h derives that from
// `at::_ops::<struct>::call` by reflection, and cross-checks the arity and
// the masks here against the same reflected pack, so a table built against
// one revision of the fork and compiled against another fails to build
// rather than recording against the wrong argument positions.
//
// Provenance of this file:
//   fork torch version      {torch_version}
//   native_functions.yaml   sha256 {yaml_sha256}
//                           {yaml_entries} `- func:` entries, all matched
//   operator structs        {op_count}
//   classified              {classified} ({opaque} OPAQUE)

#include <crucible/CKernel.h>

#include <cstdint>
#include <array>

namespace crucible::vessel {{

// The yaml this table was generated against. CI compares it with the
// checked-out fork: a table built against a different revision than the
// wheel it runs on is the failure this pins down.
inline constexpr const char* kNativeFunctionsSha256 = "{yaml_sha256}";
inline constexpr const char* kTorchVersion = "{torch_version}";

struct OpEntry {{
    // `aten::`-prefixed, exactly as the operator struct spells it, because
    // the schema hash is taken over that spelling.
    const char* name = nullptr;
    // Empty when the operator has no named overload.
    const char* overload = nullptr;
    // FNV-1a over "aten::name" plus ".overload" when the overload is named.
    // The same hash crucible_fallback.cpp:146 computes.
    uint64_t schema_hash = 0;
    // Arguments, not counting the keyword-only separator.
    uint8_t arity = 0;
    // Bit i is set when argument i is a tensor. Bit positions are argument
    // positions in the schema, which are the parameter positions of
    // `call`; record_kernel.h asserts that correspondence by reflection.
    uint32_t tensor_arg_mask = 0;
    // Bit i is set when argument i is a list of tensors.
    uint32_t tensor_list_mask = 0;
    // OPAQUE when the taxonomy does not name this operation. Never a guess:
    // Graph.h reads contiguous CKernelId ranges, so a wrong identifier
    // changes an operation's NodeKind rather than merely mislabelling it.
    CKernelId kernel_id = CKernelId::OPAQUE;
    // True when an argument writes through an alias, which is the value
    // `c10::FunctionSchema::is_mutable()` returns for this operator. The
    // boxed fallback asks the parser at run time; a recording kernel reads
    // it from here, so both paths set the IS_MUTABLE op flag from one fact.
    bool is_mutable = false;
}};

inline constexpr std::array<OpEntry, {op_count}> aten_op_table{{{{
{rows}
}}}};

// The operator structs, paired with their table index, for register.cpp to
// expand. A type cannot be spelled as data, so this is the one place the
// generator emits tokens rather than values.
#define CRUCIBLE_ATEN_OP_LIST(X) \\
{op_list}

}}  // namespace crucible::vessel
'''


def emit(ops: list[Op], yaml_sha256: str, yaml_entries: int, torch_version: str) -> str:
    rows = []
    for op in ops:
        rows.append(
            f'    OpEntry{{"{op.name}", "{op.overload}", '
            f"{schema_hash(op.name, op.overload):#018x}ULL, "
            f"{op.arity}, {op.tensor_arg_mask:#010x}, {op.tensor_list_mask:#010x}, "
            f"CKernelId::{op.ckernel}, {'true' if op.is_mutable else 'false'}}},"
        )
    op_list = " \\\n".join(
        f"    X({i}, {op.struct})" for i, op in enumerate(ops)
    )
    classified = sum(1 for op in ops if op.ckernel != OPAQUE)
    return HEADER_TEMPLATE.format(
        torch_version=torch_version,
        yaml_sha256=yaml_sha256,
        yaml_entries=yaml_entries,
        op_count=len(ops),
        classified=classified,
        opaque=len(ops) - classified,
        rows="\n".join(rows),
        op_list=op_list,
    )


def torch_version_of(torch_dir: Path) -> str:
    version_py = torch_dir / "torch" / "version.py"
    if not version_py.exists():
        return "unknown"
    for line in version_py.read_text(encoding="utf-8").splitlines():
        if line.startswith("__version__"):
            return line.split("=", 1)[1].strip().strip("'\"")
    return "unknown"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--torch-dir",
        required=True,
        type=Path,
        help="site-packages directory holding torch/include/ATen/ops",
    )
    ap.add_argument(
        "--pytorch-src",
        type=Path,
        default=Path.home() / "Downloads" / "pytorch",
        help="PyTorch source tree holding native_functions.yaml",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent.parent
        / "vessel"
        / "torch"
        / "aten_op_table.h",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="regenerate and compare with the file on disk; exit 1 on drift",
    )
    args = ap.parse_args()

    ops_dir = args.torch_dir / "torch" / "include" / "ATen" / "ops"
    if not ops_dir.is_dir():
        raise SystemExit(f"no ATen/ops directory under {args.torch_dir}")
    yaml_path = (
        args.pytorch_src / "aten" / "src" / "ATen" / "native" / "native_functions.yaml"
    )
    if not yaml_path.is_file():
        raise SystemExit(f"no native_functions.yaml at {yaml_path}")

    ops = read_ops(ops_dir)
    digest, entries, matched = read_yaml_facts(yaml_path, ops)
    text = emit(ops, digest, entries, torch_version_of(args.torch_dir))

    if args.check:
        if not args.output.exists():
            print(f"{args.output} does not exist", file=sys.stderr)
            return 1
        if args.output.read_text(encoding="utf-8") != text:
            print(
                f"{args.output} is stale; run scripts/gen-aten-op-table.py",
                file=sys.stderr,
            )
            return 1
        print(f"{args.output}: up to date ({len(ops)} operators)")
        return 0

    args.output.write_text(text, encoding="utf-8")
    classified = sum(1 for op in ops if op.ckernel != OPAQUE)
    print(
        f"{args.output}: {len(ops)} operators, {classified} classified, "
        f"{len(ops) - classified} OPAQUE"
    )
    print(f"  yaml {digest[:16]}… {matched}/{entries} func entries matched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
