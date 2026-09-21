#!/usr/bin/env python3
"""Measure whether the accelerator backward window reaches the trace.

A backward pass runs on the thread that called backward() for host
tensors, and on a per-device worker thread for accelerator tensors. The
ring is single-producer, so every thread but the first is turned away and
the window it produced is missing from the trace. attach() closes that
with its Tensor.backward wrapper, which serialises the autograd engine
across every backward window the Vigil records or replays. This script
measures the result instead of taking it on faith.

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
import logging
import os
import subprocess
import sys
import tempfile
import time

import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from crucible_native import attach  # noqa: E402


log = logging.getLogger("crucible.record_cuda")


def configure_logging(verbose: bool) -> None:
    """Send this script's report and the recorder's log to stdout.

    One handler for both loggers, so the two streams interleave in the order
    they were written. The format carries the message alone, because the lines
    below are the report of this script rather than a diagnostic trail.
    """
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    for name, level in (("crucible.record_cuda", logging.INFO),
                        ("crucible_native",
                         logging.INFO if verbose else logging.WARNING)):
        logger = logging.getLogger(name)
        logger.handlers.clear()
        logger.addHandler(handler)
        logger.setLevel(level)
        logger.propagate = False


class Block(nn.Module):
    """One pre-norm transformer block with self-attention and an MLP."""

    def __init__(self, d_model: int, n_heads: int, d_ff: int) -> None:
        """Build the two norms, the attention and the MLP of one block."""
        super().__init__()
        self.ln1 = nn.LayerNorm(d_model)
        self.attn = nn.MultiheadAttention(d_model, n_heads, batch_first=True)
        self.ln2 = nn.LayerNorm(d_model)
        self.mlp = nn.Sequential(
            nn.Linear(d_model, d_ff), nn.GELU(), nn.Linear(d_ff, d_model)
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Attention and the MLP, each around a residual connection."""
        h = self.ln1(x)
        x = x + self.attn(h, h, h, need_weights=False)[0]
        return x + self.mlp(self.ln2(x))


class MiniGPT(nn.Module):
    """Token and position embeddings, N blocks, a final norm and a head."""

    def __init__(self, vocab: int, d_model: int, n_heads: int,
                 n_layers: int, d_ff: int, max_seq: int,
                 host_embedding: bool = False) -> None:
        """Build the embeddings, the blocks, the final norm and the head."""
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

    def forward(self, idx: torch.Tensor) -> torch.Tensor:
        """Embed the tokens, run the blocks, and project to the vocabulary."""
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

    # No device argument: run_arm has already placed the parameters, and the
    # host_embedding arm placed one of them somewhere else on purpose. attach
    # leaves a model where it is when no device is named, which is what that
    # split needs.
    with attach(model, opt, verbose=verbose) as ctx:
        for i in range(iters):
            t0 = time.perf_counter()
            opt.zero_grad()
            logits = model(idx)
            loss = loss_fn(logits.reshape(-1, 128), tgt.reshape(-1))
            windows_before = ctx.serialised_backward_windows
            loss.backward()
            # The wrapper must hold the engine on this thread for every
            # backward window, the replayed ones included. A replayed
            # dispatch records nothing but it does advance the replay
            # cursor, and a window that keeps its worker threads never
            # reaches that cursor, so the first op after the window fails
            # its guard. A silent failure to arm would show up only as a
            # divergence much later, so it is checked here, once per window.
            #
            # The count is what the check reads. The guard is released by the
            # time this line runs, so its own state can no longer say whether
            # the window it covered was serialised.
            assert ctx.serialised_backward_windows == windows_before + 1, (
                "the backward window did not serialise the autograd engine"
            )
            assert not ctx.backward_serialised, (
                "leaving the backward window must give the engine its "
                "worker threads back"
            )
            # The C++ side derives the BACKWARD phase from this counter, so a
            # window that closed must leave it at zero. A counter stuck above
            # zero would label every later operation backward.
            assert ctx.backward_depth() == 0, (
                "the backward window did not close"
            )
            opt.step()
            if device.startswith("cuda"):
                torch.cuda.synchronize(device)
            dt = (time.perf_counter() - t0) * 1000
            log.info("    iter %d: loss=%.4f (%6.1f ms) bg_iters=%d "
                     "compiled=%s", i, loss.item(), dt, ctx.bg_iterations(),
                     ctx.is_compiled())

        serialised_iters = ctx.serialised_backward_windows
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

# The smallest region an arm may publish, as a fraction of the control's
# count. This is a floor and never an equality, and the docstring of
# compare() gives the measurements the number comes from. An exact count
# has drifted twice in this repository, and the two arms legitimately run a
# different number of ops, so an equality here would fail on correct code.
REGION_FLOOR = 0.50


def compare(cpu: dict, arm: dict, iters: int, label: str) -> bool:
    """Print one arm against the CPU control and report whether it passes.

    Four checks carry the verdict. The first two together say that the
    region holds the whole iteration, the third says the arm reached that
    result the supported way, and the fourth rejects a region that collapsed.

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

    The op count carries a floor and not a ratio against the control.

    What the floor catches is a collapse, and neither of the first two
    checks catches it. The background thread's iteration detector matches on
    a five-hash signature, so it can lock onto a sub-period, publish a region
    far shorter than one iteration, and then replay that region against
    itself with no divergence at all. Both of the first two checks read green
    in that state. A 6-op region against this model was measured once, so the
    shape is reachable rather than hypothetical.

    The floor is half the control's count, and the margin either side is
    wide. Measured over one replayed period of each arm: cuda:0 801 ops and
    cuda:0+host 898 against a control of 1091, which is 73% and 82%, and the
    same model with foreach=False on the accelerator 885 ops, which is 81%.
    The lowest legitimate shape measured therefore sits 23 points above the
    floor. The 6-op collapse is 0.6% of the control, which the floor rejects
    by two orders of magnitude.

    An equality, or a tolerance band around the control, would fail on
    correct code. The two arms run the same model and a different number of
    ops, because the control decomposes on the host what the accelerator does
    in one kernel. The 290-op gap between cpu 1091 and cuda:0 801 is
    dominated by three buckets. The CPU arm's SGD-with-momentum loop emits
    one aten::mul_.Tensor and two aten::add_.Tensor for each of the model's
    29 parameters, which is 87 ops against four aten::_foreach_* calls on
    the accelerator. Its BLAS wrappers add 63 aten::resolve_conj. Some 147
    more are the host's dtype conversions and view bookkeeping, led by
    aten::copy_, aten::empty_strided, aten::_to_copy, aten::to.dtype and
    aten::as_strided. The accelerator's own kernel families run a few ops
    longer and offset part of the three buckets.
    """
    missing = sorted(cpu["schemas"] - arm["schemas"])
    extra = sorted(arm["schemas"] - cpu["schemas"])
    gap = [n for n in missing if not any(m in n for m in DEVICE_SPECIFIC)]
    backward_gap = [n for n in gap if "backward" in n]

    ratio = arm["num_ops"] / cpu["num_ops"] if cpu["num_ops"] else 0.0
    backward_ok = not backward_gap
    replay_ok = arm["compiled"] and arm["diverged"] == 0
    serialised_ok = arm["serialised_iters"] == iters
    # A control of zero ops makes the fraction meaningless, so it fails the
    # floor rather than dividing by it. main() checks the control itself.
    floor_ok = (cpu["num_ops"] > 0
                and arm["num_ops"] >= REGION_FLOOR * cpu["num_ops"])

    log.info("--- %s against the cpu control ---", label)
    log.info("  schemas: cpu=%d %s=%d",
             len(cpu["schemas"]), label, len(arm["schemas"]))
    log.info("  region_ops: cpu=%d %s=%d (%.1f%% of the control, floor %.0f%%)",
             cpu["num_ops"], label, arm["num_ops"], ratio * 100,
             REGION_FLOOR * 100)
    log.info("  backward windows serialised: %d of %d",
             arm["serialised_iters"], iters)
    log.info("  recorded on cpu but not here: %d  (%d device-specific, %d a gap)",
             len(missing), len(missing) - len(gap), len(gap))
    for name in missing:
        log.info("      - %s", name)
    if extra:
        log.info("  recorded here but not on cpu: %d", len(extra))
        for name in extra:
            log.info("      + %s", name)
    if backward_gap:
        log.info("  MISSING BACKWARD SCHEMAS: %d", len(backward_gap))
        for name in backward_gap:
            log.info("      %s", name)
    log.info("  missing backward schemas = %d [%s]",
             len(backward_gap), "PASS" if backward_ok else "FAIL")
    log.info("  replay: compiled=%s diverged=%d [%s]",
             arm["compiled"], arm["diverged"], "PASS" if replay_ok else "FAIL")
    log.info("  every backward window serialised [%s]",
             "PASS" if serialised_ok else "FAIL")
    log.info("  region_ops at or above the floor [%s]",
             "PASS" if floor_ok else "FAIL")
    log.info("")
    return backward_ok and replay_ok and serialised_ok and floor_ok


def main() -> int:
    """Run every arm, compare each against the control, and return the verdict."""
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

    configure_logging(args.verbose)
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

    log.info("=" * 66)
    log.info("Crucible — accelerator backward window  (torch %s)",
             torch.__version__)
    log.info("=" * 66)

    if args.device.startswith("cuda"):
        if not torch.cuda.is_available():
            log.error("CUDA is not available")
            return 1
        ordinal = int(args.device.split(":")[1]) if ":" in args.device else 0
        free, total = torch.cuda.mem_get_info(ordinal)
        log.info("%s: %s", args.device, torch.cuda.get_device_name(ordinal))
        log.info("  free %.1f GiB of %.1f GiB", free / 2**30, total / 2**30)
        if args.frac > 0:
            log.info("  this process is capped at %.0f%% of total",
                     args.frac * 100)
    log.info("")

    # The mixed arm is the one that separates a real fix from one that
    # only holds where a single worker thread produced the whole backward
    # pass. It runs last so its numbers sit next to the verdict.
    labels = ["cpu", args.device, f"{args.device}+host"]

    arms: dict[str, dict] = {}
    with tempfile.TemporaryDirectory() as tmp:
        for label in labels:
            log.info("--- %s ---", label)
            out_json = os.path.join(tmp, label.replace(":", "").replace("+", "_") + ".json")
            cmd = [sys.executable, os.path.abspath(__file__),
                   "--arm", label, "--json", out_json,
                   "--iters", str(args.iters), "--frac", str(args.frac),
                   "--outdir", os.path.abspath(args.outdir)]
            if args.verbose:
                cmd.append("--verbose")
            rc = subprocess.call(cmd)
            if rc != 0:
                log.error("  arm %s exited %d", label, rc)
                return rc
            with open(out_json) as fh:
                arms[label] = json.load(fh)
            arms[label]["schemas"] = set(arms[label]["schemas"])
            log.info("")

    log.info("=" * 66)
    log.info("RESULT")
    log.info("=" * 66)
    for label in labels:
        r = arms[label]
        log.info("  %14s: %4d distinct schemas  region_ops=%4d  bg_iters=%3d  "
                 "compiled=%s  diverged=%d",
                 label, len(r["schemas"]), r["num_ops"], r["bg_iters"],
                 r["compiled"], r["diverged"])
    log.info("")

    cpu = arms["cpu"]
    # Every check in compare() reads the control: the schema comparison takes
    # its schemas and the floor divides by its op count. A control that
    # collapsed or diverged would leave all of them vacuous and green, so it
    # is checked once here before any arm is compared against it.
    control_ok = cpu["compiled"] and cpu["diverged"] == 0 and cpu["num_ops"] > 0
    verdicts = {"cpu control": control_ok}
    verdicts.update({label: compare(cpu, arms[label], args.iters, label)
                     for label in labels[1:]})

    log.info("=" * 66)
    for label, ok in verdicts.items():
        log.info("  %14s: %s", label, "PASS" if ok else "FAIL")
    all_ok = all(verdicts.values())
    log.info("")
    log.info("VERDICT: %s",
             "every shape records the backward window and replays it" if all_ok
             else "a shape is still short of the control")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
