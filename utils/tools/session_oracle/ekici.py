"""Check subtyping verdicts against the Rocq relation of Ekici (ITP 2025).

The development is github.com/Apiros3/smpst-sr-smer, which accompanies
"Formalising Subject Reduction and Progress for Multiparty Session
Processes" (ITP 2025).  Its README states Coq 8.20, mathcomp-ssreflect and
paco.  It has no licence, so nothing from it enters our tree, and only the
verdicts do.  Its relation subtypeC (src/local.v) is the
greatest fixed point of the synchronous subtyping rules over coinductive
local type trees whose branches are option lists indexed by label:

  sub_out  a send of the subtype sends a subset of the labels of the
           supertype, covariant in the sort (wfsend)
  sub_in   a receive of the subtype receives a superset of the labels,
           contravariant in the sort (wfrec)

The relation is not computable: it is a Prop.  So this module writes a
certificate for each pair and lets coqc check it against the development's
own definition.  For a pair that the pairing of the rules relates, the
certificate is a coinductive proof (paco) over the finite set of pairs of
states that the rules reach.  For a pair that the pairing refutes, it is a
proof of the negation along the path that reaches the failed pair.  A
certificate that coqc refuses is a failure of this module, never a verdict.

The encoding of a binary local type (model.py, after execution.head):

  label "v" (a value message)   index 0
  label "k<n>" (a keyed label)  index n
  label k (a positional branch) index 64 + k
  sort nat -> snat, bool and unit -> sbool

A keyed step (model.LChoice.step) is a choice of one branch, as in the
development, where every action is a choice.
"""

from __future__ import annotations

import hashlib
import io
import logging
import os
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from execution import _SPIN, head
from model import LChoice, LEnd, Local
from rocq import OracleError, answer_cache_path, cache_root, load_answers, save_answers

LOG = logging.getLogger("session_oracle.ekici")

REPO = "Apiros3/smpst-sr-smer"
COMMIT = "583edde920ef3833382ff6c9130f0d20db90744b"
SOURCES = ("src/sim.v", "src/header.v", "src/expr.v", "src/local.v")
POSITIONAL_BASE = 64
CHUNK = 12
CHUNK_TIMEOUT = 600


def coqc() -> list[str]:
    """Return the coqc command of the Coq 8.20 toolchain that the development builds with.

    SESSION_ORACLE_COQC20 names it.  The default runs coqc in the opam
    switch sessoracle20 (utils/scripts/session-oracle.sh says how to make it).
    """
    env = os.environ.get("SESSION_ORACLE_COQC20")
    if env:
        return shlex.split(env)
    opam = shutil.which("opam") or str(Path.home() / ".local" / "bin" / "opam")
    if not Path(opam).is_file() and shutil.which(opam) is None:
        raise OracleError("opam is not on PATH, and SESSION_ORACLE_COQC20 is not set.  The ITP 2025 development "
                          "needs Coq 8.20 with mathcomp-ssreflect 2.3 and paco; utils/scripts/session-oracle.sh says "
                          "how to install them.")
    return [opam, "exec", "--switch=sessoracle20", "--", "coqc"]


def fetch() -> Path:
    """Download and unpack the pinned commit.  Return its root."""
    root = cache_root() / f"smpst-sr-smer-{COMMIT}"
    if (root / "_CoqProject").is_file():
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
        prefix = f"smpst-sr-smer-{COMMIT}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "_CoqProject").is_file():
        raise OracleError(f"{root}: the archive has no _CoqProject")
    return root


def build(root: Path) -> None:
    """Compile the files that the relation needs.  A second call is a no-op."""
    if (root / "src" / "local.vo").is_file():
        return
    for source in SOURCES:
        LOG.info("coqc %s", source)
        proc = subprocess.run([*coqc(), "-R", str(root), "SST", str(root / source)], cwd=root,
                              capture_output=True, text=True)
        if proc.returncode != 0:
            raise OracleError(f"building the ITP 2025 development failed at {source}:\n"
                              f"{(proc.stdout + proc.stderr)[-3000:]}")


# ── Certificates ─────────────────────────────────────────────────────


def _index(label: str | int) -> int:
    if label == "v":
        return 0
    if isinstance(label, str) and label.startswith("k") and label[1:].isdigit():
        return int(label[1:])
    if isinstance(label, int):
        return POSITIONAL_BASE + label
    raise OracleError(f"ekici: no label index for {label!r}")


def _sort(sort: str) -> str:
    return {"nat": "snat", "bool": "sbool", "unit": "sbool"}[sort]


def _subsort(sub: str, sup: str) -> bool:
    """The development's subsort: reflexive, and snat below sint (which no corpus type uses)."""
    return _sort(sub) == _sort(sup)


class _States:
    """The reachable head forms of one local type, each named by a Rocq constant."""

    def __init__(self, prefix: str, start: Local) -> None:
        self.prefix = prefix
        self.names: dict[Local, str] = {}
        self.order: list[Local] = []
        self.root = self.name(head(start))

    def name(self, state: Local) -> str:
        if state is _SPIN:
            raise OracleError("ekici: a loop that never acts has no tree")
        if state not in self.names:
            self.names[state] = f"{self.prefix}{len(self.order)}"
            self.order.append(state)
            if isinstance(state, LChoice):
                for _, _, cont in state.branches:
                    self.name(head(cont))
        return self.names[state]

    def children(self, state: Local) -> dict[int, tuple[str, str]]:
        """Map each label index of a choice state to its sort and the name of its continuation."""
        assert isinstance(state, LChoice)
        return {_index(lab): (sort, self.names[head(cont)]) for lab, sort, cont in state.branches}

    def body(self, state: Local) -> str:
        """Return the constructor form of one state, with its continuations named."""
        if isinstance(state, LEnd):
            return "ltt_end"
        assert isinstance(state, LChoice)
        slots = self.children(state)
        width = max(slots, default=-1) + 1
        items = "; ".join(f"Some ({_sort(slots[i][0])}, {slots[i][1]})" if i in slots else "None"
                          for i in range(width))
        return f"{'ltt_send' if state.send else 'ltt_recv'} \"p\" [{items}]"

    def definitions(self) -> str:
        """Return one mutual CoFixpoint for every state."""
        bodies = [f"{self.names[s]} : ltt := {self.body(s)}" for s in self.order]
        return "CoFixpoint " + "\nwith ".join(bodies) + ".\n"

    def unfolding_lemmas(self) -> str:
        """Return a lemma for each state that unfolds its constant one step (ltt_eq of src/local.v)."""
        return "".join(f"Lemma unf_{self.names[s]} : {self.names[s]} = {self.body(s)}.\n"
                       f"Proof. rewrite [LHS]ltt_eq; reflexivity. Qed.\n" for s in self.order)


def _pairing(t: _States, u: _States) -> tuple[list[tuple[str, str]], list[tuple[str, str]] | None, str]:
    """Walk the pairs of states that the rules reach from the two roots.

    Returns the reached pairs, the path to a failed pair or None, and the
    reason of the failure.  O(states of T × states of U).
    """
    by_name_t = {v: k for k, v in t.names.items()}
    by_name_u = {v: k for k, v in u.names.items()}
    start = (t.root, u.root)
    parent: dict[tuple[str, str], tuple[str, str] | None] = {start: None}
    todo: deque[tuple[str, str]] = deque([start])

    def path_to(pair: tuple[str, str]) -> list[tuple[str, str]]:
        out = []
        cur: tuple[str, str] | None = pair
        while cur is not None:
            out.append(cur)
            cur = parent[cur]
        return list(reversed(out))

    while todo:
        pair = todo.popleft()
        a, b = by_name_t[pair[0]], by_name_u[pair[1]]
        if isinstance(a, LEnd) or isinstance(b, LEnd):
            if not (isinstance(a, LEnd) and isinstance(b, LEnd)):
                return list(parent), path_to(pair), "an end against an action"
            continue
        assert isinstance(a, LChoice) and isinstance(b, LChoice)
        if a.send != b.send:
            return list(parent), path_to(pair), "a send against a receive"
        ca, cb = t.children(a), u.children(b)
        labels = sorted(ca) if a.send else sorted(cb)
        for index in labels:
            if index not in ca or index not in cb:
                return list(parent), path_to(pair), f"the label index {index} is missing on one side"
            sort_ok = _subsort(ca[index][0], cb[index][0]) if a.send else _subsort(cb[index][0], ca[index][0])
            if not sort_ok:
                return list(parent), path_to(pair), f"the sort of label index {index} is not below"
            child = (ca[index][1], cb[index][1])
            if child not in parent:
                parent[child] = pair
                todo.append(child)
    return list(parent), None, ""


_HEAD = """\
From mathcomp Require Import ssreflect.seq all_ssreflect.
From Paco Require Import paco.
Require Import List String.
Import ListNotations.
From SST Require Import src.header src.expr src.local.
Open Scope string_scope.

Ltac so_member := simpl; repeat (first [left; reflexivity | right]).
Ltac so_step CIH :=
  pfold; constructor; cbn [wfsend wfrec];
  repeat split;
  repeat match goal with
         | |- upaco2 _ _ _ _ => right; apply CIH; so_member
         | |- subsort _ _ => constructor
         end.

"""


def certificate(key: str, t: Local, u: Local) -> tuple[str, bool, str]:
    """Return the Rocq certificate for the pair (T, U), the verdict it proves, and the failure reason.

    The certificate proves ``subtypeC t u`` or its negation, under the
    names of ``key``.
    """
    st, su = _States(f"{key}_t", t), _States(f"{key}_u", u)
    pairs, path, reason = _pairing(st, su)
    text = [f"Module {key}.\n", st.definitions(), su.definitions(), st.unfolding_lemmas(), su.unfolding_lemmas()]
    if path is None:
        listed = "; ".join(f"({a}, {b})" for a, b in pairs)
        cases = "".join(f"    destruct Hin as [Hin | Hin].\n"
                        f"    {{ injection Hin as <- <-. rewrite unf_{a} unf_{b}; so_step CIH. }}\n"
                        for a, b in pairs)
        text.append(f"Lemma verdict : subtypeC {st.root} {su.root}.\nProof.\n"
                    f"  assert (H : forall a b, In (a, b) [{listed}] -> paco2 subtype bot2 a b).\n"
                    f"  {{ pcofix CIH. intros a b Hin. simpl in Hin.\n{cases}    contradiction. }}\n"
                    f"  apply H. so_member.\nQed.\n")
        verdict = True
    else:
        # The path hypotheses are named Hp<depth>, because the destruct of
        # the conjunctions names its hypotheses H, H0, H1 and so on.
        steps = []
        for depth, (a, b) in enumerate(path):
            steps.append(f"  punfold Hp{depth}; [|apply sub_mon]. rewrite unf_{a} unf_{b} in Hp{depth}.\n"
                         f"  inversion Hp{depth}; subst; clear Hp{depth}; cbn [wfsend wfrec] in *;\n"
                         f"  repeat match goal with Hc : _ /\\ _ |- _ => destruct Hc end;\n"
                         f"  try contradiction; try (match goal with Hs : subsort _ _ |- _ => "
                         f"solve [inversion Hs] end).\n")
            if depth + 1 < len(path):
                c, d = path[depth + 1]
                # The right disjunct is bot2, which is False only up to
                # conversion, so contradiction does not see it.
                steps.append(f"  match goal with Hn : upaco2 _ _ {c} {d} |- _ => "
                             f"destruct Hn as [Hp{depth + 1}|Hp{depth + 1}]; [|exfalso; exact Hp{depth + 1}] end.\n")
        text.append(f"Lemma verdict : ~ subtypeC {st.root} {su.root}.\nProof.\n  intro Hp0.\n"
                    + "".join(steps) + "Qed.\n")
        verdict = False
    text.append(f"End {key}.\n")
    return "".join(text), verdict, reason


def _check(root: Path, blocks: list[str]) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="session_oracle_ekici_") as tmp:
        path = Path(tmp) / "EkiciCertificates.v"
        path.write_text(_HEAD + "".join(blocks), encoding="utf-8")
        try:
            return subprocess.run([*coqc(), "-R", str(root), "SST", str(path)], cwd=tmp, capture_output=True,
                                  text=True, timeout=CHUNK_TIMEOUT)
        except subprocess.TimeoutExpired as exc:
            return subprocess.CompletedProcess(exc.cmd, 124, "", f"timeout after {CHUNK_TIMEOUT} s")


def coq_version() -> str:
    """Return the version line of the coqc that checks the certificates."""
    out = subprocess.run([*coqc(), "--version"], check=True, capture_output=True, text=True).stdout
    return out.strip().splitlines()[0]


def _cache_key(t: Local, u: Local) -> str:
    """Return the key of the verdict of (T, U): a hash of the certificate text under a fixed name.

    The key changes when the certificate generator changes, so an answer
    that coqc checked for an older certificate is never reused.
    """
    return hashlib.sha256((_HEAD + certificate("C", t, u)[0]).encode()).hexdigest()


def decide(pairs: list[tuple[Local, Local]], workers: int = 16) -> list[tuple[bool | None, str]]:
    """Return, for each pair (T, U), the verdict that coqc checked and a note.

    A verdict is True (subtypeC T U proved), False (its negation proved) or
    None (coqc refused the certificate, which the note gives).  A verdict
    that coqc checked is kept in a cache whose file name carries the
    commit.  The other certificates run in chunks of CHUNK on ``workers``
    coqc processes, and a chunk that fails runs again one certificate at a
    time.  O(pairs × states²) to write the certificates.
    """
    root = fetch()
    build(root)
    cache_path = answer_cache_path("ekici", COMMIT)
    cache = load_answers(cache_path)
    certs = []
    keys: list[str] = []
    for i, (t, u) in enumerate(pairs):
        try:
            certs.append(certificate(f"C{i}", t, u))
            keys.append(_cache_key(t, u))
        except OracleError as exc:
            certs.append(("", None, str(exc)))  # type: ignore[arg-type]
            keys.append("")
    results: dict[int, tuple[bool | None, str]] = {
        i: (bool(cache[k][0]), str(cache[k][1])) for i, k in enumerate(keys) if k and k in cache}
    indices = [i for i, c in enumerate(certs) if c[0] and i not in results]
    chunks = [indices[i:i + CHUNK] for i in range(0, len(indices), CHUNK)]
    LOG.info("%d certificates: %d from the cache, %d in %d chunks", len(pairs), len(results),
             len(indices), len(chunks))
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            outcomes = list(pool.map(lambda ch: _check(root, [certs[i][0] for i in ch]), chunks))
            retry: list[int] = []
            for chunk, proc in zip(chunks, outcomes, strict=True):
                if proc.returncode == 0:
                    for i in chunk:
                        results[i] = (certs[i][1], certs[i][2])
                        cache[keys[i]] = [certs[i][1], certs[i][2]]
                else:
                    retry += chunk
            singles = list(pool.map(lambda i: (i, _check(root, [certs[i][0]])), retry))
        for i, proc in singles:
            if proc.returncode == 0:
                results[i] = (certs[i][1], certs[i][2])
                cache[keys[i]] = [certs[i][1], certs[i][2]]
            else:
                results[i] = (None, "coqc refused the certificate: "
                                    + " ".join((proc.stdout + proc.stderr).split())[-300:])
    finally:
        save_answers(cache_path, cache)
    for i, c in enumerate(certs):
        if not c[0]:
            results[i] = (None, c[2])
    return [results[i] for i in range(len(pairs))]
