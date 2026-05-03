#pragma once

// ── crucible::safety::reflected ─────────────────────────────────────
//
// Reflection-driven helpers for typed-enum surfaces.  Built on
// P2996R13 (`std::meta::enumerators_of`, `std::meta::identifier_of`)
// and GCC 16's expansion statements (`template for`).  Deliberately
// scoped to ENUMERATOR iteration — the struct-field counterpart lives
// in the top-level `crucible/Reflect.h` (`reflect_hash`,
// `reflect_print`, `reflect_fmix_fold`).
//
// ── What this header ships (#1089 — WRAP-Bits-Integration-3) ───────
//
//   bits_to_string<E>(Bits<E> b, char* out, size_t cap) → size_t
//       Diagnostic snprintf-style formatter for `safety::Bits<E>`.
//       Writes "Flag1|Flag2|..." for every set single-bit flag whose
//       enumerator name is declared on E.  Empty bits writes "" + NUL.
//       Returns chars NEEDED (excluding NUL).  Truncates to (cap-1)
//       chars + a NUL terminator.  Composite-bit enumerators
//       (popcount != 1) and zero-valued enumerators are deliberately
//       SKIPPED — only single-bit named flags participate.  The return
//       value follows POSIX snprintf: a return ≥ cap means truncation
//       happened.  Contract `pre(cap == 0 || out != nullptr)` —
//       passing a nullptr buffer with non-zero capacity is rejected
//       at the boundary.  If E declares two single-bit aliases for
//       the same value (e.g., `A=1; AliasOfA=1`), BOTH names are
//       emitted whenever that bit is set; deduplication is a caller
//       concern.
//
//   enumerator_name<E>(E value) → std::string_view
//       Returns the enumerator's identifier when `value` exactly
//       equals an enumerator value.  Composite values (e.g.
//       `Flag::A | Flag::B` when only A and B are named) return an
//       empty view.  The string_view points to compile-time-immortal
//       data — safe to store, hand off, or return.  When E declares
//       multiple enumerators with the same value (aliasing), the
//       FIRST one in declaration order wins; any later alias is
//       ignored even though it would also `==` match.
//
//   for_each_enumerator<E>(F&& f)
//       Iteration over E's enumerators — runtime-callable and
//       constexpr-callable.  `f` is invoked once per declared
//       enumerator with two RUNTIME arguments:
//           f(E value, std::string_view name);
//       Every iteration unrolls into its own scope; `value` and
//       `name` are initialized from compile-time splice / identifier
//       reflection, but reach the lambda as ordinary parameters so
//       the lambda body may freely mutate captured state.  Why not
//       NTTP-info?  `std::meta::info` is a consteval-only type
//       (P3032); using it as an NTTP forces lambda bodies to be
//       immediate-escalated, which forbids any runtime mutation in
//       the body (e.g., `out[needed] = c`).  Splicing into a
//       constexpr local of type E escapes the consteval-only world.
//
//   for_each_single_bit_enumerator<E>(F&& f)
//       Same as above, but pre-filters at COMPILE time so `f` is
//       only invoked for enumerators whose value has popcount 1
//       (i.e., genuine single-bit flags).  Composite-bit
//       enumerators (e.g., `Flag::AB = 0x03`) and zero-valued
//       enumerators are skipped before the call — composite
//       arithmetic is undecidable for `if constexpr` once values
//       become runtime parameters, so the filter has to live at the
//       iteration site.
//
// ── Why a separate header (not Reflect.h, not Bits.h) ──────────────
//
// Reflect.h reflects struct fields; this header reflects enum
// enumerators.  Different std::meta API surface (nonstatic_data_-
// members_of vs enumerators_of), different splice semantics (member-
// access vs enumerator-value), different iteration shape.  Folding
// both into Reflect.h would mix two unrelated responsibilities.
//
// Bits.h is the wrapper definition; Reflected.h is the diagnostic
// surface for any wrapper that IS a typed enum.  Keeping the
// reflection surface separate lets future enum-typed wrappers reuse
// it without dragging the diagnostic machinery into their own header.
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — every output byte is written before its read; the NUL
//              terminator is always written when cap > 0.
//   TypeSafe — every operation takes/returns a typed surface;
//              integer escape happens only via `b.test(flag)` (Bits's
//              public API) and `popcount` over an underlying-type.
//   NullSafe — `out` may be nullptr only if cap == 0; the function
//              writes nothing in that case (snprintf-style probing
//              for required size).  Contract enforces.
//   MemSafe  — no allocation, no destructor.  Caller-provided buffer.
//   BorrowSafe — value-only inputs.  No aliasing surface.
//   ThreadSafe — pure function of (b, out, cap).  Re-entrant; safe
//              from any thread on disjoint outputs.
//   LeakSafe — no resources.
//   DetSafe  — same E + same b → same output bytes.  Iteration order
//              follows enum declaration order, fixed at compile time.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Per-call body is a hand-unrolled (via `template for`) sequence of
// `b.test(flag)` checks + per-flag char copies.  No heap, no hash
// lookup, no virtual dispatch.  At -O3 the compiler typically
// vectorizes the per-name memcpy loops.  The `popcount(value) == 1`
// gate is `if constexpr` so composite-bit enumerators contribute zero
// runtime cost.
//
// References:
//   CLAUDE.md §II        — eight axioms
//   CLAUDE.md §XVI       — safety wrapper catalog
//   safety/Bits.h        — the typed bit-field surface this prints
//   crucible/Reflect.h   — sister reflection surface for struct fields

#include <crucible/safety/Bits.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::safety::reflected {

// ═════════════════════════════════════════════════════════════════════
// ── for_each_enumerator<E>(F&&) ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Iterate every enumerator of E.  `f` is invoked once per enumerator
// with two RUNTIME arguments:
//
//   for_each_enumerator<MyEnum>([&](MyEnum value, std::string_view name){
//       // do something with value and name
//   });
//
// `value` and `name` are initialized from `[:e:]` and
// `std::meta::identifier_of(e)` inside the per-iteration scope, so
// they reach the lambda as ordinary parameters whose initializers
// happened to be constant expressions.  This is deliberately NOT an
// NTTP-`std::meta::info` interface: `info` is a consteval-only type
// (P3032), and any lambda taking it as an NTTP gets immediate-
// escalated, which forbids the lambda from mutating runtime state.

template <ScopedEnum E, typename F>
constexpr void for_each_enumerator(F&& f) {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^E));
    // GCC 16 expansion statements unroll into successive scopes that
    // each declare the same induction variable; -Wshadow fires per
    // iteration.  Suppress locally for the loop body only.
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E                value = [:e:];
        constexpr std::string_view name  = std::meta::identifier_of(e);
        f(value, name);
    }
    #pragma GCC diagnostic pop
}

// ═════════════════════════════════════════════════════════════════════
// ── for_each_single_bit_enumerator<E>(F&&) ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Compile-time-filtered iteration: skips enumerators whose value
// either is zero or has popcount > 1.  Useful for printing a Bits<E>
// where only single-bit named flags should appear in the output.
// The filter lives in the iteration helper (not the lambda) because
// once `value` reaches the lambda as a runtime parameter, `popcount`
// on it is no longer a constant expression — `if constexpr` can't
// see it.  Putting the filter at the iteration site keeps every
// runtime byte of the lambda body conditional on a compile-time
// pre-check.

template <ScopedEnum E, typename F>
constexpr void for_each_single_bit_enumerator(F&& f) {
    using U = std::underlying_type_t<E>;
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^E));
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E flag = [:e:];
        constexpr U raw  = static_cast<U>(flag);
        if constexpr (std::popcount(
                          static_cast<std::make_unsigned_t<U>>(raw)) == 1) {
            constexpr std::string_view name = std::meta::identifier_of(e);
            f(flag, name);
        }
    }
    #pragma GCC diagnostic pop
}

// ═════════════════════════════════════════════════════════════════════
// ── enumerator_name<E>(E value) ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <ScopedEnum E>
[[nodiscard]] constexpr std::string_view enumerator_name(E value) noexcept {
    // First-match-wins.  Iteration over the unrolled `template for`
    // visits every enumerator at compile time, but the `result.empty()`
    // guard short-circuits subsequent matches at runtime.  This makes
    // the alias case (two enumerators with the same value) deterministic
    // and predictable: the FIRST enumerator in declaration order wins.
    std::string_view result{};
    for_each_enumerator<E>(
        [&](E candidate, std::string_view name) noexcept {
            if (result.empty() && candidate == value) {
                result = name;
            }
        });
    return result;
}

// ═════════════════════════════════════════════════════════════════════
// ── bits_to_string<E>(Bits<E>, char*, size_t) ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Format `b` into `out` as "Flag1|Flag2|Flag3".  snprintf-style
// truncation: returns chars that WOULD be written (not counting the
// NUL terminator).  When the return value is < cap, the buffer holds
// the complete string; when ≥ cap, the buffer was truncated to
// (cap-1) chars + NUL.
//
// Skipping rules:
//   - Enumerators with value 0 are skipped (always-set, meaningless).
//   - Enumerators with popcount > 1 are skipped (composite/aliased;
//     emitting them would over-report under typical "single-bit flag"
//     enums).  Callers wanting composite-display should special-case
//     in their own iteration via for_each_enumerator.
//
// Empty bits (b.none()) produces an empty string.  cap == 0 writes
// nothing but still computes and returns the chars-needed count
// (snprintf probing convention).

template <ScopedEnum E>
[[nodiscard]] constexpr size_t bits_to_string(Bits<E> b, char* out, size_t cap) noexcept
    pre (cap == 0 || out != nullptr)
{
    size_t needed = 0;   // chars that would be written, excluding NUL
    bool   first  = true;

    auto emit_byte = [&](char c) noexcept {
        if (cap > 0 && needed + 1 < cap) out[needed] = c;
        ++needed;
    };
    auto emit_view = [&](std::string_view s) noexcept {
        for (char c : s) emit_byte(c);
    };

    for_each_single_bit_enumerator<E>(
        [&](E flag, std::string_view name) noexcept {
            if (b.test(flag)) {
                if (!first) emit_byte('|');
                emit_view(name);
                first = false;
            }
        });

    // Always NUL-terminate when cap > 0, even on truncation.
    if (cap > 0) {
        size_t nul_pos = needed < cap ? needed : (cap - 1);
        out[nul_pos] = '\0';
    }
    return needed;
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::reflected_self_test {

enum class TestFlags : std::uint8_t {
    Alpha   = 0x01,
    Beta    = 0x02,
    Gamma   = 0x04,
    Delta   = 0x08,
    // Composite enumerator — must be SKIPPED by bits_to_string and
    // NOT match a single-bit query in enumerator_name on a composite
    // value.
    AlphaBeta = 0x03,
    // Zero-valued enumerator — must be SKIPPED.
    None    = 0x00,
};

using TF  = TestFlags;
using BTF = ::crucible::safety::Bits<TF>;

// ── enumerator_name positive ────────────────────────────────────────

inline constexpr auto name_alpha = enumerator_name(TF::Alpha);
static_assert(name_alpha == "Alpha");

inline constexpr auto name_gamma = enumerator_name(TF::Gamma);
static_assert(name_gamma == "Gamma");

// Composite enumerator IS named in this enum, so the lookup is exact:
inline constexpr auto name_alphabeta = enumerator_name(TF::AlphaBeta);
static_assert(name_alphabeta == "AlphaBeta");

// Composite VALUE that is NOT a named enumerator returns "".
[[nodiscard]] consteval bool composite_value_lookup_returns_empty() noexcept {
    constexpr auto raw = static_cast<TF>(
        static_cast<std::uint8_t>(TF::Alpha)
        | static_cast<std::uint8_t>(TF::Gamma));   // 0x05 — NOT named
    return enumerator_name(raw).empty();
}
static_assert(composite_value_lookup_returns_empty());

// Zero-VALUE enumerator IS named — enumerator_name returns its name
// regardless of the popcount-skipping rule that bits_to_string uses.
inline constexpr auto name_none = enumerator_name(TF::None);
static_assert(name_none == "None");

// First-match-wins discipline — TF has AlphaBeta=0x03, but composing
// Alpha|Beta at runtime produces value 0x03 too; AlphaBeta wins because
// it's the first (only) enumerator declared with value 0x03.  Document
// this here so a future enum that adds AliasOfAlphaBeta=0x03 after
// AlphaBeta can predict the answer (still "AlphaBeta", first-match).
static_assert(enumerator_name(static_cast<TF>(0x03)) == "AlphaBeta");

// Truly-unnamed value returns empty view (not garbage, not the
// last-iterated name).
static_assert(enumerator_name(static_cast<TF>(0xFF)).empty());

// ── bits_to_string positive ─────────────────────────────────────────

[[nodiscard]] consteval bool empty_bits_writes_empty_string() noexcept {
    char  buf[16] = {};
    BTF   b{};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 0 && buf[0] == '\0';
}
static_assert(empty_bits_writes_empty_string());

[[nodiscard]] consteval bool single_flag_writes_name() noexcept {
    char  buf[16] = {};
    BTF   b{TF::Alpha};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 5 /* "Alpha" */ && std::string_view{buf} == "Alpha";
}
static_assert(single_flag_writes_name());

[[nodiscard]] consteval bool multi_flag_writes_pipe_separated() noexcept {
    char  buf[64] = {};
    BTF   b{TF::Alpha, TF::Gamma, TF::Delta};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    // Iteration order = enum declaration order:
    //   Alpha|Gamma|Delta — composite/zero skipped.
    return std::string_view{buf} == "Alpha|Gamma|Delta"
        && n == std::string_view{buf}.size();
}
static_assert(multi_flag_writes_pipe_separated());

[[nodiscard]] consteval bool composite_enumerator_is_skipped() noexcept {
    // Set Alpha and Beta — the AlphaBeta composite enumerator MUST
    // NOT appear in the output (popcount(AlphaBeta) = 2).
    char  buf[64] = {};
    BTF   b{TF::Alpha, TF::Beta};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta"
        && n == std::string_view{buf}.size();
}
static_assert(composite_enumerator_is_skipped());

[[nodiscard]] consteval bool zero_enumerator_is_skipped() noexcept {
    // None = 0x00 — popcount == 0, must NOT appear regardless of bits.
    char  buf[64] = {};
    BTF   b{TF::Alpha};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    // No "None" prefix.
    return std::string_view{buf} == "Alpha" && n == 5;
}
static_assert(zero_enumerator_is_skipped());

// ── bits_to_string truncation ───────────────────────────────────────

[[nodiscard]] consteval bool truncation_returns_needed_length() noexcept {
    // "Alpha|Beta" needs 10 chars + NUL.  Give it cap=8.
    char  buf[8] = {};
    BTF   b{TF::Alpha, TF::Beta};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    // chars NEEDED equals 10 (the full "Alpha|Beta"), not 7 (what
    // fit) — snprintf convention so callers can detect truncation.
    if (n != 10) return false;
    // Buffer holds first (cap-1)=7 chars + NUL.
    if (std::string_view{buf} != "Alpha|B") return false;
    return buf[7] == '\0';
}
static_assert(truncation_returns_needed_length());

[[nodiscard]] consteval bool zero_capacity_probes_size() noexcept {
    // cap=0 — snprintf probing convention.  Returns chars-needed,
    // writes nothing (out may even be nullptr in this branch).
    BTF   b{TF::Alpha, TF::Beta, TF::Gamma};
    auto  n = bits_to_string<TF>(b, nullptr, 0);
    return n == std::string_view{"Alpha|Beta|Gamma"}.size();
}
static_assert(zero_capacity_probes_size());

[[nodiscard]] consteval bool unit_capacity_writes_nul_only() noexcept {
    // cap=1 — only room for the NUL terminator.
    char  buf[1] = {'X'};
    BTF   b{TF::Alpha};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    return buf[0] == '\0' && n == 5;
}
static_assert(unit_capacity_writes_nul_only());

// Boundary case: cap == needed + 1 fits the FULL string + NUL exactly.
// One byte less (cap == needed) would already truncate.
[[nodiscard]] consteval bool exact_fit_capacity_no_truncation() noexcept {
    // "Alpha|Beta" needs 10 chars + NUL = 11 bytes.
    char  buf[11] = {};
    BTF   b{TF::Alpha, TF::Beta};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|Beta") return false;
    return buf[10] == '\0';
}
static_assert(exact_fit_capacity_no_truncation());

[[nodiscard]] consteval bool one_byte_short_truncates_one_char() noexcept {
    // "Alpha|Beta" needs 10 + NUL = 11 bytes; cap=10 is one short.
    char  buf[10] = {};
    BTF   b{TF::Alpha, TF::Beta};
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    // cap=10 → write 9 chars + NUL at index 9.
    if (std::string_view{buf} != "Alpha|Bet") return false;
    return buf[9] == '\0';
}
static_assert(one_byte_short_truncates_one_char());

// All-flags set — full coverage of the iteration body across every
// single-bit enumerator.  Composite (AlphaBeta) and zero (None)
// stay out of the output even when their bits are nominally in raw.
[[nodiscard]] consteval bool all_flags_set_emits_every_single_bit() noexcept {
    char  buf[64] = {};
    BTF   b{TF::Alpha, TF::Beta, TF::Gamma, TF::Delta,
            TF::AlphaBeta, TF::None};   // composite + zero are no-ops
    auto  n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta|Gamma|Delta"
        && n == std::string_view{buf}.size();
}
static_assert(all_flags_set_emits_every_single_bit());

// ── Determinism — same input, same output ────────────────────────────

[[nodiscard]] consteval bool deterministic_output() noexcept {
    char buf1[64] = {};
    char buf2[64] = {};
    BTF  b{TF::Beta, TF::Delta};
    auto n1 = bits_to_string<TF>(b, buf1, sizeof(buf1));
    auto n2 = bits_to_string<TF>(b, buf2, sizeof(buf2));
    return n1 == n2 && std::string_view{buf1} == std::string_view{buf2};
}
static_assert(deterministic_output());

// ── for_each_enumerator coverage ────────────────────────────────────

[[nodiscard]] consteval int count_enumerators() noexcept {
    int n = 0;
    for_each_enumerator<TF>(
        [&](TF, std::string_view) noexcept { ++n; });
    return n;
}
// 6 declared enumerators: Alpha Beta Gamma Delta AlphaBeta None.
static_assert(count_enumerators() == 6);

[[nodiscard]] consteval int count_single_bit_enumerators() noexcept {
    int n = 0;
    for_each_single_bit_enumerator<TF>(
        [&](TF, std::string_view) noexcept { ++n; });
    return n;
}
// Single-bit only: Alpha Beta Gamma Delta — composite (AlphaBeta=0x03)
// and zero (None=0x00) skipped at compile time.
static_assert(count_single_bit_enumerators() == 4);

[[nodiscard]] consteval bool single_bit_iteration_yields_declaration_order() noexcept {
    // Build a comma-joined name list to verify both ORDER and CONTENTS.
    char  buf[64] = {};
    size_t pos    = 0;
    bool   first  = true;
    for_each_single_bit_enumerator<TF>(
        [&](TF, std::string_view name) noexcept {
            if (!first) buf[pos++] = ',';
            for (char c : name) buf[pos++] = c;
            first = false;
        });
    return std::string_view{buf, pos} == "Alpha,Beta,Gamma,Delta";
}
static_assert(single_bit_iteration_yields_declaration_order());

// ── Concept gate — bare integer / unscoped enum rejected ────────────

template <class T>
concept can_bits_to_string = requires(T t, char* p, size_t s) {
    { bits_to_string<T>(::crucible::safety::Bits<T>{}, p, s) }
        -> std::same_as<size_t>;
};
static_assert( can_bits_to_string<TF>);
static_assert(!can_bits_to_string<int>,
    "bits_to_string<int> MUST NOT instantiate — int is not a scoped "
    "enum, so Bits<int> is itself ill-formed (ScopedEnum concept).");

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    char buf[64] = {};

    // Empty.
    BTF empty{};
    if (bits_to_string<TF>(empty, buf, sizeof(buf)) != 0) std::abort();
    if (buf[0] != '\0') std::abort();

    // Single flag.
    BTF a{TF::Alpha};
    auto na = bits_to_string<TF>(a, buf, sizeof(buf));
    if (na != 5) std::abort();
    if (std::string_view{buf} != "Alpha") std::abort();

    // Multi-flag — declaration order.
    BTF abc{TF::Alpha, TF::Beta, TF::Gamma};
    auto nabc = bits_to_string<TF>(abc, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta|Gamma") std::abort();
    if (nabc != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    // Composite enumerator (AlphaBeta = 0x03) skipped under multi-set.
    BTF ab{TF::Alpha, TF::Beta};
    auto nab = bits_to_string<TF>(ab, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta") std::abort();
    if (nab != 10) std::abort();

    // Truncation.
    char small[8] = {};
    auto nt = bits_to_string<TF>(ab, small, sizeof(small));
    if (nt != 10) std::abort();
    if (std::string_view{small} != "Alpha|B") std::abort();
    if (small[7] != '\0') std::abort();

    // Probe (cap=0).  out=nullptr is permitted ONLY when cap==0; the
    // contract enforces that combination.  The function still computes
    // the chars-needed count (snprintf-style probing convention).
    auto np = bits_to_string<TF>(abc, nullptr, 0);
    if (np != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    // Exact-fit capacity boundary — runtime check on the consteval test.
    {
        char tight[11] = {};
        auto nfit = bits_to_string<TF>(BTF{TF::Alpha, TF::Beta},
                                       tight, sizeof(tight));
        if (nfit != 10) std::abort();
        if (std::string_view{tight} != "Alpha|Beta") std::abort();
        if (tight[10] != '\0') std::abort();
    }
    // One-byte-short — runtime mirror of the consteval test.
    {
        char short_buf[10] = {};
        auto nshort = bits_to_string<TF>(BTF{TF::Alpha, TF::Beta},
                                         short_buf, sizeof(short_buf));
        if (nshort != 10) std::abort();
        if (std::string_view{short_buf} != "Alpha|Bet") std::abort();
        if (short_buf[9] != '\0') std::abort();
    }
    // Zero-named enumerator — enumerator_name returns "None" even
    // though bits_to_string skips it.
    if (enumerator_name(TF::None) != "None") std::abort();

    // enumerator_name positive.
    if (enumerator_name(TF::Alpha) != "Alpha") std::abort();
    if (enumerator_name(TF::Delta) != "Delta") std::abort();

    // enumerator_name composite-but-named.
    if (enumerator_name(TF::AlphaBeta) != "AlphaBeta") std::abort();

    // enumerator_name composite-not-named.
    auto fancy = static_cast<TF>(
        static_cast<std::uint8_t>(TF::Alpha)
        | static_cast<std::uint8_t>(TF::Delta));    // 0x09 — unnamed
    if (!enumerator_name(fancy).empty()) std::abort();

    // for_each_enumerator iterates all six declared enumerators.
    int counter = 0;
    for_each_enumerator<TF>(
        [&](TF, std::string_view) noexcept { ++counter; });
    if (counter != 6) std::abort();

    // for_each_single_bit_enumerator filters at compile time — only
    // four single-bit names reach the lambda.
    int single_bit_counter = 0;
    for_each_single_bit_enumerator<TF>(
        [&](TF, std::string_view) noexcept { ++single_bit_counter; });
    if (single_bit_counter != 4) std::abort();
}

}  // namespace detail::reflected_self_test

}  // namespace crucible::safety::reflected
