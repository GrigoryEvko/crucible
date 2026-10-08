"""The test of the writer of the negative fixtures of the witness rows (assay.loop.derive.witness_fixtures).

`guard selftest tests.loop.derive.test_witness_fixtures` runs the test.  Each
case plants a rule table and a header in a throwaway tree, and writes one
line.  The compile cases compile a planted fixture with the patched compiler
(assay.core.testing, THE COMPILER).  The case of the registration lists the
tests of the build directory that ASSAY_TEST_BUILD_DIR names, with the ctest
of that build.  Without a compiler or a build directory, the case does not
hold.  Exit 0 when each case holds, else 1.
"""

import functools
import os
import tempfile
from pathlib import Path

from assay.core import proc, ruletable, testing
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT, listing
from assay.loop.derive import witness_fixtures as writer
from assay.setup import pins

# The variable that names the build directory of the case of the registration.
BUILD_DIRECTORY = "ASSAY_TEST_BUILD_DIR"
OPEN_HEADER = (
    "#pragma once\nnamespace planted {\nclass Open {\n    int value_ = 0;\npublic:\n"
    "    constexpr explicit Open(int value) noexcept : value_{value} {}\n};\n}  // namespace planted\n"
)
CLOSED_HEADER = OPEN_HEADER.replace("public:\n", "")
OPEN_ROW = "witness <planted/Open.h> private | planted::Open | planted::Open{42}\n"


def plant(root: Path, rows: str, header: str = OPEN_HEADER) -> None:
    """Write a rule table with the rows, and the header planted/Open.h in include/."""
    (root / writer.TABLE).parent.mkdir(parents=True, exist_ok=True)
    (root / writer.TABLE).write_text("# A planted rule table.\n\n" + rows, encoding="utf-8")
    (root / writer.INCLUDE / "planted").mkdir(parents=True, exist_ok=True)
    (root / writer.INCLUDE / "planted" / "Open.h").write_text(header, encoding="utf-8")


def text_cases(expect: testing.Expect) -> None:
    """Check the slug, the name of a fixture, the text of a fixture and the manifest."""
    expect("the slug drops the root namespace and the case", writer.slug_of("fixy::Secret") == "secret")
    expect(
        "the slug gives one underscore for each run of other characters",
        writer.slug_of("fixy::concurrent::PermissionedSpscChannel::ProducerHandle")
        == "concurrent_permissionedspscchannel_producerhandle",
    )
    expect("a name in another root namespace keeps its root", writer.slug_of("forge::Door") == "forge_door")
    witness = ruletable.parse(OPEN_ROW).witnesses[0]
    text = writer.render(witness)
    expect(
        "a fixture includes the header and tries the expression",
        "#include <planted/Open.h>" in text and "auto forged = planted::Open{42};" in text,
    )
    expect("a fixture names the text of its refusal", '"is private within this context"' in text)
    expect("a fixture declares the brand only when its expression names it", "struct brand" not in text)
    branded = ruletable.parse(
        "witness <planted/Open.h> private | planted::Open | planted::Open{forge::lvalue<forge::brand>()}\n"
    ).witnesses[0]
    expect("a fixture whose expression names the brand declares it", "struct brand {};" in writer.render(branded))
    manifest = writer.manifest_text((witness,))
    expect(
        "the manifest gives the name of the test and its two diagnostics",
        '"name": "neg_witness_planted_open_direct_construction"' in manifest
        and '"reason": "is private within this context"' in manifest
        and '"constructor": "\\\\bOpen\\\\("' in manifest,
    )
    expect(
        "the manifest gives the type and the header of each row",
        '"type": "planted::Open"' in manifest and '"header": "planted/Open.h"' in manifest,
    )


def mode_cases(expect: testing.Expect) -> None:
    """Check --write and --check: a missing header, two rows with one slug, and each kind of drift."""
    with tempfile.TemporaryDirectory(prefix="witness-fixtures-") as work:
        root = Path(work)
        plant(root, OPEN_ROW)
        directory = root / writer.FIXTURE_DIR
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("--check refuses a tree with no fixture", status == ExitCode.FINDINGS)
        status, _text = testing.captured(lambda: writer.written(root))
        fixture = directory / "neg_witness_planted_open_direct_construction.cpp"
        expect("--write writes the fixture and the manifest", status == ExitCode.OK and fixture.is_file())
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("--check accepts the files that --write wrote", status == ExitCode.OK)
        fixture.write_text(fixture.read_text(encoding="utf-8") + "// edited\n", encoding="utf-8")
        status, text = testing.captured(lambda: writer.checked(root))
        expect("--check refuses an edited fixture", status == ExitCode.FINDINGS and "is different" in text)
        stray = directory / "neg_witness_gone_direct_construction.cpp"
        stray.write_text("// a fixture of no row\n", encoding="utf-8")
        status, text = testing.captured(lambda: writer.checked(root))
        expect("--check refuses a fixture of no row", status == ExitCode.FINDINGS and "has no witness row" in text)
        (directory / writer.MANIFEST_NAME).write_text("{}\n", encoding="utf-8")
        status, text = testing.captured(lambda: writer.checked(root))
        expect("--check refuses an edited manifest", status == ExitCode.FINDINGS and "the manifest" in text)
        testing.captured(lambda: writer.written(root))
        expect(
            "--write removes the fixture of no row and writes the edited files again",
            not stray.exists() and testing.captured(lambda: writer.checked(root))[0] == ExitCode.OK,
        )
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = testing.captured(lambda: writer.checked(root))
        finally:
            os.chdir(previous)
        expect(
            "the report from / equals the report from the tree",
            from_slash == testing.captured(lambda: writer.checked(root)),
        )
        plant(root, OPEN_ROW.replace("planted/Open.h", "planted/Gone.h"))
        status, text = testing.captured(lambda: writer.written(root))
        expect(
            "--write refuses a row whose header is not a file of include/",
            status == ExitCode.FINDINGS and "is not a file of include/" in text,
        )
        plant(
            root,
            "witness <planted/Open.h> private | fixy::a::b_c | fixy::a::b_c{}\n"
            "witness <planted/Open.h> private | foundation::a_b::c | foundation::a_b::c{}\n",
        )
        status, text = testing.captured(lambda: writer.checked(root))
        expect("--check refuses two rows with one slug", status == ExitCode.FINDINGS and "gives it too" in text)


def names_constructor(output: str, base: str) -> bool:
    """Return True when OUTPUT holds BASE and `(` after a character that is no part of an identifier, as `\\bBASE\\(`.

    Complexity: linear in the length of OUTPUT.
    """
    start = output.find(f"{base}(")
    while start >= 0:
        before = output[start - 1] if start > 0 else " "
        if not (before.isalnum() or before == "_"):
            return True
        start = output.find(f"{base}(", start + 1)
    return False


def compile_one(compiler: str, root: Path, fixture: Path) -> proc.Completed:
    """Compile one fixture with the planted include directory, and give the result."""
    return proc.run(
        proc.Command(
            [
                compiler,
                "-std=c++26",
                f"-I{root / writer.INCLUDE}",
                "-fdiagnostics-color=never",
                "-fsyntax-only",
                str(fixture),
            ],
            timeout_s=proc.TOOL_S,
        )
    )


def compile_cases(expect: testing.Expect) -> None:
    """Check that a fixture over a public constructor compiles, and that a closed door refuses it as the row says."""
    compiler = testing.patched_compiler()
    expect("the environment names the patched compiler", bool(compiler))
    if not compiler:
        return
    with tempfile.TemporaryDirectory(prefix="witness-fixtures-") as work:
        root = Path(work)
        plant(root, OPEN_ROW)
        testing.captured(lambda: writer.written(root))
        fixture = root / writer.FIXTURE_DIR / "neg_witness_planted_open_direct_construction.cpp"
        expect(
            "a fixture over a public constructor compiles, which is the red that the fixture exists to show",
            compile_one(compiler, root, fixture).exit == 0,
        )
        plant(root, OPEN_ROW, CLOSED_HEADER)
        refused = compile_one(compiler, root, fixture)
        output = refused.out + refused.err
        expect(
            "a fixture over a private constructor fails with the text of its refusal and the constructor before `(`",
            refused.exit != 0 and writer.REFUSAL_TEXTS["private"] in output and names_constructor(output, "Open"),
        )


def registration_cases(expect: testing.Expect) -> None:
    """Check that the ctest of the build directory registers each fixture on disk, and no other fixture."""
    named = os.environ.get(BUILD_DIRECTORY, "")
    expect(f"the environment names a build directory in {BUILD_DIRECTORY}", bool(named))
    if not named:
        return
    build = Path(named)
    listed = proc.run(
        proc.Command(
            [pins.configured_program(build, "ctest"), "-N", "-R", "^neg_witness_.*_direct_construction$"],
            timeout_s=proc.TOOL_S,
            cwd=build,
        )
    ).out
    registered = sorted(
        word
        for word in listed.split()
        if word.startswith(writer.FIXTURE_PREFIX) and word.endswith(writer.FIXTURE_SUFFIX.removesuffix(".cpp"))
    )
    on_disk = sorted(
        path.stem
        for path in listing(REPO_ROOT / writer.FIXTURE_DIR, f"{writer.FIXTURE_PREFIX}*{writer.FIXTURE_SUFFIX}")
    )
    expect("ctest registers each fixture on disk, and no other fixture", bool(on_disk) and registered == on_disk)


# The command lines that main refuses, each with the words of its case.
USAGE_ERRORS = (
    ("a call with no mode", ()),
    ("two modes", ("--write", "--check")),
    ("an unknown word", ("--gen",)),
)


def command_cases(expect: testing.Expect) -> None:
    """Check the command line of main: the usage errors of USAGE_ERRORS, and --help."""
    statuses = {
        label: testing.captured(functools.partial(writer.main, list(words)))[0] for label, words in USAGE_ERRORS
    }
    for label, status in statuses.items():
        expect(f"main refuses {label}", status == ExitCode.USAGE)
    status, text = testing.captured(functools.partial(writer.main, ["--help"]))
    expect("main --help writes the usage and succeeds", status == ExitCode.OK and "--write" in text)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (text_cases, mode_cases, compile_cases, registration_cases, command_cases),
    )
