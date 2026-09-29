"""Check implementability on three kinds of network with Sprout(A) (Li and Wies, PLDI 2026).

"Implementability of Global Distributed Protocols Modulo Network Architectures"
(Elaine Li and Thomas Wies, PACMPL 10, PLDI 2026, doi 10.1145/3808319)
characterises the implementability of a global protocol for each kind of
network, by coherence conditions that depend on how one message buffer
inserts and removes.  Their tool Sprout(A) decides it with the muCLP solver
MuVal.  Three of their networks match our substrates:

  p2pbox   one FIFO queue for each ordered pair of roles   an SPSC channel for each pair
  mailbox  one FIFO queue for each receiver                 an MPSC channel for each receiver
  bag      one unordered buffer for each receiver           an MPMC queue with no order

The artifact (doi 10.5281/zenodo.19600644, CC BY 4.0) gives the front end of
Sprout(A) as source.zip, and MuVal only as a build tree inside an arm64
container image.  This module builds the two natively in the cache of
rocq.py, with the opam switch SWITCH (utils/scripts/session-oracle.sh says how to
make it):

  front end  source.zip of the artifact, checked against SOURCE_SHA256
  MuVal      CoAR (github.com/hiroshi-unno/coar, Apache 2.0) at COAR_COMMIT.
             Each of the 1,346 source and configuration files in the image's
             MuVal tree is equal, byte for byte, to the file of that commit.
             The 45 other files are menhir, ocamllex and ppx outputs, and
             two .DS_Store files.

The input of Sprout(A) is a finite state machine of global transitions.
Each state of the global type (keskin.GlobalStates) is a state of the
machine, and each branch is a transition p->q:v{v=n}, where the number n
names the label:

  a value of sort bool   0
  a value of sort nat    1
  the branch k           k + 2

The final states are the ends.  One run gives one of four verdicts:
implementable, non-implementable, inconclusive (a muCLP query that timed
out or failed) and gclts-ineligible (the protocol is outside the class that
the tool decides: not sender-driven, not sink-final, not deterministic or
with a global deadlock).  Only the first two are verdicts on
implementability.  A run that prints no verdict is inconclusive.  Each
protocol runs with the two query generators of MODES, and combine joins
their verdicts: when two decisive verdicts differ, the answer is
modes-disagree, which is a gap and never a pass.
"""

from __future__ import annotations

import hashlib
import io
import logging
import os
import re
import shutil
import signal
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from keskin import GlobalStates, Premise
from model import Global, read_global
from rocq import OracleError, answer_cache_path, cache_root, load_answers, save_answers

LOG = logging.getLogger("session_oracle.sprout")

ARTIFACT = "10.5281/zenodo.19600644"
SOURCE_URL = "https://zenodo.org/api/records/19600644/files/source.zip/content"
SOURCE_SHA256 = "6e24c2b69ffba4872497d064e0d7e97f759b1e4e83ac07efb8fd375daf2d1520"
COAR_REPO = "hiroshi-unno/coar"
COAR_COMMIT = "1d49999975b00f1430b3c9d10b90ab00b561e836"
SWITCH = os.environ.get("SESSION_ORACLE_SPROUT_SWITCH", "sprout")
NETWORKS = ("p2pbox", "mailbox", "bag")
# The two query generators of the front end: "opt" (the optimised queries
# of the paper's evaluation) and "naive" (one query for each condition).
# Each protocol runs in both, because they can disagree: on bag, opt
# accepts a protocol whose run of the projected context deadlocks, and
# naive refuses it (see combine).
MODES = ("opt", "naive")
QUERY_TIMEOUT = 60
RUN_TIMEOUT = 3600
WORKERS = 24
VERDICTS = ("implementable", "non-implementable", "inconclusive", "gclts-ineligible")
_RESULT = re.compile(r"^Total verification time: [0-9.]+s, (.+)$", re.MULTILINE)
_VALID = re.compile(r"^\| (\S+\.hes)\s+\| [0-9.]+\s+\| valid\s+\|$", re.MULTILINE)

# The controls: each network must refuse one protocol and accept another,
# with MuVal deciding, before any verdict counts.  A build whose MuVal
# answers "valid" to every query refuses everything, and one that fails
# answers nothing, so each fails a control.
#
#   planted   role 2 must act differently in the two branches of a choice
#             that nobody tells it: no network implements it
#   chain     a message from 0 to 1, then one from 1 to 2: every network
#             implements it
#   mb-no-p2p-yes, bag-no-p2p-yes
#             two protocols of Table 1 of the paper, section 7.2, with
#             their published verdicts
CONTROLS = (
    ("planted", "branch(0,1,[msg(2,0,nat,end),msg(2,0,bool,end)])",
     {"p2pbox": "non-implementable", "mailbox": "non-implementable", "bag": "non-implementable"}),
    ("chain", "msg(0,1,nat,msg(1,2,nat,end))",
     {"p2pbox": "implementable", "mailbox": "implementable", "bag": "implementable"}),
    ("mb-no-p2p-yes", "msg(0,1,nat,msg(2,1,nat,end))",
     {"p2pbox": "implementable", "mailbox": "non-implementable", "bag": "implementable"}),
    ("bag-no-p2p-yes", "rec(branch(0,1,[var,end]))",
     {"p2pbox": "implementable", "mailbox": "implementable", "bag": "non-implementable"}),
)


# ── The native build ─────────────────────────────────────────────────


def _opam() -> str:
    tool = shutil.which("opam") or str(Path.home() / ".local" / "bin" / "opam")
    if not os.access(tool, os.X_OK):
        raise OracleError("opam is not on PATH.  Sprout(A) builds with the opam switch "
                          f"'{SWITCH}'; utils/scripts/session-oracle.sh says how to make it.")
    return tool


def _in_switch(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    """Run a command of the opam switch SWITCH in ``cwd``."""
    return subprocess.run([_opam(), "exec", f"--switch={SWITCH}", "--", *args], cwd=cwd,
                          capture_output=True, text=True, check=False)


def _download(url: str) -> bytes:
    LOG.info("fetching %s", url)
    try:
        with urllib.request.urlopen(url, timeout=300) as resp:
            return resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {url}: {exc}") from exc


def fetch_coar() -> Path:
    """Download and unpack CoAR at COAR_COMMIT.  Return its root."""
    root = cache_root() / f"coar-{COAR_COMMIT}"
    if (root / "CoAR.opam").is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    data = _download(f"https://github.com/{COAR_REPO}/archive/{COAR_COMMIT}.tar.gz")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        prefix = f"coar-{COAR_COMMIT}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "CoAR.opam").is_file():
        raise OracleError(f"{root}: the archive has no CoAR.opam")
    return root


def fetch_front_end(coar: Path) -> Path:
    """Download source.zip, check its hash and unpack the front end.  Return its root.

    config.ml names the directory where the front end runs MuVal, so the
    unpacked copy points it at ``coar``.
    """
    root = cache_root() / f"sprout-a-{SOURCE_SHA256[:16]}" / "query-generator"
    if not (root / "dune-project").is_file():
        data = _download(SOURCE_URL)
        digest = hashlib.sha256(data).hexdigest()
        if digest != SOURCE_SHA256:
            raise OracleError(f"source.zip of doi {ARTIFACT} has sha256 {digest}, not {SOURCE_SHA256}")
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for name in archive.namelist():
                if name.startswith("query-generator/") and not name.endswith("/") \
                        and not name.endswith(".DS_Store"):
                    target = root.parent / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(name))
    config = f'let coar_location = "{coar}"\n'
    if (root / "config.ml").read_text(encoding="utf-8") != config:
        (root / "config.ml").write_text(config, encoding="utf-8")
    return root


def build() -> tuple[Path, str]:
    """Build MuVal and the front end once.  Return the front end and the identity of the build.

    The identity names the two sources and the versions of OCaml and Z3, so
    a cached verdict of one build is never read as a verdict of another.
    """
    coar = fetch_coar()
    front = fetch_front_end(coar)
    for where, target in ((coar, "main.exe"), (front, "./main.exe")):
        if not (where / "_build" / "default" / "main.exe").is_file():
            LOG.info("building %s in %s", target, where)
            proc = _in_switch(["dune", "build", target], where)
            if proc.returncode != 0 or not (where / "_build" / "default" / "main.exe").is_file():
                raise OracleError(f"building {where} failed:\n{(proc.stdout + proc.stderr)[-4000:]}")
    versions = _in_switch(["ocamlfind", "query", "-format", "%v", "z3"], coar)
    ocaml = _in_switch(["ocamlc", "-version"], coar)
    if versions.returncode != 0 or ocaml.returncode != 0:
        raise OracleError(f"the opam switch '{SWITCH}' has no ocamlc or no z3 package:\n"
                          f"{versions.stderr}{ocaml.stderr}")
    ident = (f"coar {COAR_COMMIT}, source.zip sha256 {SOURCE_SHA256}, OCaml {ocaml.stdout.strip()}, "
             f"z3 {versions.stdout.strip()}, {os.uname().machine}")
    return front / "_build" / "default" / "main.exe", ident


# ── Protocols ────────────────────────────────────────────────────────


def protocol_text(g: Global) -> str:
    """Return the input of Sprout(A) for the global type ``g``.  O(states × labels)."""
    gs = GlobalStates.of(g)
    lines = ["Initial state: (0)", "Initial register assignments: "]
    finals = []
    for i, (h, kids) in enumerate(zip(gs.heads, gs.kids, strict=True)):
        if h is None:
            finals.append(f"({i})")
            continue
        for index, kid in enumerate(kids):
            if kid is None:
                continue
            value = (1 if kid[0] == "snat" else 0) if index == 0 else index + 1
            lines.append(f"({i}) r{h[0]}->r{h[1]}:v{{v={value}}} ({kid[1]})")
    lines.append("Final states: " + ", ".join(finals))
    return "\n".join(lines) + "\n"


def _run(exe: Path, text: str, network: str, mode: str) -> dict[str, object]:
    """Run Sprout(A) on one protocol and network in one mode.  Return its verdict and the queries found valid.

    The front end writes its queries next to the protocol, so each run has
    its own temporary directory.  It runs each MuVal query under timeout(1),
    which kills the query's own process group when its budget ends.  A run
    that takes more than RUN_TIMEOUT seconds loses its process group.
    """
    with tempfile.TemporaryDirectory(prefix="session_oracle_sprout_") as tmp:
        Path(tmp, "protocol").write_text(text, encoding="utf-8")
        proc = subprocess.Popen([str(exe), "protocol", network, str(QUERY_TIMEOUT), mode, "parallel"], cwd=tmp,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
        try:
            out, err = proc.communicate(timeout=RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate()
            return {"verdict": "inconclusive", "valid": []}
    found = _RESULT.findall(out)
    if proc.returncode != 0 or not found:
        LOG.warning("sprout gave no verdict (exit %d): %s", proc.returncode, " ".join((out + err).split())[-300:])
        return {"verdict": "inconclusive", "valid": []}
    verdict = found[-1].strip()
    return {"verdict": verdict if verdict in VERDICTS else "inconclusive", "valid": sorted(_VALID.findall(out))}


def _check_controls(exe: Path) -> None:
    """Refuse every verdict unless each control gets its expected verdict on each network in each mode."""
    jobs = [(name, text, network, mode, want) for name, text, wants in CONTROLS for network, want in wants.items()
            for mode in MODES]
    with ThreadPoolExecutor(max_workers=len(jobs)) as pool:
        got = list(pool.map(lambda j: _run(exe, protocol_text(read_global(j[1])), j[2], j[3])["verdict"], jobs))
    wrong = [f"{name} on {network} in mode {mode}: {verdict}, not {want}"
             for (name, _, network, mode, want), verdict in zip(jobs, got, strict=True) if verdict != want]
    if wrong:
        raise OracleError("sprout: a control failed, so no verdict counts: " + "; ".join(wrong))


def combine(by_mode: dict[str, dict[str, object]]) -> dict[str, object]:
    """Join the answers of the modes into one answer.

    A decisive verdict (implementable, non-implementable, gclts-ineligible)
    counts when every decisive answer of the other modes agrees with it.
    Two decisive answers that differ give "modes-disagree", so one mode's
    wrong verdict is never a pass.  With no decisive answer, the verdict is
    inconclusive.  The valid queries of every mode are kept, with the mode
    as a prefix.
    """
    decisive = {str(a["verdict"]) for a in by_mode.values() if a["verdict"] != "inconclusive"}
    if len(decisive) > 1:
        verdict = "modes-disagree"
    elif decisive:
        verdict = decisive.pop()
    else:
        verdict = "inconclusive"
    valid = [f"{mode}:{q}" for mode, a in by_mode.items() for q in a["valid"]]  # type: ignore[attr-defined]
    return {"verdict": verdict, "valid": valid, "modes": {mode: a["verdict"] for mode, a in by_mode.items()}}


def decide(items: list[tuple[Global, tuple[str, ...]]], workers: int = WORKERS) -> list[dict[str, dict[str, object]]]:
    """Return, for each global type and its networks, the answer of Sprout(A) on each network.

    Each protocol runs in every mode of MODES, and combine joins the
    answers.  An answer holds the verdict, the queries that MuVal found
    valid, which name the coherence condition that fails, and the verdict of
    each mode.  Each answer of one mode is kept in a cache keyed by the
    build, the time budgets, the network, the mode and the protocol text.
    An inconclusive answer is kept too, so a run that needs more time than
    the budget does not run again until the budget changes, and it stays a
    gap.  A type with no tree gets the verdict "no-tree" on every network.
    """
    unknown = {n for _, ns in items for n in ns} - set(NETWORKS)
    if unknown:
        raise OracleError(f"sprout: unknown networks {sorted(unknown)}")
    exe, ident = build()
    _check_controls(exe)
    cache_path = answer_cache_path("sprout", hashlib.sha256(ident.encode()).hexdigest()[:16])
    cache = load_answers(cache_path)
    texts: list[str | None] = []
    for g, _ in items:
        try:
            texts.append(protocol_text(g))
        except Premise:
            texts.append(None)
    jobs = [(i, n, m) for i, t in enumerate(texts) if t is not None for n in items[i][1] for m in MODES]
    budget = f"{QUERY_TIMEOUT}/{RUN_TIMEOUT}"
    keys = {(i, n, m): hashlib.sha256(f"{ident}\n{budget}\n{n}\n{m}\n{texts[i]}".encode()).hexdigest()
            for i, n, m in jobs}
    todo = list(dict.fromkeys(keys[job] for job in jobs if keys[job] not in cache))
    by_key = {keys[job]: job for job in jobs}
    LOG.info("%d sprout runs: %d from the cache, %d distinct runs to do", len(jobs),
             sum(keys[job] in cache for job in jobs), len(todo))
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for done, (key, answer) in enumerate(zip(todo, pool.map(
                    lambda k: _run(exe, texts[by_key[k][0]], by_key[k][1], by_key[k][2]),  # type: ignore[arg-type]
                    todo), strict=True), start=1):
                cache[key] = answer
                if done % 25 == 0:
                    LOG.info("%d of %d sprout runs done", done, len(todo))
                    save_answers(cache_path, cache)
    finally:
        save_answers(cache_path, cache)
    no_tree = {"verdict": "no-tree", "valid": [], "modes": {}}
    return [{n: (no_tree if t is None else combine({m: cache[keys[(i, n, m)]] for m in MODES})) for n in items[i][1]}
            for i, t in enumerate(texts)]
