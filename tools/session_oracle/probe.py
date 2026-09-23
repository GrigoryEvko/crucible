"""Measure what our C++ relations compute, one case per translation unit.

A probe declares ``session_oracle_show<T> probe;`` for each value it
measures.  The template has no definition, so the compiler reports the
declaration as an error and spells T in full.  The spelling is the
measurement.  A relation that fails with a hard error, for example the
frozen tree's plain merge on diverging branches, is measured as a
rejection with its bracketed diagnostic tag.

Probes run only when the golden file is regenerated.  The check that CI
runs compiles the emitted tests and never probes.
"""

from __future__ import annotations

import contextlib
import logging
import os
import re
import subprocess
import tempfile
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from model import (LBranch, LChoice, LEnd, LMsg, LRec, LVar, Local, PRELUDE_NS, cpp_prelude)

LOG = logging.getLogger("session_oracle.probe")

OLD_NS = "crucible::safety::proto"
FIXY_NS = "fixy::session"

# GCC quotes names with ASCII quotes in the C locale and with curly
# quotes in a UTF-8 locale.  run_probe sets the C locale, and the
# pattern accepts the two styles.
_SHOW = re.compile(r"['‘]session_oracle_show_(\w+)<(.*?)> session_oracle_probe_\1['’]")
_TAG = re.compile(r"\[([A-Za-z_]+)\]")


@dataclass(frozen=True, slots=True)
class Measured:
    """One probe result: the spelled values, or the rejection tag."""

    values: dict[str, str]
    rejection: str | None


def canonical(spelling: str) -> str:
    """Remove the whitespace that the compiler puts between tokens."""
    return re.sub(r"\s+", "", spelling)


# Each probe starts with one of these heads.  pch_heads compiles them once
# per run, which cuts a frozen-tree probe from about 2.7 s to 0.5 s.
HEADS = {
    "old": "#include <crucible/sessions/SessionGlobal.h>\n#include <type_traits>\n",
    "fixy": "#include <fixy/session/Protocol.h>\n#include <type_traits>\n",
    "multi": "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n"
             "#include <type_traits>\n",
    "subtype": "#include <fixy/session/Subtype.h>\n#include <type_traits>\n",
}
# The constexpr budget is the one that the project build passes (CMakeLists.txt),
# so a probe answers where the build answers.
_FLAGS = ("-std=c++26", "-freflection", "-fcontracts", "-fdiagnostics-color=never",
          "-fconstexpr-ops-limit=100000000")


@contextlib.contextmanager
def pch_heads(cxx: str, include: Path) -> Iterator[Path]:
    """Yield a directory with the compiled heads.  The directory is removed after use."""
    with tempfile.TemporaryDirectory(prefix="session_oracle_pch_") as tmp:
        root = Path(tmp)
        for kind, text in HEADS.items():
            header = root / f"session_oracle_head_{kind}.h"
            header.write_text(text, encoding="utf-8")
            proc = subprocess.run([cxx, *_FLAGS, f"-I{include}", "-x", "c++-header", str(header),
                                   "-o", f"{header}.gch"], capture_output=True, text=True,
                                  env={**os.environ, "LC_ALL": "C"})
            if proc.returncode != 0:
                raise RuntimeError(f"cannot precompile the {kind} probe head:\n{proc.stderr[-2000:]}")
        yield root


def run_probe(cxx: str, include: Path, source: str, heads: Path) -> Measured:
    """Compile ``source`` with -fsyntax-only and collect its show values."""
    with tempfile.TemporaryDirectory(prefix="session_oracle_") as tmp:
        path = Path(tmp) / "probe.cpp"
        path.write_text(source, encoding="utf-8")
        proc = subprocess.run(
            [cxx, *_FLAGS, f"-I{heads}", f"-I{include}", "-Winvalid-pch",
             "-fsyntax-only", "-fmax-errors=0", str(path)],
            capture_output=True, text=True, env={**os.environ, "LC_ALL": "C"})
    out = proc.stdout + proc.stderr
    values = {m.group(1): canonical(m.group(2)) for m in _SHOW.finditer(out)}
    rejection = None
    for line in out.splitlines():
        if "error:" in line and "session_oracle_show" not in line and "incomplete type" not in line:
            tag = _TAG.search(line)
            rejection = tag.group(1) if tag else line.split("error:", 1)[1].strip()[:120]
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


def read_protocol(spelling: str, ns: str, labelled: bool) -> Local:
    """Parse a canonical protocol spelling into the local-type IR.

    ``labelled`` is true for the frozen tree, whose Select and Offer
    branches start with a Send or Recv of ``Label<k>``.  The channel of
    every action is -1, because our protocols record no channel.
    """
    name, args = _split(spelling)
    short = name.removeprefix(ns + "::")
    if short == "End":
        return LEnd()
    if short == "Continue":
        return LVar(0)
    if short == "Loop":
        return LRec(read_protocol(args[0], ns, labelled))
    if short in ("Send", "Recv"):
        return LMsg(short == "Send", -1, _sort_of(args[0]), read_protocol(args[1], ns, labelled))
    if short in ("Select", "Offer"):
        branches = []
        for arg in args:
            if labelled:
                bname, bargs = _split(arg)
                branches.append(read_protocol(bargs[1], ns, labelled))
            else:
                branches.append(read_protocol(arg, ns, labelled))
        return LBranch(short == "Select", -1, tuple(branches))
    raise SpellingError(f"unknown protocol constructor {name!r} in {spelling!r}")


def _role_of(spelling: str) -> int:
    prefix = f"{PRELUDE_NS}::R"
    if not spelling.startswith(prefix) or not spelling[len(prefix):].isdigit():
        raise SpellingError(f"unknown role {spelling!r}")
    return int(spelling[len(prefix):])


def _label_of(spelling: str) -> str | int:
    if spelling == f"{PRELUDE_NS}::Val":
        return "v"
    name, args = _split(spelling)
    if name != f"{PRELUDE_NS}::Label" or len(args) != 1 or not args[0].rstrip("u").isdigit():
        raise SpellingError(f"unknown label {spelling!r}")
    return int(args[0].rstrip("u"))


def _payload_of(spelling: str) -> str:
    if spelling == f"{PRELUDE_NS}::Unit":
        return "unit"
    return _sort_of(spelling)


def read_fixy_projection(spelling: str, role: int) -> Local | str:
    """Parse the spelling of fixy::session::project_t<G, R>.

    Returns the local type with the peer of each action encoded as the
    channel (model.channel_of), or the name of the projection failure.
    """
    name, args = _split(spelling)
    if name == f"{FIXY_NS}::NotProjectable":
        return args[0].rsplit("::", 1)[-1]
    if name != f"{FIXY_NS}::Projected" or len(args) != 2:
        raise SpellingError(f"not a projection: {spelling!r}")
    if args[0] != f"{FIXY_NS}::OutQueue<>":
        raise SpellingError(f"a static type has an empty queue, not {args[0]!r}")
    return read_fixy_peer_local(args[1], role)


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
        return LChoice(send, role * 8 + peer if send else peer * 8 + role, (branch,))
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


def erase_channels(e: Local) -> Local:
    """Return ``e`` with every channel set to -1."""
    if isinstance(e, (LEnd, LVar)):
        return e
    if isinstance(e, LRec):
        return LRec(erase_channels(e.body))
    if isinstance(e, LMsg):
        return LMsg(e.send, -1, e.sort, erase_channels(e.cont))
    return LBranch(e.send, -1, tuple(erase_channels(b) for b in e.branches))


def drop_unguarded_loops(e: Local) -> Local:
    """Replace every Loop whose body reaches Continue before an action by End.

    This is the rule of the oracle's ``trans`` for a recursion whose
    projected body is not guarded.  The classifier uses it to recognise
    the divergence where our projection keeps such a Loop.
    """
    def guarded(x: Local) -> bool:
        if isinstance(x, LVar):
            return False
        if isinstance(x, LEnd):
            return True
        if isinstance(x, LRec):
            return guarded(x.body)
        return True

    if isinstance(e, (LEnd, LVar)):
        return e
    if isinstance(e, LRec):
        body = drop_unguarded_loops(e.body)
        return LRec(body) if guarded(body) else LEnd()
    if isinstance(e, LMsg):
        return LMsg(e.send, e.channel, e.sort, drop_unguarded_loops(e.cont))
    return LBranch(e.send, e.channel, tuple(drop_unguarded_loops(b) for b in e.branches))
