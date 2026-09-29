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

WHAT READS EACH INPUT

Each input has a real parser, and the generator uses it:

    the ATen/ops/*_ops.h headers   the pinned tree-sitter kit (utils/scripts/tsast.py)
    each schema_str                torchgen's own FunctionSchema.parse
    native_functions.yaml          a YAML parser
    torch/version.py               the Python ast module

torchgen comes from the site-packages directory that --torch-dir names, so
the schema grammar is the grammar of the fork that built the wheel.  A
header the kit cannot read stops the generator, because an operator struct
that the parse does not see would be a missing row.

Usage:
    gen-aten-op-table.py --torch-dir DIR [--pytorch-src DIR] [-o FILE]
    gen-aten-op-table.py --torch-dir DIR --check   # regenerate and diff
    gen-aten-op-table.py --torch-dir DIR --self-test
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)
from repo_root import REPO_ROOT  # noqa: E402

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

# The namespace that holds every generated operator struct.
OPS_NAMESPACE = ("at", "_ops")
# The three string members that each operator struct carries.
STRING_MEMBERS = ("name", "overload_name", "schema_str")
# The two static member functions that make a struct an operator.
FUNCTION_MEMBERS = frozenset({"call", "redispatch"})
# The declarator kinds that stand between a member name and its declaration.
_WRAPPERS = frozenset({"pointer_declarator", "reference_declarator", "function_declarator"})


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


def load_schema_model(torch_dir: Path) -> ModuleType:
    """Return torchgen's model module from the site-packages of the fork.

    Args:
        torch_dir: The site-packages directory that holds torch and torchgen

    Returns:
        The `torchgen.model` module, whose FunctionSchema.parse is the grammar
        that generated every schema_str

    Raises:
        SystemExit: If that directory holds no torchgen, or a different
            torchgen shadows it
    """
    model_file = torch_dir / "torchgen" / "model.py"
    if not model_file.is_file():
        raise SystemExit(f"no torchgen under {torch_dir}. The schema grammar must come from the fork "
                         f"that built the wheel, so point --torch-dir at its site-packages.")
    sys.path.insert(0, str(torch_dir))
    import torchgen.model as model  # noqa: PLC0415  (the path insert above decides which one loads)
    if Path(model.__file__).resolve() != model_file.resolve():
        raise SystemExit(f"torchgen loads from {model.__file__}, not from {torch_dir}. "
                         f"Remove the other torchgen from the path.")
    return model


def schema_facts(schema_text: str, model: ModuleType) -> tuple[int, int, int, bool]:
    """Return the arity, the two masks and the mutation flag of one schema.

    The facts come from torchgen's own parse of the schema, so the argument
    positions are the positions torchgen gave to `call`.  An argument whose
    type is tensor-like and list-like is a tensor list, for example
    `Tensor?[]` and `Tensor[]`.  A tensor-like argument that is no list is a
    tensor, for example `Tensor?` and `Tensor(a!)`.  The mutation flag is the
    schema's own `is_mutable`, which is true when an argument carries a write
    annotation, as `c10::FunctionSchema::is_mutable()` asks at run time.

    Args:
        schema_text: One schema string, with its C escapes already removed
        model: The torchgen model module from `load_schema_model`

    Returns:
        (arity, tensor_arg_mask, tensor_list_mask, is_mutable)
    """
    parsed = model.FunctionSchema.parse(schema_text)
    arguments = parsed.arguments.flat_all
    tensor_mask = 0
    list_mask = 0
    for index, argument in enumerate(arguments):
        if not argument.type.is_tensor_like():
            continue
        if argument.type.is_list_like() is not None:
            list_mask |= 1 << index
        else:
            tensor_mask |= 1 << index
    return len(arguments), tensor_mask, list_mask, parsed.is_mutable


def member_name(field: tsast.Node) -> tuple[str, bool] | None:
    """Return the name that a member declaration declares, and whether it is a function.

    Args:
        field: A field_declaration node of a struct body

    Returns:
        (name, is_function), or None when the declarator names no field
    """
    declarator = field.child_by_field("declarator")
    is_function = False
    while declarator is not None and declarator.type in _WRAPPERS:
        is_function = is_function or declarator.type == "function_declarator"
        declarator = declarator.child_by_field("declarator")
    if declarator is None or declarator.type != "field_identifier":
        return None
    return declarator.text, is_function


def string_value(literal: tsast.Node) -> str:
    """Return the value of a C string literal node, with its escapes resolved.

    Only `_test_string_default` needs the escapes resolved, but it needs them.

    Args:
        literal: A string_literal node with no prefix

    Returns:
        The characters the literal denotes
    """
    return literal.text[1:-1].encode().decode("unicode_escape")


def ops_of_tree(tree: tsast.Tree, model: ModuleType) -> list[Op]:
    """Return every operator struct of one parsed header, in source order.

    An operator struct sits in namespace `at::_ops` and declares the static
    member functions `call` and `redispatch`.  Its `name`, `overload_name`
    and `schema_str` members are string literals.  A comment names nothing,
    so a commented-out member cannot stand in for the real one.

    Args:
        tree: One parsed ATen/ops header
        model: The torchgen model module

    Returns:
        One Op for each operator struct

    Raises:
        SystemExit: If an operator struct lacks one of the three strings, or
            has more arguments than a mask can address
    """
    header = Path(tree.path).name
    ops: list[Op] = []
    for struct in tree.find("struct_specifier"):
        named = struct.child_by_field("name")
        body = struct.child_by_field("body")
        if named is None or body is None or tsast.namespace_path(struct) != OPS_NAMESPACE:
            continue
        strings: dict[str, str] = {}
        functions: set[str] = set()
        for field in body.children_of_type("field_declaration"):
            member = member_name(field)
            if member is None:
                continue
            name, is_function = member
            if is_function:
                functions.add(name)
                continue
            value = field.child_by_field("default_value")
            if name in STRING_MEMBERS and value is not None and value.type == "string_literal":
                strings[name] = string_value(value)
        if not FUNCTION_MEMBERS <= functions:
            continue
        missing = [member for member in STRING_MEMBERS if member not in strings]
        if missing:
            raise SystemExit(
                f"{header}: struct {named.text} declares call and redispatch but no string "
                f"member {', '.join(missing)}. The generated header shape changed."
            )
        schema_text = strings["schema_str"]
        arity, tensor_mask, list_mask, mutable = schema_facts(schema_text, model)
        if arity > 32:
            raise SystemExit(
                f"{strings['name']}.{strings['overload_name']} has {arity} "
                f"arguments, past the 32 a uint32_t mask can address. Widen "
                f"OpEntry's mask fields to uint64_t and this check with them."
            )
        ops.append(Op(
            struct=named.text,
            header=header,
            name=strings["name"],
            overload=strings["overload_name"],
            schema_str=schema_text,
            arity=arity,
            tensor_arg_mask=tensor_mask,
            tensor_list_mask=list_mask,
            is_mutable=mutable,
            ckernel=ckernel_for(strings["name"]),
        ))
    return ops


def read_ops(ops_dir: Path, model: ModuleType) -> list[Op]:
    """Return every operator struct of the ops headers, header by header in sorted order.

    Complexity: linear in the total size of the headers, from one kit parse.

    Args:
        ops_dir: The ATen/ops directory of the wheel
        model: The torchgen model module

    Returns:
        Every operator, in header order and then in source order

    Raises:
        SystemExit: If a header does not parse, or no operator struct exists
    """
    ops: list[Op] = []
    for tree in tsast.parse(sorted(ops_dir.glob("*_ops.h")), strict=False):
        if tree.diagnostic is not None:
            raise SystemExit(f"{tree.path}: the kit cannot parse this header, so its operator structs "
                             f"are unknown.\n  {tree.diagnostic}")
        ops += ops_of_tree(tree, model)
    if not ops:
        raise SystemExit(f"no operator structs found under {ops_dir}")
    return ops


# ---------------------------------------------------------------------
# The yaml, read for provenance and for the cross-check.


def yaml_funcs(raw: bytes) -> list[str]:
    """Return the `func:` value of each entry of native_functions.yaml, in order.

    Args:
        raw: The bytes of the yaml file

    Returns:
        One schema string for each entry that carries a func key

    Raises:
        SystemExit: If the file is not a list of entries
    """
    import yaml  # noqa: PLC0415  (only the generator run needs the parser)

    entries = yaml.load(raw, Loader=getattr(yaml, "CSafeLoader", yaml.SafeLoader))
    if not isinstance(entries, list):
        raise SystemExit("native_functions.yaml is not a list of entries.")
    return [entry["func"] for entry in entries if isinstance(entry, dict) and isinstance(entry.get("func"), str)]


def read_yaml_facts(yaml_path: Path, ops: list[Op]) -> tuple[str, int, int]:
    """(sha256, entry count, matched count), and a hard error on a mismatch.

    Every `func:` string in the yaml must appear verbatim as some
    operator's schema_str. The reverse does not hold, because torchgen
    generates 520 operators the yaml only implies.
    """
    raw = yaml_path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    funcs = yaml_funcs(raw)
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

// aten_op_table.h — GENERATED by utils/scripts/gen-aten-op-table.py. Do not edit.
// clang-format off: the generator owns this layout, and --check compares it byte for byte.
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
    """Return the `__version__` string that torch/version.py assigns.

    The Python ast reads the module, so a docstring or a comment that
    mentions `__version__` cannot stand in for the assignment.

    Args:
        torch_dir: The site-packages directory that holds torch

    Returns:
        The version string, or "unknown" when the file or the assignment is absent
    """
    version_py = torch_dir / "torch" / "version.py"
    if not version_py.exists():
        return "unknown"
    module = ast.parse(version_py.read_text(encoding="utf-8"))
    for statement in module.body:
        if isinstance(statement, ast.Assign):
            targets = statement.targets
        elif isinstance(statement, ast.AnnAssign):
            targets = [statement.target]
        else:
            continue
        named = any(isinstance(target, ast.Name) and target.id == "__version__" for target in targets)
        if named and isinstance(statement.value, ast.Constant) and isinstance(statement.value.value, str):
            return statement.value.value
    return "unknown"


SELF_TEST_HEADER = r'''#pragma once
namespace at {
namespace _ops {

struct TORCH_API add_Tensor {
  using schema = at::Tensor (const at::Tensor &, const at::Tensor &, const at::Scalar &);
  // static constexpr const char* name = "aten::commented_out";
  static constexpr const char* name = "aten::add";
  static constexpr const char* overload_name = "Tensor";
  static constexpr const char* schema_str = "add.Tensor(Tensor self, Tensor other, *, Scalar alpha=1) -> Tensor";
  static at::Tensor call(const at::Tensor & self, const at::Tensor & other, const at::Scalar & alpha);
  static at::Tensor redispatch(c10::DispatchKeySet ks, const at::Tensor & self, const at::Tensor & other, const at::Scalar & alpha);
};

struct TORCH_API index_put_out {
  static constexpr const char* name = "aten::index_put";
  static constexpr const char* overload_name = "out";
  static constexpr const char* schema_str = "index_put.out(Tensor self, Tensor?[] indices, Tensor values, bool accumulate=False, *, Tensor(a!) out) -> Tensor(a!)";
  static at::Tensor & call(const at::Tensor & self, const c10::List<::std::optional<at::Tensor>> & indices, const at::Tensor & values, bool accumulate, at::Tensor & out);
  static at::Tensor & redispatch(c10::DispatchKeySet ks, const at::Tensor & self, const c10::List<::std::optional<at::Tensor>> & indices, const at::Tensor & values, bool accumulate, at::Tensor & out);
};

struct TORCH_API chunk {
  static constexpr const char* name = "aten::chunk";
  static constexpr const char* overload_name = "";
  static constexpr const char* schema_str = "chunk(Tensor(a -> *) self, int chunks, int dim=0) -> Tensor(a)[]";
  static ::std::vector<at::Tensor> call(const at::Tensor & self, int64_t chunks, int64_t dim);
  static ::std::vector<at::Tensor> redispatch(c10::DispatchKeySet ks, const at::Tensor & self, int64_t chunks, int64_t dim);
};

struct TORCH_API _test_string_default {
  static constexpr const char* name = "aten::_test_string_default";
  static constexpr const char* overload_name = "";
  static constexpr const char* schema_str = "_test_string_default(Tensor dummy, str a=\"\\\"'\\\\\", str b='\"\\'\\\\') -> Tensor";
  static at::Tensor call(const at::Tensor & dummy, c10::string_view a, c10::string_view b);
  static at::Tensor redispatch(c10::DispatchKeySet ks, const at::Tensor & dummy, c10::string_view a, c10::string_view b);
};

struct TORCH_API no_redispatch {
  static constexpr const char* name = "aten::no_redispatch";
  static constexpr const char* overload_name = "";
  static constexpr const char* schema_str = "no_redispatch(Tensor self) -> Tensor";
  static at::Tensor call(const at::Tensor & self);
};

}  // namespace _ops
}  // namespace at

namespace other {
struct TORCH_API outside_ops {
  static constexpr const char* name = "aten::outside_ops";
  static constexpr const char* overload_name = "";
  static constexpr const char* schema_str = "outside_ops(Tensor self) -> Tensor";
  static at::Tensor call(const at::Tensor & self);
  static at::Tensor redispatch(c10::DispatchKeySet ks, const at::Tensor & self);
};
}  // namespace other
'''

SELF_TEST_YAML = '''\
# - func: in_a_comment(Tensor self) -> Tensor
- func: add.Tensor(Tensor self, Tensor other, *, Scalar alpha=1) -> Tensor
  variants: function, method
- func: "chunk(Tensor(a -> *) self, int chunks, int dim=0) -> Tensor(a)[]"
'''

SELF_TEST_VERSION = '''\
"""The version.

__version__ = 'from-the-docstring'
"""
__version__: str = '9.9.9'
'''


def self_test(torch_dir: Path) -> int:
    """Plant an ops header, a yaml and a version module, and check each reader.

    Args:
        torch_dir: The site-packages directory that holds torchgen

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    print("gen-aten-op-table --self-test")
    model = load_schema_model(torch_dir)
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        ops_dir = root / "ops"
        ops_dir.mkdir()
        (ops_dir / "planted_ops.h").write_text(SELF_TEST_HEADER, encoding="utf-8")
        ops = {op.struct: op for op in read_ops(ops_dir, model)}
        expect("reads the four operator structs of at::_ops, in source order",
               list(ops) == ["add_Tensor", "index_put_out", "chunk", "_test_string_default"])
        add = ops.get("add_Tensor")
        expect("a commented-out member does not stand in for the real one",
               add is not None and add.name == "aten::add", True)
        expect("a keyword-only argument keeps its position and is no tensor",
               add is not None and (add.arity, add.tensor_arg_mask, add.tensor_list_mask) == (3, 0b011, 0))
        put = ops.get("index_put_out")
        expect("a list of optional tensors is a tensor list, and a written out= argument is mutable",
               put is not None and (put.arity, put.tensor_arg_mask, put.tensor_list_mask, put.is_mutable)
               == (5, 0b10101, 0b00010, True))
        chunk = ops.get("chunk")
        expect("an arrow inside an alias annotation is not the return arrow",
               chunk is not None and (chunk.arity, chunk.tensor_arg_mask, chunk.is_mutable) == (3, 0b001, False))
        escaped = ops.get("_test_string_default")
        expect("the C escapes of a schema string are resolved before the parse",
               escaped is not None and escaped.arity == 3 and escaped.schema_str.startswith('_test_string_default(Tensor dummy, str a="'))
        expect("a struct with no redispatch is no operator", "no_redispatch" not in ops, True)
        expect("a struct outside at::_ops is no operator", "outside_ops" not in ops, True)
        expect("a yaml comment names no func, and a quoted func is read unquoted",
               yaml_funcs(SELF_TEST_YAML.encode()) == [
                   "add.Tensor(Tensor self, Tensor other, *, Scalar alpha=1) -> Tensor",
                   "chunk(Tensor(a -> *) self, int chunks, int dim=0) -> Tensor(a)[]"], True)
        (root / "torch").mkdir()
        (root / "torch" / "version.py").write_text(SELF_TEST_VERSION, encoding="utf-8")
        expect("the version comes from the assignment, not from a docstring line",
               torch_version_of(root) == "9.9.9", True)
        (ops_dir / "broken_ops.h").write_text("namespace at { namespace _ops { void f() { g(1) { } } } }\n",
                                             encoding="utf-8")
        try:
            read_ops(ops_dir, model)
            refused = False
        except SystemExit:
            refused = True
        expect("a header the kit cannot parse stops the generator", refused, True)
    if failures:
        print(f"gen-aten-op-table --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"gen-aten-op-table --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main() -> int:
    """Parse the arguments and run one mode.

    Returns:
        The exit code
    """
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
        default=REPO_ROOT / "vessel" / "torch" / "aten_op_table.h",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="regenerate and compare with the file on disk; exit 1 on drift",
    )
    ap.add_argument(
        "--self-test",
        action="store_true",
        help="check each reader against planted inputs; needs torchgen from --torch-dir",
    )
    args = ap.parse_args()
    try:
        if args.self_test:
            return self_test(args.torch_dir)
        return generate(args)
    except tsast.KitMissing as exc:
        print(f"gen-aten-op-table: {exc}", file=sys.stderr)
        return 3


def generate(args: argparse.Namespace) -> int:
    """Read the fork, emit the table, and write it or compare it.

    Args:
        args: The parsed command line

    Returns:
        0 on success, 1 when --check finds drift
    """
    model = load_schema_model(args.torch_dir)
    ops_dir = args.torch_dir / "torch" / "include" / "ATen" / "ops"
    if not ops_dir.is_dir():
        raise SystemExit(f"no ATen/ops directory under {args.torch_dir}")
    yaml_path = (
        args.pytorch_src / "aten" / "src" / "ATen" / "native" / "native_functions.yaml"
    )
    if not yaml_path.is_file():
        raise SystemExit(f"no native_functions.yaml at {yaml_path}")

    ops = read_ops(ops_dir, model)
    digest, entries, matched = read_yaml_facts(yaml_path, ops)
    text = emit(ops, digest, entries, torch_version_of(args.torch_dir))

    if args.check:
        if not args.output.exists():
            print(f"{args.output} does not exist", file=sys.stderr)
            return 1
        if args.output.read_text(encoding="utf-8") != text:
            print(
                f"{args.output} is stale; run utils/scripts/gen-aten-op-table.py",
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
