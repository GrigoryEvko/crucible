#!/usr/bin/env python3
"""check-proof-routes.py: refuse the source shapes that build a proof type without its door.

The rule
--------
A proof type (a context, a key, a capability, a permission, a pin, a
reading, a placement) is built only by the door that its header names.
The language refuses most other routes: a constructor is private, the
class is not trivially copyable, and it is not an implicit-lifetime type.
The shapes below are legal C++ and pass each of those refusals, so this
guard refuses them in the source text instead:

  union                          a union may hold any object type, and the
                                 address of a member that is not active
                                 points at an object whose lifetime never
                                 started.  Each union definition in scope,
                                 a union template too, needs a reviewed
                                 entry that says it holds no proof type.
  pointer-cast                   a static_cast, reinterpret_cast or
                                 bit_cast to a pointer or a reference whose
                                 target names a proof type.  A void
                                 pointer from std::malloc, ::operator new,
                                 an arena or a buffer becomes a pointer to
                                 a proof with no constructor call.
  allocator                      std::allocator, allocator_traits or
                                 polymorphic_allocator of a proof type.
                                 allocate gives a typed pointer with no
                                 object in it.
  raw-allocation                 ::operator new, malloc, calloc, realloc
                                 or aligned_alloc with the size of a proof
                                 type.
  explicit-instantiation         an explicit instantiation turns off the
                                 access check for the names in it, so a
                                 pointer to a private member of a proof type
                                 can escape through one.
  function-specialization        an explicit specialization of a function
                                 template is a friend of each class that
                                 befriends the template, so a
                                 specialization of a door builds the proof
                                 with no call to the kernel.
  member-pointer-specialization  an explicit specialization turns off the
                                 access check for its template arguments,
                                 so a pointer to a private member escapes
                                 through one.
  friend-template-specialization a specialization of a class template
                                 that a proof type befriends is a friend of
                                 that proof type.  The file that defines the
                                 primary template may specialize it.

The proof names
---------------
The pointer-cast, allocator and raw-allocation rules need the names of
the proof types.  They come from two sources, and the guard uses their
union.  scripts/witness-roster.txt names each type that attests to a fact
it cannot see, and check-witness-roster.sh refuses a door that the roster
does not name.  test_forgeable_proofs --proof-names prints each class that
the reflection walk of that test finds to have the shape of a proof, and
each template in its witness list: one line each, the name that a source
spells, a tab, and the qualified name.  The guard matches the spelled
name, and a report gives the qualified names that the spelling can refer
to.  A type alias or a typedef whose definition names a proof type becomes
a proof name as well, in each file.

An identifier that the template parameter list of an enclosing template
declaration binds names that parameter, not a proof type of the same
spelling, so it is not a proof name inside that declaration.  The scope of
a parameter list ends at the first `;` or `}` that closes the declaration.
A `requires { ... }` clause before the body can end it early, which fails
closed: a name after it is matched again.

Two passes
----------
The lexical pass reads each C and C++ source file that git tracks, and
each untracked file that .gitignore does not exclude, after
scripts/cxx_lex.py blanks the comments and the literals.  The preprocessed
pass runs with --compile-db: it runs each entry of the compile database
through the preprocessor with the flags of the build, so a shape that a
macro or token pasting forms is seen too.  The output comes from the shared
store of scripts/preprocessed.py, so this guard and check-start-lifetime.sh
preprocess each translation unit one time between them.  The records of a
file stay in proof-routes-cache/ beside the compile database, under the key
of the file's chunk list, so a header that many units expand the same way
is read one time.  For each key the count is the larger count of the two
passes.  A preprocessor failure refuses the run.

Out of scope, stated rather than implied
----------------------------------------
- A negative-compile fixture, a file under a test directory named neg or
  *_neg.  It must fail to compile, so it builds nothing.
- The BPF programs under include/crucible/*/bpf/ and each *.bpf.c file.
  They are C for the BPF target, and no C++ header reaches them.
- A route through a template parameter.  `static_cast<T*>(buffer)` in an
  arena or a container names T, not a proof type, and the guard cannot know
  which argument reaches T.  A parameter spelled like a proof type is the
  same route.  test/fixy/test_forgeable_proofs.cpp pins this route with its
  argument.
- A template parameter list that a lambda opens with no `template` keyword.
  Its names stay proof names, so the guard fails closed there.
- A pointer value copied with std::memcpy into a variable whose type is a
  pointer to a proof type.  The copy names no proof type at the call.  The
  same ledger pins it.

The allowlist
-------------
scripts/proof-routes-allowlist.txt admits a reviewed site.  An entry is
`path:rule:key`, or `path:rule:key xN` for N sites, and a comment above
each paragraph gives the reason.  An entry above the count that the tree
has, or an entry for a key that the tree does not have, is stale.

Exit codes
  0  each site in scope has an entry, and each entry admits its sites
  1  a site with no entry, or more sites than its entry admits
  2  a stale entry, a preprocessor failure, a bad invocation, or a failed
     self-test

Usage
  check-proof-routes.py [--compile-db PATH] [--proof-names-binary PATH]
  check-proof-routes.py ... --list          print each admitted site
  check-proof-routes.py --self-test         plant each route and prove the verdicts
"""

import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cxx_lex import blank, line_of, splice  # noqa: E402
from preprocessed import Store, files_of, joined  # noqa: E402

SOURCE_SUFFIXES = frozenset({".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".inl", ".ipp", ".tpp",
                             ".tcc", ".inc", ".ixx", ".cppm"})
FIXTURE = re.compile(r"^test/(?:[^/]+/)*(?:neg|[^/]+_neg)/[^/]+$")
BPF = re.compile(r"^include/crucible/[^/]+/bpf/|\.bpf\.c$")
ENTRY = re.compile(r"^(?P<key>.*?)(?: x(?P<count>[1-9][0-9]*))?$")
# Bumped when a rule reads the text differently, so an older cache entry
# is not reused.
SCAN_VERSION = 2

UNION = re.compile(r"\bunion\s+(?:\[\[[^\]]*\]\]\s*)*(?:alignas\s*\([^)]*\)\s*)*([A-Za-z_]\w*)?\s*(?:final\s*)?\{")
CAST = re.compile(r"\b(static_cast|reinterpret_cast|bit_cast)\s*<")
ALLOCATOR = re.compile(r"\b(allocator|allocator_traits|polymorphic_allocator)\s*<")
RAW_ALLOCATION = re.compile(r"\b(operator\s+new|malloc|calloc|realloc|aligned_alloc)\s*\(")
# `template` that starts a declaration and is not followed by a template
# parameter list, `for` or a disambiguated member: an explicit instantiation.
INSTANTIATION = re.compile(r"(?:(?<=[;{}])|^|(?<=\bextern))[ \t\n]*template\b(?![ \t\n]*(?:<|for\b))", re.M)
SPECIALIZATION = re.compile(r"\btemplate\s*<\s*>")
FRIEND_TEMPLATE = re.compile(r"\btemplate\s*<[^;{}]*>\s*friend\s+(?:class|struct)\s+([A-Za-z_]\w*)\s*;")
PRIMARY = re.compile(r"\btemplate\s*<[^;{}]*?>\s*(?:class|struct)\s+(?:\[\[[^\]]*\]\]\s*)*([A-Za-z_]\w*)\s*(?:final\s*)?[:{;]")
PARTIAL = re.compile(r"\btemplate\s*<([^;{}]*?)>\s*(?:class|struct)\s+(?:\[\[[^\]]*\]\]\s*)*([A-Za-z_]\w*)\s*<")
MEMBER_POINTER = re.compile(r"&\s*(?:::)?[A-Za-z_][\w]*(?:\s*<[^;{}]*?>)?\s*::\s*[A-Za-z_~]")
ALIAS = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);|\btypedef\s+([^;]+?)\b([A-Za-z_]\w*)\s*;")
IDENT = re.compile(r"[A-Za-z_]\w*")
TEMPLATE_HEAD = re.compile(r"\btemplate\s*<")
# The first `=` of a parameter that is not part of a comparison starts its
# default argument.
DEFAULT_ARGUMENT = re.compile(r"(?<![=!<>])=(?!=)")
# Words that end a template parameter with no name, such as `class` or `int`.
UNNAMED = frozenset({"class", "typename", "struct", "template", "auto", "const", "volatile", "unsigned", "signed",
                     "int", "long", "short", "char", "bool", "double", "float", "void"})


def normalized(fragment: str) -> str:
    """Reduce white space: one space between two word characters, none elsewhere."""
    squeezed = re.sub(r"\s+", " ", fragment.strip())
    return re.sub(r" (?=\W)|(?<=\W) ", "", squeezed)


def angle_argument(code: str, open_at: int) -> tuple[str, int] | None:
    """The text of the balanced <...> that opens at the offset, and the offset after it."""
    depth = 0
    for index in range(open_at, min(len(code), open_at + 2048)):
        ch = code[index]
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return code[open_at + 1:index], index + 1
        elif ch in ";{}":
            return None
    return None


def declaration_head(code: str, start: int) -> str:
    """The text from the offset to the first ; or { outside parentheses, at most 2 KiB."""
    depth = 0
    for index in range(start, min(len(code), start + 2048)):
        ch = code[index]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch in ";{" and depth <= 0:
            return code[start:index]
    return code[start:start + 2048]


def idents(text: str) -> list[str]:
    """The distinct identifiers of the text, sorted."""
    return sorted(set(IDENT.findall(text)))


def template_parameter_names(head: str) -> frozenset[str]:
    """The names that one template parameter list binds, such as T and N in `class T, int N = 4`."""
    parameters: list[str] = []
    depth = 0
    current: list[str] = []
    for ch in head:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            parameters.append("".join(current))
            current = []
        else:
            current.append(ch)
    parameters.append("".join(current))
    names = set()
    for parameter in parameters:
        words = IDENT.findall(DEFAULT_ARGUMENT.split(parameter, 1)[0])
        if words and words[-1] not in UNNAMED:
            names.add(words[-1])
    return frozenset(names)


def declaration_end(code: str, start: int) -> int:
    """The offset after the `;` or the `}` that closes the declaration that starts at the offset.

    A brace inside parentheses, such as a default argument T{}, does not
    close it.  Complexity: linear in the length of the declaration."""
    parens = 0
    braces = 0
    for index in range(start, len(code)):
        ch = code[index]
        if ch in "([":
            parens += 1
        elif ch in ")]":
            parens -= 1
        elif ch == "{":
            braces += 1
        elif ch == "}":
            braces -= 1
            if braces <= 0 and parens <= 0:
                return index + 1
        elif ch == ";" and braces <= 0 and parens <= 0:
            return index + 1
    return len(code)


def template_scopes(code: str) -> list[tuple[int, int, frozenset[str]]]:
    """Each template parameter list of the code, as (start, end, names) of the declaration it opens.

    Complexity: linear in the length of the code times the depth of the
    nested templates."""
    scopes = []
    for m in TEMPLATE_HEAD.finditer(code):
        found = angle_argument(code, m.end() - 1)
        if found is None:
            continue
        head, after = found
        names = template_parameter_names(head)
        if names:
            scopes.append((after, declaration_end(code, after), names))
    return scopes


# A record is [rule, key, offset, names, subject].  names is the list of
# identifiers that the rule tests against the proof names, or None when the
# rule tests none.  subject is the class name of a class specialization, or
# None.  A record holds no proof name, so a cached record stays valid when
# the list of proof names changes.
def extract(code: str) -> list[list]:
    """Each candidate site and each alias of the code.  Complexity: linear in the length of the code."""
    records: list[list] = []
    for m in ALIAS.finditer(code):
        alias = m.group(1) or m.group(4)
        if alias:
            records.append(["alias", alias, m.start(), idents(m.group(2) or m.group(3)), None])
    for m in UNION.finditer(code):
        records.append(["union", m.group(1) or "<anonymous>", m.start(), None, None])
    for m in CAST.finditer(code):
        found = angle_argument(code, m.end() - 1)
        if found is None:
            continue
        target, after = found
        # A cast to a function pointer selects an overload, and a cast of
        # nullptr gives a null pointer.  Neither gives a pointer to storage.
        is_nullptr = re.match(r"\s*\(\s*nullptr\s*\)", code[after:after + 64]) is not None
        is_function_pointer = re.search(r"\(\s*[*&]\s*\)", target) is not None
        if ("*" in target or "&" in target) and not is_nullptr and not is_function_pointer:
            records.append(["pointer-cast", f"{m.group(1)}<{normalized(target)}>", m.start(), idents(target), None])
    for m in ALLOCATOR.finditer(code):
        found = angle_argument(code, m.end() - 1)
        if found is not None:
            records.append(["allocator", f"{m.group(1)}<{normalized(found[0])}>", m.start(), idents(found[0]), None])
    for m in RAW_ALLOCATION.finditer(code):
        sizes = re.findall(r"\bsizeof\s*\(([^()]*)\)", declaration_head(code, m.end()))
        if sizes:
            records.append(["raw-allocation", normalized(m.group(1)), m.start(), idents(" ".join(sizes)), None])
    for m in INSTANTIATION.finditer(code):
        head = declaration_head(code, m.end())
        records.append(["explicit-instantiation", normalized(head)[:160], m.start(), None, None])
    for m in SPECIALIZATION.finditer(code):
        head = normalized(declaration_head(code, m.end()))
        kind = re.match(r"(?:\[\[[^\]]*\]\])*(class|struct|union)\b\s*(?:\[\[[^\]]*\]\])*([A-Za-z_][\w:]*)?", head)
        if MEMBER_POINTER.search(head):
            records.append(["member-pointer-specialization", head[:160], m.start(), None, None])
        if kind is not None:
            subject = (kind.group(2) or "").rsplit("::", 1)[-1]
            records.append(["friend-template-specialization", head[:160], m.start(), None, subject])
        elif "(" in head.split("=", 1)[0]:
            records.append(["function-specialization", head.split("(", 1)[0][:160], m.start(), None, None])
    for m in PARTIAL.finditer(code):
        if m.group(1).strip():
            records.append(["friend-template-specialization", normalized(m.group(0))[:160], m.start(), None,
                            m.group(2)])
    # A name that an enclosing template parameter list binds is that
    # parameter, so it is not a proof name at the site.
    scopes = template_scopes(code)
    for record in records:
        if record[3] is None:
            continue
        bound = {name for start, end, names in scopes if start <= record[2] < end for name in names}
        if bound:
            record[3] = [name for name in record[3] if name not in bound]
    return records


def judge(records: list[list], proofs: frozenset[str], friends: frozenset[str], primaries: frozenset[str]):
    """The sites among the records, as (rule, key, offset), for these proof and friend names.

    An alias whose definition names a proof name is a proof name in the
    file, to a fixed point.  Complexity: quadratic in the number of aliases
    of the file, linear in the number of records."""
    local = set(proofs)
    aliases = [(r[1], r[3]) for r in records if r[0] == "alias"]
    grew = True
    while grew:
        grew = False
        for alias, names in aliases:
            if alias not in local and any(name in local for name in names):
                local.add(alias)
                grew = True
    sites: list[tuple[str, str, int]] = []
    for rule, key, offset, names, subject in records:
        if rule == "alias":
            continue
        if names is not None and not any(name in local for name in names):
            continue
        if rule == "friend-template-specialization" and (subject not in friends or subject in primaries):
            continue
        sites.append((rule, key, offset))
    return sites


def scan_code(code: str, proofs: frozenset[str], friends: frozenset[str], primaries: frozenset[str]):
    """Each site of the code as (rule, key, offset).  Complexity: linear in the length of the code."""
    return judge(extract(code), proofs, friends, primaries)


def blanked(text: str) -> tuple[str, str, list[int]]:
    """The spliced text, the text with comments and literals blanked, and the splice offsets."""
    joined, joins = splice(text)
    code, _ = blank(joined, blank_literals=True)
    return joined, code, joins


def in_scope(path: str) -> bool:
    """True when the guard reads the file: a source outside the fixtures and the BPF programs."""
    return Path(path).suffix in SOURCE_SUFFIXES and not FIXTURE.match(path) and not BPF.search(path)


def listed_files(root: Path) -> list[str]:
    """The files git lists under the root, or every file under it outside a work tree."""
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             check=True, capture_output=True).stdout
        return [p for p in out.decode(errors="replace").split("\0") if p and (root / p).is_file()]
    except (OSError, subprocess.CalledProcessError):
        found = []
        for directory, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs if not d.startswith((".git", "build"))]
            found += [str((Path(directory) / n).relative_to(root)) for n in names]
        return found


def roster_names(root: Path) -> dict[str, set[str]]:
    """The class name of each line of the witness roster, closed or open, with the name the roster writes."""
    names: dict[str, set[str]] = defaultdict(set)
    roster = root / "scripts" / "witness-roster.txt"
    if roster.is_file():
        for raw in roster.read_text().splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "|" in line:
                written = line.split("|", 1)[0].strip()
                names[written.split("<", 1)[0].rsplit("::", 1)[-1]].add(written)
    return names


def reflected_names(binary: str | None) -> dict[str, set[str]]:
    """The names that test_forgeable_proofs --proof-names prints, each with its qualified names.

    Each line is the spelled name, a tab and the qualified name."""
    names: dict[str, set[str]] = defaultdict(set)
    if not binary:
        return names
    result = subprocess.run([binary, "--proof-names"], capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        spelled, _, qualified = line.strip().partition("\t")
        if spelled:
            names[spelled].add(qualified or spelled)
    return names


def qualified_names(key: str, qualified: dict[str, set[str]]) -> str:
    """The qualified names of the proof names that a key spells, for a report."""
    found = [f"{name} = {' or '.join(sorted(qualified[name]))}" for name in idents(key) if name in qualified]
    return f"  The key names {', and '.join(found)}." if found else ""


def friend_and_primary_names(root: Path, files: list[str]) -> tuple[frozenset[str], dict[str, set[str]]]:
    """The class templates that a class under include/ befriends, and the files that define each primary."""
    friends: set[str] = set()
    primaries: dict[str, set[str]] = defaultdict(set)
    for path in files:
        if not path.startswith("include/") or not in_scope(path):
            continue
        _, code, _ = blanked((root / path).read_text(errors="replace"))
        friends.update(FRIEND_TEMPLATE.findall(code))
        for m in PRIMARY.finditer(code):
            primaries[m.group(1)].add(path)
    return frozenset(friends), primaries


def preprocessed_counts(root: Path, compile_db: Path, proofs, friends, primaries, failures: list[str],
                        tracked: frozenset[str]):
    """The count of each key over the preprocessed output of each database entry, the largest over the entries.

    Only a file that the lexical pass also reads counts, so a file that the
    build generates under the root has no key that depends on the name of
    the build directory.  The records of a file name no proof type, so a
    change to the list of proof names does not invalidate their cache.

    Complexity: linear in the number of units times the files each reads,
    plus one extraction for each distinct expansion of a file."""
    store = Store(compile_db, root, int(os.environ.get("PROOF_ROUTES_JOBS", "0") or 0))
    cache_dir = compile_db.parent / "proof-routes-cache"
    cache_dir.mkdir(exist_ok=True)
    records_of: dict[str, list[list]] = {}

    def records(file_key: str, chunks) -> list[list]:
        """The records of one expansion of a file, from memory, the cache or the text."""
        if file_key in records_of:
            return records_of[file_key]
        cache_file = cache_dir / f"{SCAN_VERSION}-{file_key}.json"
        try:
            found = json.loads(cache_file.read_text())
        except (OSError, ValueError):
            code, _ = blank(joined(store, chunks), blank_literals=True)
            found = extract(code)
            staging = cache_file.with_suffix(f".{os.getpid()}.tmp")
            staging.write_text(json.dumps(found))
            os.replace(staging, cache_file)
        records_of[file_key] = found
        return found

    merged: dict[str, int] = defaultdict(int)
    for unit in store.units():
        if unit.failure is not None:
            failures.append(unit.failure)
            continue
        for path, (file_key, chunks) in files_of(unit).items():
            if path not in tracked or not in_scope(path):
                continue
            counts: dict[str, int] = defaultdict(int)
            for rule, key, _ in judge(records(file_key, chunks), proofs, friends,
                                      frozenset(primaries_for(path, primaries))):
                counts[f"{path}:{rule}:{key}"] += 1
            for key, count in counts.items():
                merged[key] = max(merged[key], count)
    return merged


def primaries_for(path: str, primaries: dict[str, set[str]]) -> set[str]:
    """The class templates whose primary definition is in the file, which may specialize them."""
    return {name for name, files in primaries.items() if path in files}


def read_allowlist(path: Path) -> tuple[dict[str, tuple[int, int]], list[str]]:
    """Each entry key, the count it admits and its line, and each malformed entry."""
    entries: dict[str, tuple[int, int]] = {}
    errors: list[str] = []
    if not path.is_file():
        return entries, errors
    paragraph_has_comment = False
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line:
            paragraph_has_comment = False
            continue
        if line.startswith("#"):
            paragraph_has_comment = True
            continue
        match = ENTRY.match(line)
        key = match.group("key")
        if key.count(":") < 2:
            errors.append(f"{path.name}:{number}: `{line}` is not path:rule:key")
        if not paragraph_has_comment:
            errors.append(f"{path.name}:{number}: `{line}` has no comment above its paragraph")
        entries[key] = (int(match.group("count") or 1), number)
    return entries, errors


def scan(root: Path, compile_db: Path | None, binary: str | None, allowlist: Path, mode: str) -> int:
    """Scan the tree under the root and compare each site with the allowlist."""
    files = listed_files(root)
    qualified: dict[str, set[str]] = defaultdict(set)
    for source in (roster_names(root), reflected_names(binary)):
        for name, spellings in source.items():
            qualified[name] |= spellings
    proofs = frozenset(qualified)
    friends, primaries = friend_and_primary_names(root, files)
    lexical: dict[str, list[int]] = defaultdict(list)
    for path in files:
        if not in_scope(path):
            continue
        joined, code, joins = blanked((root / path).read_text(errors="replace"))
        admitted = frozenset(primaries_for(path, primaries))
        for rule, key, offset in scan_code(code, proofs, friends, admitted):
            lexical[f"{path}:{rule}:{key}"].append(line_of(joined, joins, offset))
    failures: list[str] = []
    preprocessed: dict[str, int] = {}
    if compile_db is not None:
        preprocessed = preprocessed_counts(root, compile_db, proofs, friends, primaries, failures, frozenset(files))
    entries, errors = read_allowlist(allowlist)
    unreviewed = 0
    for key in sorted(set(lexical) | set(preprocessed)):
        count = max(len(lexical.get(key, [])), preprocessed.get(key, 0))
        admitted = entries.get(key, (0, 0))[0]
        lines = ", ".join(map(str, lexical.get(key, []))) or "a macro expansion"
        if count > admitted:
            unreviewed += 1
            print(f"PROOF-ROUTE violation: {key} at line(s) {lines}: {count} site(s), {admitted} admitted.  "
                  f"Remove the route, or add a reviewed entry `{key}{' x' + str(count) if count > 1 else ''}` "
                  f"to {allowlist.name}.{qualified_names(key.split(':', 2)[-1], qualified)}", file=sys.stderr)
        elif mode == "list":
            print(f"REVIEWED  {key}  ({count} site(s), line(s) {lines})")
    stale = 0
    for key, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        found = max(len(lexical.get(key, [])), preprocessed.get(key, 0))
        if found < admitted:
            stale += 1
            print(f"PROOF-ROUTE stale: {allowlist.name}:{number} admits {admitted} site(s) of {key}, and the tree "
                  f"has {found}.  The list only shrinks: lower the count or delete the entry.", file=sys.stderr)
    for error in errors:
        print(f"PROOF-ROUTE malformed: {error}", file=sys.stderr)
    for failure in failures:
        print(f"PROOF-ROUTE preprocessor failure: {failure}.  A translation unit the guard cannot read can hold "
              f"a route, so the run is refused.", file=sys.stderr)
    print(f"check-proof-routes: {len(proofs)} proof name(s), {len(friends)} befriended template(s), "
          f"{len(set(lexical) | set(preprocessed))} key(s), {unreviewed} unreviewed, {stale} stale, "
          f"{len(errors)} malformed, {len(failures)} preprocessor failure(s).", file=sys.stderr)
    if unreviewed:
        return 1
    return 2 if stale or errors or failures else 0


SELF_TEST_SOURCE = r"""
#include <memory>
namespace forge {
class Door;
template <class T> class Guarded { template <class U> friend class Opener; };
union Loose { unsigned char byte; Door* door; };
template <class P> union Held { unsigned char byte; P proof; };
using Alias = Door;
void route(void* raw) {
    auto* first = static_cast<Door*>(raw);
    auto* second = static_cast<Alias*>(raw);
    auto& third = *reinterpret_cast<const Door*>(raw);
    std::allocator<Door> pool;
    void* bytes = ::operator new(sizeof(Door));
    (void)first; (void)second; (void)third; (void)pool; (void)bytes;
}
template class Guarded<int>;
template <> void route_twice<Door>(void*);
template <auto M> struct Steal {};
template <> struct Steal<&Door::secret> {};
template <> class Opener<int> {};
template <class U> class Opener<U*> {};
// union Commented { int a; };
const char* text = "union InString { int a; };";
void fine(int* p) { auto* q = static_cast<long*>(static_cast<void*>(p)); (void)q; x.template get<0>(); }
void also_fine() { auto* n = static_cast<Door*>(nullptr); auto f = static_cast<Door (*)(int)>(&make); }
template <class Door> Door* rebind(void* raw) { return static_cast<Door*>(raw); }
Door* after_template(void* raw) { return static_cast<Door*>(raw); }
template <class T, class Door = T> struct Box { Door* get(void* raw) { return static_cast<Door*>(raw); } };
template <class Door> concept Castable = requires(void* raw) { static_cast<Door*>(raw); };
template <template <class> class Door> void* hold(void* raw) { return static_cast<Door<int>*>(raw); }
template <class Door>
void* grab() { return ::operator new(sizeof(Door)); }
}
"""


def self_test() -> int:
    """Plant each route in a scratch tree and prove the verdicts."""
    failures: list[str] = []
    proofs = frozenset({"Door"})
    friends = frozenset({"Opener"})
    joined, code, _ = blanked(SELF_TEST_SOURCE)
    sites = scan_code(code, proofs, friends, frozenset())
    by_rule: dict[str, list[str]] = defaultdict(list)
    for rule, key, _ in sites:
        by_rule[rule].append(key)
    expected = {
        "union": ["Loose", "Held"],
        # The fourth is the cast after the template: a parameter named Door
        # binds only inside its own declaration.
        "pointer-cast": ["static_cast<Door*>", "static_cast<Alias*>", "reinterpret_cast<const Door*>",
                         "static_cast<Door*>"],
        "allocator": ["allocator<Door>"],
        "raw-allocation": ["operator new"],
        "explicit-instantiation": ["class Guarded<int>"],
        "function-specialization": ["void route_twice<Door>"],
        "member-pointer-specialization": ["struct Steal<&Door::secret>"],
        "friend-template-specialization": ["class Opener<int>", "template<class U>class Opener<"],
    }
    for rule, keys in expected.items():
        if sorted(by_rule.get(rule, [])) != sorted(keys):
            failures.append(f"rule {rule}: found {sorted(by_rule.get(rule, []))}, expected {sorted(keys)}")
    unexpected = set(by_rule) - set(expected)
    if unexpected:
        failures.append(f"a rule fired that the plant does not hold: {sorted(unexpected)}")
    # The defining file of a primary may specialize it.
    if any(r == "friend-template-specialization" for r, _, _ in scan_code(code, proofs, friends, frozenset({"Opener"}))):
        failures.append("a specialization in the file that defines the primary template was refused")
    # The allowlist admits a site, refuses a surplus and reports a stale entry.
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "scripts").mkdir()
        (root / "scripts" / "witness-roster.txt").write_text("forge::Door | h | forge::Door{} | private | closed\n")
        (root / "a.cpp").write_text("union Plain { int a; };\nunion Other { int b; };\n")
        allow = root / "scripts" / "allow.txt"
        allow.write_text("# plain scalars\na.cpp:union:Plain\n")
        if scan(root, None, None, allow, "check") != 1:
            failures.append("a union with no entry did not exit 1")
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\na.cpp:union:Gone\n")
        if scan(root, None, None, allow, "check") != 2:
            failures.append("a stale entry did not exit 2")
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\n")
        if scan(root, None, None, allow, "check") != 0:
            failures.append("a tree whose sites all have entries did not exit 0")
        allow.write_text("a.cpp:union:Plain\na.cpp:union:Other\n")
        if scan(root, None, None, allow, "check") != 2:
            failures.append("an entry with no comment above it was accepted")
        # A report names the qualified class that the dump gives for a
        # spelled name, and the class that the roster writes.
        (root / "b.cpp").write_text("void f(void* raw) { auto* key = static_cast<Key*>(raw); (void)key; }\n"
                                    "void g(void* raw) { auto* door = static_cast<Door*>(raw); (void)door; }\n")
        dump = root / "proof-names.sh"
        dump.write_text("#!/bin/sh\nprintf 'Key\\t::forge::detail::Key\\n'\n")
        dump.chmod(0o755)
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\n")
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, str(dump), allow, "check")
        for needle in ("b.cpp:pointer-cast:static_cast<Key*>", "The key names Key = ::forge::detail::Key.",
                       "b.cpp:pointer-cast:static_cast<Door*>", "The key names Door = forge::Door."):
            if verdict != 1 or needle not in report.getvalue():
                failures.append(f"the report of a cast to a proof type lacks `{needle}`:\n{report.getvalue()}")
        (root / "b.cpp").unlink()
        # Token pasting forms a union that the lexical pass cannot see, and
        # the preprocessed pass sees it.  A second run reads the cache.
        compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
        if compiler is None:
            failures.append("no C++ compiler to run the preprocessed pass")
        else:
            (root / "a.cpp").write_text("#define JOIN(a, b) a##b\nJOIN(uni, on) Hidden { int x; };\n")
            database = root / "compile_commands.json"
            database.write_text(json.dumps([{"directory": str(root), "file": "a.cpp",
                                             "command": f"{compiler} -std=c++20 -c a.cpp -o a.o"}]))
            allow.write_text("# none\n")
            for attempt in ("cold", "cached"):
                report = subprocess.run([sys.executable, __file__, "--scan-root", str(root), "--compile-db",
                                         str(database), "--allowlist", str(allow)], capture_output=True, text=True)
                if report.returncode != 1 or "a.cpp:union:Hidden" not in report.stderr:
                    failures.append(f"the {attempt} preprocessed pass missed a union formed by token pasting")
            if not any((root / "proof-routes-cache").glob("*.json")):
                failures.append("the preprocessed pass wrote no cache entry")
            if not any((root / "preprocessed-cache" / "units").glob("*.json")):
                failures.append("the preprocessed pass did not use the shared store")
    for failure in failures:
        print(f"check-proof-routes: SELF-TEST FAILED: {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-proof-routes: self-test passed ({sum(len(v) for v in expected.values())} planted sites found)")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the scan or the self-test."""
    if argv == ["--self-test"]:
        return self_test()
    root = Path(__file__).resolve().parent.parent
    compile_db: Path | None = None
    binary: str | None = None
    allowlist: Path | None = None
    mode = "check"
    while argv:
        flag = argv.pop(0)
        if flag == "--compile-db" and argv:
            compile_db = Path(argv.pop(0)).resolve()
        elif flag == "--proof-names-binary" and argv:
            binary = argv.pop(0)
        elif flag == "--list":
            mode = "list"
        # The self-test scans a scratch tree with its own allowlist.
        elif flag == "--scan-root" and argv:
            root = Path(argv.pop(0)).resolve()
        elif flag == "--allowlist" and argv:
            allowlist = Path(argv.pop(0)).resolve()
        else:
            print(__doc__, file=sys.stderr)
            return 2
    return scan(root, compile_db, binary, allowlist or root / "scripts" / "proof-routes-allowlist.txt", mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
