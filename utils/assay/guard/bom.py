"""guard bom — print the bill of materials of one artifact of a build in five layers, and write nothing.

usage: guard bom --build-dir DIR [--limit N] ARTIFACT

THE ARTIFACT
    ARTIFACT is the name of a target of the code model of the build, such as
    foundation or test_arena.  It can also be the path of the file of the
    target.  The target is a static library, an executable, a shared library
    or a module.

THE LAYERS
    The command prints five layers.  A layer that cannot be read gives one
    line with its problem, and the other layers stay:
      1. imports         each name that the artifact gets from a different
                         file.  For a static library, each import of the rule
                         artifact-symbols, with its owner, its family and its
                         mark.  Then the line of the source of the first
                         record of the library code that names the import
                         (assay.readers.sections, THE SYMBOLS OF THE LIBRARY
                         CODE).  For a linked file, each library of
                         DT_NEEDED, and each undefined dynamic symbol as
                         SYMBOL@VERSION with the file of its version
      2. library code    the exclusive bytes of each class of the rule
                         library-bytes, and the largest library bodies
      3. compiler calls  each import that GCC adds to the code itself
                         (COMPILER_MADE of artifact_symbols), and each
                         import of a function of the support library of
                         GCC.  A linked file also gives each member of the
                         support library that its link takes
      4. link facts      each fact of the rule artifact-links of a linked
                         file.  A static library has no link, and the layer
                         gives its members and its COMDAT groups
      5. source facts    the count of each rule and kind of the findings of the
                         quarantine plugin in the objects of the artifact.
                         Then the count of each kind of the records of the
                         library code, and the entities with the most
                         findings
    --limit N gives the maximum lines of each long list.  The default is
    LIMIT.

THE EXIT
    0 when the command prints the layers, 1 when the build or the artifact
    cannot be read, and 2 on a usage error.

`guard selftest tests.guard.test_bom` does the test of the module.
"""

from collections import Counter
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.guard.engines.elf import artifact_links, artifact_symbols, library_bytes, runner
from assay.readers import census as build_census
from assay.readers import dwarf, elf, linkmap, sections

LIMIT = 20
INDENT = "     "
DYNAMIC = "dynamic"
# The problems that end the read of one layer, and the other layers stay (THE LAYERS).
LAYER_PROBLEMS = (
    runner.BuildError,
    runner.NotApplicable,
    elf.ElfError,
    dwarf.DwarfError,
    linkmap.LinkMapError,
    sections.SectionError,
    OSError,
)


@dataclass(frozen=True, slots=True)
class Artifact:
    """The target of the command: its name, its type, its file and the first directory of its source directory."""

    name: str
    kind: str
    path: Path
    directory: str

    @property
    def is_library(self) -> bool:
        """True for a static library, which has no link."""
        return self.kind == runner.STATIC_LIBRARY


@dataclass(frozen=True, slots=True)
class Name:
    """One import of the artifact: its name, its entity, the file that gives it, its family, its mark and its users.

    `place` is FILE:LINE of the first record of the library code that names the import, or empty.
    """

    symbol: str
    entity: str
    owner: str
    family: str
    mark: str
    users: str
    place: str = ""


@dataclass(frozen=True, slots=True)
class Imports:
    """The imports of the artifact, its libraries of DT_NEEDED, and the functions of the support library of GCC."""

    names: list[Name]
    needed: list[str]
    support: frozenset[str]


# ── The artifact ───────────────────────────────────────────────────


def resolve(context: runner.ElfContext, wanted: str) -> Artifact | None:
    """Return the target of a name or of a path of the build, or None (THE ARTIFACT).

    Complexity: linear in the targets of the code model.

    Raises:
        runner.NotApplicable: If the context has no build directory
        runner.BuildError: If the code model cannot be read
    """
    model = context.code_model()
    source = Path(model.source).resolve()
    given = Path(wanted)
    candidate = (given if given.is_absolute() else Path.cwd() / given).resolve()
    for target in model.targets:
        if target.type_name not in (runner.STATIC_LIBRARY, *runner.LINKED_TYPES) or not target.artifacts:
            continue
        path = Path(target.artifacts[0])
        if target.name != wanted and path.resolve() != candidate:
            continue
        directory = Path(target.source_dir).resolve()
        parts = directory.relative_to(source).parts if directory.is_relative_to(source) else ()
        return Artifact(target.name, target.type_name, path, parts[0] if parts else "")
    return None


# ── The layers ─────────────────────────────────────────────────────


def library_imports(context: runner.ElfContext, artifact: Artifact) -> Imports:
    """Return the imports of a static library, as the rule artifact-symbols reads them.

    Raises:
        runner.NotApplicable: If the build has not made each static library of the root
        runner.BuildError: If a library, a provider or the code model cannot be read
    """
    artifacts, _, _ = artifact_symbols.read_libraries(context)
    archive = artifact.path.name
    if archive not in artifacts:
        artifacts = {**artifacts, archive: artifact_symbols.read_artifact(artifact.path)}
    providers = artifact_symbols.providers_of(context.compiler(), context.link_libraries())
    places: dict[str, str] = {}
    try:
        for member in elf.members(artifact.path):
            section = sections.of_member(member)
            for symbol, record in (section.symbols() if section is not None else {}).items():
                places.setdefault(symbol, f"{record.file}:{record.line}")
    except (OSError, elf.ElfError, sections.SectionError) as problem:
        raise runner.BuildError(f"the records of {artifact.path} cannot be read: {problem}") from None
    names = [
        Name(
            item.symbol,
            item.entity,
            item.provider.label if item.provider is not None else "no provider",
            item.provider.family if item.provider is not None else "-",
            item.mark or "-",
            build_census.names_text(item.members),
            places.get(item.symbol, ""),
        )
        for item in artifact_symbols.imports_of(artifacts, {archive: artifact.name}, providers)
    ]
    return Imports(names, [], providers.support)


def linked_imports(artifact: Artifact) -> Imports:
    """Return the libraries of DT_NEEDED and the undefined dynamic symbols of a linked file.

    Raises:
        elf.ElfError: If a table of the file is not fully in the file
        OSError: If the file cannot be read
    """
    with elf.mapped(artifact.path) as member:
        files = {need.name: need.file for need in member.version_needs()}
        needed = [entry.text for entry in member.dynamic() if entry.tag == elf.DT_NEEDED]
        names = [
            Name(
                symbol.name,
                artifact_symbols.entity_of(symbol.name, symbol.version),
                files.get(symbol.version, "no version"),
                DYNAMIC,
                "-",
                "",
            )
            for symbol in member.dynamic_symbols()
            if symbol.is_undefined
        ]
    return Imports(sorted(names, key=lambda item: (item.owner, item.entity)), needed, frozenset())


def read_imports(context: runner.ElfContext, artifact: Artifact) -> Imports:
    """Return the imports of the artifact (THE LAYERS, layer 1).

    Raises:
        runner.NotApplicable: If the build has not made each static library of the root
        runner.BuildError: If a library, a provider or the code model cannot be read
        elf.ElfError: If a table of the file is not fully in the file
        OSError: If the file cannot be read
    """
    return library_imports(context, artifact) if artifact.is_library else linked_imports(artifact)


def limited(lines: Sequence[str], limit: int) -> list[str]:
    """Return the first `limit` lines, and one line with the count of the other lines."""
    shown = [f"{INDENT}{line}" for line in lines[:limit]]
    if len(lines) > limit:
        shown.append(f"{INDENT}... {len(lines) - limit} more")
    return shown


def import_layer(imports: Imports, limit: int) -> list[str]:
    """Return the lines of layer 1: the libraries of DT_NEEDED, and each import with its owner, family and mark."""
    families = Counter(item.family for item in imports.names)
    counted = ", ".join(f"{name} {count}" for name, count in sorted(families.items()))
    lines = [f"1. imports: {len(imports.names)} names ({counted or 'none'})"]
    if imports.needed:
        lines.append(f"{INDENT}needed: {', '.join(imports.needed)}")
    rows = [
        f"{item.entity}  {item.owner}  {item.family}  {item.mark}"
        + (f"  ({item.users})" if item.users else "")
        + (f"  at {item.place}" if item.place else "")
        for item in sorted(imports.names, key=lambda item: (item.family, item.entity))
    ]
    return lines + limited(rows, limit)


def code_layer(context: runner.ElfContext, artifact: Artifact, limit: int) -> list[str]:
    """Return the lines of layer 2: the bytes of each class and the largest library bodies, from DWARF.

    Raises:
        runner.BuildError: If the DWARF of the artifact or the names of its bodies cannot be read
    """
    measure = library_bytes.read_measure(artifact.path, library_bytes.places_of(context))
    classes = ", ".join(f"{name} {measure.classes.get(name, 0)}" for name in library_bytes.CLASSES)
    largest = sorted(measure.bodies.items(), key=lambda item: (-item[1], item[0]))
    try:
        names = elf.demangle([name for name, _ in largest[:limit]])
    except elf.ElfError as problem:
        raise runner.BuildError(str(problem)) from None
    rows = [f"{INDENT}{size} bytes  {name}" for name, (_, size) in zip(names, largest[:limit], strict=True)]
    if len(largest) > limit:
        rows.append(f"{INDENT}... {len(largest) - limit} more library bodies")
    return [f"2. library code: {measure.library} bytes of library code ({classes})", *rows]


def call_layer(context: runner.ElfContext, artifact: Artifact, imports: Imports | None) -> list[str]:
    """Return the lines of layer 3: the imports that GCC adds, and the members of the support library of a link.

    Raises:
        runner.BuildError: If the map of the link of a linked file cannot be read
    """
    if imports is None:
        return ["3. compiler calls: cannot be read, because the imports cannot be read"]
    calls = sorted(
        {
            item.symbol
            for item in imports.names
            if item.symbol in artifact_symbols.COMPILER_MADE or item.symbol in imports.support
        }
    )
    lines = [f"3. compiler calls: {', '.join(calls) or 'none'}"]
    if not artifact.is_library and context.build_dir is not None:
        members = sorted(
            entity
            for kind, entity in artifact_links.map_facts(artifact.path, context.build_dir)
            if kind == artifact_links.STATIC_MEMBER and entity.startswith(artifact_symbols.SUPPORT_ARCHIVES)
        )
        lines.append(f"{INDENT}members of the support library: {', '.join(members) or 'none'}")
    return lines


def link_layer(context: runner.ElfContext, artifact: Artifact, limit: int) -> list[str]:
    """Return the lines of layer 4: the link facts of a linked file, or the members and the groups of a library.

    Raises:
        runner.BuildError: If the map of the link cannot be read
        elf.ElfError: If a table of the file is not fully in the file
        OSError: If the file cannot be read
    """
    if artifact.is_library:
        members = elf.members(artifact.path)
        groups = {group.signature for member in members for group in member.groups() if group.is_comdat}
        said = f"a static library has no link.  It has {len(members)} members and {len(groups)} COMDAT groups"
        return [f"4. link facts: {said}"]
    build_dir = context.directory()
    words = artifact_links.path_words(context.compiler(), build_dir)
    with elf.mapped(artifact.path) as member:
        is_vessel = artifact_links.group_of(artifact.directory) == artifact_links.VESSEL
        facts = artifact_links.member_facts(member, member.dynamic(), words, is_vessel)
    facts |= artifact_links.map_facts(artifact.path, build_dir)
    kinds = Counter(kind for kind, _ in facts)
    counted = ", ".join(f"{kind} {kinds[kind]}" for kind in artifact_links.KINDS if kinds[kind])
    rows = [
        f"{kind}  {entity}"
        for kind, entity in sorted(facts, key=lambda item: (artifact_links.KINDS.index(item[0]), item[1]))
    ]
    return [f"4. link facts: {len(facts)} facts ({counted})"] + limited(rows, limit)


def source_layer(context: runner.ElfContext, artifact: Artifact, limit: int) -> list[str]:
    """Return the lines of layer 5: the plugin findings in the objects of the artifact, by rule, kind and entity.

    Raises:
        runner.NotApplicable: If the build is not a build of Ninja
        runner.BuildError: If ninja cannot give the objects of the target
        sections.SectionError: If an object or its section cannot be read
        elf.ElfError: If an archive cannot be read
        OSError: If a file cannot be read
    """
    if artifact.is_library:
        found = [sections.of_member(member) for member in elf.members(artifact.path)]
    else:
        found = [sections.read(path) for path in sorted(context.objects_of(artifact.name))]
    items = [finding for section in found if section is not None for finding in section.findings]
    regions = sum(1 for item in items if item.kind == sections.REGION)
    records = Counter(item.kind for item in items if item.rule == sections.LIBRARY_RULE)
    real = [item for item in items if item.kind not in sections.NOT_FINDINGS and item.rule != sections.LIBRARY_RULE]
    kinds = Counter(f"{item.rule} {item.kind}" for item in real)
    entities = Counter(f"{item.rule} {item.kind} {item.entity or '-'}" for item in real)
    lines = [f"5. source facts: {len(real)} findings of the plugin in {len(found)} objects, {regions} opt-out regions"]
    lines += limited([f"{count}  {name}" for name, count in kinds.most_common()], limit)
    shown = ", ".join(f"{kind} {count}" for kind, count in sorted(records.items())) or "none"
    lines.append(f"{INDENT}records of the library code: {shown}")
    lines.append(f"{INDENT}the entities with the most findings:")
    return lines + limited([f"{count}  {name}" for name, count in entities.most_common()], limit)


def bill(context: runner.ElfContext, artifact: Artifact, limit: int) -> list[str]:
    """Return the lines of the five layers of an artifact (THE LAYERS).  A layer that cannot be read gives one line."""
    kind = "static library" if artifact.is_library else artifact.kind.lower().replace("_", " ")
    lines = [f"guard bom: {build_census.shown(artifact.path, context.root)}, the {kind} {artifact.name}"]
    try:
        imports: Imports | None = read_imports(context, artifact)
    except LAYER_PROBLEMS as problem:
        imports = None
        lines.append(f"1. imports: cannot be read: {problem}")
    if imports is not None:
        lines += import_layer(imports, limit)
    steps: tuple[tuple[str, Callable[[], list[str]]], ...] = (
        ("2. library code", lambda: code_layer(context, artifact, limit)),
        ("3. compiler calls", lambda: call_layer(context, artifact, imports)),
        ("4. link facts", lambda: link_layer(context, artifact, limit)),
        ("5. source facts", lambda: source_layer(context, artifact, limit)),
    )
    for heading, step in steps:
        try:
            lines += step()
        except LAYER_PROBLEMS as problem:
            lines.append(f"{heading}: cannot be read: {problem}")
    return lines


# ── The command ────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Print the five layers of one artifact of a build (THE EXIT).

    Returns:
        0 when the command prints the layers, 1 when the build or the artifact cannot be read, 2 on a usage error
    """
    from assay.core import cli

    parser = cli.parser("guard bom", "print the bill of materials of one artifact of a build in five layers")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=LIMIT)
    parser.add_argument("artifact")
    options = parser.parse_args(argv)
    if options.limit < 1:
        check_report.write_line("guard bom: --limit takes a number of 1 or more", is_error=True)
        return 2
    context = runner.ElfContext(REPO_ROOT, options.build_dir.resolve())
    try:
        artifact = resolve(context, options.artifact)
    except (runner.BuildError, runner.NotApplicable) as problem:
        check_report.write_line(f"guard bom: the build cannot be read: {problem}", is_error=True)
        return 1
    if artifact is None:
        check_report.write_line(
            f"guard bom: {options.artifact} is no static library, program, shared library or module of the build "
            f"{context.build_shown()}.  Give the name of a target or the path of its file",
            is_error=True,
        )
        return 1
    for line in bill(context, artifact, options.limit):
        check_report.write_line(line)
    return 0
