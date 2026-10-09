// The quarantine plugin of GCC: its arguments, the hooks of the unit and the
// report.
//
// The rule table (utils/rule-table.txt, THE RULE TABLE in plugin.h) gives the
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
// kinds, and they are no errors in the mode report (THE BASE KINDS in
// plugin.h).  The ledger of the rule quarantine counts them.
//
// The plugin also applies the contract rule of the tree (contract.cpp), in
// each mode.  Each build of the tree loads this plugin
// (utils/tools/quarantine/Quarantine.cmake).
//
// THE HOOKS OF THE UNIT
//     parse_file             the plugin wraps lang_hooks.parse_file.  After the
//                            parse, and before the middle end, it walks every
//                            namespace that a system header does not own
//                            (declaration.cpp) and runs the other walks of the
//                            end of the unit.  An object compile and a parse
//                            then read the same trees.
//     PLUGIN_FINISH_UNIT     the report.
//     PLUGIN_FINISH          the report, if the unit did not reach
//                            PLUGIN_FINISH_UNIT.
//     PLUGIN_GGC_START       each garbage collection clears the caches that a
//                            tree keys.
//     The other sources register the hooks of their rules.
//
// THE ARGUMENTS (-fplugin-arg-crucible_quarantine-NAME=VALUE)
//     root=PATH      the source root (necessary)
//     build=PATH     the build directory, whose files are generated.  The rule
//                    table and the report name such a file by the prefix
//                    @build/ and its path in the build directory (THE FILES
//                    in plugin.h)
//     rules=PATH     the rule table (necessary)
//     mode=report    each finding is a line of the section of the object, a
//                    line of the report file of out=, or else a note.  A
//                    finding that no region opts out is an error when the
//                    enforce mode of its file is error.  The mode of the
//                    scope include decides an include kind, and the mode of
//                    the scope all decides each other kind (EnforceRow in
//                    plugin.h)
//     mode=error     each finding that no region opts out is an error
//     out=DIR        also write the report of the unit to a file in DIR
//     stamps=DIR     the directory of the mode stamps (THE DEPENDENCIES)
//     stamp=TEXT     the head line of the section holds it; a new value makes
//                    the build system compile again
//     kinds=PATH     write the kinds of each rule (kRuleKinds in plugin.h) to
//                    PATH, one row `kind RULE KIND`, and do no other work.  The
//                    rule plugin-kinds compares the file with the registry
//     sentinel=PATH  the header of include/, relative to the source root, whose
//                    sentinel unit this unit is.  The families declaration
//                    property and expression shape read the declarations and
//                    bodies of a base header in this unit (THE SENTINEL in
//                    plugin.h), and the family home reads the gates of a
//                    header in this unit (home.cpp, THE GATES).  test/layer
//                    gives the sentinel unit of each header this argument
//     not-standalone=PATH   the list of the headers of include/ that cannot
//                    compile alone, test/layer/crucible-not-standalone.txt.
//                    No unit is the sentinel of such a header, so the family
//                    home reads it in each unit that includes it (THE
//                    SENTINEL in plugin.h)
//     padded-list-limit=N
//                    the threshold of the rule padded-list, a whole number of
//                    1 or more (necessary).  A list of more elements than N
//                    gives a line (declaration.cpp, THE RULE padded-list).
//                    Quarantine.cmake gives the error threshold of the row
//                    padded-list of utils/registry.txt
//
// THE DEPENDENCIES
//     utils/assay/loop/stamps.py writes DIR/modes/PATH for each source file of
//     a row of the table, with the two enforce modes of that file.  At
//     PLUGIN_FINISH_UNIT, the plugin adds the mode stamp of each file with a
//     finding that a mode can make an error to the dependency file of the
//     unit.  A file with no mode stamp adds DIR/enforce.txt.  A change of the
//     mode of a file then compiles again only the units with such a finding in
//     that file.  An opted-out finding, a finding of the contract rule and a
//     finding of a family rule add nothing, because no mode changes them.
//
// THE SECTION
//     A unit that writes an object puts the report into the section
//     .crucible.quarantine of the object, in each mode, with the head line
//     "# crucible-guard 2 stamp=TEXT" and then one line "RULE KIND
//     PATH:LINE:COLUMN ENTITY" for each finding and each region.  So a
//     compiler cache stores the report with the object.  The flag "e"
//     (SHF_EXCLUDE) keeps the section out of each executable and each shared
//     library that the linker writes.  utils/assay/readers/sections.py reads
//     the section.  The plugin writes the directives at PLUGIN_FINISH_UNIT,
//     which runs before the end of the assembler output.  A unit that writes no
//     object (-fsyntax-only, -E) has no section.

#include "plugin.h"

#include "langhooks.h"

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

// The report replaces the last report of the same object.
void write_report_file(const std::vector<std::string>& lines) {
    ::mkdir(state.out_dir.c_str(), 0775);
    std::string text = std::string("# unit ") + (main_input_filename != nullptr ? main_input_filename : "") + "\n";
    for (const std::string& line : lines) {
        text += line;
        text += '\n';
    }
    write_file_atomically(report_path(), text);
}

// True while the assembler output of an object is open: at PLUGIN_FINISH_UNIT
// of a unit that is not -fsyntax-only.  At PLUGIN_FINISH the output is closed.
bool writes_section() { return asm_out_file != nullptr && !flag_syntax_only; }

// Writes the report into the section of the object (THE SECTION above).  An
// .ascii string escapes '"', '\' and each byte that is not printable ASCII.
void write_section(const std::vector<std::string>& lines) {
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
    for (const std::string& line : lines) {
        append_line(line);
    }
    text += "\t.popsection\n";
    fwrite(text.data(), 1, text.size(), asm_out_file);
}

// The mode stamp of each file with a finding that a mode can make an error,
// in the dependency file of the unit (THE DEPENDENCIES).  Complexity: O(findings).
void add_mode_dependencies() {
    class mkdeps* dependencies = cpp_get_deps(parse_in);
    if (dependencies == nullptr || state.stamps_dir.empty()) {
        return;
    }
    std::unordered_set<const FileEntry*> files;
    bool needs_enforce = false;
    for (const Finding& finding : state.findings) {
        if (is_each_mode_kind(finding.kind) || is_family_kind(finding.kind) || finding.file == nullptr
            || is_opted_out(finding) || !files.insert(finding.file).second) {
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

// One line of the report: "RULE KIND PATH:LINE:COLUMN ENTITY".
std::string report_line(const char* rule, const std::string& kind, const FileEntry& file, int line, int column,
                        const std::string& entity) {
    return std::string(rule) + " " + kind + " " + file.relative + ":" + std::to_string(line) + ":"
         + std::to_string(column) + " " + entity;
}

// One line of a family rule (is_family_kind), of each family that a ledger
// judges: a line of the report, an error in the mode error, and a note when
// the unit writes no report.  A record, such as a mint record or an at-home
// line, is a line and no finding (is_record_kind).  No opt-out region covers
// such a finding, and no enforce mode makes it an error, because the ledger of
// its rule judges it.
void report_family(const Finding& finding, std::vector<std::string>& lines, bool has_section) {
    std::string kind = kind_text(finding);
    const char* rule = rule_name(rule_of(finding.kind));
    lines.push_back(report_line(rule, kind, *finding.file, finding.line, finding.column, finding.entity));
    if (is_record_kind(finding.kind)) {
        return;
    }
    if (state.is_error_mode) {
        error_at(finding.spelling,
                 "quarantine: %s %s; the rule %s refuses this code, and %<python3 utils/bin/guard explain %s%> "
                 "gives the rule and its action",
                 kind.c_str(), finding.entity.c_str(), rule, rule);
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
    std::string text = "# The kinds of the quarantine plugin, one row for each kind of each rule.  The plugin writes\n"
                       "# this file for its argument kinds, and the rule plugin-kinds compares it with the registry.\n";
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

// The parse_file hook of the front end, before parse_and_walk wraps it.
void (*front_end_parse_file)() = nullptr;

bool was_unit_walked = false;

// The walks of the end of the unit, one time.
void walk_unit() {
    if (was_unit_walked) {
        return;
    }
    was_unit_walked = true;
    walk_declarations();
    finish_expression_rules();
    walk_home();
    walk_owner();
}

// The parse of the unit, and then the walks of its end.  When the parse
// returns, the front end has made each instantiation, each implicit member and
// the function of the dynamic initializers, and the middle end has not run.
// The middle end releases the parameters of a declaration that an emitted
// function calls, so a walk at PLUGIN_FINISH_UNIT did not see them, and an
// object compile and a parse gave different findings.
void parse_and_walk() {
    front_end_parse_file();
    if (!seen_error()) {
        walk_unit();
    }
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
    state.holds_bool_of.clear();
    state.placeholder_locals.clear();
    state.default_argument_of.clear();
    state.walked.clear();
    state.contract_walked.clear();
    state.top_facts.clear();
}

}  // namespace

// The report of the unit.  CAN_ADD_DEPENDENCIES is true at PLUGIN_FINISH_UNIT,
// before GCC writes the dependency file.
void report(bool can_add_dependencies) {
    if (state.was_reported) {
        return;
    }
    state.was_reported = true;
    finish_pending_include(nullptr);
    judge_folded_names();
    // Each use of a symbol of C language linkage that the unit did not define.
    for (const ForeignUse& use : state.foreign_uses) {
        if (state.defined_c_symbols.count(use.uid) == 0) {
            add_finding(Kind::c_library_call, nullptr, use.place, use.entity);
        }
    }
    report_include_closure();
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
    std::vector<std::string> lines;
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
        lines.push_back(report_line(rule, kind, *finding.file, finding.line, finding.column, entity));
        // A use that a row with `until` and `in` admits in a file of the
        // language is a finding and no error, as it is in a quarantined file.
        // The rule of the language files is the quarantine rule.
        Rule rule_of_finding = rule_of(finding.kind);
        bool is_language = finding.file->language_layer >= 0 && finding.pending == nullptr
                        && rule_of_finding == Rule::quarantine;
        // An enforce row of the scope include sets the mode of the include
        // kinds only, and each other kind keeps the mode of the scope all.
        bool is_file_error =
            is_include_kind(finding.kind) ? finding.file->is_include_error_mode : finding.file->is_error_mode;
        bool is_error = state.is_error_mode || is_file_error || is_language;
        // The contract rule and the unit facts give an error in each mode.
        if (finding.kind == Kind::contract_specifier && !is_out) {
            report_contract(finding);
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
        } else if (!is_error && state.out_dir.empty() && !has_section) {
            inform(finding.spelling, "quarantine: %s %s", kind.c_str(), entity.c_str());
        }
    }
    // Each region, for the ledger of the rule quarantine.  A region is no
    // finding.
    for (const Region& region : state.regions) {
        lines.push_back(report_line(rule_name(Rule::quarantine), kRegionKind, *region.file, region.begin_line, 1,
                                    region.reason));
    }
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
            if (!crucible_rows::number(value, UINT64_MAX, state.padded_list_limit)
                || state.padded_list_limit == 0) {
                error("quarantine: padded-list-limit is %qs, and the threshold of the rule padded-list must be a "
                      "whole number of 1 or more",
                      value.c_str());
                return 1;
            }
            has_padded_limit = true;
        } else {
            error("quarantine: unknown argument %qs; the arguments are root, build, rules, mode, out, stamps, stamp, "
                  "kinds, sentinel, not-standalone and padded-list-limit",
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
        && (state.sentinel.front() == '/'
            || ::stat((state.root + "/" + state.sentinel).c_str(), &sentinel_status) != 0
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
    if (!load_rule_table(rules_argument)) {
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
    register_unit_facts(plugin_info->base_name);
    register_expressions(plugin_info->base_name);
    register_declarations(plugin_info->base_name);
    register_home(plugin_info->base_name);
    register_owner(plugin_info->base_name);
    front_end_parse_file = lang_hooks.parse_file;
    lang_hooks.parse_file = parse_and_walk;
    register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT, on_finish_unit, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH, on_finish, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_START, on_collection, nullptr);
    return 0;
}
