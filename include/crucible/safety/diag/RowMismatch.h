#pragma once

// ── crucible::safety::diag — structured row-mismatch builder ────────
//
// Federation foundation for human-readable diagnostic output at
// concept-rejection sites.  Ships three primitives (E02 + E03 + E19
// of 28_04_2026_effects.md §7.4):
//
//   * type_name<T>             — diagnostic-facing type display name
//   * function_display_name<F> — reflection-based function signature
//   * build_row_mismatch_message<...>()  — 8-line structured-error
//                                          builder (consteval array)
//
// And the user-facing macro:
//
//   * CRUCIBLE_ROW_MISMATCH_ASSERT(cond, tag, fn, caller, callee,
//                                  offending)
//
// ── The promise (28_04 §7.4) ────────────────────────────────────────
//
// Today, a Subrow<CalleeRow, CallerRow> failure produces a 200-line
// template instantiation backtrace ending with "constraints not
// satisfied".  Engineers cannot diagnose this without inspecting
// every instantiation frame.
//
// After this header lands, the SAME failure produces:
//
//   error: static assertion failed:
//     [HotPathViolation]
//       at crucible::vessel::dispatch_op
//       caller row contains: HotPath<Cold> | DetSafe<WallClockRead>
//       callee requires:     Subrow<_, Row<HotPath<Hot>, DetSafe<Pure>>>
//       offending atoms:     HotPath<Cold> (must be Hot)
//                            DetSafe<WallClockRead> (must be Pure)
//       remediation:         either restrict caller's row or relax
//                            callee's requirement
//
// Engineers read the diagnostic; identify the offending axis; apply
// the per-Tag remediation; ship the fix.  No template-instantiation
// archaeology required.
//
// ── Format spec ─────────────────────────────────────────────────────
//
// Eight logical lines, separated by '\n'.  Each line carries a
// fixed-position semantic role; tooling (clangd, IDE plugins) parses
// the format for hover-tooltip integration.
//
//   Line 1: [<Category::name>]
//   Line 2:   at <function_display_name>
//   Line 3:   caller row contains: <type_name<CallerRow>>
//   Line 4:   callee requires:     Subrow<_, <type_name<CalleeRow>>>
//   Line 5:   offending atoms:     <type_name<OffendingDiff>>
//   Line 6:   remediation: <Tag::remediation>
//   Line 7:   docs: see safety/Diagnostic.h, 28_04_2026_effects.md §7
//   Line 8: <empty trailing newline>
//
// Format version: CRUCIBLE_DIAG_FORMAT_VERSION = 1.  When the
// format evolves (e.g., adding source-location, line/column), bump
// the version.  IDE consumers gate on the version to avoid breaking.
//
// ── Why a consteval builder, not runtime formatting ─────────────────
//
// P2741R3 (C++26) admits any constant expression with `.data()` +
// `.size()` as the second argument to `static_assert`.  We exploit
// this to embed the structured message INTO the compile-time
// diagnostic — the user sees the formatted block as the static_assert
// failure message, no runtime formatting required.
//
// The builder returns `std::array<char, N>` where N is computed at
// consteval from the input types' string lengths.  The array is then
// referenced by the variable template `row_mismatch_message_v` and
// embedded into the static_assert via the macro.
//
// Zero runtime cost: the builder runs at compile time; the array
// content is baked into the diagnostic; production code carries no
// trace of this machinery.
//
// ── Generic over row arithmetic ─────────────────────────────────────
//
// The builder takes the offending difference as an opaque TYPE
// parameter (`OffendingDiff`); it does NOT compute the difference
// itself.  This keeps the satellite agnostic to the row algebra
// (effects/EffectRow.h's row_difference_t) and admits any concept-
// rejection site that can compute its own offending information.
//
// Caller responsibility: pass `row_difference_t<CalleeRow, CallerRow>`
// (or equivalent) as `OffendingDiff`.  The builder stringifies it
// via `type_name<OffendingDiff>`.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7.4         — design + format spec
//   misc/diagnostic_format.md          — TODO: ship the spec doc
//                                        with FOUND-E19 follow-up
//   safety/Diagnostic.h                — Tag types + Category enum
//   safety/diag/StableName.h           — stable_name_of<T> + ID hash
//   P2741R3                            — C++26 user-generated static_
//                                        assert messages
//
// FOUND-E02 — function_display_name<auto FnPtr>
// FOUND-E03 — type_name<T>
// FOUND-E19 — structured row-mismatch error format spec

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>           // Tag types + is_diagnostic_class_v
#include <crucible/safety/diag/StableName.h>      // stable_name_of<T>

#include <array>
#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Format version ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Tooling (clangd, IDE plugins) consuming the structured-error format
// gates on this version.  Bump on any breaking format change (line
// reordering, semantic role change).  Additive line additions at the
// END of the format are non-breaking and do NOT bump the version.

inline constexpr std::size_t CRUCIBLE_DIAG_FORMAT_VERSION = 1;

// ═════════════════════════════════════════════════════════════════════
// ── type_name<T> — diagnostic-facing type display (FOUND-E03) ──────
// ═════════════════════════════════════════════════════════════════════
//
// Alias of `stable_name_of<T>` for the diagnostic-facing API surface.
// Same V1 stability contract: bit-stable WITHIN one build; cross-
// compiler federation deferred (see StableName.h's federation
// contract for the full discipline).
//
// Why a separate name: callers reading diagnostic code want
// `type_name<T>` (reads as "the display name of type T") rather than
// `stable_name_of<T>` (reads as "the stable identifier name of T").
// Both names refer to the same underlying value; pick the one that
// reads naturally at the call site.
//
// Cross-reference: `stable_name_of<T>` is the canonical name in the
// federation context (cache key derivation); `type_name<T>` is the
// canonical name in the diagnostic context (error message rendering).
// Aliasing avoids the ergonomic friction without inventing a second
// implementation.

template <typename T>
inline constexpr std::string_view type_name = stable_name_of<T>;

// ═════════════════════════════════════════════════════════════════════
// ── function_display_name<auto FnPtr> (FOUND-E02) ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Reflection-based function signature display.  Renders the
// function's fully-qualified name + parameter list + return type as
// the compiler sees it (modulo TU-context-fragility per StableName.h
// federation contract).
//
// Implementation: `display_string_of(^^FnPtr)` reflects the function
// declaration (not just its type), so the result includes the
// function's qualified name AND its signature.  Equivalent to
// `display_string_of(^^decltype(FnPtr))` for stateless callables.
//
// Usage:
//
//   void my_module::dispatch_op(int);
//   constexpr auto name = function_display_name<&my_module::dispatch_op>;
//   //  → "my_module::dispatch_op(int)"  (or similar; modulo TU-frag)
//
// Used as the "at <function-name>" line of the structured row-
// mismatch format.

// V1 implementation note: GCC 16's P2996R13 implementation does NOT
// accept `^^FnPtr` directly when FnPtr is an `auto`-NTTP value (the
// splice operator requires an entity, not a value).  We work around
// by reflecting on the function's TYPE via `decltype(FnPtr)` +
// `remove_pointer_t`.  Result: the display string is the function-
// pointer signature (e.g. "void(int)") rather than the qualified
// function name (e.g. "my_module::dispatch_op").  This is sufficient
// for diagnostic disambiguation; richer per-call-site names await a
// future P2996 revision or an explicit FnType+FnPtr two-parameter
// API (deferred to FOUND-E18 / IDE integration).
//
// Same workaround as `stable_function_id<auto FnPtr>` in StableName.h.

template <auto FnPtr>
inline constexpr std::string_view function_display_name =
    std::meta::display_string_of(^^std::remove_pointer_t<decltype(FnPtr)>);

// ═════════════════════════════════════════════════════════════════════
// ── Internal: char-buffer + append helper for consteval composition
// ═════════════════════════════════════════════════════════════════════
//
// std::array<char, N> at consteval, with append-by-string_view.  Used
// to compose the multi-line diagnostic without dynamic allocation.

namespace detail {

template <std::size_t N>
struct char_buffer {
    std::array<char, N> data{};
    std::size_t         length = 0;

    constexpr void append(std::string_view s) noexcept {
        for (char c : s) {
            data[length++] = c;
        }
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view{data.data(), length};
    }
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── build_row_mismatch_message<...> — the structured builder ───────
// ═════════════════════════════════════════════════════════════════════
//
// Composes the 8-line structured-error block as a `std::array<char,
// N>` at consteval.  N is computed from the input types' string
// lengths; no padding, no overestimation.
//
// Template parameters:
//   Tag           — diagnostic tag type (must satisfy is_diagnostic_
//                   class_v); supplies `name` and `remediation`
//   FnPtr         — function pointer to the rejection site
//   CallerRow     — caller's effect row / wrapper stack type
//   CalleeRow     — callee's required effect row / wrapper stack type
//   OffendingDiff — type representing the offending atoms (typically
//                   `row_difference_t<CalleeRow, CallerRow>` from
//                   effects/EffectRow.h, but generic — caller decides)
//
// Returns: `std::array<char, N>` containing the formatted message
// terminated by '\n'.  Use `.data()` + `.size()` for static_assert
// consumption (or call `.view()` on the wrapper if needed).
//
// Note: the returned array's TYPE depends on N which depends on the
// input types' name lengths.  Each (Tag, FnPtr, CallerRow, CalleeRow,
// OffendingDiff) instantiation has its own array type.  This is
// required for the value to be constant-expression-usable in
// static_assert messages.

namespace detail {

// Compute total formatted length at consteval.  Sum of:
//   * Format literals (Line prefixes + separators)
//   * Per-input string lengths
//
// The literal count is precomputed below for stability across format
// version 1.

inline constexpr std::string_view L1_PREFIX = "[";
inline constexpr std::string_view L1_SUFFIX = "]\n";
inline constexpr std::string_view L2_PREFIX = "  at ";
inline constexpr std::string_view L2_SUFFIX = "\n";
inline constexpr std::string_view L3_PREFIX = "  caller row contains: ";
inline constexpr std::string_view L3_SUFFIX = "\n";
inline constexpr std::string_view L4_PREFIX = "  callee requires:     Subrow<_, ";
inline constexpr std::string_view L4_SUFFIX = ">\n";
inline constexpr std::string_view L5_PREFIX = "  offending atoms:     ";
inline constexpr std::string_view L5_SUFFIX = "\n";
inline constexpr std::string_view L6_PREFIX = "  remediation: ";
inline constexpr std::string_view L6_SUFFIX = "\n";
inline constexpr std::string_view L7_LINE   =
    "  docs: see safety/Diagnostic.h, 28_04_2026_effects.md §7\n";

[[nodiscard]] consteval std::size_t format_total_length(
    std::string_view category,
    std::string_view fn_name,
    std::string_view caller_name,
    std::string_view callee_name,
    std::string_view offending,
    std::string_view remediation) noexcept
{
    return L1_PREFIX.size() + category.size()  + L1_SUFFIX.size()
         + L2_PREFIX.size() + fn_name.size()   + L2_SUFFIX.size()
         + L3_PREFIX.size() + caller_name.size() + L3_SUFFIX.size()
         + L4_PREFIX.size() + callee_name.size() + L4_SUFFIX.size()
         + L5_PREFIX.size() + offending.size() + L5_SUFFIX.size()
         + L6_PREFIX.size() + remediation.size() + L6_SUFFIX.size()
         + L7_LINE.size();
}

}  // namespace detail

template <typename Tag, auto FnPtr, typename CallerRow,
          typename CalleeRow, typename OffendingDiff>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto build_row_mismatch_message() noexcept {
    constexpr auto category    = Tag::name;
    constexpr auto remediation = Tag::remediation;
    constexpr auto fn_name     = function_display_name<FnPtr>;
    constexpr auto caller_str  = type_name<CallerRow>;
    constexpr auto callee_str  = type_name<CalleeRow>;
    constexpr auto offending   = type_name<OffendingDiff>;

    constexpr std::size_t N = detail::format_total_length(
        category, fn_name, caller_str, callee_str, offending, remediation);

    detail::char_buffer<N> buf{};
    buf.append(detail::L1_PREFIX); buf.append(category);    buf.append(detail::L1_SUFFIX);
    buf.append(detail::L2_PREFIX); buf.append(fn_name);     buf.append(detail::L2_SUFFIX);
    buf.append(detail::L3_PREFIX); buf.append(caller_str);  buf.append(detail::L3_SUFFIX);
    buf.append(detail::L4_PREFIX); buf.append(callee_str);  buf.append(detail::L4_SUFFIX);
    buf.append(detail::L5_PREFIX); buf.append(offending);   buf.append(detail::L5_SUFFIX);
    buf.append(detail::L6_PREFIX); buf.append(remediation); buf.append(detail::L6_SUFFIX);
    buf.append(detail::L7_LINE);
    return buf;
}

// ═════════════════════════════════════════════════════════════════════
// ── row_mismatch_message_v — variable template caching the result ──
// ═════════════════════════════════════════════════════════════════════
//
// Materializes the builder's result as an inline-constexpr buffer
// per (Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff) tuple.  The
// CRUCIBLE_ROW_MISMATCH_ASSERT macro uses this so the static_assert's
// message expression is a simple variable reference, not a function
// invocation (which would require P2741R3 + complex grammar parsing
// in the static_assert evaluator).

template <typename Tag, auto FnPtr, typename CallerRow,
          typename CalleeRow, typename OffendingDiff>
    requires is_diagnostic_class_v<Tag>
inline constexpr auto row_mismatch_message_v =
    build_row_mismatch_message<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff>();

}  // namespace crucible::safety::diag

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_ROW_MISMATCH_ASSERT macro ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Convenience wrapper: takes the rejection condition + the five type-
// arguments to the message builder, emits a static_assert with the
// formatted block as the message.
//
// Usage:
//
//   CRUCIBLE_ROW_MISMATCH_ASSERT(
//       (Subrow<CalleeRow, CallerRow>),
//       HotPathViolation,
//       &my_module::dispatch_op,
//       CallerRow,
//       CalleeRow,
//       row_difference_t<CalleeRow, CallerRow>);
//
// IMPORTANT: parenthesise the condition if it contains a comma
// (template-arg list).  Same discipline as CRUCIBLE_DIAG_ASSERT.
//
// Produces (on failure) — abbreviated:
//
//   error: static assertion failed:
//     [HotPathViolation]
//       at crucible::my_module::dispatch_op(int)
//       caller row contains: <stringified caller row>
//       callee requires:     Subrow<_, <stringified callee row>>
//       offending atoms:     <stringified offending diff>
//       remediation: <Tag::remediation>
//       docs: see safety/Diagnostic.h, 28_04_2026_effects.md §7

#define CRUCIBLE_ROW_MISMATCH_ASSERT(cond, tag, fn, caller, callee, offending) \
    static_assert((cond),                                                       \
        ::crucible::safety::diag::row_mismatch_message_v<                       \
            ::crucible::safety::diag::tag, fn, caller, callee, offending>      \
            .view())

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════

namespace detail::row_mismatch_self_test {

// Sample function pointer for tests.
inline void sample_fn(int, float) noexcept {}

// ─── type_name<T> aliases stable_name_of<T> ────────────────────────

static_assert(type_name<int>   == stable_name_of<int>);
static_assert(type_name<float> == stable_name_of<float>);
static_assert(!type_name<int>.empty());
static_assert(type_name<int>.ends_with("int"));

// ─── function_display_name<F> non-empty ────────────────────────────

static_assert(!function_display_name<&sample_fn>.empty());

// ─── format_total_length sums correctly ────────────────────────────

constexpr auto fmt_len = format_total_length(
    std::string_view{"Cat"},
    std::string_view{"fn(...)"},
    std::string_view{"Caller"},
    std::string_view{"Callee"},
    std::string_view{"Offend"},
    std::string_view{"Fix"});

constexpr std::size_t expected_len =
      L1_PREFIX.size() + 3 + L1_SUFFIX.size()
    + L2_PREFIX.size() + 7 + L2_SUFFIX.size()
    + L3_PREFIX.size() + 6 + L3_SUFFIX.size()
    + L4_PREFIX.size() + 6 + L4_SUFFIX.size()
    + L5_PREFIX.size() + 6 + L5_SUFFIX.size()
    + L6_PREFIX.size() + 3 + L6_SUFFIX.size()
    + L7_LINE.size();

static_assert(fmt_len == expected_len,
    "format_total_length must equal the sum of literal sizes plus "
    "input string sizes; drift indicates a literal-table edit "
    "without a corresponding format_total_length update.");

// ─── build_row_mismatch_message produces non-empty output ──────────
//
// Compile-time invariants we CAN verify at static_assert:
//   * length > 0 (buffer non-empty)
//   * length matches sum of literal sizes + per-input string sizes
//
// Compile-time invariants we CANNOT verify (libstdc++ 16 limitation):
//   string_view::starts_with / contains / ends_with go through
//   string_view::find which uses pointer arithmetic that isn't
//   constexpr-safe over std::array<char, N>::data() in libstdc++ 16.
//   Verified at RUNTIME in the sentinel TU (test_row_mismatch_compile)
//   instead.

constexpr auto sample_msg =
    build_row_mismatch_message<
        EffectRowMismatch,
        &sample_fn,
        int,
        float,
        double>();

static_assert(sample_msg.length > 0,
    "build_row_mismatch_message returned an empty buffer; format-"
    "literal table or input strings collapsed somehow.");

// Length matches sum-of-parts.
static_assert(sample_msg.length == format_total_length(
    EffectRowMismatch::name,
    function_display_name<&sample_fn>,
    type_name<int>,
    type_name<float>,
    type_name<double>,
    EffectRowMismatch::remediation),
    "build_row_mismatch_message buffer length diverged from "
    "format_total_length predicted size — fold-loop drift.");

// First character is '[' (Line 1 prefix).
static_assert(sample_msg.data[std::size_t{0}] == '[');

// Last character is '\n' (Line 7 suffix).
static_assert(sample_msg.data[sample_msg.length - 1] == '\n');

// ─── row_mismatch_message_v variable template caches correctly ─────

constexpr auto& cached_msg = row_mismatch_message_v<
    HotPathViolation, &sample_fn, int, float, double>;

constexpr auto& cached_msg2 = row_mismatch_message_v<
    DetSafeLeak, &sample_fn, int, float, double>;

// Caching: same template instantiation → same address (linker
// collapses inline constexpr to one definition).
constexpr auto& cached_msg_again = row_mismatch_message_v<
    HotPathViolation, &sample_fn, int, float, double>;
static_assert(&cached_msg == &cached_msg_again);

// Different categories → different lengths (because remediation
// strings differ in length per Tag).
static_assert(cached_msg.length != cached_msg2.length
              || cached_msg.data[std::size_t{1}] != cached_msg2.data[std::size_t{1}]);

// ─── Format version is 1 ───────────────────────────────────────────

static_assert(CRUCIBLE_DIAG_FORMAT_VERSION == 1);

}  // namespace detail::row_mismatch_self_test

// ═════════════════════════════════════════════════════════════════════
// ── runtime_smoke_test — non-constant-args execution probe ─────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::row_mismatch_smoke {
inline void smoke_fn(int) noexcept {}
}  // namespace detail::row_mismatch_smoke

inline void runtime_smoke_test_row_mismatch() noexcept {
    // Materialize a row-mismatch message at runtime (well, the builder
    // is consteval — we just consume it from a runtime context).
    constexpr auto msg = row_mismatch_message_v<
        EffectRowMismatch,
        &detail::row_mismatch_smoke::smoke_fn,
        int, float, double>;

    // Volatile sink — force the optimizer to keep the buffer alive
    // and the size calculation runtime-evaluable.
    volatile std::size_t sink = msg.length;
    sink ^= static_cast<std::size_t>(static_cast<unsigned char>(msg.data[std::size_t{0}]));
    if (msg.length > 1) {
        sink ^= static_cast<std::size_t>(
            static_cast<unsigned char>(msg.data[msg.length - 1]));
    }
    (void)sink;

    // type_name<T> at runtime.
    volatile std::size_t name_sink = type_name<int>.size();
    name_sink ^= type_name<float>.size();
    (void)name_sink;

    // function_display_name at runtime.
    volatile std::size_t fn_sink =
        function_display_name<&detail::row_mismatch_smoke::smoke_fn>.size();
    (void)fn_sink;
}

}  // namespace crucible::safety::diag
