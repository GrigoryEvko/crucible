"""Check implementability on three kinds of network with Sprout(A) (Li and Wies, PLDI 2026).

"Implementability of Global Distributed Protocols Modulo Network Architectures"
(Elaine Li and Thomas Wies, PACMPL 10, PLDI 2026, doi 10.1145/3808319)
characterises the implementability of a global protocol for each kind of
network, by coherence conditions that depend on how one message buffer
inserts and removes.  Their tool Sprout(A) decides it with the muCLP solver
MuVal.  The artifact (doi 10.5281/zenodo.19600644, CC BY 4.0) is a container
image, sprout-a:latest, which this module runs with podman.  Three of their
networks match our substrates:

  p2pbox   one FIFO queue for each ordered pair of roles   an SPSC channel for each pair
  mailbox  one FIFO queue for each receiver                 an MPSC channel for each receiver
  bag      one unordered buffer for each receiver           an MPMC queue with no order

The input of Sprout(A) is a finite state machine of global transitions.
Each state of the global type (keskin.GlobalStates) is a state of the
machine, and each branch is a transition p->q:v{v=n}, where the number n
names the label:

  a value of sort bool   0
  a value of sort nat    1
  the branch k           k + 2

The final states are the ends.  A verdict that the tool does not give
(a timeout or a parse error) is inconclusive, never a verdict.
"""

from __future__ import annotations

import hashlib
import logging
import os
import re
import shutil
import subprocess
import uuid
from concurrent.futures import ThreadPoolExecutor

from keskin import GlobalStates, Premise
from model import Global
from rocq import OracleError, answer_cache_path, load_answers, save_answers

LOG = logging.getLogger("session_oracle.sprout")

ARTIFACT = "10.5281/zenodo.19600644"
IMAGE = "sprout-a:latest"
NETWORKS = ("p2pbox", "mailbox", "bag")
QUERY_TIMEOUT = 60
RUN_TIMEOUT = 900
WORKERS = 24
VERDICTS = ("implementable", "non-implementable", "inconclusive")
_RESULT = re.compile(r"^Total verification time: [0-9.]+s, (.+)$", re.MULTILINE)


def _podman() -> str:
    tool = shutil.which("podman")
    if tool is None:
        raise OracleError("podman is not on PATH.  Sprout(A) runs from the container image of its artifact "
                          f"(doi {ARTIFACT}).  scripts/session-oracle.sh says how to load it.")
    return tool


def image_id() -> str:
    """Return the identifier of the loaded image, or raise when it is not loaded."""
    proc = subprocess.run([_podman(), "image", "inspect", "--format", "{{.Id}} {{.Architecture}}", IMAGE],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise OracleError(f"the image {IMAGE} is not loaded.  Download sprout-a.tar from doi {ARTIFACT} and "
                          "run: podman load -i sprout-a.tar")
    return proc.stdout.strip()


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


def _run(text: str, network: str, platform: str) -> str:
    """Run Sprout(A) on one protocol and network.  Return its verdict.

    The protocol goes in on standard input, so the container needs no mount.
    Podman stops the container after RUN_TIMEOUT seconds, and a client that
    waits longer removes it by name, so no container outlives its run.
    """
    name = f"session_oracle_sprout_{uuid.uuid4().hex}"
    script = ("cat > ../examples/protocol && "
              f"./_build/default/main.exe ../examples/protocol {network} {QUERY_TIMEOUT} opt parallel")
    try:
        proc = subprocess.run([_podman(), "run", "--rm", "-i", f"--name={name}", f"--timeout={RUN_TIMEOUT}",
                               "--network=none", f"--platform={platform}", IMAGE, "sh", "-c", script],
                              input=text, capture_output=True, text=True, timeout=RUN_TIMEOUT + 60)
    except subprocess.TimeoutExpired:
        subprocess.run([_podman(), "rm", "-f", name], capture_output=True)
        return "inconclusive"
    found = _RESULT.findall(proc.stdout)
    if proc.returncode != 0 or not found:
        LOG.warning("sprout gave no verdict (exit %d): %s", proc.returncode,
                    " ".join((proc.stdout + proc.stderr).split())[-300:])
        return "inconclusive"
    verdict = found[-1].strip()
    return verdict if verdict in VERDICTS else "inconclusive"


def decide(items: list[tuple[Global, tuple[str, ...]]], workers: int = WORKERS) -> list[dict[str, str]]:
    """Return, for each global type and its networks, the verdict of Sprout(A) on each network.

    Each verdict is kept in a cache keyed by the image, the time budgets, the
    network and the protocol text.  An inconclusive verdict is kept too, so a
    run that needs more time than the budget does not run again until the
    budget changes, and it stays a gap.  A type with no tree gets "no-tree" on
    every network.
    """
    unknown = {n for _, ns in items for n in ns} - set(NETWORKS)
    if unknown:
        raise OracleError(f"sprout: unknown networks {sorted(unknown)}")
    ident, _, platform = image_id().partition(" ")
    platform = f"linux/{platform or os.uname().machine}"
    # A control that no network implements: role 2 must act differently in the
    # two branches of a choice that nobody tells it.
    control = "branch(0,1,[msg(2,0,nat,end),msg(2,0,bool,end)])"
    from model import read_global
    for network in NETWORKS:
        if _run(protocol_text(read_global(control)), network, platform) != "non-implementable":
            raise OracleError(f"sprout: the control {control} is not refused on {network}, so no verdict counts")
    cache_path = answer_cache_path("sprout", ident.removeprefix("sha256:"))
    cache = load_answers(cache_path)
    texts: list[str | None] = []
    for g, _ in items:
        try:
            texts.append(protocol_text(g))
        except Premise:
            texts.append(None)
    jobs = [(i, n) for i, t in enumerate(texts) if t is not None for n in items[i][1]]
    budget = f"{QUERY_TIMEOUT}/{RUN_TIMEOUT}"
    keys = {(i, n): hashlib.sha256(f"{ident}\n{budget}\n{n}\n{texts[i]}".encode()).hexdigest() for i, n in jobs}
    todo = [job for job in jobs if keys[job] not in cache]
    LOG.info("%d sprout runs: %d from the cache, %d to run", len(jobs), len(jobs) - len(todo), len(todo))
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for done, (job, verdict) in enumerate(
                    zip(todo, pool.map(lambda j: _run(texts[j[0]], j[1], platform), todo), strict=True), start=1):
                cache[keys[job]] = verdict
                if done % 25 == 0:
                    LOG.info("%d of %d sprout runs done", done, len(todo))
                    save_answers(cache_path, cache)
    finally:
        save_answers(cache_path, cache)
    return [{n: ("no-tree" if t is None else cache.get(keys[(i, n)], "inconclusive")) for n in items[i][1]}
            for i, t in enumerate(texts)]
