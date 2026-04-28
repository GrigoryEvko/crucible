// safety/diag/Runtime.h implementation (FOUND-E06).
//
// Atomic-pointer sink storage; default fprintf-to-stderr emitter;
// install/read API for production routing.  Cold path; the entire
// TU compiles to <1 KB of code in release.
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
//   * Default-sink fprintf may interleave on stderr with concurrent
//     writers; the foundation does NOT serialize stderr writes
//     (production sinks ARE expected to do their own serialization).

#include <crucible/safety/diag/Runtime.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── default_violation_sink ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Single fprintf to stderr.  Format:
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>\n
//
// Uses `%.*s` so caller-supplied string_views need NOT be NUL-
// terminated.  fprintf is line-buffered on stderr by default; the
// `\n` at the end forces a flush.  No allocation, no stdio buffer
// growth — fits in any reasonable scratch buffer fprintf uses.

void default_violation_sink(Category cat,
                            std::string_view fn,
                            std::string_view detail) noexcept {
    // name_of returns a short stable string from Diagnostic.h's
    // Category catalog; safe to embed unescaped (consteval-derived,
    // never carries newlines or format chars).
    const std::string_view cat_name = name_of(cat);

    // Cast to int for `%.*s` format specifier (printf takes int).
    // string_view::size() is size_t; cap at INT_MAX/4 defensively
    // (any sane caller passes <KB sized strings).
    constexpr int max_field_chars = 4096;
    const int cat_n =
        cat_name.size() > max_field_chars ? max_field_chars
                                          : static_cast<int>(cat_name.size());
    const int fn_n =
        fn.size() > max_field_chars ? max_field_chars
                                    : static_cast<int>(fn.size());
    const int dt_n =
        detail.size() > max_field_chars ? max_field_chars
                                        : static_cast<int>(detail.size());

    std::fprintf(stderr,
                 "crucible-violation: category=%.*s fn=%.*s detail=%.*s\n",
                 cat_n, cat_name.data(),
                 fn_n,  fn.data(),
                 dt_n,  detail.data());
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
// Format the source_location into a "file:line@function" composite
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

// Format "<file>:<line>@<function>" into the TLS buffer, return view.
// Uses snprintf which is bounded; truncation on overflow is silent
// (the parser can detect via missing '@' marker).
std::string_view format_loc(std::source_location loc) noexcept {
    const int n = std::snprintf(
        tls_loc_buf, loc_buf_capacity,
        "%s:%u@%s",
        loc.file_name(),
        loc.line(),               // source_location::line() returns uint_least32_t
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
