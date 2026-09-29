"""Run published Rocq developments as oracles.

Each development is a Development: a pinned commit of a repository, the
build of the files that the oracle needs, and a driver head that defines
the encoders of the answers.  A query is a Rocq expression of type
(nat * seq nat).  The driver evaluates each query with ``vm_compute`` and
prints the pair.  The first component is the key of the query, and the
second is the answer as a list of naturals, because the printed form of a
Rocq term depends on notations that change between releases.

None of the repositories has an Extraction command, and two of them have
no licence, so nothing from them enters our tree.  This module downloads
each pinned commit as a tarball into a cache outside the repository,
builds it there, and keeps the answers in a cache whose file name carries
the commit.

The developments:

  PROJECTION  the computable projection of Tirore, Bengtson and Carbone
              (ITP 2023), theories/Projection/indProj.v of
              github.com/Tirore96/projection.  No licence.  The encoding:

                rejected projection      [0]
                accepted projection      [1] ++ enc e
                enc (EVar n)             [0; n]
                enc EEnd                 [1]
                enc (EMsg d c u e)       [2; dir d; c; sort u] ++ enc e
                enc (EBranch d c es)     [3; dir d; c; size es] ++ enc e_1 ++ ...
                enc (ERec e)             [4] ++ enc e
                dir Sd = 0, dir Rd = 1;  sort nat = 0, sort bool = 1

  SUBJECT_REDUCTION
              the development of Tirore, Bengtson and Carbone (ECOOP
              2025), branch ECOOP2025 of github.com/Tirore96/subject_reduction,
              MIT licence.  Its branches carry labels.  sr.py holds its
              queries and its encoding.

The toolchain that both commits build with is Coq 8.15 with
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
from typing import Any, Callable

from model import Global, Local, coq_global, decode_local

LOG = logging.getLogger("session_oracle.rocq")


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


# ── Developments ─────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Development:
    """One pinned Rocq development and how to build and query it.

    ``load_path`` maps the unpacked root to the -Q and -R arguments of
    coqc.  ``build`` compiles the files that the driver needs, and a
    second call is a no-op.  ``driver_head`` is the Rocq text before the
    queries.
    """

    name: str
    repo: str
    commit: str
    load_path: Callable[[Path], list[str]]
    build: Callable[["Development", Path], None]
    driver_head: str

    @property
    def tarball(self) -> str:
        """The URL of the pinned commit's archive."""
        return f"https://github.com/{self.repo}/archive/{self.commit}.tar.gz"

    @property
    def dirname(self) -> str:
        """The directory that the archive unpacks into."""
        return f"{self.repo.split('/')[1]}-{self.commit}"


def fetch(dev: Development) -> Path:
    """Download and unpack the pinned commit of ``dev``.  Return its root.

    The download happens once.  A second call finds the unpacked tree.
    """
    root = cache_root() / dev.dirname
    if (root / "_CoqProject").is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    LOG.info("fetching %s", dev.tarball)
    try:
        with urllib.request.urlopen(dev.tarball, timeout=120) as resp:
            data = resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {dev.tarball}: {exc}") from exc
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        prefix = f"{dev.dirname}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        if not members:
            raise OracleError(f"{dev.tarball}: no directory {prefix} in the archive")
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "_CoqProject").is_file():
        raise OracleError(f"{root}: the archive has no _CoqProject")
    return root


_REQUIRE = re.compile(r"(?:From\s+(\w+)\s+)?Require\s+(?:Import|Export)\s+([\w.\s]+?)\.\s", re.S)


def _build_order(root: Path, prefix: str, subroots: bool,
                 targets: tuple[str, ...] | None = None) -> list[Path]:
    """Order the sources of ``root``/theories so that each follows its dependencies.

    The logical prefix of ``theories`` is ``prefix``.  With ``subroots``
    each subdirectory of ``theories`` is also a root of its own, as in the
    _CoqProject of the projection oracle.  The two mappings overlap, and
    coqdep then misses dependencies, so the order comes from the Require
    lines of the files themselves.  The Examples are left out, because
    the oracles need only the decision procedures.  With ``targets``
    (paths relative to ``root``) the order holds only those files and
    what they require.  O(files × requires).
    """
    theories = root / "theories"
    files = sorted(p for p in theories.rglob("*.v") if "Examples" not in p.parts)
    names: dict[str, Path] = {}
    for path in files:
        rel = path.relative_to(theories).with_suffix("")
        names[".".join((prefix,) + rel.parts)] = path
        if subroots and len(rel.parts) == 2:
            names[".".join(rel.parts)] = path
    deps: dict[Path, set[Path]] = {}
    for path in files:
        found: set[Path] = set()
        for match in _REQUIRE.finditer(path.read_text(encoding="utf-8")):
            head, mods = match.group(1), match.group(2).split()
            for mod in mods:
                full = f"{head}.{mod}" if head else mod
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

    for path in ([root / t for t in targets] if targets is not None else files):
        visit(path, ())
    return order


def _build_sources(dev: Development, root: Path, prefix: str, subroots: bool, marker: str,
                   targets: tuple[str, ...] | None = None) -> None:
    """Compile the sources of ``root`` in dependency order.  A second call is a no-op.

    ``targets`` limits the build to those files and what they require.
    """
    stamp = root / marker
    if stamp.is_file():
        return
    LOG.info("building %s in %s", dev.name, root)
    for path in _build_order(root, prefix, subroots, targets):
        LOG.info("coqc %s", path.relative_to(root))
        proc = subprocess.run([_tool("coqc"), *dev.load_path(root), str(path)], cwd=root,
                              capture_output=True, text=True)
        if proc.returncode != 0:
            tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-30:])
            raise OracleError(f"building {dev.name} failed at {path}:\n{tail}")
    if not stamp.is_file():
        raise OracleError(f"{stamp} is missing after the build")


def _projection_load_path(root: Path) -> list[str]:
    theories = root / "theories"
    return ["-Q", str(theories), "Proj",
            "-Q", str(theories / "IndTypes"), "IndTypes",
            "-Q", str(theories / "CoTypes"), "CoTypes",
            "-Q", str(theories / "Projection"), "Projection"]


def _build_projection(dev: Development, root: Path) -> None:
    _build_sources(dev, root, "Proj", True, "theories/Projection/indProj.vo")


_PROJECTION_HEAD = """\
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

PROJECTION = Development(
    name="projection",
    repo="Tirore96/projection",
    commit="fc0d3502f3e5658e3ef9f38eb5d2572271297409",
    load_path=_projection_load_path,
    build=_build_projection,
    driver_head=_PROJECTION_HEAD,
)


# ── Queries ──────────────────────────────────────────────────────────

_RESULT = re.compile(r"=\s*\(\s*(\d+)\s*,\s*\[::([^\]]*)\]\s*\)", re.S)


def _run_chunk(dev: Development, root: Path, work: Path, chunk: list[tuple[int, str]],
               timeout: int) -> dict[int, list[int]] | None:
    """Evaluate one chunk of (key, expression) in its own coqc process.  None on a timeout."""
    with tempfile.TemporaryDirectory(dir=work) as tmp:
        driver = Path(tmp) / f"SessionOracleDriver{chunk[0][0]}.v"
        driver.write_text(dev.driver_head + "".join(
            f"Eval vm_compute in ({key}, {expr}).\n" for key, expr in chunk), encoding="utf-8")
        try:
            proc = subprocess.run([_tool("coqc"), *dev.load_path(root), str(driver)], cwd=tmp,
                                  capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None
    if proc.returncode != 0:
        raise OracleError(f"coqc failed on a {dev.name} driver chunk:\n{proc.stdout[-4000:]}\n"
                          f"{proc.stderr[-4000:]}")
    results: dict[int, list[int]] = {}
    for match in _RESULT.finditer(proc.stdout):
        key = int(match.group(1))
        codes = [int(tok) for tok in match.group(2).replace("\n", " ").split(";") if tok.strip()]
        if key in results:
            raise OracleError(f"{dev.name} output names query {key} twice")
        results[key] = codes
    missing = [key for key, _ in chunk if key not in results]
    if missing:
        raise OracleError(f"{dev.name} output lacks {len(missing)} queries, first {missing[:5]}")
    return results


def answer_cache_path(name: str, commit: str) -> Path:
    """Return the answer cache of the development ``name`` at ``commit``."""
    return cache_root() / f"answers-{name}-{commit[:12]}.json"


def load_answers(path: Path) -> dict[str, Any]:
    """Return the answer cache at ``path``, or an empty one when it is missing or unreadable."""
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        LOG.warning("ignoring the unreadable answer cache %s: %s", path, exc)
        return {}
    return data if isinstance(data, dict) else {}


def save_answers(path: Path, cache: dict[str, Any]) -> None:
    """Write the answer cache to ``path`` in one rename, so a reader never sees half of it."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".answers-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(cache, handle, sort_keys=True)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def evaluate(dev: Development, exprs: list[str], cache_keys: list[str],
             workers: int = WORKERS) -> list[list[int] | Timeout]:
    """Evaluate each expression of ``dev`` and return the encoded answers in order.

    An answer that the cache holds under its cache key is not asked
    again.  The other expressions run in chunks of CHUNK on ``workers``
    coqc processes.  A chunk that times out runs again one expression at
    a time, so one slow query costs only its own answer.  Raises
    OracleError when coqc fails or a key is missing, so a silent partial
    answer cannot reach the golden file.
    """
    root = fetch(dev)
    dev.build(dev, root)
    work = cache_root() / "work"
    work.mkdir(parents=True, exist_ok=True)
    cache_path = answer_cache_path(dev.name, dev.commit)
    cache = load_answers(cache_path)
    fresh =[i for i, k in enumerate(cache_keys) if k not in cache]
    unique: dict[str, int] = {}
    for i in fresh:
        unique.setdefault(cache_keys[i], i)
    todo = [(i, exprs[i]) for i in unique.values()]
    chunks = [todo[i:i + CHUNK] for i in range(0, len(todo), CHUNK)]
    LOG.info("%d %s queries: %d from the cache, %d in %d chunks", len(exprs), dev.name,
             len(exprs) - len(fresh), len(todo), len(chunks))
    retry: list[tuple[int, str]] = []
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for chunk, answer in zip(chunks, pool.map(
                    lambda c: _run_chunk(dev, root, work, c, CHUNK_TIMEOUT), chunks), strict=True):
                if answer is None:
                    retry += chunk
                else:
                    for i, _ in chunk:
                        cache[cache_keys[i]] = answer[i]
            if retry:
                LOG.info("%d %s queries timed out in a chunk; running them one at a time",
                         len(retry), dev.name)
                for item, answer in zip(retry, pool.map(
                        lambda q: _run_chunk(dev, root, work, [q], QUERY_TIMEOUT), retry), strict=True):
                    cache[cache_keys[item[0]]] = "timeout" if answer is None else answer[item[0]]
    finally:
        save_answers(cache_path, cache)
    return [TIMEOUT if cache[k] == "timeout" else cache[k] for k in cache_keys]  # type: ignore[misc]


# ── The projection oracle ────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Query:
    """One projection request: the global type of a case onto one role.

    ``chan`` is the channel specification of model.channel_table.
    """

    key: int
    g: Global
    role: int
    chan: str = "pair"


def _cache_key(q: Query) -> str:
    return f"{coq_global(q.g, q.chan)}|{q.role}"


def run(queries: list[Query], workers: int = WORKERS) -> dict[int, Local | None | Timeout]:
    """Evaluate every projection query with the oracle and decode the results.

    Returns a map from query key to the oracle's local type, None when
    the oracle rejects the projection, or TIMEOUT.
    """
    exprs = [f"so_opt (proj {coq_global(q.g, q.chan)} (Ptcp {q.role}))" for q in queries]
    answers = evaluate(PROJECTION, exprs, [_cache_key(q) for q in queries], workers)
    results: dict[int, Local | None | Timeout] = {}
    for q, codes in zip(queries, answers, strict=True):
        results[q.key] = TIMEOUT if codes is TIMEOUT else decode_local(codes)  # type: ignore[arg-type]
    timeouts = sum(1 for v in results.values() if v is TIMEOUT)
    if timeouts:
        LOG.info("%d queries have no answer within %d s", timeouts, QUERY_TIMEOUT)
    return results
