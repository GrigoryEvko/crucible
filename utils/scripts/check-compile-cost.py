#!/usr/bin/env python3
"""check-compile-cost — the cost of each compile, each link and each object of one build, against the budget table.

THE CHECKS
    compile-cpu     The user and system CPU time of one compile job.
    compile-instructions
                    The user instructions of the compiler run of one compile
                    job, in G (10^9).
    compile-memory  The peak resident memory of one compile job, in GB of 2^30 bytes.
    link-time       The user and system CPU time of one link.
    link-memory     The peak resident memory of one link, in GB.
    function-size   The size of the largest function in one object, in KB.
    object-text     The size of the machine code of one object, in KB: the
                    sum of the sections that are allocated and executable.
    header-alone    The bytes that one header of include/ includes when it
                    compiles alone, in MB of 2^20 bytes.

    utils/scripts/budgets.txt gives the warning threshold and the error
    threshold of each check (utils/scripts/check_report.py).  header-alone
    reads the row of the layer of the header: header-alone-foundation,
    header-alone-fixy or header-alone-crucible.  Each translation unit of the
    tree reads the base headers, so their rows are the tightest.  Each finding is
    one line in the format of check_report.py.  The path of a finding is the
    source of the object, the linked output or the header, relative to the
    repository root when it is in the tree.

WHAT THE CHECKS READ
    The compile database of the build directory names each compile job and
    its object.  The codemodel reply of the CMake file API names each linked
    output (an executable, a shared library or a module).  A check reads only
    the outputs that exist, so a target that the build does not make (a
    negative fixture) has no output to read.

    The five time, instruction and memory checks read the record OUTPUT.cost
    that utils/scripts/build-launcher.py writes beside each output (the
    format is in utils/scripts/cost_meter.py).  A record that says "hit"
    comes from a ccache hit, and it holds no time: the check counts it, and
    does not judge it.  An output with no record, a record in another format,
    or a record whose output size is not the size of the output is an error,
    because that output did not come through the launcher.  When no output
    has a record, the launcher is not wired, and the check gives one error.
    The launcher itself stops a step over the memory error threshold and a
    step that runs past three times the time error threshold.  These checks
    see the time error threshold after the build, and they see a record that
    a launcher with another budget table wrote.

THE INSTRUCTION COUNT HOLDS THE ERROR LEVEL OF A COMPILE
    The CPU time of a compile rises with the load of the host: at a load of
    770 on the build host, two compiles took 20.8 s and 22.6 s, over the
    error threshold of compile-cpu, with no change in the tree.  The number
    of user instructions does not change with the load (cost_meter.py, THE
    INSTRUCTION COUNT).  So when the record of a compile holds an
    instruction count, compile-instructions judges the count with the error
    level, and compile-cpu gives a warning, not an error, for a CPU time
    over its error threshold.  The count is that of the compiler run, also
    through a ccache miss (cost_meter.py, A COMPILE THROUGH CCACHE), so it
    does not change with the path of the compile.  A record with no count (a
    host with no exact counter) keeps the error of compile-cpu.  When no
    record of the build holds a count, compile-instructions does not apply to
    the build.

    function-size and object-text read the section headers and the symbol
    table of each object (ELF64, little endian).  The largest function is the
    largest STT_FUNC symbol.  A ccache hit gives the same machine code as a
    compile, so these two checks read each object.

    header-alone reads the sentinels of test/layer/CMakeLists.txt, which
    compile each header of include/ alone.  For each sentinel, it reads the
    dependency list from the ninja log (ninja -t deps) and adds the sizes of
    the files after the source.  The header is the first file of the list
    under include/, after symbolic links resolve.  A check file of
    test/layer/checks can include more than its header, and the measure then
    holds those files too.  The finding also gives the CPU time of the
    sentinel from its record.  That time includes the checks of a check file,
    which an includer of the header does not compile, so it has no
    threshold.

THE BUILD KIND
    The sizes and the times depend on the preset: ASan code is larger than
    Release code, and the Release presets compile for -march=native.  The
    kind of the build is the one line of BUILD_DIR/build-kind.txt, which
    cmake/BuildLauncher.cmake writes, for example x86_64-debug-asan.  A ledger
    row applies only to the build of its kind.

THE LEDGERS
    utils/scripts/CHECK-ledger.txt lists the items that can exceed the error
    threshold of the check, one row each, with the reason:

        kind | item | value | reason

    The item is the output path relative to the build directory, or the path
    of the header.  The value is the measure at the last --write, in the unit
    of the budget row.  In a build of the kind of a row:
      * An item over the error threshold with a row gives a warning, so the
        debt stays visible.  With no row, it gives an error.
      * An item with a row that is at or under the error threshold is an
        error: remove the row.
      * A row whose item the build does not hold gives a warning, because an
        optional target is not in each build of a kind.
    A time or memory check does not judge an item whose record is a ccache
    hit.  On a GitHub runner, each error of compile-cpu and link-time that
    judges a time is a warning that says that it was demoted
    (utils/scripts/cost_meter.py, A CI RUNNER).  An error of a memory check, of
    a size check or of an input stays an error.  The rows do not use the
    exact-count rule of the compile-time checks, because one value changes
    between two hosts of one kind (-march=native) and with each edit to an
    included header.  The build launcher reads the
    rows of compile-memory and link-memory to admit a step over the memory
    error threshold.
    --write writes the rows of the kind of the build again from this build,
    and keeps the rows of the other kinds and the reason of each item that
    keeps its row.  A new row takes the reason of --reason.

THE SELF-TEST
    --self-test plants records, objects, linked outputs, a file API reply, a
    dependency log and ledger rows in a scratch build directory, and examines
    each verdict of each check.  It writes each record with the writer of
    utils/scripts/build-launcher.py.  test/build_launcher_test.py tests the
    launcher itself.

Usage
    check-compile-cost.py --check CHECK --build-dir DIR [--ninja NINJA] [--warnings-dir DIR]
    check-compile-cost.py --check CHECK --build-dir DIR [--ninja NINJA] --write [--reason TEXT]
    check-compile-cost.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage
error or a failed self-test, 3 when header-alone does not apply to the build
(the ninja generator did not make it, so it has no dependency log, or it holds
no sentinel object, because the build made only some targets), or when
compile-instructions does not apply (no compile record holds a count).
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
import mmap
import os
import re
import struct
import subprocess
import sys
import tempfile
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

SCRIPTS = Path(__file__).resolve().parent
LAUNCHER = SCRIPTS / "build-launcher.py"
CHECKS = ("compile-cpu", "compile-instructions", "compile-memory", "link-time", "link-memory", "function-size",
          "object-text", "header-alone")
# The step and the measure of each time, instruction or memory check.
TIMED = {"compile-cpu": ("compile", "time"), "compile-instructions": ("compile", "instructions"),
         "compile-memory": ("compile", "memory"), "link-time": ("link", "time"), "link-memory": ("link", "memory")}
# The time check whose error level an instruction count in the record holds, and the row of the count.
COUNT_HOLDS_ERROR = {"compile-cpu": "compile-instructions"}
GIGA = 1e9
# The budget row of header-alone for the headers of each layer of include/.  The headers of a base
# layer reach each translation unit of the tree, so their rows are tighter.
LAYER_ROWS = {"foundation": "header-alone-foundation", "fixy": "header-alone-fixy",
              "crucible": "header-alone-crucible"}
RESULTS = frozenset({"built", "failed", "rejected", "hit"})
LINKED_TYPES = frozenset({"EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"})
SENTINEL_MARK = "/layer_sentinel_"
CHECK_FILES = Path("test/layer/checks")
KIND = re.compile(r"[a-z0-9_]+(?:-[a-z0-9_]+)*")
# The first line of one entry of `ninja -t deps`; the files follow, indented.
DEPS_HEADER = re.compile(r"(?P<target>.+?): (?:#deps \d+, deps mtime \d+ \((?P<state>[A-Z]+)\)|deps not found)")
KB = 1024
MB = 1024 * 1024
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_SYMTAB = 2
STT_FUNC = 2
NOT_APPLICABLE = 3


class NotApplicable(Exception):
    """The check does not apply to the build, by the design of the build."""


@dataclass(frozen=True, slots=True)
class Output:
    """One output of the build that exists: an object of a compile job or a linked output."""

    path: Path
    source: Path | None


@dataclass(frozen=True, slots=True)
class Measure:
    """One measured value of one item, with the words that tell what it is.

    `error_row` names the row whose exact count holds the error level of the
    item, or is empty when this check holds it.  `row` names the budget row
    of the item, or is empty for the row of the check.
    """

    item: str
    path: str
    value: float
    message: str
    error_row: str = ""
    row: str = ""


@dataclass(frozen=True, slots=True)
class ObjectCode:
    """The machine code of one object."""

    text_bytes: int
    largest_function: int
    largest_name: str


@dataclass(slots=True)
class Ledger:
    """The rows of one ledger file: the line, the value and the reason of each (kind, item)."""

    path: Path
    shown: str
    header: list[str]
    rows: dict[tuple[str, str], tuple[int, float, str]]


@dataclass(slots=True)
class Context:
    """The inputs of one run of one check."""

    check: str
    build_dir: Path
    kind: str
    ninja: str
    root: Path
    budgets_path: Path
    ledger_dir: Path


def shown_path(path: Path, root: Path) -> str:
    """Return a path relative to the root when it is inside the root, else the absolute path."""
    return str(path.relative_to(root)) if path.is_relative_to(root) else str(path)


def error_at(check: str, path: str, message: str, line: int = 0) -> check_report.Finding:
    """Return an error finding of one check."""
    return check_report.Finding("error", path, line, check, message)


def read_objects(context: Context) -> tuple[list[Output], list[check_report.Finding]]:
    """Read the compile jobs of the build whose objects exist.

    Complexity: linear in the rows of the compile database.

    Args:
        context: The run

    Returns:
        The objects in database order, and an error for each input that cannot be read
    """
    database = context.build_dir / "compile_commands.json"
    shown = shown_path(database, context.root)
    try:
        rows = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, ValueError) as problem:
        return [], [error_at(context.check, shown, f"the compile database cannot be read ({problem}).  Configure "
                                                   f"the build directory with a preset first")]
    outputs: list[Output] = []
    findings: list[check_report.Finding] = []
    seen: set[Path] = set()
    for row in rows:
        output = row.get("output")
        if output is None:
            findings.append(error_at(context.check, shown, f"the row of {row.get('file')} has no output field.  "
                                                           f"CMake 3.20 or a subsequent version writes it"))
            continue
        object_path = Path(os.path.normpath(os.path.join(row["directory"], output)))
        if object_path in seen or not object_path.is_file():
            continue
        seen.add(object_path)
        outputs.append(Output(object_path, Path(os.path.normpath(os.path.join(row["directory"], row["file"])))))
    if not outputs and not findings:
        findings.append(error_at(context.check, shown, "no object of the compile database exists.  Build the "
                                                       "tree before the check"))
    return outputs, findings


def read_links(context: Context) -> tuple[list[Output], list[check_report.Finding]]:
    """Read the linked outputs of the build that exist, from the codemodel reply of the CMake file API.

    Complexity: linear in the size of the reply.

    Args:
        context: The run

    Returns:
        The linked outputs, and an error for each input that cannot be read
    """
    reply = context.build_dir / ".cmake" / "api" / "v1" / "reply"
    shown = shown_path(reply, context.root)
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        return [], [error_at(context.check, shown, "the build has no reply of the CMake file API.  The root "
                                                   "CMakeLists.txt asks for the codemodel.  Configure the build again")]
    try:
        index = json.loads(indexes[-1].read_text(encoding="utf-8"))
        codemodel_file = next(item["jsonFile"] for item in index.get("objects", [])
                              if item.get("kind") == "codemodel" and item.get("version", {}).get("major") == 2)
        codemodel = json.loads((reply / codemodel_file).read_text(encoding="utf-8"))
        targets = [json.loads((reply / target["jsonFile"]).read_text(encoding="utf-8"))
                   for configuration in codemodel.get("configurations", [])
                   for target in configuration.get("targets", [])]
    except (StopIteration, OSError, KeyError, ValueError) as problem:
        return [], [error_at(context.check, shown, f"the codemodel reply cannot be read ({problem!r})")]
    outputs: list[Output] = []
    seen: set[Path] = set()
    for target in targets:
        if target.get("type") not in LINKED_TYPES:
            continue
        for artifact in target.get("artifacts", []):
            path = Path(os.path.normpath(context.build_dir / artifact["path"]))
            if path not in seen and path.is_file():
                seen.add(path)
                outputs.append(Output(path, None))
    if not outputs:
        return [], [error_at(context.check, shown, "no linked output of the build exists.  Build the tree before "
                                                   "the check")]
    return outputs, []


def read_record(output: Output, step: str) -> tuple[dict[str, object] | None, str | None]:
    """Read and validate the record of one output.

    Args:
        output: The output
        step: "compile" or "link"

    Returns:
        The record, or None and the reason that the record cannot count ("missing" for no record)
    """
    record_path = Path(str(output.path) + cost_meter.RECORD_SUFFIX)
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None, "missing"
    except (OSError, ValueError) as problem:
        return None, f"the record {record_path.name} cannot be read ({problem})"
    if (not isinstance(record, dict) or record.get("format") != cost_meter.RECORD_FORMAT
            or record.get("result") not in RESULTS or record.get("step") != step):
        return None, f"the record {record_path.name} is not a {step} record of format {cost_meter.RECORD_FORMAT}"
    size = output.path.stat().st_size
    if record.get("output_bytes") != size:
        return None, (f"the record {record_path.name} gives {record.get('output_bytes')} bytes and the output has "
                      f"{size}, so the output did not come through the build launcher.  Build it again")
    if record["result"] != "hit" and not isinstance(record.get("cost"), dict):
        return None, f"the record {record_path.name} holds no cost block"
    return record, None


def instruction_count(cost: dict[str, object]) -> int | None:
    """Return the exact user instructions of the compiler run of a cost block, or None when the block holds no count."""
    count = cost.get(cost_meter.COMPILER_COUNT_KEY)
    return count if isinstance(count, int) and not isinstance(count, bool) and count >= 0 else None


def timed_measures(context: Context, outputs: list[Output]) -> tuple[list[Measure], set[str],
                                                                     list[check_report.Finding], str]:
    """Read the time, the instructions or the memory of each step from its record.

    Args:
        context: The run of a time, instruction or memory check
        outputs: The outputs of the step of the check

    Returns:
        The measures, the items of the ccache hits, the errors for the records that cannot count, and the
        summary line

    Raises:
        NotApplicable: If the check counts instructions and no record that holds a cost holds a count
    """
    step, quantity = TIMED[context.check]
    noun = "compile job" if step == "compile" else "link"
    count_row = COUNT_HOLDS_ERROR.get(context.check, "")
    measures: list[Measure] = []
    hits: set[str] = set()
    uncounted = 0
    problems: list[tuple[Output, str]] = []
    for output in outputs:
        item = shown_path(output.path, context.build_dir)
        record, problem = read_record(output, step)
        if record is None:
            problems.append((output, problem or "missing"))
            continue
        if record["result"] == "hit":
            hits.add(item)
            continue
        cost = record["cost"]
        count = instruction_count(cost)
        error_row = ""
        if quantity == "instructions":
            if count is None:
                uncounted += 1
                continue
            value = count / GIGA
            words = (f"the {noun} of {item} ran {value:.1f} G user instructions ({float(cost['cpu_s']):.1f} s CPU "
                     f"at a load of {float(cost.get('load', -1)):.0f})")
        elif quantity == "time":
            value = float(cost["cpu_s"])
            words = f"the {noun} of {item} took {value:.1f} s CPU ({float(cost['wall_s']):.1f} s wall)"
            error_row = count_row if count is not None else ""
        else:
            value = float(cost["peak_rss_kb"]) / cost_meter.KB_PER_GB
            words = f"the {noun} of {item} took {value:.2f} GB of peak memory"
        measures.append(Measure(item, shown_path(output.source or output.path, context.root), value, words,
                                error_row))
    if quantity == "instructions" and not measures and uncounted:
        raise NotApplicable(f"no record of the {uncounted} compile jobs that ran holds an instruction count, because "
                            f"the host gives no exact counter (utils/scripts/cost_meter.py, THE INSTRUCTION COUNT).  "
                            f"compile-cpu keeps the error level")
    findings: list[check_report.Finding] = []
    if outputs and len(problems) == len(outputs):
        findings.append(error_at(context.check, shown_path(context.build_dir, context.root),
                                 f"no {step} output of the build has a usable record, so the build launcher is not "
                                 f"wired.  cmake/BuildLauncher.cmake puts utils/scripts/build-launcher.py in front "
                                 f"of each compile and each link.  Configure the build again and build it.  The "
                                 f"first problem: {problems[0][1]}"))
    else:
        for output, problem in problems:
            text = "it has no record, so it did not come through the build launcher" if problem == "missing" \
                else problem
            findings.append(error_at(context.check, shown_path(output.source or output.path, context.root),
                                     f"the output {shown_path(output.path, context.build_dir)}: {text}"))
    summary = (f"{context.check}: {len(outputs)} outputs, {len(measures)} measured, {len(hits)} ccache hits with "
               f"no time, {len(problems)} without a usable record")
    if quantity == "instructions":
        summary += f", {uncounted} with no exact count"
    return measures, hits, findings, summary


def read_object_code(path: Path) -> ObjectCode:
    """Read the machine-code sizes of one ELF64 little-endian object.

    Complexity: linear in the sections and the symbols of the object.  The
    function reads only the section headers and the symbol table.

    Args:
        path: The object

    Returns:
        The sum of the executable sections and the largest function

    Raises:
        ValueError: If the file is not an ELF64 little-endian object, or a table is outside the file
    """
    with open(path, "rb") as handle:
        try:
            data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        except ValueError:
            raise ValueError("the file is empty") from None
    with data:
        if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
            raise ValueError("the file is not an ELF64 little-endian object")
        (section_offset,) = struct.unpack_from("<Q", data, 0x28)
        entry_size, count = struct.unpack_from("<HH", data, 0x3A)
        if count == 0 and section_offset:
            (count,) = struct.unpack_from("<Q", data, section_offset + 0x20)
        end = section_offset + count * entry_size
        if entry_size != 64 or end > len(data):
            raise ValueError("the section header table is outside the file")
        headers = list(struct.iter_unpack("<IIQQQQIIQQ", data[section_offset:end]))
        text_bytes = 0
        symtab = None
        for (_name, kind, flags, _address, offset, size, link, _info, _align, _entry) in headers:
            if flags & (SHF_ALLOC | SHF_EXECINSTR) == SHF_ALLOC | SHF_EXECINSTR:
                text_bytes += size
            if kind == SHT_SYMTAB:
                symtab = (offset, size, link)
        largest = 0
        largest_name = ""
        if symtab is not None:
            offset, size, link = symtab
            if offset + size > len(data) or link >= len(headers):
                raise ValueError("the symbol table is outside the file")
            name_offset = 0
            for (name, info, _other, _section, _value, symbol_size) in struct.iter_unpack(
                    "<IBBHQQ", data[offset:offset + size - size % 24]):
                if info & 0xF == STT_FUNC and symbol_size > largest:
                    largest, name_offset = symbol_size, name
            if largest:
                strings = headers[link][4]
                stop = data.find(b"\0", strings + name_offset)
                largest_name = data[strings + name_offset:stop].decode("utf-8", errors="replace")
    return ObjectCode(text_bytes, largest, largest_name)


def demangled(names: Iterable[str]) -> dict[str, str]:
    """Demangle function names with c++filt, when it is on PATH.

    Args:
        names: The mangled names

    Returns:
        The readable name of each mangled name, or the mangled name itself
    """
    unique = sorted(set(names))
    if not unique:
        return {}
    try:
        result = subprocess.run(["c++filt"], input="\n".join(unique) + "\n", capture_output=True, text=True,
                                check=True)
        readable = result.stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        readable = unique
    if len(readable) != len(unique):
        readable = unique
    return dict(zip(unique, readable, strict=True))


def code_measures(context: Context, outputs: list[Output]) -> tuple[list[Measure], list[check_report.Finding], str]:
    """Read the machine code of each object for function-size or object-text.

    Args:
        context: The run
        outputs: The objects

    Returns:
        The measures, the errors for the objects that cannot be read, and the summary line
    """
    codes: list[tuple[Output, ObjectCode]] = []
    findings: list[check_report.Finding] = []
    for output in outputs:
        try:
            codes.append((output, read_object_code(output.path)))
        except (OSError, ValueError, struct.error) as problem:
            findings.append(error_at(context.check, shown_path(output.source or output.path, context.root),
                                     f"the object {shown_path(output.path, context.build_dir)} cannot be read: "
                                     f"{problem}"))
    names = demangled(code.largest_name for _, code in codes) if context.check == "function-size" else {}
    measures: list[Measure] = []
    for output, code in codes:
        item = shown_path(output.path, context.build_dir)
        if context.check == "function-size":
            value = code.largest_function / KB
            name = names.get(code.largest_name, code.largest_name)
            words = f"the largest function of {item}, {name}, has {value:.1f} KB of machine code"
        else:
            value = code.text_bytes / KB
            words = f"the object {item} has {value:.1f} KB of machine code"
        measures.append(Measure(item, shown_path(output.source or output.path, context.root), value, words))
    return measures, findings, f"{context.check}: {len(outputs)} objects, {len(codes)} read"


def read_dependencies(ninja: str, build_dir: Path, keys: list[str]) -> dict[str, tuple[str, list[str]]]:
    """Read the dependency lists of some outputs from the ninja log.

    Args:
        ninja: The ninja program
        build_dir: The build directory
        keys: The outputs, relative to the build directory

    Returns:
        For each output in the log: its state (VALID, STALE or MISSING) and its files

    Raises:
        OSError: If ninja cannot start or fails
    """
    result = subprocess.run([ninja, "-C", str(build_dir), "-t", "deps", *keys], capture_output=True, text=True)
    if result.returncode != 0:
        raise OSError(f"{ninja} -t deps exited {result.returncode}: {result.stderr.strip()[:300]}")
    lists: dict[str, tuple[str, list[str]]] = {}
    current: list[str] | None = None
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        if line.startswith((" ", "\t")):
            if current is not None:
                current.append(line.strip())
            continue
        match = DEPS_HEADER.fullmatch(line)
        if match is None:
            current = None
            continue
        current = []
        lists[match["target"]] = (match["state"] or "MISSING", current)
    return lists


def sentinel_outputs(outputs: list[Output]) -> list[Output]:
    """Return the objects of the sentinels of test/layer."""
    return [output for output in outputs if SENTINEL_MARK in output.path.as_posix()]


def sentinel_timing(context: Context, output: Output) -> str:
    """Tell the CPU time of one sentinel compile from its record, for the message of a header.

    A ccache hit gives the time of the last real compile that its record keeps, if it keeps one.

    Args:
        context: The run of the header-alone check
        output: The object of the sentinel

    Returns:
        A clause that completes the message
    """
    record, _ = read_record(output, "compile")
    if record is None:
        return "its sentinel has no usable record"
    has_check_file = output.source is not None and output.source.is_relative_to(context.root / CHECK_FILES)
    scope = " with the checks of its check file" if has_check_file else ""
    if record["result"] != "hit":
        return f"its sentinel took {float(record['cost']['cpu_s']):.1f} s CPU{scope}"
    last_cost = record.get("last_cost")
    if isinstance(last_cost, dict) and isinstance(last_cost.get("cpu_s"), (int, float)):
        return (f"its sentinel was a ccache hit, and the last real compile took {float(last_cost['cpu_s']):.1f} s "
                f"CPU{scope}")
    return "its sentinel was a ccache hit, and its record keeps no time"


def header_measures(context: Context, outputs: list[Output]) -> tuple[list[Measure], list[check_report.Finding],
                                                                      str]:
    """Measure the bytes that each header includes alone, from the dependency list of its sentinel.

    Complexity: linear in the files of all dependency lists, with one stat for each distinct file.

    Args:
        context: The run of header-alone
        outputs: The objects of the build

    Returns:
        The measures, the errors for the sentinels that cannot be measured, and the summary line
    """
    sentinels = sentinel_outputs(outputs)
    findings: list[check_report.Finding] = []
    keys = [shown_path(output.path, context.build_dir) for output in sentinels]
    try:
        lists = read_dependencies(context.ninja, context.build_dir, keys)
    except OSError as problem:
        return [], [error_at(context.check, shown_path(context.build_dir, context.root), str(problem))], \
            f"{context.check}: no dependency log"
    include_root = context.root / "include"
    sizes: dict[str, tuple[Path, int]] = {}

    def resolved(name: str) -> tuple[Path, int]:
        if name not in sizes:
            real = Path(os.path.realpath(name if os.path.isabs(name) else context.build_dir / name))
            sizes[name] = (real, real.stat().st_size)
        return sizes[name]

    measures: list[Measure] = []
    for output, key in zip(sentinels, keys, strict=True):
        source_shown = shown_path(output.source or output.path, context.root)
        state, files = lists.get(key, ("MISSING", []))
        if state != "VALID" or not files:
            findings.append(error_at(context.check, source_shown,
                                     f"the ninja log holds no valid dependency list of {key} (state {state}).  Build "
                                     f"the object again"))
            continue
        try:
            entries = [resolved(name) for name in files[1:]]
        except OSError as problem:
            findings.append(error_at(context.check, source_shown, f"a dependency of {key} cannot be read: {problem}"))
            continue
        header = next((real for real, _ in entries if real.is_relative_to(include_root)), None)
        if header is None:
            findings.append(error_at(context.check, source_shown, f"the sentinel {key} includes no header of include/"))
            continue
        header_shown = shown_path(header, context.root)
        layer = header.relative_to(include_root).parts[0]
        if layer not in LAYER_ROWS:
            findings.append(error_at(context.check, header_shown, f"the header is in the layer {layer}, which has no "
                                                                  f"budget row.  LAYER_ROWS names a row for each layer"))
            continue
        total = sum(size for _, size in entries)
        timing = sentinel_timing(context, output)
        measures.append(Measure(header_shown, header_shown, total / MB,
                                f"the header alone includes {total / MB:.2f} MB in {len(entries)} files, and {timing}",
                                row=LAYER_ROWS[layer]))
    return measures, findings, f"{context.check}: {len(sentinels)} sentinels, {len(measures)} measured"


def ledger_path(context: Context) -> Path:
    """Return the ledger file of the check of a run."""
    return context.ledger_dir / f"{context.check}-ledger.txt"


def read_ledger(context: Context) -> tuple[Ledger, list[check_report.Finding]]:
    """Read the ledger of one check.

    Args:
        context: The run

    Returns:
        The ledger, and an error for each row that is not valid and for a missing file
    """
    path = ledger_path(context)
    shown = shown_path(path, context.root)
    try:
        rows, problems, header = cost_meter.read_ledger_rows(str(path))
    except OSError as problem:
        return Ledger(path, shown, [], {}), [
            error_at(context.check, shown, f"the ledger cannot be read ({problem}).  It holds a comment block and "
                                           f"no row when no item exceeds the error threshold")]
    return Ledger(path, shown, header, rows), [error_at(context.check, shown, text, line) for line, text in problems]


def judge(context: Context, budgets: dict[str, check_report.Budget], measures: list[Measure], unjudged: set[str],
          ledger: Ledger) -> list[check_report.Finding]:
    """Compare each measure with the thresholds of its row and with the ledger rows that apply to the build.

    Complexity: linear in the measures and the rows.

    Args:
        context: The run
        budgets: The budget rows of the check, by name
        measures: The measures
        unjudged: The items that the build holds and the check does not judge (a ccache hit)
        ledger: The ledger of the check

    Returns:
        The findings
    """
    rows = {item: entry for (kind, item), entry in ledger.rows.items() if kind == context.kind}
    findings: list[check_report.Finding] = []
    measured: set[str] = set()
    for measure in measures:
        measured.add(measure.item)
        budget = budgets[measure.row or context.check]
        budget_unit = budget.unit
        level = check_report.classify(measure.value, budget)
        row = rows.get(measure.item)
        if row is None and level == "error" and measure.error_row:
            findings.append(check_report.Finding(
                "warning", measure.path, 0, context.check,
                f"{measure.message}, over the error threshold {budget.error:g} {budget_unit}.  The record holds an "
                f"instruction count, so the row {measure.error_row} holds the error level of this step, and the CPU "
                f"time, which rises with the load of the host, gives a warning only"))
            continue
        of_row = f" of the row {measure.row}" if measure.row else ""
        if row is None:
            if level is not None:
                limit = budget.error if level == "error" else budget.warn
                findings.append(check_report.judged(
                    level, measure.path, 0, context.check,
                    f"{measure.message}, over the {level} threshold{of_row} {limit:g} {budget_unit}"))
            continue
        line, value, reason = row
        if level == "error":
            findings.append(check_report.Finding(
                "warning", measure.path, 0, context.check,
                f"{measure.message}, over the error threshold{of_row} {budget.error:g} {budget_unit}.  The row "
                f"{ledger.shown}:{line} admits it (value {value:g} at the last --write): {reason}"))
        else:
            findings.append(check_report.judged(
                "error", ledger.shown, line, context.check,
                f"the row admits {measure.item} in the kind {context.kind}, and it is at {measure.value:.2f} "
                f"{budget_unit}, at or under the error threshold{of_row} {budget.error:g} {budget_unit}.  Remove the "
                f"row"))
    for item, (line, _value, _reason) in sorted(rows.items(), key=lambda entry: entry[1][0]):
        if item not in measured and item not in unjudged:
            findings.append(check_report.Finding(
                "warning", ledger.shown, line, context.check,
                f"the build of kind {context.kind} holds no {item}.  When no build of that kind makes it, remove the "
                f"row"))
    return findings


def measure_all(context: Context) -> tuple[list[Measure], set[str], list[check_report.Finding], str]:
    """Measure each item of the check of a run.

    Args:
        context: The run

    Returns:
        The measures, the unjudged items, the errors of the inputs, and the summary line

    Raises:
        NotApplicable: If the check does not apply to the build
    """
    if context.check == "header-alone" and not context.ninja:
        raise NotApplicable("the build has no ninja dependency log, because the ninja generator did not make it")
    if context.check in TIMED and TIMED[context.check][0] == "link":
        outputs, findings = read_links(context)
    else:
        outputs, findings = read_objects(context)
    if not outputs:
        return [], set(), findings, f"{context.check}: no output"
    unjudged: set[str] = set()
    if context.check in TIMED:
        measures, unjudged, problems, summary = timed_measures(context, outputs)
    elif context.check == "header-alone":
        if not sentinel_outputs(outputs):
            raise NotApplicable("the build holds no sentinel object of test/layer, because it made only some "
                                "targets.  The target all makes the sentinels")
        measures, problems, summary = header_measures(context, outputs)
    else:
        measures, problems, summary = code_measures(context, outputs)
    return measures, unjudged, findings + problems, summary


def budget_of(context: Context) -> dict[str, check_report.Budget]:
    """Return the budget rows of the check of a run, by name: the row of the check, or the row of each layer.

    Raises:
        ValueError: If the table is not valid or has no row that the check reads
    """
    budgets = check_report.read_budgets(context.budgets_path)
    names = list(LAYER_ROWS.values()) if context.check == "header-alone" else [context.check]
    missing = [name for name in names if name not in budgets]
    if missing:
        raise ValueError(f"{context.budgets_path} has no row for {', '.join(missing)}")
    return {name: budgets[name] for name in names}


def run_check(context: Context, warnings_dir: Path | None) -> int:
    """Run one check over one build and report its findings.

    Args:
        context: The run
        warnings_dir: The warnings directory, or None

    Returns:
        The exit status
    """
    try:
        budgets = budget_of(context)
    except ValueError as problem:
        return check_report.emit([error_at(context.check, shown_path(context.budgets_path, context.root),
                                           str(problem))], context.check, warnings_dir)
    try:
        measures, unjudged, findings, summary = measure_all(context)
    except NotApplicable as reason:
        print(f"{context.check}: {reason}.  The check does not apply")
        return NOT_APPLICABLE
    ledger, ledger_findings = read_ledger(context)
    findings += ledger_findings
    findings += judge(context, budgets, measures, unjudged, ledger)
    status = check_report.emit(findings, context.check, warnings_dir)
    print(summary)
    return status


def write_ledger(context: Context, reason: str | None) -> int:
    """Write the rows of the kind of a run again, from the measures of its build.

    Args:
        context: The run
        reason: The reason of each new row, or None

    Returns:
        0 when the ledger was written, 1 when an input or the ledger cannot be read or a new row has no reason
    """
    try:
        budgets = budget_of(context)
        measures, unjudged, findings, _ = measure_all(context)
    except (ValueError, NotApplicable) as problem:
        print(f"{context.check}: {problem}", file=sys.stderr)
        return 1
    ledger, ledger_findings = read_ledger(context)
    problems = [found for found in findings + ledger_findings if found.level == "error"]
    if problems:
        for found in problems:
            print(found.text(), file=sys.stderr)
        return 1
    kept = {key: entry for key, entry in ledger.rows.items() if key[0] != context.kind or key[1] in unjudged}
    unexplained: list[str] = []
    for measure in measures:
        if measure.value <= budgets[measure.row or context.check].error:
            continue
        key = (context.kind, measure.item)
        old_reason = ledger.rows[key][2] if key in ledger.rows else reason
        if not old_reason:
            unexplained.append(measure.item)
            continue
        kept[key] = (0, round(measure.value, 3), old_reason)
    if unexplained:
        print(f"{context.check}: these items exceed the error threshold and have no row.  Give the reason of their "
              f"rows with --reason: {', '.join(unexplained)}", file=sys.stderr)
        return 1
    lines = ledger.header + [f"{kind} | {item} | {value:g} | {why}"
                             for (kind, item), (_, value, why) in sorted(kept.items())]
    ledger.path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    count = sum(1 for kind, _ in kept if kind == context.kind)
    print(f"{context.check}: {ledger.shown} holds {count} rows of the kind {context.kind}")
    return 0


# ── The self-test of the checks ────────────────────────────────────────


def load_launcher() -> object:
    """Load utils/scripts/build-launcher.py as a module, so a test writes records with its writer."""
    spec = importlib.util.spec_from_file_location("build_launcher", LAUNCHER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def plant_object(path: Path, functions: list[tuple[str, int]], loose_text: int = 0) -> None:
    """Write a small ELF64 object with one executable section for each function.

    The sections hold no bytes in the file, because read_object_code reads
    only the section headers and the symbol table.

    Args:
        path: The object
        functions: The name and the size of each function
        loose_text: The size of one more executable section with no symbol
    """
    section_names = b"\0.symtab\0.strtab\0.shstrtab\0.text\0"
    strings = b"\0" + b"".join(name.encode() + b"\0" for name, _ in functions)
    symbols = bytes(24)
    position = 1
    for index, (name, size) in enumerate(functions):
        symbols += struct.pack("<IBBHQQ", position, 0x12, 0, 4 + index, 0, size)
        position += len(name) + 1
    strings_at = 64
    names_at = strings_at + len(strings)
    symbols_at = (names_at + len(section_names) + 7) // 8 * 8
    headers_at = symbols_at + len(symbols)
    headers = [bytes(64),
               struct.pack("<IIQQQQIIQQ", 1, SHT_SYMTAB, 0, 0, symbols_at, len(symbols), 2, 1, 8, 24),
               struct.pack("<IIQQQQIIQQ", 9, 3, 0, 0, strings_at, len(strings), 0, 0, 1, 0),
               struct.pack("<IIQQQQIIQQ", 17, 3, 0, 0, names_at, len(section_names), 0, 0, 1, 0)]
    for _, size in functions:
        headers.append(struct.pack("<IIQQQQIIQQ", 27, 1, SHF_ALLOC | SHF_EXECINSTR, 0, 0, size, 0, 0, 16, 0))
    if loose_text:
        headers.append(struct.pack("<IIQQQQIIQQ", 27, 1, SHF_ALLOC | SHF_EXECINSTR, 0, 0, loose_text, 0, 0, 16, 0))
    elf_header = (b"\x7fELF\x02\x01\x01" + bytes(9)
                  + struct.pack("<HHIQQQIHHHHHH", 1, 62, 1, 0, 0, headers_at, 0, 64, 0, 0, 64, len(headers), 3))
    body = strings + section_names
    padding = bytes(symbols_at - names_at - len(section_names))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(elf_header + body + padding + symbols + b"".join(headers))


class Scratch:
    """A scratch repository root and build directory for the self-test of the checks."""

    KIND = "x86_64-debug-asan"

    def __init__(self, root: Path) -> None:
        """Make the empty tree."""
        self.root = root
        self.build = root / "build"
        self.ledgers = root / "ledgers"
        self.budgets = root / "budgets.txt"
        self.ninja = root / "fake-ninja.py"
        self.deps = root / "deps.txt"
        self.rows: list[dict[str, str]] = []
        self.targets: list[dict[str, object]] = []
        self.build.mkdir(parents=True)
        self.ledgers.mkdir()
        (self.build / cost_meter.KIND_FILE).write_text(f"{self.KIND}\n", encoding="utf-8")
        self.budgets.write_text("compile-cpu | 10 | 20 | s | t\ncompile-memory | 2 | 4 | GB | m\n"
                                "compile-instructions | 50 | 100 | G instructions | i\n"
                                "link-time | 2 | 5 | s | t\nlink-memory | 0.5 | 1 | GB | m\n"
                                "function-size | 64 | 256 | KB | f\nobject-text | 512 | 768 | KB | o\n"
                                "header-alone-foundation | 2 | 3 | MB | h\nheader-alone-fixy | 8 | 12 | MB | h\n"
                                "header-alone-crucible | 9 | 14 | MB | h\n", encoding="utf-8")
        for check in CHECKS:
            (self.ledgers / f"{check}-ledger.txt").write_text("# a planted ledger\n", encoding="utf-8")
        self.ninja.write_text(f"#!{sys.executable}\nimport sys\nsys.stdout.write(open({str(self.deps)!r}).read())\n",
                              encoding="utf-8")
        self.ninja.chmod(0o755)
        self.deps.write_text("", encoding="utf-8")
        self.write_database()
        self.write_reply()
        self.launcher = load_launcher()

    def record(self, step: str, output: Path, source: Path | None, result: str, cpu_s: float,
               peak_gb: float, instructions: int | None = None) -> None:
        """Write the record of one output with the writer of the launcher, as a compile with no ccache gives it."""
        run = cost_meter.Measurement(0, cpu_s, 0.0, cpu_s, int(peak_gb * cost_meter.KB_PER_GB), 1.0, 1.0, None,
                                     instructions)
        counts = self.launcher.compile_counts(run, False, None) if step == "compile" else ()
        self.launcher.write(step, str(output), None if source is None else str(source), run, result, counts)

    def unit(self, name: str, *, functions: list[tuple[str, int]] | None = None, loose_text: int = 0,
             result: str | None = "built", cpu_s: float = 1.0, peak_gb: float = 0.2,
             sentinel: bool = False, instructions: int | None = None) -> Path:
        """Plant one compile job: a source, its object, its row and its record."""
        source = self.root / ("test/layer/checks" if sentinel else "src") / f"{name}.cpp"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text("int x;\n", encoding="utf-8")
        directory = "test/layer/CMakeFiles/layer_sentinel_fixy.dir" if sentinel else "CMakeFiles/lib.dir"
        object_path = self.build / directory / f"{name}.cpp.o"
        plant_object(object_path, functions if functions is not None else [("f", 100)], loose_text)
        self.rows.append({"directory": str(self.build), "command": "c++", "file": str(source),
                          "output": str(object_path)})
        self.write_database()
        if result is not None:
            self.record("compile", object_path, source, result, cpu_s, peak_gb, instructions)
        return object_path

    def program(self, name: str, *, kind: str = "EXECUTABLE", result: str | None = "built", cpu_s: float = 0.5,
                peak_gb: float = 0.1, exists: bool = True) -> Path:
        """Plant one linked output: its target in the file API reply, the file and its record."""
        output = self.build / "test" / name
        self.targets.append({"name": name, "type": kind, "artifacts": [{"path": f"test/{name}"}]})
        self.write_reply()
        if exists:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(b"\x7fELF program")
            if result is not None:
                self.record("link", output, None, result, cpu_s, peak_gb)
        return output

    def write_database(self) -> None:
        """Write the compile database of the planted jobs."""
        (self.build / "compile_commands.json").write_text(json.dumps(self.rows), encoding="utf-8")

    def write_reply(self) -> None:
        """Write a codemodel reply of the CMake file API with the planted targets."""
        reply = self.build / ".cmake" / "api" / "v1" / "reply"
        reply.mkdir(parents=True, exist_ok=True)
        targets = []
        for number, target in enumerate(self.targets):
            (reply / f"target-{number}.json").write_text(json.dumps(target), encoding="utf-8")
            targets.append({"name": target["name"], "jsonFile": f"target-{number}.json"})
        library = {"name": "library", "type": "STATIC_LIBRARY", "artifacts": [{"path": "liblibrary.a"}]}
        (reply / "target-library.json").write_text(json.dumps(library), encoding="utf-8")
        targets.append({"name": "library", "jsonFile": "target-library.json"})
        (reply / "codemodel-v2-x.json").write_text(json.dumps({"configurations": [{"targets": targets}]}),
                                                   encoding="utf-8")
        (reply / "index-1.json").write_text(json.dumps({"objects": [
            {"kind": "codemodel", "version": {"major": 2, "minor": 0}, "jsonFile": "codemodel-v2-x.json"}]}),
            encoding="utf-8")

    def context(self, check: str, kind: str = KIND) -> Context:
        """Return a run over the scratch build, with the planted ninja that prints the planted dependency log."""
        return Context(check, self.build, kind, str(self.ninja), self.root, self.budgets, self.ledgers)

    def run(self, check: str, kind: str = KIND, ninja: str | None = None, write: bool = False,
            reason: str | None = None) -> tuple[int, list[check_report.Finding], str]:
        """Run one check, or its --write, and return its status, its findings and its output."""
        context = self.context(check, kind)
        if ninja is not None:
            context.ninja = ninja
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = write_ledger(context, reason) if write else run_check(context, None)
        found = [parsed for line in output.getvalue().splitlines()
                 if (parsed := check_report.parse_line(line)) is not None]
        return status, found, output.getvalue()

    def ledger(self, check: str, rows: str) -> None:
        """Write rows into the planted ledger of one check."""
        (self.ledgers / f"{check}-ledger.txt").write_text(f"# a planted ledger\n{rows}", encoding="utf-8")


def self_test() -> int:
    """Plant each verdict of each check and examine it, with GITHUB_ACTIONS removed except in the cases that set it.

    Returns:
        0 when every case holds, else 2
    """
    with check_report.github_actions(False):
        return self_test_cases()


def self_test_cases() -> int:
    """Plant each verdict of each check and examine it.

    Returns:
        0 when every case holds, else 2
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def levels(found: list[check_report.Finding]) -> list[str]:
        return sorted(f.level for f in found)

    debug = Scratch.KIND
    with tempfile.TemporaryDirectory(prefix="compile-cost-") as scratch_name:
        root = Path(scratch_name).resolve()

        # compile-cpu and compile-memory.
        tree = Scratch(root / "timed")
        tree.unit("quick", cpu_s=2.0)
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a 2 s job gives no finding", status == 0 and not found)
        tree.unit("slow", cpu_s=12.0, peak_gb=3.0)
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a 12 s job gives a warning", status == 0 and levels(found) == ["warning"]
               and "src/slow.cpp" == found[0].path)
        status, found, _ = tree.run("compile-memory")
        expect("compile-memory: a 3 GB job gives a warning", status == 0 and levels(found) == ["warning"])
        tree.unit("slower", cpu_s=25.0, peak_gb=5.0)
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a 25 s job gives an error", status == 1 and levels(found) == ["error", "warning"])
        status, found, _ = tree.run("compile-memory")
        expect("compile-memory: a 5 GB job gives an error", status == 1 and levels(found) == ["error", "warning"])
        with check_report.github_actions(True):
            status, found, _ = tree.run("compile-cpu")
            expect("compile-cpu: on a CI runner, a 25 s job gives a warning that says that the error was demoted",
                   status == 0 and levels(found) == ["warning", "warning"]
                   and any("demoted" in f.message for f in found if f.path == "src/slower.cpp"))
            status, found, _ = tree.run("compile-memory")
            expect("compile-memory: on a CI runner, a 5 GB job still gives an error",
                   status == 1 and levels(found) == ["error", "warning"])
        tree.ledger("compile-cpu", f"{debug} | CMakeFiles/lib.dir/slower.cpp.o | 25 | a planted reason\n")
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a row turns the error into a warning that gives the reason",
               status == 0 and levels(found) == ["warning"] * 2
               and any("a planted reason" in f.message for f in found))
        status, found, _ = tree.run("compile-cpu", "x86_64-release")
        expect("compile-cpu: a row of another kind does not apply", status == 1)
        tree.ledger("compile-cpu", f"{debug} | CMakeFiles/lib.dir/slow.cpp.o | 21 | a planted reason\n")
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a row of an item under the error threshold is an error",
               status == 1 and any(f.path.endswith("compile-cpu-ledger.txt") and f.level == "error" for f in found))
        with check_report.github_actions(True):
            status, found, _ = tree.run("compile-cpu")
            expect("compile-cpu: on a CI runner, a row of an item under the error threshold is a warning",
                   status == 0 and any(f.path.endswith("compile-cpu-ledger.txt") and f.level == "warning"
                                       and "demoted" in f.message for f in found))
        tree.ledger("compile-cpu", f"{debug} | CMakeFiles/lib.dir/gone.cpp.o | 21 | a planted reason\n"
                                   f"{debug} | CMakeFiles/lib.dir/slower.cpp.o | 25 | a planted reason\n")
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a row of an item that the build does not hold is a warning",
               status == 0 and sum(f.path.endswith("ledger.txt") for f in found) == 1)
        tree.ledger("compile-cpu", f"{debug} | CMakeFiles/lib.dir/slower.cpp.o | 25\n")
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a row of three cells is an error",
               status == 1 and any(f.line == 2 and f.path.endswith("ledger.txt") for f in found))
        tree.ledger("compile-cpu", "Debug | CMakeFiles/lib.dir/slower.cpp.o | 25 | a planted reason\n")
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a kind with an uppercase letter is an error", status == 1)
        (tree.ledgers / "compile-cpu-ledger.txt").unlink()
        status, found, _ = tree.run("compile-cpu")
        expect("compile-cpu: a missing ledger is an error", status == 1)
        tree.ledger("compile-cpu", "")

        # compile-instructions, and the error level of compile-cpu when a record holds a count.
        giga = int(GIGA)
        status, found, output = tree.run("compile-instructions")
        expect("compile-instructions: a build whose records hold no count exits 3",
               status == NOT_APPLICABLE and not found and "no exact counter" in output)
        counted = Scratch(root / "counted")
        counted.unit("light", cpu_s=2.0, instructions=10 * giga)
        status, found, output = counted.run("compile-instructions")
        expect("compile-instructions: 10 G instructions give no finding", status == 0 and not found)
        counted.unit("uncounted", cpu_s=25.0)
        status, found, output = counted.run("compile-instructions")
        expect("compile-instructions: a record with no count is not judged, and the summary counts it",
               status == 0 and not found and "1 with no exact count" in output)
        status, found, _ = counted.run("compile-cpu")
        expect("compile-cpu: a 25 s job with no count keeps the error",
               status == 1 and [f.level for f in found if f.path == "src/uncounted.cpp"] == ["error"])
        counted.unit("loaded", cpu_s=25.0, instructions=60 * giga)
        status, found, _ = counted.run("compile-cpu")
        loaded = [f for f in found if f.path == "src/loaded.cpp"]
        expect("compile-cpu: a 25 s job whose record holds a count gives a warning that names compile-instructions",
               len(loaded) == 1 and loaded[0].level == "warning" and "compile-instructions" in loaded[0].message)
        status, found, _ = counted.run("compile-instructions")
        expect("compile-instructions: 60 G instructions give a warning at the source",
               status == 0 and levels(found) == ["warning"] and found[0].path == "src/loaded.cpp"
               and "60.0 G user instructions" in found[0].message)
        counted.unit("heavy", cpu_s=5.0, instructions=120 * giga)
        status, found, _ = counted.run("compile-instructions")
        expect("compile-instructions: 120 G instructions give an error", status == 1
               and [f.level for f in found if f.path == "src/heavy.cpp"] == ["error"])
        with check_report.github_actions(True):
            status, found, _ = counted.run("compile-instructions")
            expect("compile-instructions: on a CI runner, an instruction error stays an error", status == 1)
        counted.ledger("compile-instructions", f"{debug} | CMakeFiles/lib.dir/heavy.cpp.o | 120 | a planted reason\n")
        status, found, _ = counted.run("compile-instructions")
        expect("compile-instructions: a row turns the error into a warning", status == 0
               and any("a planted reason" in f.message for f in found))
        counted.ledger("compile-instructions", f"{debug} | CMakeFiles/lib.dir/light.cpp.o | 120 | a planted reason\n")
        status, found, _ = counted.run("compile-instructions")
        expect("compile-instructions: a row of an item under the error threshold is an error",
               status == 1 and any(f.path.endswith("compile-instructions-ledger.txt") and f.level == "error"
                                   for f in found))
        counted.ledger("compile-instructions", "")

        hits = Scratch(root / "hits")
        hits.unit("cached", result="hit")
        status, found, output = hits.run("compile-cpu")
        expect("compile-cpu: a build of hits only passes, and the summary counts the hits",
               status == 0 and not found and "1 ccache hits" in output)
        hits.ledger("compile-cpu", f"{debug} | CMakeFiles/lib.dir/cached.cpp.o | 25 | a planted reason\n")
        status, found, _ = hits.run("compile-cpu")
        expect("compile-cpu: a row of a hit gives no finding", status == 0 and not found)

        bare = Scratch(root / "bare")
        bare.unit("plain", result=None)
        bare.unit("second", result=None)
        status, found, _ = bare.run("compile-cpu")
        expect("compile-cpu: no record at all gives one error", status == 1 and len(found) == 1
               and "not wired" in found[0].message)
        bare.unit("third", cpu_s=1.0)
        status, found, _ = bare.run("compile-cpu")
        expect("compile-cpu: an object with no record is an error", status == 1 and len(found) == 2)
        stale = bare.unit("stale", cpu_s=1.0)
        plant_object(stale, [("f", 100), ("g", 300)])
        status, found, _ = bare.run("compile-cpu")
        expect("compile-cpu: a record of another output size is an error",
               status == 1 and any("did not come through" in f.message for f in found if f.path == "src/stale.cpp"))
        Path(str(stale) + cost_meter.RECORD_SUFFIX).write_text('{"format": 2}', encoding="utf-8")
        status, found, _ = bare.run("compile-cpu")
        expect("compile-cpu: a record of another format is an error",
               status == 1 and any("format" in f.message for f in found if f.path == "src/stale.cpp"))

        empty = Scratch(root / "empty")
        status, found, _ = empty.run("compile-cpu")
        expect("compile-cpu: a build with no object is an error",
               status == 1 and len(found) == 1 and "no object" in found[0].message)
        (empty.build / "compile_commands.json").unlink()
        status, found, _ = empty.run("function-size")
        expect("function-size: a build with no compile database is an error", status == 1)

        # link-time and link-memory.
        links = Scratch(root / "links")
        links.program("quick_test", cpu_s=0.5)
        links.program("unbuilt_test", exists=False)
        status, found, output = links.run("link-time")
        expect("link-time: a 0.5 s link gives no finding, and an output that is not built is not read",
               status == 0 and not found and "1 outputs" in output)
        links.program("slow_test", cpu_s=3.0, peak_gb=0.7)
        links.program("library.so", kind="SHARED_LIBRARY", cpu_s=6.0, peak_gb=1.5)
        status, found, _ = links.run("link-time")
        expect("link-time: a 3 s link gives a warning and a 6 s link of a shared library an error",
               status == 1 and levels(found) == ["error", "warning"]
               and any(f.path.endswith("library.so") for f in found if f.level == "error"))
        status, found, _ = links.run("link-memory")
        expect("link-memory: 0.7 GB gives a warning and 1.5 GB an error",
               status == 1 and levels(found) == ["error", "warning"])
        links.program("unrecorded_test", result=None)
        status, found, _ = links.run("link-time")
        expect("link-time: a linked output with no record is an error",
               any("unrecorded_test" in f.message and f.level == "error" for f in found))
        bare_links = Scratch(root / "bare-links")
        bare_links.program("first_test", result=None)
        status, found, _ = bare_links.run("link-memory")
        expect("link-memory: no link record at all gives one error",
               status == 1 and len(found) == 1 and "not wired" in found[0].message)
        reply = empty.build / ".cmake"
        for path in sorted(reply.rglob("*"), reverse=True):
            path.unlink() if path.is_file() else path.rmdir()
        reply.rmdir()
        status, found, _ = empty.run("link-time")
        expect("link-time: a build with no file API reply is an error",
               status == 1 and "file API" in found[0].message)

        # function-size and object-text.
        code = Scratch(root / "code")
        code.unit("small", functions=[("_Z5smallv", 10 * KB)])
        status, found, _ = code.run("function-size")
        expect("function-size: a 10 KB function gives no finding", status == 0 and not found)
        code.unit("big", functions=[("_Z3bigv", 100 * KB), ("_Z4tinyv", KB)])
        status, found, _ = code.run("function-size")
        expect("function-size: a 100 KB function gives a warning that names it",
               status == 0 and levels(found) == ["warning"] and "big()" in found[0].message)
        code.unit("giant", functions=[("_Z5giantv", 300 * KB)])
        status, found, _ = code.run("function-size")
        expect("function-size: a 300 KB function gives an error", status == 1 and "error" in levels(found))
        status, found, _ = code.run("object-text")
        expect("object-text: 300 KB of code gives no finding", status == 0 and not found)
        code.unit("wide", functions=[("_Z1av", 300 * KB), ("_Z1bv", 300 * KB)])
        status, found, _ = code.run("object-text")
        expect("object-text: 600 KB of code gives a warning", status == 0 and levels(found) == ["warning"])
        code.unit("wider", functions=[("_Z1cv", 100 * KB)], loose_text=700 * KB)
        status, found, _ = code.run("object-text")
        expect("object-text: a section with no symbol counts, 800 KB gives an error",
               status == 1 and levels(found) == ["error", "warning"])
        code.ledger("object-text", f"{debug} | CMakeFiles/lib.dir/wider.cpp.o | 800 | a planted reason\n")
        status, found, _ = code.run("object-text")
        expect("object-text: a row turns the error into a warning", status == 0)
        broken = code.unit("broken")
        broken.write_bytes(b"not an object")
        status, found, _ = code.run("object-text")
        expect("object-text: an object that is not ELF is an error",
               status == 1 and any(f.path == "src/broken.cpp" for f in found))

        # header-alone.
        headers = Scratch(root / "headers")
        for layer in ("fixy", "foundation", "other"):
            (headers.root / "include" / layer).mkdir(parents=True)
            link_root = headers.build / "layer-roots" / layer
            link_root.mkdir(parents=True)
            (link_root / layer).symlink_to(headers.root / "include" / layer)
        include = headers.root / "include" / "fixy"
        (include / "Small.h").write_bytes(b"x" * MB)
        (include / "Big.h").write_bytes(b"x" * (9 * MB))
        (include / "Huge.h").write_bytes(b"x" * (13 * MB))
        (headers.root / "include" / "foundation" / "Base.h").write_bytes(b"x" * (5 * MB // 2))
        (headers.root / "include" / "other" / "Stray.h").write_bytes(b"x" * MB)
        system = headers.root / "system.h"
        system.write_bytes(b"y" * 1000)
        deps: list[str] = []

        def sentinel(name: str, header: str, state: str = "VALID", layer: str = "fixy") -> None:
            obj = headers.unit(name, sentinel=True, cpu_s=3.0)
            key = obj.relative_to(headers.build).as_posix()
            files = [str(headers.root / "test/layer/checks" / f"{name}.cpp"), str(system),
                     str(headers.build / "layer-roots" / layer / layer / header)]
            deps.append(f"{key}: #deps {len(files)}, deps mtime 1 ({state})\n" + "".join(f"    {f}\n" for f in files))
            headers.deps.write_text("\n".join(deps), encoding="utf-8")

        sentinel("Small", "Small.h")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a 1 MB header gives no finding", status == 0 and not found)
        sentinel("Big", "Big.h")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a 9 MB header gives a warning at the path of the header",
               status == 0 and levels(found) == ["warning"] and found[0].path == "include/fixy/Big.h"
               and "3.0 s CPU" in found[0].message)
        sentinel("Huge", "Huge.h")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a 13 MB header gives an error that names the row of its layer",
               status == 1 and levels(found) == ["error", "warning"]
               and any("of the row header-alone-fixy 12 MB" in f.message for f in found if f.level == "error"))
        headers.ledger("header-alone", f"{debug} | include/fixy/Huge.h | 13 | a planted reason\n")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a row turns the error into a warning", status == 0)
        sentinel("Base", "Base.h", layer="foundation")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a 2.5 MB foundation header warns by the tighter row of its layer",
               status == 0 and any(f.path == "include/foundation/Base.h" and f.level == "warning"
                                   and "of the row header-alone-foundation 2 MB" in f.message for f in found))
        sentinel("Stray", "Stray.h", layer="other")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a header of a layer with no row is an error",
               status == 1 and any(f.path == "include/other/Stray.h" and "has no budget row" in f.message
                                   for f in found))
        headers.rows = [row for row in headers.rows if not row["output"].endswith("Stray.cpp.o")]
        headers.write_database()
        sentinel("Stale", "Small.h", state="STALE")
        status, found, _ = headers.run("header-alone")
        expect("header-alone: a stale dependency list is an error",
               status == 1 and any("STALE" in f.message for f in found))
        status, found, _ = headers.run("header-alone", ninja="")
        expect("header-alone: a build with no ninja log exits 3", status == NOT_APPLICABLE and not found)
        status, found, _ = code.run("header-alone")
        expect("header-alone: a build with no sentinel exits 3", status == NOT_APPLICABLE and not found)
        status, found, _ = empty.run("header-alone")
        expect("header-alone: a build with no compile database is an error", status == 1 and len(found) == 1)

        # --write keeps the rows of the other kinds, keeps each reason, and needs --reason for a new row.
        code.ledger("function-size", f"x86_64-release | CMakeFiles/lib.dir/other.cpp.o | 300 | another kind\n"
                                     f"{debug} | CMakeFiles/lib.dir/giant.cpp.o | 290 | the kept reason\n")
        broken.unlink()
        code.rows = [row for row in code.rows if not row["output"].endswith("broken.cpp.o")]
        code.write_database()
        status, _, output = code.run("function-size", write=True)
        expect("--write refuses a new row with no reason, and names its item",
               status == 1 and "wide.cpp.o" in output)
        status, _, _ = code.run("function-size", write=True, reason="a new reason")
        written = (code.ledgers / "function-size-ledger.txt").read_text(encoding="utf-8")
        expect("--write keeps the rows of another kind and each reason, and gives a new row the new reason",
               status == 0 and "x86_64-release | CMakeFiles/lib.dir/other.cpp.o | 300 | another kind" in written
               and f"{debug} | CMakeFiles/lib.dir/giant.cpp.o | 300 | the kept reason" in written
               and f"{debug} | CMakeFiles/lib.dir/wide.cpp.o | 300 | a new reason" in written
               and "big.cpp.o" not in written and written.startswith("# a planted ledger"))
        status, found, _ = code.run("function-size")
        expect("the written ledger passes", status == 0)

    if failures:
        print(f"check-compile-cost --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-compile-cost --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the command line and run the check, the write or the self-test."""
    parser = argparse.ArgumentParser(prog="check-compile-cost.py", description=__doc__.split("\n")[0])
    parser.add_argument("--check", choices=CHECKS)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--kind", help="the kind of the build, in place of the line of BUILD_DIR/build-kind.txt")
    parser.add_argument("--ninja", default="", help="the ninja program of the build, for header-alone")
    parser.add_argument("--write", action="store_true", help="write the ledger rows of the kind again")
    parser.add_argument("--reason", help="the reason of each new ledger row that --write adds")
    parser.add_argument("--self-test", action="store_true", help="plant each verdict of each check")
    check_report.add_arguments(parser)
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.check is None or arguments.build_dir is None:
        parser.error("--check and --build-dir are necessary")
    build_dir = arguments.build_dir.resolve()
    kind = arguments.kind or cost_meter.read_kind(str(build_dir))
    if kind is None:
        return check_report.emit([error_at(arguments.check, shown_path(build_dir / cost_meter.KIND_FILE, REPO_ROOT),
                                           "the build has no build kind.  cmake/BuildLauncher.cmake writes it at "
                                           "each configure.  Configure the build again")],
                                 arguments.check, arguments.warnings_dir)
    if not KIND.fullmatch(kind):
        parser.error(f"the kind {kind!r} is not lowercase words with hyphens")
    context = Context(arguments.check, build_dir, kind, arguments.ninja, REPO_ROOT, check_report.BUDGETS, SCRIPTS)
    if arguments.write:
        return write_ledger(context, arguments.reason)
    return run_check(context, arguments.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
