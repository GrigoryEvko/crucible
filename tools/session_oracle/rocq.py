"""Run the projection of Tirore, Bengtson and Carbone (ITP 2023) as an oracle.

The oracle is the computable projection ``proj`` in
``theories/Projection/indProj.v`` of github.com/Tirore96/projection.  The
repository has no licence, so nothing from it enters our tree.  This
module downloads the pinned commit as a tarball into a cache outside
the repository, builds it with the Rocq toolchain that is on PATH, and
evaluates ``proj`` with ``vm_compute`` in a generated driver file.

The driver encodes each result as a list of naturals, because the
printed form of a local type depends on notations that change between
Rocq releases.  The table of the encoding:

    rejected projection      [0]
    accepted projection      [1] ++ enc e
    enc (EVar n)             [0; n]
    enc EEnd                 [1]
    enc (EMsg d c u e)       [2; dir d; c; sort u] ++ enc e
    enc (EBranch d c es)     [3; dir d; c; size es] ++ enc e_1 ++ ...
    enc (ERec e)             [4] ++ enc e
    dir Sd = 0, dir Rd = 1;  sort nat = 0, sort bool = 1

The toolchain that the pinned commit builds with is Coq 8.15.2 with
mathcomp-ssreflect 1.x, Equations, deriving, paco and mczify.
"""

from __future__ import annotations

import io
import json
import logging
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from model import Global, Local, coq_global, decode_local

LOG = logging.getLogger("session_oracle.rocq")

PROJECTION_REPO = "Tirore96/projection"
PROJECTION_COMMIT = "fc0d3502f3e5658e3ef9f38eb5d2572271297409"
PROJECTION_TARBALL = (
    f"https://github.com/{PROJECTION_REPO}/archive/{PROJECTION_COMMIT}.tar.gz")


class OracleError(RuntimeError):
    """The oracle could not be fetched, built or run."""


class Timeout:
    """The answer of a query that did not finish within QUERY_TIMEOUT seconds.

    The decision procedure unfolds recursion to test projectability, and
    on some deep types it takes minutes.  Such a query has no answer, and
    the classifier records it as a gap, never as a rejection.
    """

    def __repr__(self) -> str:
        return "TIMEOUT"


TIMEOUT = Timeout()
CHUNK = 8
CHUNK_TIMEOUT = 240
QUERY_TIMEOUT = 90
WORKERS = 16


@dataclass(frozen=True, slots=True)
class Query:
    """One projection request: the global type of a case onto one role.

    ``chan`` is the channel specification of model.channel_table.
    """

    key: int
    g: Global
    role: int
    chan: str = "pair"


def cache_root() -> Path:
    """Return the cache directory, outside the repository.

    SESSION_ORACLE_CACHE overrides the default ~/.cache/crucible/session_oracle.
    """
    env = os.environ.get("SESSION_ORACLE_CACHE")
    if env:
        return Path(env)
    return Path.home() / ".cache" / "crucible" / "session_oracle"


def _tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise OracleError(
            f"{name} is not on PATH.  Install the Rocq toolchain (Coq 8.15.2 with "
            "coq-mathcomp-ssreflect, coq-equations, coq-deriving, coq-paco and "
            "coq-mathcomp-zify through opam) and put it on PATH, for example with "
            "`eval $(opam env --switch=sessoracle)`.")
    return path


def coq_version() -> str:
    """Return the version line of coqc on PATH."""
    out = subprocess.run([_tool("coqc"), "--version"], check=True,
                         capture_output=True, text=True).stdout
    return out.strip().splitlines()[0]


def fetch_projection() -> Path:
    """Download and unpack the pinned oracle commit.  Return its root.

    The download happens once.  A second call finds the unpacked tree.
    """
    root = cache_root() / f"projection-{PROJECTION_COMMIT}"
    if (root / "_CoqProject").is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    LOG.info("fetching %s", PROJECTION_TARBALL)
    try:
        with urllib.request.urlopen(PROJECTION_TARBALL, timeout=120) as resp:
            data = resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {PROJECTION_TARBALL}: {exc}") from exc
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        prefix = f"projection-{PROJECTION_COMMIT}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        if not members:
            raise OracleError(f"{PROJECTION_TARBALL}: no directory {prefix} in the archive")
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "_CoqProject").is_file():
        raise OracleError(f"{root}: the archive has no _CoqProject")
    return root


def _load_path(root: Path) -> list[str]:
    """Return the -Q mappings of the pinned commit's _CoqProject."""
    theories = root / "theories"
    return ["-Q", str(theories), "Proj",
            "-Q", str(theories / "IndTypes"), "IndTypes",
            "-Q", str(theories / "CoTypes"), "CoTypes",
            "-Q", str(theories / "Projection"), "Projection"]


_REQUIRE = re.compile(r"(?:From\s+(\w+)\s+)?Require\s+(?:Import|Export)\s+([\w.\s]+?)\.\s", re.S)


def _build_order(root: Path) -> list[Path]:
    """Order the oracle's sources so that each follows its dependencies.

    The _CoqProject maps ``theories`` to ``Proj`` and three of its
    subdirectories to their own roots.  The two mappings overlap, and
    coqdep then misses dependencies, so the order comes from the Require
    lines of the files themselves.  The Examples are left out because
    the oracle needs only indProj.  O(files × requires).
    """
    theories = root / "theories"
    files = sorted(p for p in theories.rglob("*.v") if "Examples" not in p.parts)
    names: dict[str, Path] = {}
    for path in files:
        rel = path.relative_to(theories).with_suffix("")
        names[".".join(("Proj",) + rel.parts)] = path
        if len(rel.parts) == 2:
            names[".".join(rel.parts)] = path
    deps: dict[Path, set[Path]] = {}
    for path in files:
        found: set[Path] = set()
        for match in _REQUIRE.finditer(path.read_text(encoding="utf-8")):
            prefix, mods = match.group(1), match.group(2).split()
            for mod in mods:
                full = f"{prefix}.{mod}" if prefix else mod
                if full in names and names[full] != path:
                    found.add(names[full])
        deps[path] = found
    order: list[Path] = []
    done: set[Path] = set()

    def visit(path: Path, stack: tuple[Path, ...]) -> None:
        if path in done:
            return
        if path in stack:
            raise OracleError(f"cyclic Require among the oracle sources at {path}")
        for dep in sorted(deps[path]):
            visit(dep, stack + (path,))
        done.add(path)
        order.append(path)

    for path in files:
        visit(path, ())
    return order


def build_projection(root: Path) -> None:
    """Compile the oracle development.  A second call is a no-op."""
    marker = root / "theories" / "Projection" / "indProj.vo"
    if marker.is_file():
        return
    LOG.info("building the oracle in %s", root)
    for path in _build_order(root):
        LOG.info("coqc %s", path.relative_to(root))
        proc = subprocess.run([_tool("coqc"), *_load_path(root), str(path)], cwd=root,
                              capture_output=True, text=True)
        if proc.returncode != 0:
            tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-30:])
            raise OracleError(f"building the oracle failed at {path}:\n{tail}")
    if not marker.is_file():
        raise OracleError(f"{marker} is missing after the build")


_DRIVER_HEAD = """\
From mathcomp Require Import all_ssreflect.
From IndTypes Require Import syntax.
From Projection Require Import indProj.

Definition so_dir (d : dir) : nat := if d is Sd then 0 else 1.

Definition so_sort (u : value) : nat :=
  match u with
  | VSeqSort (SNat :: nil) => 0
  | VSeqSort (SBool :: nil) => 1
  | _ => 9
  end.

Fixpoint so_enc (e : lType) : seq nat :=
  match e with
  | EVar n => [:: 0; n]
  | EEnd => [:: 1]
  | EMsg d c u e0 => [:: 2; so_dir d; nat_of_ch c; so_sort u] ++ so_enc e0
  | EBranch d c es => [:: 3; so_dir d; nat_of_ch c; size es] ++ flatten (map so_enc es)
  | ERec e0 => 4 :: so_enc e0
  end.

Definition so_opt (o : option lType) : seq nat :=
  if o is Some e then 1 :: so_enc e else [:: 0].

"""

_RESULT = re.compile(r"=\s*\(\s*(\d+)\s*,\s*\[::([^\]]*)\]\s*\)", re.S)


def driver_source(queries: list[Query]) -> str:
    """Return the Rocq source that evaluates every query.  O(total size)."""
    lines = [_DRIVER_HEAD]
    for q in queries:
        lines.append(
            f"Eval vm_compute in ({q.key}, so_opt "
            f"(proj {coq_global(q.g, q.chan)} (Ptcp {q.role}))).\n")
    return "".join(lines)


def _run_chunk(root: Path, work: Path, chunk: list[Query],
               timeout: int) -> dict[int, list[int]] | None:
    """Evaluate one chunk in its own coqc process.  Return None on a timeout.

    The result maps each query key to the nat-list encoding of its answer.
    """
    with tempfile.TemporaryDirectory(dir=work) as tmp:
        driver = Path(tmp) / f"SessionOracleDriver{chunk[0].key}.v"
        driver.write_text(driver_source(chunk), encoding="utf-8")
        try:
            proc = subprocess.run([_tool("coqc"), *_load_path(root), str(driver)], cwd=tmp,
                                  capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None
    if proc.returncode != 0:
        raise OracleError(f"coqc failed on a driver chunk:\n{proc.stdout[-4000:]}\n"
                          f"{proc.stderr[-4000:]}")
    results: dict[int, list[int]] = {}
    for match in _RESULT.finditer(proc.stdout):
        key = int(match.group(1))
        codes = [int(tok) for tok in match.group(2).replace("\n", " ").split(";") if tok.strip()]
        if key in results:
            raise OracleError(f"oracle output names query {key} twice")
        results[key] = codes
    missing = [q.key for q in chunk if q.key not in results]
    if missing:
        raise OracleError(f"oracle output lacks {len(missing)} queries, first {missing[:5]}")
    return results


def _answer_cache_path() -> Path:
    return cache_root() / f"answers-{PROJECTION_COMMIT[:12]}.json"


def _load_answers() -> dict[str, list[int] | str]:
    path = _answer_cache_path()
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        LOG.warning("ignoring the unreadable answer cache %s: %s", path, exc)
        return {}
    return data if isinstance(data, dict) else {}


def _save_answers(cache: dict[str, list[int] | str]) -> None:
    path = _answer_cache_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".answers-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(cache, handle, sort_keys=True)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def _cache_key(q: Query) -> str:
    return f"{coq_global(q.g, q.chan)}|{q.role}"


def run(queries: list[Query], workers: int = WORKERS) -> dict[int, Local | None | Timeout]:
    """Evaluate every query with the oracle and decode the results.

    Returns a map from query key to the oracle's local type, None when
    the oracle rejects the projection, or TIMEOUT.  An answer that the
    cache in cache_root() holds is not asked again; the cache is keyed by
    the Rocq term and the role, and its file name carries the oracle
    commit.  The other queries run in chunks of CHUNK on ``workers``
    coqc processes.  A chunk that times out runs again one query at a
    time, so one slow query costs only its own answer.  Raises
    OracleError when coqc fails or a key is missing, so a silent partial
    answer cannot reach the golden file.
    """
    root = fetch_projection()
    build_projection(root)
    work = cache_root() / "work"
    work.mkdir(parents=True, exist_ok=True)
    cache = _load_answers()
    fresh = [q for q in queries if _cache_key(q) not in cache]
    chunks = [fresh[i:i + CHUNK] for i in range(0, len(fresh), CHUNK)]
    LOG.info("%d oracle queries: %d from the cache, %d in %d chunks", len(queries),
             len(queries) - len(fresh), len(fresh), len(chunks))
    retry: list[Query] = []
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for chunk, answer in zip(chunks, pool.map(
                    lambda c: _run_chunk(root, work, c, CHUNK_TIMEOUT), chunks), strict=True):
                if answer is None:
                    retry += chunk
                else:
                    for q in chunk:
                        cache[_cache_key(q)] = answer[q.key]
            if retry:
                LOG.info("%d queries timed out in a chunk; running them one at a time", len(retry))
                for q, answer in zip(retry, pool.map(
                        lambda q: _run_chunk(root, work, [q], QUERY_TIMEOUT), retry), strict=True):
                    cache[_cache_key(q)] = "timeout" if answer is None else answer[q.key]
    finally:
        _save_answers(cache)
    results: dict[int, Local | None | Timeout] = {}
    for q in queries:
        codes = cache[_cache_key(q)]
        results[q.key] = TIMEOUT if codes == "timeout" else decode_local(codes)  # type: ignore[arg-type]
    timeouts = sum(1 for v in results.values() if v is TIMEOUT)
    if timeouts:
        LOG.info("%d queries have no answer within %d s", timeouts, QUERY_TIMEOUT)
    return results
