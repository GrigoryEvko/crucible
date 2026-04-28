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

}  // namespace crucible::safety::diag
