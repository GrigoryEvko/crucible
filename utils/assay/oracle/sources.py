"""The pinned sources of the oracles: the rows of utils/pins.txt that give them, and their one download.

THE SOURCES
    Each oracle reads a published development or a published artifact.  A
    row of utils/pins.txt gives each one: a git repository on GitHub at a
    commit (via=git), or an archive with its SHA-256 (via=archive).  The
    version of a git row is the date of its commit, because these sources
    have no releases.  SOURCES names the pin of each source.  No module of
    the oracle holds a commit, a digest or an address of its own.  The pin
    table is then the one file that gives each version.  The rule tool-pins
    of utils/registry.txt gives each row of the table a reader, and SOURCES
    holds the readers of these rows.

THE DOWNLOAD
    fetch() downloads the archive of a git source at its commit into the
    cache of the oracle (assay.oracle.rocq.cache_root()).  It unpacks the
    directory NAME-COMMIT of the archive there, where NAME is the name of the
    repository.  A marker file of the unpacked tree shows a completed unpack.
    A second call finds the marker and does not download again.

`guard selftest tests.oracle.test_sources` does a test of the module.
"""

import functools
import io
import tarfile
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from assay.core import pins as pin_table
from assay.core import rows as rows_grammar
from assay.oracle.rocq import OracleError, cache_root

# The pin of each source (THE SOURCES).
PROJECTION = "rocq-projection"
SUBJECT_REDUCTION = "rocq-subject-reduction"
EKICI = "rocq-smpst-sr-smer"
KESKIN = "rocq-mpstlive"
MPSTK = "mpstk-crash-stop"
COAR = "coar"
SPROUT = "sprout-artifact"
SOURCES = (PROJECTION, SUBJECT_REDUCTION, EKICI, KESKIN, MPSTK, COAR, SPROUT)
# The host of each git source.
GITHUB = "github.com"
DOWNLOAD_TIMEOUT_S = 300


@dataclass(frozen=True, slots=True)
class Source:
    """One pinned source of an oracle: its pin, its address, its version, and its commit or the SHA-256 of its archive.

    `commit` is empty for an archive, and `digest` is empty for a git source.
    """

    pin: str
    address: str
    version: str
    commit: str
    digest: str

    @property
    def repository(self) -> str:
        """Return OWNER/NAME of a git source on GitHub, or an empty text for an archive."""
        if not self.commit:
            return ""
        return urllib.parse.urlsplit(self.address).path.strip("/")

    @property
    def archive_url(self) -> str:
        """Return the address of the archive: the archive of the commit for a git source, else the address itself."""
        return f"{self.address}/archive/{self.commit}.tar.gz" if self.commit else self.address

    @property
    def directory(self) -> str:
        """Return NAME-COMMIT, the directory of the archive of a git source (THE DOWNLOAD)."""
        return f"{self.repository.rsplit('/', 1)[-1]}-{self.commit}"


@functools.cache
def _table() -> tuple[pin_table.Pin, ...]:
    """Read the pin table of the tree one time for the process.

    Raises:
        OracleError: If the pin table cannot be read
    """
    try:
        return pin_table.load()
    except (rows_grammar.RowsError, OSError) as problem:
        raise OracleError(f"the pin table cannot be read: {problem}") from None


def of_pin(pin: pin_table.Pin) -> Source:
    """Return the source of one pin row.

    Raises:
        OracleError: If the row is not a git source on GitHub or an archive
    """
    if pin.via == pin_table.Via.GIT and urllib.parse.urlsplit(pin.source).netloc == GITHUB:
        return Source(pin.name, pin.source.removesuffix(".git"), pin.version, pin.commit, "")
    if pin.via == pin_table.Via.ARCHIVE:
        return Source(pin.name, pin.source, pin.version, "", pin.digest)
    raise OracleError(
        f"the pin {pin.name} of {pin_table.PINS_PATH} is no git source on {GITHUB} and no archive, so "
        f"no oracle can read it"
    )


def source(name: str, table: tuple[pin_table.Pin, ...] | None = None) -> Source:
    """Return the source of a pin of SOURCES (THE SOURCES).

    Args:
        name: The name of the pin
        table: The pins, by default the pin table of the tree

    Raises:
        OracleError: If the table has no row of the pin for this host, or the row is no source
    """
    pin = pin_table.find(_table() if table is None else table, name)
    if pin is None:
        raise OracleError(
            f"{pin_table.PINS_PATH} has no pin {name} for the platform {pin_table.host_platform()}, and "
            f"an oracle reads its source from that row"
        )
    return of_pin(pin)


def download(url: str) -> bytes:
    """Return the bytes of one download.

    Raises:
        OracleError: If the download fails
    """
    import logging

    logging.getLogger("session_oracle.sources").info("fetching %s", url)
    try:
        with urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_S) as response:
            data: bytes = response.read()
    except OSError as problem:
        raise OracleError(f"cannot download {url}: {problem}") from problem
    return data


def fetch(item: Source, marker: str) -> Path:
    """Download and unpack the archive of a git source at its commit, and return its root (THE DOWNLOAD).

    Complexity: linear in the bytes of the archive, and one stat when the unpacked tree is there.

    Args:
        item: The git source
        marker: A file of the unpacked tree, relative to its root, that shows a completed unpack

    Raises:
        OracleError: If the source is no git source, the download fails, or the archive holds no marker
    """
    if not item.commit:
        raise OracleError(f"the pin {item.pin} is an archive, and fetch() unpacks only a git source")
    root = cache_root() / item.directory
    if (root / marker).is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(download(item.archive_url)), mode="r:gz") as archive:
        prefix = f"{item.directory}/"
        members = [member for member in archive.getmembers() if member.name.startswith(prefix)]
        if not members:
            raise OracleError(f"{item.archive_url}: no directory {prefix} in the archive")
        archive.extractall(root.parent, members=members, filter="data")
    if not (root / marker).is_file():
        raise OracleError(f"{root}: the archive has no {marker}")
    return root
