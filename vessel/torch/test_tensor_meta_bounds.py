#!/usr/bin/env python3
"""The two recording paths of the vessel record no tensor that a TensorMeta cannot describe.

A TensorMeta holds at most 8 dimensions, and each size and each stride is at
most 2^59 - 1 (kMaxTensorDimExtent of crucible/TensorMeta.h).  An operation
with a tensor past one of these bounds runs eagerly and stays out of the ring.
The first check records a loop under CrucibleMode, which gives each operation
to the C entry crucible_dispatch_op_ex:

    mode rank       tensors of rank 9 and 10 as an input and as an output, and a
                    view of the size 2^60.  The record of the mode holds the
                    true rank, so the C entry refuses each such operation.  A
                    region forms, it holds no operation of these tensors, and
                    the rank-8 pair below gives two records with different bytes

Each other check records a loop under CrucibleNative, the dispatch library:

    rank            tensors of rank 9 and 10 as an input and as an output of an
                    unboxed kernel, and as an output of the boxed fallback.  A
                    region forms, and it holds no operation of these tensors
    two shapes      the views [1]*6 + [3, 5] and [1]*6 + [5, 3] of rank 8 give
                    two records with different bytes
    rank nests      an operation whose kernel runs other operations: a fused
                    operation with a tensor of rank 9 past the inline records,
                    and the boxed fallback with a tensor of rank 9 or 10 as an
                    input, as an output of a list, and past the inline records.
                    The Vigil records the operations inside, and the closing
                    entry of the nest of the refused operation keeps the loop
                    eager, so no region forms.  The same loop with a tensor of
                    rank 8 in place of the wide tensor forms a region that
                    holds the operation
    extent          a view of the size 2^60 (an expand of one element, which
                    holds no storage) and a view of the stride 2^60 (an
                    as_strided with the size 1).  A region forms, it holds no
                    operation of these views, and the process does not end.
                    The boxed fallback with the view of the size 2^60 as an
                    input forms no region, and the same loop with the element
                    forms one
    eager results   each refused operation gives the result of eager PyTorch,
                    and the replay of each region counts no divergence

Usage:
    python test_tensor_meta_bounds.py

Exit 0 when each check passes, 1 otherwise.  The extent checks run last,
because a vessel that admits the size ends the process at a contract check.
"""

from __future__ import annotations

import struct
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path
from typing import Any

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_mode import CrucibleMode  # noqa: E402
from crucible_native import CrucibleMeta, CrucibleNative  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so the
# instruction count of the test is the same in each run.  PyTorch refuses a
# second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import MAX_ITERATIONS, TraceOp, read_trace, schema_hash  # noqa: E402

META_NDIM_OFFSET = CrucibleMeta.ndim.offset
META_SIZES_OFFSET = CrucibleMeta.sizes.offset

# The most dimensions of a TensorMeta.
MAX_NDIM = 8

# A size and a stride past the largest extent, 2^59 - 1.  The views of this
# size and stride hold one element of storage, so the test allocates nothing
# large.
HUGE_EXTENT = 2**60

# The records of one operation that an entry holds inline (MAX_INLINE_METAS
# of vessel/torch/entry_slots.h).  A list of one more tensor puts the last
# tensor past the inline records.
INLINE_RECORDS = 32

# The two views of B7: one storage of 15 elements, and two shapes that differ
# only past the eighth dimension.  The rank-8 pair keeps each dimension.
WIDE_SHAPES = ([1] * 8 + [3, 5], [1] * 8 + [5, 3])
NARROW_SHAPES = ([1] * 6 + [3, 5], [1] * 6 + [5, 3])
RANK_SEVEN = [1] * 5 + [3, 5]
RANK_EIGHT = [1] * 6 + [3, 5]
RANK_NINE = [1] * 7 + [3, 5]

Step = Callable[[], list[torch.Tensor]]


@torch.library.custom_op("crucible_bounds::wide_input", mutates_args=())
def wide_input(source: torch.Tensor) -> torch.Tensor:
    """Return a tensor of three zeros.  The boxed fallback records the source as an input."""
    del source
    return torch.zeros(3)


@torch.library.custom_op("crucible_bounds::huge_input", mutates_args=())
def huge_input(source: torch.Tensor) -> torch.Tensor:
    """Return a tensor of three zeros, and read no element of the source."""
    del source
    return torch.zeros(3)


@torch.library.custom_op("crucible_bounds::wide_output", mutates_args=())
def wide_output(source: torch.Tensor) -> torch.Tensor:
    """Return a copy of the source with one more dimension in front."""
    return source.unsqueeze(0).clone()


@torch.library.custom_op("crucible_bounds::wide_outputs", mutates_args=())
def wide_outputs(source: torch.Tensor) -> list[torch.Tensor]:
    """Return a copy of the source, and a copy with one more dimension in front."""
    return [source.clone(), source.unsqueeze(0).clone()]


@torch.library.custom_op("crucible_bounds::wide_list", mutates_args=())
def wide_list(sources: list[torch.Tensor]) -> torch.Tensor:
    """Return a copy of the first source.  The boxed fallback records each source as an input."""
    return sources[0].clone()


def overflow_list(last_shape: list[int]) -> list[torch.Tensor]:
    """Return INLINE_RECORDS tensors of rank 1, and then one tensor of last_shape.

    The CPU kernel of a fused operation calls the operation once for each
    tensor, and the vessel records each call.  The detector keys an operation
    by its schema and its shape, so tensor i has the extent i + 1.  Then no run
    of those calls repeats, and the only period is the full step.
    """
    return [torch.ones(extent) for extent in range(1, INLINE_RECORDS + 1)] + [torch.ones(15).view(last_shape)]


def fused_step(tensors: list[torch.Tensor]) -> Step:
    """Return one iteration of a fused operation over tensors."""
    return lambda: torch._foreach_sqrt(tensors)


def boxed_input_step(source: torch.Tensor) -> Step:
    """Return one iteration of crucible_bounds::wide_input over source."""
    return lambda: [torch.ops.crucible_bounds.wide_input(source)]


def list_output_step(source: torch.Tensor) -> Step:
    """Return one iteration of crucible_bounds::wide_outputs over source."""
    return lambda: torch.ops.crucible_bounds.wide_outputs(source)


def boxed_list_step(tensors: list[torch.Tensor]) -> Step:
    """Return one iteration of crucible_bounds::wide_list over tensors."""
    return lambda: [torch.ops.crucible_bounds.wide_list(tensors)]


class Recording:
    """The result of one recording: the operations of the region, when one forms, and the replay."""

    def __init__(self, ops: list[TraceOp] | None, diverged: int, results: list[torch.Tensor]) -> None:
        """Keep the operations of the region (None for no region), the divergences and the last results."""
        self.ops = ops
        self.diverged = diverged
        self.results = results


def record(step: Step, directory: Path, label: str, session: Callable[[], Any] = CrucibleNative) -> Recording:
    """Record step() under a session until a region is active, or for MAX_ITERATIONS iterations.

    The session is CrucibleNative, the dispatch library, or CrucibleMode, which
    gives each operation to the C entry.  The two have the same queries.
    """
    path = directory / f"{label}.crtrace"
    results: list[torch.Tensor] = []
    with session() as ctx:
        for _ in range(MAX_ITERATIONS):
            results = step()
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        is_active = ctx.active_num_ops() > 0
        # Two more iterations run against the active region.
        if is_active:
            for _ in range(2):
                results = step()
            ctx.flush()
        diverged = ctx.diverged_count()
        if is_active and not ctx.export_trace(str(path)):
            raise RuntimeError(f"{label}: the export of the active region failed")
    return Recording(read_trace(path) if is_active else None, diverged, results)


def ranks_of(op: TraceOp) -> list[int]:
    """Return the rank of each record of an operation."""
    return [meta[META_NDIM_OFFSET] for meta in op.metas]


def check_eager(label: str, recording: Recording, step: Step) -> list[str]:
    """Return one line for each result of the last iteration that differs from the result of eager PyTorch."""
    expected = step()
    if len(recording.results) != len(expected):
        return [f"{label}: the last iteration gives {len(recording.results)} results, not {len(expected)}"]
    problems = []
    for position, (result, wanted) in enumerate(zip(recording.results, expected)):
        is_same_view = result.shape == wanted.shape and result.stride() == wanted.stride()
        # A view of the size 2^60 is compared by its shape, so the check reads
        # no element of it.
        is_same = is_same_view and (result.numel() >= HUGE_EXTENT or torch.equal(result, wanted))
        if not is_same:
            problems.append(f"{label}: result {position} differs from the result of eager PyTorch")
    if recording.diverged != 0:
        problems.append(f"{label}: the replay counts {recording.diverged} divergences")
    return problems


def check_region_without(label: str, recording: Recording, names: tuple[str, ...]) -> list[str]:
    """Return lines when no region formed, or when the region holds an operation of names."""
    if recording.ops is None:
        return [f"{label}: no region after {MAX_ITERATIONS} iterations"]
    wanted = {schema_hash(name): name for name in names}
    return [f"{label}: the region holds {wanted[op.schema_hash]} with the records of the ranks {ranks_of(op)}, but "
            f"one of its tensors has no TensorMeta" for op in recording.ops if op.schema_hash in wanted]


def check_region_with(label: str, recording: Recording, name: str) -> list[str]:
    """Return one line when no region formed, or when the region holds no operation of name."""
    if recording.ops is None:
        return [f"{label}: no region after {MAX_ITERATIONS} iterations"]
    if all(op.schema_hash != schema_hash(name) for op in recording.ops):
        return [f"{label}: the region holds no {name}"]
    return []


def check_no_region(label: str, recording: Recording) -> list[str]:
    """Return one line when a region formed, so the Vigil recorded the operation with the wide tensor."""
    if recording.ops is not None:
        return [f"{label}: a region of {len(recording.ops)} operations formed, so the Vigil recorded the operation "
                f"of a tensor that a TensorMeta cannot describe"]
    return []


def check_two_shapes(label: str, recording: Recording) -> list[str]:
    """Return one line when the rank-8 pair does not give two aten::abs records with different bytes."""
    if recording.ops is None:
        return []
    wanted = schema_hash("aten::abs")
    inputs = {op.metas[0] for op in recording.ops if op.schema_hash == wanted and op.num_inputs == 1}
    if len(inputs) != 2:
        return [f"{label}: the region holds {len(inputs)} different input records of aten::abs, not 2"]
    shapes = sorted(struct.unpack_from(f"<{MAX_NDIM}q", meta, META_SIZES_OFFSET) for meta in inputs)
    expected = sorted(tuple(shape) for shape in NARROW_SHAPES)
    if shapes != expected:
        return [f"{label}: the records of aten::abs hold the sizes {shapes}, not {expected}"]
    return []


def report(problems: list[str]) -> list[str]:
    """Print each problem at once, so a later end of the process does not hide it, and return the problems."""
    for line in problems:
        print(f"  FAIL: {line}", flush=True)
    return problems


def mode_rank_checks(directory: Path) -> list[str]:
    """Run the checks of the ranks, of the size 2^60 and of the two shapes under CrucibleMode."""
    storage = torch.arange(15, dtype=torch.float32)
    wide = [storage.view(shape) for shape in WIDE_SHAPES]
    narrow = [storage.view(shape) for shape in NARROW_SHAPES]
    rank_eight = storage.view(RANK_EIGHT)
    seed = torch.zeros(1)

    def mode_step() -> list[torch.Tensor]:
        results = [torch.neg(view) for view in wide]
        results.append(rank_eight.unsqueeze(0))
        results.append(seed.expand(HUGE_EXTENT))
        results += [torch.abs(view) for view in narrow]
        return results

    recording = record(mode_step, directory, "mode rank", session=CrucibleMode)
    problems = report(check_region_without("mode rank", recording, ("aten::neg", "aten::unsqueeze", "aten::expand")))
    problems += report(check_two_shapes("mode two shapes", recording))
    problems += report(check_eager("mode rank", recording, mode_step))
    return problems


def rank_checks(directory: Path) -> list[str]:
    """Run the checks of the ranks and of the two shapes."""
    storage = torch.arange(15, dtype=torch.float32)
    wide = [storage.view(shape) for shape in WIDE_SHAPES]
    narrow = [storage.view(shape) for shape in NARROW_SHAPES]
    rank_eight = storage.view(RANK_EIGHT)

    def rank_step() -> list[torch.Tensor]:
        results = [torch.neg(view) for view in wide]
        results.append(rank_eight.unsqueeze(0))
        results.append(torch.ops.crucible_bounds.wide_output(rank_eight))
        results += [torch.abs(view) for view in narrow]
        return results

    recording = record(rank_step, directory, "rank")
    problems = report(check_region_without("rank", recording, (
        "aten::neg", "aten::unsqueeze", "crucible_bounds::wide_output")))
    problems += report(check_two_shapes("two shapes", recording))
    problems += report(check_eager("rank", recording, rank_step))

    # Each nest: its label, the operation, the shape of the wide tensor, the
    # shape of its twin, and the step of a shape.  The list output takes a
    # source of rank 8, and its second output has the rank 9.
    nests: list[tuple[str, str, list[int], list[int], Callable[[list[int]], Step]]] = [
        ("fused overflow", "aten::_foreach_sqrt", RANK_NINE, RANK_EIGHT,
         lambda shape: fused_step(overflow_list(shape))),
        ("boxed input", "crucible_bounds::wide_input", WIDE_SHAPES[0], RANK_EIGHT,
         lambda shape: boxed_input_step(storage.view(shape))),
        ("boxed list output", "crucible_bounds::wide_outputs", RANK_EIGHT, RANK_SEVEN,
         lambda shape: list_output_step(storage.view(shape))),
        ("boxed overflow", "crucible_bounds::wide_list", RANK_NINE, RANK_EIGHT,
         lambda shape: boxed_list_step(overflow_list(shape))),
    ]
    for label, name, wide_shape, twin_shape, make_step in nests:
        wide_step = make_step(wide_shape)
        twin_step = make_step(twin_shape)
        wide = record(wide_step, directory, f"{label} wide")
        problems += report(check_no_region(f"{label}, rank above 8", wide))
        problems += report(check_eager(f"{label}, rank above 8", wide, wide_step))
        twin = record(twin_step, directory, f"{label} twin")
        problems += report(check_region_with(f"{label}, the twin of rank 8 or less", twin, name))
        problems += report(check_eager(f"{label}, the twin of rank 8 or less", twin, twin_step))
    return problems


def extent_checks(directory: Path) -> list[str]:
    """Run the checks of the size 2^60 and of the stride 2^60."""
    seed = torch.zeros(1)
    huge = seed.expand(HUGE_EXTENT)
    marker = torch.ones(4)

    def extent_step() -> list[torch.Tensor]:
        return [seed.expand(HUGE_EXTENT), huge.narrow(0, 0, 4), seed.as_strided((1,), (HUGE_EXTENT,)), marker * 2]

    recording = record(extent_step, directory, "extent")
    problems = report(check_region_without("extent", recording, ("aten::expand", "aten::narrow", "aten::as_strided")))
    problems += report(check_eager("extent", recording, extent_step))

    def huge_step() -> list[torch.Tensor]:
        return [torch.ops.crucible_bounds.huge_input(huge)]

    def seed_step() -> list[torch.Tensor]:
        return [torch.ops.crucible_bounds.huge_input(seed)]

    wide = record(huge_step, directory, "boxed extent wide")
    problems += report(check_no_region("boxed input, size 2^60", wide))
    problems += report(check_eager("boxed input, size 2^60", wide, huge_step))
    twin = record(seed_step, directory, "boxed extent twin")
    problems += report(check_region_with("boxed input, size 1", twin, "crucible_bounds::huge_input"))
    problems += report(check_eager("boxed input, size 1", twin, seed_step))
    return problems


def main() -> int:
    """Run each check and report."""
    with tempfile.TemporaryDirectory(prefix="crucible-tensor-meta-bounds-") as scratch:
        directory = Path(scratch)
        problems = mode_rank_checks(directory)
        problems += rank_checks(directory)
        problems += extent_checks(directory)
    if problems:
        return 1
    print("test_tensor_meta_bounds: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
