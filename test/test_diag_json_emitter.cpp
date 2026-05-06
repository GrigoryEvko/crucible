#include <crucible/safety/diag/JsonEmitter.h>

#include "../tools/lsp_code_action_plugin/CodeAction.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace diag = crucible::safety::diag;
namespace lsp = crucible::tools::lsp_code_action;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL: %s -- %s (%s:%d)\n",                      \
                     #cond, (msg), __FILE__, __LINE__);                        \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

[[nodiscard]] std::string read_tmp_file(std::FILE* f) {
    if (std::fflush(f) != 0) return {};
    if (std::fseek(f, 0, SEEK_END) != 0) return {};
    const long end = std::ftell(f);
    if (end < 0) return {};
    if (std::fseek(f, 0, SEEK_SET) != 0) return {};

    std::string out(static_cast<std::size_t>(end), '\0');
    if (!out.empty()) {
        const std::size_t n = std::fread(
            out.data(), 1, out.size(), f);
        out.resize(n);
    }
    return out;
}

void test_source_position_parser() {
    const auto pos = diag::parse_source_position(
        "/tmp/project/test.cpp:42:7@void ns::fn()");
    EXPECT(pos.file == "/tmp/project/test.cpp",
           "source parser must recover file path");
    EXPECT(pos.line == 42, "source parser must recover line");
    EXPECT(pos.column == 7, "source parser must recover column");
    EXPECT(pos.function == "void ns::fn()",
           "source parser must recover function");

    const auto legacy = diag::parse_source_position("plain_function");
    EXPECT(legacy.file.empty(), "plain function context has no file");
    EXPECT(legacy.function == "plain_function",
           "plain function context must be preserved");

    const auto oversized = diag::parse_source_position(
        "overflow.cpp:999999999999999999999999:7@overflow_fn");
    EXPECT(oversized.file == "overflow.cpp:999999999999999999999999:7",
           "oversized line values must not truncate into valid positions");
    EXPECT(oversized.line == 0,
           "oversized line values must leave source line unset");
}

void test_json_escape() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr, "tmpfile must be available");
    if (f == nullptr) return;

    const bool ok = diag::emit_json_record(
        f,
        diag::JsonDiagnosticRecord{
            .category = diag::Category::EffectRowMismatch,
            .source = diag::SourcePosition{
                .file = "escape.cpp",
                .line = 1,
                .column = 2,
                .function = "escape_fn",
            },
            .error_code = "EffectRowMismatch",
            .goal = "goal",
            .have = "have",
            .gap = "quote \" slash \\ newline\n",
            .suggestion = "suggestion",
            .related_snippet = "x < y",
        });
    EXPECT(ok, "JSON record emission must succeed");
    const std::string json = read_tmp_file(f);
    EXPECT(json.find("\\\"") != std::string::npos,
           "JSON string values must escape quotes");
    EXPECT(json.find("\\\\") != std::string::npos,
           "JSON string values must escape backslashes");
    EXPECT(json.find("\\n") != std::string::npos,
           "JSON string values must escape newlines");
    EXPECT(json.find("\"related_snippets\":[\"x < y\"]")
           != std::string::npos,
           "related snippet array must be emitted");
    std::fclose(f);
}

void test_violation_json_and_lsp_action() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr, "tmpfile must be available");
    if (f == nullptr) return;

    const bool ok = diag::emit_json_violation(
        f,
        diag::Category::EffectRowMismatch,
        "test_diag_json_emitter.cpp:77:9@test_fn",
        "caller row lacks Bg");
    EXPECT(ok, "violation JSON emission must succeed");
    const std::string json = read_tmp_file(f);
    std::fclose(f);

    EXPECT(json.find("\"format_version\":1") != std::string::npos,
           "JSON must carry the diagnostic format version");
    EXPECT(json.find("\"error_code\":\"EffectRowMismatch\"")
           != std::string::npos,
           "JSON must carry the named diagnostic code");
    EXPECT(json.find("\"line\":77") != std::string::npos,
           "JSON must carry source line");
    EXPECT(json.find("\"column\":9") != std::string::npos,
           "JSON must carry source column");
    EXPECT(json.find("\"gap\":\"caller row lacks Bg\"")
           != std::string::npos,
           "JSON must carry gap detail");

    const auto parsed = lsp::parse_diagnostic_json(json);
    EXPECT(parsed.file == "test_diag_json_emitter.cpp",
           "LSP parser must recover diagnostic file");
    EXPECT(parsed.line == 77, "LSP parser must recover source line");
    EXPECT(parsed.column == 9, "LSP parser must recover source column");
    EXPECT(parsed.error_code == "EffectRowMismatch",
           "LSP parser must recover diagnostic code");

    const std::string action = lsp::to_lsp_code_action(json);
    EXPECT(action.find("\"kind\":\"quickfix\"") != std::string::npos,
           "CodeAction must be a quickfix");
    EXPECT(action.find("\"source\":\"crucible\"") != std::string::npos,
           "CodeAction diagnostic source must be crucible");
    EXPECT(action.find("\"code\":\"EffectRowMismatch\"")
           != std::string::npos,
           "CodeAction must carry diagnostic code");
    EXPECT(action.find("\"line\":76") != std::string::npos,
           "LSP range line must be zero-based");
    EXPECT(action.find("\"character\":8") != std::string::npos,
           "LSP range character must be zero-based");
    EXPECT(action.find("caller row lacks Bg") != std::string::npos,
           "CodeAction must carry the original gap message");
}

}  // namespace

int main() {
    test_source_position_parser();
    test_json_escape();
    test_violation_json_and_lsp_action();

    if (g_failures != 0) {
        std::fprintf(stderr, "FAILURES: %d\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
