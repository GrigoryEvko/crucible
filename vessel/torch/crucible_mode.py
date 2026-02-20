"""Crucible TorchDispatchMode — intercepts ATen ops and feeds them to Vigil.

Usage:
    from crucible_mode import CrucibleMode

    model = torch.nn.Linear(4, 8)
    x = torch.randn(2, 4)

    with CrucibleMode() as mode:
        for i in range(10):
            y = model(x)
            print(f"iter {i}: compiled={mode.is_compiled()}")
"""

import ctypes
import os
import sys
from pathlib import Path

import torch
from torch.utils._python_dispatch import TorchDispatchMode

# ─── Locate the shared library ──────────────────────────────────────

def _find_vessel_lib():
    """Search for libcrucible_vessel.so in common build directories."""
    base = Path(__file__).resolve().parent.parent.parent
    candidates = [
        base / "build" / "lib" / "libcrucible_vessel.so",
        base / "build-gcc" / "lib" / "libcrucible_vessel.so",
        base / "build" / "vessel" / "torch" / "libcrucible_vessel.so",
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    # Fall back to LD_LIBRARY_PATH
    return "libcrucible_vessel.so"


# ─── ctypes bindings ────────────────────────────────────────────────

class _VesselLib:
    """Thin ctypes wrapper around libcrucible_vessel.so."""

    # Binary-compatible with crucible::TensorMeta (144 bytes)
    class Meta(ctypes.Structure):
        _fields_ = [
            ("sizes", ctypes.c_int64 * 8),
            ("strides", ctypes.c_int64 * 8),
            ("data_ptr", ctypes.c_void_p),
            ("ndim", ctypes.c_uint8),
            ("dtype", ctypes.c_int8),
            ("device_type", ctypes.c_int8),
            ("device_idx", ctypes.c_int8),
            ("layout", ctypes.c_int8),
            ("pad", ctypes.c_uint8 * 3),
        ]

    class DispatchResult(ctypes.Structure):
        _fields_ = [
            ("action", ctypes.c_uint8),
            ("status", ctypes.c_uint8),
            ("pad", ctypes.c_uint8 * 2),
            ("op_index", ctypes.c_uint32),
        ]

    def __init__(self, lib_path=None):
        path = lib_path or _find_vessel_lib()
        self._lib = ctypes.CDLL(path)
        self._setup_signatures()

    def _setup_signatures(self):
        lib = self._lib

        lib.crucible_create.restype = ctypes.c_void_p
        lib.crucible_create.argtypes = []

        lib.crucible_destroy.restype = None
        lib.crucible_destroy.argtypes = [ctypes.c_void_p]

        lib.crucible_hash_string.restype = ctypes.c_uint64
        lib.crucible_hash_string.argtypes = [ctypes.c_char_p]

        lib.crucible_hash_shapes.restype = ctypes.c_uint64
        lib.crucible_hash_shapes.argtypes = [
            ctypes.POINTER(ctypes.c_int64),  # all_sizes
            ctypes.POINTER(ctypes.c_uint8),  # ndims
            ctypes.c_uint32,                 # n_tensors
        ]

        lib.crucible_dispatch_op.restype = self.DispatchResult
        lib.crucible_dispatch_op.argtypes = [
            ctypes.c_void_p,                  # handle
            ctypes.c_uint64,                  # schema_hash
            ctypes.c_uint64,                  # shape_hash
            ctypes.c_uint16,                  # num_inputs
            ctypes.c_uint16,                  # num_outputs
            ctypes.POINTER(self.Meta),        # metas
            ctypes.c_uint32,                  # n_metas
        ]

        lib.crucible_flush.restype = None
        lib.crucible_flush.argtypes = [ctypes.c_void_p]

        lib.crucible_is_compiled.restype = ctypes.c_int
        lib.crucible_is_compiled.argtypes = [ctypes.c_void_p]

        lib.crucible_compiled_iterations.restype = ctypes.c_uint32
        lib.crucible_compiled_iterations.argtypes = [ctypes.c_void_p]

        lib.crucible_diverged_count.restype = ctypes.c_uint32
        lib.crucible_diverged_count.argtypes = [ctypes.c_void_p]

    def __getattr__(self, name):
        return getattr(self._lib, name)


# ─── Dtype mapping ──────────────────────────────────────────────────
#
# crucible::ScalarType mirrors c10::ScalarType ordinals.
# Map PyTorch dtypes to the int8_t values.

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

_DEVICE_MAP = {
    "cpu": 0,
    "cuda": 1,
    "xla": 9,
    "hip": 20,
}


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
        self._handle = self._vessel.crucible_create()
        self._verbose = verbose
        self._op_count = 0
        self._schema_cache = {}  # op name → schema_hash
        self._prev_compiled = False

    def __del__(self):
        if hasattr(self, "_handle") and self._handle:
            self._vessel.crucible_destroy(self._handle)
            self._handle = None

    # ── Public API ──────────────────────────────────────────────────

    def is_compiled(self):
        return bool(self._vessel.crucible_is_compiled(self._handle))

    def compiled_iterations(self):
        return self._vessel.crucible_compiled_iterations(self._handle)

    def diverged_count(self):
        return self._vessel.crucible_diverged_count(self._handle)

    def flush(self):
        self._vessel.crucible_flush(self._handle)

    # ── Dispatch interception ───────────────────────────────────────

    def __torch_dispatch__(self, func, types, args=(), kwargs=None):
        kwargs = kwargs or {}

        # Execute the actual op first (always eager in this POC).
        result = func(*args, **kwargs)

        # Extract op identity
        op_name = func.name()
        schema_hash = self._get_schema_hash(op_name)

        # Collect tensor inputs and outputs
        input_tensors = self._extract_tensors(args, kwargs)
        output_tensors = self._extract_tensors_from_result(result)

        num_inputs = len(input_tensors)
        num_outputs = len(output_tensors)

        # Compute shape_hash from input shapes
        shape_hash = self._compute_shape_hash(input_tensors)

        # Build TensorMeta array: [inputs..., outputs...]
        all_tensors = input_tensors + output_tensors
        n_metas = len(all_tensors)

        if n_metas > 0:
            MetaArray = _VesselLib.Meta * n_metas
            metas = MetaArray()
            for i, t in enumerate(all_tensors):
                self._fill_meta(metas[i], t)
            metas_ptr = ctypes.cast(metas, ctypes.POINTER(_VesselLib.Meta))
        else:
            metas_ptr = None

        # Dispatch to Vigil
        dr = self._vessel.crucible_dispatch_op(
            self._handle,
            schema_hash, shape_hash,
            num_inputs, num_outputs,
            metas_ptr, n_metas,
        )

        self._op_count += 1

        # Log mode transitions
        compiled_now = self.is_compiled()
        if compiled_now and not self._prev_compiled:
            action_str = "COMPILED" if dr.action == 1 else "RECORD"
            print(f"  [crucible] COMPILED mode activated at op #{self._op_count} "
                  f"({op_name}), action={action_str}")
        self._prev_compiled = compiled_now

        if self._verbose:
            action_str = "COMPILED" if dr.action == 1 else "RECORD"
            status_str = ["MATCH", "DIVERGED", "COMPLETE"][dr.status]
            print(f"  [{action_str}:{status_str}] {op_name} "
                  f"in={num_inputs} out={num_outputs}")

        return result

    # ── Helpers ──────────────────────────────────────────────────────

    def _get_schema_hash(self, op_name):
        """Cache FNV-1a hash of op name string."""
        h = self._schema_cache.get(op_name)
        if h is None:
            h = self._vessel.crucible_hash_string(op_name.encode("ascii"))
            self._schema_cache[op_name] = h
        return h

    def _compute_shape_hash(self, tensors):
        """FNV-1a hash of input tensor shapes."""
        if not tensors:
            return 0

        # Flatten all sizes into a contiguous array
        total_dims = sum(t.dim() for t in tensors)
        if total_dims == 0:
            return 0

        SizesArray = ctypes.c_int64 * total_dims
        NdimsArray = ctypes.c_uint8 * len(tensors)
        sizes = SizesArray()
        ndims = NdimsArray()

        offset = 0
        for i, t in enumerate(tensors):
            ndims[i] = t.dim()
            for d in range(t.dim()):
                sizes[offset] = t.size(d)
                offset += 1

        return self._vessel.crucible_hash_shapes(
            ctypes.cast(sizes, ctypes.POINTER(ctypes.c_int64)),
            ctypes.cast(ndims, ctypes.POINTER(ctypes.c_uint8)),
            len(tensors),
        )

    @staticmethod
    def _fill_meta(meta, tensor):
        """Fill a CrucibleMeta struct from a PyTorch tensor."""
        ndim = tensor.dim()
        meta.ndim = ndim
        for d in range(ndim):
            meta.sizes[d] = tensor.size(d)
            meta.strides[d] = tensor.stride(d)
        # Zero out unused dims
        for d in range(ndim, 8):
            meta.sizes[d] = 0
            meta.strides[d] = 0
        meta.data_ptr = tensor.data_ptr()
        meta.dtype = _DTYPE_MAP.get(tensor.dtype, -1)
        dev = tensor.device
        meta.device_type = _DEVICE_MAP.get(dev.type, 0)
        meta.device_idx = dev.index if dev.index is not None else -1
        meta.layout = 0  # Strided

    @staticmethod
    def _extract_tensors(args, kwargs):
        """Extract all tensor arguments (flat)."""
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
        return tensors

    @staticmethod
    def _extract_tensors_from_result(result):
        """Extract tensors from an op result."""
        if isinstance(result, torch.Tensor):
            return [result]
        if isinstance(result, (list, tuple)):
            return [x for x in result if isinstance(x, torch.Tensor)]
        return []
