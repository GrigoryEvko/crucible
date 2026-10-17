// The output of the diagnostic emitter of foundation/diag/JsonEmitter.h and
// of the default violation sink of foundation/diag/Runtime.h.
//
// test/harness/check_output.py runs this program.  It compares standard
// output and standard error with the exact bytes.  Each record and each line
// ends with one newline, and the emitter escapes each newline inside a
// record.  Each line of the output is then one record or one line of the
// emitter.  The program writes:
//   - the line of a violation, and a line whose field is cut at 4096
//     characters
//   - a record with each escape, a record whose prose comes from the
//     catalog, and a record with the prose of the catalog in place of its
//     empty fields
//   - a record longer than the buffer of the sink, with escapes across each
//     boundary of the buffer
//   - a record of exactly json_record_max_bytes, and the refusal of a record
//     one byte longer
//   - the line of a violation through the default sink, on standard error
//   - the same record through the format form, and the refusal of a text
//     that is too small.

#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/Runtime.h>

// Long literals for the fields: each macro doubles the one before it.
#define CRUCIBLE_PROBE_X1 "x"
#define CRUCIBLE_PROBE_X2 CRUCIBLE_PROBE_X1 CRUCIBLE_PROBE_X1
#define CRUCIBLE_PROBE_X4 CRUCIBLE_PROBE_X2 CRUCIBLE_PROBE_X2
#define CRUCIBLE_PROBE_X8 CRUCIBLE_PROBE_X4 CRUCIBLE_PROBE_X4
#define CRUCIBLE_PROBE_X16 CRUCIBLE_PROBE_X8 CRUCIBLE_PROBE_X8
#define CRUCIBLE_PROBE_X32 CRUCIBLE_PROBE_X16 CRUCIBLE_PROBE_X16
#define CRUCIBLE_PROBE_X64 CRUCIBLE_PROBE_X32 CRUCIBLE_PROBE_X32
#define CRUCIBLE_PROBE_X128 CRUCIBLE_PROBE_X64 CRUCIBLE_PROBE_X64
#define CRUCIBLE_PROBE_X256 CRUCIBLE_PROBE_X128 CRUCIBLE_PROBE_X128
#define CRUCIBLE_PROBE_X512 CRUCIBLE_PROBE_X256 CRUCIBLE_PROBE_X256
#define CRUCIBLE_PROBE_X1024 CRUCIBLE_PROBE_X512 CRUCIBLE_PROBE_X512
#define CRUCIBLE_PROBE_X2048 CRUCIBLE_PROBE_X1024 CRUCIBLE_PROBE_X1024
#define CRUCIBLE_PROBE_X4096 CRUCIBLE_PROBE_X2048 CRUCIBLE_PROBE_X2048
#define CRUCIBLE_PROBE_X8192 CRUCIBLE_PROBE_X4096 CRUCIBLE_PROBE_X4096

#define CRUCIBLE_PROBE_G1 "g"
#define CRUCIBLE_PROBE_G2 CRUCIBLE_PROBE_G1 CRUCIBLE_PROBE_G1
#define CRUCIBLE_PROBE_G4 CRUCIBLE_PROBE_G2 CRUCIBLE_PROBE_G2
#define CRUCIBLE_PROBE_G8 CRUCIBLE_PROBE_G4 CRUCIBLE_PROBE_G4
#define CRUCIBLE_PROBE_G16 CRUCIBLE_PROBE_G8 CRUCIBLE_PROBE_G8
#define CRUCIBLE_PROBE_G32 CRUCIBLE_PROBE_G16 CRUCIBLE_PROBE_G16
#define CRUCIBLE_PROBE_G64 CRUCIBLE_PROBE_G32 CRUCIBLE_PROBE_G32
#define CRUCIBLE_PROBE_G128 CRUCIBLE_PROBE_G64 CRUCIBLE_PROBE_G64
#define CRUCIBLE_PROBE_G256 CRUCIBLE_PROBE_G128 CRUCIBLE_PROBE_G128
#define CRUCIBLE_PROBE_G512 CRUCIBLE_PROBE_G256 CRUCIBLE_PROBE_G256
#define CRUCIBLE_PROBE_G1024 CRUCIBLE_PROBE_G512 CRUCIBLE_PROBE_G512
#define CRUCIBLE_PROBE_G2048 CRUCIBLE_PROBE_G1024 CRUCIBLE_PROBE_G1024
#define CRUCIBLE_PROBE_G4096 CRUCIBLE_PROBE_G2048 CRUCIBLE_PROBE_G2048
#define CRUCIBLE_PROBE_G8192 CRUCIBLE_PROBE_G4096 CRUCIBLE_PROBE_G4096
#define CRUCIBLE_PROBE_G16384 CRUCIBLE_PROBE_G8192 CRUCIBLE_PROBE_G8192
// 32596 characters: json_record_max_bytes less the 172 bytes of the record
// of exact_record() with an empty gap.
#define CRUCIBLE_PROBE_GAP_EXACT                                                                              \
    CRUCIBLE_PROBE_G16384 CRUCIBLE_PROBE_G8192 CRUCIBLE_PROBE_G4096 CRUCIBLE_PROBE_G2048 CRUCIBLE_PROBE_G1024 \
        CRUCIBLE_PROBE_G512 CRUCIBLE_PROBE_G256 CRUCIBLE_PROBE_G64 CRUCIBLE_PROBE_G16 CRUCIBLE_PROBE_G4

#define CRUCIBLE_PROBE_P1 "ab\"\n"
#define CRUCIBLE_PROBE_P2 CRUCIBLE_PROBE_P1 CRUCIBLE_PROBE_P1
#define CRUCIBLE_PROBE_P4 CRUCIBLE_PROBE_P2 CRUCIBLE_PROBE_P2
#define CRUCIBLE_PROBE_P8 CRUCIBLE_PROBE_P4 CRUCIBLE_PROBE_P4
#define CRUCIBLE_PROBE_P16 CRUCIBLE_PROBE_P8 CRUCIBLE_PROBE_P8
#define CRUCIBLE_PROBE_P32 CRUCIBLE_PROBE_P16 CRUCIBLE_PROBE_P16
#define CRUCIBLE_PROBE_P64 CRUCIBLE_PROBE_P32 CRUCIBLE_PROBE_P32
#define CRUCIBLE_PROBE_P128 CRUCIBLE_PROBE_P64 CRUCIBLE_PROBE_P64
#define CRUCIBLE_PROBE_P256 CRUCIBLE_PROBE_P128 CRUCIBLE_PROBE_P128
#define CRUCIBLE_PROBE_P512 CRUCIBLE_PROBE_P256 CRUCIBLE_PROBE_P256
#define CRUCIBLE_PROBE_P1024 CRUCIBLE_PROBE_P512 CRUCIBLE_PROBE_P512

namespace {

namespace diag = ::foundation::diag;
namespace core = ::foundation::core;

// A record with each escape that the emitter knows.  The control character
// 0x01 comes out as a backslash-u escape with uppercase hex.
[[nodiscard]] diag::JsonDiagnosticRecord escape_record() noexcept {
    return diag::JsonDiagnosticRecord{
        .category = diag::Category::EffectRowMismatch,
        .source = diag::SourcePosition{.file = "escape.cpp", .line = 1, .column = 2, .function = "escape_fn"},
        .error_code = "EffectRowMismatch",
        .goal = "goal",
        .have = "have",
        .gap = "quote \" slash \\ newline\n tab \t ctrl \x01",
        .suggestion = "suggestion",
        .related_snippet = "x < y",
    };
}

// A record of exactly json_record_max_bytes.
[[nodiscard]] diag::JsonDiagnosticRecord exact_record() noexcept {
    return diag::JsonDiagnosticRecord{
        .category = diag::Category::BudgetExceeded,
        .error_code = "E",
        .goal = "G",
        .have = "H",
        .gap = CRUCIBLE_PROBE_GAP_EXACT,
        .suggestion = "S",
    };
}

// The record of exact_record() and one more byte.
[[nodiscard]] diag::JsonDiagnosticRecord over_record() noexcept {
    return diag::JsonDiagnosticRecord{
        .category = diag::Category::BudgetExceeded,
        .error_code = "E",
        .goal = "G",
        .have = "H",
        .gap = CRUCIBLE_PROBE_GAP_EXACT "g",
        .suggestion = "S",
    };
}

// A record whose gap holds 1024 copies of a quote and a newline.  The escaped
// record is then much longer than the buffer of 512 bytes of the sink.
[[nodiscard]] diag::JsonDiagnosticRecord long_record() noexcept {
    return diag::JsonDiagnosticRecord{
        .category = diag::Category::EffectRowMismatch,
        .source = diag::SourcePosition{.file = "long.cpp", .line = 3, .column = 4, .function = "long_fn"},
        .error_code = "E",
        .goal = "G",
        .have = "H",
        .gap = CRUCIBLE_PROBE_P1024,
        .suggestion = "S",
    };
}

void write_formatted(core::Option<core::TextView> const& formatted) noexcept {
    formatted.match([](core::TextView text) { core::report(core::Sink::Out, "{}", text); },
                    [] { core::report(core::Sink::Out, "format refused\n"); });
}

}  // namespace

int main() {
    diag::emit_legacy_text_violation(core::Sink::Out, diag::Category::DetSafeLeak, "fn_name", "detail text");
    diag::emit_legacy_text_violation(core::Sink::Out, diag::Category::DetSafeLeak, CRUCIBLE_PROBE_X8192, "d");

    bool const escapes = diag::emit_json_record(core::Sink::Out, escape_record());
    bool const violation = diag::emit_json_violation(core::Sink::Out, diag::Category::EffectRowMismatch,
                                                     "test_diag.cpp:77:9@test_fn", "caller row lacks Bg");
    bool const fallback =
        diag::emit_json_record(core::Sink::Out, diag::JsonDiagnosticRecord{.category = diag::Category::DetSafeLeak});
    bool const long_one = diag::emit_json_record(core::Sink::Out, long_record());
    bool const exact = diag::emit_json_record(core::Sink::Out, exact_record());
    bool const over = diag::emit_json_record(core::Sink::Out, over_record());
    core::report(core::Sink::Out, "results: {} {} {} {} {} {}\n", escapes, violation, fallback, long_one, exact, over);

    diag::report_violation(diag::Category::DetSafeLeak, "default_fn", "default detail");

    core::FixedText<1024> record_text;
    write_formatted(diag::format_json_record(record_text, escape_record()));
    core::FixedText<16> small_text;
    write_formatted(diag::format_json_record(small_text, escape_record()));
    return 0;
}
