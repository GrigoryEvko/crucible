"""Run mpstk-crash-stop as an oracle for crash-stop liveness.

mpstk is the toolkit of Barwell, Scalas, Yoshida and Zhou (CONCUR 2022,
Generalised Multiparty Session Types with Crash-Stop Failures),
github.com/alcestes/mpstk-crash-stop, MIT licence.  It encodes a typing
context as an mCRL2 process and model-checks it.  An entry marked "!" is
unreliable: it can crash before each of its actions.  A branch labelled
"crash" is the crash detection of the receiver.  The communication is
synchronous.  The properties the harness asks for:

  safety            no output meets an input that cannot take it
  deadlock-freedom  a state with no communication has no pending action
  liveness+         on each fair path each pending action eventually
                    fires, and a crash detection counts as the firing of
                    an input from the crashed role

This module downloads the pinned commit into the cache of rocq.py, builds
it with sbt, and runs its verifier.  The toolchain is a Java 17 runtime,
sbt, and the mCRL2 tools mcrl22lps, lps2pbes and pbes2bool.  toolchain.py
installs a pinned release of each into the same cache, and each process of
this module runs in the environment that toolchain.py gives.
"""

from __future__ import annotations

import csv
import functools
import hashlib
import io
import logging
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import toolchain
from model import LBranch, LChoice, LEnd, LMsg, LRec, LVar, Local
from rocq import OracleError, answer_cache_path, cache_root, load_answers, save_answers

LOG = logging.getLogger("session_oracle.mpstk")

REPO = "alcestes/mpstk-crash-stop"
COMMIT = "3104e4ddfdc03385f68f53325908136c4a18b421"
PROPERTIES = ("safety", "deadlock-freedom", "liveness+")
BATCH = 24


@functools.cache
def _environment() -> dict[str, str]:
    return toolchain.environment()


def _tool(name: str) -> str:
    path = shutil.which(name, path=_environment()["PATH"])
    if path is None:
        raise OracleError(f"{name} is not in the toolchain that tools/session_oracle/toolchain.py installs.")
    return path


def fetch() -> Path:
    """Download and unpack the pinned commit.  Return its root."""
    root = cache_root() / f"mpstk-crash-stop-{COMMIT}"
    if (root / "build.sbt").is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://github.com/{REPO}/archive/{COMMIT}.tar.gz"
    LOG.info("fetching %s", url)
    try:
        with urllib.request.urlopen(url, timeout=120) as resp:
            data = resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {url}: {exc}") from exc
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        prefix = f"mpstk-crash-stop-{COMMIT}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "build.sbt").is_file():
        raise OracleError(f"{root}: the archive has no build.sbt")
    return root


def build(root: Path) -> str:
    """Compile mpstk once.  Return its class path."""
    classpath = root / "target" / "CLASSPATH"
    if not classpath.is_file():
        LOG.info("building mpstk in %s", root)
        proc = subprocess.run([_tool("sbt"), "-batch", "package"], cwd=root, capture_output=True, text=True,
                              env=_environment())
        if proc.returncode != 0 or not classpath.is_file():
            raise OracleError(f"building mpstk failed:\n{(proc.stdout + proc.stderr)[-4000:]}")
    return classpath.read_text(encoding="utf-8").strip()


# ── Local types in the syntax of mpstk ───────────────────────────────


def _label(label: str | int) -> str:
    if label == "v":
        return "v"
    if label == "crash":
        return "crash"
    if isinstance(label, int):
        return f"l{label}"
    if isinstance(label, str) and label.startswith("k") and label[1:].isdigit():
        return f"l{label[1:]}"
    raise OracleError(f"mpstk: no label for {label!r}")


def _payload(sort: str) -> str:
    return {"nat": "int", "bool": "bool", "unit": "unit"}[sort]


def _peer(e: LChoice, me: int) -> int:
    return e.channel % 8 if e.send else e.channel // 8


def local_text(e: Local, me: int, depth: int = 0) -> str:
    """Print a peer-annotated local type (model.LChoice form) in the syntax of mpstk.

    The channel of each action gives its peer (model.channel_of).  A
    recursion binder at depth d binds the variable t<d>.
    """
    if isinstance(e, LEnd):
        return "end"
    if isinstance(e, LVar):
        return f"t{depth - 1 - e.index}"
    if isinstance(e, LRec):
        return f"μ(t{depth}) {local_text(e.body, me, depth + 1)}"
    if isinstance(e, (LMsg, LBranch)):
        raise OracleError("mpstk: expected a peer-annotated local type")
    assert isinstance(e, LChoice)
    arms = []
    for label, sort, cont in e.branches:
        rest = local_text(cont, me, depth)
        if label == "crash":
            arms.append(f"crash . {rest}")
        else:
            arms.append(f"{_label(label)}({_payload(sort)}). {rest}")
    return f"r{_peer(e, me)}{'⊕' if e.send else '&'}{{{', '.join(arms)}}}"


def context_text(system: dict[int, Local], unreliable: frozenset[int]) -> str:
    """Print a typing context of one session s, with "!" before each unreliable entry."""
    entries = [f"{'!' if r in unreliable else ''}s[r{r}]: {local_text(e, r)}" for r, e in sorted(system.items())]
    return ",\n".join(entries) + "\n"


# ── Verification ─────────────────────────────────────────────────────


def _verify_batch(classpath: str, texts: list[str]) -> list[dict[str, bool]]:
    with tempfile.TemporaryDirectory(prefix="session_oracle_mpstk_") as tmp:
        root = Path(tmp)
        files = []
        for i, text in enumerate(texts):
            path = root / f"c{i}.ctx"
            path.write_text(text, encoding="utf-8")
            files.append(str(path))
        out = root / "out.csv"
        proc = subprocess.run([_tool("java"), "-cp", classpath, "mpstk.tool.Verifier",
                               "-p", ",".join(PROPERTIES), "-o", str(out), *files],
                              capture_output=True, text=True, cwd=tmp, env=_environment())
        if proc.returncode != 0 or not out.is_file():
            raise OracleError(f"mpstk failed:\n{(proc.stdout + proc.stderr)[-4000:]}")
        table = {Path(row["protocol"]).name: row for row in csv.DictReader(io.StringIO(out.read_text()))}
    results = []
    for i in range(len(texts)):
        row = table.get(f"c{i}.ctx")
        if row is None:
            raise OracleError(f"mpstk printed no verdict for context {i}:\n{texts[i]}")
        verdict = {}
        for prop in PROPERTIES:
            if row.get(prop) not in ("true", "false"):
                raise OracleError(f"mpstk gave {row.get(prop)!r} for {prop} on context {i}")
            verdict[prop] = row[prop] == "true"
        results.append(verdict)
    return results


def verify(texts: list[str], workers: int = 16) -> list[dict[str, bool]]:
    """Verify each context text.  Return, for each, the verdict of each of PROPERTIES.

    A verdict is kept in a cache whose file name carries the commit, under
    a hash of the context text and the property list.  The other contexts
    run in batches of BATCH, one verifier process per batch.  A tool
    failure raises OracleError, so a missing verdict never passes.
    """
    cache_path = answer_cache_path("mpstk", COMMIT)
    cache = load_answers(cache_path)
    keys = [hashlib.sha256(f"{','.join(PROPERTIES)}\n{text}".encode()).hexdigest() for text in texts]
    todo = list(dict.fromkeys(k for k in keys if k not in cache))
    by_key = dict(zip(keys, texts, strict=True))
    batches = [todo[i:i + BATCH] for i in range(0, len(todo), BATCH)]
    LOG.info("%d mpstk contexts: %d from the cache, %d in %d batches", len(texts),
             sum(k in cache for k in keys), len(todo), len(batches))
    if batches:
        for name in ("java", "mcrl22lps", "lps2pbes", "pbes2bool"):
            _tool(name)
        classpath = build(fetch())
        try:
            with ThreadPoolExecutor(max_workers=workers) as pool:
                for batch, part in zip(batches, pool.map(
                        lambda b: _verify_batch(classpath, [by_key[k] for k in b]), batches), strict=True):
                    cache.update(zip(batch, part, strict=True))
        finally:
            save_answers(cache_path, cache)
    return [{prop: bool(cache[k][prop]) for prop in PROPERTIES} for k in keys]
