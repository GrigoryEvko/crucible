// The reader of the rows grammar in C++.  utils/assay/core/rows.py gives the
// grammar, and this reader must agree with its reader parse().  The
// quarantine plugin reads its data files through this header, and
// rows_reader.cpp is a host program that prints the rows of a file.  The rule
// rows-readers (`guard check --rule rows-readers`) gives the same files to the
// two readers.  It refuses a file when the two readers give different rows or
// a different first problem.
//
// THE RESULT
//     parse() gives each row of a file: its line from 1, the words of its head
//     and the value of each cell.  A file that does not obey the grammar gives
//     no row, and the line and the message of its first problem.  The line of
//     that problem is the line that the Python reader gives, and the message
//     can be different.
//
// THE SEQUENCE OF THE CHECKS
//     The Python reader decodes all the bytes of the file as UTF-8 before it
//     reads a line.  A byte that is not part of a UTF-8 sequence is then the
//     first problem.  A problem of the grammar on a line before that byte does
//     not come first.  Then a file that does not end with a line break is a
//     problem at its last line.  Then each line from line 1 gets the checks of
//     the grammar.
//
// THE BYTES
//     This reader reads bytes, and the Python reader reads code points.  The
//     two give the same result: each byte that the grammar examines is ASCII,
//     and no byte of a UTF-8 sequence of more than one byte is ASCII.
//
// No function of this header uses an exception, and the header includes no
// GCC header.  The plugin includes it with -std=gnu++17 -fno-rtti.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crucible_rows {

// One row: its line from 1, the words of its head and its cells.
struct Row {
    int line = 0;
    std::vector<std::string> words;
    std::vector<std::string> cells;
};

// The rows of one file, or its first problem.  problem_line is 0 when the file obeys the grammar.
struct Rows {
    std::vector<Row> rows;
    int problem_line = 0;
    std::string problem;
    bool is_valid() const { return problem_line == 0; }
};

namespace detail {

// The largest line of a row, the largest int.  A file of more lines is a problem at this line.
constexpr int kMaxLine = __INT_MAX__;
constexpr std::size_t kNone = std::string_view::npos;
constexpr std::string_view kSeparator = " | ";
constexpr std::string_view kDividers = " \t";
constexpr std::string_view kEmptyCell = "-";

// Whether a byte divides two words: a space or a horizontal tab, and no other byte.
inline bool is_divider(char character) { return character == ' ' || character == '\t'; }

// Return the line from 1 of the byte at INDEX: the line breaks before it, plus 1.  A line more than kMaxLine gives
// kMaxLine.  Complexity: linear in INDEX.
inline int line_of(std::string_view bytes, std::size_t index) {
    std::size_t breaks = 0;
    for (std::size_t position = 0; position < index && position < bytes.size(); ++position) {
        if (bytes[position] == '\n') ++breaks;
    }
    return breaks >= static_cast<std::size_t>(kMaxLine) ? kMaxLine : static_cast<int>(breaks) + 1;
}

// Return the index of the first byte that starts no correct UTF-8 sequence, or kNone.  The correct sequences are
// those of table 3-7 of the Unicode standard, which the strict codec "utf-8" of Python accepts.  They have no
// overlong encoding, no surrogate and no value more than U+10FFFF, and the end of the bytes does not cut them.  The
// index of a bad sequence is the index of its first byte, as UnicodeDecodeError.start gives it.  Complexity: linear
// in the number of bytes.
inline std::size_t first_bad_utf8(std::string_view bytes) {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[index]);
        if (lead < 0x80) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        // The range of the second byte.  Each byte after the second byte is in 0x80..0xBF.
        unsigned char second_low = 0x80;
        unsigned char second_high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead == 0xE0) {
            length = 3;
            second_low = 0xA0;
        } else if (lead == 0xED) {
            length = 3;
            second_high = 0x9F;
        } else if (lead >= 0xE1 && lead <= 0xEF) {
            length = 3;
        } else if (lead == 0xF0) {
            length = 4;
            second_low = 0x90;
        } else if (lead == 0xF4) {
            length = 4;
            second_high = 0x8F;
        } else if (lead >= 0xF1 && lead <= 0xF3) {
            length = 4;
        } else {
            return index;
        }
        if (bytes.size() - index < length) return index;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(bytes[index + offset]);
            const unsigned char low = offset == 1 ? second_low : 0x80;
            const unsigned char high = offset == 1 ? second_high : 0xBF;
            if (next < low || next > high) return index;
        }
        index += length;
    }
    return kNone;
}

// Divide a head at each run of spaces and tabs, and only there.  Complexity: linear in the length of the head.
inline std::vector<std::string> head_words(std::string_view head) {
    std::vector<std::string> words;
    std::size_t index = 0;
    while (index < head.size()) {
        if (is_divider(head[index])) {
            ++index;
            continue;
        }
        std::size_t end = head.find_first_of(kDividers, index);
        if (end == kNone) end = head.size();
        words.emplace_back(head.substr(index, end - index));
        index = end;
    }
    return words;
}

// Divide the text of a row into its head and the raw text of its cells, as split_row() of rows.py does.  With no
// backslash in the row, the parts are those of str.split(" | ").  With a backslash, a scan from left to right does
// not examine the byte after a backslash.  A separator is then a bar with a space before it and a space after it.
// The two paths give different cells for " | | ", and each of those cells is a problem.  Complexity: linear in the
// length of the row.
inline std::vector<std::string_view> split_row(std::string_view text) {
    std::vector<std::string_view> parts;
    if (text.find('\\') == kNone) {
        std::size_t start = 0;
        while (true) {
            const std::size_t found = text.find(kSeparator, start);
            if (found == kNone) break;
            parts.push_back(text.substr(start, found - start));
            start = found + kSeparator.size();
        }
        parts.push_back(text.substr(start));
        return parts;
    }
    std::size_t start = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const char character = text[index];
        if (character == '\\') {
            index += 2;
            continue;
        }
        const bool is_separator = character == '|' && index > 0 && text[index - 1] == ' ' && index + 1 < text.size()
                               && text[index + 1] == ' ';
        if (is_separator) {
            // Python gives an empty slice when the separator starts at the end of the last one.
            const std::size_t end = index - 1;
            parts.push_back(end > start ? text.substr(start, end - start) : std::string_view{});
            start = index + 2;
            index += 2;
            continue;
        }
        ++index;
    }
    parts.push_back(text.substr(start));
    return parts;
}

// Write the value of one cell into VALUE: each escape replaced by its byte, and "" for the cell "-".  Returns
// nullptr, or the message of the problem of the cell.  Complexity: linear in the length of the cell.
inline const char* unescape(std::string_view text, std::string& value) {
    if (text.empty()) return "a cell is empty.  Write - for an empty cell";
    if (is_divider(text.front()) || is_divider(text.back())) {
        return "a cell has a space or a tab at its start or its end.  Remove it";
    }
    value.clear();
    if (text == kEmptyCell) return nullptr;
    if (text.find('\\') == kNone) {
        if (text.find('|') != kNone) return "a cell holds a bar with no backslash before it.  Write \\| for a bar";
        value.assign(text);
        return nullptr;
    }
    std::size_t index = 0;
    while (index < text.size()) {
        const char character = text[index];
        if (character == '\\') {
            const bool is_escape = index + 1 < text.size() && (text[index + 1] == '|' || text[index + 1] == '\\');
            if (!is_escape) {
                return "a cell holds a backslash before a byte that is not a bar or a backslash.  Write \\\\ for a "
                       "backslash";
            }
            value.push_back(text[index + 1]);
            index += 2;
            continue;
        }
        if (character == '|') return "a cell holds a bar with no backslash before it.  Write \\| for a bar";
        value.push_back(character);
        ++index;
    }
    return nullptr;
}

// Keep the first problem of a file, and remove the rows that the reader found before it.
inline Rows& refuse(Rows& result, int line, std::string message) {
    result.rows.clear();
    result.problem_line = line;
    result.problem = std::move(message);
    return result;
}

// Read one line of a file: no row for a blank line or a comment, one row for each other line.  Returns false after
// a problem, which refuse() keeps in RESULT.  Complexity: linear in the length of the line.
inline bool read_line(std::string_view raw, int line, Rows& result) {
    if (raw.find('\r') != kNone) {
        refuse(result, line, "the line holds a carriage return.  Use a line feed only");
        return false;
    }
    const std::size_t first = raw.find_first_not_of(kDividers);
    if (first == kNone || raw[first] == '#') return true;
    const std::vector<std::string_view> parts = split_row(raw);
    Row row;
    row.line = line;
    row.words = head_words(parts.front());
    if (row.words.empty()) {
        refuse(result, line, "the row has no word before its first cell");
        return false;
    }
    for (const std::string& word : row.words) {
        if (word.find('|') != std::string::npos) {
            refuse(result, line, "a word holds a bar.  Put \" | \" between a word and a cell");
            return false;
        }
    }
    for (std::size_t index = 1; index < parts.size(); ++index) {
        std::string value;
        if (const char* problem = unescape(parts[index], value)) {
            refuse(result, line, problem);
            return false;
        }
        row.cells.push_back(std::move(value));
    }
    result.rows.push_back(std::move(row));
    return true;
}

}  // namespace detail

// Read the bytes of one file: the grammar of utils/assay/core/rows.py, and the UTF-8 check (THE SEQUENCE OF THE
// CHECKS).  Complexity: linear in the number of bytes.
inline Rows parse(std::string_view bytes) {
    Rows result;
    const std::size_t bad = detail::first_bad_utf8(bytes);
    if (bad != detail::kNone) {
        return detail::refuse(result, detail::line_of(bytes, bad),
                              "a byte of the line is not part of a UTF-8 sequence.  Write the file in UTF-8");
    }
    if (bytes.empty()) return result;
    if (bytes.back() != '\n') {
        return detail::refuse(result, detail::line_of(bytes, bytes.size()),
                              "the file ends with no line break.  End the last line with one");
    }
    const std::string_view body = bytes.substr(0, bytes.size() - 1);
    std::size_t start = 0;
    int line = 0;
    while (true) {
        if (line == detail::kMaxLine) return detail::refuse(result, line, "the file has too many lines");
        ++line;
        const std::size_t end = body.find('\n', start);
        const std::string_view raw = end == detail::kNone ? body.substr(start) : body.substr(start, end - start);
        if (!detail::read_line(raw, line, result)) return result;
        if (end == detail::kNone) break;
        start = end + 1;
    }
    return result;
}

// Read a number: one or more ASCII digits, at most MAXIMUM.  Zeros before the first other digit are permitted.  Each
// step does a check for overflow before it adds a digit, and no step causes an overflow.  Returns false for an empty
// text, a character that is not an ASCII digit, and a value more than MAXIMUM.  Complexity: linear in the length of
// the text.
inline bool number(std::string_view text, std::uint64_t maximum, std::uint64_t& value) {
    if (text.empty()) return false;
    std::uint64_t total = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') return false;
        const auto digit = static_cast<std::uint64_t>(character - '0');
        // total * 10 + digit <= maximum is true exactly when total <= (maximum - digit) / 10.
        if (digit > maximum || total > (maximum - digit) / 10) return false;
        total = total * 10 + digit;
    }
    value = total;
    return true;
}

// The keys of the rows of one kind, for a schema that refuses a second row with the same key.
class KeyLines {
public:
    // Record KEY at LINE.  Returns 0, or the line of the first row with the same key, which the call does not
    // replace.
    int add(std::string key, int line) {
        const auto [entry, is_new] = lines_.emplace(std::move(key), line);
        return is_new ? 0 : entry->second;
    }

private:
    std::unordered_map<std::string, int> lines_;
};

}  // namespace crucible_rows
