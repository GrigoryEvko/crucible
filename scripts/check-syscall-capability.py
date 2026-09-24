#!/usr/bin/env python3
"""check-syscall-capability — each Linux syscall site holds a reviewed capability claim.

A direct call of a Linux syscall, or of its thin libc wrapper, in production
code (include/, src/ and vessel/) either carries a `// SYSCALL-CAP-OK: <reason>`
comment in its statement or has an entry in
scripts/syscall-capability-allowlist.txt.  Each entry is

    path:<call text>  — <effects::* capability claim>

where <call text> is the line that holds the syscall name, cut at its first
`//` and trimmed.  The key is the content of the line, so an edit above the
call does not move it.  An entry that matches no live site is stale.

THE SITES
    The guard reads the parse tree of the pinned tree-sitter kit.  A site is
    a call whose callee is
      * a global-scope name, `::name(...)`, where name is in SYSCALLS, or
      * a bare name in KERNEL_ONLY, the syscalls whose spellings no C++
        identifier in this tree shares.
    A member call, a call through a namespace, a declaration, a comment and
    a literal are not sites.  A call that spans lines is one site, keyed on
    the line of its name.

WHAT THE PARSER CANNOT READ
    A macro body is raw text.  The guard lexes it with scripts/cxx_lex.py and
    applies the same two spellings as a pattern.  A file in
    tsast.UNPARSEABLE gets the same lexical scan over its whole text.  A
    parse error in any other file is a guard failure.

Usage:
  check-syscall-capability.py              scan; exit 1 on a violation, 2 on a stale entry
  check-syscall-capability.py --emit-keys  print a template entry for each unlisted site
  check-syscall-capability.py --self-test  plant each shape and prove the verdicts

Exit 0 clean, 1 on a violation or a parse failure, 2 on a stale entry, a
usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from cxx_lex import blank, line_of, splice  # noqa: E402

SYSCALLS = frozenset("""
    socket bind listen connect accept send sendto sendmsg recv recvfrom recvmsg shutdown setsockopt getsockopt
    mmap munmap mremap mlock mlock2 munlock munlockall mlockall madvise mincore mprotect brk sbrk
    sched_setaffinity sched_getaffinity sched_setattr sched_getattr sched_yield prctl syscall
    epoll_create1 epoll_ctl epoll_wait eventfd ioctl open openat close read write pread pwrite readv writev
    fsync fdatasync stat fstat lstat unlink rename mkdir rmdir fork vfork clone execve waitpid kill
    sigaction signal sigprocmask clock_gettime clock_settime nanosleep clock_nanosleep gettimeofday pipe pipe2 dup
    dup2 dup3 fcntl flock poll select pselect futex bpf getrandom getrlimit setrlimit chmod chown access faccessat
""".split())
# The names that no C++ identifier in this tree shares.  A syscall with an
# ambiguous name (close, read, send, socket and the like) must be spelled
# with :: to be seen, which is the house convention.
KERNEL_ONLY = frozenset("""
    sendto sendmsg recvfrom recvmsg setsockopt getsockopt mmap munmap mremap mlock mlock2 munlock munlockall
    mlockall madvise mincore mprotect sched_setaffinity sched_getaffinity sched_setattr sched_getattr
    sched_yield prctl syscall epoll_create1 epoll_ctl epoll_wait eventfd ioctl openat pread pwrite readv
    writev fsync fdatasync fstat lstat unlink mkdir rmdir vfork execve waitpid sigaction sigprocmask
    clock_gettime clock_settime nanosleep clock_nanosleep gettimeofday pipe2 dup2 dup3 fcntl flock pselect futex
    bpf getrandom getrlimit setrlimit chmod chown faccessat
""".split())
assert KERNEL_ONLY <= SYSCALLS, "a kernel-only name is missing from SYSCALLS"

ROOTS = ("include", "src", "vessel")
EXCLUDED = frozenset({"test", "bench", "examples", "third_party", "external", "vendor"})
SUFFIXES = (".h", ".hh", ".hpp", ".cpp", ".cc", ".cxx", ".inl")
ALLOWLIST = "scripts/syscall-capability-allowlist.txt"
MARKER = re.compile(r"SYSCALL-CAP-OK:\s*\S")
LEXICAL = re.compile(r"(?<![A-Za-z_0-9])::\s*(?P<global>[A-Za-z_]\w*)\s*\("
                     r"|(?<![A-Za-z_0-9:.>\s])(?<![A-Za-z_0-9:.>])\s*(?P<bare>[A-Za-z_]\w*)\s*\(")
SYS_NAME = re.compile(r"\bSYS_([A-Za-z0-9_]+)")
# The statements that bound where a marker comment counts.
STATEMENTS = frozenset({"expression_statement", "return_statement", "declaration", "field_declaration",
                        "condition_clause", "init_statement", "for_range_loop", "throw_statement"})


@dataclass(frozen=True)
class Site:
    """One syscall call."""

    path: str
    line: int
    key: str
    names: frozenset[str]


def in_scope(rel: Path) -> bool:
    """Return True when a path relative to the scan root is production code this guard reads."""
    return (rel.suffix in SUFFIXES and bool(rel.parts) and rel.parts[0] in ROOTS
            and not any(part in EXCLUDED or part.startswith("build") for part in rel.parts[:-1]))


def scope_files(root: Path) -> list[Path]:
    """Return every file in scope under a scan root, sorted."""
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            found.extend(p for p in base.rglob("*") if p.is_file() and in_scope(p.relative_to(root)))
    return sorted(found)


def callee_name(call: tsast.Node) -> str | None:
    """Return the syscall a call names, or None when its callee is not a syscall spelling."""
    callee = call.child_by_field("function")
    if callee is None:
        return None
    if callee.type == "identifier":
        return callee.text if callee.text in KERNEL_ONLY else None
    if callee.type == "qualified_identifier" and callee.child_by_field("scope") is None:
        name = callee.child_by_field("name")
        if name is not None and name.type == "identifier" and name.text in SYSCALLS:
            return name.text
    return None


def statement_rows(node: tsast.Node) -> tuple[int, int]:
    """Return the first and last row where a marker for a node's statement may sit.

    For a call in the condition of an if, a while or a switch, the span runs
    on to the first line of the body: the opening line of a compound body,
    or the whole of a single-statement body.  That is where a formatter puts
    the comment after a condition that ends its line.
    """
    owner = node.ancestor_of_type(*STATEMENTS) or node
    first, last = owner.start[0], owner.end[0]
    if owner.type == "condition_clause" and owner.parent is not None:
        body = owner.parent.child_by_field("consequence") or owner.parent.child_by_field("body")
        if body is not None:
            last = body.start[0] if body.type in ("compound_statement", "attributed_statement") else body.end[0]
    return first, last


def lexical_names(text: str) -> list[tuple[int, str]]:
    """Return (zero-based row, syscall) for each syscall spelling in raw text, after the lexer blanks it."""
    joined, joins = splice(text)
    code, _ = blank(joined, blank_literals=True)
    found = []
    for match in LEXICAL.finditer(code):
        name = match.group("global") or match.group("bare")
        if (match.group("global") and name in SYSCALLS) or (match.group("bare") and name in KERNEL_ONLY):
            found.append((line_of(joined, joins, match.start()) - 1, name))
    return found


def key_of(line: str) -> str:
    """Return the content key of a source line: the text before its first //, trimmed."""
    return line.split("//", 1)[0].strip()


def scan(root: Path) -> tuple[list[Site], list[str]]:
    """Find every unmarked syscall site under a scan root.

    Complexity: linear in the total size of the files in scope.

    Args:
        root: The scan root

    Returns:
        The unmarked sites, and each parse failure
    """
    sites: list[Site] = []
    failures: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        source = tree.source.decode("utf-8", "replace")
        lines = source.split("\n")
        # row -> syscall names, and row -> the rows its marker may sit on
        hits: dict[int, set[str]] = {}
        spans: dict[int, list[tuple[int, int]]] = {}
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
                continue
            for row, name in lexical_names(source):
                hits.setdefault(row, set()).add(name)
                spans.setdefault(row, []).append((row, row))
        else:
            for call in tree.find("call_expression"):
                name = callee_name(call)
                if name is None:
                    continue
                callee = call.child_by_field("function")
                row = callee.end[0] if callee is not None else call.start[0]
                if name == "syscall":
                    arguments = call.child_by_field("arguments")
                    named = SYS_NAME.findall(arguments.text) if arguments is not None else []
                    hits.setdefault(row, set()).update(named or ["syscall"])
                else:
                    hits.setdefault(row, set()).add(name)
                spans.setdefault(row, []).append(statement_rows(call))
            for body in tree.find("preproc_arg"):
                for row, name in lexical_names(body.text):
                    hits.setdefault(body.start[0] + row, set()).add(name)
                    spans.setdefault(body.start[0] + row, []).append((body.start[0], body.end[0]))
        comments: dict[int, list[str]] = {}
        blanked, _ = blank(source)
        for match in re.finditer(r"//[^\n]*|/\*.*?\*/", source, re.S):
            # A match inside a literal is not a comment: the lexer blanked it.
            if blanked[match.start()] == " ":
                comments.setdefault(source.count("\n", 0, match.start()), []).append(match.group(0))
        for row in sorted(hits):
            marked = any(MARKER.search(text) for first, last in spans[row]
                         for probe in range(first, last + 1) for text in comments.get(probe, ()))
            if not marked:
                sites.append(Site(rel, row + 1, f"{rel}:{key_of(lines[row])}", frozenset(hits[row])))
    return sites, failures


def allowlist_keys(path: Path) -> list[str]:
    """Return the key of each allowlist entry: the text before the em dash, trimmed."""
    if not path.is_file():
        return []
    keys = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            key = stripped.split("—", 1)[0].rstrip()
            if key:
                keys.append(key)
    return keys


def check(root: Path, emit: bool = False) -> int:
    """Run the scan and report, or print a template entry for each unlisted site.

    Args:
        root: The scan root
        emit: True to print templates instead of a verdict

    Returns:
        0 clean, 1 on a violation or a parse failure, 2 on a stale entry
    """
    sites, failures = scan(root)
    keys = allowlist_keys(root / ALLOWLIST)
    admitted = set(keys)
    unlisted = [site for site in sites if site.key not in admitted]
    if emit:
        names: dict[str, set[str]] = {}
        for site in unlisted:
            names.setdefault(site.key, set()).update(site.names)
        for key, called in sorted(names.items()):
            print(f"{key}  — effects::? via ? ({' '.join(sorted(called))})")
        return 0
    for failure in failures:
        print(f"SYSCALL-CAP parse failure: {failure}", file=sys.stderr)
    for site in unlisted:
        print(f"SYSCALL-CAP violation: {site.path}:{site.line} — bare Linux syscall site missing effects::* "
              f"capability admission.  Allowlist key: {site.key}", file=sys.stderr)
    if unlisted or failures:
        print(f"check-syscall-capability: {len(unlisted)} site(s) with no marker and no entry.  Route the call "
              "through a §XXI mint, mark its statement with `// SYSCALL-CAP-OK: <reason>`, or add the printed key "
              f"to {ALLOWLIST} followed by `  — <effects::* capability claim>`.  --emit-keys prints templates.",
              file=sys.stderr)
        return 1
    live = {site.key for site in sites}
    stale = [key for key in keys if key not in live]
    for key in stale:
        print(f"SYSCALL-CAP stale: {key} — no live syscall call with this text in the file; remove the entry.",
              file=sys.stderr)
    if stale:
        return 2
    print("check-syscall-capability: clean — each syscall site is marked or listed, and no entry is stale.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each site shape, each exemption and each non-site, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = (
        "#include <sys/socket.h>\n"                                                        # 1
        "inline int drift() { return ::socket(1, 0, 0); }\n"                               # 2
        "inline int admitted() { return ::socket(2, 0, 0); }\n"                            # 3
        "inline int marked() { return ::socket(3, 0, 0); }  // SYSCALL-CAP-OK: fixture\n"  # 4
        "inline int wrapped() {\n"                                                         # 5
        "    return ::setsockopt(1, 2, 3,\n"                                               # 6
        "                        nullptr, 0);  // SYSCALL-CAP-OK: marker on a later line\n"  # 7
        "}\n"                                                                              # 8
        "inline int unqualified() { return sched_getaffinity(0, 0, nullptr); }\n"          # 9
        "inline int spanning() { return ::socket\n"                                        # 10
        "    (4, 0, 0); }\n"                                                               # 11
        "inline long raw() { return ::syscall(SYS_gettid); }\n"                            # 12
        "#define OPEN_IN_MACRO(p) ::openat(0, p, 0)\n"                                     # 13
        "// return ::socket(5, 0, 0);\n"                                                   # 14
        "/* return ::socket(6, 0, 0); */\n"                                                # 15
        'inline const char* text = "::socket(7, 0, 0)";\n'                                 # 16
        'inline const char* r = R"x(::socket(8, 0, 0))x";\n'                               # 17
        "struct Handle { int fd = -1; void close() {} void done() { close(); } };\n"      # 18
        "struct Decl { int ioctl(unsigned long) const noexcept; };\n"                     # 19
        "inline int member(Decl& d) { return d.ioctl(0); }\n"                              # 20
        "inline int scoped() { return ns::ioctl(0); }\n"                                   # 21
        "inline int bare_marker() { return ::socket(9, 0, 0); }  // SYSCALL-CAP-OK:\n"     # 22
        "inline int next_statement() { ::socket(10, 0, 0);\n"                              # 23
        "    int x = 0;  // SYSCALL-CAP-OK: a later statement's marker\n"                  # 24
        "    return x; }\n"                                                                # 25
        "inline int guarded() { if (::socket(12, 0, 0) < 0)\n"                             # 26
        "    [[unlikely]] {  // SYSCALL-CAP-OK: marker on the body's opening line\n"       # 27
        "    return 1; } return 0; }\n"                                                    # 28
        "inline int deep() { if (::socket(13, 0, 0) < 0) {\n"                              # 29
        "    int y = 1;\n"                                                                 # 30
        "    return y;  // SYSCALL-CAP-OK: a marker deeper in the body\n"                  # 31
        "  } return 0; }\n"                                                                # 32
        "inline bool probe() { if (::faccessat(0, nullptr, 0, 0) == 0)\n"                  # 33
        "    return true;  // SYSCALL-CAP-OK: marker on a one-statement body\n"            # 34
        "  return false; }\n"                                                              # 35
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "src/planted").mkdir(parents=True)
        (root / "scripts").mkdir()
        source = root / "src/planted/Sys.cpp"
        source.write_text(planted, encoding="utf-8")
        (root / "src/test").mkdir()
        (root / "src/test/Out.cpp").write_text("int f() { return ::socket(11, 0, 0); }\n", encoding="utf-8")
        allow = root / ALLOWLIST
        allow.write_text("# fixture\nsrc/planted/Sys.cpp:inline int admitted() { return ::socket(2, 0, 0); }  "
                         "— effects::Init proof (fixture)\n", encoding="utf-8")
        sites, broken = scan(root)
        lines = {site.line for site in sites if site.path == "src/planted/Sys.cpp"}
        for line, label in ((2, "a global-scope call"), (9, "an unqualified kernel-only call"),
                            (10, "a call whose name and arguments span lines"), (12, "a raw syscall"),
                            (13, "a call in a macro body"), (22, "a marker with no reason"),
                            (23, "a marker on a later statement"), (29, "a marker deeper in an if body")):
            expect(f"caught: {label}", line in lines)
        for line, label in ((4, "a marked call"), (6, "a call whose marker sits on its statement's last line"),
                            (14, "a line comment"), (15, "a block comment"), (16, "a string literal"),
                            (17, "a raw string"), (18, "a member function with a syscall's name"),
                            (19, "a declaration"), (20, "a member call"), (21, "a call through a namespace"),
                            (26, "a condition call marked on its body's opening line"),
                            (33, "a condition call marked on its one-statement body")):
            expect(f"not caught: {label}", line not in lines, True)
        expect("a directory named test is out of scope", all(s.path != "src/test/Out.cpp" for s in sites), True)
        expect("the raw syscall names its SYS_ number",
               any(s.line == 12 and s.names == frozenset({"gettid"}) for s in sites))
        expect("the planted file parses", not broken)
        with contextlib.redirect_stderr(io.StringIO()) as report:
            code = check(root)
        expect("the check fails on the unlisted sites", code == 1)
        expect("an allowlisted site is admitted", "socket(2, 0, 0)" not in report.getvalue(), True)
        expect("the violation prints its content key",
               "Allowlist key: src/planted/Sys.cpp:inline int drift() { return ::socket(1, 0, 0); }"
               in report.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as emitted:
            check(root, emit=True)
        expect("--emit-keys names the syscall of each unlisted site",
               "src/planted/Sys.cpp:inline long raw() { return ::syscall(SYS_gettid); }  — effects::? via ? (gettid)"
               in emitted.getvalue())
        expect("--emit-keys skips an admitted site", "socket(2, 0, 0)" not in emitted.getvalue(), True)

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    result = check(root)
            finally:
                os.chdir(previous)
            return result, buffer.getvalue()

        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        source.write_text("\n\ninline int drift() { return ::socket(1, 0, 0); }\n", encoding="utf-8")
        allow.write_text("# fixture\nsrc/planted/Sys.cpp:inline int drift() { return ::socket(1, 0, 0); }  "
                         "— effects::Init proof (fixture)\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a content key survives a line shift", check(root) == 0)
        allow.write_text(allow.read_text() + "src/planted/Sys.cpp:return ::socket(99, 0, 0);  — stale\n",
                         encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a stale entry exits 2", check(root) == 2, True)
        (root / "src/planted/Broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a file the parser cannot read fails the check", check(root) == 1, True)
    if failures:
        print(f"check-syscall-capability --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-syscall-capability --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check, the key templates or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"], ["--emit-keys"]):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        if argv == ["--self-test"]:
            return self_test()
        return check(tsast.REPO_ROOT, emit=argv == ["--emit-keys"])
    except tsast.KitMissing as exc:
        print(f"check-syscall-capability: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
