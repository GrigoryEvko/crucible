"""owners — only a site of the owner row of a name can use the name.

THE RULE
    An owner row of utils/rule-table.txt gives the sites of an entity: the
    files under its paths, and the body of its function when the row names
    one (utils/tools/quarantine/owner.cpp, THE OWNER ROWS).  The quarantine
    plugin gives each other use the kind of the row.  The kinds are:
      * lifetime-start: a use of std::start_lifetime_as or
        std::start_lifetime_as_array outside include/foundation/Lifetime.h
      * door-use: a use of host::InitOwner, the owner of the init door, or of
        host::BackgroundOwner, the owner of the background door
      * testing-use: a use of the testing door foundation::effects::testing
      * detail-use: a use of a detail namespace of foundation or fixy outside
        the base and the check files of its headers
      * unchecked-use and the five kinds that the row of the unchecked
        function gives (THE UNCHECKED WALKS).
    The walk of a body gives the kind lifetime-start: a call, an address, or
    a candidate of a dependent call.  The events of the parser give each
    other kind: a name, a using form, a member access, a friend or a
    reflection (owner.cpp, THE NAMES OF THE SOURCE).  A comment and a literal
    are no use.  The entity of a finding is HOLDER: SHAPE (THE PLACE AND THE
    ENTITY of owner.cpp).  The holder is the function that holds the use,
    with the types of its parameters, "macro NAME" for a use that a macro
    body spells, or namespace-scope.

THE PROOFS
    A proof type, such as a mint key, a context, a capability or a
    permission, is not trivially copyable, and it is not an implicit-lifetime
    type.  Three calls of the lifetime start can still give a pointer to a
    proof with no lifetime: std::start_lifetime_as<P[1]>(buffer),
    std::start_lifetime_as_array<P>(buffer, 1), and std::start_lifetime_as
    of an aggregate that holds a P.  The checked start of
    include/foundation/Lifetime.h refuses each one at compile time.
    test/fixy/test_forgeable_proofs.cpp gives the other ways to a proof that
    stay open, and the rule proof-routes reads them.

    The init context permits the effects of process startup, and the
    background context permits the effects of a background thread.  Only a
    reviewed process entry point, or the reviewed entry function of a
    background thread, names the owner of such a door.  The testing door
    mints a context with no key, so code that ships does not name it.  A
    function in a namespace with the name `detail` does the work of a door
    with no gate of its own, so only the base names it.  A type alias of a
    type of a `detail` namespace is a public name of the base, and a call
    that argument-dependent lookup finds names no namespace.

THE UNCHECKED WALKS
    In GCC 16, a splice of a nonstatic data member has no access check, and
    only a walk with std::meta::access_context::unchecked() gives the
    reflection of a private member.  The owner row of that function gives
    these kinds (owner.cpp, THE WALKED RETURN):
      * unchecked-use: a name or a member access of the function, through
        each qualifier, alias and object
      * access-alias: a using-declaration, an alias declaration or a typedef
        of access_context
      * access-reflection: a reflection of access_context, of std::meta or
        of std
      * member-splice: a splice of a member of an object, and a member access
        through a splice
      * open-static-member: a writable static data member with private or
        protected access, in a class of include/foundation, include/fixy or
        include/crucible
      * walked-return: a function or a lambda that uses unchecked and returns
        a member of a class that a walk found.
    A use of such a kind that a macro body spells counts in the macro body
    and at the expansion point.

THE SCOPE
    The rule reads each file that a build compiles.  test/ and bench/ use the
    test door on purpose, so the kinds door-use and testing-use count only in
    the trees of production code (PRODUCTION_TREES).  A check file of
    test/layer/checks/ is test code too.

THE LEDGER
    utils/ledgers/owners.txt counts the reviewed uses of each file, kind and
    entity (the policy count), and each row gives a reason:
        PATH | KIND | COUNT | HOLDER: SHAPE | REASON
    A row of a reviewed walk of unchecked has the value admit (AN ADMIT ROW
    OF A COUNT LEDGER of assay.core.ledger).  A new use is more than its
    count and fails.  These rows are errors:
      * A row with no reason
      * A row of the kinds lifetime-start, open-static-member and
        walked-return (REFUSED_KINDS).  No row admits such a finding
      * A row of the kind detail-use of a file outside test/, or in
        test/fuzz/.  A fuzz harness operates a production boundary, and it
        calls the public door.  A file that the build writes in test/ of the
        build directory, such as the roster of a test, can take a row.
    Each problem of the census of the runner is an error too.

REVIEW RULE FOR THE ADMITTED WALKS
    A file with a row of a reviewed walk does not publish a reflection of a
    nonstatic data member through a different path.  A public constexpr
    verdict with a field that names a private member opens the door again.
    The reviewer of each row does this check.

Usage
    guard check --rule owners --build-dir DIR [--warnings-dir DIR]
    guard write --rule owners --build-dir DIR [--raise | --admit] [--reason TEXT]

Complexity: linear in the finding lines of the build, times the items of the scope, and linear in the rows.
"""

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import ruletable
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "owners"
# The kinds of the uses of the two doors, which count only in the trees of production code (THE SCOPE).
PRODUCTION_KINDS = frozenset({"door-use", "testing-use"})
PRODUCTION_TREES = (
    "include/foundation/",
    "include/fixy/",
    "include/crucible/",
    "src/",
    "vessel/",
    "utils/tools/",
    "examples/",
)
DETAIL_KIND = "detail-use"
# The kinds that no ledger row admits (assay.guard.engines.meta.runner, THE REFUSED KINDS).
REFUSED_KINDS = frozenset({"lifetime-start", "open-static-member", "walked-return"})
# The files of TEST_TREE can take a row of the kind detail-use, and the files of FUZZ_TREE, in TEST_TREE, cannot.
TEST_TREE = "test/"
FUZZ_TREE = "test/fuzz/"
# The prefix of the path of a file that the build writes (utils/tools/quarantine/plugin.h, THE FILES).
BUILD_PREFIX = "@build/"
ADVICE = "Use the public door, or give the use a ledger row with the reason"
ACTIONS = {
    "lifetime-start": "the code starts the lifetime of an object in a file other than foundation/Lifetime.h.  An "
    "array of proofs, an aggregate that holds a proof and an array start can each give a proof with no lifetime.  "
    "Call foundation::lifetime::start_as_array",
    "door-use": "a use of the owner of a production door.  The owner gives a context with the effects of process "
    "startup or of a background thread.  Name it only in a reviewed process entry point or in the reviewed entry "
    "function of a background thread, and give that use a ledger row with its reason",
    "testing-use": "a use of the testing door in code that ships.  The door mints a context with no key.  Code that "
    "ships takes its context from a real mint.  A self-test in a header can use the door: give its use a ledger row "
    "with the reason",
    "detail-use": "a use of a detail namespace of the base outside the base.  A detail function has no gate of its "
    "own.  Call the public door of the base.  A test file can name a detail namespace with a ledger row that gives "
    "the reason",
    "unchecked-use": f"a use of access_context::unchecked(), the door to the private state of each proof type.  "
    f"{ADVICE}",
    "access-alias": f"a second name of access_context, which hides a use of unchecked() behind it.  {ADVICE}",
    "access-reflection": f"a reflection of access_context or of a namespace that holds it, which reaches unchecked() "
    f"with no name.  {ADVICE}",
    "member-splice": f"a splice that names a member of an object, which writes a private member with no access "
    f"check.  {ADVICE}",
    "open-static-member": "a writable private or protected static data member, which a splice writes with no "
    "object.  Make the member const or constexpr, or move it into the object",
    "walked-return": "a function that uses unchecked() and gives its caller a member that a walk found, which the "
    "caller can splice with no door of its own",
}


def is_counted(item: plugin_sections.Finding) -> bool:
    """Return True when the rule counts a finding line: a line of the two door kinds counts only in PRODUCTION_TREES."""
    return item.kind not in PRODUCTION_KINDS or any(ruletable.holds(tree, item.file) for tree in PRODUCTION_TREES)


def is_test_path(path: str) -> bool:
    """Return True when a file can take a row of the kind detail-use: a file in test/ and not in test/fuzz/."""
    tested = path.removeprefix(BUILD_PREFIX)
    return tested.startswith(TEST_TREE) and not tested.startswith(FUZZ_TREE)


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    action = ACTIONS.get(item.kind, "a use of a name outside the sites of its owner row")
    return f"{action} ({item.entity})"


def row_error(entry: rule_ledger.Entry, words: str) -> check_report.Finding:
    """Return the error of one row of the ledger, at its line."""
    return check_report.Finding("error", rule_ledger.shown_path(RULE), entry.line, RULE, words)


def row_errors(rows: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of a refused kind, and for each row of the kind detail-use outside test/.

    names.scoped_errors gives the error of a row with no reason.  Complexity: linear in the rows.
    """
    errors: list[check_report.Finding] = []
    for key, entry in sorted(rows.base.items()):
        if key.kind in REFUSED_KINDS:
            errors.append(
                row_error(
                    entry,
                    f"the row of {key.path} admits a finding of the kind {key.kind} ({key.entity}), and no row can "
                    f"admit one.  Remove the use, and remove the row",
                )
            )
        elif key.kind == DETAIL_KIND and not is_test_path(key.path):
            errors.append(
                row_error(
                    entry,
                    f"the row of {key.path} admits a use of a detail namespace, and only a file under {TEST_TREE} "
                    f"outside {FUZZ_TREE} can take a row.  Call the public door of the base, and remove the row",
                )
            )
    return errors


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each counted use outside the sites of its owner row, each bad row, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message, row_errors, is_counted)
