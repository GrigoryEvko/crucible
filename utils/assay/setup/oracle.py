"""Install the toolchain of the crash-stop oracle into the cache (`setup install oracle`).

utils/assay/oracle/mpstk.py builds mpstk-crash-stop with sbt and runs its
verifier on a Java 17 runtime, and the verifier calls the mCRL2 tools
mcrl22lps, lps2pbes and pbes2bool.  The pins temurin-jdk, sbt and mcrl2 of
utils/pins.txt give a release of each.  This module downloads each release
into the cache of utils/assay/oracle/rocq.py
(~/.cache/crucible/session_oracle, or SESSION_ORACLE_CACHE),
checks its SHA-256, and unpacks it there.  The directory of a release has the
name and the version of its pin.  The module does not depend on any tool
that the host has, except rpm2cpio and cpio for the mCRL2 package.

sbt keeps its launcher, its global base, its Ivy home and the Coursier
cache under the same directory, so a build reads and writes nothing
outside the cache.

Each archive is downloaded one time.  A later run finds the unpacked tree
and does not download again.  A checksum that differs stops the install,
so a changed release never runs.
"""

from __future__ import annotations

import hashlib
import io
import logging
import os
import subprocess
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.core import pins as pin_table
from assay.oracle.rocq import OracleError, cache_root

LOG = logging.getLogger("session_oracle.toolchain")
# The pins of the toolchain: the Java runtime, sbt and mCRL2.
JDK_PIN = "temurin-jdk"
SBT_PIN = "sbt"
MCRL2_PIN = "mcrl2"


@dataclass(frozen=True, slots=True)
class Release:
    """One pinned download: where it comes from, its checksum, and its unpacked directory name."""

    name: str
    url: str
    sha256: str
    kind: str  # "tar.gz" or "rpm"


def release(name: str) -> Release:
    """Return the release of one pin of the toolchain for this host.

    Raises:
        OracleError: If the pin table has no such pin for this host
    """
    pin = pin_table.find(pin_table.load(), name)
    if pin is None:
        raise OracleError(f"{pin_table.PINS_PATH} has no pin {name} for the platform {pin_table.host_platform()}")
    kind = "rpm" if PurePosixPath(pin.source).suffix == ".rpm" else "tar.gz"
    return Release(f"{pin.name}-{pin.version}", pin.source, pin.digest, kind)


def toolchain_root() -> Path:
    """Return the directory that holds the unpacked releases."""
    return cache_root() / "toolchain"


def _download(release: Release) -> bytes:
    LOG.info("fetching %s", release.url)
    try:
        with urllib.request.urlopen(release.url, timeout=600) as resp:
            data = resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {release.url}: {exc}") from exc
    digest = hashlib.sha256(data).hexdigest()
    if digest != release.sha256:
        raise OracleError(f"{release.url}: SHA-256 {digest}, not the pinned {release.sha256}")
    return data


def _unpack(release: Release, data: bytes, target: Path) -> None:
    staging = target.with_name(target.name + ".partial")
    staging.mkdir(parents=True, exist_ok=False)
    if release.kind == "tar.gz":
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
            tar.extractall(staging, filter="data")
    else:
        rpm = subprocess.run(["rpm2cpio", "-"], input=data, capture_output=True)
        if rpm.returncode != 0:
            raise OracleError(f"rpm2cpio failed on {release.url}: {rpm.stderr.decode()[-2000:]}")
        cpio = subprocess.run(["cpio", "-idm", "--quiet"], input=rpm.stdout, cwd=staging, capture_output=True)
        if cpio.returncode != 0:
            raise OracleError(f"cpio failed on {release.url}: {cpio.stderr.decode()[-2000:]}")
    staging.rename(target)


def install() -> Path:
    """Download, check and unpack each release that the cache lacks.  Return the toolchain root."""
    root = toolchain_root()
    root.mkdir(parents=True, exist_ok=True)
    for pinned in (release(JDK_PIN), release(SBT_PIN), release(MCRL2_PIN)):
        target = root / pinned.name
        if target.is_dir():
            continue
        partial = target.with_name(target.name + ".partial")
        if partial.exists():
            raise OracleError(f"{partial} is left from an install that stopped.  Remove it and run again.")
        _unpack(pinned, _download(pinned), target)
    return root


def _only_child(directory: Path) -> Path:
    children = [p for p in directory.iterdir() if p.is_dir()]
    if len(children) != 1:
        raise OracleError(f"{directory}: expected one top directory, found {len(children)}")
    return children[0]


def environment() -> dict[str, str]:
    """Return the environment in which sbt, java and the mCRL2 tools run from the cache.

    The installed tools come first on PATH.  sbt keeps its launcher, its
    global base, its Ivy home and the Coursier cache under the toolchain
    root.
    """
    root = install()
    java_home = _only_child(root / release(JDK_PIN).name)
    sbt_home = root / release(SBT_PIN).name / "sbt"
    mcrl2 = root / release(MCRL2_PIN).name / "usr"
    env = dict(os.environ)
    env["JAVA_HOME"] = str(java_home)
    env["PATH"] = os.pathsep.join([str(java_home / "bin"), str(sbt_home / "bin"), str(mcrl2 / "bin"),
                                   env.get("PATH", "")])
    libs = [str(p) for p in (mcrl2 / "lib64", mcrl2 / "lib") if p.is_dir()]
    if libs:
        env["LD_LIBRARY_PATH"] = os.pathsep.join([*libs, env.get("LD_LIBRARY_PATH", "")])
    state = root / "sbt-state"
    env["COURSIER_CACHE"] = str(state / "coursier")
    env["SBT_OPTS"] = " ".join([f"-Dsbt.boot.directory={state / 'boot'}", f"-Dsbt.global.base={state / 'global'}",
                                f"-Dsbt.ivy.home={state / 'ivy2'}", f"-Dsbt.coursier.home={state / 'coursier'}"])
    return env
