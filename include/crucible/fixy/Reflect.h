#pragma once

// ── crucible::fixy::reflect — reflection surface ───────────────────
//
// Surfaces the P2996-driven reflection helpers from
// `include/crucible/{Reflect.h,safety/Reflected.h}` under
// `fixy::reflect::`.  Per misc/27_04_2026.md §3.3 + FIXY-V-040:
// closes the umbrella-reach gap where consumers wanting auto-hash /
// auto-print of a struct OR per-enumerator iteration of a scoped enum
// had to descend into two different namespaces (`crucible::` for
// struct-field reflection, `crucible::safety::reflected::` for
// enumerator reflection).  Reflect.h's own doc-block explicitly calls
// them "sister surfaces"; this umbrella unifies the reach.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   crucible::reflect_hash<T>(const T&) → uint64_t
//       — P2996-driven struct-field hash; recurses into nested
//         classes; integers / enums / floats / pointers / arrays
//         each have a dedicated fmix64-mixed branch.  Family-B hash
//         (multiplicative wymix-like outer + fmix64 finalizer).
//
//   crucible::has_reflected_hash<T> (inline constexpr bool)
//       — variable-template trait gating generic code on whether
//         reflect_hash is callable; opt-in dispatch idiom.
//
//   crucible::reflect_fmix_fold<Seed, T>(const T&) → uint64_t
//       — alternative fold pattern (seed-then-fmix64-per-field).
//         Used by feedback_signature / loopterm_hash via Merkle DAG;
//         distinct from reflect_hash bit-pattern.
//
//   crucible::reflect_print<T>(const T&, FILE*) → void
//       — debug printer; "TypeName { field0 = val, ... }" format.
//
//   safety::reflected::for_each_enumerator<E>(F&&)
//       — runtime-callable iteration over a scoped enum E's
//         enumerators; lambda receives (E value, string_view name).
//
//   safety::reflected::for_each_single_bit_enumerator<E>(F&&)
//       — compile-time-filtered iteration; only enumerators with
//         popcount(value) == 1 reach the lambda.
//
//   safety::reflected::enumerator_name<E>(E value) → string_view
//       — first-match-wins enumerator-value-to-name lookup; returns
//         empty view for non-named values.
//
//   safety::reflected::bits_to_string<E>(Bits<E>, char*, size_t) → size_t
//       — snprintf-style "Flag1|Flag2|..." formatter; composite/zero
//         enumerators are skipped by design.
//
// ── Role ───────────────────────────────────────────────────────────
//
// `fixy::reflect::` is a fixy::-level umbrella over the P2996 surface
// the codebase actually uses.  Reflect.h ships struct-field hashing
// (used by KernelCache content-addressing, feedback_signature,
// loopterm_hash); Reflected.h ships enum diagnostics (used by Bits<E>
// pretty-printing, scoped-enum reflection).  A consumer wanting both
// reflection styles previously had to include both substrates AND
// reach into two namespaces.  This umbrella collapses that.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Function-template / variable-template using-declarations are
// pure name-lookup directives; no code is generated at the umbrella
// boundary.  The underlying functions inline aggressively under -O3
// (every branch in hash_field/pack_field is `if constexpr`).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — reflect_hash/reflect_print read fields by reflection
//                splice; uninit reads are P2795R5-erroneous.
//   TypeSafe   — using-decls preserve substrate type identity (no
//                implicit conversions); concept/predicate gates are
//                identical to the substrate.
//   NullSafe   — bits_to_string contract `pre(cap == 0 || out != nullptr)`
//                preserved by alias; reflect_print's FILE* nullness
//                follows libc contract at substrate (caller responsibility).
//   MemSafe    — no allocation in any of the 8 surfaces.
//   BorrowSafe — value-only inputs except reflect_print's mutable
//                FILE*; aliasing surface is the FILE* itself.
//   ThreadSafe — pure functions of (T, …) modulo FILE* in reflect_print;
//                re-entrant from any thread on disjoint outputs.
//   LeakSafe   — no resources.
//   DetSafe    — same T + same fields → same hash bytes (Family-B);
//                fmix64 + bit_cast + scalar fold give cross-platform
//                bit-equality.  Iteration order follows reflection
//                substrate's declaration order (fixed at compile time).

#include <crucible/Reflect.h>            // struct-field reflection
#include <crucible/safety/Reflected.h>   // enum-enumerator reflection

#include <cstdint>      // self_test uses uint64_t
#include <string_view>  // self_test uses std::string_view
#include <type_traits>  // self_test uses std::is_same_v

namespace crucible::fixy::reflect {

// ═══════════════════════════════════════════════════════════════════
// ── Struct-field reflection (crucible/Reflect.h) ──────────────────
// ═══════════════════════════════════════════════════════════════════

// reflect_hash<T>(obj) — P2996-driven Family-B struct-field hash.
// Iterates non-static data members via reflection; fmix64-mixes each
// field; deterministic across platforms.
using ::crucible::reflect_hash;

// has_reflected_hash<T> — variable-template trait reporting whether
// reflect_hash<T> is instantiable.  Used to gate generic code paths
// that opt into reflection-driven hashing.
using ::crucible::has_reflected_hash;

// reflect_fmix_fold<Seed, T>(obj) — alternative fold pattern with
// per-field fmix64 (vs reflect_hash's multiplicative outer).  Used
// for feedback_signature / loopterm_hash in Merkle DAG.
using ::crucible::reflect_fmix_fold;

// reflect_print<T>(obj, FILE*) — debug printer.  Format:
// "TypeName { field0 = val, field1 = val, ... }".
using ::crucible::reflect_print;

// ═══════════════════════════════════════════════════════════════════
// ── Enumerator reflection (crucible/safety/Reflected.h) ───────────
// ═══════════════════════════════════════════════════════════════════

// for_each_enumerator<E>(f) — runtime-callable iteration over E's
// enumerators.  Lambda receives (E value, string_view name); runs
// once per declared enumerator in declaration order.
using ::crucible::safety::reflected::for_each_enumerator;

// for_each_single_bit_enumerator<E>(f) — compile-time-filtered
// iteration; only enumerators with popcount(value) == 1 reach the
// lambda.  Used by bits_to_string.
using ::crucible::safety::reflected::for_each_single_bit_enumerator;

// enumerator_name<E>(value) — first-match-wins enumerator-value-to-
// name lookup; returns empty string_view when value is not a declared
// enumerator value (e.g., a composite bitwise-or of two single-bit
// flags that aren't themselves named).
using ::crucible::safety::reflected::enumerator_name;

// bits_to_string<E>(Bits<E>, char*, size_t) — snprintf-style
// "Flag1|Flag2|..." formatter for Bits<E>.  Returns chars NEEDED
// (excluding NUL); composite/zero enumerators skipped by design.
using ::crucible::safety::reflected::bits_to_string;

}  // namespace crucible::fixy::reflect

// ═══════════════════════════════════════════════════════════════════
// ── Dual-export sentinel — FIXY-V-040 ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Header-internal identity sentinels.  Same discipline as
// fixy/Handle.h::self_test (FIXY-U-016) and fixy/Kernel.h
// (FIXY-V-038 / V-039).

namespace crucible::fixy::reflect::self_test {

// ── Probe types ────────────────────────────────────────────────────

struct ReflectProbeStruct {
    std::uint32_t a = 7;
    std::uint32_t b = 11;
};

enum class ReflectProbeEnum : std::uint8_t {
    Zero  = 0x00,
    One   = 0x01,
    Two   = 0x02,
    Four  = 0x04,
    Three = 0x03,    // composite — skipped by single-bit filter
};

// ── 1. Function-template reach identity — reflect_hash ─────────────
//
// reflect_hash is `gnu::pure noexcept` but NOT constexpr — its
// recursive hash_field branch for floats / pointers / arrays uses
// runtime bit_cast paths the substrate didn't promote to constexpr.
// Compile-time identity is therefore witnessed via FUNCTION-POINTER
// identity (function addresses ARE constant expressions even for
// non-constexpr functions, since functions have static storage).
// If the using-decl truly aliases, &fixy::reflect::reflect_hash<T>
// and &crucible::reflect_hash<T> resolve to the same instantiation
// and yield the same pointer.  Value identity for runtime inputs is
// witnessed in test/test_fixy_reflect.cpp.

inline constexpr ReflectProbeStruct probe_struct{42, 99};

static_assert(
    &::crucible::fixy::reflect::reflect_hash<ReflectProbeStruct> ==
    &::crucible::reflect_hash<ReflectProbeStruct>,
    "fixy::reflect::reflect_hash MUST resolve to the same instantiated "
    "function as crucible::reflect_hash — using-decls preserve substrate "
    "identity.  Drift would mean two reach paths point to DIFFERENT "
    "functions, breaking KernelCache content-addressing (DetSafe).");

// ── 2. Variable-template reach identity — has_reflected_hash ───────
//
// has_reflected_hash<T> is an inline constexpr bool variable template;
// the using-decl makes the name visible in fixy::reflect:: while
// preserving the same instantiated value.

static_assert(
    ::crucible::fixy::reflect::has_reflected_hash<ReflectProbeStruct> ==
    ::crucible::has_reflected_hash<ReflectProbeStruct>,
    "fixy::reflect::has_reflected_hash<T> must agree with substrate.");

// Positive — ReflectProbeStruct is a class with reflectable fields.
static_assert(
    ::crucible::fixy::reflect::has_reflected_hash<ReflectProbeStruct>,
    "ReflectProbeStruct is reflectable; trait must report true.");

// Negative — int is not a class, so detect_reflected_hash returns
// false; this proves the false branch of the trait reaches through
// the alias intact.
static_assert(
    !::crucible::fixy::reflect::has_reflected_hash<int>,
    "int is not a class type; has_reflected_hash<int> must be false. "
    "Drift would mean the negative branch of the trait is bypassed.");

// ── 3. Function-template reach identity — reflect_fmix_fold ────────
//
// Distinct hash bit-pattern from reflect_hash; documenting both
// paths agree at the alias boundary.

static_assert(
    ::crucible::fixy::reflect::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct) ==
    ::crucible::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct),
    "fixy::reflect::reflect_fmix_fold<Seed, T> must produce identical "
    "output to substrate — different fold pattern from reflect_hash "
    "but same DetSafe contract.");

// Distinct seed → distinct hash (sanity-witness for the seed param
// reaching through the alias).
static_assert(
    ::crucible::fixy::reflect::reflect_fmix_fold<0xCAFEBABEULL>(probe_struct) !=
    ::crucible::fixy::reflect::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct),
    "Different seeds must produce different fmix64-fold hashes — if "
    "they collide the seed parameter has been dropped at the alias.");

// ── 4. Enumerator reflection reach — enumerator_name ───────────────
//
// safety::reflected::enumerator_name is a constexpr function template
// returning string_view; identity-witness: same string at the alias.

static_assert(
    ::crucible::fixy::reflect::enumerator_name(ReflectProbeEnum::One) ==
    ::crucible::safety::reflected::enumerator_name(ReflectProbeEnum::One),
    "fixy::reflect::enumerator_name must agree with substrate on "
    "named enumerator lookups (positive case).");

static_assert(
    ::crucible::fixy::reflect::enumerator_name(ReflectProbeEnum::One) == "One",
    "Direct positive witness — One enumerator maps to \"One\" name.");

static_assert(
    ::crucible::fixy::reflect::enumerator_name(
        static_cast<ReflectProbeEnum>(0xFF)).empty(),
    "Unknown value yields empty view, not garbage — reach-through "
    "preserves the negative case.");

// ── 5. for_each_enumerator reach — counting + ordering witness ─────
//
// consteval helper that counts enumerators via the fixy:: alias.  If
// the using-decl shadowed the template incorrectly, instantiation
// would either fail OR yield a wrong count.  ReflectProbeEnum has 5
// enumerators: Zero, One, Two, Four, Three.

[[nodiscard]] consteval int count_through_alias() noexcept {
    int n = 0;
    ::crucible::fixy::reflect::for_each_enumerator<ReflectProbeEnum>(
        [&](ReflectProbeEnum, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_through_alias() == 5,
    "ReflectProbeEnum has 5 declared enumerators; reach-through must "
    "preserve substrate's iteration cardinality.");

// ── 6. for_each_single_bit_enumerator reach — popcount filter ──────
//
// Three has popcount(0x03) == 2 → filtered out.  Zero has popcount
// 0 → filtered out.  One, Two, Four remain (popcount == 1).

[[nodiscard]] consteval int count_single_bit_through_alias() noexcept {
    int n = 0;
    ::crucible::fixy::reflect::for_each_single_bit_enumerator<ReflectProbeEnum>(
        [&](ReflectProbeEnum, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_single_bit_through_alias() == 3,
    "ReflectProbeEnum has 3 single-bit enumerators (One, Two, Four); "
    "Three (0x03, popcount=2) and Zero (popcount=0) filtered. "
    "Reach-through must preserve substrate's compile-time filter.");

// ── 7. bits_to_string reach — round-trip witness ───────────────────
//
// Bits<E>{One, Two} → "One|Two" via the fixy:: alias.  Buffer-needed
// equals the literal length; trailing NUL present.

[[nodiscard]] consteval bool bits_to_string_through_alias() noexcept {
    char buf[16] = {};
    ::crucible::safety::Bits<ReflectProbeEnum> b{
        ReflectProbeEnum::One, ReflectProbeEnum::Two};
    auto n = ::crucible::fixy::reflect::bits_to_string<ReflectProbeEnum>(
        b, buf, sizeof(buf));
    return n == 7 && std::string_view{buf} == "One|Two";
}
static_assert(bits_to_string_through_alias(),
    "Reach-through must preserve substrate's snprintf-style formatting "
    "of Bits<E> — declaration-order iteration produces \"One|Two\" for "
    "Bits<E>{One, Two}.");

// ── 8. Cardinality witness ────────────────────────────────────────
//
// 8 surfaced using-declarations across 2 substrate sub-families:
//
//   Struct-field reflection (4) — crucible/Reflect.h:
//     (1) reflect_hash<T>            — Family-B struct hash
//     (2) has_reflected_hash<T>      — opt-in gating trait
//     (3) reflect_fmix_fold<Seed, T> — fmix64-fold alternative
//     (4) reflect_print<T>           — debug formatter
//
//   Enumerator reflection (4) — crucible/safety/Reflected.h:
//     (5) for_each_enumerator<E>             — full iteration
//     (6) for_each_single_bit_enumerator<E>  — popcount-filtered
//     (7) enumerator_name<E>                 — value→name lookup
//     (8) bits_to_string<E>                  — Bits<E> formatter
//
// Future additions to safety::reflected::* / crucible::reflect_* MUST
// extend this block + bump the constant + add a sentinel above.

constexpr int reflect_alias_cardinality = 8;
static_assert(reflect_alias_cardinality == 8,
    "fixy::reflect:: cardinality changed — update Reflect.h sentinel "
    "block to track the substrate reflection surface.");

}  // namespace crucible::fixy::reflect::self_test
