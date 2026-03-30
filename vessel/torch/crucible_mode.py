"""Crucible TorchDispatchMode — MVP PROOF-OF-CONCEPT. COMPLETE BULLSHIT.

WARNING: This is a throwaway prototype. It uses TorchDispatchMode (Python-level
dispatch) instead of the real C++ DispatchKey::Crucible. Known problems:

  - TorchDispatchMode sees backward ATen ops BUT the scope_hash is stale
    (forward pre-hooks don't fire during autograd). Backward ops inherit
    whatever scope was active when the last forward pre-hook fired.
  - No autograd hooks at all — we don't track which backward op corresponds
    to which forward op. The grad_fn_hash field exists but is never correlated.
  - In-place ops and views share data_ptr, producing wrong edges downstream.
  - Python dispatch overhead is ~10μs/op. Real Crucible is ~5ns/op via C++.
  - No compiled mode, no shadow handles, no replay — just recording.
  - Scalar extraction is best-effort and misses many kwargs.
  - The .crtrace export format has the scope_hash in the callsite slot (hack).

The REAL implementation uses DispatchKey::Crucible in c10 (see patches/).
This file exists ONLY for recording traces to feed the visualization pipeline.

Usage:
    from crucible_mode import CrucibleMode

    with CrucibleMode(record_trace=True) as mode:
        mode.track_modules(model)
        train_step(model, x)
        mode.export_trace("trace.crtrace")
"""

import ctypes
import struct
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
    return "libcrucible_vessel.so"


# ─── ctypes bindings ────────────────────────────────────────────────

class _VesselLib:
    """Thin ctypes wrapper around libcrucible_vessel.so."""

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
            ("requires_grad", ctypes.c_uint8),
            ("flags", ctypes.c_uint8),
            ("output_nr", ctypes.c_uint8),
            ("storage_offset", ctypes.c_int64),
            ("version", ctypes.c_uint32),
            ("storage_nbytes", ctypes.c_uint32),
            ("grad_fn_hash", ctypes.c_uint64),
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

        lib.crucible_register_schema_name.restype = None
        lib.crucible_register_schema_name.argtypes = [
            ctypes.c_uint64, ctypes.c_char_p,
        ]

        lib.crucible_schema_name.restype = ctypes.c_char_p
        lib.crucible_schema_name.argtypes = [ctypes.c_uint64]

        lib.crucible_hash_shapes.restype = ctypes.c_uint64
        lib.crucible_hash_shapes.argtypes = [
            ctypes.POINTER(ctypes.c_int64),
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint32,
        ]

        lib.crucible_dispatch_op_ex.restype = self.DispatchResult
        lib.crucible_dispatch_op_ex.argtypes = [
            ctypes.c_void_p,                  # handle
            ctypes.c_uint64,                  # schema_hash
            ctypes.c_uint64,                  # shape_hash
            ctypes.c_uint16,                  # num_inputs
            ctypes.c_uint16,                  # num_outputs
            ctypes.POINTER(self.Meta),        # metas
            ctypes.c_uint32,                  # n_metas
            ctypes.POINTER(ctypes.c_int64),   # scalar_values
            ctypes.c_uint16,                  # num_scalars
            ctypes.c_uint8,                   # grad_enabled
            ctypes.c_uint8,                   # inference_mode
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
# Add FP8/extended types if available in this PyTorch build.
for _name, _ord in [
    ("float8_e5m2", 23), ("float8_e4m3fn", 24),
    ("float8_e5m2fnuz", 25), ("float8_e4m3fnuz", 26),
    ("uint16", 27), ("uint32", 28), ("uint64", 29),
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
    "privateuseone": 18,
}

# c10::Layout ordinals.
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


# ─── Scalar encoding ───────────────────────────────────────────────

def _fnv1a(s: bytes) -> int:
    """FNV-1a 64-bit hash, matching vessel_api.cpp crucible_hash_string."""
    h = 0xCBF29CE484222325
    for b in s:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


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

    def __init__(self, lib_path=None, verbose=False, record_trace=False):
        super().__init__()
        self._vessel = _VesselLib(lib_path)
        self._handle = self._vessel.crucible_create()
        self._verbose = verbose
        self._op_count = 0
        self._schema_cache = {}
        self._prev_compiled = False
        # Trace recording: capture per-op data for binary export.
        self._record_trace = record_trace
        self._trace_ops = []
        self._trace_metas = bytearray()
        # Module scope tracking: current module path (set by forward pre-hooks).
        self._current_scope = ""
        self._scope_cache = {}  # scope_string → scope_hash

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

    def track_modules(self, model):
        """Install forward pre-hooks on all submodules for scope tracking.
        Call before entering CrucibleMode context or before training loop.
        Each op will carry the scope_hash of the innermost active module."""
        self._scope_hooks = []
        for name, module in model.named_modules():
            if not name:
                continue  # skip root
            h = self._scope_hooks  # prevent closure over self
            scope = name

            def _make_pre(s):
                def _pre(mod, args):
                    self._current_scope = s
                return _pre

            handle = module.register_forward_pre_hook(_make_pre(scope))
            self._scope_hooks.append(handle)

    # ── Trace export ─────────────────────────────────────────────────

    def clear_trace(self):
        """Clear the recorded trace buffer (start fresh for next iteration)."""
        self._trace_ops.clear()
        self._trace_metas = bytearray()

    def export_trace(self, path):
        """Write recorded ops to a .crtrace binary file.

        File format (all little-endian):
          Header (16B):
            char[4]  magic    = "CRTR"
            uint32   version  = 1
            uint32   num_ops
            uint32   num_metas (total across all ops)

          Op records (num_ops × 80B each):
            uint64   schema_hash
            uint64   shape_hash
            uint64   scope_hash     (= 0, Vessel doesn't set it yet)
            uint64   callsite_hash  (= 0, Vessel doesn't set it yet)
            int64[5] scalar_values
            uint16   num_inputs
            uint16   num_outputs
            uint16   num_scalars
            uint8    grad_enabled
            uint8    inference_mode

          Meta records (num_metas × 144B each):
            Raw TensorMeta structs, same layout as CrucibleMeta.
        """
        meta_size = ctypes.sizeof(_VesselLib.Meta)
        num_ops = len(self._trace_ops)
        num_metas = len(self._trace_metas) // meta_size
        assert len(self._trace_metas) % meta_size == 0, \
            f"meta buffer size {len(self._trace_metas)} not multiple of {meta_size}"

        with open(path, "wb") as f:
            # Header
            f.write(b"CRTR")
            f.write(struct.pack("<III", 1, num_ops, num_metas))

            # Op records (80B each)
            for entry in self._trace_ops:
                (sh, hsh, sv, n_in, n_out, n_sc, grad, infer,
                 mutable, scope_h, tags) = entry
                f.write(struct.pack("<QQ", sh, hsh))
                f.write(struct.pack("<QQ", scope_h, tags))  # scope_hash, op_tags (in callsite slot)
                f.write(struct.pack("<5q", *sv))
                f.write(struct.pack("<HHH", n_in, n_out, n_sc))
                # Pack is_mutable into bit 1 of inference_mode byte
                infer_packed = (infer & 1) | ((mutable & 1) << 1)
                f.write(struct.pack("<BB", grad, infer_packed))

            # Meta records (raw bytes, already in CrucibleMeta layout)
            f.write(self._trace_metas)

            # Schema name table: (hash, name) pairs for visualization.
            all_names = list(self._schema_cache.items())
            # Also include scope names
            for scope_str, scope_h in self._scope_cache.items():
                if scope_str and scope_h:
                    all_names.append((scope_str, scope_h))
            names = [(h, name) for name, h in all_names]
            f.write(struct.pack("<I", len(names)))
            for h, name in names:
                name_bytes = name.encode("ascii")
                f.write(struct.pack("<QH", h, len(name_bytes)))
                f.write(name_bytes)

        total = 16 + num_ops * 80 + num_metas * meta_size
        print(f"  [crucible] Exported {num_ops} ops, {num_metas} metas "
              f"({meta_size}B each), {len(names)} names "
              f"({total // 1024}KB) → {path}")

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

        # Shape hash from input shapes
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

        # Extract scalar arguments from args and kwargs
        scalars = self._extract_scalars(args, kwargs)
        num_scalars = min(len(scalars), 5)
        ScalarArray = ctypes.c_int64 * 5
        scalar_buf = ScalarArray()
        for i in range(num_scalars):
            scalar_buf[i] = scalars[i]
        scalars_ptr = ctypes.cast(scalar_buf, ctypes.POINTER(ctypes.c_int64))

        # Grad/inference mode flags
        grad_on = 1 if torch.is_grad_enabled() else 0
        infer_on = 1 if torch.is_inference_mode_enabled() else 0

        # Per-op metadata from schema
        is_mutable = 1 if func._schema.is_mutable else 0

        # Scope hash from module tracking hooks
        scope = self._current_scope
        scope_hash = self._scope_cache.get(scope)
        if scope_hash is None:
            scope_hash = _fnv1a(scope.encode("ascii")) if scope else 0
            self._scope_cache[scope] = scope_hash

        # Op tags from PyTorch dispatch
        op_tags = 0
        try:
            for tag in func.tags:
                name = tag.name
                if name == "core":       op_tags |= 0x01
                elif name == "pointwise":  op_tags |= 0x02
                elif name == "reduction":  op_tags |= 0x04
                elif name == "view_copy":  op_tags |= 0x08
                elif name == "dynamic_output_shape": op_tags |= 0x10
                elif name == "data_dependent_output": op_tags |= 0x20
                elif name == "nondeterministic_seeded": op_tags |= 0x40
        except Exception:
            pass

        # Record trace data before dispatch (captures real data_ptrs).
        if self._record_trace:
            sv = [scalar_buf[i] for i in range(5)]
            self._trace_ops.append((
                schema_hash, shape_hash, sv,
                num_inputs, num_outputs, num_scalars,
                grad_on, infer_on, is_mutable,
                scope_hash, op_tags,
            ))
            if n_metas > 0:
                self._trace_metas.extend(bytes(metas))

        # Dispatch to Vigil
        dr = self._vessel.crucible_dispatch_op_ex(
            self._handle,
            schema_hash, shape_hash,
            num_inputs, num_outputs,
            metas_ptr, n_metas,
            scalars_ptr, num_scalars,
            grad_on, infer_on,
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
            extra = f" scalars={scalars[:num_scalars]}" if num_scalars > 0 else ""
            print(f"  [{action_str}:{status_str}] {op_name} "
                  f"in={num_inputs} out={num_outputs}{extra}")

        return result

    # ── Helpers ──────────────────────────────────────────────────────

    def _get_schema_hash(self, op_name):
        h = self._schema_cache.get(op_name)
        if h is None:
            h = self._vessel.crucible_hash_string(op_name.encode("ascii"))
            self._schema_cache[op_name] = h
            # Register name in the global SchemaTable for visualization/diagnostics.
            self._vessel.crucible_register_schema_name(
                h, op_name.encode("ascii"))
        return h

    def _compute_shape_hash(self, tensors):
        if not tensors:
            return 0
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
        ndim = tensor.dim()
        # TensorMeta.sizes/strides are fixed [8]. Clamp to avoid OOB.
        clamped = min(ndim, 8)
        meta.ndim = clamped
        if tensor.layout == torch.strided:
            for d in range(clamped):
                meta.sizes[d] = tensor.size(d)
                meta.strides[d] = tensor.stride(d)
        else:
            # Sparse tensors: sizes available, strides not.
            for d in range(clamped):
                meta.sizes[d] = tensor.size(d)
                meta.strides[d] = 0
        for d in range(clamped, 8):
            meta.sizes[d] = 0
            meta.strides[d] = 0
        meta.data_ptr = tensor.data_ptr() if tensor.layout == torch.strided else 0
        meta.dtype = _DTYPE_MAP.get(tensor.dtype, -1)
        dev = tensor.device
        meta.device_type = _DEVICE_MAP.get(dev.type, 0)
        meta.device_idx = dev.index if dev.index is not None else -1
        meta.layout = _LAYOUT_MAP.get(tensor.layout, 0)
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
            meta.storage_nbytes = tensor.untyped_storage().nbytes() & 0xFFFFFFFF
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

    @staticmethod
    def _extract_scalars(args, kwargs):
        """Extract scalar arguments as int64 bitcast values."""
        scalars = []
        for a in args:
            if isinstance(a, (int, float, bool)) and not isinstance(a, torch.Tensor):
                scalars.append(_scalar_to_int64(a))
        for v in kwargs.values():
            if isinstance(v, (int, float, bool)) and not isinstance(v, torch.Tensor):
                scalars.append(_scalar_to_int64(v))
        return scalars
