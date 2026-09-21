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
    from crucible_native import attach

    model = torchvision.models.resnet18()
    x = torch.randn(4, 3, 224, 224)

    with attach(model, optimizer, device="cuda:0") as ctx:
        for i in range(3):  # 1 warmup + 2 for iteration detection
            optimizer.zero_grad()
            out = model(x)
            loss = criterion(out, labels)
            loss.backward()
            optimizer.step()
        ctx.export_trace("resnet18.crtrace")

attach() takes over what a caller used to do by hand: it enables the dispatch
key, installs the module scope hooks for the recording phase and takes them off
again at activation, keeps torch.compile off the attached model, and wraps
Tensor.backward so that the runtime can see where a backward pass starts and
ends.  The phase of an operation is derived from that, not declared:

  BACKWARD   inside the extent of the Tensor.backward wrapper
  OPTIMIZER  a fused `_foreach_` kernel with gradients off
  FORWARD    anything else

The derivation lives in vessel/torch/record_kernel.h, which is the one copy
both recording paths read.
"""

import contextlib
import ctypes
import logging
import os
from collections.abc import Callable, Iterable, Iterator
from pathlib import Path
from typing import TYPE_CHECKING, Any

import torch

if TYPE_CHECKING:
    import torch.nn as nn
    import torch.optim as optim


log = logging.getLogger(__name__)


# =====================================================================
# FNV-1a 64-bit — must match C++ fnv1a_bytes() in crucible_fallback.cpp
# =====================================================================

_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME  = 0x100000001B3
_MASK64     = (1 << 64) - 1


# =====================================================================
# C ABI version stamp (GAPS-096)
# =====================================================================
#
# Mirrors `CRUCIBLE_VESSEL_ABI_VERSION` in vessel/torch/vessel_api.h.
# Bumped together with the C ABI surface; the loader compares this
# constant against the value returned by `crucible_abi_version()` on
# the loaded .so and refuses construction on mismatch.  Catches the
# ".py was redeployed but libcrucible_vessel.so is stale" failure
# before any dispatch runs.

EXPECTED_ABI_VERSION = 1


class CrucibleAbiMismatchError(RuntimeError):
    """Raised when the loaded libcrucible_vessel.so reports a
    crucible_abi_version() value that disagrees with this Python
    module's EXPECTED_ABI_VERSION constant.  Usually means the .so
    needs rebuilding (or the .py is stale).  Includes both observed
    and expected values for debugging."""


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
    """Search for a Crucible .so in common build directories.

    CRUCIBLE_BUILD_DIR names one directory to search before the usual
    ones. It exists because several agents share one worktree and each
    builds into its own directory, so the shared names below are not
    where a given run's libraries are. The value is a path, absolute or
    relative to the repository root.
    """
    base = Path(__file__).resolve().parent.parent.parent
    searched = []
    override = os.environ.get("CRUCIBLE_BUILD_DIR")
    if override:
        searched.append(Path(override) if Path(override).is_absolute() else base / override)
    searched += [base / d for d in ("build", "build-default", "build-gcc", "build-release")]
    for d in searched:
        p = d / "lib" / name
        if p.exists():
            return str(p)
    return None


# =====================================================================
# Vessel C API wrapper (Vigil lifecycle + trace export)
# =====================================================================

class _VesselLib:
    """Ctypes wrapper for libcrucible_vessel.so — Vigil lifecycle + queries."""

    def __init__(self, lib_path: str | None = None) -> None:
        """Load the vessel library and check its ABI stamp."""
        path = lib_path or _find_lib("libcrucible_vessel.so")
        if path is None:
            raise RuntimeError(
                "Cannot find libcrucible_vessel.so — build Crucible first:\n"
                "  cmake --preset default && cmake --build --preset default")
        self._lib = ctypes.CDLL(path)
        self._setup()
        self._check_abi(path)

    def _check_abi(self, path: str) -> None:
        """Verify the loaded .so's CRUCIBLE_VESSEL_ABI_VERSION matches
        EXPECTED_ABI_VERSION.  Raises CrucibleAbiMismatchError on
        drift — usually means the .so was rebuilt with a different
        ABI than this .py knows about.  Runs immediately after
        argtypes are declared so the call signature is well-formed."""
        observed = int(self._lib.crucible_abi_version())
        if observed != EXPECTED_ABI_VERSION:
            raise CrucibleAbiMismatchError(
                f"libcrucible_vessel.so ABI mismatch:\n"
                f"  loaded from   : {path}\n"
                f"  observed ABI  : {observed}\n"
                f"  expected ABI  : {EXPECTED_ABI_VERSION}\n"
                f"Rebuild Crucible after a vessel_api.h change:\n"
                f"  cmake --build --preset default")

    def _setup(self) -> None:
        """Declare the restype and argtypes of every symbol this class calls."""
        L = self._lib

        # ABI version (declared first so _check_abi can call it).
        L.crucible_abi_version.restype = ctypes.c_uint64
        L.crucible_abi_version.argtypes = []

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
        """Create a Vigil and return its opaque handle."""
        return self._lib.crucible_create()

    def destroy(self, h: int) -> None:
        """Destroy the Vigil behind one handle."""
        self._lib.crucible_destroy(h)

    def flush(self, h: int) -> None:
        """Wait until the background thread has drained the ring."""
        self._lib.crucible_flush(h)

    def is_compiled(self, h: int) -> bool:
        """True once the Vigil replays a region instead of recording one."""
        return bool(self._lib.crucible_is_compiled(h))

    def compiled_iterations(self, h: int) -> int:
        """The number of iterations the Vigil has replayed."""
        return self._lib.crucible_compiled_iterations(h)

    def diverged_count(self, h: int) -> int:
        """The number of times a replayed operation failed its guard."""
        return self._lib.crucible_diverged_count(h)

    def bg_iterations(self, h: int) -> int:
        """The number of iteration boundaries the background thread found."""
        return self._lib.crucible_bg_iterations(h)

    def ring_size(self, h: int) -> int:
        """The number of entries waiting in the TraceRing."""
        return self._lib.crucible_ring_size(h)

    def metalog_size(self, h: int) -> int:
        """The number of entries waiting in the MetaLog."""
        return self._lib.crucible_metalog_size(h)

    def export_crtrace(self, h: int, path: str) -> bool:
        """Write the active region to one .crtrace file."""
        return bool(self._lib.crucible_export_crtrace(h, path.encode()))

    def active_num_ops(self, h: int) -> int:
        """The number of operations in the active region."""
        return self._lib.crucible_active_num_ops(h)

    def register_schema_name(self, schema_hash: int, name: str) -> None:
        """Put one operator name into this library's copy of the SchemaTable."""
        self._lib.crucible_register_schema_name(
            ctypes.c_uint64(schema_hash), name.encode("utf-8"))


# =====================================================================
# Dispatch library wrapper (TLS accessors from crucible_fallback.cpp)
# =====================================================================

class _DispatchLib:
    """Ctypes wrapper for libcrucible_dispatch.so — TLS state + scope."""

    def __init__(self, lib_path: str | None = None) -> None:
        """Load the dispatch library, which activates the C++ fallback."""
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

    def _setup(self) -> None:
        """Declare the restype and argtypes of every symbol this class calls."""
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

        # The backward window, from which the C++ side derives the BACKWARD
        # phase into op_flags bits 2-3.
        L.crucible_dispatch_backward_enter.restype = None
        L.crucible_dispatch_backward_enter.argtypes = []
        L.crucible_dispatch_backward_exit.restype = None
        L.crucible_dispatch_backward_exit.argtypes = []
        L.crucible_dispatch_backward_depth.restype = ctypes.c_uint32
        L.crucible_dispatch_backward_depth.argtypes = []

        # Schema table accessors (for bridging to vessel lib before export)
        L.crucible_dispatch_schema_count.restype = ctypes.c_uint32
        L.crucible_dispatch_schema_count.argtypes = []
        L.crucible_dispatch_schema_entry.restype = ctypes.c_int
        L.crucible_dispatch_schema_entry.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char_p),
        ]

    def backward_enter(self) -> None:
        """Open a backward window on this thread."""
        self._lib.crucible_dispatch_backward_enter()

    def backward_exit(self) -> None:
        """Close the innermost backward window on this thread."""
        self._lib.crucible_dispatch_backward_exit()

    def backward_depth(self) -> int:
        """How many backward windows are open on this thread."""
        return self._lib.crucible_dispatch_backward_depth()

    def set_mode(self, mode: int) -> None:
        """Set the CrucibleState TLS mode for this thread."""
        self._lib.crucible_dispatch_set_tls_mode(ctypes.c_uint8(mode))

    def set_context(self, ctx: int) -> None:
        """Set the CrucibleState TLS Vigil handle for this thread."""
        self._lib.crucible_dispatch_set_tls_context(ctypes.c_void_p(ctx))

    def set_scope(self, scope_hash: int) -> None:
        """Set the scope hash the next recorded operations carry."""
        self._lib.crucible_dispatch_set_tls_scope(ctypes.c_uint64(scope_hash))

    def get_mode(self) -> int:
        """The CrucibleState TLS mode of this thread."""
        return self._lib.crucible_dispatch_get_tls_mode()

    def get_scope(self) -> int:
        """The scope hash of this thread."""
        return self._lib.crucible_dispatch_get_tls_scope()

    def schema_count(self) -> int:
        """The number of operators this library has recorded a name for."""
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

def _get_crucible_dispatch_key() -> Any:
    """Get DispatchKey::Crucible from the PyTorch fork, or None.

    The fork exposes the key on the Python enum, which is the first branch.
    The string lookup behind it serves a fork that carries the key in C++
    without the enum entry.
    """
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

    attach() is the way in.  Constructing this class directly gives the same
    controller without a model bound to it, which is what a session that only
    wants the dispatch key does.

    On activation:
      1. Load libcrucible_dispatch.so (registers TORCH_LIBRARY_IMPL fallback)
      2. Create Vigil via vessel C API
      3. Set CrucibleState TLS: mode=RECORD, context=vigil*
      4. Enable DispatchKey::Crucible in this thread's TLS

    While active:
      - Every ATen op goes through the recording kernels or crucibleFallback()
      - Scope hooks carry the module hierarchy, until the Vigil activates
      - Vigil records to TraceRing, bg thread builds DAG
      - The Tensor.backward wrapper marks the backward window, from which the
        C++ side derives the phase, and holds the autograd engine on this
        thread so that the whole window reaches the ring

    On deactivation:
      5. Disable DispatchKey::Crucible
      6. Clear TLS state
      7. Give the autograd engine its worker threads back
      8. Restore Tensor.backward and the attached model
      9. Remove scope hooks
      10. Destroy Vigil

    The TLS mode is a two-state gate for the C++ side: INACTIVE or not.  RECORD
    is what activation sets and the Vigil owns the move from recording a region
    to replaying one, which is_compiled() reports.
    """

    # CrucibleMode ordinals (must match c10::CrucibleMode enum)
    INACTIVE = 0
    RECORD   = 1
    COMPILED = 2
    DIVERGED = 3

    def __init__(self, *, vessel_lib_path: str | None = None,
                 dispatch_lib_path: str | None = None,
                 verbose: bool = False) -> None:
        """Prepare a controller.  Nothing is loaded until it is activated."""
        self._vessel_lib_path = vessel_lib_path
        self._dispatch_lib_path = dispatch_lib_path
        self._verbose = verbose

        self._vessel: _VesselLib | None = None
        self._dispatch: _DispatchLib | None = None
        self._handle: int = 0
        self._dispatch_key: Any = None
        self._active: bool = False

        # Scope tracking state
        self._hook_handles: list[Any] = []
        self._scope_names: dict[int, str] = {}  # hash -> module path

        # What attach() bound, and what has to be given back on the way out.
        # The scope model is held apart from the bound one: the hooks follow
        # whichever hierarchy track_modules was given, and only the bound
        # model's forward carries the compile mark to take off again.
        self._model: "nn.Module | None" = None
        self._scope_model: "nn.Module | None" = None
        self._optimizer: "optim.Optimizer | None" = None
        self._loader: Iterable[Any] | None = None
        self._device: torch.device | None = None
        self._model_forward_disabled: bool = False
        self._tensor_backward: Callable[..., Any] | None = None

        # The live autograd-serialisation guard, or None when the engine
        # keeps its worker threads.  One slot, so arming twice is a no-op
        # and no path restores the flag twice.
        self._mt_guard: Any = None

        # How many backward windows ran with the engine on this thread.  The
        # count is the evidence that the guard armed for each one, which no
        # later query can recover: by the time a loop asks, the guard of every
        # window it ran is already released.
        self._serialised_windows: int = 0

    def __enter__(self) -> "CrucibleNative":
        """Activate the controller, or return it unchanged if already active.

        attach() hands back an active controller, so `with attach(model) as ctx`
        reaches an already-active one here.  Returning it rather than loading a
        second Vigil is what makes the two spellings one thing.
        """
        if self._active:
            return self

        self._dispatch_key = _get_crucible_dispatch_key()
        if self._dispatch_key is None:
            raise RuntimeError(
                "PyTorch fork with DispatchKey::Crucible required.\n"
                "Build from ~/Downloads/pytorch with the crucible patch applied.")

        # Load vessel lib (Vigil lifecycle)
        self._vessel = _VesselLib(self._vessel_lib_path)
        self._handle = self._vessel.create()

        # From here the Vigil exists and owns a background thread, so every
        # later step that can fail has to give it back.  Loading the dispatch
        # library is the step that does fail in practice: it is the one built
        # only when the build was given a PyTorch tree, so a session against a
        # partial build reaches exactly this line.
        try:
            # Load dispatch lib (C++ fallback + TLS accessors)
            self._dispatch = _DispatchLib(self._dispatch_lib_path)

            # Set CrucibleState TLS: mode=RECORD, context=vigil handle
            self._dispatch.set_mode(self.RECORD)
            self._dispatch.set_context(self._handle)

            # Enable DispatchKey::Crucible in this thread
            import torch._C as _C
            _C._dispatch_tls_set_dispatch_key_included(
                self._dispatch_key, True)

            self._wrap_tensor_backward()
        except BaseException:
            self._teardown()
            raise

        self._active = True

        if self._verbose:
            log.info("[crucible] native dispatch active, vigil=%#x", self._handle)

        return self

    def __exit__(self, *exc: Any) -> None:
        """Deactivate the controller and give the runtime back as it was."""
        self.detach()

    def detach(self) -> None:
        """Take the recorder off the runtime.  Safe to call more than once."""
        if not self._active:
            return
        self._active = False
        self._teardown()

    def _teardown(self) -> None:
        """Undo each step of the activation, in the reverse order.

        A process that attached and detached is left as it was found.  Every
        step is written to be safe on a half-built controller, because the
        activation calls this when one of its own steps fails.
        """
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

        # Restore the engine before anything else can observe it.  A
        # session that left the guard armed would change the backward
        # behavior of code that is no longer being recorded.
        self._disarm_backward_serialisation()

        # Give back Tensor.backward and the model's own forward.  Both are
        # process-wide or caller-visible, so leaving either in place would
        # change code this session no longer records.
        self._restore_tensor_backward()
        self._restore_model_forward()

        # Remove scope hooks
        self._remove_hooks()

        # Destroy Vigil
        if self._vessel and self._handle:
            self._vessel.destroy(self._handle)
            self._handle = 0

        if self._verbose:
            log.info("[crucible] native dispatch deactivated")

    # ── What attach() binds ──────────────────────────────────────────

    def bind(self, model: "nn.Module",
             optimizer: "optim.Optimizer | None" = None,
             loader: Iterable[Any] | None = None,
             device: "torch.device | str | None" = None) -> None:
        """Bind one training loop's parts to this controller.

        The model is moved to the device when one is named, kept out of the
        reach of torch.compile, and tracked for module scope.  The optimizer
        and the loader are held for reference and for the check below.

        Call this on an active controller.  attach() does it for the caller.
        """
        if not self._active:
            raise RuntimeError("bind() requires an active CrucibleNative")

        self._model = model
        self._optimizer = optimizer
        self._loader = loader

        if device is not None:
            self._device = torch.device(device)
            model.to(self._device)

        self._disable_model_compile(model)
        self.track_modules(model)
        self._warn_if_optimizer_phase_is_invisible(optimizer)

    def _disable_model_compile(self, model: "nn.Module") -> None:
        """Keep torch.compile off the attached model.

        Crucible is the compiler for this model: it records the operations the
        model dispatches and replays them from a region of its own.  A dynamo
        graph would capture those operations before the dispatcher sees them,
        so the recorder would be handed a compiled artifact instead of the
        model, and the region would describe the artifact.

        The mark goes on the model's own forward rather than on a returned
        wrapper, so it holds for the caller's object.  torch.compile() applied
        to this model later then finds a forward it must not trace.
        """
        model.forward = torch.compiler.disable(model.forward)
        self._model_forward_disabled = True

    def _restore_model_forward(self) -> None:
        """Take the compile mark off the attached model's forward."""
        if not self._model_forward_disabled:
            return
        self._model_forward_disabled = False
        model = self._model
        if model is not None:
            # The mark is an instance attribute shadowing the bound method, so
            # deleting it uncovers the class's own forward.
            model.__dict__.pop("forward", None)

    def _warn_if_optimizer_phase_is_invisible(
            self, optimizer: "optim.Optimizer | None") -> None:
        """Say so when this optimizer asked for single-tensor kernels.

        The OPTIMIZER phase is derived from a fused `_foreach_` kernel with
        gradients off, so a step built on single-tensor kernels carries the
        FORWARD phase bits instead.  The step is still recorded and replayed
        correctly; only the label is lost.

        What this check reads is the one case that is visible from here: a group
        that asked for foreach=False outright.  A group that left the choice to
        the runtime is not covered, because the runtime makes it per device
        inside the step and a CPU step commonly resolves to single-tensor
        kernels.  So silence here is not a promise that the step will carry the
        OPTIMIZER label.
        """
        if optimizer is None:
            return
        groups = optimizer.param_groups
        if groups and all(group.get("foreach") is False for group in groups):
            log.warning(
                "[crucible] %s asked for foreach=False, so its step emits no "
                "_foreach_ kernel and its operations carry the FORWARD phase",
                type(optimizer).__name__)

    # ── The backward window ──────────────────────────────────────────
    #
    # The phase of an operation is derived in C++ (record_kernel.h), and the
    # one fact the derivation cannot read off the operation is whether a
    # backward pass is running.  This wrapper is where that fact comes from:
    # the window is the dynamic extent of Tensor.backward, bracketed by a
    # counter in the dispatch library.
    #
    # The extent is a better signal than the thread the operation arrived on,
    # and it is the serialisation below that makes it so.  Thread identity used
    # to be the only option, because a backward pass over accelerator tensors
    # ran on a device worker thread and a flag on the calling thread would have
    # missed every operation of it.  With the engine held on the calling
    # thread, the window and the operations it produces are on one thread, so
    # the flag sees all of them.  The counter stays thread-local so that a
    # caller who reaches the engine another way is merely short of a phase bit
    # instead of labelling another thread's forward pass backward.
    #
    # Wrapping the class rather than the tensor is what makes this work for a
    # caller who writes loss.backward() and nothing else.  torch.autograd.grad
    # and a direct torch.autograd.backward call are not covered; a loop that
    # uses either wraps it in recording_backward() to get the serialisation,
    # and its backward operations carry the FORWARD phase bits.

    def _wrap_tensor_backward(self) -> None:
        """Wrap Tensor.backward for the lifetime of this controller."""
        if self._tensor_backward is not None:
            return
        original = torch.Tensor.backward
        self._tensor_backward = original
        controller = self
        # Bound once, as the scope hooks bind it: the wrapper is installed
        # after the library is loaded and removed before the controller lets go
        # of it, so the handle cannot go away underneath a call.
        dispatch = self._dispatch

        def backward(tensor: torch.Tensor, *args: Any, **kwargs: Any) -> Any:
            """Run one backward pass inside a marked, serialised window."""
            with controller.recording_backward():
                dispatch.backward_enter()
                try:
                    return original(tensor, *args, **kwargs)
                finally:
                    dispatch.backward_exit()
                    controller.sync_scope_hooks()

        torch.Tensor.backward = backward  # type: ignore[method-assign]

    def _restore_tensor_backward(self) -> None:
        """Put the runtime's own Tensor.backward back."""
        original, self._tensor_backward = self._tensor_backward, None
        if original is not None:
            torch.Tensor.backward = original  # type: ignore[method-assign]

    def backward_depth(self) -> int:
        """How many backward windows are open on this thread."""
        return self._dispatch.backward_depth() if self._dispatch else 0

    @property
    def serialised_backward_windows(self) -> int:
        """How many backward windows ran with the engine on this thread.

        A loop reads this to check that every window it ran was serialised.
        The guard of a finished window is already released, so the count is the
        only record that it was ever armed.
        """
        return self._serialised_windows

    # ── Module scope tracking ────────────────────────────────────────

    def sync_scope_hooks(self) -> None:
        """Match the scope hooks to the mode the Vigil is in.

        The hooks exist to put a module path on each recorded operation.  A
        replayed operation records nothing and carries no scope, so from
        activation onward each hook is a Python call per module per iteration
        that reaches a value nobody reads.  Removing them also gives
        nn.Module._call_impl its no-hooks fast path back.

        The reverse direction matters as much.  A divergence puts the Vigil
        back to recording, and a session that only ever took hooks off would
        record every later region without a module path on any operation.  So
        this reads the mode and installs or removes to match it.

        The scope hash is not part of the region content hash, so neither
        direction can turn into a divergence of its own.

        The backward wrapper calls this once per iteration.  A loop with no
        backward pass calls it itself.
        """
        if self._scope_model is None:
            return
        compiled = self.is_compiled()
        if compiled and self._hook_handles:
            self._remove_hooks()
            if self._verbose:
                log.info("[crucible] region activated, scope hooks removed")
        elif not compiled and not self._hook_handles:
            self.track_modules(self._scope_model)

    def track_modules(self, model: "nn.Module", backward_hooks: bool = False) -> None:
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
        self._scope_model = model

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
            log.info("[crucible] tracking %d modules (%d hooks, %s)",
                     n_modules, len(self._hook_handles), kind)

    def _make_forward_hook(self, scope_hash: int) -> Callable[..., None]:
        """Build the forward pre-hook that names one module's scope."""
        dispatch = self._dispatch

        def hook(module: "nn.Module", input: Any) -> None:
            """Set the scope hash the next recorded operations carry."""
            dispatch.set_scope(scope_hash)
        return hook

    def _make_backward_hook(self, scope_hash: int) -> Callable[..., None]:
        """Build the backward pre-hook that names one module's scope."""
        dispatch = self._dispatch

        def hook(module: "nn.Module", grad_output: Any) -> None:
            """Set the scope hash the next recorded operations carry."""
            dispatch.set_scope(scope_hash)
        return hook

    def _remove_hooks(self) -> None:
        """Remove every scope hook this controller installed."""
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
                log.info("[crucible] no active region (bg_iterations=%d)", bg_iters)
                log.info("[crucible] need 2+ complete iterations for detection")
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
                log.info("[crucible] exported %d ops to %s", num_ops, path)
            else:
                log.error("[crucible] export failed: %s", path)
        return ok

    def _bridge_schema_names(self) -> None:
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
            log.info("[crucible] bridged %d schema names from dispatch lib "
                     "to vessel lib", len(entries))

    # ── Queries ──────────────────────────────────────────────────────

    def flush(self) -> None:
        """Wait until bg thread has fully processed all recorded ops."""
        if self._vessel and self._handle:
            self._vessel.flush(self._handle)

    def is_compiled(self) -> bool:
        """True once the Vigil replays a region instead of recording one."""
        return bool(self._vessel and self._vessel.is_compiled(self._handle))

    def compiled_iterations(self) -> int:
        """The number of iterations the Vigil has replayed."""
        return self._vessel.compiled_iterations(self._handle) if self._vessel else 0

    def diverged_count(self) -> int:
        """The number of times a replayed operation failed its guard."""
        return self._vessel.diverged_count(self._handle) if self._vessel else 0

    def bg_iterations(self) -> int:
        """The number of iteration boundaries the background thread found."""
        return self._vessel.bg_iterations(self._handle) if self._vessel else 0

    def ring_size(self) -> int:
        """The number of entries waiting in the TraceRing."""
        return self._vessel.ring_size(self._handle) if self._vessel else 0

    def metalog_size(self) -> int:
        """The number of entries waiting in the MetaLog."""
        return self._vessel.metalog_size(self._handle) if self._vessel else 0

    def active_num_ops(self) -> int:
        """The number of operations in the active region."""
        return self._vessel.active_num_ops(self._handle) if self._vessel else 0

    # ── Autograd engine serialisation ────────────────────────────────
    #
    # The recorded op order is the trace's identity.  It fixes the region
    # content hash, the memory plan and the replay order.  The ring is
    # single-producer, so one thread alone can contribute to that order.
    #
    # By default this runtime runs a backward pass on several threads.
    # Engine::execute_with_graph_task drives the graph task's CPU ready
    # queue on the calling thread, and one worker thread serves each
    # accelerator.  A graph with a host branch and a device branch runs
    # its backward pass on two threads at the same time.  A graph across
    # two accelerators runs on two worker threads, with the calling thread
    # parked.  Every thread but the first is turned away at the ring, so
    # the trace of an accelerator model was short by its whole backward
    # window.
    #
    # Engine::ready_queue routes every node to the CPU ready queue when
    # multithreading is off, and the calling thread drives that queue.
    # One thread then produces the whole backward pass, the producer gate
    # admits it, and the window reaches the trace.
    #
    # Serialising removes a source of variation rather than adding one.
    # Measured on three accelerators, 80 runs for each mode, over a graph
    # with several paths into one leaf: the default engine gave two
    # distinct gradients, 78 runs against 2, and the serialised engine
    # gave one.  Accumulation order decides that gradient, and the order
    # is what the worker threads leave undefined.
    #
    # The guard covers every backward window the Vigil sees, recording and
    # compiled alike.  A compiled dispatch records nothing, but it advances
    # the replay cursor one operation at a time, and the region it walks is
    # a recording of the serialised stream.  A backward window that keeps
    # its worker threads reaches the producer gate on those threads and is
    # turned away, so the cursor stands still across the whole window and
    # the first operation after it fails its guard.  Measured on cuda:0
    # with the guard lifted under replay: the cursor reached index 286 of
    # an 801-operation region, where the region holds aten::zero_ from the
    # engine-driven window and the arriving operation was
    # aten::_foreach_mul_.Scalar from the optimizer.
    #
    # Replaying a stream needs the same single producer that recorded it.
    # That is the rule the Tensor.backward wrapper encodes: it arms the guard
    # across every window, whichever mode the Vigil is in.

    def _arm_backward_serialisation(self) -> None:
        """Make the next backward pass run on this thread alone."""
        if self._mt_guard is not None:
            return
        guard = torch.autograd.set_multithreading_enabled(False)
        guard.__enter__()
        self._mt_guard = guard
        self._serialised_windows += 1

    def _disarm_backward_serialisation(self) -> None:
        """Give the backward pass its worker threads back."""
        guard, self._mt_guard = self._mt_guard, None
        if guard is not None:
            guard.__exit__(None, None, None)

    @property
    def backward_serialised(self) -> bool:
        """True while the guard holds the engine on the calling thread."""
        return self._mt_guard is not None

    @contextlib.contextmanager
    def recording_backward(self) -> Iterator["CrucibleNative"]:
        """Serialise the autograd engine across one backward pass.

        The Tensor.backward wrapper uses this, so a loop that writes
        loss.backward() gets it without asking.  A loop that reaches the engine
        another way asks for it:

            with ctx.recording_backward():
                torch.autograd.backward(loss)

        The body runs on one thread whether the Vigil records the window or
        replays it.  Refer to the note above.
        """
        # A guard already armed by an enclosing call stays with that caller,
        # so the restore below belongs to whoever armed it.
        armed_here = not self.backward_serialised
        if armed_here:
            self._arm_backward_serialisation()
        try:
            yield self
        finally:
            if armed_here:
                self._disarm_backward_serialisation()

    @property
    def scope_names(self) -> dict[int, str]:
        """Map of scope_hash -> module path for all tracked modules."""
        return self._scope_names


# =====================================================================
# attach — the way in
# =====================================================================

def attach(model: "nn.Module",
           optimizer: "optim.Optimizer | None" = None,
           loader: Iterable[Any] | None = None,
           device: "torch.device | str | None" = None,
           *,
           verbose: bool = False,
           vessel_lib_path: str | None = None,
           dispatch_lib_path: str | None = None) -> CrucibleNative:
    """Put the recorder on one training loop and return the live controller.

    The controller comes back active, so both spellings work:

        ctx = attach(model, optimizer, device="cuda:0")
        ...
        ctx.detach()

        with attach(model, optimizer, device="cuda:0") as ctx:
            ...

    Args:
        model: The model to record.  attach moves it to the device when one is
            named, keeps torch.compile off it, and tracks its module hierarchy
        optimizer: The optimizer of the loop, held for reference and checked
            against the phase derivation.  None when the loop has none
        loader: The data source of the loop, held for reference.  Nothing reads
            it yet: absorbing the input pipeline is a later layer of the runtime
        device: Where the model runs.  None leaves the model where it is, which
            is what a loop that places its own parameters wants
        verbose: Log the lifecycle of the recorder
        vessel_lib_path: An explicit path to libcrucible_vessel.so
        dispatch_lib_path: An explicit path to libcrucible_dispatch.so

    Returns:
        The active controller

    Raises:
        RuntimeError: If the PyTorch build carries no DispatchKey::Crucible
    """
    controller = CrucibleNative(vessel_lib_path=vessel_lib_path,
                                dispatch_lib_path=dispatch_lib_path,
                                verbose=verbose)
    controller.__enter__()
    try:
        controller.bind(model, optimizer, loader, device)
    except BaseException:
        controller.detach()
        raise
    return controller
