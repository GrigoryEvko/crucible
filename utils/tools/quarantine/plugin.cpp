// The quarantine plugin of GCC: its arguments, the hooks of the unit and the
// report.
//
// The rule table (utils/rule-table.txt, the rule table in plugin.h) gives the
// class of each file of the source root.  In a quarantined file, each object
// must have a type that fixy or foundation gives, and the code must not name a
// library entity that the table does not admit.  The plugin reads the C++
// trees of the translation unit and records each place that breaks the rule.
// The location rule decides what it checks, and the target does not: a use is
// a finding only when its spelling location is in a quarantined file.  A fixy
// template that a crucible type instantiates is not a finding.
//
// A base file that a language row of the table holds is a file of the
// language.  The rule of the library names applies to it.  A variable, a data
// member, a parameter, a return type, a base, a default template argument and
// an alias get std-object, c-library-object or std-entity when the type holds
// a library class, an enumeration or a typedef that no admit row admits there.
// An expression, also an unevaluated operand, gets std-entity or
// c-library-call when it names a library entity that no admit row admits
// there.  A row with `in` admits in a file of the language under one of its
// paths.  A row with `until` admits there only with `in`, and each such use is
// a finding of its pending kind.  An outside header must be in the allowance
// of the layer of the language row too.  Each other finding in a file of the
// language is an error in each build, so the language stays clean.
//
// The rule of the library names also reads each other base file in its
// sentinel unit, with the same admit rows.  Its findings there have the base
// kinds, and they are no errors in the mode report (the base kinds in
// plugin.h).  The ledger of the rule quarantine counts them.
//
// The plugin also applies the contract rule of the tree (contract.cpp), the
// rule binding-hook (binding.cpp), the rule borrow-match (borrow.cpp) and the
// refusal of a bit copy into the tag of a sum (the closed tags of
// library.cpp), in each mode.  Each build of the tree loads this plugin
// (utils/tools/quarantine/Quarantine.cmake).
//
// Hooks of the unit
//     parse_file             the plugin wraps lang_hooks.parse_file.  After the
//                            parse, and before the middle end, it walks every
//                            namespace that a system header does not own
//                            (declaration.cpp) and runs the other walks of the
//                            end of the unit, also after a compile error.  An
//                            object compile and a parse then read the same
//                            trees.
//     PLUGIN_FINISH_UNIT     the report, after the pass of the family library
//                            code read the last function (body.cpp).
//     PLUGIN_FINISH          the report, if the unit did not reach
//                            PLUGIN_FINISH_UNIT.
//     PLUGIN_GGC_START       each garbage collection clears the caches that a
//                            tree keys (the collections of plugin.h).
//     PLUGIN_GGC_MARKING     each garbage collection drops the waiting checks
//                            of a subject that it frees (the waiting checks
//                            of plugin.h).
//     PLUGIN_REGISTER_GGC_ROOTS   the kept bodies are a root of the
//                            collections.
//     The other sources register the hooks of their rules.
//
// Arguments (-fplugin-arg-crucible_quarantine-NAME=VALUE)
//     root=PATH      the source root (necessary)
//     build=PATH     the build directory, whose files are generated.  The rule
//                    table and the report name such a file by the prefix
//                    @build/ and its path in the build directory (the files
//                    in plugin.h)
//     rules=PATH     the rule table (necessary)
//     mode=report    each finding is a line of the section of the object, a
//                    line of the report file of out=, or else a note.  A
//                    finding that no region opts out is an error when the
//                    enforce mode of its file is error.  The mode of the
//                    scope include decides an include kind, and the mode of
//                    the scope all decides each other kind (EnforceRow in
//                    plugin.h)
//     mode=error     each finding that no region opts out is an error.  In
//                    each mode, a unit with no section and no report file
//                    gives a note for each opted-out finding, and for each
//                    region at the use of its begin macro
//                    (assay.readers.sarif, the diagnostics of the plugin)
//     out=DIR        also write the report of the unit to a file in DIR
//     stamps=DIR     the directory of the mode stamps (the dependencies)
//     stamp=TEXT     the head line of the section holds it; a new value makes
//                    the build system compile again
//     kinds=PATH     write the kinds of each rule (kRuleKinds in plugin.h) to
//                    PATH, one row `kind RULE KIND`, and do no other work.  The
//                    part kinds of the rule registry compares the file with
//                    the registry
//     sentinel=PATH  the header of include/, relative to the source root, whose
//                    sentinel unit this unit is.  The families declaration
//                    property and expression shape read the declarations and
//                    bodies of a base header in this unit (the sentinel in
//                    plugin.h), and the family home reads the gates of a
//                    header in this unit (home.cpp, the gates).  test/layer
//                    gives the sentinel unit of each header this argument
//     not-standalone=PATH   the list of the headers of include/ that cannot
//                    compile alone, test/layer/crucible-not-standalone.txt.
//                    No unit is the sentinel of such a header, so the family
//                    home reads it in each unit that includes it (THE
//                    SENTINEL in plugin.h)
//     padded-list-limit=N
//                    the threshold of the rule padded-list, a whole number of
//                    1 or more (necessary).  A list of more elements than N
//                    gives a line (declaration.cpp, the rule padded-list).
//                    Quarantine.cmake gives the error threshold of the row
//                    padded-list of utils/registry.txt
//     library-records=on
//                    run the pass of the family library code, which records
//                    the library code of each function (body.cpp).
//                    Quarantine.cmake gives it to each compile of the tree.
//                    A compile with no such argument, such as a compile of a
//                    plant of a different rule, has no record of the family
//     must-consume=on
//                    run the rule must-consume of the family linear on each
//                    body (linear.cpp).  Quarantine.cmake gives it to each
//                    compile of the tree.  A compile of a plant of a different
//                    rule has no such argument, so its report holds no line of
//                    the rule
//     linear-shapes=on
//                    run the other rules of the family linear (linear.cpp,
//                    the shapes): linear-containment, linear-capture,
//                    drop-door, linear-forge, brand-escape and brand-reentry.
//                    Quarantine.cmake gives it to each compile of the tree
//     specialization-home=on
//                    run the rule specialization-home of the family
//                    specialization home on each template with no home row
//                    (specialization.cpp).  Quarantine.cmake gives it to each
//                    compile of the tree, and a compile of a plant of a
//                    different rule has no such argument
//     base-namespace=on
//                    run the rule base-namespace of the family base namespace
//                    on each declaration of a namespace of the base
//                    (namespace.cpp).  Quarantine.cmake gives it to each
//                    compile of the tree, and a compile of a plant of a
//                    different rule has no such argument
//     fresh-brand=on
//                    run the rule fresh-brand of the family fresh brand on
//                    each function template (fresh_brand.cpp).
//                    Quarantine.cmake gives it to each compile of the tree,
//                    and a compile of a plant of a different rule has no such
//                    argument
//     definition-home=on
//                    run the rule definition-home of the family definition
//                    home on the header of the argument sentinel, and give
//                    the records of the classes of a base source file
//                    (definition.cpp).  Quarantine.cmake gives it to each
//                    compile of the tree, and a compile of a plant of a
//                    different rule has no such argument
//
// Dependencies
//     utils/assay/loop/stamps.py writes DIR/modes/PATH for each source file of
//     a row of the table, with the two enforce modes of that file.  At
//     PLUGIN_FINISH_UNIT, the plugin adds the mode stamp of each file with a
//     finding that a mode can make an error to the dependency file of the
//     unit.  A file with no mode stamp adds DIR/enforce.txt.  A change of the
//     mode of a file then compiles again only the units with such a finding in
//     that file.  An opted-out finding, a finding of the contract rule and a
//     finding of a family rule add nothing, because no mode changes them.  A
//     counted line adds its stamp, also in a region (the counted lines).
//
// Counted lines
//     A line of a rule of kCountedLineRules of plugin.h, the rules of the
//     family linear, is a counted line.  The count ledger of its rule counts
//     the lines of each directory and kind, and a line is no error in the mode
//     report.  But in a file of the enforce mode error, a counted line whose
//     directory and kind have no count row in the ledger utils/ledgers/RULE.txt
//     under the root is an error at the compile (load_counted_rows of
//     rule_table.cpp).  So a new kind of line in such a directory, such as a
//     new drop in src/ where the ledger counts none, stops the compile of the
//     developer, and a count row comes only from a deliberate write of the
//     ledger.  A kind that the ledger keys on its file, such as ungated-body,
//     reads the row of its file in place of its directory
//     (is_file_keyed_kind of plugin.h).
//     A count row above zero keeps each line of its directory and kind
//     a line of the report, and the module of the rule judges its count.  No
//     region covers a counted line.  The mode stamp of a file of the enforce
//     mode error holds the count rows of its directory (utils/assay/loop/
//     stamps.py), so a change of those rows compiles again only the units with
//     a counted line in a file of that directory.  ccache does not hash the
//     ledgers, so a cached object can miss such an error, and the module of
//     the rule still refuses its line.
//
// Section
//     A unit that writes an object puts the report into the section
//     .crucible.quarantine of the object, in each mode, with the head line
//     "# crucible-guard 2 stamp=TEXT" and then one line "RULE KIND
//     PATH:LINE:COLUMN ENTITY" for each finding and each region, in the
//     sequence of their places (is_before).  So a compiler cache stores the
//     report with the object, and the sequence in which the rules read changes
//     no byte of it.  A report file holds the same lines.  The flag "e"
//     (SHF_EXCLUDE) keeps the section out of each executable and each shared
//     library that the linker writes.  utils/assay/readers/sections.py reads
//     the section.  The plugin writes the directives at PLUGIN_FINISH_UNIT,
//     which runs before the end of the assembler output.  A unit that writes no
//     object (-fsyntax-only, -E) has no section.

#include "plugin.h"

#include "function.h"
#include "langhooks.h"
#include "tree-inline.h"

#include "rows.h"

int plugin_is_GPL_compatible;

// libcpp exports deps_add_dep, and mkdeps.h, which declares it, is not one of
// the plugin headers.
void deps_add_dep(class mkdeps*, const char*);

namespace crucible_plugin {

namespace {

// The version of the format of the section and of the report file.
constexpr int kReportFormat = 2;

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char byte : text) {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

std::string report_path() {
    std::string key = std::string(main_input_filename != nullptr ? main_input_filename : "") + '\n'
                    + (dump_dir_name != nullptr ? dump_dir_name : "") + '\n'
                    + (dump_base_name != nullptr ? dump_base_name : "");
    std::string base = main_input_filename != nullptr ? std::string(main_input_filename) : std::string("unit");
    std::size_t slash = base.rfind('/');
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    std::uint64_t hash = fnv1a(key);
    std::string hash_text(16, '0');
    for (int digit = 15; digit >= 0; --digit, hash >>= 4) {
        hash_text[static_cast<std::size_t>(digit)] = "0123456789abcdef"[hash & 0xF];
    }
    return state.out_dir + "/" + base + "." + hash_text + ".quarantine";
}

// Writes TEXT to PATH in one rename, so a reader never sees half of it.
// system.h of GCC renames the stdio functions, so the calls are not
// qualified.  Returns false, after a warning, when the write fails.
bool write_file_atomically(const std::string& path, const std::string& text) {
    std::string temporary = path + ".tmp." + std::to_string(::getpid());
    FILE* stream = fopen(temporary.c_str(), "w");
    if (stream == nullptr) {
        warning(0, "quarantine: cannot write %s: %m", temporary.c_str());
        return false;
    }
    bool was_written = fwrite(text.data(), 1, text.size(), stream) == text.size();
    was_written = fclose(stream) == 0 && was_written;
    if (!was_written || rename(temporary.c_str(), path.c_str()) != 0) {
        warning(0, "quarantine: cannot write %s: %m", path.c_str());
        remove(temporary.c_str());
        return false;
    }
    return true;
}

// One line of the report (the section).
struct ReportLine {
    std::string rule;
    std::string kind;
    std::string path;
    int line = 0;
    int column = 0;
    std::string entity;

    std::string text() const {
        return rule + " " + kind + " " + path + ":" + std::to_string(line) + ":" + std::to_string(column) + " "
             + entity;
    }
};

// The sequence of the lines of a report: the path, the line and the column as
// numbers, the kind and the entity.  A kind is of one rule, and the rule comes
// last, so two lines of one sequence are the same line.
bool is_before(const ReportLine& left, const ReportLine& right) {
    return std::tie(left.path, left.line, left.column, left.kind, left.entity, left.rule)
         < std::tie(right.path, right.line, right.column, right.kind, right.entity, right.rule);
}

// The report replaces the last report of the same object.
void write_report_file(const std::vector<ReportLine>& lines) {
    ::mkdir(state.out_dir.c_str(), 0775);
    std::string text = std::string("# unit ") + (main_input_filename != nullptr ? main_input_filename : "") + "\n";
    for (const ReportLine& line : lines) {
        text += line.text();
        text += '\n';
    }
    write_file_atomically(report_path(), text);
}

// True while the assembler output of an object is open: at PLUGIN_FINISH_UNIT
// of a unit that is not -fsyntax-only.  At PLUGIN_FINISH the output is closed.
bool writes_section() { return asm_out_file != nullptr && !flag_syntax_only; }

// Writes the report into the section of the object (the section above).  An
// .ascii string escapes '"', '\' and each byte that is not printable ASCII.
void write_section(const std::vector<ReportLine>& lines) {
    std::string text = "\t.pushsection\t.crucible.quarantine,\"e\",%progbits\n";
    auto append_line = [&text](const std::string& line) {
        text += "\t.ascii\t\"";
        for (unsigned char byte : line) {
            if (byte == '"' || byte == '\\') {
                text += '\\';
                text += static_cast<char>(byte);
            } else if (byte < 0x20 || byte >= 0x7f) {
                char escaped[8];
                snprintf(escaped, sizeof escaped, "\\%03o", static_cast<unsigned>(byte));
                text += escaped;
            } else {
                text += static_cast<char>(byte);
            }
        }
        text += "\\n\"\n";
    };
    append_line("# crucible-guard " + std::to_string(kReportFormat) + " stamp=" + state.stamp);
    for (const ReportLine& line : lines) {
        append_line(line.text());
    }
    text += "\t.popsection\n";
    fwrite(text.data(), 1, text.size(), asm_out_file);
}

// True when FINDING is a counted line: a line of a rule of kCountedLineRules
// that is no record (the counted lines).
bool is_counted_line(const Finding& finding) {
    return is_family_kind(finding.kind) && !is_record_kind(finding.kind) && is_counted_line_rule(rule_of(finding.kind));
}

// The directory of a path relative to the root, with '/' at its end, as the
// key of a ledger row (directory_of of assay.guard.engines.plugin.quarantine).
// A file at the root has the directory "./".
std::string directory_of(const std::string& relative) {
    std::size_t slash = relative.rfind('/');
    return slash == std::string::npos ? std::string("./") : relative.substr(0, slash + 1);
}

// The key of the ledger row of the counted line FINDING: its file for a kind
// that the ledger keys on the file (is_file_keyed_kind of plugin.h), and its
// directory for each other kind.
std::string counted_key_of(const Finding& finding) {
    return is_file_keyed_kind(finding.kind) ? finding.file->relative : directory_of(finding.file->relative);
}

// True when the counted line FINDING is an error: its file has the enforce
// mode error, and the ledger of its rule has no count row of its key and kind
// (the counted lines).
bool is_refused_counted_line(const Finding& finding) {
    if (!is_counted_line(finding) || finding.file == nullptr || !finding.file->is_error_mode) {
        return false;
    }
    std::string key =
        std::string(rule_name(rule_of(finding.kind))) + " " + kind_text(finding) + " " + counted_key_of(finding);
    return state.counted_rows.count(key) == 0;
}

// The mode stamp of each file with a finding that a mode can make an error,
// in the dependency file of the unit (the dependencies).  Complexity: O(findings).
void add_mode_dependencies() {
    class mkdeps* dependencies = cpp_get_deps(parse_in);
    if (dependencies == nullptr || state.stamps_dir.empty()) {
        return;
    }
    std::unordered_set<const FileEntry*> files;
    bool needs_enforce = false;
    for (const Finding& finding : state.findings) {
        bool is_counted = is_counted_line(finding);
        bool is_moded = is_counted || (!is_each_mode_kind(finding.kind) && !is_family_kind(finding.kind));
        if (!is_moded || finding.file == nullptr || (!is_counted && is_opted_out(finding))
            || !files.insert(finding.file).second) {
            continue;
        }
        std::string stamp = state.stamps_dir + "/modes/" + finding.file->relative;
        struct stat status;
        if (::stat(stamp.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
            deps_add_dep(dependencies, stamp.c_str());
        } else {
            needs_enforce = true;
        }
    }
    if (needs_enforce) {
        deps_add_dep(dependencies, (state.stamps_dir + "/enforce.txt").c_str());
    }
}

// An error for each file of the root that the unit read and that no row of
// the rule table holds.
void report_unclassified() {
    for (const FileEntry* entry : state.unclassified) {
        error("quarantine: no layer row and no quarantine row of the rule table holds %qs; add a row for its "
              "directory to the table",
              entry->relative.c_str());
    }
}

// One line of a family rule (is_family_kind), of each family that a ledger
// judges: a line of the report, an error in the mode error, and a note when
// the unit writes no report.  A record, such as a mint record or an at-home
// line, is a line and no finding (is_record_kind).  No opt-out region covers
// such a finding, and no enforce mode makes it an error, because the ledger of
// its rule judges it.
void report_family(const Finding& finding, std::vector<ReportLine>& lines, bool has_section) {
    std::string kind = kind_text(finding);
    const char* rule = rule_name(rule_of(finding.kind));
    lines.push_back(ReportLine{rule, kind, finding.file->relative, finding.line, finding.column, finding.entity});
    if (is_record_kind(finding.kind)) {
        return;
    }
    if (state.is_error_mode) {
        error_at(finding.spelling,
                 "quarantine: %s %s; the rule %s refuses this code, and %<python3 utils/bin/guard explain %s%> "
                 "gives the rule and its action",
                 kind.c_str(), finding.entity.c_str(), rule, rule);
    } else if (is_refused_counted_line(finding)) {
        error_at(finding.spelling,
                 "quarantine: %s %s; the ledger of the rule %s counts no line of this kind in %s, whose enforce mode "
                 "is error, so the rule refuses this code, and %<python3 utils/bin/guard explain %s%> gives the rule "
                 "and its action",
                 kind.c_str(), finding.entity.c_str(), rule, counted_key_of(finding).c_str(), rule);
    } else if (state.out_dir.empty() && !has_section) {
        inform(finding.spelling, "quarantine: %s %s", kind.c_str(), finding.entity.c_str());
    }
}

// Records the source root.  Returns false, after an error, when the path does
// not exist.
bool set_root(const std::string& root_argument) {
    state.root = root_argument.empty() ? std::string{} : real_path(root_argument.c_str());
    if (state.root.empty()) {
        error("quarantine: give the source root with %<-fplugin-arg-%s-root=PATH%>; the path must exist",
              state.plugin_name.c_str());
        return false;
    }
    return true;
}

// Records the build directory, whose files are generated.  A plugin with no
// build directory gives no file a path with kBuildPrefix.  Returns false,
// after an error, when the path does not exist.
bool set_build(const std::string& build_argument) {
    state.build = real_path(build_argument.c_str());
    if (state.build.empty()) {
        error("quarantine: the build directory %qs of %<-fplugin-arg-%s-build=PATH%> does not exist",
              build_argument.c_str(), state.plugin_name.c_str());
        return false;
    }
    return true;
}

// Writes the kinds of each rule to PATH (the argument kinds).  Returns false,
// after an error, when the write fails.
bool write_kinds(const std::string& path) {
    std::string text =
        "# The kinds of the quarantine plugin, one row for each kind of each rule.  The plugin writes\n"
        "# this file for its argument kinds, and the part kinds of the rule registry compares it with the\n"
        "# registry.\n";
    for (const RuleKind& rule_kind : kRuleKinds) {
        text += std::string("kind ") + rule_kind.rule + " " + rule_kind.kind + "\n";
    }
    if (!write_file_atomically(path, text)) {
        error("quarantine: cannot write the kinds to %s", path.c_str());
        return false;
    }
    return true;
}

// The action of a finding of a rule other than the quarantine rule, after the
// kind and the entity of its error.
const char* kind_action(Kind kind) {
    switch (kind) {
        case Kind::sealed_include:
        case Kind::sealed_name:
            return "a ledger verdict is a property of one machine, so it must not reach the hashing path.  Move the "
                   "consumer off the hashing path, or pass the verdict in above it (CLAUDE.md section II, DetSafe)";
        case Kind::heavy_include:
            return "each translation unit that includes the header pays its cost.  Use a builtin or a lower header, "
                   "or move the code that needs it to a source file (CLAUDE.md section XV, rule 4)";
        case Kind::unmeasured_header:
            return "no heavy row and no light row of the rule table gives the cost of the header.  Measure it with "
                   "`python3 utils/bin/loop derive header-costs --measure BUILD_DIR HEADER`";
        case Kind::upward_name:
            return "a base file names only its own root and the roots below it.  Move the code to its layer, or move "
                   "the declaration that it names down (CLAUDE.md section XXII)";
        case Kind::contract_semantic:
            return "give the target or the source file both items of CRUCIBLE_CONTRACT_IGNORE_OPTIONS of "
                   "cmake/ContractSemantic.cmake, or neither (CLAUDE.md section XII)";
        case Kind::fast_math_override:
            return "keep the FP floor of cmake/FpStrict.cmake: remove the option from the optimize attribute or the "
                   "GCC optimize pragma (CLAUDE.md section V)";
        default:
            return "";
    }
}

// The error of a unit fact, in each mode.  A fact of the whole unit has no
// location.
void report_unit_fact(const Finding& finding) {
    error_at(finding.spelling, "quarantine: %s %s; %s", kind_name(finding.kind), finding.entity.c_str(),
             kind_action(finding.kind));
}

// A check that waits for the end of the unit (the waiting checks of plugin.h).
struct WaitingCheck {
    tree subject = NULL_TREE;
    std::string name;
    void (*check)(tree subject, const std::string& name) = nullptr;
    std::vector<HeldFinding> findings;  // of the first run
};

// PLUGIN_GGC_MARKING drops the record of a subject with no mark.
std::vector<WaitingCheck> waiting_checks;

// Adds the findings of one check or reading.  A check inside another one gives
// its findings also when the other one waits, because the second run of the
// other one does not run it again.
void add_held_findings(const std::vector<HeldFinding>& findings) {
    std::vector<HeldFinding>* const outer_findings = std::exchange(state.held_findings, nullptr);
    for (const HeldFinding& finding : findings) {
        add_finding(finding.kind, finding.pending, finding.place, finding.entity);
    }
    state.held_findings = outer_findings;
}

// Calls RUN, gives its findings to FINDINGS, and returns true when RUN reached
// a part that it cannot read.
template <typename Run>
bool run_and_hold(Run run, std::vector<HeldFinding>& findings) {
    std::vector<HeldFinding>* const outer_findings = std::exchange(state.held_findings, &findings);
    unsigned const outer_parts = std::exchange(unread_parts, 0U);
    run();
    bool const is_open = unread_parts != 0;
    state.held_findings = outer_findings;
    unread_parts = outer_parts;
    return is_open;
}

// Runs each check that waits, at the end of the unit.  After a compile error
// the findings of the first run count, because the class rules read no class
// then.  Complexity: linear in the open checks, times the walk of each one.
void run_waiting_checks() {
    std::vector<WaitingCheck> const checks = std::move(waiting_checks);
    waiting_checks.clear();
    for (const WaitingCheck& waiting : checks) {
        if (seen_error()) {
            add_held_findings(waiting.findings);
        } else {
            waiting.check(waiting.subject, waiting.name);
        }
    }
}

// PLUGIN_GGC_MARKING: a subject with no mark is free after the collection.  No
// code of the unit names it, so the findings of its first run count.
// Complexity: linear in the open checks.
void on_marking(void*, void*) {
    auto const is_freed = [](const WaitingCheck& waiting) { return !ggc_marked_p(waiting.subject); };
    for (const WaitingCheck& waiting : waiting_checks) {
        if (is_freed(waiting)) {
            add_held_findings(waiting.findings);
        }
    }
    waiting_checks.erase(std::remove_if(waiting_checks.begin(), waiting_checks.end(), is_freed), waiting_checks.end());
}

// The kept bodies: the bodies that a family reads again at the end of the unit
// (read_or_keep_body of plugin.h).
//
// A list of the kept bodies, from the last one: the purpose of an entry is the
// function, and the value is a vector of the copy of its body, of its
// parameters, of its result, and of the expressions that the family keeps with
// it.  The list is a root of the collections, and it is empty after the end of
// the unit.
tree kept_bodies = NULL_TREE;

// The reading of one kept body, with the findings of its first run.
struct KeptReading {
    void (*read)(tree function) = nullptr;
    std::vector<HeldFinding> findings;
};

// The reading of each entry of kept_bodies, from the first one.
std::vector<KeptReading> kept_readings;

const ggc_root_tab kept_body_roots[] = {
    {&kept_bodies, 1, sizeof(kept_bodies), &gt_ggc_mx_tree_node, &gt_pch_nx_tree_node},
    LAST_GGC_ROOT_TAB,
};

// The walk that gives the direct parts of a node, in the sequence of
// cp_walk_tree.
struct PartWalk {
    std::vector<tree> parts;
    bool is_at_root = true;
};

tree collect_part(tree* node, int* walk_subtrees, void* data) {
    PartWalk& walk = *static_cast<PartWalk*>(data);
    if (walk.is_at_root) {
        walk.is_at_root = false;
        return NULL_TREE;
    }
    *walk_subtrees = 0;
    walk.parts.push_back(*node);
    return NULL_TREE;
}

// Adds each node of ORIGINAL with its twin in COPY to TWINS.  The copy has the
// shape of the original, so the two walks give the parts in one sequence.  A
// part that the two share has no twin.  Returns false when the shapes differ.
// Complexity: linear in the copy.
bool add_twins(tree original, tree copy, std::vector<std::pair<tree, tree>>& twins) {
    std::unordered_set<tree> seen;
    std::vector<std::pair<tree, tree>> pending{{original, copy}};
    while (!pending.empty()) {
        auto const [node, twin] = pending.back();
        pending.pop_back();
        if (node == twin || !seen.insert(twin).second) {
            continue;
        }
        if (node == NULL_TREE || twin == NULL_TREE || TREE_CODE(node) != TREE_CODE(twin)) {
            return false;
        }
        if (TYPE_P(node)) {
            continue;
        }
        twins.emplace_back(node, twin);
        if (DECL_P(node)) {
            continue;
        }
        // The copy of a block has no static local and no local type.
        if (TREE_CODE(node) == BIND_EXPR) {
            tree twin_local = BIND_EXPR_VARS(twin);
            for (tree local = BIND_EXPR_VARS(node); local != NULL_TREE; local = DECL_CHAIN(local)) {
                if (twin_local != NULL_TREE && DECL_ABSTRACT_ORIGIN(twin_local) == DECL_ORIGIN(local)) {
                    pending.emplace_back(local, twin_local);
                    if (VAR_P(local)) {
                        pending.emplace_back(DECL_INITIAL(local), DECL_INITIAL(twin_local));
                    }
                    twin_local = DECL_CHAIN(twin_local);
                }
            }
            if (twin_local != NULL_TREE) {
                return false;
            }
            pending.emplace_back(BIND_EXPR_BODY(node), BIND_EXPR_BODY(twin));
            continue;
        }
        PartWalk parts;
        PartWalk twin_parts;
        tree root = node;
        tree twin_root = twin;
        cp_walk_tree(&root, collect_part, &parts, nullptr);
        cp_walk_tree(&twin_root, collect_part, &twin_parts, nullptr);
        if (parts.parts.size() != twin_parts.parts.size()) {
            return false;
        }
        for (std::size_t index = 0; index < parts.parts.size(); ++index) {
            pending.emplace_back(parts.parts[index], twin_parts.parts[index]);
        }
    }
    return true;
}

// The copier of the trees of one function, as copy_fn of GCC copies a body for
// the constant evaluator at the same event.  The copies have their own locals
// and parameters, and they share each other declaration with the body.
class BodyCopier {
public:
    explicit BodyCopier(tree function) {
        memset(&data_, 0, sizeof(data_));
        data_.src_fn = function;
        data_.dst_fn = function;
        data_.src_cfun = DECL_STRUCT_FUNCTION(function);
        data_.decl_map = &decl_map_;
        data_.copy_decl = copy_decl_no_change;
        data_.transform_call_graph_edges = CB_CGE_DUPLICATE;
        data_.transform_parameter = true;
        data_.do_not_unshare = true;
        data_.do_not_fold = true;
    }
    BodyCopier(const BodyCopier&) = delete;
    BodyCopier& operator=(const BodyCopier&) = delete;

    // The copy of the parameter or of the result DECL.
    tree copy_decl(tree decl) { return decl != NULL_TREE ? remap_decl(decl, &data_) : NULL_TREE; }

    // The copy of NODE.  The copy of a block gives each static local of the
    // block to the function in compile, which gets it again when it lowers the
    // body, so the copier takes it off.
    tree copy(tree node) {
        unsigned const locals = cfun != nullptr ? vec_safe_length(cfun->local_decls) : 0U;
        walk_tree(&node, copy_tree_body_r, &data_, nullptr);
        if (cfun != nullptr && vec_safe_length(cfun->local_decls) > locals) {
            cfun->local_decls->truncate(locals);
        }
        return node;
    }

private:
    hash_map<tree, tree> decl_map_;
    copy_body_data data_;
};

// Keeps a copy of the body of FUNCTION for READING.  Returns false when
// KEEP_FACTS cannot read the copy.
bool keep_body(tree function, KeptReading reading, void (*keep_facts)(tree function, BodyCopy& copy)) {
    BodyCopier copier(function);
    tree parameters = NULL_TREE;
    tree* last = &parameters;
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        *last = copier.copy_decl(parameter);
        last = &DECL_CHAIN(*last);
    }
    tree result = copier.copy_decl(DECL_RESULT(function));
    tree body = copier.copy(DECL_SAVED_TREE(function));
    tree held = NULL_TREE;
    if (keep_facts != nullptr) {
        bool is_exact = true;
        BodyCopy copy;
        copy.copy_of = [&](tree expression) {
            tree made = copier.copy(expression);
            held = tree_cons(NULL_TREE, made, held);
            is_exact = add_twins(expression, made, copy.twins) && is_exact;
            return made;
        };
        copy.add_body = [&] { is_exact = add_twins(DECL_SAVED_TREE(function), body, copy.twins) && is_exact; };
        keep_facts(function, copy);
        if (!is_exact) {
            return false;
        }
    }
    tree kept = make_tree_vec(4);
    TREE_VEC_ELT(kept, 0) = body;
    TREE_VEC_ELT(kept, 1) = parameters;
    TREE_VEC_ELT(kept, 2) = result;
    TREE_VEC_ELT(kept, 3) = held;
    kept_bodies = tree_cons(function, kept, kept_bodies);
    kept_readings.push_back(std::move(reading));
    return true;
}

// Reads each kept body again, at the end of the unit.  The function has its copy
// during the reading, and its own body after it.  After a compile error the
// findings of the first run count.  Complexity: linear in the kept bodies.
void read_kept_bodies() {
    tree const bodies = nreverse(kept_bodies);
    kept_bodies = NULL_TREE;
    std::vector<KeptReading> const readings = std::move(kept_readings);
    kept_readings.clear();
    std::size_t index = 0;
    for (tree entry = bodies; entry != NULL_TREE; entry = TREE_CHAIN(entry), ++index) {
        tree const function = TREE_PURPOSE(entry);
        tree const kept = TREE_VALUE(entry);
        if (seen_error()) {
            add_held_findings(readings[index].findings);
            continue;
        }
        tree const body = std::exchange(DECL_SAVED_TREE(function), TREE_VEC_ELT(kept, 0));
        tree const parameters = std::exchange(DECL_ARGUMENTS(function), TREE_VEC_ELT(kept, 1));
        tree const result = std::exchange(DECL_RESULT(function), TREE_VEC_ELT(kept, 2));
        readings[index].read(function);
        DECL_SAVED_TREE(function) = body;
        DECL_ARGUMENTS(function) = parameters;
        DECL_RESULT(function) = result;
    }
}

// The parse_file hook of the front end, before parse_and_walk wraps it.
void (*front_end_parse_file)() = nullptr;

bool was_unit_walked = false;

// The walks of the end of the unit, one time.
void walk_unit() {
    if (was_unit_walked) {
        return;
    }
    was_unit_walked = true;
    state.is_unit_parsed = true;
    run_waiting_checks();
    read_kept_bodies();
    walk_declarations();
    finish_expression_rules();
    finish_niche_access();
    walk_home();
    walk_owner();
    finish_linear_rules();
    finish_borrow_matches();
    finish_base_namespace();
    walk_fresh_brands();
    finish_door_gates();
    finish_definition_home();
    judge_folded_names();
}

// The parse of the unit, and then the walks of its end.  When the parse
// returns, the front end has made each instantiation, each implicit member and
// the function of the dynamic initializers, and the middle end has not run.
// The middle end releases the parameters of a declaration that an emitted
// function calls, so a walk at PLUGIN_FINISH_UNIT did not see them, and an
// object compile and a parse gave different findings.  A unit with a compile
// error gets the walks too, so a negative fixture that stops on its expected
// error gives its findings.  The walks skip a declaration that the error left
// with no type (is_erroneous_decl of plugin.h).
// The walks are the last readers of the trees that the records of the
// families hold (the collections of plugin.h).  After the parse, GCC no longer
// keeps each identifier as a root (toplev.cc, ggc_protect_identifiers), and
// the middle end frees the bodies that it emits.  So the judge of the folded
// names (owner.cpp) runs here, and the report reads only findings.
void parse_and_walk() {
    front_end_parse_file();
    walk_unit();
}

void on_finish_unit(void*, void*) {
    walk_unit();
    report(true);
}

// A unit that stopped on an error, or that -fsyntax-only compiled, does not
// reach PLUGIN_FINISH_UNIT.  GCC wrote the dependency file before
// PLUGIN_FINISH, so the report adds no dependency.
void on_finish(void*, void*) { report(false); }

void on_collection(void*, void*) {
    for (int use = 0; use < 2; ++use) {
        state.library_class_of[use].clear();
        state.language_class_of[use].clear();
        state.admitting_row_of[use].clear();
    }
    state.unnamed_bit_parts_of.clear();
    state.placeholder_locals.clear();
    state.default_argument_of.clear();
    state.walked.clear();
    state.contract_walked.clear();
    state.top_facts.clear();
}

}  // namespace

void check_or_wait(tree subject, const std::string& name, void (*check)(tree subject, const std::string& name)) {
    if (state.is_unit_parsed) {
        check(subject, name);
        return;
    }
    std::vector<HeldFinding> findings;
    if (run_and_hold([&] { check(subject, name); }, findings)) {
        waiting_checks.push_back(WaitingCheck{subject, name, check, std::move(findings)});
    } else {
        add_held_findings(findings);
    }
}

bool read_or_keep_body(tree function, const BodyReader& reader) {
    if (state.is_unit_parsed) {
        reader.read(function);
        return false;
    }
    std::vector<HeldFinding> findings;
    bool is_open = run_and_hold([&] { reader.read(function); }, findings) && !seen_error();
    if (is_open && reader.read_wide != nullptr) {
        std::vector<HeldFinding> wide_findings;
        bool can_give_fact = false;
        run_and_hold([&] { can_give_fact = reader.read_wide(function); }, wide_findings);
        is_open = can_give_fact || !wide_findings.empty();
    }
    if (is_open && keep_body(function, KeptReading{reader.read, findings}, reader.keep_facts)) {
        return true;
    }
    add_held_findings(findings);
    return false;
}

tree annotation_class(tree attribute) {
    if (!annotation_p(attribute)) {
        return NULL_TREE;
    }
    tree value = TREE_VALUE(attribute);
    if (value == NULL_TREE || TREE_CODE(value) != TREE_LIST || TREE_VALUE(value) == NULL_TREE) {
        return NULL_TREE;
    }
    tree type = TREE_TYPE(TREE_VALUE(value));
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type)) {
        return NULL_TREE;
    }
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) ? type : NULL_TREE;
}

bool carries_base_annotation(tree attributes, tree name, tree space) {
    static tree const foundation = get_identifier("foundation");
    for (tree attribute = attributes; attribute != NULL_TREE; attribute = TREE_CHAIN(attribute)) {
        tree annotation = annotation_class(attribute);
        tree decl = annotation != NULL_TREE ? TYPE_NAME(annotation) : NULL_TREE;
        if (decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL || DECL_NAME(decl) != name) {
            continue;
        }
        tree scope = CP_DECL_CONTEXT(decl);
        if (space != NULL_TREE) {
            if (scope == NULL_TREE || TREE_CODE(scope) != NAMESPACE_DECL || DECL_NAME(scope) != space) {
                continue;
            }
            scope = CP_DECL_CONTEXT(scope);
        }
        if (scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && DECL_NAME(scope) == foundation
            && CP_DECL_CONTEXT(scope) == global_namespace) {
            return true;
        }
    }
    return false;
}

bool function_carries_base_annotation(tree function, tree name, tree space) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    if (carries_base_annotation(DECL_ATTRIBUTES(function), name, space)) {
        return true;
    }
    if (DECL_LANG_SPECIFIC(function) == nullptr || !DECL_TEMPLATE_INFO(function)) {
        return false;
    }
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return false;
    }
    tree pattern = DECL_TEMPLATE_RESULT(template_decl);
    if (pattern != NULL_TREE && pattern != function && carries_base_annotation(DECL_ATTRIBUTES(pattern), name, space)) {
        return true;
    }
    tree general = most_general_template(template_decl);
    tree general_pattern = general != NULL_TREE ? DECL_TEMPLATE_RESULT(general) : NULL_TREE;
    return general_pattern != NULL_TREE && general_pattern != pattern
        && carries_base_annotation(DECL_ATTRIBUTES(general_pattern), name, space);
}

bool is_std_move_cast(tree function) {
    static tree const move = get_identifier("move");
    static tree const forward = get_identifier("forward");
    static tree const move_if_noexcept = get_identifier("move_if_noexcept");
    static tree const forward_like = get_identifier("forward_like");
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    tree name = DECL_NAME(function);
    if (name != move && name != forward && name != move_if_noexcept && name != forward_like) {
        return false;
    }
    tree scope = CP_DECL_CONTEXT(function);
    return scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && DECL_NAMESPACE_STD_P(scope);
}

// The report of the unit.  CAN_ADD_DEPENDENCIES is true at PLUGIN_FINISH_UNIT,
// before GCC writes the dependency file.
void report(bool can_add_dependencies) {
    if (state.was_reported) {
        return;
    }
    state.was_reported = true;
    finish_pending_include(nullptr);
    // Each use of a symbol of C language linkage that the unit did not define.
    for (const ForeignUse& use : state.foreign_uses) {
        if (state.defined_c_symbols.count(use.uid) == 0) {
            add_finding(Kind::c_library_call, nullptr, use.place, use.entity);
        }
    }
    report_include_closure();
    report_door_units();
    report_unit_facts();
    report_open_regions();
    report_unclassified();
    if (can_add_dependencies) {
        add_mode_dependencies();
    }
    bool has_section = writes_section();
    // An admit row with `unless MACRO` refuses each unit that defines MACRO.
    for (const AdmitRow& admit : state.table.admits) {
        const std::string& macro = admit.unless_macro;
        if (!macro.empty()
            && cpp_defined(parse_in, reinterpret_cast<const unsigned char*>(macro.c_str()),
                           static_cast<int>(macro.size()))) {
            error("quarantine: the unit defines %s, and the rule table admits %s%s%s only in a unit that does not "
                  "define it; remove the definition",
                  macro.c_str(), admit.is_header ? "<" : "", admit.entry.c_str(), admit.is_header ? ">" : "");
        }
    }
    std::vector<ReportLine> lines;
    for (const Finding& finding : state.findings) {
        if (is_family_kind(finding.kind)) {
            report_family(finding, lines, has_section);
            continue;
        }
        bool is_out = is_opted_out(finding);
        std::string inner_kind = kind_text(finding);
        std::string kind = is_out ? std::string(kOptedOutKind) : inner_kind;
        std::string entity = is_out ? inner_kind + " " + finding.entity : finding.entity;
        const char* rule = is_out ? rule_name(Rule::quarantine) : rule_name(rule_of(finding.kind));
        lines.push_back(ReportLine{rule, kind, finding.file->relative, finding.line, finding.column, entity});
        // A use that a row with `until` and `in` admits in a file of the
        // language is a finding and no error, as it is in a quarantined file.
        // The rule of the language files is the quarantine rule.
        Rule rule_of_finding = rule_of(finding.kind);
        bool is_language =
            finding.file->language_layer >= 0 && finding.pending == nullptr && rule_of_finding == Rule::quarantine;
        // An enforce row of the scope include sets the mode of the include
        // kinds only, and each other kind keeps the mode of the scope all.
        bool is_file_error =
            is_include_kind(finding.kind) ? finding.file->is_include_error_mode : finding.file->is_error_mode;
        bool is_error = state.is_error_mode || is_file_error || is_language;
        // The contract rule, the unit facts, the rules specialization-home and
        // base-namespace, the escapes of a scope brand, a self-move and the
        // end of a linear sum with a payload give an error in each mode.  The
        // self-move comes before the other kinds of must-consume, whose report
        // reads only the kinds of a linear sum.
        if (finding.kind == Kind::contract_specifier && !is_out) {
            report_contract(finding);
        } else if (finding.kind == Kind::binding_hook && !is_out) {
            report_binding_hook(finding);
        } else if (finding.kind == Kind::forged_tag && !is_out) {
            report_forged_tag(finding);
        } else if (finding.kind == Kind::borrow_match && !is_out) {
            report_borrow_match(finding);
        } else if (rule_of_finding == Rule::view_escape && !is_out) {
            report_view_escape(finding);
        } else if (rule_of_finding == Rule::specialization_home && !is_out) {
            report_specialization(finding);
        } else if (rule_of_finding == Rule::base_namespace && !is_out) {
            report_base_namespace(finding);
        } else if (rule_of_finding == Rule::brand_escape && !is_out) {
            report_scope_escape(finding);
        } else if (finding.kind == Kind::linear_self_move && !is_out) {
            report_self_move(finding);
        } else if (rule_of_finding == Rule::must_consume && !is_out) {
            report_option_end(finding);
        } else if (rule_of_finding == Rule::fresh_brand && !is_out) {
            report_fresh_brand(finding);
        } else if ((finding.kind == Kind::thread_bound_static || finding.kind == Kind::loan_key_static) && !is_out) {
            report_static_reach(finding);
        } else if (is_each_mode_kind(finding.kind) && !is_out) {
            report_unit_fact(finding);
        } else if (rule_of_finding != Rule::quarantine && is_error && !is_out) {
            error_at(finding.spelling, "quarantine: %s %s; %s", kind.c_str(), entity.c_str(),
                     kind_action(finding.kind));
        } else if (is_language && !is_out && !is_include_kind(finding.kind)) {
            error_at(finding.spelling,
                     "quarantine: %s %s; a language row of the rule table holds this file, so each declaration "
                     "and each expression names only a type or an entity of the base, or a name that an admit row "
                     "admits there",
                     kind.c_str(), entity.c_str());
        } else if (is_error && !is_out && finding.kind == Kind::enum_cast) {
            error_at(finding.spelling,
                     "quarantine: enum-cast %s; the cast can make a value that names no enumerator.  Read a number "
                     "with %qs, read a field whose each value names an enumerator with %qs, or write the enumerator",
                     entity.c_str(), "fixy::enum_from", "fixy::enum_from_field");
        } else if (is_error && !is_out && finding.kind == Kind::raw_size_arithmetic) {
            error_at(finding.spelling,
                     "quarantine: raw-size-arithmetic %s; a sum or a product of two sizes can wrap with no report, "
                     "and the door then gets a wrong size or index.  Compute it with %qs or %qs, or with a checked "
                     "form at the boundary of outside data",
                     entity.c_str(), "fixy::strict_add", "fixy::strict_mul");
        } else if (is_error && !is_out && is_include_kind(finding.kind)) {
            error_at(finding.spelling,
                     "quarantine: %s %s; the rule table does not let this file include the header, so include a "
                     "header that its layer can include",
                     kind.c_str(), entity.c_str());
        } else if (is_error && !is_out) {
            error_at(finding.spelling,
                     "quarantine: %s %s; use a type or an entity of fixy or foundation.  A region of %qs is "
                     "only for a reason class of CLAUDE.md section XXII",
                     kind.c_str(), entity.c_str(), kBeginMacro);
        } else if ((is_out || !is_error) && state.out_dir.empty() && !has_section) {
            // An opted-out finding is no error in each mode, and a unit with
            // no report gives it a note also in a file of the mode error.
            inform(finding.spelling, "quarantine: %s %s", kind.c_str(), entity.c_str());
        }
    }
    // Each region, for the ledger of the rule quarantine.  A region is no
    // finding.  A unit with no report gives a note for each region too, so
    // that a census of the notes gives each opted-out finding its reason.
    for (const Region& region : state.regions) {
        lines.push_back(ReportLine{rule_name(Rule::quarantine), kRegionKind, region.file->relative, region.begin_line,
                                   1, region.reason});
        if (state.out_dir.empty() && !has_section) {
            inform(region.use, "quarantine: %s %s", kRegionKind, region.reason.c_str());
        }
    }
    std::sort(lines.begin(), lines.end(), is_before);
    if (has_section) {
        write_section(lines);
    }
    if (!state.out_dir.empty()) {
        write_report_file(lines);
    }
}

}  // namespace crucible_plugin

int plugin_init(plugin_name_args* plugin_info, plugin_gcc_version* version) {
    using namespace crucible_plugin;
    if (!plugin_default_version_check(version, &gcc_version)) {
        error("quarantine: the plugin was built for GCC %s, and this compiler is a different version",
              gcc_version.basever);
        return 1;
    }
    // A preprocessing run builds no tree, and its empty report must not
    // replace the report of the unit.
    if (flag_preprocess_only) {
        return 0;
    }
    state.plugin_name = plugin_info->base_name;
    std::string root_argument;
    std::string build_argument;
    bool has_build = false;
    std::string rules_argument;
    std::string kinds_argument;
    std::string not_standalone_argument;
    bool has_padded_limit = false;
    bool records_library_code = false;
    bool checks_must_consume = false;
    bool checks_linear_shapes = false;
    bool checks_specialization_home = false;
    bool checks_base_namespace = false;
    bool checks_fresh_brand = false;
    bool checks_definition_home = false;
    for (int index = 0; index < plugin_info->argc; ++index) {
        std::string key = plugin_info->argv[index].key;
        std::string value = plugin_info->argv[index].value != nullptr ? plugin_info->argv[index].value : "";
        if (key == "root") {
            root_argument = value;
        } else if (key == "build") {
            build_argument = value;
            has_build = true;
        } else if (key == "rules") {
            rules_argument = value;
        } else if (key == "out") {
            state.out_dir = value;
        } else if (key == "stamps") {
            state.stamps_dir = value;
        } else if (key == "mode") {
            if (value != "report" && value != "error") {
                error("quarantine: mode is %qs; the modes are report and error", value.c_str());
                return 1;
            }
            state.is_error_mode = value == "error";
        } else if (key == "stamp") {
            state.stamp = value;
        } else if (key == "kinds") {
            kinds_argument = value;
        } else if (key == "sentinel") {
            state.sentinel = value;
        } else if (key == "not-standalone") {
            not_standalone_argument = value;
        } else if (key == "padded-list-limit") {
            if (!crucible_rows::number(value, UINT64_MAX, state.padded_list_limit) || state.padded_list_limit == 0) {
                error("quarantine: padded-list-limit is %qs, and the threshold of the rule padded-list must be a "
                      "whole number of 1 or more",
                      value.c_str());
                return 1;
            }
            has_padded_limit = true;
        } else if (key == "library-records") {
            if (value != "on") {
                error("quarantine: library-records is %qs; give library-records=on to record the library code of "
                      "each function, or no such argument",
                      value.c_str());
                return 1;
            }
            records_library_code = true;
        } else if (key == "must-consume") {
            if (value != "on") {
                error("quarantine: must-consume is %qs; give must-consume=on to run the rule must-consume on each "
                      "body, or no such argument",
                      value.c_str());
                return 1;
            }
            checks_must_consume = true;
        } else if (key == "linear-shapes") {
            if (value != "on") {
                error("quarantine: linear-shapes is %qs; give linear-shapes=on to run the rules of the shapes of the "
                      "family linear, or no such argument",
                      value.c_str());
                return 1;
            }
            checks_linear_shapes = true;
        } else if (key == "specialization-home") {
            if (value != "on") {
                error("quarantine: specialization-home is %qs; give specialization-home=on to run the rule "
                      "specialization-home, or no such argument",
                      value.c_str());
                return 1;
            }
            checks_specialization_home = true;
        } else if (key == "base-namespace") {
            if (value != "on") {
                error("quarantine: base-namespace is %qs; give base-namespace=on to run the rule base-namespace, or "
                      "no such argument",
                      value.c_str());
                return 1;
            }
            checks_base_namespace = true;
        } else if (key == "fresh-brand") {
            if (value != "on") {
                error("quarantine: fresh-brand is %qs; give fresh-brand=on to run the rule fresh-brand, or no such "
                      "argument",
                      value.c_str());
                return 1;
            }
            checks_fresh_brand = true;
        } else if (key == "definition-home") {
            if (value != "on") {
                error("quarantine: definition-home is %qs; give definition-home=on to run the rule definition-home, "
                      "or no such argument",
                      value.c_str());
                return 1;
            }
            checks_definition_home = true;
        } else {
            error("quarantine: unknown argument %qs; the arguments are root, build, rules, mode, out, stamps, stamp, "
                  "kinds, sentinel, not-standalone, padded-list-limit, library-records, must-consume, "
                  "linear-shapes, specialization-home, base-namespace, fresh-brand and definition-home",
                  key.c_str());
            return 1;
        }
    }
    if (!kinds_argument.empty()) {
        return write_kinds(kinds_argument) ? 0 : 1;
    }
    if (!has_padded_limit) {
        error("quarantine: no argument gives the threshold of the rule padded-list, and "
              "utils/tools/quarantine/Quarantine.cmake gives it with %<-fplugin-arg-%s-padded-list-limit=N%> from the "
              "row padded-list of utils/registry.txt",
              state.plugin_name.c_str());
        return 1;
    }
    if (has_build && !set_build(build_argument)) {
        return 1;
    }
    if (!set_root(root_argument)) {
        return 1;
    }
    struct stat sentinel_status;
    if (!state.sentinel.empty()
        && (state.sentinel.front() == '/' || ::stat((state.root + "/" + state.sentinel).c_str(), &sentinel_status) != 0
            || !S_ISREG(sentinel_status.st_mode))) {
        error("quarantine: the argument %<sentinel=%s%> names no file under the source root; give the path of a "
              "header of include/ relative to the root",
              state.sentinel.c_str());
        return 1;
    }
    if (rules_argument.empty()) {
        error("quarantine: give the rule table with %<-fplugin-arg-%s-rules=PATH%>", state.plugin_name.c_str());
        return 1;
    }
    if (!load_rule_table(rules_argument) || !load_counted_rows()) {
        return 1;
    }
    if (!not_standalone_argument.empty() && !load_not_standalone(not_standalone_argument)) {
        return 1;
    }
    for (const AdmitRow& admit : state.table.admits) {
        (admit.is_header ? state.admitted_headers : state.admitted_names).emplace(admit.entry, &admit);
    }
    register_includes(plugin_info->base_name);
    register_regions(plugin_info->base_name);
    register_contract_rule(plugin_info->base_name);
    register_binding_hook(plugin_info->base_name);
    register_closed_tags(plugin_info->base_name);
    register_borrow_match(plugin_info->base_name);
    register_view_escape(plugin_info->base_name);
    register_unit_facts(plugin_info->base_name);
    register_expressions(plugin_info->base_name);
    register_niche_access(plugin_info->base_name);
    register_declarations(plugin_info->base_name);
    register_home(plugin_info->base_name);
    register_owner(plugin_info->base_name);
    if (records_library_code) {
        register_library_code(plugin_info->base_name);
    }
    if (checks_must_consume || checks_linear_shapes) {
        register_linear(plugin_info->base_name, checks_must_consume, checks_linear_shapes);
    }
    if (checks_must_consume) {
        register_door_gate(plugin_info->base_name);
    }
    if (checks_specialization_home) {
        register_specialization_home(plugin_info->base_name);
    }
    if (checks_base_namespace) {
        register_base_namespace(plugin_info->base_name);
    }
    if (checks_fresh_brand) {
        register_fresh_brand(plugin_info->base_name);
    }
    if (checks_definition_home) {
        register_definition_home(plugin_info->base_name);
    }
    front_end_parse_file = lang_hooks.parse_file;
    lang_hooks.parse_file = parse_and_walk;
    register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT, on_finish_unit, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH, on_finish, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_START, on_collection, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_MARKING, on_marking, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_REGISTER_GGC_ROOTS, nullptr,
                      const_cast<ggc_root_tab*>(kept_body_roots));
    return 0;
}
