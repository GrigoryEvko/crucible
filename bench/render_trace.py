#!/usr/bin/env python3
"""Render a .crtrace binary as a dataflow graph via Graphviz.

Usage:
    python3 render_trace.py ../traces/resnet18.crtrace -o resnet18.pdf
    python3 render_trace.py ../traces/sd15_unet.crtrace -o sd15.svg --max-ops 300
    python3 render_trace.py ../traces/vit_b.crtrace --dot  # print DOT to stdout
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

    # Auto-detect meta record size: 144B (legacy), 160B (v2), 168B (current).
    # Same logic as C++ TraceLoader.h.
    meta_start = off
    remaining = len(data) - meta_start
    if num_metas > 0:
        for candidate in (168, 160, 144):
            if remaining >= num_metas * candidate:
                meta_size = candidate
                break
        else:
            meta_size = 144
    else:
        meta_size = 168
    print(f"  meta record size: {meta_size}B", file=sys.stderr)

    # Parse metas — field offsets are the same across all sizes,
    # the difference is trailing fields added in v2/v3.
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
        off += meta_size

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


def enrich_ops(ops, metas, edges, names, max_ops=None):
    """Build enriched node list with resolved scope paths."""
    if max_ops and len(ops) > max_ops:
        ops = ops[:max_ops]
        op_set = {o["idx"] for o in ops}
        edges = [(s, d) for s, d in edges if s in op_set and d in op_set]

    meta_cursor = 0
    nodes = []
    for op in ops:
        idx = op["idx"]
        n_in, n_out = op["n_in"], op["n_out"]
        full = names.get(op["schema"])
        sn = short_name(full)
        family = classify_op(full) if full else "unknown"
        fill, border = FAMILY_COLORS.get(family, FAMILY_COLORS["unknown"])

        # Resolve scope path from names table (scope hashes stored there too)
        scope_path = names.get(op.get("scope", 0), "")
        # Filter out non-module-path names (ATen op names contain ::)
        if "::" in scope_path:
            scope_path = ""

        # Output shape
        out_start = meta_cursor + n_in
        shp = ""
        if n_out > 0 and out_start < len(metas):
            shp = shape_str(metas[out_start]["sizes"])

        nodes.append({
            "id": idx,
            "name": sn or f"0x{op['schema']:04x}",
            "family": family,
            "fill": fill,
            "border": border,
            "shape": shp,
            "scope": scope_path,
            "n_in": n_in,
            "n_out": n_out,
            "grad": op.get("grad", 0),
        })
        meta_cursor += n_in + n_out

    return nodes, edges


def export_json(ops, metas, edges, names, max_ops=None):
    """Export raw JSON (no layout)."""
    import json
    nodes, edges = enrich_ops(ops, metas, edges, names, max_ops)
    return json.dumps({
        "nodes": nodes,
        "edges": [{"src": s, "dst": d} for s, d in edges],
    }, separators=(",", ":"))


def export_viewer_json(ops, metas, edges, names, max_ops=None, title="Trace"):
    """Generate clustered DOT, run dot -Tjson0, emit positioned JSON."""
    import json
    import subprocess

    nodes, edges = enrich_ops(ops, metas, edges, names, max_ops)

    # Build scope tree: scope_path → [node indices]
    scope_children = defaultdict(list)
    for i, nd in enumerate(nodes):
        scope_children[nd["scope"]].append(i)

    # Collect all unique scope prefixes for nested clusters
    all_scopes = set()
    for nd in nodes:
        if not nd["scope"]:
            continue
        parts = nd["scope"].split(".")
        for depth in range(1, len(parts) + 1):
            all_scopes.add(".".join(parts[:depth]))

    # Sort scopes by depth (deepest first for DOT nesting)
    sorted_scopes = sorted(all_scopes, key=lambda s: s.count("."), reverse=True)

    # Map scope → parent scope
    scope_parent = {}
    for s in sorted_scopes:
        if "." in s:
            scope_parent[s] = s.rsplit(".", 1)[0]
        else:
            scope_parent[s] = ""

    # Build DOT with nested subgraph clusters
    # DOT cluster names: sanitize dots to underscores
    def cluster_id(scope):
        return "cluster_" + scope.replace(".", "__")

    def scope_label(scope):
        return scope.rsplit(".", 1)[-1] if "." in scope else scope

    # Recursive DOT emission
    def emit_cluster(scope, indent=2):
        lines = []
        prefix = " " * indent
        cid = cluster_id(scope)
        lab = scope_label(scope)

        # Soft cluster colors by depth
        depth = scope.count(".") + 1
        bg_colors = ["#1a1a24", "#1c1c28", "#1e1e2c", "#202030", "#222234", "#242438"]
        border_colors = ["#2a2a3a", "#303045", "#363650", "#3c3c5a", "#424264", "#48486e"]
        bg = bg_colors[min(depth, len(bg_colors) - 1)]
        bc = border_colors[min(depth, len(border_colors) - 1)]

        lines.append(f'{prefix}subgraph {cid} {{')
        lines.append(f'{prefix}  label="{lab}";')
        lines.append(f'{prefix}  style=filled;')
        lines.append(f'{prefix}  color="{bc}";')
        lines.append(f'{prefix}  fillcolor="{bg}";')
        lines.append(f'{prefix}  fontcolor="#808090";')
        lines.append(f'{prefix}  fontsize=9;')
        lines.append(f'{prefix}  fontname="Inter,-apple-system,sans-serif";')
        lines.append(f'{prefix}  penwidth=0.8;')

        # Emit child clusters
        for child_scope in sorted_scopes:
            if scope_parent.get(child_scope) == scope:
                lines.extend(emit_cluster(child_scope, indent + 2))

        # Emit nodes that belong directly to this scope
        for i, nd in enumerate(nodes):
            if nd["scope"] == scope:
                lines.append(f'{prefix}  op{nd["id"]};')

        lines.append(f'{prefix}}}')
        return lines

    # DOT header
    dot_lines = [
        "digraph CrucibleTrace {",
        f'  graph [rankdir=TB, fontname="Inter,-apple-system,sans-serif",',
        f'         fontsize=11, bgcolor="#0f0f13", pad=0.5,',
        f'         nodesep=0.10, ranksep=0.22, margin=0.3,',
        f'         label=<{title}>, labelloc=t, fontcolor="#808090"];',
        f'  node [shape=box, style="filled,rounded",',
        f'         fontname="Inter,-apple-system,sans-serif",',
        f'         fontsize=7, height=0.20, penwidth=0.6,',
        f'         margin="0.06,0.02"];',
        f'  edge [color="#303040", arrowsize=0.3, penwidth=0.3];',
        "",
    ]

    # Emit top-level clusters (scope with no parent or parent="")
    top_scopes = [s for s in sorted_scopes if scope_parent.get(s, "") == ""]
    for s in sorted(top_scopes):
        dot_lines.extend(emit_cluster(s))

    # Emit unclustered nodes (no scope)
    for nd in nodes:
        if not nd["scope"]:
            dot_lines.append(f'  op{nd["id"]};')

    # Node attributes
    dot_lines.append("")
    in_deg = defaultdict(int)
    out_deg = defaultdict(int)
    for s, d in edges:
        in_deg[d] += 1
        out_deg[s] += 1

    for nd in nodes:
        idx = nd["id"]
        label = f'<b>{nd["name"]}</b>'
        if nd["shape"]:
            label += f'<br/><font point-size="5" color="#7a7a9a">{nd["shape"]}</font>'
        pw = "0.8" if (out_deg[idx] > 3 or in_deg[idx] > 3) else "0.5"
        dot_lines.append(
            f'  op{idx} [label=<{label}>, fillcolor="{nd["fill"]}", '
            f'color="{nd["border"]}", penwidth={pw}];'
        )

    # Edges
    dot_lines.append("")
    for src, dst in edges:
        dot_lines.append(f"  op{src} -> op{dst};")

    dot_lines.append("}")
    dot_src = "\n".join(dot_lines)

    # Run dot -Tjson0
    print(f"  Running dot -Tjson0 ({len(nodes)} nodes, {len(edges)} edges)...",
          file=sys.stderr)
    result = subprocess.run(
        ["dot", "-Tjson0"],
        input=dot_src, capture_output=True, text=True, timeout=120,
    )
    if result.returncode != 0:
        print(f"  Graphviz error: {result.stderr[:200]}", file=sys.stderr)
        # Fallback: return raw JSON
        return export_json(ops, metas, edges, names, max_ops)

    gv = json.loads(result.stdout)

    # Parse Graphviz output: extract positioned nodes, clusters, edges
    # Graphviz Y is bottom-up; we flip to top-down.
    bb = gv.get("bb", "0,0,100,100").split(",")
    graph_h = float(bb[3])

    def flip_y(y):
        return graph_h - y

    # Build gvid → node map
    gv_objects = gv.get("objects", [])
    positioned_nodes = []
    clusters = []

    for obj in gv_objects:
        name = obj.get("name", "")
        if name.startswith("cluster_"):
            # Cluster bounding box
            cbb = obj.get("bb", "0,0,0,0").split(",")
            x1, y1, x2, y2 = float(cbb[0]), float(cbb[1]), float(cbb[2]), float(cbb[3])
            # Flip Y
            fy1, fy2 = flip_y(y2), flip_y(y1)
            scope_path = name[8:].replace("__", ".")
            clusters.append({
                "scope": scope_path,
                "x": x1, "y": fy1,
                "w": x2 - x1, "h": fy2 - fy1,
                "label": obj.get("label", ""),
            })
        elif name.startswith("op"):
            # Node position
            op_id = int(name[2:])
            pos = obj.get("pos", "0,0").split(",")
            cx, cy = float(pos[0]), flip_y(float(pos[1]))
            w = float(obj.get("width", "1")) * 72  # inches→points
            h = float(obj.get("height", "0.5")) * 72

            # Find matching enriched node
            nd_data = None
            for nd in nodes:
                if nd["id"] == op_id:
                    nd_data = nd
                    break

            positioned_nodes.append({
                "id": op_id,
                "x": cx - w / 2, "y": cy - h / 2,
                "w": w, "h": h,
                "name": nd_data["name"] if nd_data else name,
                "family": nd_data["family"] if nd_data else "unknown",
                "fill": nd_data["fill"] if nd_data else "#F9FAFB",
                "border": nd_data["border"] if nd_data else "#9CA3AF",
                "shape": nd_data["shape"] if nd_data else "",
                "scope": nd_data["scope"] if nd_data else "",
                "grad": nd_data["grad"] if nd_data else 0,
            })

    # Parse edges with spline control points
    positioned_edges = []
    for edge in gv.get("edges", []):
        pos_str = edge.get("pos", "")
        if not pos_str:
            continue
        # Parse Graphviz edge pos: "e,ex,ey sx,sy cx1,cy1 cx2,cy2 ex,ey ..."
        # or "s,sx,sy ... e,ex,ey"
        points = []
        for token in pos_str.split():
            if token.startswith("e,") or token.startswith("s,"):
                xy = token[2:].split(",")
            else:
                xy = token.split(",")
            if len(xy) == 2:
                try:
                    points.append((float(xy[0]), flip_y(float(xy[1]))))
                except ValueError:
                    pass

        # Get source/dest from gvid
        tail_gvid = edge.get("tail")
        head_gvid = edge.get("head")
        tail_name = ""
        head_name = ""
        for obj in gv_objects:
            if obj.get("_gvid") == tail_gvid:
                tail_name = obj.get("name", "")
            if obj.get("_gvid") == head_gvid:
                head_name = obj.get("name", "")

        src_id = int(tail_name[2:]) if tail_name.startswith("op") else -1
        dst_id = int(head_name[2:]) if head_name.startswith("op") else -1

        positioned_edges.append({
            "src": src_id, "dst": dst_id,
            "points": points,
        })

    return json.dumps({
        "title": title,
        "graph_w": float(bb[2]),
        "graph_h": graph_h,
        "nodes": positioned_nodes,
        "edges": positioned_edges,
        "clusters": clusters,
    }, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description="Render .crtrace as graph")
    parser.add_argument("trace", help="Path to .crtrace file")
    parser.add_argument("-o", "--output", default=None,
                       help="Output file (pdf/svg/png/json/html)")
    parser.add_argument("--max-ops", type=int, default=None,
                       help="Limit to first N ops")
    parser.add_argument("--dot", action="store_true",
                       help="Output DOT source to stdout")
    parser.add_argument("--json", action="store_true",
                       help="Export raw JSON graph data")
    parser.add_argument("--viewer", action="store_true",
                       help="Export positioned JSON via Graphviz for viewer")
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

    if args.viewer:
        json_data = export_viewer_json(ops, metas, edges, names,
                                        max_ops=args.max_ops, title=title)
        output = args.output or str(trace_path.with_suffix(".json"))
        Path(output).write_text(json_data)
        print(f"  {output} ({len(json_data)//1024} KB)", file=sys.stderr)
        return

    if args.json:
        json_data = export_json(ops, metas, edges, names, max_ops=args.max_ops)
        output = args.output or str(trace_path.with_suffix(".json"))
        Path(output).write_text(json_data)
        print(f"  {output} ({len(json_data)//1024} KB)", file=sys.stderr)
        return

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
