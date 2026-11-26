"""Crucible TorchDispatchMode — MVP PROOF-OF-CONCEPT. COMPLETE BULLSHIT.

WARNING: This is a throwaway prototype. It uses TorchDispatchMode (Python-level
dispatch) instead of the real C++ DispatchKey::Crucible. Known problems:

  - The C entry of this mode takes no module scope, so no op of this mode
    has a scope hash.  The native vessel keeps the scope of each op.
  - No autograd hooks at all — we don't track which backward op corresponds
    to which forward op. The grad_fn_hash field exists but is never correlated.
  - In-place ops and views share data_ptr, producing wrong edges downstream.
  - Python dispatch overhead is ~10μs/op. Real Crucible is ~5ns/op via C++.
  - No compiled mode, no shadow handles, no replay — just recording.
  - The mode does not see the ops that a kernel calls through the
    dispatcher, which the native vessel records as nested ops.  The entry of
    each op that the mode sees is the entry of the native vessel
    (test_mode_entries.py).

The REAL implementation uses DispatchKey::Crucible in c10 (see patches/).
This file exists ONLY for recording traces to feed the visualization pipeline.
The .crtrace file comes from the C++ encoder of vessel_api.cpp, which is the
one writer of the format.

Usage:
    from crucible_mode import CrucibleMode

    with CrucibleMode() as mode:
        train_step(model, x)
        mode.export_trace("trace.crtrace")
"""

import ctypes
import struct
import sys
from typing import NamedTuple

import torch
from torch.utils._python_dispatch import TorchDispatchMode

from crucible_native import (
    CrucibleMeta,
    DispatchAction,
    DispatchLimit,
    DispatchStatus,
    SchemaNameResult,
    _DispatchLib,
    _VesselLib as _NativeVesselLib,
    check_handle_status,
    wrap_tensor_backward,
)


# ─── ctypes bindings ────────────────────────────────────────────────

class _VesselLib(_NativeVesselLib):
    """The vessel wrapper of crucible_native with the names of the ops of this mode.

    The wrapper of crucible_native loads the library once its layout is the
    layout of its build, it declares each entry from its line of the layout
    table, and it checks the handle status of each entry that takes the handle
    of the Vigil.  The mode calls the other entries through _lib, which holds
    only the entries of the table.
    """

    def register_schema_name(self, h: int, schema_hash: int, name: bytes) -> SchemaNameResult:
        """Register the name of an op for its schema hash in the tables of the process, and give the result.

        Raises:
            CrucibleHandleError: If the library refuses the handle
        """
        code = ctypes.c_uint32(0)
        check_handle_status(self._lib.crucible_register_schema_name(h, schema_hash, name, ctypes.byref(code)),
                            "crucible_register_schema_name")
        return SchemaNameResult(code.value)


# ─── Dtype mapping ──────────────────────────────────────────────────

# c10::ScalarType ordinals — must match Types.h exactly.
_DTYPE_MAP = {
    torch.uint8: 0,
    torch.int8: 1,
    torch.int16: 2,
    torch.int32: 3,
    torch.int64: 4,
    torch.float16: 5,
    torch.float32: 6,
    torch.float64: 7,
    torch.complex32: 8,
    torch.complex64: 9,
    torch.complex128: 10,
    torch.bool: 11,
    torch.bfloat16: 15,
}
# Add FP8/extended types if available in this PyTorch build.  uint16, uint32
# and uint64 keep their c10 ordinals, which name no crucible element type, so
# the trust ladder of the C ABI refuses an operation with such a tensor.
for _name, _ord in [
    ("float8_e5m2", 23), ("float8_e4m3fn", 24),
    ("float8_e5m2fnuz", 25), ("float8_e4m3fnuz", 26),
    ("uint16", 27), ("uint32", 28), ("uint64", 29),
    ("float8_e8m0fnu", 44), ("float4_e2m1fn_x2", 45),
]:
    _dt = getattr(torch, _name, None)
    if _dt is not None:
        _DTYPE_MAP[_dt] = _ord

# c10::DeviceType ordinals — must match Types.h exactly.
_DEVICE_MAP = {
    "cpu": 0,
    "cuda": 1,
    "mkldnn": 2,
    "hip": 6,
    "xla": 9,
    "mps": 13,
    "meta": 14,
    "privateuseone": 20,
}

# The ordinals of crucible::Layout, which differ from the c10 ordinals from
# sparse_csc on.
_LAYOUT_MAP = {
    torch.strided: 0,
    torch.sparse_coo: 1,
}
# Add layouts that may not exist in all builds.
for _name, _ord in [("sparse_csr", 2), ("sparse_csc", 3),
                     ("sparse_bsr", 4), ("sparse_bsc", 5)]:
    _ly = getattr(torch, _name, None)
    if _ly is not None:
        _LAYOUT_MAP[_ly] = _ord

# The byte of an element type, a device or a layout that crucible does not
# name.  No enumerator of crucible::ScalarType, crucible::DeviceType or
# crucible::Layout has this value, so the trust ladder of the C ABI refuses
# the operation, as the decoders of the native vessel refuse it.
_UNNAMED_ORDINAL = 127

# The lanes of the sizes and of the strides of a CrucibleMeta (kMaxTensorNDim
# of crucible/TensorMeta.h), and the largest rank that its byte holds.
MAX_META_LANES = 8
MAX_META_RANK_BYTE = 255


def _dtype_ordinal(dtype: torch.dtype) -> int:
    """Return the crucible ordinal of an element type, or _UNNAMED_ORDINAL."""
    return _DTYPE_MAP.get(dtype, _UNNAMED_ORDINAL)


def _device_ordinal(device_type: str) -> int:
    """Return the crucible ordinal of a device type name, or _UNNAMED_ORDINAL."""
    return _DEVICE_MAP.get(device_type, _UNNAMED_ORDINAL)


def _layout_ordinal(layout: torch.layout) -> int:
    """Return the crucible ordinal of a layout, or _UNNAMED_ORDINAL."""
    return _LAYOUT_MAP.get(layout, _UNNAMED_ORDINAL)


# ─── Scalar encoding ───────────────────────────────────────────────

_FNV_OFFSET = 0xCBF29CE484222325


def _fnv1a(s: bytes) -> int:
    """FNV-1a 64-bit fold, the fnv1a_text of vessel/torch/entry_slots.h: the fold of the grad_fn hash."""
    h = _FNV_OFFSET
    for b in s:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _schema_hash(name: bytes) -> int:
    """Return the schema hash of an op name, as schema_hash_of_name of vessel/torch/entry_slots.h gives it.

    The fold zero is the sentinel of no schema, so it maps to the fold of the empty text.  crucible_hash_string of
    vessel_api.cpp gives the same value.
    """
    fold = _fnv1a(name)
    return fold if fold != 0 else _FNV_OFFSET


# ─── The arguments of an op ────────────────────────────────────────
#
# The native fallback reads the boxed stack of an op (extract_inputs of
# vessel/torch/crucible_fallback.cpp), and this mode reads the args and the
# kwargs that PyTorch gives __torch_dispatch__.  PyTorch makes those from the
# same stack, with two changes: it leaves out each trailing positional
# argument and each kwarg-only argument at its default value, and it gives a
# dtype, a layout and a memory format as Python objects, which the stack holds
# as integers.  So the mode walks the arguments of the schema in their order,
# takes the default of each argument that PyTorch left out, and pushes the
# slots that extract_inputs pushes for the boxed value.  The rules of
# extract_inputs, for the value of each argument:
#
#   None of an optional       one slot with the tag NONE, structural
#   None of a tensor that is  no slot: the stack holds an undefined tensor
#   no optional
#   a tensor                  one input record
#   a Python number in a      one input record of rank zero: PyTorch wraps the
#   tensor position           number into a tensor before the dispatch
#   a list of tensors         one input record for each tensor
#   a list of optional        one input record for each tensor, and one NONE
#   tensors                   slot for each None
#   int, SymInt               one INT slot
#   float                     one FLOAT slot
#   bool                      one BOOL slot
#   Scalar                    one slot with the tag of its Python type, and no
#                             slot for a complex number
#   int[], SymInt[]           one INT slot for each element
#   float[]                   one FLOAT slot for each element
#   Scalar[]                  one slot for each element, as for a Scalar
#   ScalarType, Layout,       one INT slot with the c10 ordinal
#   MemoryFormat
#   each other type           no slot, such as a Device, a str or a bool[]
#
# These rules are a second statement of extract_inputs.  test_mode_entries.py
# records one op for each rule under this mode and under CrucibleNative, and
# it compares the two entries, so it keeps the two statements equal.  The rule
# of None of a tensor that is no optional has no case: a Python caller cannot
# give an op an undefined tensor, and only a kernel of PyTorch can.

# The tags of a scalar slot, as ScalarType2 of crucible/TraceRing.h numbers
# them, and the roles of a slot, as ScalarRole of crucible/Types.h numbers them.
TAG_INT, TAG_FLOAT, TAG_BOOL, TAG_NONE = 0, 1, 2, 3
ROLE_STRUCTURAL, ROLE_DATA = 0, 1

# The positions of the arguments of a schema that the native paths give a
# role (kMaxSchemaArguments of vessel/torch/entry_slots.h).  An argument at a
# later position is structural.
MAX_SCHEMA_ARGUMENTS = 32

# The op flags of an entry (op_flag of crucible/TraceRing.h), and the training
# phases of bits 2 and 3 (TrainingPhase of crucible/Types.h).
OP_INFERENCE_MODE = 1 << 0
OP_IS_MUTABLE = 1 << 1
OP_PHASE_SHIFT = 2
OP_TORCH_FUNCTION = 1 << 4
OP_GRAD_ENABLED = 1 << 5
PHASE_FORWARD, PHASE_BACKWARD, PHASE_OPTIMIZER = 0, 1, 2

# The kinds of an argument of a schema, one for each row of the rules above
# that reads the type of the argument.
(_KIND_TENSOR, _KIND_TENSOR_LIST, _KIND_OPTIONAL_TENSOR_LIST, _KIND_INT, _KIND_FLOAT, _KIND_BOOL, _KIND_NUMBER,
 _KIND_INT_LIST, _KIND_FLOAT_LIST, _KIND_NUMBER_LIST, _KIND_DTYPE, _KIND_LAYOUT, _KIND_MEMORY_FORMAT,
 _KIND_OTHER) = range(14)

# The kind of each type of a schema, by the name of its kind.  The kind of an
# optional is the kind of its payload.
_KIND_OF_TYPE = {
    "TensorType": _KIND_TENSOR,
    "IntType": _KIND_INT,
    "SymIntType": _KIND_INT,
    "FloatType": _KIND_FLOAT,
    "BoolType": _KIND_BOOL,
    "NumberType": _KIND_NUMBER,
    "ScalarTypeType": _KIND_DTYPE,
    "LayoutType": _KIND_LAYOUT,
    "MemoryFormatType": _KIND_MEMORY_FORMAT,
}

# The kind of a list, by the name of the kind of its element.  A list of
# optional tensors is a kind of its own.
_KIND_OF_LIST = {
    "TensorType": _KIND_TENSOR_LIST,
    "IntType": _KIND_INT_LIST,
    "SymIntType": _KIND_INT_LIST,
    "FloatType": _KIND_FLOAT_LIST,
    "NumberType": _KIND_NUMBER_LIST,
}

# The position of a kwarg-only argument in the walk: past each args tuple, so
# the walk reads its value from the kwargs.
_KWARG_ONLY = sys.maxsize

# The c10 ordinal of each element type (c10::ScalarType of
# torch/headeronly/core/ScalarType.h), which the boxed stack holds for an
# argument of the type ScalarType.  These differ from the crucible ordinals of
# _DTYPE_MAP, which name only the element types of a tensor that crucible
# reads.  test_mode_entries.py compares each row with torch.ops.prim.dtype.
_C10_DTYPE_ORDINALS = {
    getattr(torch, name): ordinal for name, ordinal in (
        ("uint8", 0), ("int8", 1), ("int16", 2), ("int32", 3), ("int64", 4), ("float16", 5), ("float32", 6),
        ("float64", 7), ("complex32", 8), ("complex64", 9), ("complex128", 10), ("bool", 11), ("qint8", 12),
        ("quint8", 13), ("qint32", 14), ("bfloat16", 15), ("quint4x2", 16), ("quint2x4", 17), ("bits1x8", 18),
        ("bits2x4", 19), ("bits4x2", 20), ("bits8", 21), ("bits16", 22), ("float8_e5m2", 23),
        ("float8_e4m3fn", 24), ("float8_e5m2fnuz", 25), ("float8_e4m3fnuz", 26), ("uint16", 27), ("uint32", 28),
        ("uint64", 29), ("uint1", 30), ("uint2", 31), ("uint3", 32), ("uint4", 33), ("uint5", 34), ("uint6", 35),
        ("uint7", 36), ("int1", 37), ("int2", 38), ("int3", 39), ("int4", 40), ("int5", 41), ("int6", 42),
        ("int7", 43), ("float8_e8m0fnu", 44), ("float4_e2m1fn_x2", 45), ("bcomplex32", 46))
    if hasattr(torch, name)
}

# The c10 ordinal of each layout (c10::Layout of
# torch/headeronly/core/Layout.h) and of each memory format (c10::MemoryFormat
# of torch/headeronly/core/MemoryFormat.h).  legacy_contiguous_format is the
# object of contiguous_format.
_C10_LAYOUT_ORDINALS = {
    getattr(torch, name): ordinal for name, ordinal in (
        ("strided", 0), ("sparse_coo", 1), ("sparse_csr", 2), ("_mkldnn", 3), ("sparse_csc", 4),
        ("sparse_bsr", 5), ("sparse_bsc", 6), ("jagged", 7))
    if hasattr(torch, name)
}
_C10_MEMORY_FORMAT_ORDINALS = {
    torch.contiguous_format: 0,
    torch.preserve_format: 1,
    torch.channels_last: 2,
    torch.channels_last_3d: 3,
}

# The crucible ordinal of the element type of a tensor that PyTorch wraps a
# Python number into: a double, a long, a bool or a complex double.
_WRAPPED_DTYPE = {float: 7, int: 4, bool: 11, complex: 10}
# The bytes of the storage of such a tensor.
_WRAPPED_NBYTES = {float: 8, int: 8, bool: 1, complex: 16}

_DOUBLE = struct.Struct("<d")
_INT64 = struct.Struct("<q")


def _is_data_type(argument_type) -> bool:
    """True for a schema type whose slots are data.

    The type Scalar or float, an optional of one, or a list of one, is data
    (ScalarRole of crucible/Types.h), unless crucible_is_structural_scalar
    names the argument.  The rule is data_arguments_of of
    vessel/torch/crucible_fallback.cpp: it reads the type of the schema, and
    no value.
    """
    if argument_type.kind() == "OptionalType":
        argument_type = argument_type.getElementType()
    if argument_type.kind() == "ListType":
        argument_type = argument_type.getElementType()
    return argument_type.kind() in ("NumberType", "FloatType")


def _kind_of(argument_type) -> tuple[int, bool]:
    """Return the kind of a type of a schema and True when the type is an optional."""
    is_optional = argument_type.kind() == "OptionalType"
    if is_optional:
        argument_type = argument_type.getElementType()
    if argument_type.kind() != "ListType":
        return _KIND_OF_TYPE.get(argument_type.kind(), _KIND_OTHER), is_optional
    element = argument_type.getElementType()
    if element.kind() == "OptionalType" and element.getElementType().kind() == "TensorType":
        return _KIND_OPTIONAL_TENSOR_LIST, is_optional
    return _KIND_OF_LIST.get(element.kind(), _KIND_OTHER), is_optional


def _push_number(value, role: int, values: list, tags: list, roles: list) -> None:
    """Push the slot of a Python number in a Scalar position: no slot for a complex number."""
    if isinstance(value, bool):
        values.append(int(value))
        tags.append(TAG_BOOL)
    elif isinstance(value, int):
        values.append(value)
        tags.append(TAG_INT)
    elif isinstance(value, float):
        values.append(_INT64.unpack(_DOUBLE.pack(value))[0])
        tags.append(TAG_FLOAT)
    else:
        return
    roles.append(role)


def _extract_inputs(steps: tuple, args: tuple, kwargs: dict) -> tuple[list, list[int], list[int], list[int]]:
    """Walk the arguments of an op by its plan, and return its inputs and the values, tags and roles of its slots.

    Each input is a tensor, or a Python number that PyTorch wraps into a
    tensor.  The walk follows the rules at the head of this section, which
    state the rules of extract_inputs of vessel/torch/crucible_fallback.cpp
    again.  test_mode_entries.py keeps the two statements equal.

    Complexity: O(arguments + elements of the lists).
    """
    inputs: list = []
    values: list[int] = []
    tags: list[int] = []
    roles: list[int] = []
    count = len(args)
    for position, name, default, kind, is_optional, role in steps:
        value = args[position] if position < count else kwargs.get(name, default)
        if value is None:
            # An undefined tensor of a position that is no optional gives no
            # slot.  PyTorch gives it as None too.
            if is_optional:
                values.append(0)
                tags.append(TAG_NONE)
                roles.append(ROLE_STRUCTURAL)
        elif kind == _KIND_TENSOR:
            inputs.append(value)
        elif kind == _KIND_TENSOR_LIST:
            inputs.extend(element for element in value if element is not None)
        elif kind == _KIND_OPTIONAL_TENSOR_LIST:
            for element in value:
                if element is None:
                    values.append(0)
                    tags.append(TAG_NONE)
                    roles.append(ROLE_STRUCTURAL)
                else:
                    inputs.append(element)
        elif kind == _KIND_INT:
            if isinstance(value, int):
                values.append(int(value))
                tags.append(TAG_INT)
                roles.append(role)
        elif kind == _KIND_FLOAT:
            values.append(_INT64.unpack(_DOUBLE.pack(float(value)))[0])
            tags.append(TAG_FLOAT)
            roles.append(role)
        elif kind == _KIND_BOOL:
            values.append(int(bool(value)))
            tags.append(TAG_BOOL)
            roles.append(role)
        elif kind == _KIND_NUMBER:
            _push_number(value, role, values, tags, roles)
        elif kind == _KIND_INT_LIST:
            for element in value:
                if isinstance(element, int):
                    values.append(int(element))
                    tags.append(TAG_INT)
                    roles.append(role)
        elif kind == _KIND_FLOAT_LIST:
            for element in value:
                values.append(_INT64.unpack(_DOUBLE.pack(float(element)))[0])
                tags.append(TAG_FLOAT)
                roles.append(role)
        elif kind == _KIND_NUMBER_LIST:
            for element in value:
                _push_number(element, role, values, tags, roles)
        elif kind in (_KIND_DTYPE, _KIND_LAYOUT, _KIND_MEMORY_FORMAT):
            ordinal = value if isinstance(value, int) else _ORDINALS_OF_KIND[kind].get(value)
            if ordinal is not None:
                values.append(ordinal)
                tags.append(TAG_INT)
                roles.append(role)
    return inputs, values, tags, roles


# The c10 ordinals of the objects of each kind that the stack holds as an
# integer.
_ORDINALS_OF_KIND = {
    _KIND_DTYPE: _C10_DTYPE_ORDINALS,
    _KIND_LAYOUT: _C10_LAYOUT_ORDINALS,
    _KIND_MEMORY_FORMAT: _C10_MEMORY_FORMAT_ORDINALS,
}


def _extract_outputs(result, num_returns: int) -> list:
    """Return the outputs of an op, as extract_outputs of vessel/torch/crucible_fallback.cpp reads the stack.

    Each output is a tensor, or a Python number, such as the value of
    _local_scalar_dense, that the native path records as a meta of rank zero.
    Complexity: O(returns + elements of the lists).
    """
    if num_returns == 0 or result is None:
        return []
    returned = (result,) if num_returns == 1 else result
    outputs = []
    for value in returned:
        if isinstance(value, (torch.Tensor, bool, int, float, complex)):
            outputs.append(value)
        elif isinstance(value, (list, tuple)):
            outputs.extend(element for element in value if isinstance(element, torch.Tensor))
    return outputs


def _fill_number_meta(meta, number, is_input: bool) -> None:
    """Fill the record of a Python number.

    An input is the tensor of rank zero that PyTorch wraps the number into.
    This mode sees no such tensor, so the record has no data address, and the
    memory plan gives it no slot.  An output is the meta of rank zero of
    scalar_result_meta of vessel/torch/entry_slots.h, with no storage.
    """
    kind = complex if isinstance(number, complex) else type(number)
    meta.ndim = 0
    meta.dtype = _WRAPPED_DTYPE.get(kind, _UNNAMED_ORDINAL)
    meta.device_type = _DEVICE_MAP["cpu"]
    meta.device_idx = -1
    meta.layout = _LAYOUT_MAP[torch.strided]
    if is_input:
        # A leaf and contiguous (the flags of _fill_meta).
        meta.flags = 0x03
        meta.storage_nbytes = _WRAPPED_NBYTES.get(kind, 0)


class _OpPlan(NamedTuple):
    """The walk plan of one operator overload, which the mode builds once.

    steps holds one tuple for each argument of the schema that can give an
    input or a slot: (position, name, default, kind, is_optional, role).  The
    position of a kwarg-only argument is _KWARG_ONLY.  schema_flags holds the
    op flags of the schema: IS_MUTABLE for an op that writes an argument.
    is_foreach is True for a fused multi-tensor op, whose name holds
    "_foreach_" (is_foreach_op_name of record_kernel.h).
    """

    name: str
    schema_hash: int
    num_returns: int
    steps: tuple
    schema_flags: int
    is_foreach: bool


# ─── CrucibleMode ──────────────────────────────────────────────────

class CrucibleMode(TorchDispatchMode):
    """TorchDispatchMode that feeds every ATen op into Crucible's Vigil.

    In RECORDING mode: ops execute eagerly, metadata is recorded.
    In COMPILED mode: guards are checked (still executes eagerly in
    this proof-of-concept — pre-allocated outputs require Tier 2).
    """

    def __init__(self, lib_path=None, verbose=False):
        super().__init__()
        self._vessel = _VesselLib(lib_path)
        # The dispatch library reads the eager word of this thread, which
        # PyTorch keeps partly in the FP control register.  This mode includes
        # no Crucible dispatch key, so the kernels of the library never fire.
        self._dispatch = _DispatchLib()
        # The create raises ValueError for a word that the library refuses,
        # and CrucibleWrongThreadError while another thread holds a live Vigil.
        self._handle = self._vessel.create(self._dispatch.eager_numerics())
        self._verbose = verbose
        self._op_count = 0
        self._schema_cache = {}
        # The walk plan of each operator overload that the mode saw
        # (_plan_of).  The table holds one plan for each overload, and
        # PyTorch registers a finite set of them (4407 schemas in the build of
        # the fork), so the table holds at most that many plans and the
        # overloads of the custom operators of the program.
        self._plans: dict = {}
        self._prev_compiled = False
        # The Tensor.backward before the wrapper of the backward window, while
        # the mode is entered.
        self._tensor_backward = None

    def __enter__(self):
        """Enter the mode, and wrap Tensor.backward in the backward window of the dispatch library.

        The phase of an op comes from the counter of that window, as on the
        native paths (wrap_tensor_backward of crucible_native.py).
        """
        entered = super().__enter__()
        self._tensor_backward = wrap_tensor_backward(self._dispatch)
        return entered

    def __exit__(self, exc_type, exc_value, traceback):
        """Leave the mode, put Tensor.backward back, and give up the producer claim of this thread.

        The create of the Vigil and each op claim the thread of the mode
        (crucible_create).  The exit gives the claim up (crucible_release_claim),
        so a mode object that lives on after its context, such as in a
        reference cycle, holds no claim, and a session of another thread can
        start.  The next op of the mode claims the Vigil again.
        """
        try:
            return super().__exit__(exc_type, exc_value, traceback)
        finally:
            if self._tensor_backward is not None:
                torch.Tensor.backward = self._tensor_backward
                self._tensor_backward = None
            if self._handle:
                self._vessel.release_claim(self._handle)

    def __del__(self):
        """End the Vigil of the mode.

        A finalizer raises nothing, so the status of the destroy stays unread.
        The garbage collector can run the finalizer on any thread.  After the
        exit of the mode, the slot of the Vigil is free, and the thread of the
        finalizer claims it and ends the Vigil.  If another thread still holds
        the Vigil, the destroy gives WRONG_THREAD, and the Vigil and its slot
        stay until the end of the process.  This leak is accepted: it reads no
        freed memory, and the slot counts against CRUCIBLE_VIGIL_SLOTS, so a
        create past the limit gives FULL.
        """
        handle = getattr(self, "_handle", 0)
        if handle:
            self._handle = 0
            self._vessel._lib.crucible_destroy(handle)

    # ── Public API ──────────────────────────────────────────────────
    #
    # Each query raises the CrucibleHandleError of crucible_native when the
    # library refuses the handle, such as on a thread that does not hold the
    # Vigil.

    def is_compiled(self):
        return self._vessel.is_compiled(self._handle)

    def active_num_ops(self) -> int:
        """Return the count of the ops of the active region, or 0 with no active region."""
        return self._vessel.active_num_ops(self._handle)

    def compiled_iterations(self):
        return self._vessel.compiled_iterations(self._handle)

    def diverged_count(self):
        return self._vessel.diverged_count(self._handle)

    def flush(self):
        """Wait until the background thread processed each recorded op.

        Raises RuntimeError when the background thread stopped first, because
        the ops after that point stay unprocessed.
        """
        self._vessel.flush(self._handle)

    # ── Trace export ─────────────────────────────────────────────────

    def export_trace(self, path) -> bool:
        """Write the active region of the Vigil to a .crtrace file.

        The C++ encoder of vessel_api.cpp (crucible_export_crtrace) writes the
        file.  It is the one writer of the format, so the file of this mode
        and the file of the native vessel hold the same fields, the guard word
        of each op included.

        Args:
            path: The path of the file to write

        Returns:
            True when the Vigil has an active region and the encoder wrote the
            file, False otherwise
        """
        return self._vessel.export_crtrace(self._handle, str(path))

    # ── Dispatch interception ───────────────────────────────────────

    def __torch_dispatch__(self, func, types, args=(), kwargs=None):
        kwargs = kwargs or {}

        # Execute eagerly first (always, in this POC).
        result = func(*args, **kwargs)

        plan = self._plans.get(func)
        if plan is None:
            plan = self._plan_of(func)
        op_name = plan.name
        inputs, values, tags, roles = _extract_inputs(plan.steps, args, kwargs)
        outputs = _extract_outputs(result, plan.num_returns)
        num_inputs = len(inputs)
        num_outputs = len(outputs)

        # The records of the op: [inputs..., outputs...]
        n_metas = num_inputs + num_outputs
        metas = None
        if n_metas > 0:
            metas = (CrucibleMeta * n_metas)()
            for i, item in enumerate(inputs):
                if isinstance(item, torch.Tensor):
                    self._fill_meta(metas[i], item)
                else:
                    _fill_number_meta(metas[i], item, is_input=True)
            for i, item in enumerate(outputs, num_inputs):
                if isinstance(item, torch.Tensor):
                    self._fill_meta(metas[i], item)
                else:
                    _fill_number_meta(metas[i], item, is_input=False)

        # The slots of the op as three arrays.  A count past the bound gives
        # the action REFUSED, and the entry reads no slot.  The count has 16
        # bits, so a longer list gives the bound plus one.
        num_scalars = len(values)
        value_array = tag_array = role_array = None
        if num_scalars > 0:
            value_array = (ctypes.c_int64 * num_scalars)(*values)
            tag_array = (ctypes.c_uint8 * num_scalars)(*tags)
            role_array = (ctypes.c_uint8 * num_scalars)(*roles)
        count_word = min(num_scalars, DispatchLimit.FFI_SCALARS + 1)

        flags = self._op_flags(plan, types)

        # Dispatch to Vigil
        dr = self._vessel._lib.crucible_dispatch_op_ex(
            self._handle,
            plan.schema_hash,
            num_inputs, num_outputs,
            metas, n_metas,
            value_array, tag_array, role_array, count_word,
            flags,
        )
        # A handle that the library refuses, such as on a thread that does not
        # hold the Vigil, raises its error here.  The op already ran eagerly.
        check_handle_status(dr.handle_status, "crucible_dispatch_op_ex")
        # The next op starts an iteration of a region, so the Vigil takes the
        # eager word again, once for each iteration.
        if dr.enters_region:
            self._vessel.observe_eager_numerics(self._handle, self._dispatch.eager_numerics())

        self._op_count += 1

        # Log mode transitions
        compiled_now = self.is_compiled()
        action = DispatchAction(dr.action)
        if compiled_now and not self._prev_compiled:
            print(f"  [crucible] COMPILED mode activated at op #{self._op_count} "
                  f"({op_name}), action={action.name}")
        self._prev_compiled = compiled_now

        if self._verbose:
            # The status is a field of the action COMPILED only.
            label = (f"{action.name}:{DispatchStatus(dr.status).name}"
                     if action == DispatchAction.COMPILED else action.name)
            extra = f" scalars={values}" if num_scalars > 0 else ""
            print(f"  [{label}] {op_name} "
                  f"in={num_inputs} out={num_outputs}{extra}")

        return result

    # ── Helpers ──────────────────────────────────────────────────────

    def _get_schema_hash(self, op_name):
        h = self._schema_cache.get(op_name)
        if h is None:
            h = self._vessel._lib.crucible_hash_string(op_name.encode("ascii"))
            self._schema_cache[op_name] = h
            # Register name in the global SchemaTable for visualization/diagnostics.
            # A collision means that two op names give one schema hash.  The
            # table keeps the first name, and two ops share one schema hash.
            # A full table keeps each name, and this op has no name in it.
            result = self._vessel.register_schema_name(self._handle, h, op_name.encode("ascii"))
            if result == SchemaNameResult.COLLISION:
                raise RuntimeError(
                    f"crucible: the schema hash {h} of the op {op_name} names a different op in the schema table")
            if result == SchemaNameResult.FULL:
                raise RuntimeError(
                    f"crucible: the schema table has no room for the op {op_name}.  Raise "
                    f"LATE_SCHEMA_TABLE_CAP or SCHEMA_NAME_BYTES_MAX in crucible/SchemaTable.h")
        return h

    @staticmethod
    def _fill_meta(meta, tensor):
        ndim = tensor.dim()
        # The record holds the true rank, and the sizes and the strides of at
        # most MAX_META_LANES dimensions.  The rank saturates at the largest
        # value of its byte, so a rank of 264 cannot wrap to 8.  The C entry
        # refuses each rank above 8 with the action REFUSED, so a tensor that
        # a TensorMeta cannot describe keeps its op out of the ring, as on the
        # native path.
        meta.ndim = min(ndim, MAX_META_RANK_BYTE)
        lanes = min(ndim, MAX_META_LANES)
        if tensor.layout == torch.strided:
            for d in range(lanes):
                meta.sizes[d] = tensor.size(d)
                meta.strides[d] = tensor.stride(d)
        else:
            # Sparse tensors: sizes available, strides not.
            for d in range(lanes):
                meta.sizes[d] = tensor.size(d)
                meta.strides[d] = 0
        for d in range(lanes, MAX_META_LANES):
            meta.sizes[d] = 0
            meta.strides[d] = 0
        meta.data_ptr = tensor.data_ptr() if tensor.layout == torch.strided else 0
        meta.dtype = _dtype_ordinal(tensor.dtype)
        dev = tensor.device
        meta.device_type = _device_ordinal(dev.type)
        meta.device_idx = dev.index if dev.index is not None else -1
        meta.layout = _layout_ordinal(tensor.layout)
        meta.requires_grad = 1 if tensor.requires_grad else 0

        # Packed flags: is_leaf|is_contiguous|has_grad_fn|is_view|is_neg|is_conj
        flags = 0
        if tensor.is_leaf:
            flags |= 0x01
        if tensor.is_contiguous():
            flags |= 0x02
        if tensor.grad_fn is not None:
            flags |= 0x04
        if tensor.storage_offset() != 0 or (
                tensor.data_ptr() != 0 and hasattr(tensor, '_base')
                and tensor._base is not None):
            flags |= 0x08  # is_view
        if tensor.is_neg():
            flags |= 0x10
        if tensor.is_conj():
            flags |= 0x20
        meta.flags = flags

        # Autograd output number
        try:
            meta.output_nr = getattr(tensor, 'output_nr', 0) & 0xFF
        except Exception:
            meta.output_nr = 0

        # Storage offset (view chain tracking)
        try:
            meta.storage_offset = tensor.storage_offset() if tensor.layout == torch.strided else 0
        except Exception:
            meta.storage_offset = 0

        # Data version counter (in-place mutation detection)
        try:
            meta.version = tensor._version & 0xFFFFFFFF
        except Exception:
            meta.version = 0

        # Actual storage size in bytes (may differ from view size)
        try:
            meta.storage_nbytes = tensor.untyped_storage().nbytes()
        except Exception:
            meta.storage_nbytes = 0

        # grad_fn class name hash (forward↔backward pairing)
        gfn = tensor.grad_fn
        if gfn is not None:
            meta.grad_fn_hash = _fnv1a(type(gfn).__name__.encode("ascii"))
        else:
            meta.grad_fn_hash = 0

    def _plan_of(self, func) -> _OpPlan:
        """Build the walk plan of an operator overload, keep it in the table of the plans, and return it.

        The type of each argument in the schema gives its kind (_kind_of) and
        its role.  An argument of the type Scalar or float, an optional of one,
        or a list of one, is data (_is_data_type), unless its position is past
        the positions that the native paths name (MAX_SCHEMA_ARGUMENTS) or the
        list of the vessel makes it structural (crucible_is_structural_scalar).
        A tensor, a list of tensors and an optional of each other kind can
        give an input or a slot.  An argument of each other kind gives
        neither, and the plan leaves it out.

        Complexity: O(arguments of the schema), once for each overload.
        """
        name = func.name()
        schema_hash = self._get_schema_hash(name)
        schema = func._schema
        steps = []
        for position, argument in enumerate(schema.arguments):
            kind, is_optional = _kind_of(argument.real_type)
            if kind == _KIND_OTHER and not is_optional:
                continue
            is_data = (position < MAX_SCHEMA_ARGUMENTS and _is_data_type(argument.real_type)
                       and not self._vessel._lib.crucible_is_structural_scalar(schema_hash, position))
            default = argument.default_value if argument.has_default_value() else None
            steps.append((_KWARG_ONLY if argument.kwarg_only else position, argument.name, default, kind,
                          is_optional, ROLE_DATA if is_data else ROLE_STRUCTURAL))
        plan = _OpPlan(name, schema_hash, len(schema.returns), tuple(steps),
                       OP_IS_MUTABLE if schema.is_mutable else 0, "_foreach_" in name)
        self._plans[func] = plan
        return plan

    def _op_flags(self, plan: _OpPlan, types) -> int:
        """Return the op flags of one op, as the native paths set them (entry_op_flags of record_kernel.h).

        The inference and the grad bits read the modes of this thread.  The
        phase is BACKWARD in a backward window of the dispatch library, which
        the wrapper of Tensor.backward opens.  It is OPTIMIZER for a fused
        multi-tensor op with grad off, and FORWARD for each other op
        (derive_training_phase of record_kernel.h).  TORCH_FUNCTION tells that
        the native paths see the Python dispatch key: an argument is a tensor
        subclass with __torch_dispatch__ (types is not empty), or a Python
        dispatch mode other than this one is on the stack.  PyTorch takes this
        mode off the stack while its handler runs, so the count of the stack
        leaves it out.  The mode stands in for the native recorder, so its own
        dispatch sets no bit.
        """
        grad_enabled = torch.is_grad_enabled()
        flags = plan.schema_flags
        if torch.is_inference_mode_enabled():
            flags |= OP_INFERENCE_MODE
        if grad_enabled:
            flags |= OP_GRAD_ENABLED
        if self._dispatch.backward_depth() != 0:
            phase = PHASE_BACKWARD
        elif plan.is_foreach and not grad_enabled:
            phase = PHASE_OPTIMIZER
        else:
            phase = PHASE_FORWARD
        flags |= phase << OP_PHASE_SHIFT
        if types or torch._C._len_torch_dispatch_stack() > 0:
            flags |= OP_TORCH_FUNCTION
        return flags
