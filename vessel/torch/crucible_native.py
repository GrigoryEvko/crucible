"""Crucible native vessel controller — C++ dispatch via DispatchKey::Crucible.

Activates the C++ boxed fallback (crucible_fallback.cpp) so that EVERY ATen
op — forward, backward, optimizer — is intercepted at ~100ns/op and fed to
the Vigil runtime (TraceRing -> BackgroundThread -> MerkleDag).

Requires:
  - PyTorch fork with DispatchKey::Crucible + CrucibleState TLS
    (built from patches/pytorch-crucible-integration.patch)
  - libcrucible_vessel.so  (C API to Vigil lifecycle)
  - libcrucible_dispatch.so (C++ fallback + TLS accessors)

Usage:
    from crucible_native import CrucibleNative

    model = torchvision.models.resnet18()
    x = torch.randn(4, 3, 224, 224)

    with CrucibleNative(verbose=True) as ctx:
        ctx.track_modules(model)
        for i in range(3):  # 1 warmup + 2 for iteration detection
            optimizer.zero_grad()
            out = model(x)
            loss = criterion(out, labels)
            loss.backward()
            optimizer.step()
        ctx.export_trace("resnet18.crtrace")
"""

import ctypes
from pathlib import Path
from typing import TYPE_CHECKING

import torch

if TYPE_CHECKING:
    import torch.nn as nn


# =====================================================================
# FNV-1a 64-bit — must match C++ fnv1a_bytes() in crucible_fallback.cpp
# =====================================================================

_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME  = 0x100000001B3
_MASK64     = (1 << 64) - 1


def _fnv1a(data: bytes) -> int:
    """FNV-1a 64-bit hash, identical to C++ fnv1a_bytes()."""
    h = _FNV_OFFSET
    for b in data:
        h = ((h ^ b) * _FNV_PRIME) & _MASK64
    return h


# =====================================================================
# Library locator
# =====================================================================

def _find_lib(name: str) -> str | None:
    """Search for a Crucible .so in common build directories."""
    base = Path(__file__).resolve().parent.parent.parent
    for d in ("build", "build-default", "build-gcc", "build-release"):
        p = base / d / "lib" / name
        if p.exists():
            return str(p)
    return None


# =====================================================================
# Vessel C API wrapper (Vigil lifecycle + trace export)
# =====================================================================

class _VesselLib:
    """Ctypes wrapper for libcrucible_vessel.so — Vigil lifecycle + queries."""

    def __init__(self, lib_path: str | None = None):
        path = lib_path or _find_lib("libcrucible_vessel.so")
        if path is None:
            raise RuntimeError(
                "Cannot find libcrucible_vessel.so — build Crucible first:\n"
                "  cmake --preset default && cmake --build --preset default")
        self._lib = ctypes.CDLL(path)
        self._setup()

    def _setup(self):
        L = self._lib

        # Lifecycle
        L.crucible_create.restype = ctypes.c_void_p
        L.crucible_create.argtypes = []
        L.crucible_destroy.restype = None
        L.crucible_destroy.argtypes = [ctypes.c_void_p]
        L.crucible_flush.restype = None
        L.crucible_flush.argtypes = [ctypes.c_void_p]

        # Queries
        L.crucible_is_compiled.restype = ctypes.c_int
        L.crucible_is_compiled.argtypes = [ctypes.c_void_p]
        L.crucible_compiled_iterations.restype = ctypes.c_uint32
        L.crucible_compiled_iterations.argtypes = [ctypes.c_void_p]
        L.crucible_diverged_count.restype = ctypes.c_uint32
        L.crucible_diverged_count.argtypes = [ctypes.c_void_p]
        L.crucible_bg_iterations.restype = ctypes.c_uint32
        L.crucible_bg_iterations.argtypes = [ctypes.c_void_p]
        L.crucible_ring_size.restype = ctypes.c_uint32
        L.crucible_ring_size.argtypes = [ctypes.c_void_p]
        L.crucible_metalog_size.restype = ctypes.c_uint32
        L.crucible_metalog_size.argtypes = [ctypes.c_void_p]

        # Trace export
        L.crucible_export_crtrace.restype = ctypes.c_int
        L.crucible_export_crtrace.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.crucible_active_num_ops.restype = ctypes.c_uint32
        L.crucible_active_num_ops.argtypes = [ctypes.c_void_p]

        # Schema name registration (bridge from dispatch lib)
        L.crucible_register_schema_name.restype = None
        L.crucible_register_schema_name.argtypes = [ctypes.c_uint64, ctypes.c_char_p]

    def create(self) -> int:
        return self._lib.crucible_create()

    def destroy(self, h: int):
        self._lib.crucible_destroy(h)

    def flush(self, h: int):
        self._lib.crucible_flush(h)

    def is_compiled(self, h: int) -> bool:
        return bool(self._lib.crucible_is_compiled(h))

    def compiled_iterations(self, h: int) -> int:
        return self._lib.crucible_compiled_iterations(h)

    def diverged_count(self, h: int) -> int:
        return self._lib.crucible_diverged_count(h)

    def bg_iterations(self, h: int) -> int:
        return self._lib.crucible_bg_iterations(h)

    def ring_size(self, h: int) -> int:
        return self._lib.crucible_ring_size(h)

    def metalog_size(self, h: int) -> int:
        return self._lib.crucible_metalog_size(h)

    def export_crtrace(self, h: int, path: str) -> bool:
        return bool(self._lib.crucible_export_crtrace(h, path.encode()))

    def active_num_ops(self, h: int) -> int:
        return self._lib.crucible_active_num_ops(h)

    def register_schema_name(self, schema_hash: int, name: str):
        self._lib.crucible_register_schema_name(
            ctypes.c_uint64(schema_hash), name.encode("utf-8"))


# =====================================================================
# Dispatch library wrapper (TLS accessors from crucible_fallback.cpp)
# =====================================================================

class _DispatchLib:
    """Ctypes wrapper for libcrucible_dispatch.so — TLS state + scope."""

    def __init__(self, lib_path: str | None = None):
        path = lib_path or _find_lib("libcrucible_dispatch.so")
        if path is None:
            raise RuntimeError(
                "Cannot find libcrucible_dispatch.so — build with:\n"
                "  cmake -DTORCH_DIR=~/Downloads/pytorch --preset default\n"
                "  cmake --build --preset default")
        # Ensure torch._C is loaded first (provides libc10 symbols).
        import torch._C  # noqa: F401
        # CDLL triggers the .so's global constructors, which include
        # TORCH_LIBRARY_IMPL registration — the C++ fallback is active
        # as soon as the library is loaded.
        self._lib = ctypes.CDLL(path)
        self._setup()

    def _setup(self):
        L = self._lib

        # TLS mode/context
        L.crucible_dispatch_set_tls_mode.restype = None
        L.crucible_dispatch_set_tls_mode.argtypes = [ctypes.c_uint8]
        L.crucible_dispatch_get_tls_mode.restype = ctypes.c_uint8
        L.crucible_dispatch_get_tls_mode.argtypes = []
        L.crucible_dispatch_set_tls_context.restype = None
        L.crucible_dispatch_set_tls_context.argtypes = [ctypes.c_void_p]
        L.crucible_dispatch_get_tls_context.restype = ctypes.c_void_p
        L.crucible_dispatch_get_tls_context.argtypes = []

        # TLS scope hash
        L.crucible_dispatch_set_tls_scope.restype = None
        L.crucible_dispatch_set_tls_scope.argtypes = [ctypes.c_uint64]
        L.crucible_dispatch_get_tls_scope.restype = ctypes.c_uint64
        L.crucible_dispatch_get_tls_scope.argtypes = []

        # TLS training phase (op_flags bits 2-3)
        L.crucible_dispatch_set_training_phase.restype = None
        L.crucible_dispatch_set_training_phase.argtypes = [ctypes.c_uint8]
        L.crucible_dispatch_get_training_phase.restype = ctypes.c_uint8
        L.crucible_dispatch_get_training_phase.argtypes = []

        # Schema table accessors (for bridging to vessel lib before export)
        L.crucible_dispatch_schema_count.restype = ctypes.c_uint32
        L.crucible_dispatch_schema_count.argtypes = []
        L.crucible_dispatch_schema_entry.restype = ctypes.c_int
        L.crucible_dispatch_schema_entry.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char_p),
        ]

    def set_training_phase(self, phase: int):
        self._lib.crucible_dispatch_set_training_phase(ctypes.c_uint8(phase))

    def set_mode(self, mode: int):
        self._lib.crucible_dispatch_set_tls_mode(ctypes.c_uint8(mode))

    def set_context(self, ctx: int):
        self._lib.crucible_dispatch_set_tls_context(ctypes.c_void_p(ctx))

    def set_scope(self, scope_hash: int):
        self._lib.crucible_dispatch_set_tls_scope(ctypes.c_uint64(scope_hash))

    def get_mode(self) -> int:
        return self._lib.crucible_dispatch_get_tls_mode()

    def get_scope(self) -> int:
        return self._lib.crucible_dispatch_get_tls_scope()

    def schema_count(self) -> int:
        return self._lib.crucible_dispatch_schema_count()

    def schema_entries(self) -> list[tuple[int, str]]:
        """Return all (schema_hash, name) pairs from dispatch lib's table."""
        n = self.schema_count()
        result = []
        for i in range(n):
            h = ctypes.c_uint64(0)
            name = ctypes.c_char_p(None)
            ok = self._lib.crucible_dispatch_schema_entry(
                ctypes.c_uint32(i),
                ctypes.byref(h),
                ctypes.byref(name),
            )
            if ok and name.value is not None:
                result.append((h.value, name.value.decode("utf-8")))
        return result


# =====================================================================
# DispatchKey::Crucible accessor
# =====================================================================

def _get_crucible_dispatch_key():
    """Get DispatchKey::Crucible from the PyTorch fork, or None."""
    import torch._C as _C
    try:
        return _C.DispatchKey.Crucible
    except AttributeError:
        pass
    # String-based lookup (key exists in C++ but Python enum not updated)
    for fn_name in ("_parse_dispatch_key", "_dispatch_key_parse"):
        fn = getattr(_C, fn_name, None)
        if fn is not None:
            try:
                key = fn("Crucible")
                if key is not None:
                    return key
            except Exception:
                pass
    return None


# =====================================================================
# CrucibleNative — the vessel controller
# =====================================================================

class CrucibleNative:
    """Context manager for native C++ Crucible dispatch.

    On __enter__:
      1. Load libcrucible_dispatch.so (registers TORCH_LIBRARY_IMPL fallback)
      2. Create Vigil via vessel C API
      3. Set CrucibleState TLS: mode=RECORD, context=vigil*
      4. Enable DispatchKey::Crucible in this thread's TLS

    While active:
      - Every ATen op goes through crucibleFallback() in C++
      - track_modules() installs scope hooks for module hierarchy
      - Vigil records to TraceRing, bg thread builds DAG

    On __exit__:
      5. Disable DispatchKey::Crucible
      6. Clear TLS state
      7. Remove scope hooks
      8. Destroy Vigil
    """

    # CrucibleMode ordinals (must match c10::CrucibleMode enum)
    INACTIVE = 0
    RECORD   = 1
    COMPILED = 2
    DIVERGED = 3

    # TrainingPhase ordinals (packed into op_flags bits 2-3)
    PHASE_FORWARD   = 0
    PHASE_BACKWARD  = 1
    PHASE_OPTIMIZER = 2
    PHASE_OTHER     = 3

    def __init__(self, *, vessel_lib_path: str | None = None,
                 dispatch_lib_path: str | None = None,
                 verbose: bool = False):
        self._vessel_lib_path = vessel_lib_path
        self._dispatch_lib_path = dispatch_lib_path
        self._verbose = verbose

        self._vessel: _VesselLib | None = None
        self._dispatch: _DispatchLib | None = None
        self._handle: int = 0
        self._dispatch_key = None

        # Scope tracking state
        self._hook_handles: list = []
        self._scope_names: dict[int, str] = {}  # hash -> module path

    def __enter__(self):
        self._dispatch_key = _get_crucible_dispatch_key()
        if self._dispatch_key is None:
            raise RuntimeError(
                "PyTorch fork with DispatchKey::Crucible required.\n"
                "Build from ~/Downloads/pytorch with the crucible patch applied.")

        # Load vessel lib (Vigil lifecycle)
        self._vessel = _VesselLib(self._vessel_lib_path)
        self._handle = self._vessel.create()

        # Load dispatch lib (C++ fallback + TLS accessors)
        self._dispatch = _DispatchLib(self._dispatch_lib_path)

        # Set CrucibleState TLS: mode=RECORD, context=vigil handle
        self._dispatch.set_mode(self.RECORD)
        self._dispatch.set_context(self._handle)

        # Enable DispatchKey::Crucible in this thread
        import torch._C as _C
        _C._dispatch_tls_set_dispatch_key_included(
            self._dispatch_key, True)

        if self._verbose:
            print(f"[crucible] native dispatch active, vigil={self._handle:#x}")

        return self

    def __exit__(self, *exc):
        # Disable dispatch key (stops fallback from firing)
        if self._dispatch_key is not None:
            try:
                import torch._C as _C
                _C._dispatch_tls_set_dispatch_key_included(
                    self._dispatch_key, False)
            except Exception:
                pass

        # Clear TLS state
        if self._dispatch is not None:
            self._dispatch.set_mode(self.INACTIVE)
            self._dispatch.set_context(0)
            self._dispatch.set_scope(0)

        # Remove scope hooks
        self._remove_hooks()

        # Destroy Vigil
        if self._vessel and self._handle:
            self._vessel.destroy(self._handle)
            self._handle = 0

        if self._verbose:
            print("[crucible] native dispatch deactivated")

    # ── Module scope tracking ────────────────────────────────────────

    def track_modules(self, model: "nn.Module", backward_hooks: bool = False):
        """Install scope hooks on all modules for hierarchy tracking.

        Each hook sets the CrucibleState TLS scope_hash to the FNV-1a hash
        of the module's path (e.g. "layer1.0.conv1").  The C++ fallback
        reads this per-op and records it in the TraceRing.

        Forward hooks: always installed. Fire when model(x) enters each module.
        Backward hooks: opt-in (backward_hooks=True). Fire during loss.backward().
            WARNING: backward hooks conflict with PyTorch's inplace ops (relu_).
            When disabled, backward ops inherit the last forward module's scope.
        """
        if not self._dispatch:
            raise RuntimeError("track_modules() requires active CrucibleNative context")

        self._remove_hooks()  # clean any previous hooks

        for name, module in model.named_modules():
            if not name:
                name = "<root>"
            scope_hash = _fnv1a(name.encode("utf-8"))
            self._scope_names[scope_hash] = name

            # Forward pre-hook: sets scope for ops during forward pass
            handle = module.register_forward_pre_hook(
                self._make_forward_hook(scope_hash))
            self._hook_handles.append(handle)

            # Backward pre-hook: optional (conflicts with inplace ops)
            if backward_hooks:
                handle = module.register_full_backward_pre_hook(
                    self._make_backward_hook(scope_hash))
                self._hook_handles.append(handle)

        if self._verbose:
            n_modules = len(self._scope_names)
            kind = "fwd+bwd" if backward_hooks else "fwd-only"
            print(f"[crucible] tracking {n_modules} modules "
                  f"({len(self._hook_handles)} hooks, {kind})")

    def _make_forward_hook(self, scope_hash: int):
        dispatch = self._dispatch
        def hook(module, input):
            dispatch.set_scope(scope_hash)
        return hook

    def _make_backward_hook(self, scope_hash: int):
        dispatch = self._dispatch
        def hook(module, grad_output):
            dispatch.set_scope(scope_hash)
        return hook

    def _remove_hooks(self):
        for h in self._hook_handles:
            h.remove()
        self._hook_handles.clear()

    # ── Trace export ─────────────────────────────────────────────────

    def export_trace(self, path: str) -> bool:
        """Export the active compiled region as .crtrace binary.

        Flushes the ring buffer first, waits for the bg thread to process
        all pending entries, then serializes the RegionNode to disk.

        Requires at least 2 complete iterations for the IterationDetector
        to have built a region.  Returns True on success.
        """
        if not self._vessel or not self._handle:
            raise RuntimeError("export_trace() requires active CrucibleNative context")

        self._vessel.flush(self._handle)

        num_ops = self._vessel.active_num_ops(self._handle)
        if num_ops == 0:
            if self._verbose:
                bg_iters = self._vessel.bg_iterations(self._handle)
                print(f"[crucible] no active region (bg_iterations={bg_iters})")
                print("[crucible] need 2+ complete iterations for detection")
            return False

        # Bridge schema names from dispatch lib to vessel lib.
        # Each .so has its own copy of global_schema_table() (inline static
        # local).  crucible_fallback.cpp registers names into the dispatch
        # lib's copy, but crucible_export_crtrace reads from the vessel
        # lib's copy.  Copy all entries before export.
        self._bridge_schema_names()

        ok = self._vessel.export_crtrace(self._handle, path)
        if self._verbose:
            if ok:
                print(f"[crucible] exported {num_ops} ops to {path}")
            else:
                print(f"[crucible] export failed: {path}")
        return ok

    def _bridge_schema_names(self):
        """Copy schema names from dispatch lib's table to vessel lib's table.

        Both libraries have independent copies of global_schema_table()
        because it uses an inline function with a static local variable.
        The dispatch lib populates its copy during op recording; the vessel
        lib reads its copy during .crtrace export.  This method bridges them.
        """
        if not self._dispatch or not self._vessel:
            return
        entries = self._dispatch.schema_entries()
        for schema_hash, name in entries:
            self._vessel.register_schema_name(schema_hash, name)
        if self._verbose and entries:
            print(f"[crucible] bridged {len(entries)} schema names "
                  f"from dispatch lib to vessel lib")

    # ── Queries ──────────────────────────────────────────────────────

    def flush(self):
        """Wait until bg thread has fully processed all recorded ops."""
        if self._vessel and self._handle:
            self._vessel.flush(self._handle)

    def is_compiled(self) -> bool:
        return bool(self._vessel and self._vessel.is_compiled(self._handle))

    def compiled_iterations(self) -> int:
        return self._vessel.compiled_iterations(self._handle) if self._vessel else 0

    def diverged_count(self) -> int:
        return self._vessel.diverged_count(self._handle) if self._vessel else 0

    def bg_iterations(self) -> int:
        return self._vessel.bg_iterations(self._handle) if self._vessel else 0

    def ring_size(self) -> int:
        return self._vessel.ring_size(self._handle) if self._vessel else 0

    def metalog_size(self) -> int:
        return self._vessel.metalog_size(self._handle) if self._vessel else 0

    def active_num_ops(self) -> int:
        return self._vessel.active_num_ops(self._handle) if self._vessel else 0

    def set_training_phase(self, phase: int):
        """Set the training phase for subsequent ops (op_flags bits 2-3).

        Call this between training phases so the trace records which ops
        belong to forward, backward, or optimizer passes:

            ctx.set_training_phase(ctx.PHASE_FORWARD)
            out = model(x)
            ctx.set_training_phase(ctx.PHASE_BACKWARD)
            loss.backward()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.step()
        """
        if self._dispatch:
            self._dispatch.set_training_phase(phase)

    @property
    def scope_names(self) -> dict[int, str]:
        """Map of scope_hash -> module path for all tracked modules."""
        return self._scope_names
