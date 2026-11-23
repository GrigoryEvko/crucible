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
  - Scalar extraction is best-effort and misses many kwargs.

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

import torch
from torch.utils._python_dispatch import TorchDispatchMode

from crucible_native import (
    CrucibleDispatchResult,
    CrucibleMeta,
    DispatchAction,
    DispatchStatus,
    SchemaNameResult,
    _DispatchLib,
    load_vessel_library,
)


# ─── ctypes bindings ────────────────────────────────────────────────

class _VesselLib:
    """Thin ctypes wrapper around libcrucible_vessel.so."""

    def __init__(self, lib_path: str | None = None) -> None:
        """Load the vessel library once its layout is the layout of its build.

        CrucibleMeta and CrucibleDispatchResult of crucible_native mirror the
        structs of the C ABI, and the loader compares them and the layout note
        of the library with the layout table of the build before the load
        (crucible_native.load_vessel_library).
        """
        self._lib = load_vessel_library(lib_path)
        self._setup_signatures()

    def _setup_signatures(self):
        lib = self._lib

        lib.crucible_create.restype = ctypes.c_void_p
        lib.crucible_create.argtypes = [ctypes.c_uint32]

        lib.crucible_observe_eager_numerics.restype = ctypes.c_int
        lib.crucible_observe_eager_numerics.argtypes = [ctypes.c_void_p, ctypes.c_uint32]

        lib.crucible_destroy.restype = None
        lib.crucible_destroy.argtypes = [ctypes.c_void_p]

        lib.crucible_release_claim.restype = None
        lib.crucible_release_claim.argtypes = [ctypes.c_void_p]

        lib.crucible_active_num_ops.restype = ctypes.c_uint32
        lib.crucible_active_num_ops.argtypes = [ctypes.c_void_p]

        lib.crucible_hash_string.restype = ctypes.c_uint64
        lib.crucible_hash_string.argtypes = [ctypes.c_char_p]

        lib.crucible_register_schema_name.restype = ctypes.c_int
        lib.crucible_register_schema_name.argtypes = [
            ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p,
        ]

        lib.crucible_schema_name.restype = ctypes.c_char_p
        lib.crucible_schema_name.argtypes = [ctypes.c_uint64]

        # The thunk computes the guard word of the op from the records and the
        # scalars, so the call gives no guard word.
        lib.crucible_dispatch_op_ex.restype = CrucibleDispatchResult
        lib.crucible_dispatch_op_ex.argtypes = [
            ctypes.c_void_p,                  # handle
            ctypes.c_uint64,                  # schema_hash
            ctypes.c_uint16,                  # num_inputs
            ctypes.c_uint16,                  # num_outputs
            ctypes.POINTER(CrucibleMeta),     # metas
            ctypes.c_uint32,                  # n_metas
            ctypes.POINTER(ctypes.c_int64),   # scalar_values
            ctypes.c_uint16,                  # num_scalars
            ctypes.c_uint16,                  # scalar_types, two bits for each value
            ctypes.c_uint8,                   # data_slots, one bit for each value
            ctypes.c_uint8,                   # grad_enabled
            ctypes.c_uint8,                   # inference_mode
        ]

        lib.crucible_is_structural_scalar.restype = ctypes.c_int
        lib.crucible_is_structural_scalar.argtypes = [ctypes.c_uint64, ctypes.c_uint32]

        lib.crucible_flush.restype = ctypes.c_int
        lib.crucible_flush.argtypes = [ctypes.c_void_p]

        lib.crucible_is_compiled.restype = ctypes.c_int
        lib.crucible_is_compiled.argtypes = [ctypes.c_void_p]

        lib.crucible_compiled_iterations.restype = ctypes.c_uint64
        lib.crucible_compiled_iterations.argtypes = [ctypes.c_void_p]

        lib.crucible_diverged_count.restype = ctypes.c_uint64
        lib.crucible_diverged_count.argtypes = [ctypes.c_void_p]

        lib.crucible_export_crtrace.restype = ctypes.c_int
        lib.crucible_export_crtrace.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    def __getattr__(self, name):
        return getattr(self._lib, name)


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


def _scalar_tag(val):
    """Return the tag of a Python scalar, as ScalarType2 of crucible/TraceRing.h numbers it: 0 int, 1 float, 2 bool."""
    if isinstance(val, bool):
        return 2
    if isinstance(val, float):
        return 1
    return 0


def _is_data_type(argument_type) -> bool:
    """True for a schema type whose Python scalar fills a data slot.

    The type Scalar or float, an optional of one, or a list of one, is data
    (ScalarRole of crucible/Types.h), unless crucible_is_structural_scalar
    names the argument.  The type comes from the schema of the operator, as on
    the native recording paths.

    A Python scalar in a position of the type Tensor is a wrapped number.
    PyTorch makes a tensor of rank zero from the number, and it gives this mode
    the number again, such as the float divisor of div.Tensor in the step of
    the AdamW with no fused kernel.  The native vessel records that tensor as
    an input, and its guard reads no value of an input.  So the value of a
    wrapped number is data here too, and a loop whose wrapped number changes
    at each iteration keeps its period.
    """
    kind = argument_type.kind()
    if kind in ("OptionalType", "ListType"):
        return _is_data_type(argument_type.getElementType())
    return kind in ("NumberType", "FloatType", "TensorType")


def _scalar_to_int64(val):
    """Bitcast a Python scalar to int64 for TraceRing::Entry::scalar_values."""
    if isinstance(val, bool):
        return 1 if val else 0
    if isinstance(val, int):
        return val & 0xFFFFFFFFFFFFFFFF  # truncate to uint64 bits
    if isinstance(val, float):
        # Bitcast float64 → int64 via struct pack/unpack
        return struct.unpack("q", struct.pack("d", val))[0]
    return 0


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
        self._handle = self._vessel.crucible_create(self._dispatch.eager_numerics())
        if not self._handle:
            raise ValueError("crucible_create refuses the eager word of the dispatch library")
        self._verbose = verbose
        self._op_count = 0
        self._schema_cache = {}
        # The names of the data arguments of each operator (_data_arguments).
        self._data_argument_cache = {}
        self._prev_compiled = False

    def __exit__(self, exc_type, exc_value, traceback):
        """Leave the mode, and give up the producer claim of this thread.

        The create of the Vigil and each op claim the thread of the mode
        (crucible_create).  The exit gives the claim up (crucible_release_claim),
        so a mode object that lives on after its context, such as in a
        reference cycle, holds no claim, and a session of another thread can
        start.  The next op of the mode claims the Vigil again.
        """
        try:
            return super().__exit__(exc_type, exc_value, traceback)
        finally:
            if self._handle:
                self._vessel.crucible_release_claim(self._handle)

    def __del__(self):
        if hasattr(self, "_handle") and self._handle:
            self._vessel.crucible_destroy(self._handle)
            self._handle = None

    # ── Public API ──────────────────────────────────────────────────

    def is_compiled(self):
        return bool(self._vessel.crucible_is_compiled(self._handle))

    def active_num_ops(self) -> int:
        """Return the count of the ops of the active region, or 0 with no active region."""
        return self._vessel.crucible_active_num_ops(self._handle)

    def compiled_iterations(self):
        return self._vessel.crucible_compiled_iterations(self._handle)

    def diverged_count(self):
        return self._vessel.crucible_diverged_count(self._handle)

    def flush(self):
        """Wait until the background thread processed each recorded op.

        Raises RuntimeError when the background thread stopped first, because
        the ops after that point stay unprocessed.
        """
        if self._vessel.crucible_flush(self._handle) == 0:
            raise RuntimeError("the background thread of the Vigil stopped before the flush completed")

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
        return bool(self._vessel.crucible_export_crtrace(self._handle, str(path).encode()))

    # ── Dispatch interception ───────────────────────────────────────

    def __torch_dispatch__(self, func, types, args=(), kwargs=None):
        kwargs = kwargs or {}

        # Execute eagerly first (always, in this POC).
        result = func(*args, **kwargs)

        # Op identity
        op_name = func.name()
        schema_hash = self._get_schema_hash(op_name)

        # Tensor inputs/outputs
        input_tensors = self._extract_tensors(args, kwargs)
        output_tensors = self._extract_tensors_from_result(result)
        num_inputs = len(input_tensors)
        num_outputs = len(output_tensors)

        # Build TensorMeta array: [inputs..., outputs...]
        all_tensors = input_tensors + output_tensors
        n_metas = len(all_tensors)
        if n_metas > 0:
            MetaArray = CrucibleMeta * n_metas
            metas = MetaArray()
            for i, t in enumerate(all_tensors):
                self._fill_meta(metas[i], t)
            metas_ptr = ctypes.cast(metas, ctypes.POINTER(CrucibleMeta))
        else:
            metas_ptr = None

        # Extract scalar arguments from args and kwargs, each with its tag and
        # its role.
        scalars = self._extract_scalars(func, args, kwargs, self._data_arguments(func, op_name, schema_hash))
        num_scalars = min(len(scalars), 5)
        ScalarArray = ctypes.c_int64 * 5
        scalar_buf = ScalarArray()
        scalar_types = 0
        data_slots = 0
        for i in range(num_scalars):
            scalar_buf[i], tag, is_data = scalars[i]
            scalar_types |= tag << (2 * i)
            data_slots |= int(is_data) << i
        scalars_ptr = ctypes.cast(scalar_buf, ctypes.POINTER(ctypes.c_int64))

        # Grad/inference mode flags
        grad_on = 1 if torch.is_grad_enabled() else 0
        infer_on = 1 if torch.is_inference_mode_enabled() else 0

        # Dispatch to Vigil
        dr = self._vessel.crucible_dispatch_op_ex(
            self._handle,
            schema_hash,
            num_inputs, num_outputs,
            metas_ptr, n_metas,
            scalars_ptr, num_scalars, scalar_types, data_slots,
            grad_on, infer_on,
        )
        # The next op starts an iteration of a region, so the Vigil takes the
        # eager word again, once for each iteration.
        if dr.enters_region:
            self._vessel.crucible_observe_eager_numerics(self._handle, self._dispatch.eager_numerics())

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
            extra = f" scalars={[value for value, _, _ in scalars[:num_scalars]]}" if num_scalars > 0 else ""
            print(f"  [{label}] {op_name} "
                  f"in={num_inputs} out={num_outputs}{extra}")

        return result

    # ── Helpers ──────────────────────────────────────────────────────

    def _get_schema_hash(self, op_name):
        h = self._schema_cache.get(op_name)
        if h is None:
            h = self._vessel.crucible_hash_string(op_name.encode("ascii"))
            self._schema_cache[op_name] = h
            # Register name in the global SchemaTable for visualization/diagnostics.
            # A collision means that two op names give one schema hash.  The
            # table keeps the first name, and two ops share one schema hash.
            # A full table keeps each name, and this op has no name in it.
            result = self._vessel.crucible_register_schema_name(
                self._handle, h, op_name.encode("ascii"))
            if result == SchemaNameResult.COLLISION:
                raise RuntimeError(
                    f"crucible: the schema hash {h} of the op {op_name} names a different op in the schema table")
            if result == SchemaNameResult.FULL:
                raise RuntimeError(
                    f"crucible: the schema table is full, so it has no place for the op {op_name}.  Raise "
                    f"SCHEMA_TABLE_CAP in crucible/SchemaTable.h")
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

    @staticmethod
    def _extract_tensors(args, kwargs):
        tensors = []
        for a in args:
            if isinstance(a, torch.Tensor):
                tensors.append(a)
            elif isinstance(a, (list, tuple)):
                for x in a:
                    if isinstance(x, torch.Tensor):
                        tensors.append(x)
        for v in kwargs.values():
            if isinstance(v, torch.Tensor):
                tensors.append(v)
            elif isinstance(v, (list, tuple)):
                for x in v:
                    if isinstance(x, torch.Tensor):
                        tensors.append(x)
        return tensors

    @staticmethod
    def _extract_tensors_from_result(result):
        if isinstance(result, torch.Tensor):
            return [result]
        if isinstance(result, (list, tuple)):
            return [x for x in result if isinstance(x, torch.Tensor)]
        return []

    def _data_arguments(self, func, op_name, schema_hash):
        """Return the names of the arguments of the operator whose Python scalars are data.

        The type of an argument in the schema gives its role (_is_data_type),
        and the list of the vessel makes some of them structural
        (crucible_is_structural_scalar).  The answer depends on the operator
        only, so the mode keeps it for each name.  O(arguments) at the first
        call for an operator.
        """
        names = self._data_argument_cache.get(op_name)
        if names is None:
            names = frozenset(
                argument.name for position, argument in enumerate(func._schema.arguments)
                if _is_data_type(argument.type)
                and not self._vessel.crucible_is_structural_scalar(schema_hash, position))
            self._data_argument_cache[op_name] = names
        return names

    @staticmethod
    def _extract_scalars(func, args, kwargs, data_arguments):
        """Extract the scalar arguments as triples of an int64 bitcast value, its tag and its role.

        The role is True for a data scalar: an argument whose name is in
        data_arguments.  A positional argument has the name of the argument
        of the schema at its position.
        """
        arguments = func._schema.arguments
        scalars = []
        for position, a in enumerate(args):
            if isinstance(a, (int, float, bool)) and not isinstance(a, torch.Tensor):
                is_data = position < len(arguments) and arguments[position].name in data_arguments
                scalars.append((_scalar_to_int64(a), _scalar_tag(a), is_data))
        for name, v in kwargs.items():
            if isinstance(v, (int, float, bool)) and not isinstance(v, torch.Tensor):
                scalars.append((_scalar_to_int64(v), _scalar_tag(v), name in data_arguments))
        return scalars
