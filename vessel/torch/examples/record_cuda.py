#!/usr/bin/env python3
"""Measure the accelerator backward-window gap that crucible_fallback.cpp
documents.

That file states the behavior: a backward pass over CPU tensors runs on
the thread that called backward() and is recorded, while a backward pass
over accelerator tensors runs on the engine's per-device worker thread
and is not. This script measures the size of the resulting gap instead
of taking it on faith.

The CPU arm is the control. Both arms train the same model for the same
number of iterations, so a schema the CPU arm records and the CUDA arm
does not is either an unrecorded backward op or a kernel that genuinely
differs between the two devices. The report separates the two.

Each arm runs in its own process. The dispatch library's schema table is
a process-global static, so two arms in one process would leave the
second holding the union of both and no difference could show.

Usage:
    python record_cuda.py [--device cuda:3] [--iters 4] [--frac 0.12]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from crucible_native import CrucibleNative  # noqa: E402


class Block(nn.Module):
    """One pre-norm transformer block with self-attention and an MLP."""

    def __init__(self, d_model: int, n_heads: int, d_ff: int):
        super().__init__()
        self.ln1 = nn.LayerNorm(d_model)
        self.attn = nn.MultiheadAttention(d_model, n_heads, batch_first=True)
        self.ln2 = nn.LayerNorm(d_model)
        self.mlp = nn.Sequential(
            nn.Linear(d_model, d_ff), nn.GELU(), nn.Linear(d_ff, d_model)
        )

    def forward(self, x):
        h = self.ln1(x)
        x = x + self.attn(h, h, h, need_weights=False)[0]
        return x + self.mlp(self.ln2(x))


class MiniGPT(nn.Module):
    """Token and position embeddings, N blocks, a final norm and a head."""

    def __init__(self, vocab: int, d_model: int, n_heads: int,
                 n_layers: int, d_ff: int, max_seq: int):
        super().__init__()
        self.tok_emb = nn.Embedding(vocab, d_model)
        self.pos_emb = nn.Embedding(max_seq, d_model)
        self.blocks = nn.ModuleList(
            [Block(d_model, n_heads, d_ff) for _ in range(n_layers)]
        )
        self.ln_f = nn.LayerNorm(d_model)
        self.head = nn.Linear(d_model, vocab, bias=False)

    def forward(self, idx):
        _, seq_len = idx.shape
        x = self.tok_emb(idx) + self.pos_emb(
            torch.arange(seq_len, device=idx.device)
        )
        for block in self.blocks:
            x = block(x)
        return self.head(self.ln_f(x))


def run_arm(device: str, iters: int, out_path: str, verbose: bool) -> dict:
    """Train on one device under the recorder and return what it captured."""
    torch.manual_seed(42)
    model = MiniGPT(vocab=128, d_model=64, n_heads=4,
                    n_layers=2, d_ff=128, max_seq=32).to(device)
    model.train()
    opt = torch.optim.SGD(model.parameters(), lr=0.1, momentum=0.9)
    loss_fn = nn.CrossEntropyLoss()

    idx = torch.randint(0, 128, (4, 16), device=device)
    tgt = torch.randint(0, 128, (4, 16), device=device)

    with CrucibleNative(verbose=verbose) as ctx:
        ctx.track_modules(model)
        for i in range(iters):
            t0 = time.perf_counter()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            opt.zero_grad()
            ctx.set_training_phase(ctx.PHASE_FORWARD)
            logits = model(idx)
            loss = loss_fn(logits.reshape(-1, 128), tgt.reshape(-1))
            ctx.set_training_phase(ctx.PHASE_BACKWARD)
            loss.backward()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            opt.step()
            if device.startswith("cuda"):
                torch.cuda.synchronize(device)
            dt = (time.perf_counter() - t0) * 1000
            print(f"    iter {i}: loss={loss.item():.4f} ({dt:6.1f} ms) "
                  f"bg_iters={ctx.bg_iterations()} "
                  f"compiled={ctx.is_compiled()}")

        schemas = {name for _, name in ctx._dispatch.schema_entries()}
        result = {
            "device": device,
            "schemas": schemas,
            "num_ops": ctx.active_num_ops(),
            "bg_iters": ctx.bg_iterations(),
            "compiled": ctx.is_compiled(),
            "diverged": ctx.diverged_count(),
        }
        result["exported"] = ctx.export_trace(out_path)

    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default="cuda:3")
    ap.add_argument("--iters", type=int, default=4)
    ap.add_argument("--frac", type=float, default=0.12,
                    help="cap on this device's memory, as a fraction of total")
    ap.add_argument("--outdir", default="traces")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--arm", default=None,
                    help="internal: run one device and write JSON to --json")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    # One arm per process. The dispatch library's schema table is a
    # process-global static, so two arms in one process would leave the
    # second holding the union of both and no difference could show.
    if args.arm:
        if args.arm.startswith("cuda"):
            ordinal = int(args.arm.split(":")[1]) if ":" in args.arm else 0
            torch.cuda.set_per_process_memory_fraction(args.frac, ordinal)
        result = run_arm(
            args.arm, args.iters,
            os.path.join(args.outdir, f"minigpt_{args.arm.replace(':', '')}.crtrace"),
            args.verbose,
        )
        result["schemas"] = sorted(result["schemas"])
        with open(args.json, "w") as fh:
            json.dump(result, fh)
        return 0

    print("=" * 66)
    print(f"Crucible — CUDA vs CPU recording  (torch {torch.__version__})")
    print("=" * 66)

    if args.device.startswith("cuda"):
        if not torch.cuda.is_available():
            print("CUDA is not available")
            return 1
        ordinal = int(args.device.split(":")[1]) if ":" in args.device else 0
        free, total = torch.cuda.mem_get_info(ordinal)
        print(f"{args.device}: {torch.cuda.get_device_name(ordinal)}")
        print(f"  free {free / 2**30:.1f} GiB of {total / 2**30:.1f} GiB")
        print(f"  this process is capped at {args.frac:.0%} of total")
    print()

    arms = {}
    with tempfile.TemporaryDirectory() as tmp:
        for device in ("cpu", args.device):
            print(f"--- {device} ---")
            out_json = os.path.join(tmp, f"{device.replace(':', '')}.json")
            cmd = [sys.executable, os.path.abspath(__file__),
                   "--arm", device, "--json", out_json,
                   "--iters", str(args.iters), "--frac", str(args.frac),
                   "--outdir", os.path.abspath(args.outdir)]
            if args.verbose:
                cmd.append("--verbose")
            rc = subprocess.call(cmd)
            if rc != 0:
                print(f"  arm {device} exited {rc}")
                return rc
            with open(out_json) as fh:
                arms[device] = json.load(fh)
            arms[device]["schemas"] = set(arms[device]["schemas"])
            print()

    cpu, gpu = arms["cpu"], arms[args.device]
    print("=" * 66)
    print("RESULT")
    print("=" * 66)
    for tag, r in (("cpu", cpu), (args.device, gpu)):
        print(f"  {tag:>10}: {len(r['schemas']):4d} distinct schemas  "
              f"region_ops={r['num_ops']:4d}  bg_iters={r['bg_iters']:3d}  "
              f"compiled={r['compiled']}  diverged={r['diverged']}")

    missing = sorted(cpu["schemas"] - gpu["schemas"])
    extra = sorted(gpu["schemas"] - cpu["schemas"])
    print()
    print(f"  recorded on CPU but NOT on {args.device}: {len(missing)}")
    for name in missing:
        print(f"      - {name}")
    if extra:
        print(f"  recorded on {args.device} but not on CPU: {len(extra)}")
        for name in extra:
            print(f"      + {name}")

    # A name carrying the other device's kernel family is a kernel
    # difference, not a recording gap. Keep the two apart so the count
    # of genuinely unrecorded ops is not inflated.
    device_specific = ("_for_cpu", "_foreach_", "_efficient_attention",
                       "_flash_attention")
    gap = [n for n in missing if not any(m in n for m in device_specific)]
    backward_gap = [n for n in gap if "backward" in n]

    print()
    print(f"  of those {len(missing)}, {len(missing) - len(gap)} are "
          f"device-specific kernels and {len(gap)} are a recording gap")
    if backward_gap:
        print(f"  {len(backward_gap)} of the gap are backward ops:")
        for name in backward_gap:
            print(f"      {name}")
        print()
        print("  The loss fell on both arms, so backward ran on both. On the")
        print("  accelerator it ran on the engine's per-device worker thread,")
        print("  which the recorder does not serve. See the backward-window")
        print("  note in vessel/torch/crucible_fallback.cpp.")
        print(f"  Region size: cpu={cpu['num_ops']} ops, "
              f"{args.device}={gpu['num_ops']} ops.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
