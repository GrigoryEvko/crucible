// The reader of the rule table.  rows.h reads the rows grammar of
// utils/assay/core/rows.py, and this file reads the schema of the rule table
// on those rows, as utils/assay/core/ruletable.py does.  The plant and the
// control directories of the rule quarantine in utils/plants/quarantine/ give
// the two readers the same tables, and each reader must give the same verdict
// at the same line.
//
// THE SCHEMA
//     The first word of a row is its kind, and the head comment of the table
//     gives the words of each kind.  A row has at most one cell, such as the
//     reason of an admit row or of a home row.  Only a witness row has two
//     cells: its type and its expression.  The rows grammar divides the words
//     only at a space and a tab, and a word or a cell that holds a different
//     white-space character is an error, because a reader that divides at each
//     white-space character would read different words.

#include "plugin.h"

#include "rows.h"

namespace crucible_plugin {

namespace {

// The code point of the first white-space character of TEXT that is not a
// space or a tab, or -1 when TEXT holds no such character.  The characters are
// the characters of Python str.isspace(): the ASCII control characters 0x0B to
// 0x0D and 0x1C to 0x1F, and the UTF-8 encodings of U+0085, U+00A0, U+1680,
// U+2000 to U+200A, U+2028, U+2029, U+202F, U+205F and U+3000.  A lead byte of
// UTF-8 is never a continuation byte, so a byte sequence below names its code
// point only.  Complexity: linear in the length of TEXT.
long white_space_code(const std::string& text) {
    auto byte_at = [&text](std::size_t index) -> unsigned {
        return index < text.size() ? static_cast<unsigned char>(text[index]) : 0U;
    };
    for (std::size_t index = 0; index < text.size(); ++index) {
        unsigned lead = byte_at(index);
        unsigned second = byte_at(index + 1);
        unsigned third = byte_at(index + 2);
        if ((lead >= 0x0BU && lead <= 0x0DU) || (lead >= 0x1CU && lead <= 0x1FU)) {
            return static_cast<long>(lead);
        }
        if (lead == 0xC2U && (second == 0x85U || second == 0xA0U)) {
            return static_cast<long>(second);
        }
        if (lead == 0xE1U && second == 0x9AU && third == 0x80U) {
            return 0x1680L;
        }
        bool is_general_space =
            (third >= 0x80U && third <= 0x8AU) || third == 0xA8U || third == 0xA9U || third == 0xAFU;
        if (lead == 0xE2U && second == 0x80U && is_general_space) {
            return 0x2000L + static_cast<long>(third - 0x80U);
        }
        if (lead == 0xE2U && second == 0x81U && third == 0x9FU) {
            return 0x205FL;
        }
        if (lead == 0xE3U && second == 0x80U && third == 0x80U) {
            return 0x3000L;
        }
    }
    return -1;
}

// The error of a row that holds the white-space character CODE.
// ruletable.white_space_message() gives the same text.
std::string white_space_message(long code) {
    std::string hex;  // at least four upper-case digits, as the format U+%04X gives
    for (long rest = code; rest > 0 || hex.size() < 4; rest >>= 4) {
        hex.insert(hex.begin(), "0123456789ABCDEF"[rest & 0xF]);
    }
    return "the row holds the white-space character U+" + hex + ".  Only a space and a tab divide the words of a row";
}

// The first white-space character of the words and the cell of ROW that is not
// a space or a tab, or -1.  The words and the cells are in the order of the
// line, so the answer is the first such character of the line.
long white_space_code_of(const crucible_rows::Row& row) {
    for (const std::string& word : row.words) {
        if (long code = white_space_code(word); code >= 0) {
            return code;
        }
    }
    for (const std::string& cell : row.cells) {
        if (long code = white_space_code(cell); code >= 0) {
            return code;
        }
    }
    return -1;
}

bool is_plain_path(const std::string& path) {
    if (path.empty() || path.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    std::size_t length = path.back() == '/' ? path.size() - 1 : path.size();
    while (start <= length) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos || end > length) {
            end = length;
        }
        std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool is_header(const std::string& text) { return text.size() >= 3 && text.front() == '<' && text.back() == '>'; }

bool is_digits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

// The largest number of a rank or an arity.  ruletable.MAX_NUMBER gives the
// same value.
constexpr int kMaxNumber = INT_MAX;

// Reads the number TEXT, a run of digits (is_digits), into VALUE.  Returns
// false when the value is more than kMaxNumber.  crucible_rows::number checks
// each step before it adds a digit, so no step overflows.
bool number_value(const std::string& text, int& value) {
    std::uint64_t read = 0;
    if (!crucible_rows::number(text, static_cast<std::uint64_t>(kMaxNumber), read)) {
        return false;
    }
    value = static_cast<int>(read);
    return true;
}

// The error of a number that is more than kMaxNumber.  ruletable.number_value()
// gives the same text.
std::string number_message(const std::string& text) {
    return "the number " + text + " is more than " + std::to_string(kMaxNumber)
         + ", the largest number of a rank or an arity";
}

// The error of an admit row that does not obey the format of its words.
constexpr const char* kAdmitFormat =
    "an admit row is 'admit ENTRY [RESTRICTION...] | REASON'.  The restrictions are 'until FAMILY', 'arity N', "
    "'parameters', 'plain-result' and 'consteval-only' of a name, 'concepts' of a header, 'unless MACRO' and "
    "'in PATH...', each one time, and the reason is necessary";

bool is_identifier(const std::string& text) {
    if (text.empty() || (text[0] >= '0' && text[0] <= '9')) {
        return false;
    }
    for (char character : text) {
        bool is_word = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9') || character == '_';
        if (!is_word) {
            return false;
        }
    }
    return true;
}

// The words of one admit row after the row kind: ENTRY, then each restriction
// one time at most, in any order, and `in PATH...` last.  Returns the error of
// the row when a restriction is unknown, repeated, malformed or does not fit
// the entry, and an empty text when the row obeys the format.
std::string parse_admit(const std::vector<std::string>& args, AdmitRow& row) {
    row.entry = args[0];
    row.is_header = is_header(args[0]);
    if (row.is_header) {
        row.entry = args[0].substr(1, args[0].size() - 2);
    } else if (!args[0].empty() && args[0][0] == '<') {
        return kAdmitFormat;
    }
    std::vector<std::string> seen;
    std::size_t index = 1;
    while (index < args.size()) {
        const std::string& word = args[index];
        for (const std::string& known : seen) {
            if (known == word) {
                return kAdmitFormat;
            }
        }
        seen.push_back(word);
        std::string value = index + 1 < args.size() ? args[index + 1] : std::string{};
        if (word == "until" && !value.empty()) {
            row.until = value;
            index += 2;
        } else if (word == "plain-result" && !row.is_header) {
            row.is_plain_result = true;
            index += 1;
        } else if (word == "consteval-only" && !row.is_header) {
            row.is_consteval_only = true;
            index += 1;
        } else if (word == "arity" && is_digits(value) && !row.is_header) {
            if (!number_value(value, row.arity)) {
                return number_message(value);
            }
            index += 2;
        } else if (word == "concepts" && row.is_header) {
            row.is_concepts_only = true;
            index += 1;
        } else if (word == "parameters" && !row.is_header) {
            row.is_parameters_only = true;
            index += 1;
        } else if (word == "unless" && is_identifier(value)) {
            row.unless_macro = value;
            index += 2;
        } else if (word == "in" && !value.empty()) {
            for (std::size_t path = index + 1; path < args.size(); ++path) {
                if (!is_plain_path(args[path])) {
                    return kAdmitFormat;
                }
                row.in_paths.push_back(args[path]);
            }
            index = args.size();
        } else {
            return kAdmitFormat;
        }
    }
    if (!row.until.empty()) {
        row.pending_kind = std::string(kPendingKindPrefix) + args[0];
    }
    return {};
}

// True when TEXT is a qualified name: identifiers that "::" divides, with no
// leading "::".
bool is_qualified_name(const std::string& text) {
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find("::", start);
        if (!is_identifier(text.substr(start, end == std::string::npos ? std::string::npos : end - start))) {
            return false;
        }
        if (end == std::string::npos) {
            return true;
        }
        start = end + 2;
    }
}

// True when TEXT is the path of one file of the source root, not of the build
// directory.
bool is_file_path(const std::string& text) {
    return is_plain_path(text) && text.back() != '/' && !has_prefix(text, kBuildPrefix);
}

// The full name of the class TYPE: the text with each template argument list
// removed.  Returns an empty text when the angle brackets of TYPE do not pair,
// or when the remaining text is not identifiers with "::" between them.
// ruletable.witness_name() gives the same answer.  Complexity: linear in the
// length of TYPE.
std::string witness_name(const std::string& type) {
    std::string kept;
    int depth = 0;
    for (char character : type) {
        if (character == '<') {
            ++depth;
        } else if (character == '>') {
            if (--depth < 0) {
                return {};
            }
        } else if (depth == 0) {
            kept.push_back(character);
        }
    }
    return depth == 0 && is_qualified_name(kept) ? kept : std::string{};
}

// The error of a cite row, a grant row and a witness row that do not obey the
// format of their words.  ruletable.py gives the same texts.
constexpr const char* kCiteFormat =
    "a cite row is 'cite PREDICATE PROCEDURE...', with the qualified name of a predicate class and the name of each "
    "decide procedure that tests it";
constexpr const char* kGrantFormat =
    "a grant row is 'grant FILE ALIAS ENUMERATION [DOOR...]', with the grant file, the qualified names of its alias "
    "and of the enumeration of the system calls, and each door file";
constexpr const char* kWitnessFormat =
    "a witness row is 'witness HEADER REFUSAL | TYPE | EXPRESSION', with the HEADER of the type in angle brackets, a "
    "REFUSAL of private, deleted or no-match, the class TYPE with its template arguments, and the expression of a "
    "forger";

// The error of a home row that does not obey the format of its words.
// ruletable.HOME_FORMAT gives the same text.
constexpr const char* kHomeFormat =
    "a home row is 'home RULE ENTITY [PATH...] | REASON', and a home-args row is 'home-args RULE ENTITY [PATH...] | "
    "REASON'.  RULE is a rule of the family home, ENTITY is a qualified name or one name, and the reason is "
    "necessary";

// The words of one home row or home-args row after the row kind: RULE, ENTITY
// and the paths.  Returns the error of the row, or an empty text when the row
// obeys the format.
std::string parse_home(const std::vector<std::string>& args, bool is_args, HomeRow& row) {
    if (args.size() < 2) {
        return kHomeFormat;
    }
    bool is_rule = false;
    for (Rule rule : kHomeRules) {
        if (args[0] == rule_name(rule)) {
            row.rule = rule;
            is_rule = true;
        }
    }
    if (!is_rule) {
        return "the rule " + args[0]
             + " is no rule of the family home; the rules are federation-admission, host-owners, "
               "no-combine-ids-duplicate, splits-orphan and trait-guard";
    }
    row.entity = args[1];
    row.is_args = is_args;
    if (!is_qualified_name(row.entity)) {
        return "the entity " + row.entity + " is not a qualified name: each part is an identifier";
    }
    row.is_one_name = row.entity.find("::") == std::string::npos;
    if (row.is_one_name && is_args) {
        return "a home-args row names a template by its qualified name, and " + row.entity + " is one name";
    }
    for (std::size_t index = 2; index < args.size(); ++index) {
        if (!is_plain_path(args[index]) || has_prefix(args[index], kBuildPrefix)) {
            return "a path of a home row must be relative to the source root, with no '.', '..' or empty "
                   "component, and not in the build directory";
        }
        row.paths.push_back(args[index]);
    }
    return {};
}

// The namespace of a sealed PREFIX: its path after include/, one name for each
// directory.  Returns an empty list when PREFIX is no directory under include/
// or a part is no identifier.  ruletable.seal_namespace() gives the same
// answer.
std::vector<std::string> seal_namespace(const std::string& prefix) {
    const std::string include = "include/";
    if (!has_prefix(prefix, include) || prefix.back() != '/' || prefix.size() <= include.size()) {
        return {};
    }
    std::vector<std::string> names;
    std::size_t start = include.size();
    while (start < prefix.size()) {
        std::size_t end = prefix.find('/', start);
        std::string part = prefix.substr(start, end - start);
        if (!is_identifier(part)) {
            return {};
        }
        names.push_back(part);
        start = end + 1;
    }
    return names;
}

// The error of a seal row that does not obey the format of its words.
constexpr const char* kSealFormat =
    "a seal row is 'seal ROOT PREFIX...', with one ROOT file of the source root and each PREFIX a directory under "
    "include/ whose parts are identifiers";

// The bytes of the file at PATH, or false after an error.
bool read_bytes(const std::string& path, std::string& text) {
    FILE* stream = fopen(path.c_str(), "rb");
    if (stream == nullptr) {
        error("quarantine: cannot read the rule table %s: %m", path.c_str());
        return false;
    }
    char chunk[4096];
    for (std::size_t count = 0; (count = fread(chunk, 1, sizeof chunk, stream)) > 0;) {
        text.append(chunk, count);
    }
    bool is_read = ferror(stream) == 0;
    fclose(stream);
    if (!is_read) {
        error("quarantine: cannot read the rule table %s", path.c_str());
    }
    return is_read;
}

}  // namespace

// Reads the rule table at PATH into state.table.  The schema and each check
// are those of utils/assay/core/ruletable.py.  Returns false after an error.
// Complexity: O(rows * rows of the same kind), for the checks of a second row.
bool load_rule_table(const std::string& path) {
    std::string text;
    if (!read_bytes(path, text)) {
        return false;
    }
    crucible_rows::Rows rows = crucible_rows::parse(text);
    if (!rows.is_valid()) {
        error("quarantine: %s:%d: %s", path.c_str(), rows.problem_line, rows.problem.c_str());
        return false;
    }
    RuleTable table;
    std::vector<std::pair<std::string, std::string>> class_paths;  // a path of a layer or quarantine row, and its row
    std::vector<std::pair<std::string, int>> allow_layers;  // the layer name of each allow row, and its line
    std::vector<std::string> allow_headers;
    std::vector<std::pair<std::string, int>> in_path_lines;  // each path after `in`, and its line
    std::vector<std::pair<std::string, int>> language_layers;  // the layer name of each language row, and its line
    std::vector<std::pair<std::string, int>> language_path_lines;  // each path of a language row, and its line
    // The entry of each admit row as the table spells it, the root of each
    // seal row and the header of each heavy and light row, and the line of each.
    crucible_rows::KeyLines admit_lines;
    crucible_rows::KeyLines seal_lines;
    crucible_rows::KeyLines cost_lines;
    crucible_rows::KeyLines cite_lines;
    crucible_rows::KeyLines grant_lines;
    // The entity of each home row and home-args row, and its line.
    crucible_rows::KeyLines home_lines;
    crucible_rows::KeyLines witness_lines;  // the class name of each witness row, and its line
    bool is_valid = true;
    int line_number = 0;
    auto refuse = [&](const std::string& message) {
        error("quarantine: %s:%d: %s", path.c_str(), line_number, message.c_str());
        is_valid = false;
    };
    auto add_class_path = [&](const std::string& class_path, const std::string& row) {
        for (const auto& [known, known_row] : class_paths) {
            if (known == class_path) {
                error("quarantine: %s:%d: the path %s is also in %s", path.c_str(), line_number, class_path.c_str(),
                      known_row.c_str());
                is_valid = false;
                return;
            }
        }
        class_paths.emplace_back(class_path, row);
    };
    for (const crucible_rows::Row& row : rows.rows) {
        line_number = row.line;
        long white_space = white_space_code_of(row);
        if (white_space >= 0) {
            refuse(white_space_message(white_space));
            continue;
        }
        if (row.cells.size() > 1 && row.words[0] != "witness") {
            refuse("a row has at most one cell: the reason of an admit row or of a home row, or the measure of a heavy "
                   "or a light row.  Only a witness row has two cells");
            continue;
        }
        const std::string& kind = row.words[0];
        std::vector<std::string> args(row.words.begin() + 1, row.words.end());
        std::string reason = row.cells.empty() ? std::string{} : row.cells[0];
        if (kind == "layer") {
            if (args.size() < 3 || !is_digits(args[1])) {
                refuse("a layer row is 'layer NAME RANK PATH...', with a RANK of digits");
                continue;
            }
            LayerRow layer;
            layer.name = args[0];
            if (!number_value(args[1], layer.rank)) {
                refuse(number_message(args[1]));
                continue;
            }
            for (const LayerRow& known : table.layers) {
                if (known.name == layer.name) {
                    refuse("the layer has a second row");
                } else if (known.rank == layer.rank) {
                    refuse("the rank belongs to a second layer");
                }
            }
            for (std::size_t index = 2; index < args.size(); ++index) {
                if (!is_plain_path(args[index])) {
                    refuse("a path must be relative to the source root, with no '.', '..' or empty component");
                    continue;
                }
                if (has_prefix(args[index], kBuildPrefix)) {
                    refuse("a layer row holds no path of the build directory, because the build writes no base file");
                    continue;
                }
                add_class_path(args[index], "the layer " + layer.name);
                layer.paths.push_back(args[index]);
            }
            table.layers.push_back(std::move(layer));
        } else if (kind == "allow") {
            if (args.size() != 2 || !is_header(args[1])) {
                refuse("an allow row is 'allow LAYER HEADER', with a HEADER in angle brackets");
                continue;
            }
            allow_layers.emplace_back(args[0], line_number);
            allow_headers.push_back(args[1].substr(1, args[1].size() - 2));
        } else if (kind == "door") {
            if (args.size() != 2 || !is_header(args[0]) || !is_plain_path(args[1]) || args[1].back() == '/'
                || has_prefix(args[1], kBuildPrefix)) {
                refuse("a door row is 'door HEADER OWNER', with a HEADER in angle brackets and one OWNER file of the "
                       "source root");
                continue;
            }
            DoorRow door{args[0].substr(1, args[0].size() - 2), args[1]};
            for (const DoorRow& known : table.doors) {
                if (known.header == door.header) {
                    refuse("the header has a second door");
                }
            }
            table.doors.push_back(std::move(door));
        } else if (kind == "admit") {
            AdmitRow admit;
            std::string problem = args.empty() || reason.empty() ? std::string(kAdmitFormat) : parse_admit(args, admit);
            if (!problem.empty()) {
                refuse(problem);
                continue;
            }
            // The plugin keeps the first row of an entry (plugin_init), so a
            // second row would make the order of the rows decide the findings.
            if (int first_line = admit_lines.add(args[0], line_number); first_line > 0) {
                refuse("the entry " + args[0] + " has a second admit row, and its first admit row is on line "
                       + std::to_string(first_line));
                continue;
            }
            for (const std::string& in_path : admit.in_paths) {
                in_path_lines.emplace_back(in_path, line_number);
            }
            table.admits.push_back(std::move(admit));
        } else if (kind == "quarantine") {
            if (args.size() != 1 || !is_plain_path(args[0])) {
                refuse("a quarantine row is 'quarantine PATH', with a PATH relative to the source root");
                continue;
            }
            add_class_path(args[0], "a quarantine row");
            table.quarantines.push_back(args[0]);
        } else if (kind == "enforce") {
            if (args.size() != 2 || (args[1] != "report" && args[1] != "error") || !is_plain_path(args[0])) {
                refuse("an enforce row is 'enforce PATH MODE', and MODE is report or error");
                continue;
            }
            for (const EnforceRow& known : table.enforces) {
                if (known.path == args[0]) {
                    refuse("the path has a second enforce row");
                }
            }
            table.enforces.push_back(EnforceRow{args[0], args[1] == "error"});
        } else if (kind == "language") {
            if (args.size() < 2) {
                refuse("a language row is 'language LAYER PATH...'");
                continue;
            }
            LanguageRow language;
            for (std::size_t index = 1; index < args.size(); ++index) {
                if (!is_plain_path(args[index])) {
                    refuse("a path must be relative to the source root, with no '.', '..' or empty component");
                    continue;
                }
                for (const auto& [known, known_line] : language_path_lines) {
                    if (known == args[index]) {
                        refuse("the path has a second language row");
                    }
                }
                language_path_lines.emplace_back(args[index], line_number);
                language.paths.push_back(args[index]);
            }
            language_layers.emplace_back(args[0], line_number);
            table.languages.push_back(std::move(language));
        } else if (kind == "seal") {
            if (args.size() < 2 || !is_plain_path(args[0]) || args[0].back() == '/' || has_prefix(args[0], kBuildPrefix)
                || !reason.empty()) {
                refuse(kSealFormat);
                continue;
            }
            SealRow seal;
            seal.root = args[0];
            bool is_valid_row = true;
            for (std::size_t index = 1; index < args.size() && is_valid_row; ++index) {
                std::vector<std::string> names = is_plain_path(args[index]) ? seal_namespace(args[index])
                                                                            : std::vector<std::string>{};
                is_valid_row = !names.empty();
                seal.prefixes.push_back(args[index]);
                seal.namespaces.push_back(std::move(names));
            }
            if (!is_valid_row) {
                refuse(kSealFormat);
                continue;
            }
            if (int first_line = seal_lines.add(seal.root, line_number); first_line > 0) {
                refuse("the root " + seal.root + " has a second seal row, and its first seal row is on line "
                       + std::to_string(first_line));
                continue;
            }
            table.seals.push_back(std::move(seal));
        } else if (kind == "heavy" || kind == "light") {
            if (args.size() != 1 || !is_header(args[0])) {
                refuse("a " + kind + " row is '" + kind + " HEADER [| MEASURE]', with a HEADER in angle brackets");
                continue;
            }
            if (int first_line = cost_lines.add(args[0], line_number); first_line > 0) {
                refuse("the header " + args[0] + " has a second heavy or light row, and its first row is on line "
                       + std::to_string(first_line));
                continue;
            }
            table.header_costs.emplace(args[0].substr(1, args[0].size() - 2),
                                       kind == "heavy" ? HeaderCost::heavy : HeaderCost::light);
        } else if (kind == "cite") {
            bool is_valid_row = args.size() >= 2 && reason.empty() && is_qualified_name(args[0]);
            for (std::size_t index = 1; index < args.size() && is_valid_row; ++index) {
                is_valid_row = is_identifier(args[index]);
            }
            if (!is_valid_row) {
                refuse(kCiteFormat);
                continue;
            }
            if (int first_line = cite_lines.add(args[0], line_number); first_line > 0) {
                refuse("the predicate " + args[0] + " has a second cite row, and its first cite row is on line "
                       + std::to_string(first_line));
                continue;
            }
            table.cites.push_back(CiteRow{args[0], std::vector<std::string>(args.begin() + 1, args.end())});
        } else if (kind == "grant") {
            bool is_valid_row = args.size() >= 3 && reason.empty() && is_file_path(args[0])
                             && is_qualified_name(args[1]) && is_qualified_name(args[2]);
            for (std::size_t index = 3; index < args.size() && is_valid_row; ++index) {
                is_valid_row = is_file_path(args[index]);
            }
            if (!is_valid_row) {
                refuse(kGrantFormat);
                continue;
            }
            if (int first_line = grant_lines.add(args[0], line_number); first_line > 0) {
                refuse("the file " + args[0] + " has a second grant row, and its first grant row is on line "
                       + std::to_string(first_line));
                continue;
            }
            table.grants.push_back(
                GrantRow{args[0], args[1], args[2], std::vector<std::string>(args.begin() + 3, args.end())});
        } else if (kind == "witness") {
            std::string name = row.cells.size() == 2 ? witness_name(row.cells[0]) : std::string{};
            bool is_valid_row = args.size() == 2 && is_header(args[0])
                             && is_file_path(args[0].substr(1, args[0].size() - 2))
                             && (args[1] == "private" || args[1] == "deleted" || args[1] == "no-match") && !name.empty()
                             && !row.cells[1].empty();
            if (!is_valid_row) {
                refuse(kWitnessFormat);
                continue;
            }
            if (int first_line = witness_lines.add(name, line_number); first_line > 0) {
                refuse("the class " + name + " has a second witness row, and its first witness row is on line "
                       + std::to_string(first_line));
                continue;
            }
            table.witnesses.push_back(
                WitnessRow{args[0].substr(1, args[0].size() - 2), args[1], row.cells[0], std::move(name)});
        } else if (kind == "home" || kind == "home-args") {
            HomeRow home;
            std::string problem = reason.empty() ? std::string(kHomeFormat) : parse_home(args, kind == "home-args", home);
            if (!problem.empty()) {
                refuse(problem);
                continue;
            }
            // One row gives the home of an entity.  With a second row, the
            // sequence of the rows can change the findings.
            if (int first_line = home_lines.add(home.entity, line_number); first_line > 0) {
                refuse("the entity " + home.entity + " has a second home row, and its first home row is on line "
                       + std::to_string(first_line));
                continue;
            }
            home.line = line_number;
            table.homes.push_back(std::move(home));
        } else {
            refuse("the row kind is unknown; the kinds are layer, allow, door, admit, quarantine, enforce, language, "
                   "seal, heavy, light, cite, grant, home, home-args and witness");
        }
    }
    for (std::size_t index = 0; index < allow_layers.size(); ++index) {
        int layer = -1;
        for (std::size_t known = 0; known < table.layers.size(); ++known) {
            if (table.layers[known].name == allow_layers[index].first) {
                layer = static_cast<int>(known);
            }
        }
        if (layer < 0) {
            error("quarantine: %s:%d: the allow row names the layer %s, and no layer row gives it", path.c_str(),
                  allow_layers[index].second, allow_layers[index].first.c_str());
            is_valid = false;
            continue;
        }
        table.allows.emplace_back(layer, allow_headers[index]);
    }
    for (std::size_t index = 0; index < language_layers.size(); ++index) {
        for (std::size_t known = 0; known < table.layers.size(); ++known) {
            if (table.layers[known].name == language_layers[index].first) {
                table.languages[index].layer = static_cast<int>(known);
            }
        }
        if (table.languages[index].layer < 0) {
            error("quarantine: %s:%d: the language row names the layer %s, and no layer row gives it", path.c_str(),
                  language_layers[index].second, language_layers[index].first.c_str());
            is_valid = false;
        }
    }
    // A row with `in` admits only in a base file of the language, and a
    // language row holds base files only.
    auto refuse_outside_base = [&](const std::vector<std::pair<std::string, int>>& paths, const char* what) {
        for (const auto& [row_path, row_line] : paths) {
            bool is_base = false;
            for (const LayerRow& layer : table.layers) {
                for (const std::string& layer_path : layer.paths) {
                    is_base = is_base || row_holds(layer_path, row_path);
                }
            }
            if (!is_base) {
                error("quarantine: %s:%d: the path %s of %s is in no layer; it must be a path of the base",
                      path.c_str(), row_line, row_path.c_str(), what);
                is_valid = false;
            }
        }
    };
    refuse_outside_base(in_path_lines, "an in restriction");
    refuse_outside_base(language_path_lines, "a language row");
    if (!is_valid) {
        return false;
    }
    state.table = std::move(table);
    return true;
}

}  // namespace crucible_plugin
