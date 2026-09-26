#include <foundation/diag/Runtime.h>
#include <foundation/diag/JsonEmitter.h>

#include <atomic>
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

constexpr std::size_t loc_buf_capacity = 2048;
thread_local char tls_loc_buf[loc_buf_capacity];

std::string_view format_loc(std::source_location loc) noexcept {
    const int n = std::snprintf(tls_loc_buf, loc_buf_capacity, "%s:%u:%u@%s", loc.file_name(), loc.line(), loc.column(),
                                loc.function_name());
    if (n < 0) {
        tls_loc_buf[0] = '\0';
        return {};
    }
    const std::size_t len =
        static_cast<std::size_t>(n) >= loc_buf_capacity ? loc_buf_capacity - 1 : static_cast<std::size_t>(n);
    return {tls_loc_buf, len};
}

}  // namespace

void report_violation_at(Category cat, std::string_view detail, std::source_location loc) noexcept {
    report_violation(cat, format_loc(loc), detail);
}

void report_violation_at_and_abort(Category cat, std::string_view detail, std::source_location loc) noexcept {
    report_violation_at(cat, detail, loc);
    std::abort();
}

}  // namespace foundation::diag
