#!/usr/bin/env python3
"""Render a .crtrace binary as a dataflow graph via Graphviz.

Usage:
    python3 render_trace.py vit_b.crtrace -o vit_b.pdf
    python3 render_trace.py sd15_unet.crtrace -o sd15_unet.svg --max-ops 300
    python3 render_trace.py vit_b.crtrace --dot  # print DOT to stdout
"""

import argparse
import struct
import sys
from collections import defaultdict
from pathlib import Path

# ── ATen op hash table (FNV-1a hash → op name) ────────────────────────
# Generated from torch.ops.aten with all overloads (2886 ops).

from aten_hash_table import ATEN_HASH_TABLE as BUILTIN_HASH_TABLE

# ── Op family classification for coloring ──────────────────────────────

def classify_op(name: str) -> str:
    if not name:
        return "unknown"
    n = name.lower()
    if "mm" in n or "matmul" in n or "linear" in n or "gemm" in n or "einsum" in n:
        return "matmul"
    if "conv" in n:
        return "conv"
    if "attention" in n or "sdp" in n:
        return "attention"
    if "norm" in n:
        return "norm"
    if any(a in n for a in ["gelu", "silu", "relu", "sigmoid", "tanh",
                             "softmax", "dropout", "mish", "swish"]):
        return "activation"
    if any(a in n for a in ["add", "mul", "sub", "div", "neg", "exp", "log",
                             "sqrt", "rsqrt", "pow", "abs", "clamp", "where",
                             "fill", "zero", "masked", "lerp", "addcmul",
                             "addcdiv"]):
        return "elementwise"
    if any(a in n for a in ["sum", "mean", "max", "min", "argmax", "topk",
                             "cumsum", "sort"]):
        return "reduction"
    if any(a in n for a in ["view", "reshape", "permute", "transpose", "t",
                             "expand", "squeeze", "unsqueeze", "contiguous",
                             "slice", "select", "cat", "stack", "clone",
                             "detach", "narrow", "flatten", "unfold",
                             "unsafe_view", "as_strided", "index", "gather",
                             "scatter", "alias", "copy", "pad"]):
        return "data_move"
    if "embedding" in n:
        return "embedding"
    if any(a in n for a in ["foreach", "fused_adam", "fused_sgd"]):
        return "optimizer"
    if any(a in n for a in ["upsample", "interpolate", "pool"]):
        return "pool"
    if any(a in n for a in ["loss", "nll", "cross_entropy"]):
        return "loss"
    return "other"

FAMILY_COLORS = {
    "matmul":     ("#DBEAFE", "#1E40AF"),  # blue
    "conv":       ("#D1FAE5", "#065F46"),  # green
    "attention":  ("#FEF3C7", "#92400E"),  # amber
    "norm":       ("#EDE9FE", "#5B21B6"),  # violet
    "activation": ("#FFEDD5", "#9A3412"),  # orange
    "elementwise":("#F3F4F6", "#374151"),  # gray
    "reduction":  ("#FEE2E2", "#991B1B"),  # red
    "data_move":  ("#E0F2FE", "#0C4A6E"),  # sky
    "embedding":  ("#F5F3FF", "#6D28D9"),  # purple
    "optimizer":  ("#FDF2F8", "#9D174D"),  # pink
    "pool":       ("#ECFDF5", "#047857"),  # emerald
    "loss":       ("#FFF1F2", "#BE123C"),  # rose
    "other":      ("#F9FAFB", "#6B7280"),  # neutral
    "unknown":    ("#F9FAFB", "#9CA3AF"),  # light gray
}

# ── .crtrace parser ────────────────────────────────────────────────────

OP_SIZE = 80
META_SIZE = 144

def load_trace(path):
    with open(path, "rb") as f:
        data = f.read()

    magic, version, num_ops, num_metas = struct.unpack_from("<4sIII", data, 0)
    assert magic == b"CRTR", f"Bad magic: {magic}"
    assert version == 1, f"Bad version: {version}"

    # Parse ops
    ops = []
    off = 16
    for i in range(num_ops):
        sh, shape_h, scope_h, call_h = struct.unpack_from("<QQQQ", data, off)
        n_in, n_out = struct.unpack_from("<HH", data, off + 72)[:2]
        grad = struct.unpack_from("<B", data, off + 78)[0]
        ops.append({
            "idx": i, "schema": sh, "shape": shape_h,
            "scope": scope_h, "n_in": n_in, "n_out": n_out, "grad": grad,
        })
        off += OP_SIZE

    # Parse metas
    metas = []
    for i in range(num_metas):
        sizes = struct.unpack_from("<8q", data, off)
        ptr = struct.unpack_from("<Q", data, off + 128)[0]
        ndim = struct.unpack_from("<B", data, off + 136)[0]
        dtype = struct.unpack_from("<b", data, off + 137)[0]
        metas.append({
            "sizes": sizes[:min(ndim, 8)], "data_ptr": ptr,
            "ndim": ndim, "dtype": dtype,
        })
        off += META_SIZE

    # Parse optional schema name table
    names = dict(BUILTIN_HASH_TABLE)  # start with builtins
    if off + 4 <= len(data):
        num_names = struct.unpack_from("<I", data, off)[0]
        off += 4
        for _ in range(num_names):
            if off + 10 > len(data):
                break
            sh, name_len = struct.unpack_from("<QH", data, off)
            off += 10
            if off + name_len > len(data):
                break
            name = data[off:off + name_len].decode("ascii", errors="replace")
            off += name_len
            names[sh] = name

    return ops, metas, names


def build_edges(ops, metas):
    ptr_to_producer = {}
    edges = []
    meta_cursor = 0
    for op in ops:
        n_in, n_out = op["n_in"], op["n_out"]
        for j in range(n_in):
            if meta_cursor + j < len(metas):
                ptr = metas[meta_cursor + j]["data_ptr"]
                if ptr and ptr in ptr_to_producer:
                    src = ptr_to_producer[ptr]
                    if src != op["idx"]:
                        edges.append((src, op["idx"]))
        for j in range(n_out):
            out_idx = meta_cursor + n_in + j
            if out_idx < len(metas):
                ptr = metas[out_idx]["data_ptr"]
                if ptr:
                    ptr_to_producer[ptr] = op["idx"]
        meta_cursor += n_in + n_out
    return edges


def shape_str(sizes):
    if not sizes:
        return ""
    return "\u00d7".join(str(s) for s in sizes)


def short_name(full_name):
    if not full_name:
        return None
    if full_name.startswith("aten::"):
        return full_name[6:]
    return full_name


def render_dot(ops, metas, edges, names, max_ops=None, title="Trace"):
    if max_ops and len(ops) > max_ops:
        ops = ops[:max_ops]
        op_set = {o["idx"] for o in ops}
        edges = [(s, d) for s, d in edges if s in op_set and d in op_set]

    # Get output shape per op
    meta_cursor = 0
    op_shapes = {}
    for op in ops:
        n_in, n_out = op["n_in"], op["n_out"]
        out_start = meta_cursor + n_in
        if n_out > 0 and out_start < len(metas):
            op_shapes[op["idx"]] = shape_str(metas[out_start]["sizes"])
        meta_cursor += n_in + n_out

    # In/out degree for visual weight
    in_deg = defaultdict(int)
    out_deg = defaultdict(int)
    for s, d in edges:
        in_deg[d] += 1
        out_deg[s] += 1

    lines = [
        "digraph CrucibleTrace {",
        f'  graph [rankdir=TB, fontname="Helvetica Neue,Helvetica,Arial",',
        f'         fontsize=11, label=<',
        f'           <b>{title}</b><br/>'
        f'           <font point-size="9" color="#888888">'
        f'{len(ops)} ops, {len(edges)} data-flow edges</font>>,',
        f'         labelloc=t, bgcolor="#FCFCFC", pad=0.4,',
        f'         nodesep=0.12, ranksep=0.20, margin=0.2];',
        f'  node [shape=box, style="filled,rounded", fontname="Helvetica Neue,Helvetica,Arial",',
        f'         fontsize=7, height=0.22, penwidth=0.6, margin="0.06,0.03"];',
        f'  edge [color="#C0C0C0", arrowsize=0.35, penwidth=0.35];',
        "",
    ]

    for op in ops:
        idx = op["idx"]
        full = names.get(op["schema"])
        sn = short_name(full)
        family = classify_op(full) if full else "unknown"
        fill, border = FAMILY_COLORS.get(family, FAMILY_COLORS["unknown"])
        shp = op_shapes.get(idx, "")

        # Build label
        if sn:
            label = f"<b>{sn}</b>"
        else:
            label = f'<font color="#999999">0x{op["schema"]:04x}</font>'
        if shp:
            label += f'<br/><font point-size="5.5" color="#888888">{shp}</font>'

        # High fan-in/fan-out → slightly bolder
        pw = "0.6"
        if out_deg[idx] > 3 or in_deg[idx] > 3:
            pw = "1.0"

        lines.append(
            f'  op{idx} [label=<{label}>, fillcolor="{fill}", '
            f'color="{border}", penwidth={pw}];'
        )

    lines.append("")
    for src, dst in edges:
        lines.append(f"  op{src} -> op{dst};")

    lines.append("}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Render .crtrace as graph")
    parser.add_argument("trace", help="Path to .crtrace file")
    parser.add_argument("-o", "--output", default=None,
                       help="Output file (pdf/svg/png)")
    parser.add_argument("--max-ops", type=int, default=None,
                       help="Limit to first N ops")
    parser.add_argument("--dot", action="store_true",
                       help="Output DOT source to stdout")
    parser.add_argument("--engine", default="dot",
                       help="Graphviz engine (dot/neato/fdp/sfdp)")
    args = parser.parse_args()

    trace_path = Path(args.trace)
    if not trace_path.exists():
        print(f"Error: {trace_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Loading {trace_path}...", file=sys.stderr)
    ops, metas, names = load_trace(trace_path)
    print(f"  {len(ops)} ops, {len(metas)} metas, "
          f"{len(names)} schema names", file=sys.stderr)

    print("Building edges...", file=sys.stderr)
    edges = build_edges(ops, metas)
    print(f"  {len(edges)} data-flow edges", file=sys.stderr)

    title = trace_path.stem.replace("_", " ").upper()
    dot_src = render_dot(ops, metas, edges, names,
                         max_ops=args.max_ops, title=title)

    if args.dot:
        print(dot_src)
        return

    output = args.output or str(trace_path.with_suffix(".pdf"))
    output_path = Path(output)
    fmt = output_path.suffix.lstrip(".")
    if fmt not in ("pdf", "svg", "png"):
        fmt = "pdf"

    import subprocess
    dot_path = output_path.with_suffix(".dot")
    dot_path.write_text(dot_src)
    print(f"Rendering with {args.engine}...", file=sys.stderr)

    result = subprocess.run(
        [args.engine, f"-T{fmt}", str(dot_path), "-o", str(output_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"Graphviz error: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    size_kb = output_path.stat().st_size // 1024
    print(f"  {output_path} ({size_kb} KB)", file=sys.stderr)
    dot_path.unlink()


if __name__ == "__main__":
    main()
