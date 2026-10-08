// rows_reader — the host program of rows.h.  The rule rows-readers compiles it
// and gives it the files that the Python reader of the rows grammar reads.
//
// USAGE
//     rows_reader FILE...
//
// THE OUTPUT
//     One line of JSON for each file, in the sequence of the arguments:
//         {"path":"FILE","rows":[{"line":1,"words":["w"],"cells":["c"]}]}
//         {"path":"FILE","problem":{"line":3,"message":"..."}}
//     A text escapes each quotation mark, each backslash and each byte less
//     than 0x20 (as \u00XX).  Each other byte goes out as it is.  rows.h
//     refuses a file that is not UTF-8.  The text of a row is then UTF-8.
//
// THE EXIT
//     0 after the line of each file, also when a file does not obey the
//     grammar.  2 when the arguments name no file, when the program cannot
//     read a file, or when it cannot write the output.  The program then
//     writes a message to the standard error and nothing to the standard
//     output.
//
// The program uses the flags of the quarantine plugin: -std=gnu++17 -fno-rtti
// -fPIC -O2 -g -Wall -Wextra -Werror.
#include "rows.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The number of bytes of one read of a file.
constexpr std::size_t kChunk = 1 << 16;

// Read each byte of the file at PATH into BYTES.  Returns false when the program cannot open or read the file, for
// example a directory.  Complexity: linear in the number of bytes of the file.
bool read_file(const char* path, std::string& bytes) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    std::string chunk(kChunk, '\0');
    while (true) {
        const std::size_t count = std::fread(chunk.data(), 1, chunk.size(), file);
        bytes.append(chunk.data(), count);
        if (count < chunk.size()) break;
    }
    const bool has_failed = std::ferror(file) != 0;
    std::fclose(file);
    return !has_failed;
}

// Append TEXT to OUTPUT as a JSON string (THE OUTPUT).  Complexity: linear in the length of the text.
void append_text(std::string& output, std::string_view text) {
    constexpr std::string_view kHexDigits = "0123456789abcdef";
    output.push_back('"');
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output.push_back('\\');
            output.push_back(character);
        } else if (byte < 0x20) {
            output.append("\\u00");
            output.push_back(kHexDigits[byte >> 4]);
            output.push_back(kHexDigits[byte & 0x0F]);
        } else {
            output.push_back(character);
        }
    }
    output.push_back('"');
}

// Append a JSON list of texts to OUTPUT.  Complexity: linear in the length of the texts.
void append_texts(std::string& output, const std::vector<std::string>& texts) {
    output.push_back('[');
    for (std::size_t index = 0; index < texts.size(); ++index) {
        if (index > 0) output.push_back(',');
        append_text(output, texts[index]);
    }
    output.push_back(']');
}

// Append the line of one file to OUTPUT (THE OUTPUT).  Complexity: linear in the number of bytes of the rows.
void append_file(std::string& output, std::string_view path, const crucible_rows::Rows& result) {
    output.append("{\"path\":");
    append_text(output, path);
    if (!result.is_valid()) {
        output.append(",\"problem\":{\"line\":");
        output.append(std::to_string(result.problem_line));
        output.append(",\"message\":");
        append_text(output, result.problem);
        output.append("}}\n");
        return;
    }
    output.append(",\"rows\":[");
    for (std::size_t index = 0; index < result.rows.size(); ++index) {
        const crucible_rows::Row& row = result.rows[index];
        if (index > 0) output.push_back(',');
        output.append("{\"line\":");
        output.append(std::to_string(row.line));
        output.append(",\"words\":");
        append_texts(output, row.words);
        output.append(",\"cells\":");
        append_texts(output, row.cells);
        output.push_back('}');
    }
    output.append("]}\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: rows_reader FILE...\n", stderr);
        return 2;
    }
    std::string output;
    for (int index = 1; index < argc; ++index) {
        std::string bytes;
        if (!read_file(argv[index], bytes)) {
            std::fprintf(stderr, "rows_reader: the program cannot read %s.  Make sure that the path names a file\n",
                         argv[index]);
            return 2;
        }
        append_file(output, argv[index], crucible_rows::parse(bytes));
    }
    if (std::fwrite(output.data(), 1, output.size(), stdout) != output.size() || std::fflush(stdout) != 0) {
        std::fputs("rows_reader: the program cannot write the standard output\n", stderr);
        return 2;
    }
    return 0;
}
