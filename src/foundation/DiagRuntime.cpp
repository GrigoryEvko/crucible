#include <foundation/diag/Runtime.h>
#include <foundation/diag/JsonEmitter.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace foundation::diag {

namespace {

std::atomic<int> g_default_sink_format{0};

[[nodiscard]] bool default_sink_wants_json() noexcept {
    constexpr int unknown = 0;
    constexpr int text = 1;
    constexpr int json = 2;

    const int cached = g_default_sink_format.load(std::memory_order_acquire);
    if (cached != unknown) return cached == json;

    const char* format = std::getenv("CRUCIBLE_DIAG_FORMAT");
    const int selected = format != nullptr && std::strcmp(format, "json") == 0 ? json : text;
    int expected = unknown;
    if (g_default_sink_format.compare_exchange_strong(expected, selected, std::memory_order_acq_rel,
                                                      std::memory_order_acquire)) {
        return selected == json;
    }
    return expected == json;
}

}  // namespace

void default_violation_sink(Category cat, std::string_view fn, std::string_view detail) noexcept {
    if (default_sink_wants_json()) {
        (void)emit_json_violation(stderr, cat, fn, detail);
        return;
    }
    (void)emit_legacy_text_violation(stderr, cat, fn, detail);
}

// One sink for the process.  A program installs its sink one time, and a
// violation in any shared library that links this file reaches it.  The
// object is inline, so that it has the vague linkage that the marker needs.
// No header declares it, so only this file names it.
CRUCIBLE_PROCESS_WIDE inline constinit std::atomic<violation_sink_t> violation_sink_slot{&default_violation_sink};

violation_sink_t set_violation_sink(violation_sink_t sink) noexcept {
    return violation_sink_slot.exchange(sink, std::memory_order_acq_rel);
}

violation_sink_t current_violation_sink() noexcept { return violation_sink_slot.load(std::memory_order_acquire); }

void report_violation(Category cat, std::string_view fn, std::string_view detail) noexcept {
    const auto sink = violation_sink_slot.load(std::memory_order_acquire);
    if (sink) [[likely]] {
        sink(cat, fn, detail);
    }
}

void report_violation_and_abort(Category cat, std::string_view fn, std::string_view detail) noexcept {
    report_violation(cat, fn, detail);
    std::abort();
}

namespace {

// The most characters of the field fn of report_violation_at.  A longer text
// loses its end.
constexpr std::size_t location_capacity = 2048;

// The text "<file>:<line>:<column>@<function>" of one violation.  The object
// is on the stack of report_violation_at, so it uses no thread-local storage
// and no heap.  The sink runs inside that call, so the view of the text is
// valid while the sink reads it.  The text is built with no stdio and no
// locale.
class LocationText {
public:
    explicit LocationText(std::source_location const& loc) noexcept {
        append(loc.file_name());
        append(':');
        append_decimal(loc.line());
        append(':');
        append_decimal(loc.column());
        append('@');
        append(loc.function_name());
    }

    LocationText(LocationText const&) = delete("the view of the text points into this object");
    LocationText& operator=(LocationText const&) = delete("the view of the text points into this object");

    [[nodiscard]] std::string_view view() const noexcept { return {bytes_, length_}; }

private:
    void append(char byte) noexcept {
        if (length_ < location_capacity) {
            bytes_[length_] = byte;
            ++length_;
        }
    }

    // A null name writes nothing.
    void append(char const* text) noexcept {
        if (text == nullptr) return;
        for (; *text != '\0' && length_ < location_capacity; ++text)
            append(*text);
    }

    // A std::uint_least32_t has at most 10 decimal digits on each target of
    // the tree.
    void append_decimal(std::uint_least32_t value) noexcept {
        char digits[10] = {};
        std::size_t count = 0;
        do {
            digits[count] = static_cast<char>('0' + value % 10U);
            ++count;
            value /= 10U;
        } while (value != 0U);
        while (count > 0) {
            --count;
            append(digits[count]);
        }
    }

    char bytes_[location_capacity] = {};
    std::size_t length_ = 0;
};

}  // namespace

void report_violation_at(Category cat, std::string_view detail, std::source_location loc) noexcept {
    LocationText const location{loc};
    report_violation(cat, location.view(), detail);
}

void report_violation_at_and_abort(Category cat, std::string_view detail, std::source_location loc) noexcept {
    report_violation_at(cat, detail, loc);
    std::abort();
}

}  // namespace foundation::diag
