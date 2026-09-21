#!/usr/bin/env python3
"""Measure whether the accelerator backward window reaches the trace.

A backward pass runs on the thread that called backward() for host
tensors, and on a per-device worker thread for accelerator tensors. The
ring is single-producer, so every thread but the first is turned away and
the window it produced is missing from the trace. CrucibleNative closes
that by serialising the autograd engine across every backward window it
records or replays. This script measures the result instead of taking it
on faith.

Three arms, and the shapes are the point:

  cpu            the control. Every schema it records is one the
                 accelerator arms are expected to record too.
  <device>       every tensor on one accelerator. The backward pass runs
                 on one worker thread, so a merge that concatenated one
                 thread after another would already look correct here.
  <device>+host  the token table on the host and the blocks on the
                 accelerator. The backward pass runs on the calling
                 thread and on a worker thread at the same time. This is
                 the shape that separates a real fix from one that only
                 holds for a single device, so it is part of the gate.

Each arm runs in its own process. The dispatch library's schema table is
a process-global static, so two arms in one process would leave the
second holding the union of both and no difference could show.

Usage:
    python record_cuda.py [--device cuda:0] [--iters 4]
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
                 n_layers: int, d_ff: int, max_seq: int,
                 host_embedding: bool = False):
        super().__init__()
        self.tok_emb = nn.Embedding(vocab, d_model)
        self.pos_emb = nn.Embedding(max_seq, d_model)
        self.blocks = nn.ModuleList(
            [Block(d_model, n_heads, d_ff) for _ in range(n_layers)]
        )
        self.ln_f = nn.LayerNorm(d_model)
        self.head = nn.Linear(d_model, vocab, bias=False)
        # True keeps the token table on the host while the blocks run on
        # the accelerator, which is what a large vocabulary does in
        # practice. The backward pass then has a host segment and a device
        # segment, and the default engine runs the two at the same time on
        # two threads.
        self.host_embedding = host_embedding

    def forward(self, idx):
        _, seq_len = idx.shape
        if self.host_embedding:
            tok = self.tok_emb(idx.to("cpu")).to(idx.device)
        else:
            tok = self.tok_emb(idx)
        x = tok + self.pos_emb(torch.arange(seq_len, device=idx.device))
        for block in self.blocks:
            x = block(x)
        return self.head(self.ln_f(x))


def run_arm(device: str, iters: int, out_path: str, verbose: bool,
            host_embedding: bool = False) -> dict:
    """Train on one device under the recorder and return what it captured."""
    torch.manual_seed(42)
    model = MiniGPT(vocab=128, d_model=64, n_heads=4,
                    n_layers=2, d_ff=128, max_seq=32,
                    host_embedding=host_embedding).to(device)
    if host_embedding:
        model.tok_emb.to("cpu")
    model.train()
    # The optimizer keeps its per-device default, which is the shape a
    # reader of this example is most likely to run.
    #
    # Pinning foreach=False was rejected once, on a measurement that no
    # longer holds. It emits three ops for each parameter, which turns the
    # optimizer phase into a long run with a period of three, and the
    # iteration detector matches on a five-hash signature. A detector reset
    # that lands inside that run locks onto the period of three and
    # publishes six-op regions from then on. That reset came from a
    # divergence, and the divergence came from a replayed backward window
    # that was not serialised. Re-measured on cuda:0 over 12 iterations
    # once the window is serialised under replay as well: foreach=False
    # gives an 885-op region, one detected boundary and no divergence,
    # against 801, one and none for the default.
    #
    # The hazard the earlier note described is real and belongs to the
    # detector rather than to the optimizer: a reset part way through a
    # short repeating run can lock onto that run's period. Nothing in a
    # divergence-free session triggers one.
    opt = torch.optim.SGD(model.parameters(), lr=0.1, momentum=0.9)
    loss_fn = nn.CrossEntropyLoss()

    idx = torch.randint(0, 128, (4, 16), device=device)
    tgt = torch.randint(0, 128, (4, 16), device=device)

    serialised_iters = 0
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
            # The guard must hold the engine on this thread for every
            # backward window, the replayed ones included. A replayed
            # dispatch records nothing but it does advance the replay
            # cursor, and a window that keeps its worker threads never
            # reaches that cursor, so the first op after the window fails
            # its guard. A silent failure to arm would show up only as a
            # divergence much later, so it is checked here.
            assert ctx.backward_serialised, (
                "the backward phase did not serialise the autograd engine"
            )
            serialised_iters += int(ctx.backward_serialised)
            loss.backward()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            assert not ctx.backward_serialised, (
                "leaving the backward phase must give the engine its "
                "worker threads back"
            )
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
            "host_embedding": host_embedding,
            "schemas": schemas,
            "num_ops": ctx.active_num_ops(),
            "bg_iters": ctx.bg_iterations(),
            "compiled": ctx.is_compiled(),
            "diverged": ctx.diverged_count(),
            "serialised_iters": serialised_iters,
        }
        result["exported"] = ctx.export_trace(out_path)

    return result


# A name carrying the other device's kernel family is a kernel difference,
# not a recording gap. Keeping the two apart is what stops the count of
# genuinely unrecorded ops from being inflated.
DEVICE_SPECIFIC = ("_for_cpu", "_foreach_", "_efficient_attention",
                   "_flash_attention")


def compare(cpu: dict, arm: dict, iters: int, label: str) -> bool:
    """Print one arm against the CPU control and report whether it passes.

    Three checks carry the verdict. The first two together say that the
    region holds the whole iteration, and the third says the arm reached
    that result the supported way.

    The schema comparison against the control says the region holds the
    backward window. A schema the control records and this arm does not,
    whose name is not a kernel family of the other device, is an op the
    recorder never saw.

    A divergence-free replay says the region holds a whole period and
    nothing else. The replay cursor checks one guard per op and walks the
    region end to end, so an op absent from the region, or one op of the
    region that never arrives, fails a guard within a single iteration.

    Every backward window serialised says the engine ran on the producer
    thread for each one. An arm that armed the guard for some windows and
    not others would pass the first two checks on a lucky schedule.

    The op count is reported and does not vote. The two arms run the same
    model and a different number of ops, because the control decomposes on
    the host what the accelerator does in one kernel. Measured over one
    replayed period of each arm, cpu 1091 ops against cuda:0 801, the 290
    are accounted for op by op. The CPU arm's SGD-with-momentum loop emits
    one aten::mul_.Tensor and two aten::add_.Tensor for each of the model's
    29 parameters, which is 87 ops against four aten::_foreach_* calls on
    the accelerator. Its BLAS wrappers add 63 aten::resolve_conj. The
    remaining 147 are the host's dtype conversions and view bookkeeping,
    led by aten::copy_, aten::empty_strided, aten::_to_copy, aten::to.dtype
    and aten::as_strided. A ratio against the control therefore measures
    the host's decomposition, not this arm's coverage.
    """
    missing = sorted(cpu["schemas"] - arm["schemas"])
    extra = sorted(arm["schemas"] - cpu["schemas"])
    gap = [n for n in missing if not any(m in n for m in DEVICE_SPECIFIC)]
    backward_gap = [n for n in gap if "backward" in n]

    ratio = arm["num_ops"] / cpu["num_ops"] if cpu["num_ops"] else 0.0
    backward_ok = not backward_gap
    replay_ok = arm["compiled"] and arm["diverged"] == 0
    serialised_ok = arm["serialised_iters"] == iters

    print(f"--- {label} against the cpu control ---")
    print(f"  schemas: cpu={len(cpu['schemas'])} {label}={len(arm['schemas'])}")
    print(f"  region_ops: cpu={cpu['num_ops']} {label}={arm['num_ops']} "
          f"({ratio:.1%} of the control, reported only)")
    print(f"  backward windows serialised: {arm['serialised_iters']} of {iters}")
    print(f"  recorded on cpu but not here: {len(missing)}  "
          f"({len(missing) - len(gap)} device-specific, {len(gap)} a gap)")
    for name in missing:
        print(f"      - {name}")
    if extra:
        print(f"  recorded here but not on cpu: {len(extra)}")
        for name in extra:
            print(f"      + {name}")
    if backward_gap:
        print(f"  MISSING BACKWARD SCHEMAS: {len(backward_gap)}")
        for name in backward_gap:
            print(f"      {name}")
    print(f"  missing backward schemas = {len(backward_gap)} "
          f"[{'PASS' if backward_ok else 'FAIL'}]")
    print(f"  replay: compiled={arm['compiled']} diverged={arm['diverged']} "
          f"[{'PASS' if replay_ok else 'FAIL'}]")
    print(f"  every backward window serialised "
          f"[{'PASS' if serialised_ok else 'FAIL'}]")
    print()
    return backward_ok and replay_ok and serialised_ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default="cuda:0")
    ap.add_argument("--iters", type=int, default=4)
    ap.add_argument("--frac", type=float, default=0.0,
                    help="cap on this device's memory as a fraction of total, "
                         "0 for no cap")
    ap.add_argument("--outdir", default="traces")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--arm", default=None,
                    help="internal: run one arm and write JSON to --json")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    # One arm per process. The dispatch library's schema table is a
    # process-global static, so two arms in one process would leave the
    # second holding the union of both and no difference could show.
    if args.arm:
        host_embedding = args.arm.endswith("+host")
        device = args.arm[: -len("+host")] if host_embedding else args.arm
        if device.startswith("cuda") and args.frac > 0:
            ordinal = int(device.split(":")[1]) if ":" in device else 0
            torch.cuda.set_per_process_memory_fraction(args.frac, ordinal)
        stem = args.arm.replace(":", "").replace("+", "_")
        result = run_arm(
            device, args.iters,
            os.path.join(args.outdir, f"minigpt_{stem}.crtrace"),
            args.verbose, host_embedding=host_embedding,
        )
        result["schemas"] = sorted(result["schemas"])
        with open(args.json, "w") as fh:
            json.dump(result, fh)
        return 0

    print("=" * 66)
    print(f"Crucible — accelerator backward window  (torch {torch.__version__})")
    print("=" * 66)

    if args.device.startswith("cuda"):
        if not torch.cuda.is_available():
            print("CUDA is not available")
            return 1
        ordinal = int(args.device.split(":")[1]) if ":" in args.device else 0
        free, total = torch.cuda.mem_get_info(ordinal)
        print(f"{args.device}: {torch.cuda.get_device_name(ordinal)}")
        print(f"  free {free / 2**30:.1f} GiB of {total / 2**30:.1f} GiB")
        if args.frac > 0:
            print(f"  this process is capped at {args.frac:.0%} of total")
    print()

    # The mixed arm is the one that separates a real fix from one that
    # only holds where a single worker thread produced the whole backward
    # pass. It runs last so its numbers sit next to the verdict.
    labels = ["cpu", args.device, f"{args.device}+host"]

    arms = {}
    with tempfile.TemporaryDirectory() as tmp:
        for label in labels:
            print(f"--- {label} ---")
            out_json = os.path.join(tmp, label.replace(":", "").replace("+", "_") + ".json")
            cmd = [sys.executable, os.path.abspath(__file__),
                   "--arm", label, "--json", out_json,
                   "--iters", str(args.iters), "--frac", str(args.frac),
                   "--outdir", os.path.abspath(args.outdir)]
            if args.verbose:
                cmd.append("--verbose")
            rc = subprocess.call(cmd)
            if rc != 0:
                print(f"  arm {label} exited {rc}")
                return rc
            with open(out_json) as fh:
                arms[label] = json.load(fh)
            arms[label]["schemas"] = set(arms[label]["schemas"])
            print()

    print("=" * 66)
    print("RESULT")
    print("=" * 66)
    for label in labels:
        r = arms[label]
        print(f"  {label:>14}: {len(r['schemas']):4d} distinct schemas  "
              f"region_ops={r['num_ops']:4d}  bg_iters={r['bg_iters']:3d}  "
              f"compiled={r['compiled']}  diverged={r['diverged']}")
    print()

    cpu = arms["cpu"]
    verdicts = {label: compare(cpu, arms[label], args.iters, label)
                for label in labels[1:]}

    print("=" * 66)
    for label, ok in verdicts.items():
        print(f"  {label:>14}: {'PASS' if ok else 'FAIL'}")
    all_ok = all(verdicts.values())
    print()
    print("VERDICT:", "every shape records the backward window and replays it"
          if all_ok else "a shape is still short of the control")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
