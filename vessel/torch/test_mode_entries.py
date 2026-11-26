#!/usr/bin/env python3
"""CrucibleMode and CrucibleNative record one entry for one op, and the mode keeps each scalar of an op.

The native fallback reads the boxed stack of an op (extract_inputs and
extract_outputs of crucible_fallback.cpp, and entry_op_flags of
record_kernel.h).  CrucibleMode walks the schema of the op, and it states the
same rules again (_extract_inputs, _extract_outputs and _op_flags of
crucible_mode.py).  This test keeps the two statements equal.  Each check
records a loop under a session until the region of the loop replays, and it
exports the region:

    rules       one loop for each rule (rule_steps), under the two sessions.
                Each op at depth zero must have the same fields that the
                content hash and the guard word read: the schema hash, the
                guard word, the counts, the scalar values, their tags, the data
                slots, the depth and the fields of each record that the two
                hashes read.  It must also have the same op flags and the same
                grad bit.  When the native region holds no nested op, the two
                content hashes must be the same too.  A CPU kernel that calls
                other ops through the dispatcher gives nested ops, which the
                native path records and the mode does not see
    maximize    two loops of aten::_fused_adam_ that differ only in maximize,
                its seventh scalar, give two content hashes under the mode
    stack       in the handler of the mode, torch._C._len_torch_dispatch_stack()
                does not count the mode, and it counts a mode below it.  The
                TORCH_FUNCTION bit of the mode rests on this fact
    dtypes      the c10 ordinal of each element type of the mode is the
                ordinal of torch.ops.prim.dtype, for each element type of a
                tensor that torch.empty makes

Usage:
    python test_mode_entries.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import contextlib
import gc
import sys
import tempfile
from pathlib import Path
from typing import Callable, NamedTuple

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from torch.utils._python_dispatch import TorchDispatchMode  # noqa: E402

# The import sets one intra-op thread and one inter-op thread.  PyTorch refuses
# a second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import OP_RECORD, read_trace  # noqa: E402

import crucible_mode  # noqa: E402
from crucible_native import CrucibleMeta, CrucibleNative  # noqa: E402

# The most iterations of a loop before its region must replay.
MAX_ITERATIONS = 64

# The bits of the flags of a record that the content hash reads: the negative
# and the conjugate bits.
VALUE_BITS = 0x30

# The rules whose check compared the two content hashes.  A CPU kernel that
# calls other ops through the dispatcher gives nested ops, which the native
# path records and the mode does not see, so such a rule compares no hash.
COMPARED_HASHES: list[str] = []


class Region(NamedTuple):
    """The region of one loop: its ops and its content hash."""

    ops: list
    content_hash: int

    def top_ops(self) -> list:
        """Return the ops at depth zero."""
        return [op for op in self.ops if op.depth == 0]


class Rule(NamedTuple):
    """One rule of the native paths, and a loop of ops that the rule reads.

    outer gives a context that holds the session, such as a dispatch mode
    below the session.
    """

    label: str
    step: Callable[[], None]
    outer: Callable[[], contextlib.AbstractContextManager] = contextlib.nullcontext


class PassThrough(TorchDispatchMode):
    """A dispatch mode that runs each op as it is."""

    def __torch_dispatch__(self, func, types, args=(), kwargs=None):
        """Run the op."""
        return func(*args, **(kwargs or {}))


class Subclass(torch.Tensor):
    """A tensor subclass with __torch_dispatch__, so PyTorch gives its tensors the Python dispatch key."""

    @classmethod
    def __torch_dispatch__(cls, func, types, args=(), kwargs=None):
        """Run the op with the Python dispatch key off, and give plain tensors."""
        with torch._C._DisableTorchDispatch():
            return func(*args, **(kwargs or {}))


def fused_adam(width: int, lr=0.001, maximize: bool = False) -> Callable[[], None]:
    """Return one step of aten::_fused_adam_ over width parameters of distinct extents."""
    params = [torch.ones(4 + i) for i in range(width)]
    grads = [torch.full((4 + i,), 0.1) for i in range(width)]
    exp_avgs = [torch.zeros(4 + i) for i in range(width)]
    exp_avg_sqs = [torch.zeros(4 + i) for i in range(width)]
    state_steps = [torch.tensor(1.0) for _ in range(width)]

    def step() -> None:
        torch._fused_adam_(params, grads, exp_avgs, exp_avg_sqs, [], state_steps, lr=lr, beta1=0.9, beta2=0.999,
                           weight_decay=0.0, eps=1e-8, amsgrad=False, maximize=maximize)

    return step


def rule_steps() -> list[Rule]:
    """Return one loop for each rule.  Each loop makes its tensors before the session starts."""
    source = torch.arange(6.0).reshape(2, 3)
    other = torch.ones(2, 3)
    index = torch.tensor([0, 2])
    vector = torch.ones(2)
    first = torch.ones(3)
    second = torch.ones(4)
    single = torch.tensor(2.5)
    complex_source = torch.ones(2, 3, dtype=torch.complex64)
    subclass = torch.Tensor._make_subclass(Subclass, torch.ones(2, 3))
    weight = torch.ones(3, requires_grad=True)
    factor = torch.full((3,), 2.0)
    # Distinct extents, so the detector of the iterations finds no shorter
    # period in the inputs of one op.
    forty = [torch.ones(1 + i) for i in range(40)]
    many = [torch.ones(1 + i) for i in range(130)]

    def optimizer_step() -> None:
        with torch.no_grad():
            torch.ops.aten._foreach_add_.ScalarList([first, second], [0.5, 0.25])

    def backward_step() -> None:
        # With no gradient to accumulate, the engine runs the same ops under
        # the two sessions.  Under a dispatch mode, PyTorch accumulates a
        # gradient out of place, and with no mode in place.
        weight.grad = None
        torch.ops.aten.mul.Tensor(weight, factor).sum().backward()

    return [
        Rule("a left-out default (alpha of add.Tensor)", lambda: torch.ops.aten.add.Tensor(source, other)),
        Rule("a complex Scalar, which gives no slot (alpha of add.Tensor)",
             lambda: torch.ops.aten.add.Tensor(complex_source, complex_source, alpha=1 + 2j)),
        Rule("a None (the max of clamp)", lambda: torch.ops.aten.clamp.default(source, 0.5)),
        Rule("a None element of a Tensor?[] list (index.Tensor)",
             lambda: torch.ops.aten.index.Tensor(source, [None, index])),
        Rule("an int[] list (the dim of sum.dim_IntList)",
             lambda: torch.ops.aten.sum.dim_IntList(source, [1])),
        Rule("a float[] list (the addends of _test_optional_floatlist)",
             lambda: torch.ops.aten._test_optional_floatlist.default(vector, [1.5, 2.5])),
        Rule("a Scalar[] list (the scalars of _foreach_add.ScalarList)",
             lambda: torch.ops.aten._foreach_add.ScalarList([first, second], [1.5, 2])),
        Rule("a dtype, a layout and a memory format (empty.memory_format)",
             lambda: torch.ops.aten.empty.memory_format([1, 2, 3, 4], dtype=torch.float64, layout=torch.strided,
                                                        memory_format=torch.channels_last)),
        # PyTorch converts the int to a float before the mode sees it, so the
        # case pins the agreement of the two paths.
        Rule("a Python int in a float argument (the lr of _fused_adam_)", fused_adam(1, lr=1)),
        Rule("a Python number in a tensor position (the divisor of div.Tensor)",
             lambda: torch.ops.aten.div.Tensor(source, 2.0)),
        Rule("a scalar result (_local_scalar_dense)", lambda: torch.ops.aten._local_scalar_dense.default(single)),
        Rule("inputs past the 32 records (cat of 40 tensors)", lambda: torch.ops.aten.cat.default(forty)),
        Rule("more records than the record stack (cat of 130 tensors)", lambda: torch.ops.aten.cat.default(many)),
        Rule("an in-place op of a fused optimizer step with grad off (the phase OPTIMIZER)", optimizer_step),
        Rule("the ops of a backward pass (the phase BACKWARD)", backward_step),
        Rule("a tensor subclass argument (TORCH_FUNCTION)", lambda: torch.ops.aten.add.Tensor(subclass, other)),
        Rule("another dispatch mode on the stack (TORCH_FUNCTION)", lambda: torch.ops.aten.mul.Tensor(source, other),
             PassThrough),
    ]


def region_of(kind: str, rule: Rule, directory: Path, label: str) -> Region | None:
    """Record the loop of rule under the session KIND until its region replays, export the region, and return it."""
    path = directory / f"{label}-{kind}.crtrace"
    with rule.outer():
        if kind == "mode":
            mode = crucible_mode.CrucibleMode()
            with mode:
                for _ in range(MAX_ITERATIONS):
                    rule.step()
                    mode.flush()
                    if mode.is_compiled():
                        break
            region = None
            if mode.is_compiled() and mode.export_trace(path):
                region = Region(read_trace(path), mode._vessel.active_content_hash(mode._handle))
            # The finalizer of the mode ends its Vigil, so the next session of
            # the thread can start.
            del mode
            gc.collect()
            return region
        with CrucibleNative() as ctx:
            for _ in range(MAX_ITERATIONS):
                rule.step()
                ctx.flush()
                if ctx.is_compiled():
                    if not ctx.export_trace(str(path)):
                        return None
                    return Region(read_trace(path), ctx.active_content_hash())
    return None


def hashed_fields(meta: bytes) -> tuple:
    """Return the fields of a record that the content hash and the guard word read."""
    record = CrucibleMeta.from_buffer_copy(meta)
    lanes = min(record.ndim, 8)
    return (record.ndim, tuple(record.sizes[:lanes]), tuple(record.strides[:lanes]), record.dtype,
            record.device_type, record.device_idx, record.layout, record.flags & VALUE_BITS, record.storage_offset)


def entry_fields(op) -> dict:
    """Return the fields of an op of an exported region that the two hashes read, with the op flags."""
    (schema_hash, shape_hash, _scope, _callsite, *rest) = OP_RECORD.unpack(op.record)
    values = rest[:5]
    num_inputs, num_outputs, num_scalars, grad_enabled, flags, depth, scalar_types, data_slots = rest[5:]
    return {
        "schema hash": schema_hash,
        "guard word": shape_hash,
        "inputs": num_inputs,
        "outputs": num_outputs,
        "scalars": num_scalars,
        "scalar values": tuple(values[:num_scalars]),
        "tags": scalar_types,
        "data slots": data_slots,
        "depth": depth,
        "grad bit": grad_enabled,
        "op flags": flags,
        "records": tuple(hashed_fields(meta) for meta in op.metas),
    }


def pairs_of(label: str, native_ops: list, mode_ops: list) -> tuple[list, list[str]]:
    """Return the pairs of ops at depth zero to compare, and the problems of the alignment.

    A loop of one op gives regions of one or more of its iterations, so each op
    of the mode pairs with the first native op.  A loop of more ops gives one
    iteration in each region, and the ops pair in their order.
    """
    if len({op.schema_hash for op in native_ops} | {op.schema_hash for op in mode_ops}) == 1:
        return [(native_ops[0], mode_op) for mode_op in mode_ops], []
    if len(native_ops) != len(mode_ops):
        return [], [f"{label}: the region holds {len(native_ops)} ops at depth zero under CrucibleNative and "
                    f"{len(mode_ops)} under CrucibleMode"]
    return list(zip(native_ops, mode_ops)), []


def check_rule(rule: Rule, directory: Path, number: int) -> list[str]:
    """Return one line for each field of the entries of the rule that differs between the two sessions."""
    label = f"rule{number}"
    native = region_of("native", rule, directory, label)
    mode = region_of("mode", rule, directory, label)
    if native is None or mode is None:
        missing = "CrucibleNative" if native is None else "CrucibleMode"
        return [f"{rule.label}: the loop does not replay under {missing} after {MAX_ITERATIONS} iterations"]
    native_ops = native.top_ops()
    mode_ops = mode.top_ops()
    if not native_ops or not mode_ops:
        return [f"{rule.label}: a region holds no op at depth zero"]
    pairs, problems = pairs_of(rule.label, native_ops, mode_ops)
    for native_op, mode_op in pairs:
        native_fields = entry_fields(native_op)
        mode_fields = entry_fields(mode_op)
        for name, native_value in native_fields.items():
            if mode_fields[name] != native_value:
                problems.append(f"{rule.label}: the {name} of the entry is {mode_fields[name]} under CrucibleMode "
                                f"and {native_value} under CrucibleNative")
        if problems:
            break
    nested = len(native.ops) - len(native_ops)
    if nested == 0 and len(native.ops) == len(mode.ops):
        COMPARED_HASHES.append(rule.label)
        if native.content_hash != mode.content_hash:
            problems.append(f"{rule.label}: the content hash is {mode.content_hash:#x} under CrucibleMode and "
                            f"{native.content_hash:#x} under CrucibleNative")
    else:
        print(f"  note: {rule.label}: the native region holds {len(native_ops)} ops at depth zero and {nested} nested "
              f"ops, and the region of the mode holds {len(mode.ops)} ops, so the check compares no content hash")
    return problems


def check_maximize(directory: Path) -> list[str]:
    """Return a line when two loops that differ only in maximize give one content hash under the mode."""
    words = []
    for maximize in (False, True):
        region = region_of("mode", Rule("maximize", fused_adam(1, maximize=maximize)), directory,
                           f"maximize-{maximize}")
        if region is None:
            return [f"maximize: the loop with maximize={maximize} does not replay under CrucibleMode"]
        words.append(region.content_hash)
    if words[0] == words[1]:
        return [f"maximize: the two values of maximize give one content hash {words[0]:#x} under CrucibleMode, so "
                f"the mode drops the seventh scalar of _fused_adam_"]
    return []


def check_stack_count() -> list[str]:
    """Return a line when the count of the dispatch stack in the handler of the mode counts the mode itself."""
    counts: list[int] = []

    class Probe(crucible_mode.CrucibleMode):
        """The mode, with a record of the count of the dispatch stack in its handler."""

        def __torch_dispatch__(self, func, types, args=(), kwargs=None):
            """Record the count, then run the handler of the mode."""
            counts.append(torch._C._len_torch_dispatch_stack())
            return super().__torch_dispatch__(func, types, args, kwargs)

    source = torch.ones(2)
    for outer, expected in ((contextlib.nullcontext, 0), (PassThrough, 1)):
        counts.clear()
        with outer():
            probe = Probe()
            with probe:
                torch.ops.aten.add.Tensor(source, source)
            del probe
            gc.collect()
        if counts != [expected]:
            return [f"stack: the handler of the mode counts {counts} modes on the stack with "
                    f"{outer.__name__}, not [{expected}]"]
    return []


def check_dtype_ordinals() -> list[str]:
    """Return one line for each element type whose c10 ordinal in the mode is not the ordinal of PyTorch."""
    problems = []
    ordinals = getattr(crucible_mode, "_C10_DTYPE_ORDINALS", None)
    if ordinals is None:
        return ["dtypes: crucible_mode has no table of the c10 ordinals of the element types"]
    for name in dir(torch):
        dtype = getattr(torch, name)
        if not isinstance(dtype, torch.dtype):
            continue
        ordinal = ordinals.get(dtype)
        if ordinal is None:
            problems.append(f"dtypes: the mode has no c10 ordinal of torch.{name}")
            continue
        try:
            probe = torch.empty(0, dtype=dtype)
        except (RuntimeError, TypeError):
            continue
        if torch.ops.prim.dtype(probe) != ordinal:
            problems.append(f"dtypes: the mode gives torch.{name} the c10 ordinal {ordinal}, and PyTorch gives "
                            f"{torch.ops.prim.dtype(probe)}")
    return problems


def main() -> int:
    """Run each check and report."""
    problems = []
    with tempfile.TemporaryDirectory(prefix="crucible-mode-entries-") as scratch:
        directory = Path(scratch)
        for number, rule in enumerate(rule_steps()):
            problems += check_rule(rule, directory, number)
        problems += check_maximize(directory)
    if not COMPARED_HASHES:
        problems.append("rules: no rule compared the two content hashes")
    problems += check_stack_count()
    problems += check_dtype_ordinals()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_mode_entries: each check passed, and {len(COMPARED_HASHES)} rules compared the content hashes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
