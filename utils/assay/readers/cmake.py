"""cmake — the replies of the CMake file API in a build directory, and the presets of a source tree.

THE INPUTS
    CMake writes CMakeCache.txt into each build directory that it configures.
    A directory with that file is a build directory that CMake configured.
    No module reads that file, because the reply cache-v2 gives each entry of it.

    The root CMakeLists.txt gives the query of each object of QUERIES with
    cmake_file_api().  At the end of each configure, CMake writes the replies
    into BUILD_DIR/.cmake/api/v1/reply: one JSON file for each object, and
    then an index file index-<time>.json that names each object.  The index
    with the largest name is the current one.  A scratch project with no such
    call writes its query with request() before its configure.

    CMakePresets.json and CMakeUserPresets.json of a source tree give the
    configure presets.  The reader reads them with json.

WHAT THE READER GIVES
    is_configured(build_dir) and cache_file(build_dir): the cache file of a
    build directory, for a check that CMake configured the directory.
    cache(build_dir): each entry of the cache, as a CacheEntry.
    roots(build_dir): the source root and the build root that CMake names.
    cmake_files(build_dir): each CMake file that the configure read.
    toolchains(build_dir): the compiler of each language.
    codemodel(build_dir): each target of the build, with its type, its
    artifacts, its sources, its precompiled headers and its link fragments.
    Each target also gives the position of the CMake command that makes it.
    presets(root): the configure presets of each presets file of a tree.
    request(build_dir) and plant(build_dir, ...): the query of a scratch
    project, and a planted reply for a test.

ERRORS
    CmakeError names the file and the command that writes it.  MissingReply,
    a subclass, tells that the build directory holds no reply of an object.
    A build directory of the tree has each reply after a configure with this
    version of the root CMakeLists.txt.

THE COST
    A read of a reply reads the names of the reply directory, about 2,500
    files in a build of the tree.  Then it parses the index and one object:
    about 1 ms in all.  codemodel() also parses one file for each target,
    about 0.25 s for the tree.  The module imports json and os only, and its
    classes have __slots__, because `loop fixture` imports it.
"""

import json
import os

TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping
    from pathlib import Path

CACHE_NAME = "CMakeCache.txt"
API_DIRECTORY = (".cmake", "api", "v1")
# The query of each object that the root CMakeLists.txt gives, as a query file name: the kind and the version.
QUERIES = ("codemodel-v2", "cache-v2", "cmakeFiles-v1", "toolchains-v1")
# The client of request(): a scratch project gets its replies through this query directory.
CLIENT = "client-assay"
PRESET_FILES = ("CMakePresets.json", "CMakeUserPresets.json")
AGAIN = "Configure the build directory again, for example with `cmake --preset default` or `cmake BUILD_DIR`."


class CmakeError(Exception):
    """A reply of the file API or a presets file cannot be read, or it does not have the shape of its format."""


class MissingReply(CmakeError):
    """The build directory holds no reply of an object, because no configure wrote one for its query."""


class CacheEntry:
    """One entry of the CMake cache: its name, its type, its value and its properties (ADVANCED, HELPSTRING)."""

    __slots__ = ("name", "properties", "type_name", "value")

    def __init__(self, name: str, type_name: str, value: str, properties: Mapping[str, str] | None = None) -> None:
        """Hold the parts of one entry."""
        self.name = name
        self.type_name = type_name
        self.value = value
        self.properties = dict(properties or {})

    def __eq__(self, other: object) -> bool:
        """Two entries are equal when each part is equal."""
        if not isinstance(other, CacheEntry):
            return NotImplemented
        return (self.name, self.type_name, self.value, self.properties) == (
            other.name,
            other.type_name,
            other.value,
            other.properties,
        )

    def __hash__(self) -> int:
        """Hash the name, the type and the value."""
        return hash((self.name, self.type_name, self.value))

    def __repr__(self) -> str:
        """Return the entry as the cache file spells it."""
        return f"CacheEntry({self.name}:{self.type_name}={self.value!r})"


class Origin:
    """A position in a CMake file: the absolute path of the file, and the line, or 0 when the backtrace gives none."""

    __slots__ = ("file", "line")

    def __init__(self, file: str, line: int) -> None:
        """Hold the file and the line."""
        self.file = file
        self.line = line

    def __eq__(self, other: object) -> bool:
        """Two positions are equal when the file and the line are equal."""
        if not isinstance(other, Origin):
            return NotImplemented
        return (self.file, self.line) == (other.file, other.line)

    def __hash__(self) -> int:
        """Hash the file and the line."""
        return hash((self.file, self.line))

    def __repr__(self) -> str:
        """Return the position as FILE:LINE."""
        return f"Origin({self.file}:{self.line})"


class Source:
    """One source of a target: its absolute path, True when the build writes it, and its compile group or None."""

    __slots__ = ("compile_group", "is_generated", "path")

    def __init__(self, path: str, is_generated: bool, compile_group: int | None) -> None:
        """Hold the parts of one source."""
        self.path = path
        self.is_generated = is_generated
        self.compile_group = compile_group

    def __repr__(self) -> str:
        """Return the source for a message."""
        return f"Source({self.path!r}, generated={self.is_generated})"


class PrecompileHeader:
    """One precompiled header of a compile group: the header as the target names it, and the call that adds it."""

    __slots__ = ("header", "origin")

    def __init__(self, header: str, origin: Origin | None) -> None:
        """Hold the header and the position of the call."""
        self.header = header
        self.origin = origin

    def __repr__(self) -> str:
        """Return the header for a message."""
        return f"PrecompileHeader({self.header!r}, {self.origin!r})"


class Target:
    """One target of the code model.

    `artifacts` holds the absolute path of each file that the target writes.
    `source_dir` is the absolute directory of the CMake file that makes the
    target, and `origin` is the position of the command that makes it.
    `link_fragments` holds each (fragment, role) of the link line, and it is
    empty for a target with no link step.
    """

    __slots__ = (
        "artifacts",
        "folder",
        "json_file",
        "link_fragments",
        "name",
        "origin",
        "precompile_headers",
        "source_dir",
        "sources",
        "type_name",
    )

    def __init__(
        self,
        name: str,
        type_name: str,
        *,
        artifacts: tuple[str, ...] = (),
        folder: str | None = None,
        source_dir: str = "",
        sources: tuple[Source, ...] = (),
        precompile_headers: tuple[PrecompileHeader, ...] = (),
        link_fragments: tuple[tuple[str, str], ...] = (),
        origin: Origin | None = None,
        json_file: str = "",
    ) -> None:
        """Hold the parts of one target."""
        self.name = name
        self.type_name = type_name
        self.artifacts = artifacts
        self.folder = folder
        self.source_dir = source_dir
        self.sources = sources
        self.precompile_headers = precompile_headers
        self.link_fragments = link_fragments
        self.origin = origin
        self.json_file = json_file

    def __repr__(self) -> str:
        """Return the target for a message."""
        return f"Target({self.name!r}, {self.type_name})"


class CodeModel:
    """The code model of a build: the source root, the build root and each target of each configuration."""

    __slots__ = ("build", "source", "targets")

    def __init__(self, source: str, build: str, targets: tuple[Target, ...]) -> None:
        """Hold the roots and the targets."""
        self.source = source
        self.build = build
        self.targets = targets

    def __repr__(self) -> str:
        """Return the code model for a message."""
        return f"CodeModel({self.source!r}, {len(self.targets)} targets)"


class Toolchain:
    """The compiler of one language: its language, its id, its path and its version."""

    __slots__ = ("compiler", "compiler_id", "language", "version")

    def __init__(self, language: str, compiler_id: str, compiler: str, version: str) -> None:
        """Hold the parts of one toolchain."""
        self.language = language
        self.compiler_id = compiler_id
        self.compiler = compiler
        self.version = version

    def __repr__(self) -> str:
        """Return the toolchain for a message."""
        return f"Toolchain({self.language}, {self.compiler!r})"


class Preset:
    """One configure preset: its name, its cache variables and its environment.

    A value is a text, or None when the presets file gives a value that is
    not a text, such as a boolean or null.
    """

    __slots__ = ("cache_variables", "environment", "name")

    def __init__(
        self, name: str, cache_variables: Mapping[str, str | None], environment: Mapping[str, str | None]
    ) -> None:
        """Hold the parts of one preset."""
        self.name = name
        self.cache_variables = dict(cache_variables)
        self.environment = dict(environment)

    def __repr__(self) -> str:
        """Return the preset for a message."""
        return f"Preset({self.name!r})"


class PresetFile:
    """One presets file of a tree: its path, its configure presets, and the problem that stops its read, or None."""

    __slots__ = ("configure", "path", "problem")

    def __init__(self, path: Path, configure: tuple[Preset, ...], problem: str | None) -> None:
        """Hold the parts of one presets file."""
        self.path = path
        self.configure = configure
        self.problem = problem

    def __repr__(self) -> str:
        """Return the file for a message."""
        return f"PresetFile({str(self.path)!r}, {len(self.configure)} presets)"


# ── The cache file ─────────────────────────────────────────────────────────


def cache_file(build_dir: Path) -> Path:
    """Return the path of the CMake cache of a build directory.  The file can be missing."""
    return build_dir / CACHE_NAME


def is_configured(build_dir: Path) -> bool:
    """Tell if a directory holds the cache that CMake writes when it configures a build directory."""
    return cache_file(build_dir).is_file()


# ── The replies ────────────────────────────────────────────────────────────


def api_directory(build_dir: Path) -> Path:
    """Return the directory of the file API in a build directory.  The directory can be missing."""
    return build_dir.joinpath(*API_DIRECTORY)


def request(build_dir: Path) -> None:
    """Write the client query of each object of QUERIES, and the next configure of the directory writes the replies.

    The root CMakeLists.txt of the tree gives the query with cmake_file_api.
    Only a scratch project with no such call uses this function.
    """
    query = api_directory(build_dir) / "query" / CLIENT
    query.mkdir(parents=True, exist_ok=True)
    for name in QUERIES:
        (query / name).write_bytes(b"")


def is_index(name: str) -> bool:
    """Tell if a file name of the reply directory is the name of an index file."""
    return name.startswith("index-") and name.endswith(".json")


def _read_json(path: str, what: str) -> object:
    """Return the JSON value of one file of the reply.

    Raises:
        MissingReply: If the file is missing
        CmakeError: If the file cannot be read or is not JSON
    """
    try:
        with open(path, encoding="utf-8", errors="surrogateescape") as handle:
            text = handle.read()
    except FileNotFoundError:
        raise MissingReply(
            f"{path} is missing, so the {what} cannot be read.  A configure that runs at this time can remove it.  "
            f"When no configure runs: {AGAIN}"
        ) from None
    except OSError as problem:
        raise CmakeError(f"{path} cannot be read: {problem}") from None
    try:
        return json.loads(text)
    except ValueError as problem:
        raise CmakeError(f"{path} is not JSON ({problem}), so the {what} cannot be read.  {AGAIN}") from None


def _text_of(value: object, path: str, what: str) -> str:
    """Return a text field of a reply.

    Raises:
        CmakeError: If the field is not a text
    """
    if not isinstance(value, str):
        raise CmakeError(f"{path}: the {what} is not a text.  {AGAIN}")
    return value


def _list_of(value: object, path: str, what: str) -> list[object]:
    """Return a list field of a reply, or an empty list for a field that the reply does not give.

    Raises:
        CmakeError: If the reply gives the field and it is not a list
    """
    if value is None:
        return []
    if not isinstance(value, list):
        raise CmakeError(f"{path}: the {what} is not a list.  {AGAIN}")
    return value


def _dict_of(value: object, path: str, what: str) -> dict[str, object]:
    """Return an object field of a reply, or an empty object for a field that the reply does not give.

    Raises:
        CmakeError: If the reply gives the field and it is not an object
    """
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise CmakeError(f"{path}: the {what} is not an object.  {AGAIN}")
    return value


def _reply_object(build_dir: Path, kind: str, version: int) -> tuple[str, dict[str, object]]:
    """Return the path and the JSON object of the newest reply of one kind and one version.

    Complexity: one sorted listing of the reply directory, and the parse of the index and of the object.

    Raises:
        MissingReply: If the build directory holds no index, or the index names no object of the kind
        CmakeError: If a file of the reply cannot be read or does not have the shape of its format
    """
    reply = os.path.join(api_directory(build_dir), "reply")
    try:
        indexes = [name for name in sorted(os.listdir(reply)) if is_index(name)]
    except FileNotFoundError:
        indexes = []
    except OSError as problem:
        raise CmakeError(f"{reply} cannot be listed: {problem}") from None
    if not indexes:
        raise MissingReply(
            f"{reply} holds no index of the CMake file API.  The root CMakeLists.txt gives the query of the replies "
            f"with cmake_file_api, and CMake writes them at the end of each configure.  {AGAIN}"
        )
    index_path = os.path.join(reply, indexes[-1])
    index = _read_json(index_path, "index of the file API")
    objects = index.get("objects") if isinstance(index, dict) else None
    if not isinstance(objects, list):
        raise CmakeError(f"{index_path} has no list of objects.  {AGAIN}")
    for item in objects:
        if not isinstance(item, dict):
            continue
        found_version, json_file = item.get("version"), item.get("jsonFile")
        if (
            item.get("kind") == kind
            and isinstance(found_version, dict)
            and found_version.get("major") == version
            and isinstance(json_file, str)
        ):
            path = os.path.join(reply, json_file)
            found = _read_json(path, f"reply {kind}-v{version}")
            if not isinstance(found, dict):
                raise CmakeError(f"{path} is not a JSON object.  {AGAIN}")
            return path, found
    raise MissingReply(
        f"{index_path} names no object {kind}-v{version}, which the root CMakeLists.txt gives the query of with "
        f"cmake_file_api.  {AGAIN}"
    )


def cache(build_dir: Path) -> dict[str, CacheEntry]:
    """Return each entry of the CMake cache of a build directory, from the reply cache-v2.

    Complexity: linear in the length of the reply.

    Raises:
        MissingReply: If no configure wrote the reply
        CmakeError: If the reply cannot be read or does not have the shape of its format
    """
    path, found = _reply_object(build_dir, "cache", 2)
    entries: dict[str, CacheEntry] = {}
    for item in _list_of(found.get("entries"), path, "list of entries"):
        record = _dict_of(item, path, "entry")
        properties: dict[str, str] = {}
        for prop in _list_of(record.get("properties"), path, "list of properties"):
            fields = _dict_of(prop, path, "property")
            properties[_text_of(fields.get("name"), path, "property name")] = _text_of(
                fields.get("value"), path, "property value"
            )
        name = _text_of(record.get("name"), path, "entry name")
        entries[name] = CacheEntry(
            name,
            _text_of(record.get("type"), path, "entry type"),
            _text_of(record.get("value"), path, "entry value"),
            properties,
        )
    return entries


def cache_value(build_dir: Path, name: str) -> str | None:
    """Return the value of one entry of the CMake cache, or None when the cache has no such entry.

    Raises:
        MissingReply: If no configure wrote the reply
        CmakeError: If the reply cannot be read
    """
    found = cache(build_dir).get(name)
    return None if found is None else found.value


def roots(build_dir: Path) -> tuple[str, str]:
    """Return the source root and the build root of a build directory, from the reply cmakeFiles-v1.

    Raises:
        MissingReply: If no configure wrote the reply
        CmakeError: If the reply cannot be read
    """
    path, found = _reply_object(build_dir, "cmakeFiles", 1)
    paths = _dict_of(found.get("paths"), path, "paths")
    return _text_of(paths.get("source"), path, "source root"), _text_of(paths.get("build"), path, "build root")


def cmake_files(build_dir: Path) -> tuple[str, ...]:
    """Return the absolute path of each file that the last configure read, in the sequence of the reply.

    Raises:
        MissingReply: If no configure wrote the reply
        CmakeError: If the reply cannot be read
    """
    path, found = _reply_object(build_dir, "cmakeFiles", 1)
    source = _text_of(_dict_of(found.get("paths"), path, "paths").get("source"), path, "source root")
    return tuple(
        os.path.join(source, _text_of(_dict_of(item, path, "input").get("path"), path, "input path"))
        for item in _list_of(found.get("inputs"), path, "list of inputs")
    )


def toolchains(build_dir: Path) -> tuple[Toolchain, ...]:
    """Return the compiler of each language that the build enables, from the reply toolchains-v1.

    Raises:
        MissingReply: If no configure wrote the reply
        CmakeError: If the reply cannot be read
    """
    path, found = _reply_object(build_dir, "toolchains", 1)
    chains: list[Toolchain] = []
    for item in _list_of(found.get("toolchains"), path, "list of toolchains"):
        record = _dict_of(item, path, "toolchain")
        compiler = _dict_of(record.get("compiler"), path, "compiler")
        chains.append(
            Toolchain(
                _text_of(record.get("language"), path, "language"),
                str(compiler.get("id") or ""),
                str(compiler.get("path") or ""),
                str(compiler.get("version") or ""),
            )
        )
    return tuple(chains)


def _origin_of(graph: dict[str, object], index: object, source: str, path: str) -> Origin | None:
    """Return the position of one node of the backtrace graph of a target, or None for no node.

    Raises:
        CmakeError: If the node or its file is not in the graph
    """
    if not isinstance(index, int):
        return None
    nodes = _list_of(graph.get("nodes"), path, "list of backtrace nodes")
    files = _list_of(graph.get("files"), path, "list of backtrace files")
    if not 0 <= index < len(nodes):
        raise CmakeError(f"{path}: the backtrace {index} is not a node of the graph.  {AGAIN}")
    node = _dict_of(nodes[index], path, "backtrace node")
    file_index = node.get("file")
    if not isinstance(file_index, int) or not 0 <= file_index < len(files):
        raise CmakeError(f"{path}: the backtrace node {index} names no file of the graph.  {AGAIN}")
    line = node.get("line")
    return Origin(
        os.path.join(source, _text_of(files[file_index], path, "backtrace file")), line if isinstance(line, int) else 0
    )


def _source_of(item: object, path: str, source: str) -> Source:
    """Return one source of a target object.

    Raises:
        CmakeError: If the source has no text path
    """
    record = _dict_of(item, path, "source")
    group = record.get("compileGroupIndex")
    return Source(
        os.path.join(source, _text_of(record.get("path"), path, "source path")),
        record.get("isGenerated") is True,
        group if isinstance(group, int) else None,
    )


def _target_of(found: dict[str, object], path: str, source: str, build: str) -> Target:
    """Return the target of one target object of the reply codemodel-v2.

    Raises:
        CmakeError: If the object does not have the shape of its format
    """
    graph = _dict_of(found.get("backtraceGraph"), path, "backtrace graph")
    paths = _dict_of(found.get("paths"), path, "paths")
    headers: list[PrecompileHeader] = []
    for group in _list_of(found.get("compileGroups"), path, "compile groups"):
        for header in _list_of(_dict_of(group, path, "compile group").get("precompileHeaders"), path, "headers"):
            fields = _dict_of(header, path, "precompiled header")
            headers.append(
                PrecompileHeader(
                    _text_of(fields.get("header"), path, "header"),
                    _origin_of(graph, fields.get("backtrace"), source, path),
                )
            )
    fragments = tuple(
        (
            _text_of(_dict_of(item, path, "fragment").get("fragment"), path, "fragment"),
            str(_dict_of(item, path, "fragment").get("role") or ""),
        )
        for item in _list_of(_dict_of(found.get("link"), path, "link").get("commandFragments"), path, "fragments")
    )
    artifacts = tuple(
        os.path.normpath(os.path.join(build, _text_of(_dict_of(item, path, "artifact").get("path"), path, "artifact")))
        for item in _list_of(found.get("artifacts"), path, "artifacts")
    )
    folder = _dict_of(found.get("folder"), path, "folder").get("name")
    return Target(
        _text_of(found.get("name"), path, "target name"),
        _text_of(found.get("type"), path, "target type"),
        artifacts=artifacts,
        folder=folder if isinstance(folder, str) else None,
        source_dir=os.path.normpath(os.path.join(source, str(paths.get("source") or "."))),
        sources=tuple(_source_of(item, path, source) for item in _list_of(found.get("sources"), path, "sources")),
        precompile_headers=tuple(headers),
        link_fragments=fragments,
        origin=_origin_of(graph, found.get("backtrace"), source, path),
        json_file=os.path.basename(path),
    )


def codemodel(build_dir: Path) -> CodeModel:
    """Return the code model of a build directory, from the reply codemodel-v2 and the object of each target.

    A source path and a backtrace file of the reply are relative to the source
    root.  An artifact path is relative to the build root.  The reader gives
    each one as an absolute path.

    Complexity: linear in the length of the reply.  The tree has about 2,500 targets, and one read takes about 0.25 s.

    Raises:
        MissingReply: If no configure wrote the reply, or a target object is missing
        CmakeError: If a file of the reply cannot be read or does not have the shape of its format
    """
    path, found = _reply_object(build_dir, "codemodel", 2)
    paths = _dict_of(found.get("paths"), path, "paths")
    source = _text_of(paths.get("source"), path, "source root")
    build = _text_of(paths.get("build"), path, "build root")
    reply = os.path.dirname(path)
    targets: list[Target] = []
    for configuration in _list_of(found.get("configurations"), path, "configurations"):
        for stub in _list_of(_dict_of(configuration, path, "configuration").get("targets"), path, "targets"):
            json_file = _text_of(_dict_of(stub, path, "target").get("jsonFile"), path, "target file")
            target_path = os.path.join(reply, json_file)
            detail = _read_json(target_path, "target object")
            if not isinstance(detail, dict):
                raise CmakeError(f"{target_path} is not a JSON object.  {AGAIN}")
            targets.append(_target_of(detail, target_path, source, build))
    return CodeModel(source, build, tuple(targets))


# ── The presets ────────────────────────────────────────────────────────────


def _preset_values(value: object) -> dict[str, str | None]:
    """Return the variables of a preset field: a text for a text or for an object with a text value, else None."""
    if not isinstance(value, dict):
        return {}
    found: dict[str, str | None] = {}
    for name, item in value.items():
        text = item.get("value") if isinstance(item, dict) else item
        found[str(name)] = text if isinstance(text, str) else None
    return found


def _preset_file(path: Path) -> PresetFile:
    """Return the configure presets of one presets file.  A missing file has no preset and no problem."""
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return PresetFile(path, (), None)
    except (OSError, UnicodeDecodeError) as problem:
        return PresetFile(path, (), f"the file cannot be read: {problem}")
    try:
        document = json.loads(text)
    except ValueError as problem:
        return PresetFile(path, (), f"the file is not valid JSON: {problem}")
    if not isinstance(document, dict):
        return PresetFile(path, (), "the top level of the file is not a JSON object")
    configure = document.get("configurePresets", [])
    if not isinstance(configure, list) or not all(isinstance(item, dict) for item in configure):
        return PresetFile(path, (), "configurePresets is not a list of objects")
    found = tuple(
        Preset(
            str(item.get("name", "?")),
            _preset_values(item.get("cacheVariables")),
            _preset_values(item.get("environment")),
        )
        for item in configure
    )
    return PresetFile(path, found, None)


def presets(root: Path) -> tuple[PresetFile, ...]:
    """Return each presets file of a source tree that is on the disk, CMakePresets.json first.

    The reader does not follow the field include, because no presets file of the tree uses it.
    """
    files = (_preset_file(root / name) for name in PRESET_FILES)
    return tuple(found for found in files if found.configure or found.problem is not None or found.path.is_file())


# ── Plants for the tests ───────────────────────────────────────────────────


def plant(
    build_dir: Path,
    *,
    cache: Mapping[str, str | tuple[str, str]] | None = None,
    targets: Iterable[Mapping[str, object]] | None = None,
    inputs: Iterable[str] = (),
    source: str | None = None,
    build: str | None = None,
    compilers: Mapping[str, str] | None = None,
) -> Path:
    """Write a planted reply of the file API into a directory, for a test, and return the reply directory.

    The reply holds the four objects of QUERIES.  The function removes each
    file of a reply that a previous call wrote, and a second call then
    replaces the first reply.

    Complexity: one sorted listing of the reply directory, and one write for each object and each target.

    Args:
        build_dir: The directory
        cache: The entries of the cache: a value, or (type, value).  A value alone has the type INTERNAL
        targets: The target objects, in the shape of the reply codemodel-v2
        inputs: The paths of the CMake files that the configure read, relative to the source root
        source: The source root, by default the directory itself
        build: The build root, by default the directory itself
        compilers: The path of the compiler of each language
    """
    reply = api_directory(build_dir) / "reply"
    reply.mkdir(parents=True, exist_ok=True)
    for stale in sorted(reply.iterdir()):
        stale.unlink()
    roots_field = {
        "source": source if source is not None else str(build_dir),
        "build": build if build is not None else str(build_dir),
    }
    entries = [
        {
            "name": name,
            "type": value[0] if isinstance(value, tuple) else "INTERNAL",
            "value": value[1] if isinstance(value, tuple) else value,
            "properties": [],
        }
        for name, value in (cache or {}).items()
    ]
    stubs = []
    for number, target in enumerate(targets or ()):
        name = f"target-{number}.json"
        (reply / name).write_text(json.dumps(dict(target)), encoding="utf-8")
        stubs.append({"name": target.get("name", name), "jsonFile": name})
    chains = [
        {"language": language, "compiler": {"id": "GNU", "path": compiler, "version": ""}}
        for language, compiler in (compilers or {}).items()
    ]
    documents: dict[str, tuple[str, int, dict[str, object]]] = {
        "cache-v2-0.json": ("cache", 2, {"entries": entries}),
        "cmakeFiles-v1-0.json": (
            "cmakeFiles",
            1,
            {"paths": roots_field, "inputs": [{"path": item} for item in inputs]},
        ),
        "toolchains-v1-0.json": ("toolchains", 1, {"toolchains": chains}),
        "codemodel-v2-0.json": ("codemodel", 2, {"paths": roots_field, "configurations": [{"targets": stubs}]}),
    }
    objects = []
    for name, (kind, version, document) in documents.items():
        shaped = {"kind": kind, "version": {"major": version, "minor": 0}, **document}
        (reply / name).write_text(json.dumps(shaped), encoding="utf-8")
        objects.append({"kind": kind, "version": {"major": version, "minor": 0}, "jsonFile": name})
    (reply / "index-0000-00-00T00-00-00-0000.json").write_text(json.dumps({"objects": objects}), encoding="utf-8")
    return reply
