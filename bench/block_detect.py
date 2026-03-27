#!/usr/bin/env python3
"""Detect semantic blocks in .crtrace files.

Coarsens 12K raw ATen ops into ~50 named blocks (ResBlock, Attention, MLP, etc.)
by pattern-matching on the schema_hash sequence and data-flow structure.

Four-phase algorithm:
  1. Phase detection: forward / backward / optimizer via grad_enabled transitions
  2. Forward block detection: residual-add boundaries + op family classification
  3. Backward block detection: mirror forward blocks using backward anchor ops
  4. Optimizer: single block (10 ops/parameter AdamW pattern)

Usage:
    python3 block_detect.py sd15_unet.crtrace
    python3 block_detect.py vit_b.crtrace
"""

import struct
import sys
from collections import defaultdict
from dataclasses import dataclass, field

from aten_hash_table import ATEN_HASH_TABLE

# ── Op family classification ───────────────────────────────────────────

FAMILY_GEMM = "gemm"
FAMILY_CONV = "conv"
FAMILY_NORM = "norm"
FAMILY_ATTN = "attn"
FAMILY_ACT  = "act"
FAMILY_ELEM = "elem"
FAMILY_MOVE = "move"
FAMILY_REDUCE = "reduce"
FAMILY_OPTIM = "optim"
FAMILY_LOSS = "loss"
FAMILY_EMBED = "embed"
FAMILY_POOL = "pool"
FAMILY_OTHER = "other"


def classify_family(name: str) -> str:
    if not name:
        return FAMILY_OTHER
    n = name.lower()
    # Order matters: most specific patterns first to avoid false matches
    if any(k in n for k in ["attention", "sdp"]):
        return FAMILY_ATTN
    if any(k in n for k in ["loss", "nll", "cross_entropy", "mse_loss"]):
        return FAMILY_LOSS
    if any(k in n for k in ["foreach", "fused_adam", "fused_sgd",
                             "addcmul", "addcdiv", "lerp"]):
        return FAMILY_OPTIM
    if any(k in n for k in ["mm", "matmul", "linear", "einsum"]):
        return FAMILY_GEMM
    if "conv" in n:
        return FAMILY_CONV
    if "norm" in n:
        return FAMILY_NORM
    if any(k in n for k in ["gelu", "silu", "relu", "sigmoid", "tanh",
                             "softmax", "dropout", "mish"]):
        return FAMILY_ACT
    if any(k in n for k in ["sum", "mean", "var_mean",
                             "argmax", "topk", "cumsum"]):
        return FAMILY_REDUCE
    if any(k in n for k in ["add", "mul", "sub", "div", "neg", "exp",
                             "log", "sqrt", "rsqrt", "pow", "abs",
                             "clamp", "where", "fill", "zero",
                             "masked", "sin", "cos"]):
        return FAMILY_ELEM
    if any(k in n for k in ["view", "reshape", "permute", "transpose",
                             "expand", "squeeze", "unsqueeze", "contiguous",
                             "slice", "select", "cat", "stack", "clone",
                             "detach", "narrow", "flatten", "unfold",
                             "unsafe_view", "as_strided", "index", "gather",
                             "scatter", "alias", "copy", "pad", "split",
                             "chunk", "unbind", "repeat", "to_copy",
                             "arange", "zeros", "ones", "empty", "full",
                             "new_zeros", "lift_fresh"]):
        return FAMILY_MOVE
    if "embedding" in n:
        return FAMILY_EMBED
    if any(k in n for k in ["upsample", "interpolate", "pool"]):
        return FAMILY_POOL
    return FAMILY_OTHER


# ── Data structures ────────────────────────────────────────────────────

@dataclass
class Op:
    idx: int
    schema: int
    name: str           # e.g. "addmm", "convolution"
    family: str         # FAMILY_* constant
    n_in: int
    n_out: int
    grad: int           # grad_enabled flag from trace
    out_shape: tuple    # first output tensor shape
    data_ptr_in: list   # input data_ptr values
    data_ptr_out: list  # output data_ptr values


@dataclass
class Block:
    kind: str           # "resblock", "self_attn", "cross_attn", "mlp", "conv",
                        # "time_embed", "downsample", "upsample", "optimizer",
                        # "head", "loss", "generic",
                        # backward variants: "resblock_bwd", "self_attn_bwd", etc.
    start: int          # first op index
    end: int            # last op index (inclusive)
    ops: list           # Op objects in this block
    label: str = ""     # human-readable label
    out_shape: str = "" # representative output shape
    phase: str = ""     # "forward", "backward", "optimizer", or ""
    fwd_pair: int = -1  # index of corresponding forward block (for backward blocks)


# ── Trace parser ───────────────────────────────────────────────────────

def load_ops(path: str) -> list[Op]:
    with open(path, "rb") as f:
        data = f.read()

    _, _, num_ops, num_metas = struct.unpack_from("<4sIII", data, 0)

    # Parse ops (80B each): schema_hash at +0, grad_enabled at +78
    ops_raw = []
    off = 16
    for i in range(num_ops):
        sh = struct.unpack_from("<Q", data, off)[0]
        n_in, n_out = struct.unpack_from("<HH", data, off + 72)[:2]
        grad = struct.unpack_from("<B", data, off + 78)[0]
        ops_raw.append((i, sh, n_in, n_out, grad))
        off += 80

    # Parse metas (144B each): sizes[8] at +0, data_ptr at +128, ndim at +136
    metas = []
    for i in range(num_metas):
        sizes = struct.unpack_from("<8q", data, off)
        ptr = struct.unpack_from("<Q", data, off + 128)[0]
        ndim = struct.unpack_from("<B", data, off + 136)[0]
        shape = tuple(sizes[:min(ndim, 8)])
        metas.append((ptr, shape))
        off += 144

    # Build Op objects
    ops = []
    meta_cursor = 0
    for idx, sh, n_in, n_out, grad in ops_raw:
        full_name = ATEN_HASH_TABLE.get(sh, f"0x{sh:016x}")
        short = full_name.replace("aten::", "").split(".")[0]
        family = classify_family(full_name)

        ptrs_in, ptrs_out = [], []
        out_shape = ()
        for j in range(n_in):
            if meta_cursor + j < len(metas):
                ptrs_in.append(metas[meta_cursor + j][0])
        for j in range(n_out):
            mi = meta_cursor + n_in + j
            if mi < len(metas):
                ptrs_out.append(metas[mi][0])
                if j == 0:
                    out_shape = metas[mi][1]
        meta_cursor += n_in + n_out

        ops.append(Op(
            idx=idx, schema=sh, name=short, family=family,
            n_in=n_in, n_out=n_out, grad=grad,
            out_shape=out_shape,
            data_ptr_in=ptrs_in, data_ptr_out=ptrs_out,
        ))

    return ops


# ── Phase detection ───────────────────────────────────────────────────

def detect_phases(ops: list[Op]) -> tuple[int, int, int]:
    """Find forward/backward/optimizer phase boundaries.

    Returns (fwd_end, bwd_end, optim_start):
      - Forward:  ops[0 .. fwd_end]        grad_enabled=1 until loss
      - Backward: ops[fwd_end+1 .. bwd_end]  grad_enabled=0, contains *_backward ops
      - Optimizer: ops[optim_start .. end]  repeating 10-op AdamW pattern

    If any boundary cannot be found, returns len(ops) for that position.
    """
    n = len(ops)

    # Strategy 1: Find the first *_backward op (loss_backward marks the turn)
    # The forward/backward boundary is the op just before it.
    first_bwd = n
    for i, op in enumerate(ops):
        if "backward" in op.name:
            first_bwd = i
            break

    # The loss op is the last forward op before the backward starts
    fwd_end = max(0, first_bwd - 1)

    # Strategy 2: Find optimizer start via the repeating AdamW pattern.
    # AdamW per-parameter = 10 ops: add_ mul_ lerp_ mul_ addcmul_
    #                                _local_scalar_dense sqrt div add_ addcdiv_
    # Detect by finding the first 'profiler::_record_function_enter_new' after
    # backward (grad goes 1 briefly then 0), OR the first run of addcdiv_.
    # Most reliable: find where grad_enabled goes from 0->1 after backward.
    optim_start = n
    bwd_end = n

    # Look for grad=0->1 transition after fwd_end (profiler enter op)
    in_backward = False
    for i in range(first_bwd, n):
        if ops[i].grad == 0:
            in_backward = True
        elif in_backward and ops[i].grad == 1 and ops[i].name.startswith("profiler"):
            # This is the profiler hook before optimizer step
            optim_start = i
            bwd_end = i - 1
            break

    # If no profiler transition found, try to find optimizer by pattern:
    # Look for a run of addcdiv_ ops (unique to AdamW)
    if optim_start == n:
        addcdiv_run = 0
        for i in range(first_bwd, n):
            if ops[i].name == "addcdiv_":
                addcdiv_run += 1
                if addcdiv_run >= 3:
                    # Rewind to find the start of this optimizer section
                    # Each parameter is 10 ops; the first op is add_
                    j = i
                    while j > first_bwd and addcdiv_run < 100:
                        j -= 1
                        if ops[j].name == "addcdiv_":
                            addcdiv_run += 1
                    # Find the actual start (first add_ before first addcmul_)
                    while j > first_bwd and ops[j].name != "add_":
                        j -= 1
                    optim_start = j
                    bwd_end = j - 1
                    break
            else:
                addcdiv_run = 0

    return fwd_end, bwd_end, optim_start


# ── Forward block detection ──────────────────────────────────────────

def detect_forward_blocks(ops: list[Op], fwd_end: int) -> list[Block]:
    """Detect blocks in the forward pass using residual-add boundaries."""
    fwd_ops = ops[:fwd_end + 1]
    if not fwd_ops:
        return []

    # Build ptr -> producer map
    ptr_producer = {}
    for op in fwd_ops:
        for ptr in op.data_ptr_out:
            if ptr:
                ptr_producer[ptr] = op.idx

    # Find residual-add boundaries: add.Tensor where one input comes from
    # far earlier (skip connection gap > threshold)
    SKIP_GAP = 8
    block_ends = []

    for op in fwd_ops:
        if op.name in ("add", "add_") and op.family == FAMILY_ELEM:
            for ptr in op.data_ptr_in:
                if ptr and ptr in ptr_producer:
                    src = ptr_producer[ptr]
                    if op.idx - src > SKIP_GAP:
                        block_ends.append(op.idx)
                        break

    # If no residual adds found, fall back to fixed-size chunks
    if not block_ends:
        chunk = max(20, len(fwd_ops) // 30)
        block_ends = list(range(chunk - 1, len(fwd_ops), chunk))

    # Split into blocks
    block_ends_set = set(block_ends)
    blocks = []
    block_start = 0

    for i, op in enumerate(fwd_ops):
        if i in block_ends_set or i == len(fwd_ops) - 1:
            block_ops = fwd_ops[block_start:i + 1]
            if block_ops:
                block = classify_block(block_ops, ptr_producer, "forward")
                blocks.append(block)
            block_start = i + 1

    if block_start < len(fwd_ops):
        remaining = fwd_ops[block_start:]
        if remaining:
            block = classify_block(remaining, ptr_producer, "forward")
            blocks.append(block)

    return blocks


# ── Backward block detection ─────────────────────────────────────────

def detect_backward_blocks(ops: list[Op], fwd_end: int, bwd_end: int,
                           fwd_blocks: list[Block]) -> list[Block]:
    """Detect blocks in the backward pass using forward-mirror strategy.

    PyTorch autograd generates backward ops in REVERSE order of the forward
    pass. We exploit this by:

    1. Finding the loss/head backward preamble (before first structural anchor)
    2. Counting forward "transformer blocks" (each SelfAttn+MLP pair = 1 block)
       or "resblocks" to know expected backward block count
    3. Using the SDPA backward / group_norm_backward ops as definitive anchors
       that delimit backward blocks (each transformer/resblock backward has
       exactly one SDPA backward or specific anchor pattern)
    4. Splitting the remaining tail (conv backward, etc.)
    """
    bwd_start = fwd_end + 1
    bwd_ops = ops[bwd_start:bwd_end + 1]
    if not bwd_ops:
        return []

    n_bwd = len(bwd_ops)

    # Build ptr -> producer for data-flow analysis
    ptr_producer = {}
    for op in ops[:bwd_end + 1]:
        for ptr in op.data_ptr_out:
            if ptr:
                ptr_producer[ptr] = op.idx

    # Classify the forward to understand what the backward should mirror.
    # Count transformer blocks (SelfAttn or CrossAttn) in forward.
    fwd_attn_count = sum(1 for b in fwd_blocks
                         if b.kind in ("self_attn", "cross_attn"))
    fwd_resblock_count = sum(1 for b in fwd_blocks
                             if b.kind == "resblock")

    # Find SDPA backward ops — definitive transformer block boundaries
    sdpa_bwd_indices = [i for i, op in enumerate(bwd_ops)
                        if "scaled_dot_product" in op.name and "backward" in op.name]

    # Find group_norm_backward ops — definitive ResBlock boundaries
    gnorm_bwd_indices = [i for i, op in enumerate(bwd_ops)
                         if op.name == "native_group_norm_backward"]

    # Strategy: use the dominant backward anchor type as block delimiter.
    # For ViT-like: SDPA backward ops split transformer blocks
    # For UNet-like: combination of SDPA + group_norm backward ops

    if sdpa_bwd_indices and len(sdpa_bwd_indices) >= 3:
        blocks = _split_backward_by_sdpa(bwd_ops, bwd_start, sdpa_bwd_indices,
                                         gnorm_bwd_indices, ptr_producer,
                                         fwd_blocks)
    else:
        # Fallback: use add-based boundaries
        bwd_add_boundaries = []
        for op in bwd_ops:
            if op.name == "add" and op.family == FAMILY_ELEM:
                for ptr in op.data_ptr_in:
                    if ptr and ptr in ptr_producer:
                        src = ptr_producer[ptr]
                        if op.idx - src > 8:
                            bwd_add_boundaries.append(op.idx - bwd_start)
                            break
        if bwd_add_boundaries:
            blocks = _split_backward_by_adds(bwd_ops, bwd_start,
                                             bwd_add_boundaries, ptr_producer)
        else:
            # Final fallback: single block
            blocks = [classify_block(bwd_ops, ptr_producer, "backward")]

    return blocks


def _split_backward_by_sdpa(bwd_ops: list[Op], bwd_start: int,
                            sdpa_indices: list[int],
                            gnorm_indices: list[int],
                            ptr_producer: dict,
                            fwd_blocks: list[Block]) -> list[Block]:
    """Split backward using structural norm_backward boundaries.

    The backward of each transformer block follows this pattern:
        [norm_bwd(MLP), MLP_grads, norm_bwd(attn), add(residual), SDPA_bwd, proj_grads, add(residual)]

    norm_backward ops come in pairs: first for MLP norm, second for attn norm.
    Every other norm_backward (the one starting a new block) is preceded by an
    'add' op -- the residual gradient accumulation from the previous block.
    This 'add' marks the true block boundary.

    For UNet: additional group_norm_backward ops mark ResBlock sections.
    """
    blocks = []
    n = len(bwd_ops)

    # Find ALL norm_backward ops (both layer_norm and group_norm)
    lnorm_bwd = [i for i, op in enumerate(bwd_ops)
                 if op.name == "native_layer_norm_backward"]
    gnorm_bwd = [i for i, op in enumerate(bwd_ops)
                 if op.name == "native_group_norm_backward"]

    # Find the preamble: everything before the first structural backward.
    first_structural = n
    for i, op in enumerate(bwd_ops):
        if op.name in ("native_layer_norm_backward",
                        "native_group_norm_backward",
                        "_scaled_dot_product_flash_attention_for_cpu_backward"):
            first_structural = i
            break

    # Emit preamble (loss backward / head backward)
    if first_structural > 0:
        preamble = bwd_ops[:first_structural]
        block = classify_block(preamble, ptr_producer, "backward")
        blocks.append(block)

    # Strategy: find block boundaries using the 'add' ops that precede
    # norm_backward ops. These are the residual gradient accumulations.
    #
    # In ViT backward: norm_bwd indices come in pairs [15,43], [107,136], ...
    # The block-starting norm_bwd (at 107, 200, 293, ...) is preceded by 'add'
    # at 106, 199, 292, ... These are the boundaries.
    #
    # In UNet backward: similar but with group_norm_backward between sections.

    # Collect ALL structural anchor positions (both norm types)
    all_norm_bwd = sorted(lnorm_bwd + gnorm_bwd)

    # Find block boundaries: an 'add' that immediately FOLLOWS a norm_backward
    # marks the end of a backward block (residual gradient accumulation).
    #
    # Pattern in ViT backward:
    #   ... [proj grads] add(residual) native_layer_norm_backward [next block]
    #   So norm_backward PRECEDED by add = block start
    #
    # Pattern in UNet backward:
    #   ... native_group_norm_backward add(residual) [next section starts]
    #   So norm_backward FOLLOWED by add = block end
    #
    # We handle BOTH patterns: look for 'add' adjacent to norm_backward.

    block_starts = [first_structural]

    for idx in all_norm_bwd:
        if idx <= first_structural:
            continue

        # Pattern A (ViT): norm_backward preceded by 'add' -> block starts at norm
        if idx > 0 and bwd_ops[idx - 1].name == "add":
            block_starts.append(idx)
        elif idx > 1 and bwd_ops[idx - 2].name == "add":
            block_starts.append(idx)

        # Pattern B (UNet): norm_backward followed by 'add' -> block starts AFTER add
        if idx + 1 < n and bwd_ops[idx + 1].name == "add":
            # The next block starts at idx+2
            if idx + 2 < n:
                block_starts.append(idx + 2)

    # Deduplicate and sort
    block_starts = sorted(set(block_starts))

    # Remove block starts that are too close together (< 10 ops apart)
    # to avoid splitting into tiny blocks
    filtered_starts = [block_starts[0]]
    for i in range(1, len(block_starts)):
        if block_starts[i] - filtered_starts[-1] >= 10:
            filtered_starts.append(block_starts[i])
    block_starts = filtered_starts

    # Split into blocks using these starts
    for i, start in enumerate(block_starts):
        end = block_starts[i + 1] - 1 if i + 1 < len(block_starts) else n - 1
        block_ops = bwd_ops[start:end + 1]
        if block_ops:
            block = classify_block(block_ops, ptr_producer, "backward")
            blocks.append(block)

    return blocks


def _split_backward_by_adds(bwd_ops: list[Op], bwd_start: int,
                            add_boundaries: list[int],
                            ptr_producer: dict) -> list[Block]:
    """Split backward using residual-add boundaries (same logic as forward)."""
    blocks = []
    boundary_set = set(add_boundaries)
    block_start = 0

    for i in range(len(bwd_ops)):
        if i in boundary_set or i == len(bwd_ops) - 1:
            block_ops = bwd_ops[block_start:i + 1]
            if block_ops:
                block = classify_block(block_ops, ptr_producer, "backward")
                blocks.append(block)
            block_start = i + 1

    if block_start < len(bwd_ops):
        remaining = bwd_ops[block_start:]
        if remaining:
            block = classify_block(remaining, ptr_producer, "backward")
            blocks.append(block)

    return blocks


# ── Block classification ─────────────────────────────────────────────

def classify_block(ops: list[Op], ptr_producer: dict, phase: str) -> Block:
    """Classify a group of ops into a named block type."""
    families = defaultdict(int)
    for op in ops:
        families[op.family] += 1

    n_conv = families.get(FAMILY_CONV, 0)
    n_gemm = families.get(FAMILY_GEMM, 0)
    n_norm = families.get(FAMILY_NORM, 0)
    n_attn = families.get(FAMILY_ATTN, 0)
    n_act  = families.get(FAMILY_ACT, 0)
    n_optim = families.get(FAMILY_OPTIM, 0)
    n_loss = families.get(FAMILY_LOSS, 0)

    # Detect specific op names (works for both forward and backward variants)
    has_sdpa = any("attention" in op.name for op in ops)
    has_gelu = any(op.name in ("gelu", "gelu_backward") for op in ops)
    has_silu = any(op.name in ("silu", "silu_", "silu_backward") for op in ops)
    has_conv = n_conv > 0
    has_group_norm = any("group_norm" in op.name for op in ops)
    has_layer_norm = any("layer_norm" in op.name for op in ops)
    is_backward = any("backward" in op.name for op in ops)
    has_upsample = any("upsample" in op.name for op in ops)

    # Check for cross-attention: mm ops with inputs from outside the block.
    # Only meaningful in forward pass -- backward mm ops always consume
    # saved forward tensors that appear to come from outside.
    has_cross_input = False
    if phase == "forward":
        block_idx_set = {op.idx for op in ops}
        for op in ops:
            if op.name == "mm" and op.family == FAMILY_GEMM:
                for ptr in op.data_ptr_in:
                    if ptr and ptr in ptr_producer:
                        src = ptr_producer[ptr]
                        if src not in block_idx_set:
                            has_cross_input = True
                            break

    # Output shape from last significant op
    out_shape = ""
    for op in reversed(ops):
        if op.out_shape and op.family not in (FAMILY_MOVE, FAMILY_OTHER):
            out_shape = "x".join(str(s) for s in op.out_shape)
            break
    if not out_shape and ops[-1].out_shape:
        out_shape = "x".join(str(s) for s in ops[-1].out_shape)

    start, end = ops[0].idx, ops[-1].idx
    suffix = " BWD" if phase == "backward" and is_backward else ""

    # Classification rules
    if n_optim > len(ops) // 4:  # relaxed threshold (was //2)
        kind, label = "optimizer", "Optimizer"
    elif n_loss > 0 and not is_backward:
        kind, label = "loss", "Loss"
    elif n_loss > 0 and is_backward:
        kind, label = "loss_bwd", "Loss BWD"
    elif has_sdpa and has_layer_norm and has_cross_input:
        kind = "cross_attn_bwd" if is_backward else "cross_attn"
        label = "CrossAttn" + suffix
    elif has_sdpa and has_gelu and has_layer_norm:
        # Transformer block (attention + MLP merged in one block)
        kind = "transformer_bwd" if is_backward else "transformer"
        label = "Transformer" + suffix
    elif has_sdpa and has_layer_norm:
        kind = "self_attn_bwd" if is_backward else "self_attn"
        label = "SelfAttn" + suffix
    elif has_conv and has_group_norm and has_silu:
        kind = "resblock_bwd" if is_backward else "resblock"
        label = "ResBlock" + suffix
    elif has_gelu and n_gemm >= 2 and has_layer_norm:
        kind = "mlp_bwd" if is_backward else "mlp"
        label = "MLP" + suffix
        if any(op.name == "split" for op in ops):
            label = "GEGLU MLP" + suffix
    elif has_gelu and has_layer_norm:
        kind = "mlp_bwd" if is_backward else "mlp"
        label = "MLP" + suffix
    elif has_conv and len(ops) <= 6 and not has_group_norm:
        if has_upsample:
            kind = "upsample_bwd" if is_backward else "upsample"
            label = "Upsample" + suffix
        else:
            kind = "downsample_bwd" if is_backward else "downsample"
            label = "Downsample" + suffix
    elif has_conv and has_group_norm:
        kind = "conv_block_bwd" if is_backward else "conv_block"
        label = "ConvBlock" + suffix
    elif any(op.name in ("sin", "cos", "arange", "exp") for op in ops) and n_gemm >= 1:
        kind = "time_embed_bwd" if is_backward else "time_embed"
        label = "TimeEmbed" + suffix
    elif has_layer_norm and n_gemm >= 1 and not has_sdpa:
        kind = "linear_block_bwd" if is_backward else "linear_block"
        label = "Linear" + suffix
    elif has_conv and n_conv == 1:
        kind = "conv_bwd" if is_backward else "conv"
        label = "Conv" + suffix
    elif has_sdpa and not has_layer_norm:
        # Attention without norm (backward often strips norms into separate block)
        kind = "attn_bwd" if is_backward else "attn"
        label = "Attn" + suffix
    elif has_gelu and not has_layer_norm:
        kind = "mlp_bwd" if is_backward else "mlp"
        label = "MLP" + suffix
    elif has_silu and has_conv:
        kind = "resblock_bwd" if is_backward else "resblock"
        label = "ResBlock" + suffix
    else:
        dom = max(families, key=families.get) if families else FAMILY_OTHER
        kind, label = "generic", dom.upper()
        if is_backward:
            kind += "_bwd"
            label += " BWD"

    if out_shape:
        label = f"{label} [{out_shape}]"

    return Block(
        kind=kind, start=start, end=end, ops=ops,
        label=label, out_shape=out_shape, phase=phase,
    )


# ── Top-level detection ──────────────────────────────────────────────

def detect_blocks(ops: list[Op]) -> list[Block]:
    """Three-phase block detection: forward, backward, optimizer."""
    n = len(ops)
    if n == 0:
        return []

    # Phase 1: Detect boundaries
    fwd_end, bwd_end, optim_start = detect_phases(ops)

    # Phase 2: Forward blocks
    fwd_blocks = detect_forward_blocks(ops, fwd_end)

    # Phase 3: Backward blocks
    bwd_blocks = detect_backward_blocks(ops, fwd_end, bwd_end, fwd_blocks)

    # Phase 4: Optimizer block
    optim_blocks = []
    if optim_start < n:
        optim_ops = ops[optim_start:]
        # Filter out trailing profiler/scalar ops that aren't part of optimizer
        last_addcdiv = optim_start
        for op in optim_ops:
            if op.name == "addcdiv_":
                last_addcdiv = op.idx
        optim_end = last_addcdiv

        # Count parameters
        n_params = sum(1 for op in optim_ops if op.name == "addcdiv_"
                       and op.idx <= optim_end)
        core_ops = [op for op in optim_ops if op.idx <= optim_end]
        if core_ops:
            n_core = len(core_ops)
            label = f"Optimizer ({n_params} params, {n_core} ops)"
            optim_blocks.append(Block(
                kind="optimizer", start=core_ops[0].idx, end=core_ops[-1].idx,
                ops=core_ops, label=label, phase="optimizer",
            ))

        # Trailing ops after optimizer (profiler cleanup, scalar dense, etc.)
        trailing = [op for op in optim_ops if op.idx > optim_end]
        if trailing:
            optim_blocks.append(Block(
                kind="epilogue", start=trailing[0].idx, end=trailing[-1].idx,
                ops=trailing, label=f"Epilogue ({len(trailing)} ops)",
                phase="optimizer",
            ))

    return fwd_blocks + bwd_blocks + optim_blocks


# ── 2D Layout ─────────────────────────────────────────────────────────

@dataclass
class LayoutNode:
    block: Block
    x: float = 0.0     # column position
    y: float = 0.0     # row position
    col: int = 0        # column index (0=left/encoder, 1=mid, 2=right/decoder)


def get_spatial_res(block: Block) -> tuple[int, int] | None:
    """Extract (H, W) spatial resolution from block's output tensors."""
    for op in reversed(block.ops):
        if op.out_shape and len(op.out_shape) == 4:
            return (op.out_shape[2], op.out_shape[3])
    return None


def layout_blocks(blocks: list[Block]) -> list[LayoutNode]:
    """Position blocks in 2D for visualization.

    UNet: U-shape with encoder left, decoder right, skip connections.
    ViT: Vertical stack.
    Generic: Vertical list.
    """
    if not blocks:
        return []

    # Separate by phase
    fwd = [b for b in blocks if b.phase == "forward"]
    bwd = [b for b in blocks if b.phase == "backward"]
    opt = [b for b in blocks if b.phase == "optimizer"]

    # Detect architecture type from forward blocks
    arch = detect_architecture(fwd)

    if arch == "unet":
        return layout_unet(fwd, bwd, opt)
    elif arch == "vit":
        return layout_vit(fwd, bwd, opt)
    else:
        return layout_generic(fwd, bwd, opt)


def detect_architecture(fwd_blocks: list[Block]) -> str:
    """Detect architecture type from forward block pattern."""
    resolutions = []
    for b in fwd_blocks:
        r = get_spatial_res(b)
        if r:
            resolutions.append(r[0])  # H dimension

    if not resolutions:
        return "generic"

    # UNet: resolutions go down then up
    if len(resolutions) >= 5:
        min_res = min(resolutions)
        min_idx = resolutions.index(min_res)
        max_res = max(resolutions)
        if min_idx > 0 and min_idx < len(resolutions) - 1:
            # Check rough U-shape
            before_min = resolutions[:min_idx + 1]
            after_min = resolutions[min_idx:]
            if max(before_min) >= 2 * min_res and max(after_min) >= 2 * min_res:
                return "unet"

    # ViT: all blocks have same resolution (or all non-4D)
    if resolutions:
        if len(set(resolutions)) <= 2:  # uniform or nearly
            return "vit"

    return "generic"


def layout_unet(fwd: list[Block], bwd: list[Block],
                opt: list[Block]) -> list[LayoutNode]:
    """U-shape layout: encoder left, mid center, decoder right."""
    nodes = []

    # Detect encoder/mid/decoder from resolution changes
    resolutions = [(i, get_spatial_res(b)) for i, b in enumerate(fwd)]

    # Find minimum resolution point (bottleneck)
    res_vals = [(i, r[0]) for i, r in resolutions if r is not None]
    if not res_vals:
        return layout_generic(fwd, bwd, opt)

    min_res = min(r for _, r in res_vals)
    # Find the range of blocks at minimum resolution
    mid_start = None
    mid_end = None
    for i, r in res_vals:
        if r == min_res:
            if mid_start is None:
                mid_start = i
            mid_end = i

    # Encoder: blocks before mid, Decoder: blocks after mid
    encoder = fwd[:mid_start] if mid_start else []
    mid = fwd[mid_start:mid_end + 1] if mid_start is not None else []
    decoder = fwd[mid_end + 1:] if mid_end is not None else []

    # Layout: encoder in left column (y going down),
    # mid at bottom center, decoder in right column (y going up)
    y = 0
    for b in encoder:
        nodes.append(LayoutNode(block=b, x=0, y=y, col=0))
        y += 1
    for b in mid:
        nodes.append(LayoutNode(block=b, x=1, y=y, col=1))
        y += 1
    y_dec = y
    for b in decoder:
        nodes.append(LayoutNode(block=b, x=2, y=y_dec, col=2))
        y_dec -= 1

    # Backward and optimizer at bottom
    y_bwd = y + 1
    for b in bwd:
        nodes.append(LayoutNode(block=b, x=0, y=y_bwd, col=0))
        y_bwd += 1
    for b in opt:
        nodes.append(LayoutNode(block=b, x=0, y=y_bwd, col=0))
        y_bwd += 1

    return nodes


def layout_vit(fwd: list[Block], bwd: list[Block],
               opt: list[Block]) -> list[LayoutNode]:
    """Vertical stack layout for ViT."""
    nodes = []
    y = 0
    for b in fwd:
        nodes.append(LayoutNode(block=b, x=0, y=y, col=0))
        y += 1
    for b in bwd:
        nodes.append(LayoutNode(block=b, x=0, y=y, col=0))
        y += 1
    for b in opt:
        nodes.append(LayoutNode(block=b, x=0, y=y, col=0))
        y += 1
    return nodes


def layout_generic(fwd: list[Block], bwd: list[Block],
                   opt: list[Block]) -> list[LayoutNode]:
    """Simple vertical layout."""
    nodes = []
    y = 0
    for b in fwd + bwd + opt:
        nodes.append(LayoutNode(block=b, x=0, y=y, col=0))
        y += 1
    return nodes


# ── Text output ────────────────────────────────────────────────────────

def print_block_tree(blocks: list[Block], ops: list[Op]):
    """Print block structure as a text tree with phase headers."""
    total_ops = sum(len(b.ops) for b in blocks)
    print(f"\n{'='*72}")
    print(f" Block Detection: {len(blocks)} blocks from {total_ops} ops")
    print(f"{'='*72}")

    current_phase = None
    current_res = None
    stage_num = 0

    phase_headers = {
        "forward": "FORWARD PASS",
        "backward": "BACKWARD PASS",
        "optimizer": "OPTIMIZER",
    }

    for i, block in enumerate(blocks):
        # Phase transition header
        if block.phase != current_phase:
            current_phase = block.phase
            header = phase_headers.get(current_phase, current_phase.upper())
            phase_ops = sum(len(b.ops) for b in blocks if b.phase == current_phase)
            print(f"\n  {'='*66}")
            print(f"  {header} ({phase_ops} ops)")
            print(f"  {'='*66}")
            current_res = None
            stage_num = 0

        # Resolution-based stage headers (forward and backward)
        if current_phase in ("forward", "backward"):
            res = get_spatial_res(block)
            if res is not None:
                res_tuple = (res[0], res[1])
                if res_tuple != current_res:
                    current_res = res_tuple
                    stage_num += 1
                    print(f"\n  {'~'*62}")
                    print(f"  Stage {stage_num}: {res[0]}x{res[1]}")
                    print(f"  {'~'*62}")

        # Block summary line
        n_ops = len(block.ops)
        op_range = f"[{block.start:5d}..{block.end:5d}]"

        # Icon
        base_kind = block.kind.replace("_bwd", "")
        icon = {
            "resblock": "\u2588", "self_attn": "\u25c6", "cross_attn": "\u25c7",
            "mlp": "\u25ac", "time_embed": "\u23f1", "downsample": "\u25bc",
            "upsample": "\u25b2", "conv": "\u25a0", "conv_block": "\u25a0",
            "optimizer": "\u2699", "loss": "\u2717", "loss_bwd": "\u2717",
            "linear_block": "\u2500", "generic": "\u00b7", "head": "\u25cb",
            "transformer": "\u25a3", "attn": "\u25c6", "epilogue": "\u00b7",
        }.get(base_kind, "?")

        print(f"\n  {icon} {block.label:36s} {op_range} ({n_ops:4d} ops)")

        # Key ops
        key_ops = []
        for op in block.ops:
            if op.family in (FAMILY_CONV, FAMILY_ATTN, FAMILY_GEMM,
                            FAMILY_NORM, FAMILY_ACT, FAMILY_LOSS):
                key_ops.append(op.name)
        key_str = " \u2192 ".join(dict.fromkeys(key_ops))
        if key_str:
            print(f"    {key_str}")

    # Summary
    print(f"\n{'='*72}")
    print(" Summary by phase:")

    for phase in ("forward", "backward", "optimizer"):
        phase_blocks = [b for b in blocks if b.phase == phase]
        if not phase_blocks:
            continue
        phase_ops = sum(len(b.ops) for b in phase_blocks)
        kind_counts = defaultdict(int)
        for b in phase_blocks:
            kind_counts[b.kind] += 1
        kinds_str = " | ".join(f"{k}={v}" for k, v in
                               sorted(kind_counts.items(), key=lambda x: -x[1]))
        print(f"   {phase:10s}: {len(phase_blocks):3d} blocks, {phase_ops:5d} ops -- {kinds_str}")

    # Architecture detection
    fwd_blocks = [b for b in blocks if b.phase == "forward"]
    arch = detect_architecture(fwd_blocks)
    if arch != "generic":
        print(f"\n Architecture: {arch.upper()}")
        if arch == "unet":
            resolutions = []
            for b in fwd_blocks:
                r = get_spatial_res(b)
                if r:
                    resolutions.append(f"{r[0]}x{r[1]}")
            if resolutions:
                # Deduplicate consecutive
                deduped = [resolutions[0]]
                for r in resolutions[1:]:
                    if r != deduped[-1]:
                        deduped.append(r)
                print(f"   Resolution path: {' -> '.join(deduped)}")

    print(f"{'='*72}\n")


# ── Main ───────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <trace.crtrace>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    print(f"Loading {path}...", file=sys.stderr)
    ops = load_ops(path)
    print(f"  {len(ops)} ops loaded", file=sys.stderr)

    print("Detecting phases...", file=sys.stderr)
    fwd_end, bwd_end, optim_start = detect_phases(ops)
    print(f"  Forward:  ops[0..{fwd_end}] ({fwd_end + 1} ops)", file=sys.stderr)
    print(f"  Backward: ops[{fwd_end + 1}..{bwd_end}] ({bwd_end - fwd_end} ops)", file=sys.stderr)
    print(f"  Optimizer: ops[{optim_start}..{len(ops) - 1}] ({len(ops) - optim_start} ops)", file=sys.stderr)

    print("Detecting blocks...", file=sys.stderr)
    blocks = detect_blocks(ops)
    print(f"  {len(blocks)} blocks detected", file=sys.stderr)

    print_block_tree(blocks, ops)


if __name__ == "__main__":
    main()
