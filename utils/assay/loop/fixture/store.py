"""Run one negative-compile fixture, and keep the result of its compile in a store.

The program
-----------
CTest runs `loop fixture` for each fixture, and the master loop calls main()
of this module.  Python compiles the script that it runs on each start, and it
keeps the compiled form of each module that it imports in the cache root.  The
master is small, so a run that uses the store does not compile this module
again.  This module imports the modules of a compile (assay.core.proc and
shutil) only when it compiles or when a memo misses.  It imports the test
launcher (assay.loop.teststep, with assay.core.meter) and resource on each
run, for the test record.  It imports utils/assay/core/report.py only when it
has a finding to report, when a warnings file of the fixture exists, or when
the memo of the budget misses.

The driver reads the compile command of the fixture from compile_commands.json.
It runs that command, and it puts the object file and the dependency file in a
scratch directory of the fixture.  It does not call Ninja.  CTest runs many
fixtures at the same time, and each `cmake --build` call writes to the same
Ninja log and to the same build graph.

The parse of the compile database takes about 20 ms.  So the driver keeps the
command of the fixture in `NAME.command` in the scratch directory, with the
device, the inode, the size and the two change times of the database.  It uses
that file only while the five fields are the same.  It writes the file only
when the database did not change in the settle period of the store, so a change
in the same clock tick cannot hide.

How the regexes apply
---------------------
Each compile writes a SARIF log of its diagnostics (assay.readers.sarif).  The
driver applies the regexes to the lines that render() gives for the records of
the log: the message lines, the `note:` lines, the bullet lines of GCC 16, and
the context lines of each function, instantiation, constant evaluation,
constraint and macro.  These lines can contain a type name, a concept name or
a list of template arguments, and a regex must find its text there.  The lines
hold no source line, no caret line and no include chain.  A regex then cannot
find its text in a source line of the fixture, which is not a message of the
compiler.  The driver prints the text output of the compile for the reader.

The reason of the failure
-------------------------
A fixture must fail only for its own reason.  When each regex matches, the
driver also makes sure that each error of the log has a regex
(`unmatched_errors`).  The diagnostic of an error is the group of its head in
the rendering: the head line, its context lines, and each line after the head
up to the next head.  A regex match in one of these lines gives the error a
regex.  An error with no regex fails the fixture, also when the regexes match
its other errors.  An error with no regex can hide a change: a fixture can keep
its own error and get a second error from a different cause.

The ledger utils/ledgers/fixture-own-reason.txt names the fixtures that had
an error with no regex when the rule started, `- | - | admit | FIXTURE |
REASON`.  The rule fixture-own-reason of the registry has the policy admit,
and the driver holds it (held-by=loop-fixture).  A ledger that does not
exist has no row, and the driver then imports no ledger module.  A listed
fixture gives a warning in the format of utils/assay/core/report.py, and the
driver writes it to the warnings directory (`fixture-own-reason.NAME.txt`).  A
listed fixture with no such error fails, so that its row goes.
CRUCIBLE_NEG_REASON_LEDGER names a different ledger in the same format, for
the tests of the driver.

The quarantine plugin
---------------------
Each compile of the tree loads the quarantine plugin, and the compile of a
fixture keeps it.  The driver sets the plugin argument mode= to report
(`_report_mode`).  A finding is then a note, and a finding in a file of the
enforce mode error is an error.  An opted-out finding is a note in each file.
Each region of the unit is a note too, at the macro that opens it.  The
census then gives each opted-out finding the reason of its region.  A
fixture stops on its own error, and the
compile writes no object.  The plugin then gives each finding as a result
"quarantine: KIND ENTITY" of the SARIF log (assay.readers.sarif, THE
DIAGNOSTICS OF THE PLUGIN).

The regexes and the rule of the reason read the log with no result of the
plugin: the driver renders the log with sarif.is_plugin_result as `hidden`.
The plugin then changes no line that a regex reads, and no finding of the
plugin fails a fixture for its reason.  An error of the plugin that is no
finding stays, such as the error of the contract rule.  The regexes of the
fixtures neg_contract_specifier_* read that error.  The results of the plugin in the
source of the fixture are its census (`fixture_census`).  The record of the
run holds the census and the stamp argument of the plugin (The record of a
run), and the rule fixture-quarantine counts them.

A compile that succeeds writes an object, and the plugin then writes its
findings into the section of the object and not into the log.  The census of
such a compile holds the lines of the rule quarantine in the source, from the
section (`section_census`).  The store keeps no object.  It then keeps no
compile that succeeds, and a stored result of such a compile is a miss.  A
command that loads no quarantine plugin has no stamp argument, and its record
holds no census.  A compile with no log or no section gives no census too.

The plugin is a file that the command names, and the key of a stored result
holds the bytes of the plugin (item 1 below).  A new build of the plugin then
compiles each fixture again.  A second compile for the census costs as much
as the compile of the fixture, after each edit of a header.

The result store
----------------
Without the store, CTest compiles each fixture again on each run, and these
compiles are most of the cost of the run.  The driver keeps the exit code, the
combined output and the records of the SARIF log of each compile in a store in
`~/.cache/crucible/neg/`.
The store is the "neg" cache of utils/assay/core/cache.py, so
CRUCIBLE_CACHE_DIR moves it with every other cache, and the value "off" turns
it off.  When the store holds a correct entry for the compile, the driver uses
the stored exit code, output and log, and it does not compile.  Then it does
the same checks as after a compile: the regexes, the rule of the reason and the
independence report.  The store holds the output and the log of the compiler,
and not the decision to pass or fail.

One entry holds the results of at most four compiles of one key, newest
first.  Each result has its own inputs (items 2 to 4 below).  The driver uses
the first result whose inputs are all the same.  So the undo of an edit, or a
change to a different branch and back, finds the result of the earlier
compile.  A new result goes first.  A result with the same inputs as the new
result goes, and the oldest result goes when the entry holds more than four.
The entry keeps each dependency path one time, for all its results.

CRUCIBLE_NEG_CACHE=0 turns the store off.  CRUCIBLE_NEG_CACHE_MAX_MB sets the
size limit of the store, and the preset value is the row "neg" of LIMITS in
utils/assay/core/cache.py.  A size is the space that a file takes on the
disk.  The memos can use _MEMO_PART of the limit, and the entries use the
rest.  The store keeps each entry in one of 256 buckets, by the first two
digits of its key.  Each bucket gets an equal part of the entries.  When a
write makes a bucket larger than its part, the driver removes the entries of
that bucket that it did not use for the longest time.  So a write reads one
bucket and not the full store.  One write in each _MEMO_SWEEP_SECONDS reads
the memos, and it removes the memos with the oldest write while they use
more than their part.  A removed memo costs one more hash of its file.  A
damaged entry, an entry that the driver cannot read and an entry in a
different format are not correct entries.

The compile environment
-----------------------
Each compile, and each query of the compiler, runs with LC_MESSAGES=C, and the
value of LC_ALL goes to LC_CTYPE.  GCC then reads no message catalog, and its
diagnostics are in English, the language of the regexes.  Without this, a host
with LANGUAGE=de gets German diagnostics from the catalogs of the compiler, and
each fixture fails.  LC_CTYPE keeps the codeset, which sets the quote
characters of a diagnostic.

Why a stored result is the result of the compile
------------------------------------------------
A compile gives the same exit code and the same output when all its inputs are
the same.  The inputs are the compiler and its environment, the bytes of each
file that the compile reads, and the answer of each lookup that the compile
makes.  The store makes sure of each input:

1. The key is a hash of these items: the compiler identity, the full command,
   the working directory, each environment variable that GCC or the loader
   reads, and the bytes of the driver (the master utils/bin/loop and this
   module).  The compiler identity is the path and the bytes of the compiler
   driver, of cc1plus and of the assembler, of each shared object that the
   loader maps into them, and of the specs file, and the codeset of LC_CTYPE.
   The key also holds the bytes of each file that the command names, for
   example a plugin.  When the command contains `-march=native`, the key holds
   the model and the flags of the host CPU.  In each item, a mark takes the
   place of each text of the source root and of the build root (item 8).  In
   the command, a placeholder replaces the path of the dependency file and
   the path of the SARIF log, because the two paths name the process.
2. Each result lists each file that GCC read, from the dependency file of the
   compile, with a hash of its bytes.  The driver compares the hash of each
   file now with the hash in the result.  `NAME.digests` in the scratch
   directory of the fixture keeps the hash of each file that the fixture read,
   with the device, the inode, the size and the two change times of the file.
   The driver uses a kept hash while the five fields are the same, and it
   reads the file again when a field changed.  It keeps a hash only for a file
   that did not change in the settle period before the read (item 7).  After a
   fatal error, GCC writes no dependency file.  Then the driver preprocesses
   the source again with `-M -MG`, which writes the dependency rule and
   continues after a missing header.  The pass also gives -Wno-fatal-errors,
   so a -Wfatal-errors of the command does not stop it at the first error.
   That pass reads each file that the compile read before its fatal error,
   and it can read more.  It also names each missing header.
3. Each result lists the search roots of the compile, with the kind and the
   real path of each root.  The roots are each directory that GCC names in its
   search list (the output of `-E -v` on an empty file, with each directory
   that GCC ignores), and the directory of each dependency.  For each root
   that is a directory, the result holds a hash of the names under it: the name
   and the kind of each entry, the target of each symbolic link, and the names
   under each directory that a link names.  A root inside a different root
   gets no hash of its own.
4. Each result lists the state of each exact path that an include name with a
   `..` component, or an absolute name, can reach (item 5), and of its
   precompiled name `NAME.gch`.

Why these inputs are sufficient
-------------------------------
5. GCC finds a name N of an `#include`, `#include_next`, `#embed` or
   `__has_include` at the first path D/N in its search sequence that is a
   file.  D is a directory of the search list or the directory of the current
   file.  Item 3 holds the directory of the current file, because the current
   file is a dependency.  The command, the environment and the compiler in the
   key, and the directories that GCC names in the list, set the search list.  When N has no `..`
   component and is not absolute, each path D/N is under the root D, so the
   hash of the names under D sets the answer at D.  The bytes of the file that
   GCC finds are the bytes of a dependency, which item 2 holds.  A name with a
   `..` component can reach a path outside each root.  When GCC finds such a
   name, the dependency file spells the path D/N, and item 4 holds the path
   that N reaches from each root.  When a fatal error stops at such a name, the
   `-M -MG` pass spells it, and item 4 holds its paths.  An absolute name has
   one path, which is a dependency or the missing header of a fatal error.
   A `__has_include` probe that finds no file leaves no trace.  When its name
   has a `..` component or is absolute, the store does not see that probe.  No
   file of the tree has such a probe.
6. The compiler makes three other kinds of lookup, and the key sets each one.
   The driver of GCC finds cc1plus, the assembler and the specs file: the
   store asks the driver for each path (`-print-prog-name`,
   `-print-file-name`).  GCC takes the first usable path in a fixed list:
   each directory that `-print-search-dirs` names, with the name of the file,
   after the assembler of `--with-as` when GCC has one.  For an assembler that
   GCC does not find, the store takes the first one in PATH.  A memo keeps
   the three answers with the state of each path of these lists.  The memo
   gives the answers while the driver, its real path, the working directory,
   the environment and the state of each path are the same.  The store writes
   the memo only when the lists give the three answers of the driver.  The
   loader finds the shared objects: the loader lists them (`--list`), and with
   LD_DEBUG=libs it names each path that it tries.  A memo keeps the list
   while each tried path has the same state, and each object, the cache of the
   loader and its preload list have the same size and times.  GCC reads its
   message catalogs only for a language other than C, and the compile
   environment sets LC_MESSAGES=C.  The driver computes the key again after
   the compile, and it stores no result when the key changed.
7. A change in a root or of a dependency after the compile changes the hash or
   the state in the result.  A change while GCC reads a file can hide.  The
   driver does not store a result when a dependency, a directory under a root,
   a root, a probed path or the target of a symbolic link changed in the second
   before the compile started or after that.  For a path that does not exist,
   the driver reads the time of the nearest directory above it.  A directory
   gets a new change time when a process adds, removes or renames a name in
   it.  A memo keeps the hash of the names under a root, with the inode and the
   two change times of each directory under the root and the target of each
   link.  The memo gives the hash while each one is the same.
   CRUCIBLE_NEG_NOW_NS gives the driver a time earlier than the clock, for the
   tests of the driver.  An earlier time only makes the driver refuse more
   results.

Why a result from a different root is the result of the compile
---------------------------------------------------------------
8. Two build directories of one commit, in two work trees, share each result.
   The source root and the build root of a build directory come from its
   CMake cache (CMAKE_HOME_DIRECTORY and CMAKE_CACHEFILE_DIR).  The memo of
   the compile command keeps them.  The key
   holds a mark in place of each text of a root: in the command, in each path
   and in the environment.  A result holds the marks in each path, in each
   hash of the names under a root, and in the output and the log.  A lookup
   changes each mark to the root of this build, and it does the checks of
   items 2 to 4 at the paths that it gets.  It gives the output and the log
   with the paths of this build, so each regex sees the paths of this build.
   A compile in a different root gives the same output and the same log with
   the other roots when these conditions hold:
   - The command, the environment and the names under each root are the same
     with the marks (item 1, item 3 and item 4).
   - Each dependency has the same bytes (item 2).  No dependency holds the
     text of a root of the compile that stored the result, or of the build
     that reads it.  A file that names a root can make GCC read a file
     outside the roots, or compare a path with that text.  The store finds
     such a text as one run of bytes, so a file that joins it from parts,
     for example from two string literals, breaks this condition unseen.
   - GCC uses a path only to print it, or to compare it with another path of
     the same compile, for equality or for a suffix.  At compile time, the
     tree reads a file name only in these ways: fixy/Atom.h compares two file
     names, and test/layer/checks/crucible/Types.cpp compares a suffix.  Code
     that reads the length, a hash or the sequence of a path and other text
     can break this condition.
   A root takes part only when it is an absolute path of two components or
   more, because GCC writes no such text on its own.  A mark holds a NUL
   character, and no path and no command holds one.  The store keeps no
   output and no log that holds a NUL character, so a mark stands for its
   root only.

A new file in a root changes the key of each compile that searches that root,
also when no lookup names the new file.  A new header in `include/` then
compiles each fixture again, and a new fixture file compiles each fixture of
its directory again.

The driver stores no result in these conditions:

- The exit code is not 0 or 1.  A signal or an internal error of the compiler
  can give a different output on the next run.
- An input changed in the settle period of item 7, or the key changed during
  the compile (item 6).
- The driver cannot read a root or a probed path.
- The output or the log holds a NUL character, or a dependency holds the text
  of a root (item 8).
- The compile writes no SARIF log, or the reader refuses its log.  The test of
  the fixture then fails, and the reason names the log.
- A search directory holds a precompiled header (a name that ends in `.gch`),
  or a probed path of item 4 is one.  GCC reads its bytes, and the dependency
  file does not name it.
- A root holds more than 200,000 names.
- The `-M -MG` pass after a fatal error writes no dependency rule.
- The command gives a plugin an output directory (`-fplugin-arg-NAME-out=`).
  The compile writes a file there, and a stored result does not write it.
- The command reads a profile (`-fprofile-use`, `-fauto-profile`).
- The command names a response file (`@FILE`), a `-B` prefix, a plugin by a
  name that is not a path, or a file for `-include` or `-imacros` by a
  relative name.  GCC looks for such a file in the working directory or under
  the prefix, and no root holds those lookups.

The cost of a compile
---------------------
The driver measures the user and the system CPU time of each compile from the
resource use of its child processes.  It runs one compile at a time, so the
CPU time that its waited children gain during the call is the time of the
compiler driver, cc1plus and the assembler.  It also counts the user
instructions of the compile, when the host gives an exact count
(utils/assay/core/meter.py, THE INSTRUCTION COUNT).  The CPU budget applies to
the sum of the user time and the system time.  The kernel measures the CPU
time of a process exactly, and it divides that time between the user time and
the system time from the samples of the timer tick.  A compile of a few
milliseconds whose samples all fall in the kernel gets a user time of 0 s.
The finding gives the two times.  The entry of the store keeps the times and
the count, and a result from the store gives the cost of the compile that
made it.

The measured rules fixture-cpu and fixture-instructions of utils/registry.txt
give the thresholds.  A measure above the warning threshold of a rule prints a
warning in the format of utils/assay/core/report.py, and writes it to the
warnings directory, in a file of this fixture (`fixture-cpu.NAME.txt`,
`fixture-instructions.NAME.txt`).  The error level belongs to the instruction
count when the compile has one, because the CPU time rises with the load of the
host and the count does not.  A CPU time above its error threshold then gives a
warning only.  With no count, the error level belongs to the CPU time.  A
measure above the error threshold of the rule that holds the error level fails
the test, and the driver does not store that result, so the next run measures
again.  The ledgers utils/ledgers/fixture-cpu.txt and
utils/ledgers/fixture-instructions.txt keep the fixtures above the warning
threshold of their rule, `- | - | VALUE | FIXTURE | REASON`, with VALUE the
measure of the compile when the row was written, in the unit of the rule.  A
row applies to each build.  A kept fixture gives a warning up to VALUE with the
noise of the unit (assay.core.budget, THE NOISE).  A measure above that gives
the level of a fixture with no row.  A kept fixture at or below the
warning threshold fails, so that its row goes.  A ledger that does not exist
has no row.  On a GitHub runner, each error of the
CPU time is a warning that says that it was demoted (utils/assay/core/meter.py,
A CI RUNNER), and an error of the instruction count stays an error.  A
registry that cannot be read stays an error.  CRUCIBLE_NEG_REGISTRY,
CRUCIBLE_NEG_CPU_LEDGER and CRUCIBLE_NEG_INSTRUCTIONS_LEDGER name a different
registry and different ledgers in the same format, for the tests of the
driver.

A memo in the store keeps the two measured rules, the two ledgers and the
thresholds of the rules of the test launcher (The test record).  The memo
stays while the two registries and the two ledgers have the same stat fields.
A ledger that does not exist has the stamp of an absent file, so a new ledger
replaces the memo.
The driver writes the memo only when no file changed in the settle period.  A
run with no finding and no warnings file of its fixture then reads no registry
and imports no report module.

The record of a run
-------------------
After each run, from a compile or from the store, the driver writes the
fixture record `NAME.inputs` in the scratch directory of the fixture
(assay.core.record, THE RECORDS): the result, the CPU times, the instruction
count, and each file that the compile read.  The list is the dependency file
of GCC, the list of the `-M -MG` pass after a fatal error, or the list of the
stored result.  The rule parse-total (`guard check --rule
parse-total`) adds the bytes of these files for each fixture.  The record
also holds the census of the fixture and the stamp argument of the plugin
(The quarantine plugin).

The test record
---------------
CMake gives a fixture no test launcher.  The driver judges its own run as the
launcher judges a test, with the same function
(assay.loop.teststep.judge_and_record).  A second start of Python for each
fixture costs more than the judge.  The measure holds three values.  The
first is the wall time from the start of main().  The second is the CPU time
of this process and of each process that it waited for.  The third is the
peak memory of the largest of these processes.

The judge holds the measured rules test-time and test-memory, with the
thresholds of the registry of the launcher (CRUCIBLE_TEST_REGISTRY) and its
ledgers (CRUCIBLE_TEST_LEDGERS).  A fixture with the status 0 fails over an
error threshold that no ledger row keeps.  A run that takes its result from
the store does not measure a compile.  Its judge then refuses no row of the
fixture.  The driver writes the record
BUILD_DIR/test-records/NAME.test.cost, with the keys of the record of the
launcher.  In a run of the test launcher, the environment holds
CRUCIBLE_TEST_LAUNCHED.  The driver then gives the judge and the record to
the launcher.

utils/assay/tests/loop/test_fixture.py holds the tests of the store, the cost
budget, the two records, the rule of the reason, the quarantine plugin and
the census.
"""

from __future__ import annotations

import bisect
import hashlib
import json
import os
import re
import stat
import sys
import time
import zlib
from collections.abc import Mapping
from pathlib import Path
from types import ModuleType

from assay.core import budget as cost_budget
from assay.core import record as cost_record
from assay.core.paths import LOOP, REPO_ROOT
from assay.readers import compdb, sarif

# A fixture name is a key of a warnings file, so it obeys WRITER_KEY of
# utils/assay/core/report.py.  The test of the driver holds the two
# patterns equal.
_FIXTURE_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]*")

# The marks that stand for the build root and the source root in a key and in
# a stored result (the module text, item 8).  A path and a command hold no NUL
# character, and the store keeps no output and no log that holds one, so a mark
# stands for its root only.
_BUILD_MARK = "\x00build root\x00"
_SOURCE_MARK = "\x00source root\x00"


def _is_markable(root: str | None) -> bool:
    """Return True when `root` can take part in the marks: an absolute path of two components or more, with no NUL.

    GCC writes no such text on its own.  A root such as `/` or `/tmp` can be
    part of a text that GCC writes, for example the name of a temporary file.
    """
    return (root is not None and os.path.isabs(root) and "\x00" not in root
            and len([part for part in root.split(os.sep) if part]) >= 2)


class Roots:
    """The build root and the source root of a compile, and the change of each one to its mark and back.

    `relabel` changes each text of a root to its mark, the longer root first,
    so a build root inside the source root takes the mark of the build root.
    `restore` changes each mark back to the text of its root.  For a text
    with no NUL character, restore(relabel(text)) is the text.  A root that
    `_is_markable` refuses takes no part, and its text stays as it is.
    """

    __slots__ = ("pairs", "texts")

    def __init__(self, source: str | None, build: str | None) -> None:
        """Hold the roots that can take part in the marks, and the bytes that a byte search for a root reads."""
        chosen = [(root, mark) for root, mark in ((build, _BUILD_MARK), (source, _SOURCE_MARK)) if _is_markable(root)]
        self.pairs: list[tuple[str, str]] = sorted(chosen, key=lambda pair: -len(pair[0]))  # type: ignore[arg-type]
        # A root whose text holds the text of another root needs no search of its own.
        texts = [root for root, _ in self.pairs]
        self.texts = [os.fsencode(root) for root in texts
                      if not any(other != root and other in root for other in texts)]

    def relabel(self, text: str) -> str:
        """Return `text` with each text of a root changed to its mark."""
        for root, mark in self.pairs:
            text = text.replace(root, mark)
        return text

    def restore(self, text: str) -> str:
        """Return `text` with each mark changed back to the text of its root."""
        for root, mark in self.pairs:
            text = text.replace(mark, root)
        return text

    def relabel_tree(self, value: object) -> object:
        """Return `value`, a tree of lists, dictionaries and texts, with each text relabeled."""
        if isinstance(value, str):
            return self.relabel(value)
        if isinstance(value, list):
            return [self.relabel_tree(item) for item in value]
        if isinstance(value, dict):
            return {key: self.relabel_tree(item) for key, item in value.items()}
        return value

    def holds_root(self, data: bytes) -> bool:
        """Return True when the bytes `data` hold the text of a root.  The cost is O(n) in the length of `data`."""
        return any(text in data for text in self.texts)

    def identity(self) -> list[str]:
        """Return the texts of the roots that take part, the longer first, for the name of a memo."""
        return [root for root, _ in self.pairs]


def root_texts(build_dir: Path) -> tuple[str | None, str | None]:
    """Return the source root and the build root of the build in `build_dir`, from its CMake cache.

    The entries are CMAKE_HOME_DIRECTORY and CMAKE_CACHEFILE_DIR of the reply
    cache-v2 (assay.readers.cmake).  With no reply, or no such entry, a root is
    None, and the store keys its text as it is.  The memo of the compile
    command keeps the roots, and only a miss of the memo imports the reader.
    """
    from assay.readers import cmake

    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError:
        return None, None
    source = entries.get("CMAKE_HOME_DIRECTORY")
    build = entries.get("CMAKE_CACHEFILE_DIR")
    return None if source is None else source.value, None if build is None else build.value


def build_roots(build_dir: Path) -> Roots:
    """Return the roots of the build in `build_dir` (root_texts)."""
    return Roots(*root_texts(build_dir))


def summarise_errors(rendering: sarif.Rendering, source: Path) -> tuple[int, list[int]]:
    """Return the number of error heads of a rendering, and the sorted lines of the fixture that hold one.

    A fixture that fails for one reason has one such line.  More than one line
    means that the fixture fails for more than one reason, and a regex can then
    match a rejection other than the one that the fixture documents.  An error
    with no file or no line counts in the number, and it gives no line.
    Complexity: linear in the number of error heads.
    """
    own: set[int] = set()
    for error in rendering.errors:
        if not error.path or not error.line:
            continue
        try:
            is_own = Path(error.path).resolve() == source
        except OSError:
            is_own = error.path == str(source)
        if is_own:
            own.add(error.line)
    return len(rendering.errors), sorted(own)


def unmatched_errors(rendering: sarif.Rendering, expected_regexes: list[str]) -> list[str]:
    """Return the head line of each error of a rendering that no match of a regex reaches.

    The regexes apply to the lines of the rendering, joined with line breaks,
    as in `evaluate`, with re.MULTILINE.  The diagnostic of an error is the
    group of its head (sarif.Rendering).  A match reaches a diagnostic when its
    span holds a character of a line of its group, so one match can reach two
    diagnostics.  The cost is O(n + m log n) for n lines and m matches, plus
    the cost of the searches.
    """
    if not rendering.errors:
        return []
    text = "\n".join(rendering.lines)
    starts: list[int] = []
    offset = 0
    for line in rendering.lines:
        starts.append(offset)
        offset += len(line) + 1
    reached: set[int] = set()
    for pattern in expected_regexes:
        for match in re.finditer(pattern, text, flags=re.MULTILINE):
            first = bisect.bisect_right(starts, match.start()) - 1
            last = bisect.bisect_right(starts, max(match.start(), match.end() - 1)) - 1
            reached.update(rendering.groups[number] for number in range(first, last + 1))
    return [rendering.lines[head] for group, head in enumerate(rendering.heads) if group not in reached]


# ── The compile command ────────────────────────────────────────────


class CompileResult:
    """The exit code, the combined standard output and error, the log, the cost and the inputs of one compile.

    `log` holds the records of the SARIF log of the compile, or None when the
    compile wrote no log that the reader reads, and `log_problem` then gives
    the reason.  `user_s` and `system_s` are the user and the system CPU time
    of the compile in seconds, and `cpu_s` is their sum, which the budget
    reads.  `instructions` is the exact count of its user instructions, or
    None when the host gives no exact count.  `inputs` is each file that the
    compile read, by absolute path, or None when the driver does not know
    them.  A result from the store gives the log, the cost and the inputs of
    the compile that made it.
    """

    __slots__ = ("returncode", "output", "user_s", "system_s", "instructions", "inputs", "log", "log_problem")

    def __init__(self, returncode: int, output: str, user_s: float, system_s: float, instructions: int | None,
                 inputs: tuple[str, ...] | None = None, log: sarif.Log | None = None,
                 log_problem: str = "the compile wrote no SARIF log") -> None:
        """Hold the exit code, the output, the two CPU times, the instruction count, the inputs and the log."""
        self.returncode = returncode
        self.output = output
        self.user_s = user_s
        self.system_s = system_s
        self.instructions = instructions
        self.inputs = inputs
        self.log = log
        self.log_problem = "" if log is not None else log_problem

    @property
    def cpu_s(self) -> float:
        """Return the user and the system CPU time of the compile, whose sum the kernel measures exactly."""
        return self.user_s + self.system_s

    def with_inputs(self, inputs: tuple[str, ...] | None) -> CompileResult:
        """Return the same result with the inputs `inputs`."""
        return CompileResult(self.returncode, self.output, self.user_s, self.system_s, self.instructions, inputs,
                             self.log, self.log_problem)

    def with_log(self, log: sarif.Log | None, log_problem: str) -> CompileResult:
        """Return the same result with the log `log`, or with no log and the reason `log_problem`."""
        return CompileResult(self.returncode, self.output, self.user_s, self.system_s, self.instructions,
                             self.inputs, log, log_problem)


def _replace_output(argv: list[str], output: Path) -> list[str]:
    """Return `argv` with the object file of the compile set to `output`."""
    result = list(argv)
    for index, arg in enumerate(result):
        if arg == "-o" and index + 1 < len(result):
            result[index + 1] = str(output)
            return result
        if arg.startswith("-o") and len(arg) > 2:
            result[index] = f"-o{output}"
            return result
    return result + ["-o", str(output)]


def _strip_dependency_flags(argv: list[str]) -> list[str]:
    """Return `argv` without the options that write a dependency file.

    The driver adds its own dependency options, and the store reads the file
    that they name.
    """
    result: list[str] = []
    skip_next = False
    for arg in argv:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-MD", "-MMD", "-MP", "-MG"):
            continue
        if arg in ("-MF", "-MT", "-MQ"):
            skip_next = True
            continue
        if arg.startswith(("-MF", "-MT", "-MQ")):
            continue
        result.append(arg)
    return result


# The options of the GCC driver that load a plugin.
_PLUGIN_LOAD = ("-fplugin=", "--plugin=")
# The head of each argument of the quarantine plugin, PLUGIN_ARGUMENT of
# assay.guard.engines.plugin.runner, and the arguments of the mode and of the
# stamp.  The test of the driver holds the head equal to PLUGIN_ARGUMENT.
QUARANTINE_ARGUMENT = "-fplugin-arg-crucible_quarantine-"
_MODE_ARGUMENT = f"{QUARANTINE_ARGUMENT}mode="
_STAMP_ARGUMENT = f"{QUARANTINE_ARGUMENT}stamp="
_ROOT_ARGUMENT = f"{QUARANTINE_ARGUMENT}root="
# The mode of the plugin in the compile of a fixture (The quarantine plugin).
_CENSUS_MODE = "report"
# The rule of the lines of the section of an object that make the census of a compile that succeeds (The quarantine
# plugin).
_SECTION_CENSUS_RULE = "quarantine"
# Why the store keeps no compile that succeeds: the census reads the object of the compile, and the store keeps no
# object (The quarantine plugin).
_SUCCEEDED = "the compile succeeded, and its census reads the object of a new compile"


def _report_mode(argv: list[str]) -> list[str]:
    """Return `argv` with the mode argument of the quarantine plugin set to report (The quarantine plugin).

    A command with no such argument stays as it is.  The cost is O(n) in the length of the command.
    """
    return [f"{_MODE_ARGUMENT}{_CENSUS_MODE}" if arg.startswith(_MODE_ARGUMENT) else arg for arg in argv]


def census_stamp(argv: list[str]) -> str | None:
    """Return the value of the stamp argument of the quarantine plugin in `argv`, or None when it has none."""
    return next((arg[len(_STAMP_ARGUMENT):] for arg in argv if arg.startswith(_STAMP_ARGUMENT)), None)


def fixture_census(result: CompileResult, argv: list[str], directory: Path, source: Path,
                   output: Path) -> tuple[str, tuple[cost_record.CensusFinding, ...]] | None:
    """Return the stamp argument of the plugin and the findings of the plugin in the source of the fixture.

    The findings of a compile with an error are the results of the plugin in
    the log with the file `source`, each one time, in sorted sequence.  A
    result names its file by the path of sarif.plugin_path(), as the plugin
    names the file of a finding.  `source` is a path of `realpath()`.  The
    findings of a compile that succeeded come from the section of its object
    `output` (`section_census`).  The result is None when the command loads
    no quarantine plugin, and when the compile wrote no log or no section
    (The quarantine plugin).  The cost is O(n log n) in the results of the
    log or the lines of the section.
    """
    stamp = census_stamp(argv)
    if stamp is None:
        return None
    if result.returncode == 0:
        from_section = section_census(argv, directory, source, output, stamp)
        return None if from_section is None else (stamp, from_section)
    if result.log is None:
        return None
    found: set[cost_record.CensusFinding] = set()
    for record in result.log.diagnostics:
        if sarif.is_plugin_result(record) and sarif.plugin_path(record) == str(source):
            kind, entity = sarif.plugin_finding(record)
            found.add((kind, record.line, record.column, entity))
    return stamp, tuple(sorted(found))


def section_census(argv: list[str], directory: Path, source: Path, output: Path,
                   stamp: str) -> tuple[cost_record.CensusFinding, ...] | None:
    """Return the lines of the rule quarantine in the source of the fixture, from the section of its object.

    A compile that succeeds writes an object, and the plugin writes its
    findings into the section of the object and not into the log.  The lines
    of the rule quarantine are each finding that the rule fixture-quarantine
    counts, also each opted-out finding and each region.  A line names its
    file relative to the root argument of the plugin.  The result is None
    when the command has no root argument, or when the object has no section.
    It is None when the section cannot be read or holds a different stamp
    too.  The cost is O(n log n) in the lines of the section.
    """
    root = next((arg[len(_ROOT_ARGUMENT):] for arg in argv if arg.startswith(_ROOT_ARGUMENT)), None)
    if root is None:
        return None
    sections = _sections_module()
    try:
        section = sections.read(output)
    except (sections.SectionError, OSError):
        return None
    if section is None or section.stamp != stamp:
        return None
    shown = os.path.relpath(source, os.path.realpath(os.path.join(directory, root)))
    return tuple(sorted({(item.kind, item.line, item.column, item.entity) for item in section.findings
                         if item.rule == _SECTION_CENSUS_RULE and item.file == shown}))


def compile_argv(command: list[str], output: Path, depfile: Path, sarif_log: Path) -> list[str]:
    """Return the command that compiles the fixture.

    The command keeps the quarantine plugin of the build in the mode report
    (The quarantine plugin).  The object file goes to `output`, GCC writes the
    SARIF log of its diagnostics to `sarif_log`, and it writes the list of the
    files that it read to `depfile`.  The last two arguments are `-MF` and
    `depfile`.
    """
    argv = _replace_output(_report_mode(_strip_dependency_flags(command)), output)

    # A negative-compile fixture asserts a COMPILE-TIME rejection — a
    # property that only manifests under the `enforce` contract
    # evaluation semantic.  Presets that relax contracts (release =>
    # observe; the `ignore` hot-path TUs) would otherwise let the
    # fixture compile clean, inverting its WILL_FAIL / expected-
    # diagnostic gate.  GCC honors the LAST -fcontract-evaluation-
    # semantic flag, so append `enforce` to override whatever the
    # replayed preset command carried.  No-op under default/tsan
    # (already enforce); fixes the release preset.  The flag does not
    # depend on the fixes in utils/toolchain/gcc/patches.  Only appended when
    # contracts are already enabled on the replayed command.
    if any(arg.startswith("-fcontract") for arg in argv):
        argv.append("-fcontract-evaluation-semantic=enforce")

    # A fixture that refuses a target other than the host gets the flags of
    # that target from CRUCIBLE_NEG_EXTRA_FLAGS, which the test sets.  The
    # flags do not go on the fixture target, because they can make the
    # preprocessor fail.  Each guard that preprocesses every unit of the
    # compile database refuses a unit that it cannot read.  The database
    # entry of the fixture then stays a unit of the host.
    extra = os.environ.get("CRUCIBLE_NEG_EXTRA_FLAGS", "")
    if extra:
        import shlex

        argv.extend(shlex.split(extra))

    # The regexes read the records of the SARIF log (the module text, How the
    # regexes apply).  The text output stays, for the reader of the test.
    argv.append(f"{sarif.SARIF_FLAG}{sarif_log}")

    # GCC writes the dependency file also for a compile that fails with an
    # error.  It writes none after a fatal error.
    argv.extend(["-MD", "-MF", str(depfile)])
    return argv


def find_compile_command(build_dir: Path, source: Path, memo: Path) -> tuple[list[str], Path, Roots] | None:
    """Return the command and the working directory of `source` in the compile database, and the roots of the build.

    `memo` keeps the answer with the stat fields of the database, and the
    function uses it while the fields are the same (the module text).  The
    memo also keeps the roots (root_texts).  A configure that changes a root
    writes different paths into the database, and the same database then has
    the same roots.  The search compares the file names as text first, and it resolves
    each name only when no name is equal.  The cost is O(n) in the rows of the
    database, and O(1) with a correct memo.
    """
    database = compdb.database_file(build_dir)
    fields = _stat_fields(os.stat(database))
    text = str(source)
    try:
        recorded = json.loads(memo.read_text())
        roots = recorded["roots"]
        if (recorded["database"] == fields and recorded["source"] == text
                and all(isinstance(arg, str) for arg in recorded["command"]) and recorded["command"]
                and isinstance(roots, list) and len(roots) == 2
                and all(root is None or isinstance(root, str) for root in roots)):
            return list(recorded["command"]), Path(recorded["directory"]), Roots(roots[0], roots[1])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    compiles = compdb.entries(build_dir)
    match = next((entry for entry in compiles if entry.file == text), None)
    if match is None:
        match = next((entry for entry in compiles if Path(entry.file).resolve() == source), None)
    if match is None:
        return None
    command = list(match.argv())
    directory = Path(match.directory)
    source_root, build_root = root_texts(build_dir)
    after = os.stat(database)
    if _stat_fields(after) == fields and after.st_ctime_ns < _now_ns() - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({"database": fields, "source": text, "command": command,
                                            "directory": str(directory),
                                            "roots": [source_root, build_root]}).encode("ascii"))
        except OSError:
            pass
    return command, directory, Roots(source_root, build_root)


def compile_environment(base: Mapping[str, str]) -> dict[str, str]:
    """Return the environment of each compile and of each query of the compiler.

    With LC_MESSAGES=C, GCC reads no message catalog.  LC_ALL takes precedence
    over LC_MESSAGES, so its value goes to LC_CTYPE, which keeps the codeset.
    GCC reads no other locale category.
    """
    env = dict(base)
    every = env.pop("LC_ALL", "")
    if every:
        env["LC_CTYPE"] = every
    env["LC_MESSAGES"] = "C"
    return env


def locale_codeset(env: Mapping[str, str]) -> str:
    """Return the codeset that setlocale(LC_CTYPE, "") gives under `env`.

    GCC uses its quote characters and its text art for UTF-8 only.  A locale
    that the host does not hold leaves GCC in the C locale, and the function
    then gives the codeset of C.
    """
    import locale

    saved = locale.setlocale(locale.LC_CTYPE)
    try:
        try:
            locale.setlocale(locale.LC_CTYPE, env.get("LC_CTYPE") or env.get("LANG") or "C")
        except locale.Error:
            locale.setlocale(locale.LC_CTYPE, "C")
        return locale.nl_langinfo(locale.CODESET)
    finally:
        locale.setlocale(locale.LC_CTYPE, saved)


def run_compile(argv: list[str], directory: Path, env: Mapping[str, str]) -> CompileResult:
    """Compile with `argv` in `directory` under `env`, and return the exit code, the output and the cost.

    The driver runs one child process at a time, so the change of the resource
    use of its waited children over the call is the use of this compile and of
    each process that it waited for.  The instruction counter counts the
    compile and each process that it starts, and not the driver.

    Raises:
        proc.ToolFailed: If the compiler cannot start, or the compile runs past the limit TOOL_S of assay.core.proc.
            The fixture then fails, because a compile that did not end proves no rejection
    """
    import resource

    from assay.core import proc

    meter = _cost_meter_module()
    counter = meter.open_instruction_counter()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    try:
        done = proc.run(proc.Command(argv, timeout_s=proc.TOOL_S, cwd=directory, env=dict(env)))
    except BaseException:
        if counter is not None:
            os.close(counter)
        raise
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    instructions = meter.read_instruction_counter(counter) if counter is not None else None
    return CompileResult(done.exit, done.out + done.err, after.ru_utime - before.ru_utime,
                         after.ru_stime - before.ru_stime, instructions)


# ── The result store ───────────────────────────────────────────────

_STORE_MAGIC = b"crucible-neg-store 8\n"
# An entry holds at most this number of results of one key (the module text).
_RESULTS_PER_ENTRY = 4
# A change in this period before the compile started can be a change that the
# compiler did not see.  The period is longer than one tick of the clock that
# sets file times.
_SETTLE_NS = 1_000_000_000
# The name of a bucket is the first two hexadecimal digits of a key.  A key is
# a SHA-256 hash, so each bucket gets an equal part of the keys.
_BUCKET_DIGITS = 2
_BUCKETS = 16**_BUCKET_DIGITS
# A bucket removes entries until it uses at most this part of its share of the limit.
_EVICT_TO = 0.9
# The part of the size limit that the memos can use.  The entries use the rest.
_MEMO_PART = 1 / 16
# The time between two sweeps of the memos.
_MEMO_SWEEP_SECONDS = 120
# The stamp of the last sweep of the memos, in the directory of the store.
_MEMO_SWEEP_STAMP = "memo-swept"
_DEPFILE_PLACEHOLDER = "<dependency file>"
# The path of the SARIF log of a compile names the process.  The key holds this text for that path.
_SARIF_PLACEHOLDER = f"{sarif.SARIF_FLAG}<SARIF log>"
# The environment variables that GCC reads.  Each one can change the files
# that GCC finds or the text of a diagnostic.  The variables of the loader
# (LD_*) can change the shared objects of the compiler.
_GCC_ENVIRONMENT = frozenset({
    "COLUMNS", "COMPILER_PATH", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH",
    "DEPENDENCIES_OUTPUT", "LANG", "LANGUAGE", "LIBRARY_PATH", "OBJC_INCLUDE_PATH",
    "SOURCE_DATE_EPOCH", "SUNPRO_DEPENDENCIES", "TEMP", "TERM", "TERM_URLS", "TMP", "TMPDIR",
})
_GCC_ENVIRONMENT_PREFIXES = ("GCC_", "LC_", "LD_")
_PLUGIN_OUTPUT = re.compile(r"(?:-fplugin-arg-|--plugin-arg-)[^=]*-out=")
_PROFILE_INPUT = ("-fprofile-use", "-fauto-profile")
# The options that name a file which GCC looks for in the working directory
# first, before the search list.
_WORKING_DIRECTORY_INCLUDES = ("-include", "-imacros", "--include", "--imacros")
# The loader of the C library reads this list of objects in each process.
_PRELOAD_LIST = "/etc/ld.so.preload"
# A root with more names than this is not a search directory of a fixture.
_LISTING_LIMIT = 200_000
# The format of the memo of the answers of the compiler driver.
_ANSWERS_FORMAT = 1
# The format of the record of the hashes of the dependencies of one fixture.
_DIGESTS_FORMAT = 2
_DIGESTS_SUFFIX = ".digests"
# The format of the memo of the budget rows and the ledgers.  Format 2 keeps the path of each ledger as a finding
# names it, and the stamp of a ledger that does not exist.  Format 3 also keeps the thresholds of the rules of the
# test launcher.  Format 4 keeps the kept value of each ledger row with its reason.
_BUDGET_FORMAT = 4

_driver_digest_value: str | None = None


def _now_ns() -> int:
    """Return the time of the clock, in nanoseconds since the epoch.

    CRUCIBLE_NEG_NOW_NS gives an earlier time, for the tests of the driver.  The
    function then returns the earlier of that time and the clock.  Each use of
    the time compares it with the time of a file, and an earlier time only makes
    the driver refuse more results, write fewer memos and remove fewer temporary
    files.  So the variable cannot make the store take a result that the clock
    makes it refuse.
    """
    now = time.time_ns()
    text = os.environ.get("CRUCIBLE_NEG_NOW_NS", "")
    return min(now, int(text)) if text.isdigit() else now


def _driver_digest() -> str:
    """Return the hash of the bytes of the driver: the master utils/bin/loop, then this module."""
    global _driver_digest_value
    if _driver_digest_value is None:
        hasher = hashlib.sha256()
        for path in (_LOOP_MASTER, Path(__file__).resolve()):
            hasher.update(path.read_bytes())
        _driver_digest_value = hasher.hexdigest()
    return _driver_digest_value


def _short_digest(data: bytes) -> str:
    """Return the first 128 bits of the SHA-256 hash of `data`, as hexadecimal text."""
    return hashlib.sha256(data).hexdigest()[:32]


def _stat_fields(status: os.stat_result) -> list[int]:
    """Return the fields of `status` that change when the file changes."""
    return [status.st_dev, status.st_ino, status.st_size, status.st_mtime_ns, status.st_ctime_ns]


def _write_atomic(path: Path, data: bytes) -> None:
    """Write `data` to `path` through a temporary file and a rename.

    A reader sees the previous file or the full new file, and never a part of
    one.  An OSError goes to the caller.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.{os.getpid()}.{os.urandom(4).hex()}.tmp"
    try:
        temporary.write_bytes(data)
        os.replace(temporary, path)
    except OSError:
        temporary.unlink(missing_ok=True)
        raise


def file_digest(path: str, memo_dir: Path) -> str:
    """Return the hash of the bytes of the file `path`.

    The hash of cc1plus (about 400 MB) costs more than the rest of a lookup.  A
    memo keeps each hash with the device, the inode, the size and the two change
    times of the file.  The memo gives the hash only while the five fields are
    the same.  The function writes a memo only for a file that did not change
    in the settle period, so a change in the same clock tick cannot hide.
    """
    status = os.stat(path)
    fields = _stat_fields(status)
    memo = memo_dir / f"file-{_short_digest(os.fsencode(os.path.realpath(path)))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if recorded.get("stat") == fields and isinstance(recorded.get("digest"), str):
            return recorded["digest"]
    except (OSError, ValueError, AttributeError):
        pass
    hasher = hashlib.sha256()
    with open(path, "rb") as stream:
        while chunk := stream.read(1 << 20):
            hasher.update(chunk)
    digest = hasher.hexdigest()[:32]
    after = os.stat(path)
    if _stat_fields(after) == fields and after.st_ctime_ns < _now_ns() - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({"stat": fields, "digest": digest}).encode("ascii"))
        except OSError:
            pass
    return digest


def _ask_driver(driver: str, option: str, directory: Path, env: Mapping[str, str]) -> tuple[str, str]:
    """Return what the compiler driver prints for one option on its standard output and its standard error.

    The output is empty when the driver fails, cannot start or runs past the
    limit QUICK_S of assay.core.proc.  The driver prints the answer of the last
    `-print-*` option only, so each question is one run.
    """
    from assay.core import proc

    try:
        done = proc.run(proc.Command([driver, option], timeout_s=proc.QUICK_S, cwd=directory, env=dict(env)))
    except proc.ToolFailed:
        return "", ""
    return (done.out.strip(), done.err) if done.exit == 0 else ("", "")


def _is_usable(path: str, mode: int) -> bool:
    """Return True when the compiler driver takes the file `path` for the access `mode`.

    For a program (os.X_OK), the driver takes a file that is not a directory
    and that it can run.  For a file that it reads (os.R_OK), it takes a file
    that it can read (gcc.cc, access_check).  shutil.which takes a program of
    PATH with the same test.
    """
    if mode == os.X_OK:
        try:
            if stat.S_ISDIR(os.stat(path).st_mode):
                return False
        except OSError:
            return False
    return os.access(path, mode)


def _search_list(text: str, label: str) -> list[str] | None:
    """Return the directories of one list of `-print-search-dirs` (`programs` or `libraries`), or None.

    The function returns None when the output has no such list, or when a
    directory of the list is not an absolute path.
    """
    prefix = f"{label}: ="
    for line in text.splitlines():
        if line.startswith(prefix):
            directories = line[len(prefix) :].split(os.pathsep)
            return directories if all(os.path.isabs(directory) for directory in directories) else None
    return None


def _which_candidates(name: str, env: Mapping[str, str]) -> list[str]:
    """Return each path, in sequence, where shutil.which looks for the program `name` in the PATH of `env`."""
    seen: set[str] = set()
    candidates: list[str] = []
    for directory in env.get("PATH", "").split(os.pathsep) if env.get("PATH") else []:
        if directory in seen:
            continue
        seen.add(directory)
        candidates.append(os.path.join(directory, name))
    return candidates


def _first_usable(candidates: list[str], mode: int) -> str | None:
    """Return the first candidate that the compiler driver takes for the access `mode`, or None."""
    return next((path for path in candidates if _is_usable(path, mode)), None)


def _answer_candidates(programs: list[str], libraries: list[str], default_assembler: str | None,
                       env: Mapping[str, str]) -> list[list[object]]:
    """Return each path whose state sets an answer of the compiler driver, with its access mode.

    The rows are [path, mode] for cc1plus, for the assembler and for the specs
    file, in the sequence of the search of the driver (the module text, item 6).
    When the driver finds no assembler, it prints the bare name, and the store
    looks in PATH as shutil.which does, so the rows always hold each path of
    PATH where shutil.which looks.
    """
    rows: list[list[object]] = [[os.path.join(directory, "cc1plus"), os.X_OK] for directory in programs]
    if default_assembler is not None:
        rows.append([default_assembler, os.X_OK])
    rows.extend([os.path.join(directory, "as"), os.X_OK] for directory in programs)
    rows.extend([path, os.X_OK] for path in _which_candidates("as", env))
    rows.extend([os.path.join(directory, "specs"), os.R_OK] for directory in libraries)
    return rows


def _model_answers(programs: list[str], libraries: list[str], default_assembler: str | None,
                   env: Mapping[str, str]) -> list[str | None]:
    """Return the answers that the search of the compiler driver gives from the state of its paths now.

    The answers are the path of cc1plus, the path of the assembler and the
    specs text, as `driver_answers` gives them.
    """
    cc1plus = _first_usable([os.path.join(directory, "cc1plus") for directory in programs], os.X_OK)
    assembler = default_assembler if default_assembler is not None and _is_usable(default_assembler, os.X_OK) else None
    if assembler is None:
        assembler = _first_usable([os.path.join(directory, "as") for directory in programs], os.X_OK)
    if assembler is None:
        assembler = _first_usable(_which_candidates("as", env), os.X_OK)
    specs = _first_usable([os.path.join(directory, "specs") for directory in libraries], os.R_OK) or "specs"
    return [cc1plus, assembler, specs]


def _asked_answers(driver: str, directory: Path, env: Mapping[str, str]) -> list[str | None]:
    """Return the path of cc1plus, the path of the assembler and the specs text that the compiler driver prints.

    A program path is None when the driver finds no such program.  GCC prints
    the bare name of a program outside its own directories, and it then finds
    the program through PATH, so the function does that too.
    """
    import shutil

    answers: list[str | None] = []
    for program in ("cc1plus", "as"):
        name = _ask_driver(driver, f"-print-prog-name={program}", directory, env)[0]
        if name and not os.path.isabs(name):
            name = shutil.which(name, path=env.get("PATH")) or ""
        answers.append(name if name and os.path.isfile(name) else None)
    answers.append(_ask_driver(driver, "-print-file-name=specs", directory, env)[0])
    return answers


def driver_answers(driver: str, directory: Path, env: Mapping[str, str], memo_dir: Path) -> list[str | None]:
    """Return the path of cc1plus, the path of the assembler and the specs text of the compiler driver.

    A memo keeps the answers with the state of each path of the search of the
    driver, and it gives them while the driver, its real path, the working
    directory, the environment and each state are the same (the module text,
    item 6).  The function writes the memo only when the search gives the
    answers that the driver printed, and when no state changed during the
    queries.  A check of the memo costs one stat and one access for each path,
    in place of three runs of the driver.
    """
    status = os.stat(driver)
    environment = sorted([name, value] for name, value in env.items()
                         if name in _GCC_ENVIRONMENT or name == "PATH" or name.startswith(_GCC_ENVIRONMENT_PREFIXES))
    material = json.dumps([driver, os.path.realpath(driver), _stat_fields(status), str(directory), os.getcwd(),
                           environment])
    memo = memo_dir / f"answers-{_short_digest(material.encode('utf-8', 'surrogateescape'))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if (recorded["format"] == _ANSWERS_FORMAT
                and all(_is_usable(path, mode) == state for path, mode, state in recorded["candidates"])):
            return list(recorded["answers"])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    search = _ask_driver(driver, "-print-search-dirs", directory, env)[0]
    configured = _ask_driver(driver, "-v", directory, env)[1]
    programs = _search_list(search, "programs")
    libraries = _search_list(search, "libraries")
    default_assembler = next((word.split("=", 1)[1] for line in configured.splitlines()
                              if line.startswith("Configured with:") for word in line.split()
                              if word.startswith("--with-as=")), None)
    candidates = (_answer_candidates(programs, libraries, default_assembler, env)
                  if programs is not None and libraries is not None else None)
    before = [_is_usable(str(path), int(mode)) for path, mode in candidates] if candidates is not None else None
    answers = _asked_answers(driver, directory, env)
    if candidates is None or before is None or programs is None or libraries is None:
        return answers
    after = [_is_usable(str(path), int(mode)) for path, mode in candidates]
    if after == before and _model_answers(programs, libraries, default_assembler, env) == answers:
        rows = [[path, mode, state] for (path, mode), state in zip(candidates, after)]
        try:
            _write_atomic(memo, json.dumps({"format": _ANSWERS_FORMAT, "answers": answers,
                                            "candidates": rows}).encode("utf-8", "surrogateescape"))
        except OSError:
            pass
    return answers


def _host_cpu() -> str:
    """Return the lines of /proc/cpuinfo that tell the model and the flags of the first CPU.

    `-march=native` gives the compiler the features of the host CPU, and no file
    that GCC reads holds them.  The kernel writes the text of each CPU when a
    reader asks for it, so the function reads the first block only.
    """
    first = ""
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="replace") as stream:
            while "\n\n" not in first:
                chunk = stream.read(1 << 16)
                if not chunk:
                    break
                first += chunk
    except OSError:
        return ""
    first = first.split("\n\n", 1)[0]
    fields = {
        "vendor_id", "cpu family", "model", "flags",
        "CPU implementer", "CPU architecture", "CPU variant", "CPU part", "Features",
    }
    return "\n".join(line for line in first.splitlines() if line.split(":", 1)[0].strip() in fields)


def _named_files(arg: str) -> list[str]:
    """Return the texts in one argument that can name a file."""
    names = [arg]
    if "=" in arg:
        names.append(arg.split("=", 1)[1])
    return [name for name in names if name]


def _key_refusal(argv: list[str], directory: Path) -> str:
    """Return the reason that the store cannot key the command `argv`, or an empty text.

    The store refuses a command whose compile writes an output that a stored
    result does not give, or makes a lookup that no search root holds.
    """
    for index, arg in enumerate(argv[1:], start=1):
        if arg.startswith(_PROFILE_INPUT):
            return "the command reads a profile"
        if _PLUGIN_OUTPUT.match(arg):
            return "a plugin of the command writes an output file"
        if arg.startswith("@"):
            return "the command names a response file"
        if arg.startswith("-B"):
            return "the command names a -B prefix"
        if arg.startswith(_PLUGIN_LOAD):
            plugin = arg.split("=", 1)[1]
            # GCC looks for a plugin name with no directory part in its plugin
            # directory or in the search path of the loader.
            if os.sep not in plugin or not os.path.isfile(os.path.join(directory, plugin)):
                return "the command names a plugin by a name that is not a path"
        for option in _WORKING_DIRECTORY_INCLUDES:
            if arg == option:
                name = argv[index + 1] if index + 1 < len(argv) else ""
            elif arg.startswith(option):
                name = arg[len(option) :].removeprefix("=")
            else:
                continue
            if not os.path.isabs(name):
                return f"the command names a file for {option} by a relative name"
    return ""


def compile_key(argv: list[str], directory: Path, protected: set[str], memo_dir: Path,
                env: Mapping[str, str], roots: Roots) -> tuple[str | None, str]:
    """Return the store key of the compile, or None and the reason for no key.

    `argv` ends with `-MF` and the dependency file, and `env` is the compile
    environment.  `protected` holds the resolved paths of the source, the
    object file and the dependency file, which are not inputs of the key.  The
    key holds the inputs of items 1 and 6 of the module text, with the mark of
    each root of `roots` in place of its text (item 8).
    """
    refusal = _key_refusal(argv, directory)
    if refusal:
        return None, refusal
    if os.sep in argv[0]:
        driver: str | None = argv[0]
    else:
        import shutil

        driver = shutil.which(argv[0], path=env.get("PATH"))
    if not driver:
        return None, f"the compiler driver {argv[0]} is not in PATH"
    driver = os.path.join(directory, driver)
    # A fixture imports the store at its start, and tests.guard.test_import_closure holds the imports of that
    # step.  This import is in the function, and it does not change that list.
    from assay.readers import elf

    try:
        cc1plus, assembler, specs = driver_answers(driver, directory, env, memo_dir)
        programs = [driver]
        for program, path in (("cc1plus", cc1plus), ("as", assembler)):
            if path is None:
                return None, f"the compiler driver finds no {program}"
            programs.append(path)
        identity: list[object] = [[path, file_digest(path, memo_dir)] for path in programs]
        specs_text = specs or ""
        has_specs = os.path.isabs(specs_text) and os.path.isfile(specs_text)
        identity.append(["specs", specs_text, file_digest(specs_text, memo_dir) if has_specs else None])
        plugins = [os.path.join(directory, arg.split("=", 1)[1]) for arg in argv if arg.startswith(_PLUGIN_LOAD)]
        for path in [*programs, *plugins]:
            # cc1plus loads a plugin, so its loader maps the objects of the plugin.
            interpreter = elf.interpreter(path) or (elf.interpreter(programs[1]) if path in plugins else None)
            if interpreter is None:
                identity.append([path, "static"])
                continue
            objects, reason = shared_objects(path, interpreter, directory, env, memo_dir)
            if objects is None:
                return None, reason
            identity.append([path, interpreter, [[name, file_digest(name, memo_dir)] for name in objects]])
        files: list[list[int | str]] = []
        for index, arg in enumerate(argv[1:], start=1):
            for name in _named_files(arg):
                full = os.path.join(directory, name)
                if os.path.isfile(full) and os.path.realpath(full) not in protected:
                    files.append([index, name, file_digest(full, memo_dir)])
    except (OSError, elf.ElfError) as error:
        return None, f"an input of the key cannot be read: {error}"
    environment = sorted(
        [name, value]
        for name, value in env.items()
        if name in _GCC_ENVIRONMENT or name.startswith(_GCC_ENVIRONMENT_PREFIXES)
    )
    is_native = any(arg.startswith("-m") and arg.endswith("=native") for arg in argv)
    material = json.dumps(
        roots.relabel_tree({
            "argv": [_SARIF_PLACEHOLDER if arg.startswith(sarif.SARIF_FLAG) else arg for arg in argv[:-1]]
                    + [_DEPFILE_PLACEHOLDER],
            "codeset": locale_codeset(env),
            "directory": str(directory),
            "driver": _driver_digest(),
            "environment": environment,
            "files": files,
            "format": _STORE_MAGIC.decode("ascii"),
            "host": _host_cpu() if is_native else "",
            "identity": identity,
        }),
        sort_keys=True,
    )
    return hashlib.sha256(material.encode("ascii")).hexdigest(), ""


def parse_dependency_file(text: str) -> list[str] | None:
    """Return the prerequisites of the one rule in a dependency file of GCC.

    GCC puts a backslash before a space, a tab and a `#` in a file name, and it
    writes a `$` two times.  The function returns None for a file with no rule
    or with more than one rule.  The cost is O(n) in the length of the text.
    """
    words: list[str] = []
    current: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        follower = text[index + 1] if index + 1 < length else ""
        if char == "\\" and follower in ("\n", " ", "\t", "#"):
            if follower == "\n":
                if current:
                    words.append("".join(current))
                    current = []
            else:
                current.append(follower)
            index += 2
            continue
        if char == "$" and follower == "$":
            current.append("$")
            index += 2
            continue
        if char in " \t\r\n":
            if current:
                words.append("".join(current))
                current = []
            index += 1
            continue
        current.append(char)
        index += 1
    if current:
        words.append("".join(current))
    targets = [position for position, word in enumerate(words) if word.endswith(":")]
    if len(targets) != 1:
        return None
    return words[targets[0] + 1 :]


# ── The compiler identity: the loader ──────────────────────────────


def parse_loader_list(stdout: str, stderr: str, directory: Path) -> tuple[list[str], list[str], list[str]] | None:
    """Return the objects, the tried paths and the caches of one `--list` run of the loader with LD_DEBUG=libs.

    stdout names one object on each line: `name => path (address)`, `path
    (address)`, or an object of the kernel such as `linux-vdso.so.1
    (address)`, which is not a file.  stderr names each path that the loader
    tried (`trying file=PATH`) and each cache that it read (`search
    cache=PATH`).  An empty entry of LD_LIBRARY_PATH gives a tried path
    relative to the working directory.  The function returns None when the
    loader finds no object for a name (`=> not found`).
    """
    objects: list[str] = []
    for line in stdout.splitlines():
        text = line.strip()
        if "=>" in text:
            text = text.split("=>", 1)[1].strip()
            if text.startswith("not found"):
                return None
        if text.startswith("/"):
            objects.append(text.rsplit(" (", 1)[0])
    tried: list[str] = []
    caches: list[str] = []
    for line in stderr.splitlines():
        _, is_try, rest = line.partition("trying file=")
        if is_try:
            tried.append(os.path.join(directory, rest))
            continue
        _, is_cache, rest = line.partition("search cache=")
        if is_cache:
            caches.append(rest.strip())
    return list(dict.fromkeys(objects)), list(dict.fromkeys(tried)), list(dict.fromkeys(caches))


def _exists(path: str) -> bool:
    """Return True when a file has the name `path` after each symbolic link."""
    try:
        os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return False
    return True


def _entry_ctime(path: str) -> int:
    """Return the change time of the file that `path` names, or of the nearest directory above it when none exists.

    A new file at `path` gives that directory a new change time, so a time
    older than a moment shows that the state of `path` did not change after
    that moment.
    """
    current = os.path.realpath(path)
    while True:
        try:
            return os.stat(current).st_ctime_ns
        except OSError:
            parent = os.path.dirname(current)
            if parent == current:
                return 0
            current = parent


def shared_objects(program: str, interpreter: str, directory: Path, env: Mapping[str, str],
                   memo_dir: Path) -> tuple[list[str] | None, str]:
    """Return the shared objects that the loader maps for `program`, or None and the reason.

    The loader `interpreter` lists them as it maps them in a run, and it names
    each path that it tries.  A memo keeps the list.  The memo gives the list
    while each tried path has the same state, and each object, each cache of
    the loader and the preload list have the same device, inode, size and
    times.  The function writes no memo when a file that exists changed in the
    settle period, because the run of the loader can then miss it.  A tried
    path that does not exist needs no such check: when a file appears there,
    the memo check fails.  The driver computes the key again after the
    compile, so a change between this run and the compile stores no result.
    """
    status = os.stat(program)
    loader_variables = sorted([name, value] for name, value in env.items() if name.startswith("LD_"))
    material = json.dumps([program, interpreter, _stat_fields(status), str(directory), loader_variables])
    memo = memo_dir / f"loader-{_short_digest(material.encode('ascii'))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if all(_stat_fields(os.stat(path)) == fields for path, fields in recorded["files"]) and all(
            _exists(path) == exists for path, exists in recorded["tried"]
        ):
            return list(recorded["objects"]), ""
    except (OSError, ValueError, KeyError, TypeError):
        pass
    from assay.core import proc

    loader_env = {name: value for name, value in env.items() if name != "LD_DEBUG_OUTPUT"}
    loader_env["LD_DEBUG"] = "libs"
    started_ns = _now_ns()
    parsed = None
    try:
        done = proc.run(proc.Command([interpreter, "--list", program], timeout_s=proc.QUICK_S, cwd=directory,
                                     env=loader_env))
        if done.exit == 0:
            parsed = parse_loader_list(proc.decoded(done.stdout, "surrogateescape"),
                                       proc.decoded(done.stderr, "surrogateescape"), directory)
    except proc.ToolFailed:
        pass
    if parsed is None:
        return None, f"the loader {interpreter} cannot list the shared objects of {program}"
    objects, tried, caches = parsed
    files = list(dict.fromkeys([*objects, *caches, *([_PRELOAD_LIST] if _exists(_PRELOAD_LIST) else [])]))
    file_rows = [(path, _stat_fields(os.stat(path))) for path in files]
    tried_rows = [(path, _exists(path)) for path in [*tried, _PRELOAD_LIST]]
    settled_ns = started_ns - _SETTLE_NS
    is_settled = all(fields[4] < settled_ns for _, fields in file_rows) and all(
        os.stat(path).st_ctime_ns < settled_ns for path, exists in tried_rows if exists
    )
    if is_settled:
        payload = json.dumps({"objects": objects, "files": file_rows, "tried": tried_rows})
        try:
            _write_atomic(memo, payload.encode("ascii"))
        except OSError:
            pass
    return objects, ""


# ── The search roots ───────────────────────────────────────────────


def _command_without_output(argv: list[str]) -> list[str]:
    """Return `argv` without `-c`, the object file, the SARIF log and the dependency options.

    The search query and the dependency pass give their own mode and output,
    and they write no SARIF log.
    """
    result: list[str] = []
    skip_next = False
    for arg in _strip_dependency_flags(argv):
        if skip_next:
            skip_next = False
            continue
        if arg == "-o":
            skip_next = True
            continue
        if arg == "-c" or (arg.startswith("-o") and len(arg) > 2) or arg.startswith(sarif.SARIF_FLAG):
            continue
        result.append(arg)
    return result


def parse_search_list(text: str) -> list[str] | None:
    """Return each directory that the output of `-E -v` names, or None when the output has no full search list.

    GCC prints each directory of the quote list and of the bracket list on a
    line that starts with a space, between `search starts here:` and `End of
    search list.`.  Before the lists, it prints in quotes each directory that
    it ignores: one that does not exist, and a duplicate of an earlier one.
    These texts are English, which the compile environment gives.
    """
    named: list[str] = []
    is_inside = False
    for line in text.splitlines():
        if line.startswith("ignoring ") and line.count('"') >= 2:
            named.append(line[line.index('"') + 1 : line.rindex('"')])
        elif line.startswith("#include ") and line.endswith("search starts here:"):
            is_inside = True
        elif line == "End of search list.":
            return named
        elif is_inside and line.startswith(" "):
            named.append(line[1:].removesuffix(" (framework directory)"))
    return None


def search_directories(argv: list[str], directory: Path, source: Path, scratch: Path,
                       env: Mapping[str, str]) -> tuple[list[str] | None, str]:
    """Return each directory that GCC names in the search list of the compile, or None and the reason.

    The query preprocesses an empty file with the suffix of the source, with
    the options of the compile, so GCC gives the same search list.
    """
    from assay.core import proc

    empty = scratch / f"search-query{source.suffix}"
    command = [arg for arg in _command_without_output(argv)
               if arg.startswith("-") or os.path.realpath(os.path.join(directory, arg)) != str(source)]
    try:
        empty.write_bytes(b"")
        done = proc.run(proc.Command([*command, "-E", "-v", str(empty), "-o", os.devnull], timeout_s=proc.TOOL_S,
                                     cwd=directory, env=dict(env)))
    except (OSError, proc.ToolFailed) as error:
        return None, f"the search query cannot run: {error}"
    finally:
        empty.unlink(missing_ok=True)
    named = parse_search_list(proc.decoded(done.stderr, "surrogateescape"))
    if named is None:
        return None, f"the compiler prints no search list (exit code {done.exit})"
    return [os.path.join(directory, name) for name in named], ""


def dependency_pass(argv: list[str], directory: Path, scratch: Path,
                    env: Mapping[str, str]) -> tuple[list[str] | None, list[str], str]:
    """Return the dependencies and the missing headers of the source, from a preprocess with `-M -MG`.

    The function returns None and the reason when the preprocess writes no
    dependency rule.  A name in the rule that does not name a file is a header
    that the preprocess did not find, as the source spells it.
    """
    from assay.core import proc

    depfile = scratch / "dependency-pass.d"
    # A -Wfatal-errors of the command stops the pass at the first error, before
    # it writes the rule, and the last of the two options applies.
    pass_options = ["-M", "-MG", "-MF", str(depfile), "-Wno-fatal-errors"]
    try:
        done = proc.run(proc.Command([*_command_without_output(argv), *pass_options], timeout_s=proc.TOOL_S,
                                     cwd=directory, env=dict(env)))
        parsed = parse_dependency_file(depfile.read_text(errors="surrogateescape")) if depfile.exists() else None
    except (OSError, proc.ToolFailed) as error:
        return None, [], f"the dependency pass cannot run: {error}"
    finally:
        depfile.unlink(missing_ok=True)
    if parsed is None:
        return None, [], f"the preprocess with -M -MG writes no dependency rule (exit code {done.exit})"
    present: list[str] = []
    missing: list[str] = []
    for name in dict.fromkeys(parsed):
        full = os.path.join(directory, name)
        if os.path.isfile(full):
            present.append(full)
        else:
            missing.append(name)
    return present, missing, ""


def root_state(path: str) -> list[str] | None:
    """Return the state of a search root or of a probed path.

    The state is None when no file has the name, else the kind of the file
    (_kind) and its real path.  A path that the driver cannot read has the
    state of an error.
    """
    try:
        status = os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return None
    except OSError as error:
        return [f"error {error.errno}", path]
    return [_kind(status), os.path.realpath(path)]


def search_roots(named: list[str], dependencies: list[str], missing: list[str]) -> tuple[list[str], list[str]]:
    """Return the search roots and the probed paths of a compile (items 3 and 4 of the module text).

    The roots are the named directories and the directory of each dependency.
    A dependency path whose part after a root has a `..` component was found
    by a name with that component.  The probes are the path that such a name
    reaches from each root, the path of each missing header with an absolute
    name or a `..` component, and the precompiled name of each one.  The cost
    is O(r * (d + m)) for r roots, d dependencies and m missing headers.
    """
    roots = list(dict.fromkeys([*named, *(os.path.dirname(path) for path in dependencies)]))
    names: list[str] = []
    for path in dependencies:
        if ".." not in path.split(os.sep):
            continue
        for root in roots:
            prefix = root.rstrip(os.sep) + os.sep
            if path.startswith(prefix) and ".." in path[len(prefix) :].split(os.sep):
                names.append(path[len(prefix) :])
    names.extend(name for name in missing if not os.path.isabs(name) and ".." in name.split(os.sep))
    reached = [os.path.join(root, name) for name in dict.fromkeys(names) for root in roots]
    reached.extend(name for name in missing if os.path.isabs(name))
    probes = [path for reach in reached for path in (reach, reach + ".gch")]
    return roots, list(dict.fromkeys(probes))


def listing_roots(states: Mapping[str, list[str] | None]) -> list[str]:
    """Return the real path of each directory root that no other directory root holds.

    The hash of the names under a root covers each directory inside it.  The
    cost is O(r^2) for r roots.
    """
    reals = sorted({state[1] for state in states.values() if state is not None and state[0] == "dir"})
    kept: list[str] = []
    for real in reals:
        if not any(real == outer or real.startswith(outer.rstrip(os.sep) + os.sep) for outer in kept):
            kept.append(real)
    return kept


class Listing:
    """The hash of the names under one root, and what the store reads from them.

    `newest_ctime_ns` is the newest change time of a directory under the root
    or of the target of a link.  `precompiled` names the first precompiled
    header under the root, or is empty.  `entries` counts the names.
    """

    __slots__ = ("digest", "newest_ctime_ns", "precompiled", "entries")

    def __init__(self, digest: str, newest_ctime_ns: int, precompiled: str, entries: int) -> None:
        """Hold the hash, the newest change time, the first precompiled header and the count of names of a root."""
        self.digest = digest
        self.newest_ctime_ns = newest_ctime_ns
        self.precompiled = precompiled
        self.entries = entries


def _walk(root: str, roots: Roots) -> tuple[Listing, list[list[object]], list[list[object]]] | None:
    """Walk the tree under the real directory `root`, and return the hash of its names, its directories and its links.

    Each directory is a row (path, inode, mtime, ctime), and each symbolic link
    is a row (path, target, kind of the file that it names, real path of a
    named directory).  The walk follows each link to a directory, and it walks
    each real directory one time.  The hash holds the path of each directory
    and the target of each link with the marks of `roots` (the module text,
    item 8).  The function returns None for a tree of more than _LISTING_LIMIT
    names.  The cost is O(n log n) for n names, because the names of each
    directory are sorted.
    """
    hasher = hashlib.sha256()
    directories: list[list[object]] = []
    links: list[list[object]] = []
    seen: set[str] = set()
    pending = [root]
    entries = 0
    newest = 0
    precompiled = ""
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        status = os.stat(current)
        directories.append([current, status.st_ino, status.st_mtime_ns, status.st_ctime_ns])
        newest = max(newest, status.st_ctime_ns)
        with os.scandir(current) as iterator:
            items = sorted(iterator, key=lambda item: item.name)
        hasher.update(b"D" + os.fsencode(roots.relabel(current)) + b"\0")
        for item in items:
            entries += 1
            name = os.fsencode(item.name)
            if item.name.endswith(".gch") and not precompiled:
                precompiled = item.path
            if item.is_symlink():
                target = os.readlink(item.path)
                try:
                    kind: str | None = _kind(os.stat(item.path))
                except (FileNotFoundError, NotADirectoryError):
                    kind = None
                real = os.path.realpath(item.path) if kind == "dir" else None
                links.append([item.path, target, kind, real])
                newest = max(newest, _entry_ctime(item.path))
                hasher.update(b"l" + name + b"\0" + os.fsencode(roots.relabel(target)) + b"\0" + str(kind).encode()
                              + b"\0")
                if real is not None:
                    pending.append(real)
            elif item.is_dir(follow_symlinks=False):
                hasher.update(b"d" + name + b"\0")
                pending.append(item.path)
            else:
                hasher.update((b"f" if item.is_file(follow_symlinks=False) else b"o") + name + b"\0")
        if entries > _LISTING_LIMIT:
            return None
    return Listing(hasher.hexdigest()[:32], newest, precompiled, entries), directories, links


def _stamps_agree(directories: list[list[object]]) -> bool:
    """Return True when each directory has the same inode and the same two change times."""
    for path, inode, mtime_ns, ctime_ns in directories:
        try:
            status = os.stat(str(path))
        except OSError:
            return False
        if (status.st_ino, status.st_mtime_ns, status.st_ctime_ns) != (inode, mtime_ns, ctime_ns):
            return False
    return True


def _links_agree(links: list[list[object]]) -> bool:
    """Return True when each symbolic link has the same target, the same kind of named file and the same real path."""
    for path, target, kind, real in links:
        try:
            if os.readlink(str(path)) != target:
                return False
            now_kind: str | None = _kind(os.stat(str(path)))
        except (FileNotFoundError, NotADirectoryError):
            now_kind = None
        except OSError:
            return False
        if now_kind != kind or (real is not None and os.path.realpath(str(path)) != real):
            return False
    return True


def tree_listing(root: str, memo_dir: Path, roots: Roots) -> tuple[Listing | None, str]:
    """Return the hash of the names under the real directory `root`, with the marks of `roots`, or None and the reason.

    A memo keeps the hash with the stamp of each directory and each link
    (item 7 of the module text).  The roots of the marks are part of the name
    of the memo.  The function writes the memo only when no directory changed
    in the settle period before the walk.  A check of a memo costs one stat
    for each directory and two for each link.  A walk of /usr/include, with
    1,600 directories, costs about 40 ms, and the check of its memo about 5 ms.
    """
    material = json.dumps([root, roots.identity()]).encode("utf-8", "surrogateescape")
    memo = memo_dir / f"listing-{_short_digest(material)}.json"
    try:
        recorded = json.loads(memo.read_text())
        if (recorded["root"] == root and recorded["roots"] == roots.identity()
                and _stamps_agree(recorded["directories"]) and _links_agree(recorded["links"])):
            return Listing(*recorded["listing"]), ""
    except (OSError, ValueError, KeyError, TypeError):
        pass
    started_ns = _now_ns()
    try:
        walked = _walk(root, roots)
    except OSError as error:
        return None, f"the search directory {root} cannot be read: {error}"
    if walked is None:
        return None, f"the search directory {root} holds more than {_LISTING_LIMIT} names"
    listing, directories, links = walked
    if not (_stamps_agree(directories) and _links_agree(links)):
        return None, f"the search directory {root} changed during its walk"
    if listing.newest_ctime_ns < started_ns - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({
                "root": root,
                "roots": roots.identity(),
                "listing": [listing.digest, listing.newest_ctime_ns, listing.precompiled, listing.entries],
                "directories": directories,
                "links": links,
            }).encode("ascii"))
        except OSError:
            pass
    return listing, ""


def _kind(status: os.stat_result) -> str:
    """Return the kind of a file: "file" for a regular file, "dir" for a directory, or "other"."""
    if stat.S_ISREG(status.st_mode):
        return "file"
    return "dir" if stat.S_ISDIR(status.st_mode) else "other"


def encode_entry(entry: dict[str, object]) -> bytes:
    """Return the bytes of a store entry: a format line, a checksum and the packed entry."""
    payload = zlib.compress(json.dumps(entry, separators=(",", ":")).encode("ascii"), 6)
    return _STORE_MAGIC + hashlib.sha256(payload).hexdigest().encode("ascii") + b"\n" + payload


def _is_text_pairs(value: object) -> bool:
    """Return True when `value` is a list of pairs of texts."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and all(isinstance(part, str) for part in item) for item in value
    )


def _is_states(value: object) -> bool:
    """Return True when `value` is a list of pairs of a path and a `root_state` result."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and isinstance(item[0], str)
        and (item[1] is None or _is_text_pairs([item[1]]))
        for item in value
    )


def _is_index_pairs(value: object, count: int) -> bool:
    """Return True when `value` is a list of pairs of an index below `count` and a text."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and isinstance(item[0], int) and not isinstance(item[0], bool)
        and 0 <= item[0] < count and isinstance(item[1], str)
        for item in value
    )


def _is_log(value: object) -> bool:
    """Return True when `value` is None or a log in the stored shape of assay.readers.sarif (sarif.to_json).

    A result of the fixture driver always holds a log.  A result of a
    different user of the store, such as assay.oracle.unit_store, can hold
    none.
    """
    if value is None:
        return True
    try:
        sarif.from_json(value)
    except sarif.SarifError:
        return False
    return True


def _is_result(value: object, path_count: int) -> bool:
    """Return True when `value` is a correct result of an entry with `path_count` dependency paths."""
    if not isinstance(value, dict) or not _is_log(value.get("log")):
        return False
    returncode = value.get("returncode")
    times = (value.get("user_s"), value.get("system_s"))
    instructions = value.get("instructions", False)
    return (
        _is_index_pairs(value.get("dependencies"), path_count)
        and _is_states(value.get("roots"))
        and _is_states(value.get("probes"))
        and _is_text_pairs(value.get("listings"))
        and isinstance(returncode, int)
        and not isinstance(returncode, bool)
        and isinstance(value.get("output"), str)
        and all(
            isinstance(time_s, (int, float)) and not isinstance(time_s, bool) and 0 <= time_s < float("inf")
            for time_s in times
        )
        and (instructions is None or (isinstance(instructions, int) and not isinstance(instructions, bool)
                                      and instructions >= 0))
    )


def decode_entry(blob: bytes) -> dict[str, object] | None:
    """Return the entry in `blob`, or None when the bytes are not a correct entry.

    An entry holds `paths`, the dependency paths of all its results, and
    `results`, from one result to _RESULTS_PER_ENTRY results, newest first.  A
    dependency of a result is a pair of an index in `paths` and a hash.
    """
    if not blob.startswith(_STORE_MAGIC):
        return None
    checksum, newline, payload = blob[len(_STORE_MAGIC) :].partition(b"\n")
    if not newline or hashlib.sha256(payload).hexdigest().encode("ascii") != checksum:
        return None
    try:
        entry = json.loads(zlib.decompress(payload))
    except (zlib.error, ValueError):
        return None
    if not isinstance(entry, dict):
        return None
    paths = entry.get("paths")
    results = entry.get("results")
    is_well_formed = (
        isinstance(paths, list)
        and all(isinstance(name, str) for name in paths)
        and isinstance(results, list)
        and 1 <= len(results) <= _RESULTS_PER_ENTRY
        and all(_is_result(result, len(paths)) for result in results)
    )
    return entry if is_well_formed else None


def pack_entry(results: list[dict[str, object]]) -> dict[str, object]:
    """Return the entry of `results`, whose dependencies are pairs of a path and a hash.

    The entry keeps each path one time, in `paths`, and each result names a
    path by its index there.  The cost is O(r * d) for r results of d
    dependencies.
    """
    indices: dict[str, int] = {}
    packed: list[dict[str, object]] = []
    for result in results:
        dependencies = [[indices.setdefault(name, len(indices)), digest]
                        for name, digest in result["dependencies"]]  # type: ignore[attr-defined]
        packed.append({**result, "dependencies": dependencies})
    return {"paths": list(indices), "results": packed}


def unpack_results(entry: dict[str, object]) -> list[dict[str, object]]:
    """Return the results of a correct entry, with each dependency as a pair of a path and a hash."""
    paths: list[str] = entry["paths"]  # type: ignore[assignment]
    return [{**result, "dependencies": [[paths[index], digest] for index, digest in result["dependencies"]]}
            for result in entry["results"]]  # type: ignore[attr-defined]


def _result_inputs(result: Mapping[str, object]) -> tuple[object, ...]:
    """Return the inputs of a result (items 2 to 4 of the module text), for a comparison."""
    return result["dependencies"], result["roots"], result["probes"], result["listings"]


def merge_results(new_result: dict[str, object], earlier: list[dict[str, object]]) -> list[dict[str, object]]:
    """Return the results of an entry after the write of `new_result` into an entry with the results `earlier`.

    The new result goes first.  Each earlier result with the same inputs goes,
    and the list keeps at most _RESULTS_PER_ENTRY results, newest first.
    """
    inputs = _result_inputs(new_result)
    kept = [old for old in earlier if _result_inputs(old) != inputs]
    return [new_result, *kept][:_RESULTS_PER_ENTRY]


class DigestMemo:
    """The hash of each dependency that one fixture read, with the stat fields of the file at the time of the hash.

    The memo is `NAME.digests` in the scratch directory of the fixture (the
    module text, item 2).  It gives the kept hash of a file while the device,
    the inode, the size and the two change times of the file are the same.  It
    keeps a hash only for a file that did not change in the settle period
    before the read, so a change in the same clock tick cannot hide.  With each
    hash it keeps whether the bytes hold the text of a root (item 8).  One
    build directory has one pair of roots, so the answer stays correct.  The
    memo keeps the files of the last check only, so it holds no more than the
    dependencies of one fixture.  A check of a kept hash costs one stat.
    """

    def __init__(self, path: Path | None, roots: Roots) -> None:
        """Read the memo at `path`, or use no memo when `path` is None.  `roots` gives the texts of the roots."""
        self.path = path
        self.roots = roots
        self.kept: dict[str, list[object]] = {}
        self.used: dict[str, list[object]] = {}
        if path is None:
            return
        try:
            recorded = json.loads(path.read_text())
        except (OSError, ValueError):
            return
        is_current = (isinstance(recorded, dict) and recorded.get("format") == _DIGESTS_FORMAT
                      and recorded.get("roots") == roots.identity())
        files = recorded.get("files") if is_current else None
        if isinstance(files, dict):
            self.kept = files

    def digest(self, name: str) -> tuple[str | None, bool]:
        """Return the hash of the bytes of the file `name`, and True when the bytes hold the text of a root.

        The hash is None when the file cannot be read.
        """
        try:
            fields = _stat_fields(os.stat(name))
        except OSError:
            return None, False
        kept = self.kept.get(name)
        if (isinstance(kept, list) and len(kept) == 7 and kept[:5] == fields and isinstance(kept[5], str)
                and isinstance(kept[6], bool)):
            self.used[name] = kept
            return kept[5], kept[6]
        try:
            with open(name, "rb") as stream:
                data = stream.read()
            after = os.stat(name)
        except OSError:
            return None, False
        digest = _short_digest(data)
        holds_root = self.roots.holds_root(data)
        if _stat_fields(after) == fields and after.st_ctime_ns < _now_ns() - _SETTLE_NS:
            self.used[name] = [*fields, digest, holds_root]
        return digest, holds_root

    def save(self) -> None:
        """Write the memo when the files of this check differ from the kept files.  A failed write changes nothing."""
        if self.path is None or self.used == self.kept:
            return
        try:
            _write_atomic(self.path, json.dumps({"format": _DIGESTS_FORMAT, "roots": self.roots.identity(),
                                                 "files": self.used}).encode("ascii"))
        except OSError:
            pass


class InputCheck:
    """The check of the inputs of the results of one entry.

    The check finds the hash of each dependency, the state of each root and
    probed path, and the listing of each root one time, and it compares each
    result with these answers.  The answers come from the same moment, so a
    result matches when its inputs are the inputs at that moment.  The cost is
    O(n + d + r * k): n bytes of dependencies that the digest memo does not
    hold, d directories under the roots, and k inputs in each of r results.
    """

    def __init__(self, memo_dir: Path, digests: DigestMemo, roots: Roots) -> None:
        """Make a check that keeps the memos of the root listings in `memo_dir` and reads hashes through `digests`.

        A stored result names each path with the marks of the roots (the
        module text, item 8), and `roots` gives the paths of this build.
        """
        self.memo_dir = memo_dir
        self.digest_memo = digests
        self.roots = roots
        self.digests: dict[str, tuple[str | None, bool]] = {}
        self.states: dict[str, list[str] | None] = {}
        self.listings: dict[str, tuple[Listing | None, str]] = {}

    def digest(self, name: str) -> tuple[str | None, bool]:
        """Return the hash of the bytes of the file `name`, or None, and True when the bytes hold the text of a root."""
        if name not in self.digests:
            self.digests[name] = self.digest_memo.digest(name)
        return self.digests[name]

    def state(self, name: str) -> list[str] | None:
        """Return the `root_state` of the path `name`, with the marks of the roots."""
        if name not in self.states:
            state = root_state(name)
            self.states[name] = None if state is None else [state[0], self.roots.relabel(state[1])]
        return self.states[name]

    def listing(self, root: str) -> tuple[Listing | None, str]:
        """Return the `tree_listing` of the root `root`."""
        if root not in self.listings:
            self.listings[root] = tree_listing(root, self.memo_dir, self.roots)
        return self.listings[root]

    def reason(self, result: Mapping[str, object]) -> str:
        """Return the reason that `result` does not match, or an empty text when each input is the same.

        The dependencies come first.  An edit changes a dependency more
        frequently than a new file changes a root, so an edit stops the check
        before the walk of a root.  A dependency whose bytes hold the text of a
        root of this build does not match (item 8).  The reason names each
        path of this build.
        """
        restore = self.roots.restore
        for name, digest in result["dependencies"]:  # type: ignore[attr-defined]
            path = restore(name)
            now, holds_root = self.digest(path)
            if now is None:
                return f"a dependency cannot be read: {path}"
            if now != digest:
                return f"a dependency changed: {path}"
            if holds_root:
                return f"a dependency holds the text of a root: {path}"
        for name, state in result["roots"]:  # type: ignore[attr-defined]
            if self.state(restore(name)) != state:
                return f"a search root changed: {restore(name)}"
        for name, state in result["probes"]:  # type: ignore[attr-defined]
            if self.state(restore(name)) != state:
                return f"a probed path changed: {restore(name)}"
        for root, digest in result["listings"]:  # type: ignore[attr-defined]
            listing, why = self.listing(restore(root))
            if listing is None or listing.digest != digest:
                return why or f"a search directory changed: {restore(root)}"
        return ""


class ResultStore:
    """The store of compile results in one directory.

    `entries/` holds one file for each key, and `memo/` holds the memos of
    `file_digest`, `driver_answers`, `shared_objects`, `tree_listing` and
    `read_cost_budget`.  Many fixtures read and write the store at the same
    time.  Each write goes through a temporary file and a rename, and a reader
    that loses a file to a removal counts a miss.
    """

    def __init__(self, root: Path, limit_bytes: int) -> None:
        """Use the store in `root`, with a size limit of `limit_bytes`."""
        self.root = root
        self.entries = root / "entries"
        self.memo = root / "memo"
        self.limit_bytes = limit_bytes

    def entry_path(self, key: str) -> Path:
        """Return the path of the entry for `key`, in the bucket of the key."""
        return self.entries / key[:_BUCKET_DIGITS] / key

    def lookup(self, key: str, digests: DigestMemo, roots: Roots) -> tuple[CompileResult | None, str]:
        """Return the stored result for `key`, or None and the reason for a miss.

        The function returns the first result of the entry whose inputs are
        the same (InputCheck): each dependency has the same bytes, each search
        root and each probed path has the same state, and the names under each
        root are the same.  The output and the inputs of the result get the
        paths of `roots` in place of the marks (the module text, item 8).  The
        reason for a miss is the reason of the newest result.  The cost is the
        cost of one InputCheck.
        """
        path = self.entry_path(key)
        try:
            blob = path.read_bytes()
        except OSError:
            return None, "no entry"
        entry = decode_entry(blob)
        if entry is None:
            return None, "the entry is damaged"
        check = InputCheck(self.memo, digests, roots)
        newest_reason = ""
        for result in unpack_results(entry):
            reason = check.reason(result)
            if reason:
                newest_reason = newest_reason or reason
                continue
            try:
                os.utime(path)
            except OSError:
                pass
            inputs = tuple(roots.restore(name) for name, _ in result["dependencies"])  # type: ignore[attr-defined]
            stored_log = result["log"]
            log = sarif.map_texts(sarif.from_json(stored_log), roots.restore) if stored_log is not None else None
            return CompileResult(result["returncode"], roots.restore(result["output"]),  # type: ignore[arg-type]
                                 float(result["user_s"]), float(result["system_s"]),  # type: ignore[arg-type]
                                 result["instructions"], inputs, log,  # type: ignore[arg-type]
                                 "the stored result holds no SARIF log"), ""
        return None, newest_reason

    def record(
        self,
        key: str,
        result: CompileResult,
        dependencies: list[str],
        missing: list[str],
        named: list[str],
        started_ns: int,
        roots: Roots,
    ) -> tuple[bool, str]:
        """Store `result` for `key`, and return True, or False and the reason.

        `dependencies` holds each file that GCC read, `missing` each header that
        a fatal error did not find, and `named` each directory of the search
        list.  The function refuses a result whose inputs changed in the settle
        period before `started_ns`, and a result that a precompiled header or a
        root that it cannot read can change.  It also refuses a result whose
        output or log holds a NUL character.  It refuses a dependency that
        holds the text of a root of `roots`.  The result names each path, and
        its output and its log name each root, with the marks of `roots` (the
        module text, item 8).  A result with no log keeps no log: the fixture
        driver stores no such result, and a different user of the store can
        (`_is_log`).  The new
        result goes first in the entry.  The entry drops each earlier result
        with the same inputs, and keeps at most _RESULTS_PER_ENTRY results.  Two
        writers of one entry at the same time can lose one result, which costs
        one compile later.  The cost is O(n + d) for n bytes of dependencies and
        d directories under the roots, plus a walk of each root that has no memo.
        """
        log_texts = sarif.texts(result.log) if result.log is not None else []
        if "\x00" in result.output or any("\x00" in text for text in log_texts):
            return False, "the output or the log holds a NUL character, which the store uses in a mark"
        settled_ns = started_ns - _SETTLE_NS
        recorded: list[list[str]] = []
        for name in dependencies:
            try:
                before = os.stat(name)
                data = Path(name).read_bytes()
                after = os.stat(name)
            except OSError:
                return False, f"a dependency cannot be read: {name}"
            if _stat_fields(before) != _stat_fields(after) or after.st_ctime_ns >= settled_ns:
                return False, f"a dependency changed less than one second before the compile: {name}"
            if roots.holds_root(data):
                return False, f"a dependency holds the text of a root: {name}"
            recorded.append([roots.relabel(name), _short_digest(data)])
        searched, probes = search_roots(named, dependencies, missing)
        states = {path: root_state(path) for path in searched}
        probe_states = {path: root_state(path) for path in probes}
        for path, state in [*states.items(), *probe_states.items()]:
            if state is not None and state[0].startswith("error"):
                return False, f"a search root cannot be read: {path}"
            if state is not None and path in probe_states and path.endswith(".gch"):
                return False, f"a precompiled header is on a probed path: {path}"
        listed = [(root, *tree_listing(root, self.memo, roots)) for root in listing_roots(states)]
        for root, listing, reason in listed:
            if listing is None:
                return False, reason
            if listing.precompiled:
                return False, f"a precompiled header is in a search directory: {listing.precompiled}"
        for root, listing, _ in listed:
            if listing is not None and listing.newest_ctime_ns >= settled_ns:
                return False, f"a search directory changed less than one second before the compile: {root}"
        changed = next((path for path in [*states, *probe_states] if _entry_ctime(path) >= settled_ns), None)
        if changed is not None:
            return False, f"a search root changed less than one second before the compile: {changed}"
        listings = [[roots.relabel(root), listing.digest] for root, listing, _ in listed if listing is not None]
        new_result: dict[str, object] = {
            "dependencies": recorded,
            "roots": roots.relabel_tree([[path, state] for path, state in states.items()]),
            "probes": roots.relabel_tree([[path, state] for path, state in probe_states.items()]),
            "listings": listings,
            "output": roots.relabel(result.output),
            "log": sarif.to_json(sarif.map_texts(result.log, roots.relabel)) if result.log is not None else None,
            "returncode": result.returncode,
            "user_s": round(result.user_s, 3),
            "system_s": round(result.system_s, 3),
            "instructions": result.instructions,
        }
        path = self.entry_path(key)
        try:
            earlier = decode_entry(path.read_bytes())
        except OSError:
            earlier = None
        results = merge_results(new_result, unpack_results(earlier) if earlier is not None else [])
        try:
            _write_atomic(path, encode_entry(pack_entry(results)))
        except OSError as error:
            return False, f"the entry cannot be written: {error}"
        self.evict(path)
        self.sweep_memos()
        return True, ""

    def evict(self, written: Path) -> None:
        """Remove the entries of the bucket of `written` that the store did not use for the longest time.

        Each bucket gets an equal share of the part of the limit that the
        entries use.  When the bucket uses more than its share, the function
        removes entries until the bucket uses at most 90 percent of its share.
        It keeps `written`, the entry that the caller wrote, also when that
        entry alone is larger than the share.  It also removes each temporary
        file of the bucket older than one hour, the remains of a writer that
        stopped.  The cost is O(m log m) in the m entries of one bucket, which
        hold about 1/256 of the store.
        """
        expired_ns = _now_ns() - 3600 * 1_000_000_000
        try:
            with os.scandir(written.parent) as iterator:
                names = list(iterator)
        except OSError:
            return
        files = [file for file in _removable(names, expired_ns) if file[2] != str(written)]
        share = int(self.limit_bytes * (1 - _MEMO_PART)) // _BUCKETS
        try:
            total = sum(size for _, size, _ in files) + _allocated(written.stat())
        except OSError:
            return
        _remove_oldest(files, total, share)

    def sweep_memos(self) -> None:
        """Remove the memos with the oldest write while the memos use more than _MEMO_PART of the limit.

        One write each _MEMO_SWEEP_SECONDS does the sweep, so a cold run of
        many fixtures reads the memo directory rarely.  A memo gets a new write
        only when its file changes, so a memo of a removed work tree goes
        first.  A reader that loses a memo hashes its file again.  The sweep
        also removes each temporary file of the memos older than one hour.  The
        cost is O(m log m) in the m memos.
        """
        stamp = self.root / _MEMO_SWEEP_STAMP
        now_ns = _now_ns()
        try:
            if now_ns - stamp.stat().st_mtime_ns < _MEMO_SWEEP_SECONDS * 1_000_000_000:
                return
        except FileNotFoundError:
            pass
        try:
            stamp.touch()
            with os.scandir(self.memo) as iterator:
                names = list(iterator)
        except OSError:
            return
        files = _removable(names, now_ns - 3600 * 1_000_000_000)
        _remove_oldest(files, sum(size for _, size, _ in files), int(self.limit_bytes * _MEMO_PART))


def _allocated(status: os.stat_result) -> int:
    """Return the bytes that a file takes on the disk."""
    return status.st_blocks * 512


def _removable(items: list[os.DirEntry], expired_ns: int) -> list[tuple[int, int, str]]:
    """Return (mtime, allocated bytes, path) of each file of a listing, and remove each old temporary file.

    A temporary file is old when its mtime is before `expired_ns`.
    """
    files: list[tuple[int, int, str]] = []
    for item in items:
        try:
            status = item.stat(follow_symlinks=False)
        except OSError:
            continue
        if item.name.endswith(".tmp"):
            if status.st_mtime_ns < expired_ns:
                Path(item.path).unlink(missing_ok=True)
            continue
        files.append((status.st_mtime_ns, _allocated(status), item.path))
    return files


def _remove_oldest(files: list[tuple[int, int, str]], total: int, share: int) -> None:
    """When `total` is over `share`, remove the oldest of `files` until the total is at most _EVICT_TO of the share."""
    if total <= share:
        return
    goal = int(share * _EVICT_TO)
    for _, size, name in sorted(files):
        if total <= goal:
            break
        Path(name).unlink(missing_ok=True)
        total -= size


def _store_limit_bytes(default: int) -> int:
    """Return the size limit of the store from CRUCIBLE_NEG_CACHE_MAX_MB, else `default`."""
    text = os.environ.get("CRUCIBLE_NEG_CACHE_MAX_MB", "")
    if not text:
        return default
    megabytes = int(text) if text.isdigit() else 0
    if megabytes <= 0:
        print(
            f"CRUCIBLE_NEG_CACHE_MAX_MB is '{text}'.  Set it to a positive number of megabytes.  "
            f"The store uses {default >> 20} MB.",
            file=sys.stderr,
        )
        return default
    return megabytes << 20


def open_store() -> tuple[ResultStore | None, str]:
    """Return the result store, or None and the reason when the store is off."""
    if os.environ.get("CRUCIBLE_NEG_CACHE") == "0":
        return None, "the store is off"
    from assay.core import cache as cache_dir

    try:
        root = cache_dir.cache_root("neg")
        if root is None:
            return None, f"{cache_dir.ROOT_VARIABLE} turns every cache off"
        (root / "entries").mkdir(parents=True, exist_ok=True)
        (root / "memo").mkdir(parents=True, exist_ok=True)
    except OSError as error:
        return None, f"the store directory cannot be made: {error}"
    return ResultStore(root, _store_limit_bytes(cache_dir.LIMITS["neg"])), ""


def _note(fixture_name: str, text: str) -> None:
    """Print one line about the store for the fixture."""
    print(f"neg-compile {fixture_name}: {text}", file=sys.stderr)


# ── The cost budget of a compile ───────────────────────────────────

_REPO_ROOT = REPO_ROOT
# The directory of the ledgers of the rules, utils/ledgers.  It is the directory of assay.core.ledger, and the
# test of the driver holds the two equal.  The driver does not import that module for each run.
_LEDGER_DIRECTORY = _REPO_ROOT / "utils" / "ledgers"
# The registry of the measured rules, utils/registry.txt.
_REGISTRY = _REPO_ROOT / "utils" / "registry.txt"
# The master that ctest starts for each fixture.  Its bytes are a part of the key (the module text, item 1).
_LOOP_MASTER = LOOP
_CPU_CHECK = "fixture-cpu"
_INSTRUCTIONS_CHECK = "fixture-instructions"
# The unit of each check, as utils/registry.txt gives it.  The noise of a ledger row comes from it (assay.core.budget).
_UNITS = {_CPU_CHECK: "s", _INSTRUCTIONS_CHECK: "G"}
# The variable that names a different ledger for each check, and the ledger of the tree.
_LEDGERS = {
    _CPU_CHECK: ("CRUCIBLE_NEG_CPU_LEDGER", _LEDGER_DIRECTORY / f"{_CPU_CHECK}.txt"),
    _INSTRUCTIONS_CHECK: ("CRUCIBLE_NEG_INSTRUCTIONS_LEDGER", _LEDGER_DIRECTORY / f"{_INSTRUCTIONS_CHECK}.txt"),
}
_GIGA = 1e9
_ADVICE = "Include only the header that the fixture attacks, or make the tables that it does not read lazy."
# The verdict of an error of the CPU time when the instruction count holds the error level (_row_verdict()).
_DEMOTED = "demoted"


class BudgetRow:
    """The thresholds of one measured rule of a fixture compile, and the kept value and the reason of each fixture of
    its ledger."""

    __slots__ = ("check", "warn", "error", "ledger", "ledger_shown")

    def __init__(self, check: str, warn: float, error: float, ledger: dict[str, tuple[float, str]],
                 ledger_shown: str) -> None:
        """Hold the check name, the two thresholds, the ledger rows and the path of the ledger of one rule, relative
        to the root of the tree when the ledger is in the tree."""
        self.check = check
        self.warn = warn
        self.error = error
        self.ledger = ledger
        self.ledger_shown = ledger_shown


class CostBudget:
    """The measured rules fixture-cpu and fixture-instructions, with their ledgers, and the rules of the test launcher.

    `problems` gives, for each check whose rule or ledger cannot be read, the
    reason.  A budget with a problem fails the test.  `test_rows` gives the
    two thresholds of each rule of the test launcher, from the registry of
    the launcher (assay.loop.teststep, THE MEASURED RULES).  The launcher
    reads their ledgers itself.
    """

    __slots__ = ("rows", "problems", "test_rows")

    def __init__(self, rows: dict[str, BudgetRow], problems: dict[str, str],
                 test_rows: dict[str, tuple[float, float]] | None = None) -> None:
        """Hold the rules by check name, the reason of each check with a problem, and the rules of the test launcher."""
        self.rows = rows
        self.problems = problems
        self.test_rows = test_rows or {}

    def is_over_error(self, fixture_name: str, result: CompileResult) -> bool:
        """Return True when the measure of the rule that holds the error level fails the test.

        The instruction count holds the error level when the compile has one,
        and the CPU time holds it when the compile has no count.  A ledger row
        keeps the measure up to its kept value with the noise of its unit
        (assay.core.budget, THE NOISE).
        """
        if self.problems:
            return False
        if result.instructions is not None:
            check, value = _INSTRUCTIONS_CHECK, result.instructions / _GIGA
        else:
            check, value = _CPU_CHECK, result.cpu_s
        found = _row_verdict(self.rows[check], fixture_name, value, True)
        return found == cost_budget.ERROR or (
            found == cost_budget.RISEN and cost_budget.risen_level(value, self.rows[check].error) == cost_budget.ERROR
        )

    @staticmethod
    def excess_text(result: CompileResult) -> str:
        """Return the words that tell which measure of a compile holds the error level, for a note."""
        return "ran more user instructions" if result.instructions is not None else "took more CPU time"


def _report_module() -> ModuleType:
    """Return the module utils/assay/core/report.py."""
    from assay.core import report as check_report

    return check_report


def _cost_meter_module() -> ModuleType:
    """Return the module utils/assay/core/meter.py."""
    from assay.core import meter as cost_meter

    return cost_meter


def _sections_module() -> ModuleType:
    """Return the module utils/assay/readers/sections.py, which only the census of a compile that succeeds reads."""
    from assay.readers import sections as plugin_sections

    return plugin_sections


def _shown(path: Path) -> str:
    """Return a path relative to the root of the tree when it is in the tree, else the path itself."""
    try:
        return str(path.relative_to(_REPO_ROOT))
    except ValueError:
        return str(path)


def _read_fixture_ledger(path: Path, check: str) -> tuple[dict[str, tuple[float, str]], str]:
    """Read one ledger of fixtures, and return the kept value and the reason of each fixture, or the problem.

    A row is `- | - | VALUE | FIXTURE | REASON` (the module text, The reason of
    the failure and The cost of a compile).  A ledger that does not exist has
    no row, and the call then imports no module.  The ledger has the policy of
    the rule of the check in the registry.  A row with the value admit has the
    kept value 0.

    Args:
        path: The ledger
        check: The check, which the row `rule CHECK` of the ledger names

    Raises:
        OSError: If the ledger exists and cannot be read, or the registry cannot be read
    """
    if not path.is_file():
        return {}, ""
    from assay.core import ledger as rule_ledger
    from assay.core.registry import Policy

    try:
        ledger = rule_ledger.read(rule_ledger.rule_of(check), file=path)
    except ValueError as problem:
        return {}, str(problem)
    value = rule_ledger.ADMIT_VALUE if ledger.policy == Policy.ADMIT else "VALUE"
    rows: dict[str, tuple[float, str]] = {}
    for key, entry in sorted(ledger.base.items(), key=lambda pair: pair[1].line):
        if key.path or key.kind or not key.entity:
            return {}, (f"{path}:{entry.line}: a row is `- | - | {value} | FIXTURE | REASON`, with the name of the "
                        f"fixture")
        rows[key.entity] = (0.0 if isinstance(entry.value, str) else float(entry.value), entry.reason)
    return rows, ""


def _parse_cost_budget(registry: Path, ledger_paths: Mapping[str, Path], test_registry: Path) -> CostBudget:
    """Read the measured rules fixture-cpu and fixture-instructions of the registry `registry`, the ledgers, and the
    rules of the test launcher of the registry `test_registry`."""
    from assay.loop import teststep as test_launcher

    rows: dict[str, BudgetRow] = {}
    problems: dict[str, str] = {}
    try:
        test_rows = _cost_meter_module().budget_rows(str(test_registry), test_launcher.ROWS)
    except (OSError, ValueError):
        test_rows = {}
    try:
        budgets = _report_module().read_budgets(registry)
    except (OSError, ValueError) as error:
        return CostBudget({}, {check: f"the budget of {check} cannot be read: {error}" for check in _LEDGERS},
                          test_rows)
    for check, path in ledger_paths.items():
        row = budgets.get(check)
        if row is None:
            problems[check] = f"the registry {registry} has no measured rule {check}"
            continue
        try:
            ledger, problem = _read_fixture_ledger(path, check)
        except OSError as error:
            ledger, problem = {}, f"the ledger of {check} cannot be read: {error}"
        if problem:
            problems[check] = problem
            continue
        rows[check] = BudgetRow(check, row.warn, row.error, ledger, _shown(path))
    return CostBudget(rows, problems, test_rows)


def cost_budget_at(registry: Path, ledgers: Path) -> CostBudget:
    """Read the measured rules of a fixture compile from a registry and from the ledgers of one directory, with no memo.

    The witness of each rule of the driver reads the registry and the
    ledgers of the copy of a plant with it (assay.guard.engines.buildsys.held).
    """
    return _parse_cost_budget(registry, {check: ledgers / f"{check}.txt" for check in _LEDGERS}, registry)


def _file_stamps(paths: list[Path]) -> list[list[int]] | None:
    """Return the stat fields of each file of `paths`, an empty list for a file that does not exist, or None when a
    file cannot be read."""
    stamps: list[list[int]] = []
    for path in paths:
        try:
            stamps.append(_stat_fields(os.stat(path)))
        except FileNotFoundError:
            stamps.append([])
        except OSError:
            return None
    return stamps


def read_cost_budget(memo_dir: Path | None) -> CostBudget:
    """Read the measured rules fixture-cpu and fixture-instructions of the registry, their ledgers, and the
    thresholds of the rules of the test launcher.

    CRUCIBLE_NEG_REGISTRY, CRUCIBLE_NEG_CPU_LEDGER and
    CRUCIBLE_NEG_INSTRUCTIONS_LEDGER name a different registry and ledgers.
    The test launcher reads its rules from the registry of
    CRUCIBLE_TEST_REGISTRY, and the driver reads them there too.  A memo in
    `memo_dir` keeps a budget with no problem while the four files have the
    same stat fields, and a ledger that does not exist has the empty stamp
    (the module text, The cost of a compile).  The function writes the memo
    only when no file changed in the settle period and during the read.
    """
    from assay.loop import teststep as test_launcher

    registry = Path(os.environ.get("CRUCIBLE_NEG_REGISTRY") or _REGISTRY)
    ledger_paths = {check: Path(os.environ.get(variable) or default) for check, (variable, default) in _LEDGERS.items()}
    test_registry = Path(os.environ.get(test_launcher.REGISTRY_ENV) or test_launcher.REGISTRY_FILE)
    files = [registry, *ledger_paths.values(), test_registry]
    memo = None
    if memo_dir is not None:
        material = json.dumps([str(path) for path in files])
        memo = memo_dir / f"budget-{_short_digest(material.encode('utf-8', 'surrogateescape'))}.json"
        try:
            recorded = json.loads(memo.read_text())
            if (recorded["format"] == _BUDGET_FORMAT and set(recorded["rows"]) == set(_LEDGERS)
                    and recorded["stamps"] == _file_stamps(files)):
                return CostBudget({check: BudgetRow(check, float(warn), float(error),
                                                    {name: (float(kept), str(reason))
                                                     for name, (kept, reason) in ledger.items()}, str(shown))
                                   for check, (warn, error, ledger, shown) in recorded["rows"].items()}, {},
                                  {rule: (float(warn), float(error))
                                   for rule, (warn, error) in recorded["test_rows"].items()})
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            pass
    started_ns = _now_ns()
    before = _file_stamps(files)
    budget = _parse_cost_budget(registry, ledger_paths, test_registry)
    if memo is None or budget.problems or before is None or _file_stamps(files) != before:
        return budget
    if all(not stamp or stamp[4] < started_ns - _SETTLE_NS for stamp in before):
        rows = {check: [row.warn, row.error, row.ledger, row.ledger_shown] for check, row in budget.rows.items()}
        test_rows = {rule: [warn, error] for rule, (warn, error) in budget.test_rows.items()}
        try:
            _write_atomic(memo, json.dumps({"format": _BUDGET_FORMAT, "stamps": before, "rows": rows,
                                            "test_rows": test_rows}).encode("utf-8", "surrogateescape"))
        except OSError:
            pass
    return budget


def _row_verdict(row: BudgetRow, fixture_name: str, value: float, holds_error: bool) -> str:
    """Return the verdict of one measure against one measured rule and its ledger (assay.core.budget, THE VERDICT).

    An error of a rule that does not hold the error level is _DEMOTED, a
    warning: the CPU time of a compile with an instruction count.  A ledger
    row keeps the measure up to its kept value with the noise of the unit of
    the rule (assay.core.budget, THE NOISE).
    """
    kept = row.ledger.get(fixture_name)
    found = cost_budget.verdict(value, row.warn, row.error, None if kept is None else kept[0],
                                cost_budget.noise_of(_UNITS[row.check]))
    is_error = found == cost_budget.ERROR or (
        found == cost_budget.RISEN and cost_budget.risen_level(value, row.error) == cost_budget.ERROR)
    return _DEMOTED if is_error and not holds_error else found


def _judge_row(report: ModuleType, row: BudgetRow, verdict: str, fixture_name: str, place: str, text: str,
               unit: str, value: float) -> list[object]:
    """Return the finding of one measure of a fixture compile, for the verdict of `_row_verdict`."""
    if verdict == cost_budget.UNNEEDED:
        return [report.judged("error", place, 0, row.check,
                              f"{text}, and with the noise {cost_budget.noise_of(unit):.0%} of the unit it stays at "
                              f"or under the warning threshold {row.warn:g} {unit}.  Remove its row from "
                              f"{row.ledger_shown}.")]
    if verdict in (cost_budget.ADMITTED, cost_budget.RISEN):
        kept, reason = row.ledger[fixture_name]
        noise = cost_budget.noise_of(unit)
        if verdict == cost_budget.ADMITTED:
            return [report.judged("warning", place, 0, row.check,
                                  f"{text}, more than the warning threshold {row.warn:g} {unit}.  The row of "
                                  f"{row.ledger_shown} keeps it up to {kept:g} {unit} with the noise {noise:.0%}: "
                                  f"{reason}")]
        return [report.judged(cost_budget.risen_level(value, row.error), place, 0, row.check,
                              f"{text}, more than the row of {row.ledger_shown} keeps: {kept:g} {unit} with the "
                              f"noise {noise:.0%}.  {_ADVICE}  When the growth is wanted, write the measure into "
                              f"the row: {reason}")]
    if verdict == _DEMOTED:
        return [report.Finding("warning", place, 0, row.check,
                               f"{text}, more than the error threshold {row.error:g} {unit}.  The compile has an "
                               f"instruction count, so the rule {_INSTRUCTIONS_CHECK} holds the error level, and the "
                               f"CPU time, which rises with the load of the host, gives a warning only.")]
    if verdict == cost_budget.ERROR:
        return [report.judged("error", place, 0, row.check,
                              f"{text}, more than the error threshold {row.error:g} {unit}.  {_ADVICE}")]
    if verdict == cost_budget.WARNING:
        return [report.judged("warning", place, 0, row.check,
                              f"{text}, more than the warning threshold {row.warn:g} {unit}.")]
    return []


def _warnings_file(warnings_dir: Path, check: str, fixture_name: str) -> Path:
    """Return the warnings file of one check of this fixture, by the name that check_report.write_warnings gives."""
    return warnings_dir / f"{check}.{fixture_name}.txt"


def report_cost(fixture_name: str, source: Path, result: CompileResult, is_stored: bool, budget: CostBudget,
                warnings_dir: Path | None) -> int:
    """Report the CPU time and the instruction count of the compile against the budget, and return 1 when it fails.

    The warning lines of each check go to the file of this fixture in the
    warnings directory, so the many fixtures that run at the same time do not
    write one file.  A run with no finding, whose fixture has no warnings file,
    has nothing to write, and it does not import the report module.
    """
    findings = cost_findings(fixture_name, source, result, is_stored, budget)
    has_file = warnings_dir is not None and any(_warnings_file(warnings_dir, check, fixture_name).exists()
                                                for check in _LEDGERS)
    if not any(findings.values()) and not has_file:
        return 0
    report = _report_module()
    status = 0
    for check, found in findings.items():
        status |= report.emit(found, check, warnings_dir, fixture_name)
    return status


def cost_findings(fixture_name: str, source: Path, result: CompileResult, is_stored: bool,
                  budget: CostBudget) -> dict[str, list[object]]:
    """Return the findings of the CPU time and of the instruction count of the compile against the budget, by check.

    A compile with no finding, with a budget with no problem, gives two
    empty lists, and the call does not import the report module.
    report_cost() reports the findings.  The witness of each rule of the
    driver judges a planted record of a fixture with this function
    (assay.guard.engines.buildsys.held).
    """
    has_count = result.instructions is not None
    verdicts: dict[str, str] = {}
    if _CPU_CHECK in budget.rows:
        verdicts[_CPU_CHECK] = _row_verdict(budget.rows[_CPU_CHECK], fixture_name, result.cpu_s, not has_count)
    if _INSTRUCTIONS_CHECK in budget.rows and result.instructions is not None:
        verdicts[_INSTRUCTIONS_CHECK] = _row_verdict(budget.rows[_INSTRUCTIONS_CHECK], fixture_name,
                                                     result.instructions / _GIGA, True)
    findings: dict[str, list[object]] = {_CPU_CHECK: [], _INSTRUCTIONS_CHECK: []}
    if not budget.problems and not any(verdicts.values()):
        return findings
    report = _report_module()
    try:
        place = str(source.relative_to(_REPO_ROOT))
    except ValueError:
        place = str(source)
    measured = "the stored compile" if is_stored else "the compile"
    for check, problem in budget.problems.items():
        findings[check].append(report.Finding("error", place, 0, check, f"{problem}."))
    if _CPU_CHECK in verdicts:
        text = (f"{measured} of the fixture {fixture_name} took {result.cpu_s:.2f} s of CPU time ({result.user_s:.2f} s "
                f"of user time and {result.system_s:.2f} s of system time)")
        findings[_CPU_CHECK] += _judge_row(report, budget.rows[_CPU_CHECK], verdicts[_CPU_CHECK], fixture_name,
                                           place, text, _UNITS[_CPU_CHECK], result.cpu_s)
    if _INSTRUCTIONS_CHECK in verdicts and result.instructions is not None:
        text = f"{measured} of the fixture {fixture_name} ran {result.instructions / _GIGA:.2f} G user instructions"
        findings[_INSTRUCTIONS_CHECK] += _judge_row(report, budget.rows[_INSTRUCTIONS_CHECK],
                                                    verdicts[_INSTRUCTIONS_CHECK], fixture_name, place, text,
                                                    _UNITS[_INSTRUCTIONS_CHECK], result.instructions / _GIGA)
    return findings


def _record_compile(store: ResultStore, key: str, result: CompileResult, missing: list[str], argv: list[str],
                    directory: Path, source: Path, env: Mapping[str, str], started_ns: int,
                    roots: Roots) -> tuple[bool, str]:
    """Find the search list of a compile, and store its result with the marks of `roots`.

    `result.inputs` is the list of the dependency file of GCC, or after a
    fatal error the list of the `-M -MG` pass, and `missing` holds each header
    that the pass did not find.  The search query gives the directories of the
    search list, and it uses the scratch directory of the dependency file.
    """
    if result.inputs is None:
        return False, "the driver does not know the files that the compile read"
    named, reason = search_directories(argv, directory, source, Path(argv[-1]).parent, env)
    if named is None:
        return False, reason
    return store.record(key, result, list(result.inputs), missing, named, started_ns, roots)


_COMMAND_SUFFIX = ".command"


def write_run_record(build_dir: Path, fixture_name: str, result: CompileResult, is_stored: bool,
                     census: tuple[str, tuple[cost_record.CensusFinding, ...]] | None = None) -> None:
    """Write the record of a run of the fixture: the cost of its compile, each file that it read, and its census.

    The record is BUILD_DIR/neg-compile/NAME/NAME.inputs (assay.core.record,
    THE RECORDS).  The rules parse-total, header-fanout and instruction-total
    read it, and the rule fixture-quarantine reads the census of
    fixture_census().  A failure to write changes no verdict.
    """
    stamp, findings = census if census is not None else (None, None)
    record = cost_record.FixtureRecord(fixture_name, "stored" if is_stored else "compiled", result.user_s,
                                       result.system_s, result.instructions, result.inputs, stamp, findings)
    try:
        cost_record.write(cost_record.fixture_record_file(build_dir, fixture_name), record)
    except OSError:
        pass


def read_log(path: Path) -> tuple[sarif.Log | None, str]:
    """Return the records of the SARIF log at `path`, or None and the reason when the reader cannot read it."""
    try:
        return sarif.read(path), ""
    except OSError as error:
        return None, f"the compile wrote no SARIF log that the driver can read: {error}"
    except sarif.SarifError as error:
        return None, f"the reader refuses the SARIF log of the compile: {error}"


def obtain_result(fixture_name: str, argv: list[str], directory: Path, source: Path, output: Path,
                  budget: CostBudget, store: ResultStore | None, store_reason: str,
                  roots: Roots, sarif_log: Path) -> tuple[CompileResult, bool]:
    """Return the result of the compile, from the store or from a compile, and True when it comes from the store.

    `argv` ends with `-MF` and the dependency file of this process, and it
    names `sarif_log` for the SARIF log.  After a compile, the dependency file
    and the log get the name of the fixture.  The scratch directory of the
    fixture holds them and the digest memo of the fixture.  The compile
    runs in the compile environment, with or without the store, and
    `store_reason` tells why `store` is None.  The key and the stored result
    hold the marks of `roots` in place of the source root and the build root.
    A stored result whose compile is over the error threshold of the rule that
    holds the error level is a miss, so the driver measures again.  A stored
    result of a compile with no error is a miss too.  The store keeps no such
    compile, because its census reads the object (The quarantine plugin).
    """
    depfile = Path(argv[-1])
    env = compile_environment(os.environ)
    protected = {os.path.realpath(path) for path in (source, output, depfile)}
    reason = store_reason
    key: str | None = None
    if store is not None:
        key, reason = compile_key(argv, directory, protected, store.memo, env, roots)
    if store is not None and key is not None:
        digests = DigestMemo(depfile.parent / f"{fixture_name}{_DIGESTS_SUFFIX}", roots)
        stored, reason = store.lookup(key, digests, roots)
        digests.save()
        if stored is not None and budget.is_over_error(fixture_name, stored):
            stored, reason = None, f"the stored compile {budget.excess_text(stored)} than the error threshold"
        if stored is not None and stored.returncode == 0:
            stored, reason = None, _SUCCEEDED
        if stored is not None:
            _note(fixture_name, f"the result comes from the store (entry {key})")
            return stored, True
    _note(fixture_name, f"compiled ({reason})")
    depfile.unlink(missing_ok=True)
    sarif_log.unlink(missing_ok=True)
    started_ns = _now_ns()
    result = run_compile(argv, directory, env)
    log, log_problem = read_log(sarif_log)
    result = result.with_log(log, log_problem)
    try:
        os.replace(sarif_log, sarif_log.with_name(f"{fixture_name}.sarif"))
    except OSError:
        pass
    dependencies: list[str] | None = None
    missing: list[str] = []
    dependency_reason = "the dependency file of GCC does not hold exactly one rule"
    try:
        text = depfile.read_text(errors="surrogateescape")
        os.replace(depfile, depfile.with_name(f"{fixture_name}.d"))
    except OSError:
        text = None
    if text is not None:
        parsed = parse_dependency_file(text)
        if parsed is not None:
            dependencies = list(dict.fromkeys(os.path.join(directory, name) for name in parsed))
    else:
        # GCC writes no dependency file after a fatal error.  The `-M -MG`
        # pass reads each file that the compile read before it stopped.
        dependencies, missing, dependency_reason = dependency_pass(argv, directory, depfile.parent, env)
    result = result.with_inputs(None if dependencies is None else tuple(dependencies))
    if store is not None and key is not None:
        if dependencies is None:
            is_stored, why = False, dependency_reason
        elif result.log is None:
            is_stored, why = False, result.log_problem
        elif result.returncode == 0:
            is_stored, why = False, _SUCCEEDED
        elif result.returncode != 1:
            # A signal or an internal error of the compiler can give a different
            # output on the next run.
            is_stored, why = False, f"the exit code is {result.returncode}"
        elif budget.is_over_error(fixture_name, result):
            is_stored, why = False, f"the compile {budget.excess_text(result)} than the error threshold"
        elif compile_key(argv, directory, protected, store.memo, env, roots)[0] != key:
            # The compiler, a shared object, a path that the loader tried or a
            # file of the command changed between the key and the compile.
            is_stored, why = False, "the compiler identity changed during the compile"
        else:
            is_stored, why = _record_compile(store, key, result, missing, argv, directory, source, env, started_ns,
                                             roots)
        _note(fixture_name, f"stored (entry {key})" if is_stored else f"not stored ({why})")
    return result, False


# ── The reason of the failure ──────────────────────────────────────

_REASON_CHECK = "fixture-own-reason"
# The variable that names a different ledger, and the ledger of the tree.
_REASON_LEDGER = ("CRUCIBLE_NEG_REASON_LEDGER", _LEDGER_DIRECTORY / f"{_REASON_CHECK}.txt")


def _shown_line(line: str) -> str:
    """Return the diagnostic line `line` with each path of the tree relative to the root of the tree."""
    return line.replace(f"{_REPO_ROOT}{os.sep}", "")


def report_own_reason(fixture_name: str, source: Path, unmatched: list[str], is_stored: bool,
                      warnings_dir: Path | None) -> int:
    """Report each error of the compile that no regex reaches, against the ledger, and return 1 when the test fails.

    `unmatched` comes from `evaluate`.  An error with no regex fails the test,
    unless the ledger names the fixture: a listed fixture gives one warning.
    A listed fixture with no such error fails, so that its row goes (the
    module text, The reason of the failure).  A run with no finding, whose
    fixture has no row and no warnings file, reads no report module.  The
    driver reads the ledger on each run, when it exists.
    """
    variable, default = _REASON_LEDGER
    path = Path(os.environ.get(variable) or default)
    findings = own_reason_findings(fixture_name, source, unmatched, is_stored, path)
    has_file = warnings_dir is not None and _warnings_file(warnings_dir, _REASON_CHECK, fixture_name).exists()
    if not findings and not has_file:
        return 0
    return _report_module().emit(findings, _REASON_CHECK, warnings_dir, fixture_name)


def own_reason_findings(fixture_name: str, source: Path, unmatched: list[str], is_stored: bool,
                        path: Path) -> list[object]:
    """Return the findings of each error of the compile that no regex matches, against the ledger at `path`.

    A run with no such error gives no finding when the ledger has no row of
    the fixture.  Then the call does not import the report module.
    report_own_reason() reports the findings.  The witness of the rule judges
    the planted log of a fixture with this function
    (assay.guard.engines.buildsys.held).
    """
    try:
        ledger, problem = _read_fixture_ledger(path, _REASON_CHECK)
    except OSError as error:
        ledger, problem = {}, f"the ledger {path} cannot be read: {error}"
    is_listed = fixture_name in ledger
    if not problem and not unmatched and not is_listed:
        return []
    report = _report_module()
    try:
        place = str(source.relative_to(_REPO_ROOT))
    except ValueError:
        place = str(source)
    measured = "the stored compile" if is_stored else "the compile"
    findings: list[object] = []
    if problem:
        findings.append(report.Finding("error", place, 0, _REASON_CHECK, f"{problem}."))
    elif unmatched and is_listed:
        findings.append(report.Finding(
            "warning", place, 0, _REASON_CHECK,
            f"{measured} of the fixture {fixture_name} gave {len(unmatched)} error(s) that no regex matches, and the "
            f"fixture has a row in the ledger: {ledger[fixture_name][1]}.  The first error: "
            f"{_shown_line(unmatched[0])}"))
    elif unmatched:
        findings.extend(report.Finding(
            "error", place, 0, _REASON_CHECK,
            f"{measured} of the fixture {fixture_name} gave an error that no regex matches: {_shown_line(line)}.  A "
            f"fixture must fail only for its own reason: give the error a regex, or remove its cause.")
            for line in unmatched)
    elif is_listed:
        findings.append(report.Finding(
            "error", place, 0, _REASON_CHECK,
            f"{measured} of the fixture {fixture_name} gave no error that no regex matches.  Remove its row from "
            f"{_shown(path)}."))
    return findings


# ── The test result ────────────────────────────────────────────────


def evaluate(fixture_name: str, source: Path, expected_regexes: list[str],
             result: CompileResult) -> tuple[int, list[str] | None]:
    """Print the output of the compile, apply the regexes, and return the exit code of the regexes.

    The second value is the list of the errors that no regex reaches
    (`unmatched_errors`), for `report_own_reason`.  It is None when the
    compile succeeded or a regex did not match, because the test then fails
    for that cause.  The function gives the same values and the same messages
    for a stored result and for a fresh compile.
    """
    # The reader of the test sees the text output of the compiler, with each
    # source line and caret line.
    sys.stdout.write(result.output)

    if result.returncode == 0:
        print(
            f"negative fixture {fixture_name} compiled successfully",
            file=sys.stderr,
        )
        return 1, None
    if result.log is None:
        _report_module().write_line(f"negative fixture {fixture_name}: {result.log_problem}", is_error=True)
        return 1, None
    # The regexes read only the text of the compiler, and not the text of the
    # fixture (the module text, How the regexes apply).  No result of the
    # quarantine plugin gives a line (the module text, The quarantine plugin).
    rendering = sarif.render(result.log, sarif.is_plugin_result)
    matchable = "\n".join(rendering.lines)

    # Independence report.  A fixture documents ONE rejection; if its
    # own source produces error headers at several distinct lines it is
    # rejecting for several reasons at once, and the registered regex
    # may be witnessing a different one than the file claims.  Reported
    # unconditionally so the count shows up in CI logs; promoted to a
    # failure under CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 so the discipline
    # can be swept without editing 2,000+ registrations.
    total_errors, own_lines = summarise_errors(rendering, source)
    strict = os.environ.get("CRUCIBLE_NEG_STRICT_SINGLE_ERROR") == "1"
    not_independent = len(own_lines) > 1
    if not_independent:
        detail = ", ".join(str(n) for n in own_lines)
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local errors at {len(own_lines)} distinct lines "
            f"({detail}) — the fixture rejects for more than one reason",
            file=sys.stderr,
        )
    else:
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local error lines {own_lines}",
            file=sys.stderr,
        )

    # EVERY required regex must appear.  For a single-regex fixture this
    # is identical to the original `re.search` gate.  For a multi-cell
    # fixture, a missing regex means one cell stopped failing (discipline
    # slipped on that cell) OR its diagnostic text drifted — either way
    # the merged TU no longer proves what it claims, and we fail loudly
    # naming the specific cell that went silent.
    missing = [
        pattern
        for pattern in expected_regexes
        if not re.search(pattern, matchable, flags=re.MULTILINE)
    ]
    if missing:
        compiler_log = sarif.Log(result.log.tool, tuple(
            record for record in result.log.diagnostics if not sarif.is_plugin_result(record)))
        quoted = "\n".join(record.snippet for record in sarif.records(compiler_log) if record.snippet)
        for pattern in missing:
            print(
                f"expected diagnostic not found for {fixture_name}: "
                f"{pattern}",
                file=sys.stderr,
            )
            # The most frequent cause is a regex that only a source line of
            # the fixture matches, which GCC quotes below a diagnostic.  The
            # note names that cause.
            if re.search(pattern, quoted, flags=re.MULTILINE):
                print(
                    f"  note: {fixture_name} matched this pattern ONLY in "
                    f"the quoted source line, not in any diagnostic the "
                    f"compiler produced — the fixture is asserting its own "
                    f"text, not a rejection",
                    file=sys.stderr,
                )
        return 1, None
    # The regex verdict is the more informative one, so it is reported
    # first; the strict-mode verdict is applied only after it.
    return (1 if (strict and not_independent) else 0), unmatched_errors(rendering, expected_regexes)


def main(argv: list[str]) -> int:
    """Run one fixture (`loop fixture`), and return the exit code of its test.

    Args:
        argv: [--warnings-dir DIR] BUILD_DIR SOURCE FIXTURE_NAME EXPECTED_REGEX...

    Returns:
        The exit code of the test of the fixture, or 2 on a usage error
    """
    # arguments[3..] are one or more required-diagnostic regexes.  The
    # harnesses in the CMake files pass two or more: the first shows that
    # a rejection occurred, and the next ones show which gate rejected.
    # EVERY listed regex must appear in the compile output.  AND semantics
    # are mandatory: a fixture whose generic regex matched but whose
    # specific regex went silent would otherwise pass.  See the two-regex
    # floor above `crucible_neg_compile_test` in test/CMakeLists.txt.
    # `--warnings-dir DIR` before the other arguments names the warnings
    # directory of utils/assay/core/report.py.
    parsed = fixture_arguments(argv)
    if parsed is None:
        print(
            "usage: loop fixture [--warnings-dir DIR] <build-dir> <source> "
            "<fixture-name> <expected-regex> [<expected-regex> ...]",
            file=sys.stderr,
        )
        return 2
    warnings_dir, arguments = parsed

    start = _cost_meter_module().start_self()
    build_dir = Path(arguments[0]).resolve()
    source = Path(arguments[1]).resolve()
    fixture_name = arguments[2]
    expected_regexes = arguments[3:]
    if not _FIXTURE_NAME.fullmatch(fixture_name):
        print(f"the fixture name {fixture_name!r} is not a word of letters, digits, '_', '.' and '-' that starts with "
              f"a letter, a digit or '_'", file=sys.stderr)
        return 2
    status, budget, is_stored = run_fixture(build_dir, source, fixture_name, expected_regexes, warnings_dir)
    return judge_test(fixture_name, build_dir, source, status, start, budget, warnings_dir, is_stored)


def fixture_arguments(argv: list[str]) -> tuple[Path | None, list[str]] | None:
    """Return the warnings directory and the other words of the command line of the driver, or None on a usage error.

    The command line is `[--warnings-dir DIR] BUILD_DIR SOURCE FIXTURE_NAME
    EXPECTED_REGEX...`, with one regex or more.  The other words start with
    BUILD_DIR.  The witness of the rule fixture-own-reason reads the regexes
    of a planted fixture with this function (assay.guard.engines.buildsys.held).
    """
    arguments = argv
    warnings_dir: Path | None = None
    if len(arguments) >= 2 and arguments[0] == "--warnings-dir":
        warnings_dir = Path(arguments[1])
        arguments = arguments[2:]
    return (warnings_dir, arguments) if len(arguments) >= 4 else None


def run_fixture(build_dir: Path, source: Path, fixture_name: str, expected_regexes: list[str],
                warnings_dir: Path | None) -> tuple[int, CostBudget | None, bool]:
    """Compile the fixture or take its stored result, and return the status of its test, the budget and the source.

    The source is True when the result comes from the store.  The budget is None, and the source is False, when the
    driver stops before it reads the budget.
    """
    scratch = build_dir / cost_record.FIXTURE_DIRECTORY / fixture_name
    scratch.mkdir(parents=True, exist_ok=True)
    found = find_compile_command(build_dir, source, scratch / f"{fixture_name}{_COMMAND_SUFFIX}")
    if found is None:
        _report_module().write_line(f"no compile command for {source}", is_error=True)
        return 2, None, False
    command, directory, roots = found
    output = scratch / f"{fixture_name}.o"
    depfile = scratch / f"{fixture_name}.{os.getpid()}.d"
    sarif_log = scratch / f"{fixture_name}.{os.getpid()}.sarif"
    argv = compile_argv(command, output, depfile, sarif_log)
    store, store_reason = open_store()
    budget = read_cost_budget(store.memo if store is not None else None)
    result, is_stored = obtain_result(fixture_name, argv, directory, source, output, budget, store, store_reason,
                                      roots, sarif_log)
    write_run_record(build_dir, fixture_name, result, is_stored,
                     fixture_census(result, argv, directory, source, output))
    verdict, unmatched = evaluate(fixture_name, source, expected_regexes, result)
    sys.stdout.flush()
    own_reason = 0
    if unmatched is not None:
        own_reason = report_own_reason(fixture_name, source, unmatched, is_stored, warnings_dir)
    over_budget = report_cost(fixture_name, source, result, is_stored, budget, warnings_dir)
    return verdict or own_reason or over_budget, budget, is_stored


def judge_test(fixture_name: str, build_dir: Path, source: Path, status: int, start: tuple[float, float],
               budget: CostBudget | None, warnings_dir: Path | None, is_stored: bool = False) -> int:
    """Judge the run of the fixture as a test, and write its record, as the test launcher does (The test record).

    In a run of the test launcher, the launcher judges and records the test, and the function gives the status.

    Args:
        fixture_name: The name of the fixture, which is the name of its test
        build_dir: The build directory
        source: The source of the fixture, which a finding names
        status: The status of the test before the judge
        start: The start of the measure at the start of main() (assay.core.meter, start_self())
        budget: The cost budget, which holds the thresholds of the rules of the launcher, or None
        warnings_dir: The warnings directory, or None
        is_stored: True when the result of the fixture comes from the store.  The run then measures no compile, and
            the judge refuses no row (The test record)

    Returns:
        The status of the test, or 1 when the judge rejects a test with the status 0
    """
    from assay.loop import teststep as test_launcher

    if os.environ.get(test_launcher.LAUNCHED_ENV):
        return status
    run = _cost_meter_module().measure_self(status, start)
    sys.stdout.flush()
    return test_launcher.judge_and_record(fixture_name, sys.executable, _shown(source), str(build_dir), run,
                                          None if warnings_dir is None else str(warnings_dir),
                                          None if budget is None else budget.test_rows, is_measured=not is_stored)
