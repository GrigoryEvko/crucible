"""Crucible native dispatch mode — C++ fallback via DispatchKey::Crucible.

Activates the C++ boxed fallback (crucible_fallback.cpp) instead of the
Python TorchDispatchMode.  Requires the PyTorch fork with DispatchKey::Crucible
and libcrucible_dispatch.so built with TORCH_DIR.

Usage:
    from crucible_native import CrucibleNative

    model = torch.nn.Linear(4, 8)
    x = torch.randn(2, 4)

    with CrucibleNative() as ctx:
        for i in range(10):
            y = model(x)
        ctx.flush()
        print(f"compiled={ctx.is_compiled()}, iters={ctx.compiled_iterations()}")

Falls back to CrucibleMode (Python TorchDispatchMode) if the native dispatch
library is not available.
"""

import ctypes
from pathlib import Path

import torch

# ─── Locate libraries ──────────────────────────────────────────────────

def _find_lib(name: str) -> str | None:
    """Search for a Crucible .so in common build directories."""
    base = Path(__file__).resolve().parent.parent.parent
    for d in ("build", "build-gcc"):
        p = base / d / "lib" / name
        if p.exists():
            return str(p)
    return None


# ─── Vessel C API (reuse from crucible_mode.py) ────────────────────────

class _VesselLib:
    """Minimal ctypes wrapper — just lifecycle + query functions."""

    def __init__(self, lib_path: str | None = None):
        path = lib_path or _find_lib("libcrucible_vessel.so")
        if path is None:
            raise RuntimeError(
                "Cannot find libcrucible_vessel.so — build Crucible first")
        self._lib = ctypes.CDLL(path)
        self._setup_signatures()

    def _setup_signatures(self):
        L = self._lib
        L.crucible_create.restype = ctypes.c_void_p
        L.crucible_create.argtypes = []
        L.crucible_destroy.restype = None
        L.crucible_destroy.argtypes = [ctypes.c_void_p]
        L.crucible_flush.restype = None
        L.crucible_flush.argtypes = [ctypes.c_void_p]
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


# ─── CrucibleState TLS access via torch._C ─────────────────────────────
#
# The PyTorch fork exposes CrucibleState through the dispatcher's TLS.
# DispatchKey::Crucible is enabled/disabled via _set_dispatch_key_included.
# The CrucibleState (mode + context pointer) is set via c10 TLS accessors
# exposed through pybind11.
#
# For the initial version, we use a simpler approach: a tiny C extension
# that sets the TLS state directly.  This is built into libcrucible_dispatch.so.

def _get_crucible_dispatch_key():
    """Get DispatchKey::Crucible from the PyTorch fork, or None.

    The key exists in the C++ enum but may not be registered as a named
    Python attribute (shows as DispatchKey.???).  We try the attribute first,
    then fall back to string-based parsing.
    """
    try:
        return torch._C.DispatchKey.Crucible
    except AttributeError:
        pass
    try:
        key = torch._C._parse_dispatch_key("Crucible")
        if key is not None:
            return key
    except Exception:
        pass
    return None


class CrucibleNative:
    """Context manager for native C++ Crucible dispatch.

    Activates DispatchKey::Crucible + sets CrucibleState TLS, so the C++
    boxed fallback intercepts every ATen op and feeds it to Vigil.

    On __enter__:
      1. Load libcrucible_dispatch.so (registers TORCH_LIBRARY_IMPL)
      2. Create Vigil via vessel C API
      3. Set CrucibleState TLS: mode=RECORD, context=vigil
      4. Enable DispatchKey::Crucible in TLS

    On __exit__:
      5. Disable DispatchKey::Crucible
      6. Set CrucibleState mode=INACTIVE, context=nullptr
      7. Destroy Vigil
    """

    # CrucibleMode ordinals (must match c10::CrucibleMode enum)
    INACTIVE = 0
    RECORD   = 1
    COMPILED = 2
    DIVERGED = 3

    def __init__(self, vessel_lib_path: str | None = None,
                 dispatch_lib_path: str | None = None,
                 verbose: bool = False):
        self._vessel_lib_path = vessel_lib_path
        self._dispatch_lib_path = dispatch_lib_path
        self._verbose = verbose
        self._vessel: _VesselLib | None = None
        self._handle: int = 0
        self._dispatch_lib: ctypes.CDLL | None = None
        self._dispatch_key = None  # cached DispatchKey::Crucible

    def __enter__(self):
        self._dispatch_key = _get_crucible_dispatch_key()
        if self._dispatch_key is None:
            raise RuntimeError(
                "PyTorch fork with DispatchKey::Crucible required. "
                "Use CrucibleMode (Python TorchDispatchMode) for stock PyTorch.")

        # Load vessel lib (C API to Vigil)
        self._vessel = _VesselLib(self._vessel_lib_path)
        self._handle = self._vessel.create()

        # Load dispatch lib (registers C++ fallback via TORCH_LIBRARY_IMPL).
        # Also exports crucible_dispatch_set_tls_{mode,context} for TLS access.
        dispatch_path = self._dispatch_lib_path or _find_lib(
            "libcrucible_dispatch.so")
        if not dispatch_path:
            raise RuntimeError(
                "Cannot find libcrucible_dispatch.so — "
                "build with -DTORCH_DIR=~/Downloads/pytorch")

        # Load via torch to register TORCH_LIBRARY_IMPL, then via ctypes
        # for the exported TLS accessors.
        torch.ops.load_library(dispatch_path)
        self._dispatch_lib = ctypes.CDLL(dispatch_path)
        self._dispatch_lib.crucible_dispatch_set_tls_mode.restype = None
        self._dispatch_lib.crucible_dispatch_set_tls_mode.argtypes = [
            ctypes.c_uint8]
        self._dispatch_lib.crucible_dispatch_set_tls_context.restype = None
        self._dispatch_lib.crucible_dispatch_set_tls_context.argtypes = [
            ctypes.c_void_p]

        # Set CrucibleState TLS: mode=RECORD, context=vigil handle.
        # The C++ fallback reads these from TLS on every op.
        self._set_tls_state(self.RECORD, self._handle)

        # Enable DispatchKey::Crucible in this thread's TLS.
        torch._C._dispatch_tls_set_dispatch_key_included(
            self._dispatch_key, True)

        if self._verbose:
            print(f"[crucible] native dispatch active, handle={self._handle:#x}")

        return self

    def __exit__(self, *exc):
        # Disable dispatch key first (stops fallback from firing)
        if self._dispatch_key is not None:
            try:
                torch._C._dispatch_tls_set_dispatch_key_included(
                    self._dispatch_key, False)
            except Exception:
                pass

        # Clear TLS state
        self._set_tls_state(self.INACTIVE, 0)

        # Destroy Vigil
        if self._vessel and self._handle:
            self._vessel.destroy(self._handle)
            self._handle = 0

    # ── TLS state management ──────────────────────────────────────

    def _set_tls_state(self, mode: int, context: int):
        """Set CrucibleState TLS via exported C functions in libcrucible_dispatch.so.

        These are thin wrappers around CrucibleState::get_tls_state().set_mode()
        and CrucibleState::get_tls_state().set_context().  No pybind11 needed.
        """
        if not self._dispatch_lib:
            return
        self._dispatch_lib.crucible_dispatch_set_tls_mode(
            ctypes.c_uint8(mode))
        self._dispatch_lib.crucible_dispatch_set_tls_context(
            ctypes.c_void_p(context))

    # ── Query methods (mirror CrucibleMode API) ──────────────────

    def flush(self):
        if self._vessel and self._handle:
            self._vessel.flush(self._handle)

    def is_compiled(self) -> bool:
        return bool(self._vessel and self._vessel.is_compiled(self._handle))

    def compiled_iterations(self) -> int:
        if not self._vessel:
            return 0
        return self._vessel.compiled_iterations(self._handle)

    def diverged_count(self) -> int:
        if not self._vessel:
            return 0
        return self._vessel.diverged_count(self._handle)

    def bg_iterations(self) -> int:
        if not self._vessel:
            return 0
        return self._vessel.bg_iterations(self._handle)

    def ring_size(self) -> int:
        if not self._vessel:
            return 0
        return self._vessel.ring_size(self._handle)

    def metalog_size(self) -> int:
        if not self._vessel:
            return 0
        return self._vessel.metalog_size(self._handle)
