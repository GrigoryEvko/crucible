#pragma once

// ── crucible::safety::DetSafe<DetSafeTier_v Tier, T> ────────────────
//
// Type-pinned determinism-safety wrapper.  A value of type T whose
// determinism-safety tier (NonDeterministicSyscall ⊑ FilesystemMtime
// ⊑ EntropyRead ⊑ WallClockRead ⊑ MonotonicClockRead ⊑ PhiloxRng ⊑
// Pure) is fixed at the type level via the non-type template parameter
// Tier.  First Month-2 wrapper from the 28_04_2026_effects.md §4.3.1
// catalog (FOUND-G14) — THE LOAD-BEARING ONE.  Without it, the 8th
// safety axiom (DetSafe per CLAUDE.md §II.8) is the only un-fenced
// axiom in the framework: validation depends entirely on the
// `bit_exact_replay_invariant` CI test running 12 hours after a
// regression lands.  With it, Cipher::record_event refuses non-Pure-
// or-PhiloxRng inputs at compile time at the call site.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     DetSafeLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(DetSafe<Tier, T>) == sizeof(T))
//
//   Use case:  the load-bearing Cipher write-fence from
//              28_04_2026_effects.md §3.4:
//
//                template <DetSafeTier_v Tier>
//                auto Cipher::record_event(DetSafe<Tier, Event> event)
//                    requires (Tier >= DetSafeTier_v::PhiloxRng);
//
//              A `MonotonicClockRead`-tier value passed to
//              record_event becomes a compile error at the call site
//              instead of a `bit_exact_replay_invariant` red bar
//              hours later.  The 8th axiom finally has the same
//              compile-time fence the other 7 have.
//
//              Other production call sites:
//              - `Philox.h::generate` returns `DetSafe<PhiloxRng, ...>`
//              - `chrono::steady_clock::now()` wrappers return
//                `DetSafe<MonotonicClockRead, uint64_t>`
//              - `std::chrono::system_clock::now()` returns
//                `DetSafe<WallClockRead, uint64_t>`
//              - `getrandom(2)` wrappers return
//                `DetSafe<EntropyRead, std::byte_array>`
//
//   Axiom coverage:
//     TypeSafe — DetSafeTier_v is a strong scoped enum;
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier>
//                gates.
//     DetSafe — THIS WRAPPER IS THE 8TH AXIOM'S ENFORCEMENT.  Per-
//                value DetSafe-tier carried at the type level fences
//                the entire class of "accidentally records non-
//                deterministic value into replay log" bugs at the
//                Cipher boundary.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(DetSafe<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A determinism-safety pin is a STATIC property of HOW the value's
// bytes were produced (which class of source) — not a context the
// value lives in.  The bytes themselves carry no information about
// the source; the wrapper carries that information at the TYPE
// level.  Mirrors NumericalTier (recipe-tier promise), Consistency
// (commit protocol), OpaqueLifetime (declared scope) — all Absolute
// modalities over At<>-pinned grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// DetSafe subsumption-direction (per DetSafeLattice.h docblock):
//
//   leq(weaker, stronger) reads "weaker-determinism is below
//   stronger-determinism in the lattice."
//   Bottom = NonDeterministicSyscall (weakest); Top = Pure
//   (strongest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tier (Pure) satisfies a consumer at a
//   WEAKER tier (PhiloxRng).  Stronger replay-safety serves weaker
//   requirement.  A DetSafe<Pure, T> can be relaxed to
//   DetSafe<PhiloxRng, T> — the Pure-computed value trivially
//   satisfies the PhiloxRng-acceptance gate.
//
//   The converse is forbidden: a DetSafe<MonotonicClockRead, T>
//   CANNOT become a DetSafe<PhiloxRng, T> — the clock-reading value
//   is replay-unsafe; relaxing the type to claim PhiloxRng compliance
//   would defeat the Cipher write-fence.  No `tighten()` method
//   exists; the only way to obtain a DetSafe<Pure, T> or
//   DetSafe<PhiloxRng, T> is to construct one at a genuinely-pure
//   call site (e.g., Philox.h::generate, kernel deterministic ops).
//
// API:
//
//   - relax<WeakerTier>() &  / && — convert to a less-strict tier;
//                                   compile error if WeakerTier >
//                                   Tier (would CLAIM more replay-
//                                   safety than the source provides
//                                   — the LOAD-BEARING bug class).
//   - satisfies<RequiredTier>     — static predicate: does this
//                                   wrapper's pinned tier subsume
//                                   the required tier?  Equivalent
//                                   to leq(RequiredTier, Tier).
//   - tier (static constexpr)     — the pinned DetSafeTier_v value.
//
// SEMANTIC NOTE on the "relax" naming: for DetSafe, "weakening the
// tier" means accepting MORE impurity sources (going down the
// chain).  Calling `relax<MonotonicClockRead>()` on a Pure-pinned
// value means "I'm OK treating this Pure value as
// MonotonicClockRead-tolerable here."  This is a downgrade of the
// REPLAY-SAFETY guarantee, not the value's inherent purity.  The
// API uses `relax` for uniformity with NumericalTier / Consistency /
// OpaqueLifetime; the docblock here documents the semantic mapping.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned tier and would be the LOAD-BEARING BUG: a
// MonotonicClockRead value claiming Pure compliance would defeat
// the entire Cipher write-fence.  Hidden by the wrapper.
//
// See FOUND-G13 (algebra/lattices/DetSafeLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §3.4 + §4.3.1 for the
// production-call-site rationale and the Cipher write-fence design;
// CRUCIBLE.md §II.8 for the underlying axiom.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/DetSafeLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the DetSafeTier enum into the safety:: namespace under
// `DetSafeTier_v`.  No name collision — the wrapper class is
// `DetSafe`, not `DetSafeTier`.  Naming convention matches
// Consistency_v + Lifetime_v from sibling wrappers.
using ::crucible::algebra::lattices::DetSafeLattice;
using DetSafeTier_v = ::crucible::algebra::lattices::DetSafeTier;

template <DetSafeTier_v Tier, typename T>
class [[nodiscard]] DetSafe {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = DetSafeLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    static constexpr DetSafeTier_v tier = Tier;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed DetSafe<Pure, T> claims
    // its T{} bytes were produced under Pure discipline.  For
    // trivially-zero T, this is vacuously true (zero is bit-equal
    // across every replay).  For non-trivial T or non-zero T{} in
    // a populated context, the claim becomes meaningful only if
    // the wrapper is constructed in a context that genuinely
    // honors the tier (e.g., a kernel emit producing a
    // deterministically-zeroed buffer).  Production callers SHOULD
    // prefer the explicit-T constructor at tier-anchored production
    // sites (Philox.h::generate, kernel deterministic ops); the
    // default ctor exists for compatibility with std::array<
    // DetSafe<Pure, T>, N> / struct-field default-init contexts.
    constexpr DetSafe() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a determinism-safe production site
    // (Philox.h::generate, clock wrapper, kernel emit) constructs
    // the wrapper at the appropriate tier.
    constexpr explicit DetSafe(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit DetSafe(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — DetSafe IS COPYABLE within the
    // same tier pin.
    constexpr DetSafe(const DetSafe&)            = default;
    constexpr DetSafe(DetSafe&&)                 = default;
    constexpr DetSafe& operator=(const DetSafe&) = default;
    constexpr DetSafe& operator=(DetSafe&&)      = default;
    ~DetSafe()                                   = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        DetSafe const& a, DetSafe const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(DetSafe& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(DetSafe& a, DetSafe& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ────────
    //
    // True iff this wrapper's pinned tier is at least as strong as
    // RequiredTier.  Stronger replay-safety satisfies weaker
    // requirement (a Pure-produced value is admissible at a
    // PhiloxRng-accepting consumer).
    //
    // Use:
    //   static_assert(DetSafe<DetSafeTier_v::Pure, T>
    //                     ::satisfies<DetSafeTier_v::PhiloxRng>);
    //   // ✓ — Pure subsumes PhiloxRng
    //
    //   static_assert(!DetSafe<DetSafeTier_v::MonotonicClockRead, T>
    //                      ::satisfies<DetSafeTier_v::PhiloxRng>);
    //   // ✓ — MonotonicClockRead does NOT subsume PhiloxRng
    //   //     (this is the load-bearing rejection that the Cipher
    //   //     write-fence depends on)
    template <DetSafeTier_v RequiredTier>
    static constexpr bool satisfies = DetSafeLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ─────────
    //
    // Returns a DetSafe<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  Stronger
    // replay-safety still serves weaker requirement.
    //
    // Compile error when WeakerTier > Tier — would CLAIM more
    // replay-safety than the source provides.  THIS IS THE LOAD-
    // BEARING REJECTION: without it, a MonotonicClockRead value
    // could be re-typed as PhiloxRng and silently defeat the Cipher
    // write-fence.
    template <DetSafeTier_v WeakerTier>
        requires (DetSafeLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr DetSafe<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return DetSafe<WeakerTier, T>{this->peek()};
    }

    template <DetSafeTier_v WeakerTier>
        requires (DetSafeLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr DetSafe<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return DetSafe<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace det_safe {
    template <typename T> using Pure         = DetSafe<DetSafeTier_v::Pure,                    T>;
    template <typename T> using PhiloxRng    = DetSafe<DetSafeTier_v::PhiloxRng,               T>;
    template <typename T> using MonoClock    = DetSafe<DetSafeTier_v::MonotonicClockRead,      T>;
    template <typename T> using WallClock    = DetSafe<DetSafeTier_v::WallClockRead,           T>;
    template <typename T> using EntropyRead  = DetSafe<DetSafeTier_v::EntropyRead,             T>;
    template <typename T> using FsMtime      = DetSafe<DetSafeTier_v::FilesystemMtime,         T>;
    template <typename T> using NDS          = DetSafe<DetSafeTier_v::NonDeterministicSyscall, T>;
}  // namespace det_safe

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::det_safe_layout {

template <typename T> using PureD     = DetSafe<DetSafeTier_v::Pure,               T>;
template <typename T> using PhiloxD   = DetSafe<DetSafeTier_v::PhiloxRng,          T>;
template <typename T> using NdsD      = DetSafe<DetSafeTier_v::NonDeterministicSyscall, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD,   char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD,   double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxD, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxD, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsD,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsD,    double);

}  // namespace detail::det_safe_layout

static_assert(sizeof(DetSafe<DetSafeTier_v::Pure,                    int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng,               int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::MonotonicClockRead,      int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::WallClockRead,           int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::EntropyRead,             int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::FilesystemMtime,         int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>)    == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::Pure,                    double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::det_safe_self_test {

using PureInt     = DetSafe<DetSafeTier_v::Pure,               int>;
using PhiloxInt   = DetSafe<DetSafeTier_v::PhiloxRng,          int>;
using MonoInt     = DetSafe<DetSafeTier_v::MonotonicClockRead, int>;
using NdsInt      = DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr PureInt d_default{};
static_assert(d_default.peek() == 0);
static_assert(d_default.tier == DetSafeTier_v::Pure);

inline constexpr PureInt d_explicit{42};
static_assert(d_explicit.peek() == 42);

// ── Pinned tier accessor ──────────────────────────────────────────
static_assert(PureInt::tier   == DetSafeTier_v::Pure);
static_assert(PhiloxInt::tier == DetSafeTier_v::PhiloxRng);
static_assert(MonoInt::tier   == DetSafeTier_v::MonotonicClockRead);
static_assert(NdsInt::tier    == DetSafeTier_v::NonDeterministicSyscall);

// ── satisfies<RequiredTier> — subsumption-up direction ────────────
//
// Pure satisfies every consumer.
static_assert(PureInt::satisfies<DetSafeTier_v::Pure>);
static_assert(PureInt::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(PureInt::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::EntropyRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::FilesystemMtime>);
static_assert(PureInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);

// PhiloxRng satisfies weaker-or-equal tiers; FAILS on Pure.  THIS IS
// THE LOAD-BEARING POSITIVE TEST: PhiloxRng-pinned values pass the
// Cipher write-fence (`requires PhiloxRng` is a self-or-stronger
// gate; PhiloxRng is the weakest acceptable).
static_assert( PhiloxInt::satisfies<DetSafeTier_v::PhiloxRng>);          // self
static_assert( PhiloxInt::satisfies<DetSafeTier_v::MonotonicClockRead>); // weaker
static_assert( PhiloxInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert( PhiloxInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!PhiloxInt::satisfies<DetSafeTier_v::Pure>);               // STRONGER fails ✓

// MonotonicClockRead does NOT satisfy PhiloxRng — THE LOAD-BEARING
// REJECTION.  A MonotonicClockRead-pinned value cannot pass through
// the Cipher write-fence.  Without this rejection, the Cipher write-
// fence is silently defeated by clock reads flowing into the replay
// log.
static_assert(!MonoInt::satisfies<DetSafeTier_v::PhiloxRng>,
    "MonotonicClockRead MUST NOT satisfy PhiloxRng — this is the "
    "load-bearing rejection that the Cipher write-fence depends on. "
    "If this fires, the 8th axiom (DetSafe) is no longer compile-"
    "time-fenced and cross-replay determinism may regress without "
    "warning.");
static_assert( MonoInt::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert( MonoInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert( MonoInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);

// NDS satisfies only NDS.
static_assert( NdsInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::FilesystemMtime>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::Pure>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_pure_to_philox =
    PureInt{42}.relax<DetSafeTier_v::PhiloxRng>();
static_assert(from_pure_to_philox.peek() == 42);
static_assert(from_pure_to_philox.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto from_pure_to_nds =
    PureInt{99}.relax<DetSafeTier_v::NonDeterministicSyscall>();
static_assert(from_pure_to_nds.peek() == 99);
static_assert(from_pure_to_nds.tier == DetSafeTier_v::NonDeterministicSyscall);

inline constexpr auto from_philox_to_mono =
    PhiloxInt{7}.relax<DetSafeTier_v::MonotonicClockRead>();
static_assert(from_philox_to_mono.peek() == 7);

inline constexpr auto from_philox_to_self =
    PhiloxInt{8}.relax<DetSafeTier_v::PhiloxRng>();   // identity
static_assert(from_philox_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, DetSafeTier_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<PureInt,   DetSafeTier_v::PhiloxRng>);          // ✓ down
static_assert( can_relax<PureInt,   DetSafeTier_v::NonDeterministicSyscall>); // ✓ down
static_assert( can_relax<PhiloxInt, DetSafeTier_v::MonotonicClockRead>); // ✓ down
static_assert( can_relax<PhiloxInt, DetSafeTier_v::PhiloxRng>);          // ✓ self
static_assert(!can_relax<PhiloxInt, DetSafeTier_v::Pure>,                 // ✗ up
    "relax<Pure> on a PhiloxRng-pinned wrapper MUST be rejected — "
    "this is the load-bearing claim-stronger-than-source rejection. "
    "If this fires, the wrapper is no longer fencing the 8th-axiom "
    "load-bearing bug class.");
static_assert(!can_relax<MonoInt,   DetSafeTier_v::PhiloxRng>);           // ✗ up
static_assert(!can_relax<NdsInt,    DetSafeTier_v::FilesystemMtime>);     // ✗ up
// NDS reflexivity — the bottom of the chain still admits relax to
// itself (leq is reflexive at every point including bottom).  Pinning
// this proves the requires-clause uses ≤ not strict-< at the chain
// endpoint; a refactor accidentally using strict-< would break NDS-
// to-NDS identity and break user code that materialized NDS values
// through the relax<> surface.
static_assert( can_relax<NdsInt,    DetSafeTier_v::NonDeterministicSyscall>); // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(PureInt::value_type_name().ends_with("int"));
static_assert(PureInt::lattice_name()   == "DetSafeLattice::At<Pure>");
static_assert(PhiloxInt::lattice_name() == "DetSafeLattice::At<PhiloxRng>");
static_assert(MonoInt::lattice_name()   == "DetSafeLattice::At<MonotonicClockRead>");
static_assert(NdsInt::lattice_name()    == "DetSafeLattice::At<NonDeterministicSyscall>");

// ── swap exchanges T values within the same tier pin ─────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    PureInt a{10};
    PureInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    PureInt a{10};
    PureInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    PureInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-tier, same-T comparison ─────────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    PureInt a{42};
    PureInt b{42};
    PureInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// SFINAE: operator== is only present when T has its own ==.
struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert( can_equality_compare<PureInt>);
static_assert(!can_equality_compare<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — DetSafe<Pure, NoEqualityT>
// must inherit that deletion.  Pins the structural property that
// T's move-only-ness propagates through the wrapper layer.  Mirrors
// the Linear<int> cross-composition cell's discipline at the wrapper
// boundary instead of the cross-wrapper composition surface.
static_assert(!std::is_copy_constructible_v<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>,
    "DetSafe<Tier, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper — the wrapper has accidentally "
    "introduced its own copy ctor that bypasses T's move-only "
    "discipline.");
static_assert(std::is_move_constructible_v<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    PureInt a{99};
    auto b = a.relax<DetSafeTier_v::Pure>();
    return b.peek() == 99 && b.tier == DetSafeTier_v::Pure;
}
static_assert(relax_to_self_is_identity());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(PureInt::value_type_name().size() > 0);
static_assert(PureInt::lattice_name().size() > 0);
static_assert(PureInt::lattice_name().starts_with("DetSafeLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(det_safe::Pure<int>::tier        == DetSafeTier_v::Pure);
static_assert(det_safe::PhiloxRng<int>::tier   == DetSafeTier_v::PhiloxRng);
static_assert(det_safe::MonoClock<int>::tier   == DetSafeTier_v::MonotonicClockRead);
static_assert(det_safe::WallClock<int>::tier   == DetSafeTier_v::WallClockRead);
static_assert(det_safe::EntropyRead<int>::tier == DetSafeTier_v::EntropyRead);
static_assert(det_safe::FsMtime<int>::tier     == DetSafeTier_v::FilesystemMtime);
static_assert(det_safe::NDS<int>::tier         == DetSafeTier_v::NonDeterministicSyscall);

static_assert(std::is_same_v<det_safe::Pure<double>,
                             DetSafe<DetSafeTier_v::Pure, double>>);

// ── Cipher write-fence simulation — the load-bearing scenario ────
//
// This block simulates the 28_04 §3.4 Cipher write-fence at the
// concept level.  A function like Cipher::record_event would have
// the requires-clause:
//   `requires DetSafe<Tier, T>::satisfies<DetSafeTier_v::PhiloxRng>`
// (or equivalently `requires (Tier == Pure || Tier == PhiloxRng)`).
//
// Below: the concept can_pass_cipher_fence proves that
//   Pure-tier values PASS the gate (✓)
//   PhiloxRng-tier values PASS the gate (✓ — at the boundary)
//   MonotonicClockRead-tier values are REJECTED (✓ — load-bearing)
//   NDS-tier values are REJECTED (✓)

template <typename W>
concept can_pass_cipher_fence =
    W::template satisfies<DetSafeTier_v::PhiloxRng>;

static_assert( can_pass_cipher_fence<PureInt>,
    "Pure-tier value MUST pass the Cipher write-fence (Pure ≥ PhiloxRng).");
static_assert( can_pass_cipher_fence<PhiloxInt>,
    "PhiloxRng-tier value MUST pass the Cipher write-fence (PhiloxRng = boundary).");
static_assert(!can_pass_cipher_fence<MonoInt>,
    "MonotonicClockRead-tier value MUST be REJECTED at the Cipher "
    "write-fence — this is the LOAD-BEARING TEST.  The 8th axiom "
    "(DetSafe) is enforced by exactly this rejection.  If this fires, "
    "clock reads can flow into the replay log undetected.");
static_assert(!can_pass_cipher_fence<NdsInt>,
    "NonDeterministicSyscall-tier value MUST be REJECTED at the "
    "Cipher write-fence.");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    PureInt a{};
    PureInt b{42};
    PureInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static tier accessor at runtime.
    if (PureInt::tier != DetSafeTier_v::Pure) {
        std::abort();
    }

    // peek_mut.
    PureInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    PureInt sx{1};
    PureInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax<WeakerTier> — both overloads.
    PureInt source{77};
    auto relaxed_copy = source.relax<DetSafeTier_v::PhiloxRng>();
    auto relaxed_move = std::move(source).relax<DetSafeTier_v::MonotonicClockRead>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = PureInt::satisfies<DetSafeTier_v::PhiloxRng>;
    [[maybe_unused]] bool s2 = MonoInt::satisfies<DetSafeTier_v::PhiloxRng>;

    // operator== — same-tier.
    PureInt eq_a{42};
    PureInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    PureInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    det_safe::Pure<int>      alias_pure{123};
    det_safe::PhiloxRng<int> alias_philox{456};
    [[maybe_unused]] auto av = alias_pure.peek();
    [[maybe_unused]] auto pv = alias_philox.peek();

    // Cipher write-fence simulation at runtime.
    [[maybe_unused]] bool can_pure_pass   = can_pass_cipher_fence<PureInt>;
    [[maybe_unused]] bool can_philox_pass = can_pass_cipher_fence<PhiloxInt>;
    [[maybe_unused]] bool can_mono_pass   = can_pass_cipher_fence<MonoInt>;
}

}  // namespace detail::det_safe_self_test

}  // namespace crucible::safety
