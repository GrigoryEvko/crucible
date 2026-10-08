// The shared part of the quarantine plugin of GCC: the files of the source
// root, the places, the findings, the rule table and the state of one unit.
// Each source of the plugin includes this header first.
//
// THE SOURCES
//     Quarantine.cmake builds crucible_quarantine.so from these sources.  Each
//     rule family of the plugin has one source:
//         plugin.cpp       the arguments, the hooks of the unit and the report
//         rule_table.cpp   the reader of the rule table and of the list of
//                          the headers that cannot compile alone, on rows.h
//         regions.cpp      the opt-out regions and their two pragmas
//         library.cpp      the library entities and the admitted list
//         owner.cpp        the family owner: each named reference of the code,
//                          and the rules start-lifetime and syscall-capability
//         declaration.cpp  the family declaration property: the walk of the
//                          namespaces, the classes and the templates
//         expression.cpp   the family expression shape: the walk of a body
//         include.cpp      the family include closure: the include rules, the
//                          seals, the heavy headers, the names of a higher
//                          root and the provenance of each place
//         unit.cpp         the family unit fact: the contract semantic of the
//                          unit and the optimize options of each function
//         home.cpp         the family home: where a definition or a
//                          specialization of an entity stands
//         contract.cpp     the contract rule
//
// THE RULE TABLE
//     utils/rule-table.txt gives the layers of the base, the headers that
//     each layer can include, the door of each system header, the admitted
//     entities of the standard library, the quarantined directories, the
//     enforce mode of each path and the files of the language.  Its head
//     comment gives the rows, and utils/assay/core/ruletable.py reads the
//     same rows in Python.  The plugin takes the table as rules=PATH.
//
// THE FILES
//     Each file has one class, from its real path:
//         base          a file that a layer row of the table holds
//         quarantined   a file that a quarantine row of the table holds
//         unclassified  a file under the source root or in the build
//                       directory that no row holds
//         outside       a system header, a file outside the source root, or
//                       a scratch buffer
//     The longest path of a layer row or a quarantine row decides.  The
//     plugin gives an error for each unclassified file that a unit reads.
//     The contract rule and the opt-out regions apply to each file under the
//     source root and in the build directory.  The quarantine rule applies to
//     a quarantined file only, and its rule of the library names also applies
//     to a base file of the language.
//     The plugin takes the build directory as build=PATH.  A file that the
//     build writes there, such as a generated header or a unit that xxd
//     writes, has the path kBuildPrefix and its path inside the build
//     directory, such as @build/test/layer/walk_headers/walk_headers.h.  So a
//     generated file has the same name in each build directory, also in one
//     outside the source root, and a quarantine row with that prefix holds it.
//     The real path decides the class, and the system flag of a line map
//     does not: a file under the root that -isystem or #pragma GCC
//     system_header marks is not a library file (in_library_file_at).  A file
//     under the root holds no #line directive, no line marker and no #pragma
//     GCC system_header, because each one changes the file, the line or the
//     system flag of the code after it (include.cpp, THE PROVENANCE).
//
// THE RULES AND THE KINDS
//     Each finding has a rule and a kind.  The registry utils/registry.txt
//     gives each rule of the engine plugin with its kinds, and the rule
//     plugin-kinds compares those kinds with the table kRuleKinds below.
//     The rule quarantine:
//     std-entity            a named reference to a declaration in namespace std.
//                           A typedef or an alias template of namespace std that
//                           the type of a declaration names is one too, such as
//                           std::size_t
//     replace-pending:ENTRY a use that the admit row of ENTRY admits, when the
//                           row has `until FAMILY`, such as std::bit_cast.  Each
//                           such row has a kind of its own, so the ratchet holds
//                           the uses of each entry in each file and a new use of
//                           one entry cannot pass behind a removed use of another
//     std-object            a variable, data member, parameter or return type whose
//                           type holds a class or an enumeration of the standard
//                           library, also through a typedef of fixy, such as
//                           std::memory_order
//     c-library-object      the same, for a struct, union or enumeration in the
//                           global namespace of a library header: a header
//                           outside the source root, such as a C header or the
//                           header of a third-party library that -I names
//     raw-pointer-object    a variable, data member or parameter of type T*
//     raw-function-pointer  the same, for a pointer to a function or to a member
//     c-array-object        a variable or data member of type T[N]
//     raw-new-delete        a new-expression or a delete-expression, and a
//                           declaration of an allocation function that is not
//                           deleted, such as a replacement of operator new
//     c-library-call        a use of a function or a variable in the global
//                           namespace of a library header, such as memcpy or
//                           optind, and a call of a builtin that has a function
//                           of the C library behind it, such as
//                           __builtin_memcpy.  A use of a foreign symbol of the
//                           tree is one too: a function or a variable that a
//                           file of the root declares with an asm label or the
//                           attribute weakref, or with C language linkage when
//                           the unit does not define it
//     foreign-symbol        the declaration of such a foreign symbol in a
//                           quarantined file, such as extern "C" void abort()
//                           or a function with __asm__("memcpy")
//     compiler-builtin      a call of each other builtin of GCC that the source
//                           spells, such as __builtin_trap or __atomic_load_n,
//                           __builtin_bit_cast and va_arg
//     inline-asm            an asm statement, and a variable in a register
//                           that an asm label names
//     assert-expansion      an expansion of the macro assert of a system header.
//                           libcpp reports the expansion, so the finding is the
//                           same with and without NDEBUG.  A token that the
//                           definition of assert spells, such as __assert_fail
//                           or __builtin_FILE, is no finding of its own
//     layer-header          an include of a header outside the root that the
//                           allow rows do not give the layer of the base file
//     door-header           an include of a door header by a file that is not
//                           its door
//     upward-include        an include of a header of a higher layer, or of a
//                           file outside the base, by a base file
//     opted-out             a finding of each kind of each rule, inside a region
//                           that CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason")
//                           opens.  Its entity is the kind and the entity of the
//                           finding inside the region
//     region                no finding: each region of a file under the root,
//                           with its reason, so that the ledger of the rule
//                           counts the regions of each file and reason
//     The rule contract-specifier:
//     contract-specifier    a `pre` or a `post` contract specifier (in each file
//                           under the root and in each mode)
//     The rule detsafe-ledger (the seal rows, include.cpp):
//     sealed-include        an #include of a file under a sealed PREFIX by a
//                           file of the include closure of its seal ROOT
//     sealed-name           a declaration of the namespace of a sealed PREFIX
//                           in a file of that closure, or a reference to a
//                           declaration of that namespace that such a file
//                           spells
//     The rule heavy-include (the heavy and light rows, include.cpp):
//     heavy-include         an include of a header with a heavy row by a file
//                           under include/
//     unmeasured-header     an include of a header outside the root that no
//                           heavy row and no light row gives, by such a file
//     The rule layer-boundary (include.cpp):
//     upward-name           a declaration of the namespace of a higher project
//                           root in a base file, or a reference to such a
//                           declaration that a base file spells
//     The rule contract-semantic (unit.cpp, in each mode):
//     contract-semantic     a unit whose contract semantic is not ignore and
//                           whose command line defines
//                           CRUCIBLE_CONTRACT_SEMANTIC_IGNORE, or the reverse
//     The rule fast-math-override (unit.cpp, in each mode):
//     fast-math-override    a function of a file under the root whose optimize
//                           attribute or GCC optimize pragma turns on a
//                           fast-math option that the command line does not
//                           turn on
//     The rules of the families declaration property (declaration.cpp, THE
//     FAMILY) and expression shape (expression.cpp) read each quarantined
//     file, and a base file in its sentinel unit (THE SENTINEL).  A finding of
//     such a rule is an error only in the mode error, no opt-out region covers
//     it, and the module of its rule judges it against the ledger of the rule
//     (is_family_kind).  A record is no finding (is_record_kind).
//     The rule mint-pattern:
//     mint                  no finding: one record of each mint, with its facts,
//                           for the inventory of the mints
//     mint-alias            no finding: one record of each alias of a class
//                           with a member mint in a file of include/, for the
//                           count of the fixtures of the mint
//     mint-not-nodiscard    a mint with no [[nodiscard]]
//     mint-not-constexpr    a mint that is not constexpr or consteval, and that
//                           is no borrow projection
//     mint-not-noexcept     a mint with no noexcept
//     mint-unconstrained    a mint template with no constraint
//     mint-unfit-context    a mint that takes a context first, and no constraint
//                           of which names that context
//     The rule no-raw-array-member:
//     array-member          a data member of a C array type
//     array-alias           an alias or a typedef of a C array type
//     The rule permission-storage:
//     permission-member     a data member whose type names a Permission, with no
//                           [[no_unique_address]]
//     static-permission     a static data member whose type names a Permission
//     The rule escape-doors:
//     bare-door             a public function that returns a reference or a raw
//                           pointer, and is no accessor, no mint and no hatch
//     hatch                 such a function that is a hatch, such as into_raw
//     The rule stub-discipline:
//     marker-without-stub   a file with an honesty marker and no deprecation
//                           CRUCIBLE_STUB, at its first marker
//     stub-without-marker   a file with a deprecation CRUCIBLE_STUB and no
//                           honesty marker, at its first deprecation
//     The rule process-wide-state:
//     unmarked-static       an object with static or thread storage duration
//                           that can change, with no CRUCIBLE_PROCESS_WIDE
//     marker-in-template    a marked object of a template
//     marker-internal-linkage
//                           a marked object with internal linkage
//     marker-not-inline     a marked object whose variable or function is not
//                           inline
//     marker-without-object a marked declaration that holds no such object
//     The rule lifetime-twin:
//     missing-twin          a parameter that CRUCIBLE_LIFETIMEBOUND marks, with
//                           no deleted overload that refuses a temporary there
//     The rule proof-routes (expression.cpp, THE PROOF ROUTES):
//     union                 a union definition
//     pointer-cast          a cast to a pointer or a reference to a proof type
//     allocator             an allocator of the library of a proof type
//     function-specialization
//                           an explicit specialization of a function template
//     member-pointer-specialization
//                           an explicit specialization whose template arguments
//                           hold a pointer to a member
//     friend-template-specialization
//                           a specialization of a class template that a class
//                           of include/ befriends, outside the file that
//                           defines the template
//     The rule witness-roster:
//     unrostered-witness    a class of a base file with a door shape and no
//                           witness row of the rule table
//     The rule padded-list:
//     padded-alias          an alias with a type that holds a fixed list of
//                           more elements than the argument
//                           padded-list-limit, and the element type has a
//                           padding bit.  The entity ends with the count and
//                           the padding bits of the list
//     padded-member         the same, for a variable or a data member
//     padded-return         the same, for the return type of a function
//     padded-parameter      the same, for a parameter that a function takes by
//                           value
//     The rule refined-pre-subsumption (expression.cpp, THE REFINED PARAMETERS):
//     retest                a precondition that tests again the predicate of a
//                           refined parameter
//     The rule syscall-grant-coverage (expression.cpp, THE GRANTS):
//     not-granted           a system call that the grant file issues and that
//                           its grant alias does not name
//     not-issued            a system call that the grant alias names and that
//                           the grant file does not issue
//     wrong-helper          a helper NAME_sys whose syscall() number is not
//                           SYS_NAME
//     unread-grant          a grant file whose alias, enumeration, grants or
//                           calls the plugin cannot read
//     compared              no finding: the unit compared the two sets
//     The rules of the family home (home.cpp, THE FAMILY) read where a
//     definition or a specialization stands.  A finding of such a rule is an
//     error only in the mode error, no opt-out region covers it, and the
//     module of its rule judges it against the ledger of the rule
//     (is_family_kind).  Each rule has the kinds:
//     outside-home          a definition or a specialization of an entity
//                           outside the paths of its home row (the rules
//                           federation-admission, host-owners,
//                           no-combine-ids-duplicate and trait-guard)
//     at-home               no finding: one site that a path of a home row
//                           admits, so that the module of the rule finds a
//                           home row with no site
//     The rule no-combine-ids-duplicate also has:
//     second-body           a function named combine_ids outside its home, or a
//                           function or a lambda with the body of combine_ids
//     derived-name          a declaration with a name that holds combine_ids
//                           and more
//     The rule splits-orphan has away-from-arguments, a specialization that is
//     not in the file that defines each of its arguments, and at-home.  The
//     rule trait-guard also has open-read: a gate that reads a template that a
//     unit can specialize.
//     The rules of the family owner (owner.cpp, THE OWNER ROWS and THE CLAIMS)
//     read where the code uses an entity.  A finding of such a rule is an
//     error only in the mode error, no opt-out region covers it, and the
//     module of its rule judges it against the ledger of the rule
//     (is_family_kind).  The rule start-lifetime:
//     lifetime-start        a use of an entity of an owner row of the rule
//                           outside the sites of the row
//     The rule syscall-capability:
//     raw-syscall           a call of a system call of a syscall row, or a call
//                           of syscall() with the number of a macro SYS_NAME,
//                           in a file of the claim trees
//     outside-door          a use of an entity of an owner row of the rule
//                           outside the sites of the row
//
// THE SENTINEL
//     The argument sentinel=PATH names a header of include/, relative to the
//     source root.  Only test/layer/CMakeLists.txt gives it, to the one unit
//     of the target all that is the sentinel of that header.  The family
//     declaration property reads the declarations of a base file, and the
//     family expression shape reads its bodies, only in the unit whose
//     sentinel names it, or in the base unit whose main file it is.  The
//     family home reads the gates of a header of include/ only in that unit
//     too (home.cpp, THE GATES).  The rules of the quarantine do not read a
//     base file.  A finding of a family rule in a base file counts only in
//     that unit.  So each base file costs one walk in each build.
//     The argument not-standalone=PATH names the list of the headers that
//     cannot compile alone, test/layer/crucible-not-standalone.txt.  Each row
//     is `HEADER | REASON`, with HEADER relative to include/.  No unit is the
//     sentinel of such a header, so the family home reads it in each unit
//     that includes it (home.cpp, THE SCOPE).
//
// THE SECTION
//     A unit that writes an object puts its report into the section
//     .crucible.quarantine of the object, in each mode (plugin.cpp).  The
//     first line is "# crucible-guard 2 stamp=TEXT", and each other line is
//     "RULE KIND PATH:LINE:COLUMN ENTITY".  utils/assay/readers/sections.py
//     reads the section.
//
// Each source includes this header before any other header, because the
// headers of GCC rename some functions of the C library: the standard headers
// and the system headers that the plugin uses come first, here.

#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "cp/cp-tree.h"
#include "cp/contracts.h"
#include "c-family/c-pragma.h"
#include "diagnostic-core.h"
#include "diagnostic.h"
#include "diagnostics/file-cache.h"
#include "input.h"
#include "stringpool.h"
#include "attribs.h"
#include "incpath.h"
#include "output.h"
#include "tree-iterator.h"

namespace crucible_plugin {

// ── The files, the places and the findings ──────────────────────────────

// The bound of each walk of a macro expansion, and of each walk of a type or
// of a chain of scopes.  The type walks keep each answer, so they stay linear,
// and the bounds only guard the stack.  The source can nest a type or a macro
// deeper than a small bound, so a bound must not hide a finding.  A type walk
// that reaches its bound fails closed.
inline constexpr int kMacroDepth = 1 << 16;
inline constexpr int kTypeDepth = 4096;

// The class of a file, as THE FILES above gives it.
enum class FileClass : std::uint8_t {
    outside,
    base,
    quarantined,
    unclassified,
};

// The prefix of the path of a file of the build directory.  The rule table
// and each report name such a file by this prefix and its path inside the
// build directory.  Only a quarantine row and an enforce row can hold such a
// path, because the build writes no base file.
inline constexpr const char* kBuildPrefix = "@build/";

struct GrantRow;

struct FileEntry {
    FileClass file_class = FileClass::outside;
    std::string relative;  // empty when the file is outside the root
    std::string real;  // the real path, empty for a scratch buffer
    int layer = -1;  // the index of the layer row of a base file
    bool is_error_mode = false;  // the enforce mode of the file is error
    // The index of the layer of the language row of a base file of the
    // language, whose allowance caps the outside includes, or -1.
    int language_layer = -1;
    // A base file that the argument sentinel names, or the base main file of
    // the unit (THE SENTINEL).
    bool is_sentinel = false;
    // A header of include/ that the list of the argument not-standalone names.
    // It cannot compile alone, so no unit is its sentinel (THE SENTINEL).
    bool cannot_compile_alone = false;
    // The grant row whose grant file this file is, or null, and the grant row
    // whose door file this file is, or null (expression.cpp, THE GRANTS).
    const GrantRow* grant = nullptr;
    const GrantRow* grant_door = nullptr;
};

// The files that can own a place.
enum class Scope : std::uint8_t {
    quarantine,  // a base or a quarantined file: the place of a finding of the quarantine rule
    tree,  // each file under the root: the place of a pragma of an opt-out region
};

inline bool owns_place(FileClass file_class, Scope scope) {
    switch (file_class) {
        case FileClass::base:
        case FileClass::quarantined:
            return true;
        case FileClass::unclassified:
            return scope == Scope::tree;
        case FileClass::outside:
            return false;
    }
    return false;
}

// ── The rule table ──────────────────────────────────────────────────────

struct LayerRow {
    std::string name;
    int rank = 0;
    std::vector<std::string> paths;
};

struct DoorRow {
    std::string header;  // the name inside the angle brackets
    std::string owner;
};

struct EnforceRow {
    std::string path;
    bool is_error = false;
};

struct LanguageRow {
    int layer = -1;  // the index of the layer whose allowance caps the outside includes
    std::vector<std::string> paths;
};

// The kind of a use that a row with `until FAMILY` admits starts with this
// text, and the entry of the row as the table spells it follows.
inline constexpr const char* kPendingKindPrefix = "replace-pending:";

// One admit row and its restrictions, as the head comment of the table gives
// them.  A row with `until FAMILY` admits each use of its entry with a finding
// of its own kind, so the ratchet holds the count of the uses of each such
// entry in each file.
struct AdmitRow {
    std::string entry;  // a qualified name, or the name inside the angle brackets of a header
    bool is_header = false;
    std::string until;  // the family that replaces the entry, or empty
    std::string pending_kind;  // kPendingKindPrefix and the spelled entry, for a row with `until`
    int arity = -1;  // the parameter count of each admitted function, or -1
    bool is_concepts_only = false;
    bool is_parameters_only = false;
    bool is_plain_result = false;  // the result type of the function holds no bool and no enumeration
    bool is_consteval_only = false;  // the entity is a consteval-only class or a member of one
    std::string unless_macro;
    std::vector<std::string> in_paths;
};

// One seal row: the include closure of ROOT holds no file under a PREFIX, and
// no file of that closure names the namespace of a PREFIX.  The namespace of
// a PREFIX is its path after include/, with "::" for each '/', such as
// crucible::ledger for include/crucible/ledger/.  Each namespace keeps its
// names, outermost first.
struct SealRow {
    std::string root;
    std::vector<std::string> prefixes;
    std::vector<std::vector<std::string>> namespaces;
};

// The cost of a header outside the root, as a heavy or a light row gives it.
enum class HeaderCost : std::uint8_t {
    light,
    heavy,
};

// One cite row: the predicate class of a refinement, and the decide
// procedures that test that predicate (expression.cpp, THE REFINED
// PARAMETERS).
struct CiteRow {
    std::string predicate;  // the qualified name of the class, with no template arguments
    std::vector<std::string> procedures;
};

// One grant row: the grant file, the qualified name of its grant alias, the
// qualified name of the enumeration of the system calls, and the door files
// whose functions stand for the system calls of their bodies
// (expression.cpp, THE GRANTS).
struct GrantRow {
    std::string file;
    std::string alias;
    std::string enumeration;
    std::vector<std::string> doors;
};

// One witness row: a class that attests to a fact that it cannot see.  The
// rule witness-roster reads the name of the class (declaration.cpp, THE RULE
// witness-roster), and the rule proof-routes reads it as a proof type
// (expression.cpp, THE PROOF ROUTES).  The writer of the negative fixtures
// reads the header, the refusal and the expression of a forger
// (utils/assay/loop/derive/witness_fixtures.py).
struct WitnessRow {
    std::string header;  // the name inside the angle brackets
    std::string refusal;  // private, deleted or no-match
    std::string type;  // the class as a caller spells it, with its template arguments
    std::string name;  // the full name of the class, with no template arguments
};

// ── The rules and the kinds ─────────────────────────────────────────────

enum class Rule : std::uint8_t {
    quarantine,
    contract_specifier,
    detsafe_ledger,
    heavy_include,
    layer_boundary,
    contract_semantic,
    fast_math_override,
    mint_pattern,
    no_raw_array_member,
    permission_storage,
    escape_doors,
    stub_discipline,
    process_wide_state,
    lifetime_twin,
    witness_roster,
    proof_routes,
    padded_list,
    refined_pre_subsumption,
    syscall_grant_coverage,
    federation_admission,
    host_owners,
    no_combine_ids_duplicate,
    splits_orphan,
    trait_guard,
    start_lifetime,
    syscall_capability,
};

// One home row or home-args row of the family home (home.cpp, THE ROWS).  The
// entity is a qualified name, or one name for each namespace of that name.
struct HomeRow {
    Rule rule = Rule::trait_guard;
    std::string entity;
    bool is_one_name = false;  // the entity names each namespace of that name
    bool is_args = false;  // a home-args row
    std::vector<std::string> paths;
    int line = 0;
};

// The kinds of a finding, which the table below gives.
enum class Kind : std::uint8_t;

// One owner row of the family owner (owner.cpp, THE OWNER ROWS): a use of
// ENTITY stands only at a site of the row, and each other use is the kind KIND
// of the rule RULE.  The entity is a qualified name, or the name of a function
// of the global namespace.
struct OwnerRow {
    Rule rule = Rule::syscall_capability;
    Kind kind{};
    std::string entity;
    std::vector<std::string> paths;
    std::string function;  // the qualified name of the function of by=, or empty
    int line = 0;
};

// The rows of the rule table, in the order of the file.  A header keeps the
// name inside its angle brackets.
struct RuleTable {
    std::vector<LayerRow> layers;
    std::vector<std::pair<int, std::string>> allows;  // the layer index and the header
    std::vector<DoorRow> doors;
    std::vector<AdmitRow> admits;
    std::vector<std::string> quarantines;
    std::vector<EnforceRow> enforces;
    std::vector<LanguageRow> languages;
    std::vector<SealRow> seals;
    std::unordered_map<std::string, HeaderCost> header_costs;  // by the name inside the angle brackets
    std::vector<CiteRow> cites;
    std::vector<GrantRow> grants;
    std::vector<HomeRow> homes;
    std::vector<WitnessRow> witnesses;
    std::vector<OwnerRow> owners;
    std::vector<std::string> syscalls;  // each name of a syscall row (owner.cpp, THE CLAIMS)
};

// The kinds of a finding (THE RULES AND THE KINDS above).  opted-out and
// region are kinds of the report of a unit, and plugin.cpp writes them.
enum class Kind : std::uint8_t {
    std_entity,
    std_object,
    c_library_object,
    raw_pointer_object,
    raw_function_pointer,
    c_array_object,
    raw_new_delete,
    c_library_call,
    foreign_symbol,
    compiler_builtin,
    inline_asm,
    assert_expansion,
    layer_header,
    door_header,
    upward_include,
    replace_pending,
    contract_specifier,
    sealed_include,
    sealed_name,
    heavy_include,
    unmeasured_header,
    upward_name,
    contract_semantic,
    fast_math_override,
    mint_record,
    mint_alias,
    mint_not_nodiscard,
    mint_not_constexpr,
    mint_not_noexcept,
    mint_unconstrained,
    mint_unfit_context,
    array_member,
    array_alias,
    permission_member,
    static_permission,
    bare_door,
    hatch,
    marker_without_stub,
    stub_without_marker,
    unmarked_static,
    marker_in_template,
    marker_internal_linkage,
    marker_not_inline,
    marker_without_object,
    missing_twin,
    unrostered_witness,
    route_union,
    route_pointer_cast,
    route_allocator,
    route_function_specialization,
    route_member_pointer_specialization,
    route_friend_template_specialization,
    padded_alias,
    padded_member,
    padded_return,
    padded_parameter,
    refined_retest,
    grant_not_granted,
    grant_not_issued,
    grant_wrong_helper,
    grant_unread,
    grant_compared,
    // The family home (home.cpp): one kind for each kind of each rule.
    federation_outside_home,
    federation_at_home,
    owner_outside_home,
    owner_at_home,
    combine_outside_home,
    combine_second_body,
    combine_derived_name,
    combine_at_home,
    split_away_from_arguments,
    split_at_home,
    trait_outside_home,
    trait_open_read,
    trait_at_home,
    // The family owner (owner.cpp).
    lifetime_start,
    raw_syscall,
    outside_door,
};

inline const char* rule_name(Rule rule) {
    switch (rule) {
        case Rule::quarantine:
            return "quarantine";
        case Rule::contract_specifier:
            return "contract-specifier";
        case Rule::detsafe_ledger:
            return "detsafe-ledger";
        case Rule::heavy_include:
            return "heavy-include";
        case Rule::layer_boundary:
            return "layer-boundary";
        case Rule::contract_semantic:
            return "contract-semantic";
        case Rule::fast_math_override:
            return "fast-math-override";
        case Rule::mint_pattern:
            return "mint-pattern";
        case Rule::no_raw_array_member:
            return "no-raw-array-member";
        case Rule::permission_storage:
            return "permission-storage";
        case Rule::escape_doors:
            return "escape-doors";
        case Rule::stub_discipline:
            return "stub-discipline";
        case Rule::process_wide_state:
            return "process-wide-state";
        case Rule::lifetime_twin:
            return "lifetime-twin";
        case Rule::witness_roster:
            return "witness-roster";
        case Rule::proof_routes:
            return "proof-routes";
        case Rule::padded_list:
            return "padded-list";
        case Rule::refined_pre_subsumption:
            return "refined-pre-subsumption";
        case Rule::syscall_grant_coverage:
            return "syscall-grant-coverage";
        case Rule::federation_admission:
            return "federation-admission";
        case Rule::host_owners:
            return "host-owners";
        case Rule::no_combine_ids_duplicate:
            return "no-combine-ids-duplicate";
        case Rule::splits_orphan:
            return "splits-orphan";
        case Rule::trait_guard:
            return "trait-guard";
        case Rule::start_lifetime:
            return "start-lifetime";
        case Rule::syscall_capability:
            return "syscall-capability";
    }
    return "unknown";
}

// The rules of the family home, in the order of the enumeration.
inline constexpr Rule kHomeRules[] = {Rule::federation_admission, Rule::host_owners, Rule::no_combine_ids_duplicate,
                                      Rule::splits_orphan, Rule::trait_guard};

// The rules of the family owner, in the order of the enumeration.
inline constexpr Rule kOwnerRules[] = {Rule::start_lifetime, Rule::syscall_capability};

inline const char* kind_name(Kind kind) {
    switch (kind) {
        case Kind::std_entity:
            return "std-entity";
        case Kind::std_object:
            return "std-object";
        case Kind::c_library_object:
            return "c-library-object";
        case Kind::raw_pointer_object:
            return "raw-pointer-object";
        case Kind::raw_function_pointer:
            return "raw-function-pointer";
        case Kind::c_array_object:
            return "c-array-object";
        case Kind::raw_new_delete:
            return "raw-new-delete";
        case Kind::c_library_call:
            return "c-library-call";
        case Kind::foreign_symbol:
            return "foreign-symbol";
        case Kind::compiler_builtin:
            return "compiler-builtin";
        case Kind::inline_asm:
            return "inline-asm";
        case Kind::assert_expansion:
            return "assert-expansion";
        case Kind::layer_header:
            return "layer-header";
        case Kind::door_header:
            return "door-header";
        case Kind::upward_include:
            return "upward-include";
        case Kind::replace_pending:
            return "replace-pending";
        case Kind::contract_specifier:
            return "contract-specifier";
        case Kind::sealed_include:
            return "sealed-include";
        case Kind::sealed_name:
            return "sealed-name";
        case Kind::heavy_include:
            return "heavy-include";
        case Kind::unmeasured_header:
            return "unmeasured-header";
        case Kind::upward_name:
            return "upward-name";
        case Kind::contract_semantic:
            return "contract-semantic";
        case Kind::fast_math_override:
            return "fast-math-override";
        case Kind::mint_record:
            return "mint";
        case Kind::mint_alias:
            return "mint-alias";
        case Kind::mint_not_nodiscard:
            return "mint-not-nodiscard";
        case Kind::mint_not_constexpr:
            return "mint-not-constexpr";
        case Kind::mint_not_noexcept:
            return "mint-not-noexcept";
        case Kind::mint_unconstrained:
            return "mint-unconstrained";
        case Kind::mint_unfit_context:
            return "mint-unfit-context";
        case Kind::array_member:
            return "array-member";
        case Kind::array_alias:
            return "array-alias";
        case Kind::permission_member:
            return "permission-member";
        case Kind::static_permission:
            return "static-permission";
        case Kind::bare_door:
            return "bare-door";
        case Kind::hatch:
            return "hatch";
        case Kind::marker_without_stub:
            return "marker-without-stub";
        case Kind::stub_without_marker:
            return "stub-without-marker";
        case Kind::unmarked_static:
            return "unmarked-static";
        case Kind::marker_in_template:
            return "marker-in-template";
        case Kind::marker_internal_linkage:
            return "marker-internal-linkage";
        case Kind::marker_not_inline:
            return "marker-not-inline";
        case Kind::marker_without_object:
            return "marker-without-object";
        case Kind::missing_twin:
            return "missing-twin";
        case Kind::unrostered_witness:
            return "unrostered-witness";
        case Kind::route_union:
            return "union";
        case Kind::route_pointer_cast:
            return "pointer-cast";
        case Kind::route_allocator:
            return "allocator";
        case Kind::route_function_specialization:
            return "function-specialization";
        case Kind::route_member_pointer_specialization:
            return "member-pointer-specialization";
        case Kind::route_friend_template_specialization:
            return "friend-template-specialization";
        case Kind::padded_alias:
            return "padded-alias";
        case Kind::padded_member:
            return "padded-member";
        case Kind::padded_return:
            return "padded-return";
        case Kind::padded_parameter:
            return "padded-parameter";
        case Kind::refined_retest:
            return "retest";
        case Kind::grant_not_granted:
            return "not-granted";
        case Kind::grant_not_issued:
            return "not-issued";
        case Kind::grant_wrong_helper:
            return "wrong-helper";
        case Kind::grant_unread:
            return "unread-grant";
        case Kind::grant_compared:
            return "compared";
        case Kind::federation_outside_home:
        case Kind::owner_outside_home:
        case Kind::combine_outside_home:
        case Kind::trait_outside_home:
            return "outside-home";
        case Kind::federation_at_home:
        case Kind::owner_at_home:
        case Kind::combine_at_home:
        case Kind::split_at_home:
        case Kind::trait_at_home:
            return "at-home";
        case Kind::combine_second_body:
            return "second-body";
        case Kind::combine_derived_name:
            return "derived-name";
        case Kind::split_away_from_arguments:
            return "away-from-arguments";
        case Kind::trait_open_read:
            return "open-read";
        case Kind::lifetime_start:
            return "lifetime-start";
        case Kind::raw_syscall:
            return "raw-syscall";
        case Kind::outside_door:
            return "outside-door";
    }
    return "unknown";
}

// The rule of each kind: the kinds of THE RULES AND THE KINDS above that a
// rule other than the quarantine rule lists, and the quarantine rule for each
// other kind.
inline Rule rule_of(Kind kind) {
    switch (kind) {
        case Kind::contract_specifier:
            return Rule::contract_specifier;
        case Kind::sealed_include:
        case Kind::sealed_name:
            return Rule::detsafe_ledger;
        case Kind::heavy_include:
        case Kind::unmeasured_header:
            return Rule::heavy_include;
        case Kind::upward_name:
            return Rule::layer_boundary;
        case Kind::contract_semantic:
            return Rule::contract_semantic;
        case Kind::fast_math_override:
            return Rule::fast_math_override;
        case Kind::mint_record:
        case Kind::mint_alias:
        case Kind::mint_not_nodiscard:
        case Kind::mint_not_constexpr:
        case Kind::mint_not_noexcept:
        case Kind::mint_unconstrained:
        case Kind::mint_unfit_context:
            return Rule::mint_pattern;
        case Kind::array_member:
        case Kind::array_alias:
            return Rule::no_raw_array_member;
        case Kind::permission_member:
        case Kind::static_permission:
            return Rule::permission_storage;
        case Kind::bare_door:
        case Kind::hatch:
            return Rule::escape_doors;
        case Kind::marker_without_stub:
        case Kind::stub_without_marker:
            return Rule::stub_discipline;
        case Kind::unmarked_static:
        case Kind::marker_in_template:
        case Kind::marker_internal_linkage:
        case Kind::marker_not_inline:
        case Kind::marker_without_object:
            return Rule::process_wide_state;
        case Kind::missing_twin:
            return Rule::lifetime_twin;
        case Kind::unrostered_witness:
            return Rule::witness_roster;
        case Kind::route_union:
        case Kind::route_pointer_cast:
        case Kind::route_allocator:
        case Kind::route_function_specialization:
        case Kind::route_member_pointer_specialization:
        case Kind::route_friend_template_specialization:
            return Rule::proof_routes;
        case Kind::padded_alias:
        case Kind::padded_member:
        case Kind::padded_return:
        case Kind::padded_parameter:
            return Rule::padded_list;
        case Kind::refined_retest:
            return Rule::refined_pre_subsumption;
        case Kind::grant_not_granted:
        case Kind::grant_not_issued:
        case Kind::grant_wrong_helper:
        case Kind::grant_unread:
        case Kind::grant_compared:
            return Rule::syscall_grant_coverage;
        case Kind::federation_outside_home:
        case Kind::federation_at_home:
            return Rule::federation_admission;
        case Kind::owner_outside_home:
        case Kind::owner_at_home:
            return Rule::host_owners;
        case Kind::combine_outside_home:
        case Kind::combine_second_body:
        case Kind::combine_derived_name:
        case Kind::combine_at_home:
            return Rule::no_combine_ids_duplicate;
        case Kind::split_away_from_arguments:
        case Kind::split_at_home:
            return Rule::splits_orphan;
        case Kind::trait_outside_home:
        case Kind::trait_open_read:
        case Kind::trait_at_home:
            return Rule::trait_guard;
        case Kind::lifetime_start:
            return Rule::start_lifetime;
        case Kind::raw_syscall:
        case Kind::outside_door:
            return Rule::syscall_capability;
        default:
            return Rule::quarantine;
    }
}

// True for a kind whose finding is an error in each mode: the contract rule and
// the unit facts.  No enforce mode changes such a finding.
inline bool is_each_mode_kind(Kind kind) {
    return kind == Kind::contract_specifier || kind == Kind::contract_semantic || kind == Kind::fast_math_override;
}

// True when a rule of the families declaration property (declaration.cpp),
// expression shape (expression.cpp), owner (owner.cpp) and home (home.cpp)
// gives the kind.  Such a finding is an error only in the mode error, no
// opt-out region covers it, and no enforce mode changes it.  The module of its
// rule judges it against the ledger of the rule.  plugin.cpp reports each
// such line in one way (report_family).
inline bool is_family_kind(Kind kind) {
    switch (rule_of(kind)) {
        case Rule::mint_pattern:
        case Rule::no_raw_array_member:
        case Rule::permission_storage:
        case Rule::escape_doors:
        case Rule::stub_discipline:
        case Rule::process_wide_state:
        case Rule::lifetime_twin:
        case Rule::witness_roster:
        case Rule::proof_routes:
        case Rule::padded_list:
        case Rule::refined_pre_subsumption:
        case Rule::syscall_grant_coverage:
        case Rule::federation_admission:
        case Rule::host_owners:
        case Rule::no_combine_ids_duplicate:
        case Rule::splits_orphan:
        case Rule::trait_guard:
        case Rule::start_lifetime:
        case Rule::syscall_capability:
            return true;
        default:
            return false;
    }
}

// True when a line of a family kind is a record: a fact for a reader of the
// records, and no finding of its rule.  The record of a mint, the record of an
// alias of a class with a member mint, the line of a grant set that the unit
// compared, and a site that a home row admits (at-home).
inline bool is_record_kind(Kind kind) {
    switch (kind) {
        case Kind::mint_record:
        case Kind::mint_alias:
        case Kind::grant_compared:
        case Kind::federation_at_home:
        case Kind::owner_at_home:
        case Kind::combine_at_home:
        case Kind::split_at_home:
        case Kind::trait_at_home:
            return true;
        default:
            return false;
    }
}

// The two kinds of a line of the report that the rule quarantine gives for
// each rule: a finding inside an opt-out region, and the region itself.
inline constexpr const char* kOptedOutKind = "opted-out";
inline constexpr const char* kRegionKind = "region";

// One kind of one rule, for the table that plugin.cpp writes for the
// argument kinds=PATH.
struct RuleKind {
    const char* rule;
    const char* kind;
};

// Each kind that the plugin can give, with its rule.  A finding of the kind
// replace-pending has the entry of its admit row after the kind and a colon.
inline constexpr RuleKind kRuleKinds[] = {
    {"quarantine", "std-entity"},
    {"quarantine", "std-object"},
    {"quarantine", "c-library-object"},
    {"quarantine", "raw-pointer-object"},
    {"quarantine", "raw-function-pointer"},
    {"quarantine", "c-array-object"},
    {"quarantine", "raw-new-delete"},
    {"quarantine", "c-library-call"},
    {"quarantine", "foreign-symbol"},
    {"quarantine", "compiler-builtin"},
    {"quarantine", "inline-asm"},
    {"quarantine", "assert-expansion"},
    {"quarantine", "layer-header"},
    {"quarantine", "door-header"},
    {"quarantine", "upward-include"},
    {"quarantine", "replace-pending"},
    {"quarantine", "opted-out"},
    {"quarantine", "region"},
    {"contract-specifier", "contract-specifier"},
    {"detsafe-ledger", "sealed-include"},
    {"detsafe-ledger", "sealed-name"},
    {"heavy-include", "heavy-include"},
    {"heavy-include", "unmeasured-header"},
    {"layer-boundary", "upward-name"},
    {"contract-semantic", "contract-semantic"},
    {"fast-math-override", "fast-math-override"},
    {"mint-pattern", "mint"},
    {"mint-pattern", "mint-alias"},
    {"mint-pattern", "mint-not-nodiscard"},
    {"mint-pattern", "mint-not-constexpr"},
    {"mint-pattern", "mint-not-noexcept"},
    {"mint-pattern", "mint-unconstrained"},
    {"mint-pattern", "mint-unfit-context"},
    {"no-raw-array-member", "array-member"},
    {"no-raw-array-member", "array-alias"},
    {"permission-storage", "permission-member"},
    {"permission-storage", "static-permission"},
    {"escape-doors", "bare-door"},
    {"escape-doors", "hatch"},
    {"stub-discipline", "marker-without-stub"},
    {"stub-discipline", "stub-without-marker"},
    {"process-wide-state", "unmarked-static"},
    {"process-wide-state", "marker-in-template"},
    {"process-wide-state", "marker-internal-linkage"},
    {"process-wide-state", "marker-not-inline"},
    {"process-wide-state", "marker-without-object"},
    {"lifetime-twin", "missing-twin"},
    {"witness-roster", "unrostered-witness"},
    {"proof-routes", "union"},
    {"proof-routes", "pointer-cast"},
    {"proof-routes", "allocator"},
    {"proof-routes", "function-specialization"},
    {"proof-routes", "member-pointer-specialization"},
    {"proof-routes", "friend-template-specialization"},
    {"padded-list", "padded-alias"},
    {"padded-list", "padded-member"},
    {"padded-list", "padded-return"},
    {"padded-list", "padded-parameter"},
    {"refined-pre-subsumption", "retest"},
    {"syscall-grant-coverage", "not-granted"},
    {"syscall-grant-coverage", "not-issued"},
    {"syscall-grant-coverage", "wrong-helper"},
    {"syscall-grant-coverage", "unread-grant"},
    {"syscall-grant-coverage", "compared"},
    {"federation-admission", "outside-home"},
    {"federation-admission", "at-home"},
    {"host-owners", "outside-home"},
    {"host-owners", "at-home"},
    {"no-combine-ids-duplicate", "outside-home"},
    {"no-combine-ids-duplicate", "second-body"},
    {"no-combine-ids-duplicate", "derived-name"},
    {"no-combine-ids-duplicate", "at-home"},
    {"splits-orphan", "away-from-arguments"},
    {"splits-orphan", "at-home"},
    {"trait-guard", "outside-home"},
    {"trait-guard", "open-read"},
    {"trait-guard", "at-home"},
    {"start-lifetime", "lifetime-start"},
    {"syscall-capability", "raw-syscall"},
    {"syscall-capability", "outside-door"},
};

// True when the entity is part of the key of a finding: two findings of such
// a kind at one place, with different entities, are two findings.  Each other
// kind has one finding for each place.
inline bool is_name_kind(Kind kind) {
    switch (kind) {
        case Kind::std_entity:
        case Kind::c_library_call:
        case Kind::foreign_symbol:
        case Kind::compiler_builtin:
        case Kind::layer_header:
        case Kind::door_header:
        case Kind::upward_include:
        case Kind::replace_pending:
        case Kind::sealed_include:
        case Kind::sealed_name:
        case Kind::heavy_include:
        case Kind::unmeasured_header:
        case Kind::upward_name:
        case Kind::contract_semantic:
        case Kind::fast_math_override:
            return true;
        default:
            return is_family_kind(kind);
    }
}

inline bool is_include_kind(Kind kind) {
    return kind == Kind::layer_header || kind == Kind::door_header || kind == Kind::upward_include;
}

struct Place {
    FileClass file_class = FileClass::outside;
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    location_t spelling = UNKNOWN_LOCATION;
};

struct Finding {
    Kind kind = Kind::contract_specifier;
    // The admit row of a finding of the kind replace-pending, or null.
    const AdmitRow* pending = nullptr;
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    // The spelling location, or for a contract specifier the location of the
    // tree, so that a diagnostic also names the macro expansion.
    location_t spelling = UNKNOWN_LOCATION;
    std::string entity;
};

// The kind of a finding as the report spells it: the name of its kind, and
// for the kind replace-pending also the entry of its admit row.
inline std::string kind_text(const Finding& finding) {
    return finding.pending != nullptr ? finding.pending->pending_kind : std::string(kind_name(finding.kind));
}

struct Region {
    const FileEntry* file = nullptr;
    int begin_line = 0;
    int end_line = 0;  // zero while the region is open
    location_t begin = UNKNOWN_LOCATION;
    std::string reason;
};

// ── The state of one unit ───────────────────────────────────────────────

// An #include directive of a base or a quarantined file whose target the
// plugin does not know yet.
struct PendingInclude {
    bool is_set = false;
    Place place;  // the directive
    std::string spelled;  // the name inside the delimiters
    bool is_angle = false;
    bool is_next = false;  // an #include_next directive
};

// The library classes and enumerations that a type holds: the first one that
// no admit row admits, and the first one that a row with `until` admits, with
// that row.
struct LibraryClasses {
    tree refused = NULL_TREE;
    tree pending = NULL_TREE;
    const AdmitRow* pending_row = nullptr;
    // The refused class sits in a function type, which makes no object, so
    // the declaration names the class and holds no object of it.
    bool is_refused_name = false;

    // Keeps the first refused and the first pending class of the two.
    void add(const LibraryClasses& other) {
        if (refused == NULL_TREE) {
            refused = other.refused;
            is_refused_name = other.is_refused_name;
        }
        if (pending == NULL_TREE) {
            pending = other.pending;
            pending_row = other.pending_row;
        }
    }
    [[nodiscard]] bool is_full() const { return refused != NULL_TREE && pending != NULL_TREE; }
};

// One #include directive of a base or a quarantined file whose target the
// plugin knows: the file of the directive, its target and its place.
struct IncludeEdge {
    const FileEntry* from = nullptr;
    const FileEntry* to = nullptr;
    Place directive;
};

// A reference of the code to a declaration of a namespace of a seal row, at
// a place in a base or a quarantined file.  The report judges it when it
// knows the include closure of each seal root (include.cpp, THE SEALS).
struct SealedReference {
    Place place;
    std::string scope;  // the namespace of the declaration, such as crucible::ledger
    std::string entity;
};

// The facts of a namespace at the top of the global namespace, for each
// reference to a declaration in it (include.cpp, facts_of_top): the rank of
// its project root or -1, and whether a seal row names a namespace in it.
struct TopFacts {
    int rank = -1;
    bool is_sealed_root = false;
};

// A use of a function or a variable of C language linkage that a file of the
// root declares, at a place in a quarantined file.  The DECL_UID names the
// declaration, because a garbage collection can free a tree before the report.
struct ForeignUse {
    unsigned uid = 0;
    Place place;
    std::string entity;
};

using IncludeCallback = void (*)(cpp_reader*, location_t, const unsigned char*, const char*, int, const cpp_token**);
using MacroCallback = void (*)(cpp_reader*, location_t, cpp_hashnode*);
using FileChangeCallback = void (*)(cpp_reader*, const line_map_ordinary*);

// The library classes of a type for each use: the type of a parameter, or
// each other use.  An admit row with the restriction `parameters` admits its
// class only as the type of a parameter.
enum class Use : std::uint8_t {
    other,
    parameter,
};

// The state of one translation unit.
struct State {
    std::string plugin_name;
    std::string root;  // the real path of the source root, without a final '/'
    std::string build;  // the real path of the build directory, or empty
    // The header of include/ of the argument sentinel, relative to the root,
    // or empty, and the real path of the main file of the unit (THE SENTINEL).
    std::string sentinel;
    std::string main_real;
    // Each header of include/ that the list of the argument not-standalone
    // names, relative to the root, such as include/crucible/ledger/ProbeSupport.h
    // (THE SENTINEL).
    std::unordered_set<std::string> not_standalone_headers;
    bool was_reported = false;
    RuleTable table;

    std::string out_dir;
    std::string stamps_dir;
    std::string stamp;
    bool is_error_mode = false;
    // The threshold of the rule padded-list (the argument padded-list-limit of
    // plugin.cpp): a list of more elements than it gives a line, and a list of
    // fewer or the same number gives none (declaration.cpp, THE RULE
    // padded-list).
    std::uint64_t padded_list_limit = 0;

    // One entry for each real path.  The empty path holds the entry of each
    // scratch buffer.  A node of an unordered map keeps its address, so the
    // pointers of `files` and `unclassified` stay valid.
    std::unordered_map<std::string, FileEntry> entries;
    std::unordered_map<const char*, const FileEntry*> files;  // by the file name of a line map
    std::vector<const FileEntry*> unclassified;
    std::vector<Finding> findings;
    std::unordered_set<std::string> finding_keys;
    std::vector<Region> regions;

    // The admit row of each name entry, and of each header entry by its name.
    std::unordered_map<std::string, const AdmitRow*> admitted_names;
    std::unordered_map<std::string, const AdmitRow*> admitted_headers;
    // The admit row of each file that an #include of an admitted header
    // entered, by its real path, and the row of the last directive when it
    // names an admitted header.
    std::unordered_map<std::string, const AdmitRow*> admitted_header_paths;
    const AdmitRow* admitted_header_pending = nullptr;

    // The include rules.
    IncludeCallback previous_include = nullptr;
    PendingInclude pending;
    // The include closure (include.cpp, THE SEALS): each directive whose
    // target the plugin knows, and each reference to a declaration of a
    // sealed namespace.
    std::vector<IncludeEdge> include_edges;
    std::vector<SealedReference> sealed_references;
    // The facts of each top namespace that a reference named.  The key is a
    // tree, so the collection hook clears the cache.
    std::unordered_map<tree, TopFacts> top_facts;

    // The unit facts (unit.cpp): the command line defines
    // CRUCIBLE_CONTRACT_SEMANTIC_IGNORE, and the DECL_UID of each function
    // whose optimize options the plugin read.
    bool is_ignore_defined = false;
    std::unordered_set<unsigned> optimized_functions;

    // The foreign symbols: the DECL_UID of each function and variable of C
    // language linkage that the unit defines, and each use in a quarantined
    // file of such a symbol that a file of the root declares.  The report
    // judges each use when it knows each definition of the unit.
    std::unordered_set<unsigned> defined_c_symbols;
    std::vector<ForeignUse> foreign_uses;

    // The provenance: the callback of the front end, the start of the main
    // file, and the location of the last #include directive, which the entry
    // of its target follows.
    FileChangeCallback previous_file_change = nullptr;
    bool is_main_started = false;
    location_t include_directive = UNKNOWN_LOCATION;

    // The expansions of assert.
    MacroCallback previous_used = nullptr;

    // The family declaration property (declaration.cpp, THE FAMILY).
    // DECL_UID keys the facts of a body that PLUGIN_PRE_GENERICIZE reads,
    // because a garbage collection can free a tree before the walk.
    std::unordered_set<unsigned> defined_functions;  // each mint that the unit defines
    std::unordered_set<unsigned> projection_bodies;  // each mint whose body is a borrow projection

    // Caches keyed by tree.  A garbage collection can free a tree and reuse
    // its address, so each collection clears them.  Nothing depends on them
    // for correctness: a finding is recorded one time for each place.
    // The first three hold one map for each Use.  A type walk visits each
    // type one time with these maps, also when a type tree shares its
    // subtrees, so a walk stays linear in the distinct types.
    std::unordered_map<tree, LibraryClasses> library_class_of[2];
    // The answers in a file of the language, which has admit rows of its own.
    std::unordered_map<const FileEntry*, std::unordered_map<tree, LibraryClasses>> language_class_of[2];
    std::unordered_map<tree, const AdmitRow*> admitting_row_of[2];
    // The answer of holds_bool_or_enumeration for each class and union.
    std::unordered_map<tree, bool> holds_bool_of;
    // The locations of the local variables that a placeholder declares in
    // each function pattern (declaration.cpp, placeholder_locals_of).
    std::unordered_map<tree, std::unordered_set<location_t>> placeholder_locals;
    std::unordered_map<tree, bool> default_argument_of;
    std::unordered_set<tree> walked;
    // The trees that the walk of the contract rule visited.
    std::unordered_set<tree> contract_walked;
};

inline State state;

// ── Paths and places ────────────────────────────────────────────────────

inline bool has_prefix(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

inline std::string real_path(const char* path) {
    std::string resolved(PATH_MAX + 1, '\0');
    if (::realpath(path, resolved.data()) == nullptr) {
        return {};
    }
    resolved.resize(std::strlen(resolved.c_str()));
    return resolved;
}

// A path of a row holds a file: a directory holds each file under it, and a
// file path holds that file only.
inline bool row_holds(const std::string& path, const std::string& relative) {
    return !path.empty() && path.back() == '/' ? has_prefix(relative, path) : relative == path;
}

// The class, the layer and the enforce mode of a file of the root or of the
// build directory.  The longest path decides.  Complexity: O(rows).
inline void classify_by_table(FileEntry& entry) {
    std::size_t best_length = 0;
    bool is_found = false;
    for (std::size_t index = 0; index < state.table.layers.size(); ++index) {
        for (const std::string& path : state.table.layers[index].paths) {
            if (row_holds(path, entry.relative) && (!is_found || path.size() > best_length)) {
                entry.file_class = FileClass::base;
                entry.layer = static_cast<int>(index);
                best_length = path.size();
                is_found = true;
            }
        }
    }
    for (const std::string& path : state.table.quarantines) {
        if (row_holds(path, entry.relative) && (!is_found || path.size() > best_length)) {
            entry.file_class = FileClass::quarantined;
            entry.layer = -1;
            best_length = path.size();
            is_found = true;
        }
    }
    if (!is_found) {
        entry.file_class = FileClass::unclassified;
    }
    std::size_t language_length = 0;
    for (const LanguageRow& row : state.table.languages) {
        for (const std::string& path : row.paths) {
            if (entry.file_class == FileClass::base && row_holds(path, entry.relative)
                && path.size() > language_length) {
                entry.language_layer = row.layer;
                language_length = path.size();
            }
        }
    }
    std::size_t mode_length = 0;
    bool has_mode = false;
    for (const EnforceRow& row : state.table.enforces) {
        if (row_holds(row.path, entry.relative) && (!has_mode || row.path.size() > mode_length)) {
            entry.is_error_mode = row.is_error;
            mode_length = row.path.size();
            has_mode = true;
        }
    }
    for (const GrantRow& grant : state.table.grants) {
        if (grant.file == entry.relative) {
            entry.grant = &grant;
        }
        for (const std::string& door : grant.doors) {
            if (door == entry.relative) {
                entry.grant_door = &grant;
            }
        }
    }
    entry.is_sentinel =
        entry.file_class == FileClass::base
        && ((!state.sentinel.empty() && entry.relative == state.sentinel) || entry.real == state.main_real);
    entry.cannot_compile_alone = state.not_standalone_headers.count(entry.relative) != 0;
}

// True when RESOLVED is a file of the build directory.  A build directory that
// holds the source root, or is the root, names no generated file, because
// each file of the root would be one.
inline bool is_build_file(const std::string& resolved) {
    return !state.build.empty() && !resolved.empty() && has_prefix(resolved, state.build + "/")
        && !has_prefix(state.root + "/", state.build + "/");
}

// The entry of one real path, made and kept on the first request.  The first
// request also resolves the main file of the unit, so classify_by_table knows
// the base main file (THE SENTINEL).
inline const FileEntry& entry_of_real_path(const std::string& resolved) {
    auto found = state.entries.find(resolved);
    if (found != state.entries.end()) {
        return found->second;
    }
    if (state.main_real.empty() && main_input_filename != nullptr) {
        state.main_real = real_path(main_input_filename);
    }
    FileEntry entry;
    entry.real = resolved;
    if (is_build_file(resolved)) {
        entry.relative = std::string(kBuildPrefix) + resolved.substr(state.build.size() + 1);
        classify_by_table(entry);
    } else if (!resolved.empty() && has_prefix(resolved, state.root + "/")) {
        entry.relative = resolved.substr(state.root.size() + 1);
        classify_by_table(entry);
    }
    const FileEntry& stored = state.entries.emplace(resolved, std::move(entry)).first->second;
    if (stored.file_class == FileClass::unclassified) {
        state.unclassified.push_back(&stored);
    }
    return stored;
}

// The entry of the file name FILE.  The line map keeps one name string for
// each file, and successive calls usually ask one file, so the last name and
// its entry answer most calls before the map does.  An entry stays in
// state.entries to the end of the unit, so the last entry stays valid.
inline const FileEntry& classify_file(const char* file) {
    static const char* last_name = nullptr;
    static const FileEntry* last_entry = nullptr;
    if (file != nullptr && file == last_name) {
        return *last_entry;
    }
    const FileEntry* entry = nullptr;
    auto found = state.files.find(file);
    if (found != state.files.end()) {
        entry = found->second;
    } else {
        std::string resolved = file != nullptr ? real_path(file) : std::string{};
        entry = &entry_of_real_path(resolved);
        state.files.emplace(file, entry);
    }
    if (file != nullptr) {
        last_name = file;
        last_entry = entry;
    }
    return *entry;
}

// True when the token at LOCATION is spelled in a library file: a file outside
// the source root, also a header that -I names and no system flag marks, such
// as the header of a third-party library.  A file under the root is never a
// library file: -isystem and #pragma GCC system_header do not make it one, so
// the rules and the walks read it as each other file of the tree.  A scratch
// buffer, <built-in> and <command-line> have no real path, and they are
// library files only in a system header, so a token that a macro of the tree
// pastes stays a token of the tree.
inline bool in_library_file_at(location_t location) {
    const line_map_ordinary* map = nullptr;
    linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, &map);
    const char* file = map != nullptr ? ORDINARY_MAP_FILE_NAME(map) : nullptr;
    const FileEntry* entry = file != nullptr ? &classify_file(file) : nullptr;
    if (entry != nullptr && entry->file_class == FileClass::outside && !entry->real.empty()) {
        return true;
    }
    return (entry == nullptr || entry->file_class == FileClass::outside) && in_system_header_at(location);
}

// The place of a location is its spelling location.  When the file that
// spells it owns no place in SCOPE, as a system header owns none, the place is
// the expansion point of that macro, one level at a time.  A macro of fixy
// therefore owns what it spells, and a quarantined file owns each system macro
// that it uses.  An unclassified file owns a place only in the scope of the
// tree.
inline Place place_of(location_t location, Scope scope) {
    Place place;
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION) {
        return place;
    }
    location_t current = location;
    for (int depth = 0; depth < kMacroDepth; ++depth) {
        location_t spelled = linemap_resolve_location(line_table, current, LRK_SPELLING_LOCATION, nullptr);
        expanded_location where = expand_location(spelled);
        if (where.file != nullptr) {
            const FileEntry& entry = classify_file(where.file);
            if (owns_place(entry.file_class, scope)) {
                place.file_class = entry.file_class;
                place.file = &entry;
                place.line = where.line;
                place.column = where.column;
                place.spelling = spelled;
                return place;
            }
        }
        if (!linemap_location_from_macro_expansion_p(line_table, current)) {
            return place;
        }
        const line_map* map = linemap_lookup(line_table, current);
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return place;
}

inline bool is_quarantined(location_t location) {
    return place_of(location, Scope::quarantine).file_class == FileClass::quarantined;
}

// The file of the language that holds PLACE, or null.
inline const FileEntry* language_of(const Place& place) {
    return place.file != nullptr && place.file->language_layer >= 0 ? place.file : nullptr;
}

// The file of the language that holds the place of LOCATION, or null.
inline const FileEntry* language_file_of(location_t location) {
    return language_of(place_of(location, Scope::quarantine));
}

// A quarantined file and a file of the language get a walk.  A declaration
// that a macro expansion makes can hold tokens that a quarantined file
// spells, even when the name token is spelled elsewhere.  PLACE is the place
// of LOCATION in the scope quarantine.  A sentinel base file gets only the
// walk of the family expression shape (expression.cpp, walk_family_body).
inline bool is_worth_a_walk_at(const Place& place, location_t location) {
    return place.file_class == FileClass::quarantined || (place.file != nullptr && place.file->language_layer >= 0)
        || (location > BUILTINS_LOCATION && !in_library_file_at(location)
            && linemap_location_from_macro_expansion_p(line_table, location));
}

inline bool is_worth_a_walk(location_t location) {
    return is_worth_a_walk_at(place_of(location, Scope::quarantine), location);
}

// True when a finding of a rule with a base scope counts at PLACE in this
// unit: in a quarantined file, and in a base file only in its sentinel unit
// (THE SENTINEL).
inline bool counts_in_unit(const Place& place) {
    return place.file_class == FileClass::quarantined
        || (place.file_class == FileClass::base && place.file != nullptr && place.file->is_sentinel);
}

// ── The record of a finding ─────────────────────────────────────────────

// One finding for each kind and place.  Two names at one place, such as a type
// and its member function in one expression, are two findings, and so are two
// headers that one place includes (is_name_kind).  PENDING is the admit row of
// a finding of the kind replace-pending, or null.
inline void add_finding(Kind kind, const AdmitRow* pending, const Place& place, const std::string& entity) {
    std::string kind_spelling = pending != nullptr ? pending->pending_kind : std::string(kind_name(kind));
    std::string key = kind_spelling + ' ' + place.file->relative + ':' + std::to_string(place.line) + ':'
                    + std::to_string(place.column) + (is_name_kind(kind) ? ' ' + entity : std::string{});
    if (!state.finding_keys.insert(key).second) {
        return;
    }
    Finding finding;
    finding.kind = kind;
    finding.pending = pending;
    finding.file = place.file;
    finding.line = place.line;
    finding.column = place.column;
    finding.spelling = place.spelling;
    finding.entity = entity;
    state.findings.push_back(std::move(finding));
}

// The macro assert of a system header outside the root.  A file under the
// root that marks itself as a system header and defines assert does not hide
// the tokens of its definition.
inline bool is_system_assert(cpp_hashnode* node) {
    if (node == nullptr || !cpp_user_macro_p(node) || node->value.macro == nullptr || !node->value.macro->syshdr
        || std::strcmp(reinterpret_cast<const char*>(NODE_NAME(node)), "assert") != 0) {
        return false;
    }
    expanded_location definition = expand_location(cpp_macro_definition_location(node));
    return definition.file == nullptr || classify_file(definition.file).file_class == FileClass::outside;
}

// True when a macro of a system header spells the token at LOCATION inside an
// expansion of the macro assert of a system header.  The finding
// assert-expansion stands for each such token, such as __assert_fail or
// __builtin_FILE.  A token of the condition is spelled where assert is used,
// so it stays a finding.  Complexity: O(depth of the macro expansion).
inline bool is_inside_system_assert(location_t location) {
    if (!linemap_location_from_macro_expansion_p(line_table, location)
        || !in_library_file_at(linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, nullptr))) {
        return false;
    }
    location_t current = location;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, current); ++depth) {
        const line_map* map = linemap_lookup(line_table, current);
        if (is_system_assert(MACRO_MAP_MACRO(linemap_check_macro(map)))) {
            return true;
        }
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return false;
}

// A finding of the quarantine rule, at a place in a quarantined file.
inline void record(Kind kind, location_t location, const std::string& entity) {
    if (is_inside_system_assert(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class == FileClass::quarantined) {
        add_finding(kind, nullptr, place, entity);
    }
}

// A finding of the rule of the library names: a use of a library entity that
// no admit row admits, or that a row with `until` admits.  It counts in a
// quarantined file and in a file of the language.  Each other kind is no
// finding in a file of the language, because the base holds the raw pointers,
// the arrays and the builtins of its families.  The caller decides the
// admission with the admit rows of the file that holds LOCATION.  PENDING is
// the admit row of a finding of the kind replace-pending, or null.
inline void record_library_kind(Kind kind, const AdmitRow* pending, location_t location, const std::string& entity) {
    if (is_inside_system_assert(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class == FileClass::quarantined || language_of(place) != nullptr) {
        add_finding(kind, pending, place, entity);
    }
}

inline void record_library(Kind kind, location_t location, const std::string& entity) {
    record_library_kind(kind, nullptr, location, entity);
}

// A use of ENTITY that ROW admits with `until FAMILY`.
inline void record_pending(const AdmitRow& row, location_t location, const std::string& entity) {
    record_library_kind(Kind::replace_pending, &row, location, entity);
}

// A finding of the rule for the type of a declaration.  LANGUAGE is null in a
// quarantined file, and names the file of the language that holds the
// declaration otherwise.
inline void record_declared(Kind kind, location_t location, const std::string& entity, const FileEntry* language) {
    if (language == nullptr) {
        record(kind, location, entity);
        return;
    }
    if (is_inside_system_assert(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file == language) {
        add_finding(kind, nullptr, place, entity);
    }
}

// ── The macros ──────────────────────────────────────────────────────────

// The value of the object-like macro NAME, through each macro that its one
// token names, as libcpp holds it.  The families expression shape and owner
// read the number of a system call from its macro SYS_NAME this way.
// Complexity: O(depth of the chain), at most 8 macros.
inline bool macro_number(const std::string& name, int depth, long long& number) {
    if (depth > 8
        || !cpp_defined(parse_in, reinterpret_cast<const unsigned char*>(name.c_str()),
                        static_cast<int>(name.size()))) {
        return false;
    }
    cpp_hashnode* node =
        cpp_lookup(parse_in, reinterpret_cast<const unsigned char*>(name.c_str()), static_cast<unsigned>(name.size()));
    if (node == nullptr || !cpp_user_macro_p(node) || node->value.macro == nullptr) {
        return false;
    }
    const cpp_macro* macro = node->value.macro;
    if (macro->fun_like || macro->kind != cmk_macro || macro->count != 1) {
        return false;
    }
    const cpp_token& token = macro->exp.tokens[0];
    if (token.type == CPP_NAME) {
        return macro_number(reinterpret_cast<const char*>(NODE_NAME(token.val.node.node)), depth + 1, number);
    }
    if (token.type != CPP_NUMBER || token.val.str.len == 0) {
        return false;
    }
    long long value = 0;
    for (unsigned index = 0; index < token.val.str.len; ++index) {
        unsigned char digit = token.val.str.text[index];
        if (digit < '0' || digit > '9' || value > (LLONG_MAX - 9) / 10) {
            return false;
        }
        value = value * 10 + (digit - '0');
    }
    number = value;
    return true;
}

// ── The interface of each source ────────────────────────────────────────

// rule_table.cpp: reads the rule table at PATH into state.table.  Returns
// false after an error.
bool load_rule_table(const std::string& path);
// rule_table.cpp: reads the list of the headers that cannot compile alone at
// PATH into state.not_standalone_headers (THE SENTINEL).  Returns false after
// an error.
bool load_not_standalone(const std::string& path);

// regions.cpp: the opt-out regions.
inline constexpr char kBeginMacro[] = "CRUCIBLE_I_KNOW_WHAT_IM_DOING";
inline constexpr char kEndMacro[] = "CRUCIBLE_END_I_KNOW_WHAT_IM_DOING";
void register_regions(const char* plugin_name);
bool is_opted_out(const Finding& finding);
void report_open_regions();

// library.cpp: the names, the library and the admitted list.
enum class Library : std::uint8_t {
    none,
    standard,
    c_library
};
std::string identifier_text(tree identifier);
std::string simple_name(tree decl);
std::string qualified_name(tree decl);
std::string type_text(tree type);
tree top_namespace(tree decl);
Library library_of(tree decl);
const AdmitRow* admitting_row_of_name(const std::string& name, const FileEntry* language = nullptr);
const AdmitRow* admitting_row(tree decl, Use use = Use::other, tree result = NULL_TREE,
                              const FileEntry* language = nullptr);
bool is_plainly_admitted(const AdmitRow* row);
tree result_of_explicit_arguments(tree function, tree arguments);
bool record_standard_use(tree decl, location_t location, const std::string& entity, Use use = Use::other,
                         tree result = NULL_TREE, const FileEntry* language = nullptr);
LibraryClasses library_class_in(tree type, int depth, Use use = Use::other, const FileEntry* language = nullptr);
LibraryClasses library_class_in_argument(tree argument, int depth, const FileEntry* language = nullptr);
tree library_type_name_in(tree type, int depth, const FileEntry* language = nullptr);
bool is_standard_class(tree type);
void record_pending_class(const LibraryClasses& classes, location_t location, const std::string& suffix = {});
LibraryClasses library_classes_at(tree type, location_t location, Use use, const FileEntry*& language);
void record_named_entities(tree type, location_t location, const FileEntry* language = nullptr);

// owner.cpp: the family owner, each named reference of the code.
std::string identifier_spelled_at(location_t location);
bool spells_class_at(location_t location, tree class_type);
bool check_builtin_use(tree function, location_t location);
std::string foreign_binding_of(tree decl);
bool has_c_linkage(tree decl);
void check_named(tree named, location_t location, int arguments = -1, tree explicit_arguments = NULL_TREE);
void record_namespace_name(tree ns, const std::string& member, location_t location);
void check_scope_use(tree scope_ref, location_t location);
void check_type_use(tree type, location_t location);
void check_using(tree decl);
// The owner rules (owner.cpp, THE OWNER ROWS and THE CLAIMS): each use of an
// entity of an owner row at NODE, and each call of a system call, in the walk
// of a body of FUNCTION, or of an initializer when FUNCTION is null.
void check_owned_node(tree node, location_t location, tree function);
void register_owner(const char* plugin_name);
void walk_owner();

// declaration.cpp: the family declaration property, the walk of the
// declarations.
enum class Role : std::uint8_t {
    variable,
    member,
    parameter,
    return_value
};
void walk_decltype(tree type, location_t location);
void check_object(tree decl, Role role);
void check_alias(tree decl);
void check_foreign_declaration(tree decl);
void check_signature(tree function, bool is_pattern);
void walk_constraints(tree decl);
void walk_class(tree type, bool is_pattern);
bool is_in_instantiation(tree decl);
void check_instantiation(tree function);
void walk_declarations();
void register_declarations(const char* plugin_name);
std::string function_signature(tree function);

// expression.cpp: the family expression shape, the walk of a body, and the
// rules proof-routes, refined-pre-subsumption and syscall-grant-coverage.
void walk_root(tree* root, location_t location, bool is_pattern);
void walk_body(tree function, bool is_pattern);
void check_route_alias(tree decl);
void register_expressions(const char* plugin_name);
void finish_expression_rules();
void walk_family_pattern(tree function);

// include.cpp: the family include closure, the include rules, the seals, the
// heavy headers, the names of a higher root and the provenance.
void register_includes(const char* plugin_name);
void finish_pending_include(const FileEntry* entered);
void note_project_reference(tree named, location_t location);
void report_include_closure();

// unit.cpp: the family unit fact.
void register_unit_facts(const char* plugin_name);
void note_main_started();
void report_unit_facts();

// contract.cpp: the contract rule.
void register_contract_rule(const char* plugin_name);
void walk_contract_namespace(tree ns);
void report_contract(const Finding& finding);

// home.cpp: the family home, where a definition or a specialization stands.
void register_home(const char* plugin_name);
void walk_home();

// plugin.cpp: the report of the unit.
void report(bool can_add_dependencies);

// ── The callees of a call ───────────────────────────────────────────────

// Calls VISIT for each function that the callee CALLEE of a call can name:
// the function itself, each candidate of an overload set and the function of
// a template.  A template keeps the set of a dependent call.  The families
// expression shape and owner read a call this way.
template <typename Visit>
void for_each_function(tree callee, Visit&& visit, int depth = 0) {
    if (callee == NULL_TREE || depth > 8) {
        return;
    }
    switch (TREE_CODE(callee)) {
        case FUNCTION_DECL:
            visit(callee);
            return;
        case TEMPLATE_DECL:
            if (tree result = DECL_TEMPLATE_RESULT(callee); result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL) {
                visit(result);
            }
            return;
        case ADDR_EXPR:
        case TEMPLATE_ID_EXPR:
            for_each_function(TREE_OPERAND(callee, 0), visit, depth + 1);
            return;
        case COMPONENT_REF:
            for_each_function(TREE_OPERAND(callee, 1), visit, depth + 1);
            return;
        case BASELINK:
            for_each_function(BASELINK_FUNCTIONS(callee), visit, depth + 1);
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(callee, true); candidate; ++candidate) {
                for_each_function(*candidate, visit, depth + 1);
            }
            return;
        default:
            return;
    }
}

// The callee tree of a call: the function of a CALL_EXPR or of an
// AGGR_INIT_EXPR.
inline tree callee_of(tree call) {
    return TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_FN(call) : CALL_EXPR_FN(call);
}

inline int argument_count(tree call) {
    return TREE_CODE(call) == AGGR_INIT_EXPR ? aggr_init_expr_nargs(call) : call_expr_nargs(call);
}

inline tree argument_at(tree call, int index) {
    return TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_ARG(call, index) : CALL_EXPR_ARG(call, index);
}

// True when FUNCTION is a function of the global namespace of a library
// header named NAME.
inline bool is_library_function(tree function, tree name) {
    return DECL_NAME(function) == name && library_of(function) == Library::c_library;
}

// The number of the first argument of a call of syscall(), through its
// location wrappers and conversions.
inline bool syscall_number_of(tree call, long long& number) {
    if (argument_count(call) == 0) {
        return false;
    }
    tree argument = argument_at(call, 0);
    for (int depth = 0; argument != NULL_TREE && depth < kTypeDepth; ++depth) {
        argument = tree_strip_any_location_wrapper(argument);
        if (TREE_CODE(argument) != NOP_EXPR && TREE_CODE(argument) != CONVERT_EXPR
            && TREE_CODE(argument) != NON_LVALUE_EXPR) {
            break;
        }
        argument = TREE_OPERAND(argument, 0);
    }
    if (argument == NULL_TREE || TREE_CODE(argument) != INTEGER_CST || !tree_fits_shwi_p(argument)) {
        return false;
    }
    number = tree_to_shwi(argument);
    return true;
}

}  // namespace crucible_plugin
