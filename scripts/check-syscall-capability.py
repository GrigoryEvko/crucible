#!/usr/bin/env python3
"""check-syscall-capability — each Linux syscall site holds a reviewed capability claim.

A direct call of a Linux syscall, or of its thin libc wrapper, in production
code (include/, src/ and vessel/) either carries a `// SYSCALL-CAP-OK: <reason>`
comment in its statement or has an entry in
scripts/syscall-capability-allowlist.txt.  Each entry is

    path:<call text>  — <effects::* capability claim> via <function> (<note>)

where <call text> is the line that holds the syscall name, with each comment
node on it removed, trimmed (tsast.site_key).  A `//` inside a string literal
stays in the key.  The key is the content of the line, so an edit above the
call does not move it.  An entry that matches no live site is stale.

THE PROSE
    The text after the em dash is the audit surface, so the guard holds it
    to the code of the sites that the key names:
      * (B) It names a syscall that one of the sites calls.  For
        `::syscall(SYS_x, ...)` that is x, read from the call node, so the
        number can sit on a later line than the key.
      * (C) Each SHOUTING_SNAKE word in it is an identifier node or a macro
        token of that file.  A word that only a comment holds does not count.
      * (D) It has a `via <name>` clause, with more names joined by `and`.
        Each site sits in a function whose namespaces, classes and name end
        with one of the names, and each name holds a site.  Template
        arguments in a name are ignored.  A macro body counts as a function
        named after the macro.
    The guard does not judge the effects::* claim itself.  That needs the
    type system.

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

Exit 0 clean, 1 on a violation, a false prose or a parse failure, 2 on a
stale entry, a dead marker, a usage error or a failed self-test, 3 when the
kit is not installed.
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
    accept4 socketpair copy_file_range sendfile splice renameat renameat2 unlinkat linkat mkdirat fchmod fchown
    ftruncate fallocate statx memfd_create mbind set_mempolicy get_mempolicy membarrier timerfd_create
    sched_setscheduler sched_getscheduler setpriority getpriority getpid gettid
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
    accept4 socketpair copy_file_range sendfile splice renameat renameat2 unlinkat linkat mkdirat fchmod fchown
    ftruncate fallocate statx memfd_create mbind set_mempolicy get_mempolicy membarrier timerfd_create
    sched_setscheduler sched_getscheduler setpriority getpriority getpid gettid
""".split())
assert KERNEL_ONLY <= SYSCALLS, "a kernel-only name is missing from SYSCALLS"

ROOTS = ("include", "src", "vessel")
EXCLUDED = frozenset({"test", "bench", "examples", "third_party", "external", "vendor"})
SUFFIXES = (".h", ".hh", ".hpp", ".cpp", ".cc", ".cxx", ".inl")
ALLOWLIST = "scripts/syscall-capability-allowlist.txt"
MARKER_WORD = "SYSCALL-CAP-OK"
MARKER = re.compile(rf"{MARKER_WORD}:\s*\S")
LEXICAL = re.compile(r"(?<![A-Za-z_0-9])::\s*(?P<global>[A-Za-z_]\w*)\s*\("
                     r"|(?<![A-Za-z_0-9:.>\s])(?<![A-Za-z_0-9:.>])\s*(?P<bare>[A-Za-z_]\w*)\s*\(")
# The statements that bound where a marker comment counts.
STATEMENTS = frozenset({"expression_statement", "return_statement", "declaration", "field_declaration",
                        "condition_clause", "init_statement", "for_range_loop", "throw_statement"})
# The node types whose text is one name, for the SHOUTING_SNAKE check.
NAME_LEAVES = ("identifier", "field_identifier", "type_identifier", "namespace_identifier",
               "statement_identifier")
SHOUT = re.compile(r"\b[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+\b")
# One part of a C++ name as prose writes it: an operator, a destructor or a
# name, then an optional template argument list with one level of nesting.
_PART = r"(?:operator\s*(?:\(\)|\[\]|[^\s\w(]+)|~?[A-Za-z_]\w*)(?:\s*<[^<>]*(?:<[^<>]*>[^<>]*)*>)?"
_NAME = rf"(?:::)?{_PART}(?:::{_PART})*"
VIA = re.compile(rf"\bvia\s+({_NAME}(?:\s+and\s+{_NAME})*)")
TEMPLATE_ARGUMENTS = re.compile(r"<[^<>]*>")


@dataclass(frozen=True)
class Site:
    """The syscall calls on one line.

    holders holds, for each call, the namespaces, the classes and the name
    parts of the function that holds it, or () for a call in no function.
    """

    path: str
    line: int
    key: str
    names: frozenset[str]
    holders: frozenset[tuple[str, ...]]


def in_scope(rel: Path) -> bool:
    """Return True when a path relative to the scan root is production code this guard reads.

    A file of tsast.UNPARSEABLE is not C++, so it is out of scope.
    """
    return (rel.suffix in SUFFIXES and bool(rel.parts) and rel.parts[0] in ROOTS
            and rel.as_posix() not in tsast.UNPARSEABLE
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


def holder_of(node: tsast.Node) -> tuple[str, ...]:
    """Return the namespaces, classes and name parts of the function that holds a node.

    An anonymous namespace has no name that prose can write, so it is left
    out.  A node in no function gives ().
    """
    function = tsast.enclosing_function(node)
    if function is None:
        return ()
    return tuple(part for part in tsast.namespace_path(node) if part) + function


def file_words(tree: tsast.Tree) -> frozenset[str]:
    """Return each name that a file spells in code: the name nodes and the identifier tokens of its macro bodies.

    Complexity: linear in the size of the file.
    """
    words = {node.text for node in tree.find(*NAME_LEAVES)}
    for body in tree.find("preproc_arg"):
        words.update(token.text for token in tsast.pp_tokens(body.text) if token.kind == "identifier")
    return frozenset(words)


def syscall_numbers(call: tsast.Node) -> set[str]:
    """Return the x of each `SYS_x` identifier among the arguments of a call."""
    arguments = call.child_by_field("arguments")
    if arguments is None:
        return set()
    return {node.text[4:] for node in arguments.descendants("identifier") if node.text.startswith("SYS_")}


@dataclass(frozen=True)
class Marker:
    """One marker comment that sits on no syscall site."""

    path: str
    line: int


@dataclass
class Scan:
    """What one scan finds.

    words maps each file that holds an unmarked site to file_words() of it,
    for the prose check.
    """

    sites: list[Site]
    failures: list[str]
    dead: list[Marker]
    words: dict[str, frozenset[str]]


def scan(root: Path) -> Scan:
    """Find every unmarked syscall site and every dead marker under a scan root.

    Complexity: linear in the total size of the files in scope.

    Args:
        root: The scan root

    Returns:
        The unmarked sites, each parse failure, each marker that exempts no
        site, and the names that each file with a site spells
    """
    found = Scan([], [], [], {})
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            found.failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        # row -> syscall names, row -> the rows its marker may sit on, row -> the holders of its calls
        hits: dict[int, set[str]] = {}
        spans: dict[int, list[tuple[int, int]]] = {}
        holders: dict[int, set[tuple[str, ...]]] = {}
        for call in tree.find("call_expression"):
            name = callee_name(call)
            if name is None:
                continue
            callee = call.child_by_field("function")
            row = callee.end[0] if callee is not None else call.start[0]
            if name == "syscall":
                hits.setdefault(row, set()).update(syscall_numbers(call) or {"syscall"})
            else:
                hits.setdefault(row, set()).add(name)
            spans.setdefault(row, []).append(statement_rows(call))
            holders.setdefault(row, set()).add(holder_of(call))
        for body in tree.find("preproc_arg"):
            macro = body.parent.child_by_field("name") if body.parent is not None else None
            for row, name in lexical_names(body.text):
                hits.setdefault(body.start[0] + row, set()).add(name)
                spans.setdefault(body.start[0] + row, []).append((body.start[0], body.end[0]))
                holders.setdefault(body.start[0] + row, set()).add(() if macro is None else (macro.text,))
        markers: dict[int, list[tsast.Node]] = {}
        for comment in tree.find("comment"):
            if MARKER_WORD in comment.text:
                markers.setdefault(comment.start[0], []).append(comment)
        used: set[int] = set()
        before = len(found.sites)
        for row in sorted(hits):
            covering = [comment for first, last in spans[row] for probe in range(first, last + 1)
                        for comment in markers.get(probe, ())]
            used.update(comment.index for comment in covering)
            if not any(MARKER.search(comment.text) for comment in covering):
                found.sites.append(Site(rel, row + 1, f"{rel}:{tsast.site_key(tree, row)}", frozenset(hits[row]),
                                        frozenset(holders[row])))
        found.dead.extend(Marker(rel, comment.line) for row in sorted(markers) for comment in markers[row]
                          if comment.index not in used)
        if len(found.sites) > before:
            found.words[rel] = file_words(tree)
    return found


def allowlist_entries(path: Path) -> list[tuple[str, str]]:
    """Return (key, prose) for each allowlist entry: the text before the first em dash, trimmed, and the rest."""
    if not path.is_file():
        return []
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            key, _, prose = stripped.partition("—")
            if key.rstrip():
                entries.append((key.rstrip(), prose.strip()))
    return entries


def via_names(prose: str) -> list[tuple[str, ...]]:
    """Return the parts of each name that a `via` clause of the prose gives, template arguments removed."""
    names = []
    for clause in VIA.finditer(prose):
        for spelled in re.split(r"\s+and\s+", clause.group(1)):
            bare = spelled
            while TEMPLATE_ARGUMENTS.search(bare):
                bare = TEMPLATE_ARGUMENTS.sub("", bare)
            names.append(tuple(part for part in "".join(bare.split()).split("::") if part))
    return names


def ends_with(holder: tuple[str, ...], name: tuple[str, ...]) -> bool:
    """Say whether the parts of a holder end with the parts of a name."""
    return 0 < len(name) <= len(holder) and holder[-len(name):] == name


def prose_findings(entries: list[tuple[str, str]], found: Scan) -> list[str]:
    """Hold the prose of each live entry to the sites its key names.

    Complexity: O(E * S) for E entries and S sites of one key, both small.

    Args:
        entries: The (key, prose) pairs of the allowlist
        found: The scan

    Returns:
        One line for each false statement.  A stale entry gives none: the
        stale check reports it.
    """
    by_key: dict[str, list[Site]] = {}
    for site in found.sites:
        by_key.setdefault(site.key, []).append(site)
    findings: list[str] = []
    for key, prose in entries:
        live = by_key.get(key)
        if not live:
            continue
        path = live[0].path
        called = set().union(*(site.names for site in live)) - {"syscall"}
        if called and not any(re.search(rf"(?<![A-Za-z_0-9]){re.escape(name)}", prose) for name in called):
            findings.append(f"{key} — the prose names no syscall that the line calls ({' '.join(sorted(called))})")
        spelled = found.words.get(path, frozenset())
        for word in sorted(set(SHOUT.findall(prose))):
            if word not in spelled:
                findings.append(f"{key} — the prose names {word}, which is no identifier and no macro token "
                                f"in {path}")
        named = via_names(prose)
        if not named:
            findings.append(f"{key} — the prose has no `via <function>` clause")
            continue
        holders = {holder for site in live for holder in site.holders}
        for holder in sorted(holders):
            if not any(ends_with(holder, name) for name in named):
                findings.append(f"{key} — the call in {'::'.join(holder) or 'no function'} is named by no via "
                                "clause")
        for name in named:
            if not any(ends_with(holder, name) for holder in holders):
                findings.append(f"{key} — via {'::'.join(name)} holds no site that has this key")
    return findings


def check(root: Path, emit: bool = False) -> int:
    """Run the scan and report, or print a template entry for each unlisted site.

    Args:
        root: The scan root
        emit: True to print templates instead of a verdict

    Returns:
        0 clean, 1 on a violation, a false prose or a parse failure, 2 on a
        stale entry or a dead marker
    """
    found = scan(root)
    entries = allowlist_entries(root / ALLOWLIST)
    keys = [key for key, _ in entries]
    admitted = set(keys)
    unlisted = [site for site in found.sites if site.key not in admitted]
    if emit:
        names: dict[str, set[str]] = {}
        for site in unlisted:
            names.setdefault(site.key, set()).update(site.names)
        for key, called in sorted(names.items()):
            print(f"{key}  — effects::? via ? ({' '.join(sorted(called))})")
        return 0
    for failure in found.failures:
        print(f"SYSCALL-CAP parse failure: {failure}", file=sys.stderr)
    for site in unlisted:
        print(f"SYSCALL-CAP violation: {site.path}:{site.line} — bare Linux syscall site missing effects::* "
              f"capability admission.  Allowlist key: {site.key}", file=sys.stderr)
    if unlisted or found.failures:
        print(f"check-syscall-capability: {len(unlisted)} site(s) with no marker and no entry.  Route the call "
              "through a §XXI mint, mark its statement with `// SYSCALL-CAP-OK: <reason>`, or add the printed key "
              f"to {ALLOWLIST} followed by `  — <effects::* capability claim> via <function> (<note>)`.  "
              "--emit-keys prints templates.", file=sys.stderr)
        return 1
    findings = prose_findings(entries, found)
    for finding in findings:
        print(f"SYSCALL-CAP prose: {finding}", file=sys.stderr)
    if findings:
        print(f"check-syscall-capability: {len(findings)} false statement(s) in {ALLOWLIST}.  Make the prose "
              "name the syscall that the line calls, only constants that the file spells in code, and the "
              "function that holds each call.", file=sys.stderr)
        return 1
    live = {site.key for site in found.sites}
    stale = [key for key in keys if key not in live]
    for key in stale:
        print(f"SYSCALL-CAP stale: {key} — no live syscall call with this text in the file; remove the entry.",
              file=sys.stderr)
    for marker in found.dead:
        print(f"SYSCALL-CAP dead marker: {marker.path}:{marker.line} — the {MARKER_WORD} comment sits on no "
              f"syscall site, so it exempts nothing.  Delete it.", file=sys.stderr)
    if stale or found.dead:
        return 2
    print("check-syscall-capability: clean — each syscall site is marked or listed, each entry's prose agrees "
          "with its sites, and no entry is stale.", file=sys.stderr)
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
        "inline long literal() { return ::write(1, \"a//b\", 3); }  // SYSCALL-CAP-OK: // in a literal\n"  # 36
        "inline int nothing() { return 0; }  // SYSCALL-CAP-OK: a marker on no syscall\n"  # 37
        "inline int widened() { return ::renameat2(0, nullptr, 0, nullptr, 0); }\n"        # 38
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
                         "— effects::Init via admitted (socket, fixture)\n", encoding="utf-8")
        found = scan(root)
        sites, broken, dead = found.sites, found.failures, found.dead
        lines = {site.line for site in sites if site.path == "src/planted/Sys.cpp"}
        for line, label in ((2, "a global-scope call"), (9, "an unqualified kernel-only call"),
                            (10, "a call whose name and arguments span lines"), (12, "a raw syscall"),
                            (13, "a call in a macro body"), (22, "a marker with no reason"),
                            (23, "a marker on a later statement"), (29, "a marker deeper in an if body"),
                            (38, "a call of a syscall that the table gained with the dead-marker check")):
            expect(f"caught: {label}", line in lines)
        for line, label in ((4, "a marked call"), (6, "a call whose marker sits on its statement's last line"),
                            (14, "a line comment"), (15, "a block comment"), (16, "a string literal"),
                            (17, "a raw string"), (18, "a member function with a syscall's name"),
                            (19, "a declaration"), (20, "a member call"), (21, "a call through a namespace"),
                            (26, "a condition call marked on its body's opening line"),
                            (33, "a condition call marked on its one-statement body"),
                            (36, "a marked call whose line holds // in a string literal")):
            expect(f"not caught: {label}", line not in lines, True)
        dead_lines = {marker.line for marker in dead if marker.path == "src/planted/Sys.cpp"}
        expect("a marker on a statement with no syscall is dead", 37 in dead_lines)
        expect("a marker on a later statement or deeper in a body exempts nothing, so it is dead",
               {24, 31} <= dead_lines)
        expect("a marker that exempts a site is not dead", not dead_lines & {4, 7, 22, 27, 34, 36}, True)
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
                         "— effects::Init via drift (socket, fixture)\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a content key survives a line shift", check(root) == 0)
        allow.write_text(allow.read_text() + "src/planted/Sys.cpp:return ::socket(99, 0, 0);  — stale\n",
                         encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a stale entry exits 2", check(root) == 2, True)
        (root / "src/planted/Broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            expect("a file the parser cannot read fails the check", check(root) == 1, True)

    prose_file = "src/planted/Prose.cpp"
    prose_fixture = (
        "namespace crucible::planted {\n"
        "struct PlantedSock final {\n"
        "    int other() noexcept { return 0; }  // TCP_ONLY_IN_COMMENT\n"
        "    int apply(int fd) noexcept {\n"
        "        return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);\n"
        "    }\n"
        "};\n"
        "inline long split_raw(int fd) noexcept {\n"
        "    return ::syscall(\n"
        "        SYS_mlock2, fd, 0, 0);\n"
        "}\n"
        "template <int Source> struct Reader final {\n"
        "    long read() noexcept { return ::syscall(SYS_gettid); }\n"
        "};\n"
        "struct Handle final {\n"
        "    int fd_ = -1;\n"
        "    ~Handle() { (void)::close(fd_); }\n"
        "    Handle& operator=(Handle&&) noexcept { (void)::close(fd_); return *this; }\n"
        "};\n"
        "inline void first(int fd) noexcept {\n"
        "    (void)::close(fd);\n"
        "}\n"
        "inline void second(int fd) noexcept {\n"
        "    (void)::close(fd);\n"
        "}\n"
        "#define PLANTED_FLAG_WORD PLANTED_MACRO_ONLY\n"
        "}\n"
    )
    setsockopt_key = f"{prose_file}:return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);"
    split_key = f"{prose_file}:return ::syscall("
    twice_key = f"{prose_file}:(void)::close(fd);"
    honest = {
        setsockopt_key: "effects::IO via PlantedSock::apply (TCP_PLANTED_OPT setsockopt)",
        split_key: "effects::Init via split_raw (mlock2 through the raw syscall)",
        f"{prose_file}:long read() noexcept {{ return ::syscall(SYS_gettid); }}":
            "effects::Bg via Reader<Source>::read (gettid)",
        f"{prose_file}:~Handle() {{ (void)::close(fd_); }}": "effects::IO via Handle::~Handle (close on drop)",
        f"{prose_file}:Handle& operator=(Handle&&) noexcept {{ (void)::close(fd_); return *this; }}":
            "effects::IO via Handle::operator= (close the old fd)",
        twice_key: "effects::IO via first and planted::second (close, two sites, one key)",
    }
    rotten = (
        ("the honest prose passes: templates, a destructor, an operator and two holders", {}, 0, ""),
        ("a prose that names no syscall of the line is rot",
         {setsockopt_key: "effects::IO via PlantedSock::apply (ctx-bound)"}, 1, "names no syscall"),
        ("a SYS_ number on the line after the key is read",
         {split_key: "effects::Init via split_raw (raw syscall)"}, 1, "names no syscall that the line calls (mlock2)"),
        ("a constant that the file does not spell is rot",
         {setsockopt_key: "effects::IO via PlantedSock::apply (TCP_QUICKACK setsockopt)"}, 1, "TCP_QUICKACK"),
        ("a constant that only a comment spells is rot",
         {setsockopt_key: "effects::IO via PlantedSock::apply (TCP_ONLY_IN_COMMENT setsockopt)"}, 1,
         "TCP_ONLY_IN_COMMENT"),
        ("a constant that a macro body spells counts",
         {setsockopt_key: "effects::IO via PlantedSock::apply (TCP_PLANTED_OPT PLANTED_MACRO_ONLY setsockopt)"},
         0, ""),
        ("a qualified via name that does not hold the call is rot",
         {setsockopt_key: "effects::IO via PlantedSock::other (TCP_PLANTED_OPT setsockopt)"}, 1,
         "via PlantedSock::other holds no site"),
        ("a bare via name that does not hold the call is rot",
         {setsockopt_key: "effects::IO via other (TCP_PLANTED_OPT setsockopt)"}, 1,
         "crucible::planted::PlantedSock::apply is named by no via clause"),
        ("a via name of a class alone is not the function that holds the call",
         {setsockopt_key: "effects::IO via PlantedSock (TCP_PLANTED_OPT setsockopt)"}, 1, "holds no site"),
        ("a prose with no via clause is rot",
         {setsockopt_key: "effects::IO (TCP_PLANTED_OPT setsockopt)"}, 1, "no `via <function>` clause"),
        ("a key with two holders needs both names",
         {twice_key: "effects::IO via first (close)"}, 1, "crucible::planted::second is named by no via clause"),
        ("a via name that holds no site of the key is rot",
         {twice_key: "effects::IO via first and second and third (close)"}, 1, "via third holds no site"),
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "src/planted").mkdir(parents=True)
        (root / "scripts").mkdir()
        (root / prose_file).write_text(prose_fixture, encoding="utf-8")
        for label, changes, want, needle in rotten:
            rows = {**honest, **changes}
            (root / ALLOWLIST).write_text("# fixture\n" + "".join(f"{key}  — {text}\n" for key, text in rows.items()),
                                          encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()) as report:
                code = check(root)
            expect(f"prose: {label}", code == want and needle in report.getvalue(), want != 0)
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
