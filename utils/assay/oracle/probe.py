"""Measure what our C++ relations compute, one case per translation unit.

A probe declares ``session_oracle_show<T> probe;`` for each value it
measures.  The template has no definition, so the compiler reports the
declaration as an error and spells T in full.  The spelling is the
measurement.  A relation that fails with a hard error is measured as a
rejection with its bracketed diagnostic tag.  The probe reads the
messages from the SARIF log of its compile (assay.readers.sarif), and
not from the text output of the compiler.

Probes run only when the golden file is regenerated.  The check that CI
runs compiles the emitted tests and never probes.
"""

from __future__ import annotations

import contextlib
import logging
import re
import tempfile
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.oracle.model import (LChoice, LEnd, LRec, LVar, Local, PRELUDE_NS, cpp_prelude)
from assay.readers import sarif

LOG = logging.getLogger("session_oracle.probe")

FIXY_NS = "fixy::session"

# GCC quotes names with ASCII quotes in the C locale and with curly
# quotes in a UTF-8 locale.  run_probe sets the C locale, and the
# pattern accepts the two styles.
_SHOW = re.compile(r"['‘]session_oracle_show_(\w+)<(.*?)> session_oracle_probe_\1['’]")
_TAG = re.compile(r"\[([A-Za-z_]+)\]")
# The kinds of a diagnostic that the text output of GCC prints with the word "error:" in its head.  The first such
# diagnostic that gives no show value gives the field `rejection` of a probe.
_REJECTION_KINDS = frozenset({sarif.Kind.ERROR, sarif.Kind.FATAL, sarif.Kind.ICE, sarif.Kind.PERMERROR})
# The texts of the message of a show value.  A message that holds one of them gives no `rejection`.
_SHOW_MARKS = ("session_oracle_show", "incomplete type")


@dataclass(frozen=True, slots=True)
class Measured:
    """One probe result: the spelled values, or the rejection tag."""

    values: dict[str, str]
    rejection: str | None


def canonical(spelling: str) -> str:
    """Remove the whitespace that the compiler puts between tokens."""
    return "".join(spelling.split())


# Each probe starts with one of these heads.  pch_heads compiles them once
# per run, so a probe does not parse its head again.
HEADS = {
    "fixy": "#include <fixy/session/Protocol.h>\n#include <type_traits>\n",
    "multi": "#include <fixy/session/Liveness.h>\n#include <fixy/session/Network.h>\n"
             "#include <fixy/session/Projection.h>\n#include <type_traits>\n",
    "subtype": "#include <fixy/session/Subtype.h>\n#include <type_traits>\n",
    "keyed": "#include <fixy/session/Projection.h>\n#include <fixy/session/Subtype.h>\n"
             "#include <type_traits>\n",
    "semantics": "#include <fixy/session/CrashAssociation.h>\n#include <fixy/session/Semantics.h>\n"
                 "#include <type_traits>\n",
}
# The constexpr budget is the one that the project build passes, the measured
# rule constexpr-ops of utils/registry.txt, so a probe answers where the build
# answers.
_FLAGS = ("-std=c++26", "-freflection", "-fcontracts", "-fdiagnostics-color=never",
          f"-fconstexpr-ops-limit={check_report.threshold_text(check_report.read_budgets()['constexpr-ops'].error)}")


@contextlib.contextmanager
def pch_heads(cxx: str, include: Path) -> Iterator[Path]:
    """Yield a directory with the compiled heads.  The directory is removed after use."""
    with tempfile.TemporaryDirectory(prefix="session_oracle_pch_") as tmp:
        root = Path(tmp)
        for kind, text in HEADS.items():
            header = root / f"session_oracle_head_{kind}.h"
            header.write_text(text, encoding="utf-8")
            completed = proc.run(proc.Command([cxx, *_FLAGS, f"-I{include}", "-x", "c++-header", str(header),
                                               "-o", f"{header}.gch"], timeout_s=proc.TOOL_S,
                                              env=proc.environment({"LC_ALL": "C"})))
            if completed.exit != 0:
                raise RuntimeError(f"cannot precompile the {kind} probe head:\n{completed.err[-2000:]}")
        yield root


def run_probe(cxx: str, include: Path, source: str, heads: Path) -> Measured:
    """Compile ``source`` with -fsyntax-only, and collect its show values and its `rejection` from the SARIF log.

    Each record of the log is a diagnostic or a context line, in the sequence of the text output.  _SHOW reads the
    show values from the message of each record.  The field `rejection` comes from the first record of
    _REJECTION_KINDS with a message that gives no show value.  It is the bracketed tag of the message, or the first
    120 characters of the message.  The option of the record follows the message in brackets, as in the text output
    of GCC.

    Raises:
        RuntimeError: If the compiler cannot start, or the log cannot be read
    """
    with tempfile.TemporaryDirectory(prefix="session_oracle_") as tmp:
        path = Path(tmp) / "probe.cpp"
        log_path = Path(tmp) / "probe.sarif"
        path.write_text(source, encoding="utf-8")
        try:
            proc.run(proc.Command(
                [cxx, *_FLAGS, f"-I{heads}", f"-I{include}", "-Winvalid-pch",
                 "-fsyntax-only", "-fmax-errors=0", f"{sarif.SARIF_FLAG}{log_path}", str(path)],
                timeout_s=proc.TOOL_S, env=proc.environment({"LC_ALL": "C"}),
                stdout=proc.Output.DISCARD, stderr=proc.Output.DISCARD))
            records = sarif.records(sarif.read(log_path))
        except (proc.ToolFailed, OSError, sarif.SarifError) as problem:
            raise RuntimeError(f"a probe gives no SARIF log that the reader can read: {problem}") from None
    values = {m.group(1): canonical(m.group(2)) for record in records for m in _SHOW.finditer(record.message)}
    rejection = None
    for record in records:
        if record.kind in _REJECTION_KINDS and not any(mark in record.message for mark in _SHOW_MARKS):
            tag = _TAG.search(record.message)
            text = f"{record.message} [{record.option}]" if record.option else record.message
            rejection = tag.group(1) if tag else text.strip()[:120]
            break
    return Measured(values, rejection)


def run_many(cxx: str, include: Path, sources: list[str], workers: int,
             heads: Path) -> list[Measured]:
    """Run every probe on a thread pool.  The order of results matches."""
    with ThreadPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(lambda s: run_probe(cxx, include, s, heads), sources))


def probe_header(kind: str, alias: str) -> str:
    """Return the common head of a probe translation unit (``kind`` of HEADS)."""
    return f'#include "session_oracle_head_{kind}.h"\n{cpp_prelude()}{alias}\n'


def show(name: str, type_spelling: str) -> str:
    """Return one measurement declaration."""
    return (f"template <class T> struct session_oracle_show_{name};\n"
            f"session_oracle_show_{name}<{type_spelling}> session_oracle_probe_{name};\n")


# ── Reading a spelled protocol back into the local-type IR ───────────


class SpellingError(ValueError):
    """A spelled type is not a protocol that the parser knows."""


def _split(text: str) -> tuple[str, list[str]]:
    """Split ``Name<A,B>`` into the name and the top-level arguments."""
    lt = text.find("<")
    if lt < 0:
        return text, []
    if not text.endswith(">"):
        raise SpellingError(f"unbalanced spelling {text!r}")
    name, inner = text[:lt], text[lt + 1:-1]
    if not inner:
        return name, []
    args, depth, start = [], 0, 0
    for i, ch in enumerate(inner):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(inner[start:i])
            start = i + 1
    args.append(inner[start:])
    return name, args


def _sort_of(spelling: str) -> str:
    table = {f"{PRELUDE_NS}::Nat": "nat", f"{PRELUDE_NS}::Bool": "bool"}
    if spelling not in table:
        raise SpellingError(f"unknown payload {spelling!r}")
    return table[spelling]


def _role_of(spelling: str) -> int:
    prefix = f"{PRELUDE_NS}::R"
    if not spelling.startswith(prefix) or not spelling[len(prefix):].isdigit():
        raise SpellingError(f"unknown role {spelling!r}")
    return int(spelling[len(prefix):])


def _label_of(spelling: str) -> str | int:
    if spelling == f"{PRELUDE_NS}::Val":
        return "v"
    if spelling == f"{FIXY_NS}::global::CrashLabel":
        return "crash"
    name, args = _split(spelling)
    if name != f"{PRELUDE_NS}::Label" or len(args) != 1 or not args[0].rstrip("u").isdigit():
        raise SpellingError(f"unknown label {spelling!r}")
    return int(args[0].rstrip("u"))


def _payload_of(spelling: str) -> str:
    if spelling == f"{PRELUDE_NS}::Unit":
        return "unit"
    if spelling == "void":
        return "void"
    return _sort_of(spelling)


def read_fixy_projection(spelling: str, role: int) -> Local | str:
    """Parse the spelling of fixy::session::project_t<G, R> for a static global type.

    Returns the local type with the peer of each action encoded as the
    channel (model.channel_of), or the name of the projection failure.
    A static global type has no message en route, so the queue must be
    empty.
    """
    local, queue = read_fixy_projection_with_queue(spelling, role)
    if queue:
        raise SpellingError(f"a static type has an empty queue, not {queue!r}")
    return local


def read_fixy_projection_with_queue(spelling: str, role: int) -> tuple[Local | str, tuple]:
    """Parse the spelling of fixy::session::project_t<G, R> for a runtime global type.

    Returns the local type (or the name of the projection failure) and
    the queue of the role as messages in flight: tuples (channel, sender,
    receiver, label, sort) in the form of execution.explore.
    """
    name, args = _split(spelling)
    if name == f"{FIXY_NS}::NotProjectable":
        return args[0].rsplit("::", 1)[-1], ()
    if name != f"{FIXY_NS}::Projected" or len(args) != 2:
        raise SpellingError(f"not a projection: {spelling!r}")
    qname, qargs = _split(args[0])
    if qname != f"{FIXY_NS}::OutQueue":
        raise SpellingError(f"not a queue: {args[0]!r}")
    queue = []
    for item in qargs:
        iname, iargs = _split(item)
        if iname != f"{FIXY_NS}::Queued" or len(iargs) != 3:
            raise SpellingError(f"not a queued message: {item!r}")
        to = _role_of(iargs[0])
        queue.append((role * 8 + to, role, to, _label_of(iargs[1]), _payload_of(iargs[2])))
    return read_fixy_peer_local(args[1], role), tuple(queue)


def read_fixy_peer_local(spelling: str, role: int) -> Local:
    """Parse a peer-annotated fixy local type into the choice form of the IR."""
    name, args = _split(spelling)
    short = name.removeprefix(FIXY_NS + "::")
    if short == "End":
        return LEnd()
    if short == "Continue":
        return LVar(0)
    if short == "Loop":
        return LRec(read_fixy_peer_local(args[0], role))

    def arm(text: str, want: str) -> tuple[int, tuple[str | int, str, Local]]:
        arm_name, arm_args = _split(text)
        if arm_name != f"{FIXY_NS}::{want}":
            raise SpellingError(f"expected {want} in {text!r}")
        msg_name, msg_args = _split(arm_args[0])
        if msg_name != f"{FIXY_NS}::PeerMsg" or len(msg_args) != 3:
            raise SpellingError(f"expected PeerMsg in {text!r}")
        return (_role_of(msg_args[0]),
                (_label_of(msg_args[1]), _payload_of(msg_args[2]),
                 read_fixy_peer_local(arm_args[1], role)))

    if short in ("Send", "Recv"):
        peer, branch = arm(spelling, short)
        send = short == "Send"
        return LChoice(send, role * 8 + peer if send else peer * 8 + role, (branch,), True)
    if short in ("Select", "Offer"):
        send = short == "Select"
        items = args if send else args[1:]
        arms = [arm(a, "Send" if send else "Recv") for a in items]
        peers = {peer for peer, _ in arms}
        if len(peers) != 1:
            raise SpellingError(f"a choice with more than one peer: {spelling!r}")
        peer = peers.pop()
        return LChoice(send, role * 8 + peer if send else peer * 8 + role,
                       tuple(branch for _, branch in arms))
    raise SpellingError(f"unknown fixy local constructor {name!r} in {spelling!r}")
