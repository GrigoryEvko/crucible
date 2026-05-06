// safety/diag/Runtime.h implementation (FOUND-E06).
//
// Atomic-pointer sink storage; default stderr emitter; install/read
// API for production routing.  Cold path; kept out of foreground
// dispatch by [[gnu::cold]] declarations in the public header.
//
// Thread-safety model:
//   * Sink pointer lives in a single std::atomic<violation_sink_t>.
//   * `set_violation_sink` exchanges with `memory_order::acq_rel`
//     so a thread reading the active sink concurrently with an
//     install observes EITHER the old OR the new sink atomically
//     (no torn read possible — pointer is naturally lock-free on
//     every supported platform).
//   * `report_violation` reads with `memory_order::acquire` so the
//     callee sees a happens-before of the previous install.
//   * Default-sink stdio writes may interleave on stderr with concurrent
//     writers; the foundation does NOT serialize stderr writes
//     (production sinks ARE expected to do their own serialization).

#include <crucible/safety/diag/Runtime.h>
#include <crucible/safety/diag/JsonEmitter.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crucible::safety::diag {

namespace {

std::atomic<int> g_default_sink_format{0};

[[nodiscard]] bool default_sink_wants_json() noexcept {
    constexpr int unknown = 0;
    constexpr int text = 1;
    constexpr int json = 2;

    const int cached = g_default_sink_format.load(std::memory_order_acquire);
    if (cached != unknown) return cached == json;

    const char* format = std::getenv("CRUCIBLE_DIAG_FORMAT");
    const int selected =
        format != nullptr && std::strcmp(format, "json") == 0 ? json : text;
    int expected = unknown;
    if (g_default_sink_format.compare_exchange_strong(
            expected,
            selected,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return selected == json;
    }
    return expected == json;
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════
// ── default_violation_sink ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Single emission to stderr.  Default format:
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>\n
//
// If CRUCIBLE_DIAG_FORMAT=json is set in the process environment at
// first emission, emit one JSON record instead.  This is still a cold-
// path sink; format selection is intentionally here rather than at
// every call site, and the getenv result is cached once per process.
//
// Uses `%.*s` so caller-supplied string_views need NOT be NUL-
// terminated.  fprintf is line-buffered on stderr by default; the
// `\n` at the end forces a flush.  No allocation, no stdio buffer
// growth — fits in any reasonable scratch buffer fprintf uses.

void default_violation_sink(Category cat,
                            std::string_view fn,
                            std::string_view detail) noexcept {
    if (default_sink_wants_json()) {
        (void)emit_json_violation(stderr, cat, fn, detail);
        return;
    }
    (void)emit_legacy_text_violation(stderr, cat, fn, detail);
}

namespace {

// Sink storage.  Initialized to the default emitter at static-init
// time — this happens before main(), so the first report_violation
// from a static-init constructor sees a valid sink.  No init-order
// fiasco: the std::atomic constexpr-constructible to nullptr would
// also work here, but starting at default_violation_sink lets the
// no-config case Just Work.
std::atomic<violation_sink_t> g_sink{&default_violation_sink};

}  // namespace

// ═════════════════════════════════════════════════════════════════════
// ── set_violation_sink ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

violation_sink_t set_violation_sink(violation_sink_t sink) noexcept {
    // Allow installing nullptr to mean "no sink" (drop violations).
    // Bench harnesses use this to avoid stderr pollution during runs.
    return g_sink.exchange(sink, std::memory_order_acq_rel);
}

// ═════════════════════════════════════════════════════════════════════
// ── current_violation_sink ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

violation_sink_t current_violation_sink() noexcept {
    return g_sink.load(std::memory_order_acquire);
}

// ═════════════════════════════════════════════════════════════════════
// ── report_violation ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void report_violation(Category cat,
                      std::string_view fn,
                      std::string_view detail) noexcept {
    const auto sink = g_sink.load(std::memory_order_acquire);
    if (sink) [[likely]] {
        sink(cat, fn, detail);
    }
    // Installed nullptr sink → silent drop.  Bench harness pattern.
}

// ═════════════════════════════════════════════════════════════════════
// ── report_violation_and_abort ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void report_violation_and_abort(Category cat,
                                std::string_view fn,
                                std::string_view detail) noexcept {
    report_violation(cat, fn, detail);
    std::abort();
}

// ═════════════════════════════════════════════════════════════════════
// ── report_violation_at ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Format the source_location into a "file:line:column@function" composite
// in a thread-local fixed buffer (no heap allocation on the cold
// path, no shared mutex contention between concurrent emitters).
// The buffer is sized for typical paths (<2KB) — pathological-long
// paths get truncated, never overrun.
//
// Why thread_local: each emitter gets its own scratch buffer, so
// concurrent calls don't race on a shared static buffer.  The TLS
// allocation is one-time per thread (cold path; cost amortized
// across the thread's lifetime).

namespace {

constexpr std::size_t loc_buf_capacity = 2048;
thread_local char tls_loc_buf[loc_buf_capacity];

// Format "<file>:<line>:<column>@<function>" into the TLS buffer, return view.
// Uses snprintf which is bounded; truncation on overflow is silent
// (the parser can detect via missing '@' marker).
std::string_view format_loc(std::source_location loc) noexcept {
    const int n = std::snprintf(
        tls_loc_buf, loc_buf_capacity,
        "%s:%u:%u@%s",
        loc.file_name(),
        loc.line(),
        loc.column(),
        loc.function_name());
    if (n < 0) {
        // snprintf encoding error — return an empty view rather than
        // emit garbage; the caller's diagnostic still includes
        // category + detail.
        tls_loc_buf[0] = '\0';
        return {};
    }
    const std::size_t len =
        static_cast<std::size_t>(n) >= loc_buf_capacity
            ? loc_buf_capacity - 1
            : static_cast<std::size_t>(n);
    return {tls_loc_buf, len};
}

}  // namespace

void report_violation_at(Category cat,
                         std::string_view detail,
                         std::source_location loc) noexcept {
    report_violation(cat, format_loc(loc), detail);
}

void report_violation_at_and_abort(Category cat,
                                   std::string_view detail,
                                   std::source_location loc) noexcept {
    report_violation_at(cat, detail, loc);
    std::abort();
}

}  // namespace crucible::safety::diag
