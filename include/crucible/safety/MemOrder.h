#pragma once

// ── crucible::safety::MemOrder<MemOrderTag_v Tag, T> ────────────────
//
// Type-pinned memory-ordering wrapper.  A value of type T whose
// permitted memory ordering for atomic ops (SeqCst ⊑ AcqRel ⊑
// Release ⊑ Acquire ⊑ Relaxed) is fixed at the type level via the
// non-type template parameter Tag.  Fourth Month-2 chain wrapper
// from 28_04_2026_effects.md §4.3.4 (FOUND-G29).
//
// THE LOAD-BEARING USE CASE: type-fences CLAUDE.md §VI's "never use
// `memory_order_seq_cst` outside narrow exceptions" discipline.
// Today this is review-enforced; with the wrapper, a function
// declared `requires MemOrder::satisfies<AcqRel>` rejects callees
// carrying `MemOrderTag::SeqCst` at compile time.
//
// Composes orthogonally with HotPath / Wait via wrapper-nesting.
// The canonical hot-path atomic-RMW caller:
//
//   HotPath<Hot, Wait<SpinPause, MemOrder<AcqRel, T>>>
//
// — a foreground hot-path function using only spin-pause waits and
// AcqRel (no SeqCst) memory ordering.  All three axes EBO-collapse.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     MemOrderLattice::At<Tag>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tag>::element_type is
//                 empty, sizeof(MemOrder<Tag, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.4 + CLAUDE.md §VI):
//     - TraceRing head/tail counter loads of OWN atomic — Relaxed
//     - SPSC ring publish (cross-thread) — Release
//     - SPSC ring consume (cross-thread) — Acquire
//     - KernelCache slot CAS — AcqRel
//     - Cipher cross-region ordering (rare) — SeqCst (cold-tier)
//
//   The bug class caught: a refactor that adds `seq_cst` to a hot-
//   path atomic op.  Today caught by review (CLAUDE.md §VI ban) or
//   perf regression hours later; with the wrapper, becomes a
//   compile error at the call boundary because hot-path call sites
//   declare `requires MemOrder::satisfies<AcqRel>` and SeqCst-tier
//   callees fail the gate.
//
//   Axiom coverage:
//     TypeSafe — MemOrderTag_v is a strong scoped enum;
//                cross-tag mismatches are compile errors via the
//                relax<WeakerTag>() and satisfies<RequiredTag>
//                gates.
//     ThreadSafe — THE LOAD-BEARING AXIS.  CLAUDE.md §VI's seq_cst
//                  ban becomes type-fenced at every site.
//     MemSafe — defaulted copy/move; T's move semantics carry
//               through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(MemOrder<Tag, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A memory-ordering pin is a STATIC property of WHAT FENCE the
// function uses — not a context the value lives in.  Mirrors the
// five sister chain wrappers — all Absolute over At<>-pinned grades.
//
// ── Tag-conversion API: relax + satisfies ──────────────────────────
//
// MemOrder subsumption-direction (per MemOrderLattice.h docblock):
//
//   Bottom = SeqCst (weakest claim; uses heaviest fence).
//   Top    = Relaxed (strongest claim; uses no fence).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tag (Relaxed) satisfies a consumer at
//   a WEAKER tag (Acquire).  Stronger no-fence-needed claim serves
//   weaker requirement.  A MemOrder<Relaxed, T> can be relaxed to
//   MemOrder<Acquire, T> — the relaxed-only value trivially
//   satisfies the Acquire-acceptance gate.
//
//   The converse is forbidden: a MemOrder<SeqCst, T> CANNOT become
//   a MemOrder<Relaxed, T> — the seq-cst-fenced value carries a
//   total-order dependency that the relaxed discipline forbids.
//   No `tighten()` method exists.
//
// API:
//   - relax<WeakerTag>() &  / && — convert to a less-strict tag;
//                                  compile error if WeakerTag > Tag.
//   - satisfies<RequiredTag>     — static predicate.
//   - tag (static constexpr)     — the pinned MemOrderTag_v value.
//
// SEMANTIC NOTE on the chain LINEARIZATION: per MemOrderLattice.h
// divergence (2), the chain order between Acquire and Release is
// purely an ARTIFACT of the spec's enum ordinal (Acquire above
// Release) — it does NOT model semantic interchangeability between
// the two C++ memory orderings.  Production callers SHOULD specify
// the exact memory ordering they want; relax<Acquire→Release>
// means "I claimed I needed an acquire-fence-friendly context, but
// I'm fine being treated as release-fence-friendly here" — an
// admission relaxation, not a semantic equivalence claim.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned tag and would be the LOAD-BEARING BUG: a SeqCst-tier
// value claiming Relaxed compliance would defeat the seq_cst-ban
// discipline.  Hidden by the wrapper.
//
// See FOUND-G28 (algebra/lattices/MemOrderLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.4 + CLAUDE.md
// §VI for the production-call-site rationale; CLAUDE.md §III opt-out
// for the consume omission.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the MemOrderTag enum into the safety:: namespace under
// `MemOrderTag_v`.  No name collision — the wrapper class is
// `MemOrder`, not `MemOrderTag`.
using ::crucible::algebra::lattices::MemOrderLattice;
using MemOrderTag_v = ::crucible::algebra::lattices::MemOrderTag;

template <MemOrderTag_v Tag, typename T>
class [[nodiscard]] MemOrder {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = MemOrderLattice::At<Tag>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tag — exposed as a static constexpr for callers
    // doing tag-aware dispatch without instantiating the wrapper.
    static constexpr MemOrderTag_v tag = Tag;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    constexpr MemOrder() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit MemOrder(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit MemOrder(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr MemOrder(const MemOrder&)            = default;
    constexpr MemOrder(MemOrder&&)                 = default;
    constexpr MemOrder& operator=(const MemOrder&) = default;
    constexpr MemOrder& operator=(MemOrder&&)      = default;
    ~MemOrder()                                    = default;

    [[nodiscard]] friend constexpr bool operator==(
        MemOrder const& a, MemOrder const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names ────────────────────────────────────────────
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
    constexpr void swap(MemOrder& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(MemOrder& a, MemOrder& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTag> ─────────────────────────────────────
    template <MemOrderTag_v RequiredTag>
    static constexpr bool satisfies = MemOrderLattice::leq(RequiredTag, Tag);

    // ── relax<WeakerTag> ───────────────────────────────────────────
    template <MemOrderTag_v WeakerTag>
        requires (MemOrderLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr MemOrder<WeakerTag, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return MemOrder<WeakerTag, T>{this->peek()};
    }

    template <MemOrderTag_v WeakerTag>
        requires (MemOrderLattice::leq(WeakerTag, Tag))
    [[nodiscard]] constexpr MemOrder<WeakerTag, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return MemOrder<WeakerTag, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace mem_order {
    template <typename T> using Relaxed = MemOrder<MemOrderTag_v::Relaxed, T>;
    template <typename T> using Acquire = MemOrder<MemOrderTag_v::Acquire, T>;
    template <typename T> using Release = MemOrder<MemOrderTag_v::Release, T>;
    template <typename T> using AcqRel  = MemOrder<MemOrderTag_v::AcqRel,  T>;
    template <typename T> using SeqCst  = MemOrder<MemOrderTag_v::SeqCst,  T>;
}  // namespace mem_order

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::mem_order_layout {

template <typename T> using RelaxM = MemOrder<MemOrderTag_v::Relaxed, T>;
template <typename T> using AcqRelM = MemOrder<MemOrderTag_v::AcqRel,  T>;
template <typename T> using SeqCstM = MemOrder<MemOrderTag_v::SeqCst,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM,  char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RelaxM,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelM, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AcqRelM, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstM, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SeqCstM, double);

}  // namespace detail::mem_order_layout

static_assert(sizeof(MemOrder<MemOrderTag_v::Relaxed, int>)    == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, int>)    == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Release, int>)    == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::AcqRel,  int>)    == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::SeqCst,  int>)    == sizeof(int));
static_assert(sizeof(MemOrder<MemOrderTag_v::Relaxed, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::mem_order_self_test {

using RelaxInt   = MemOrder<MemOrderTag_v::Relaxed, int>;
using AcqInt     = MemOrder<MemOrderTag_v::Acquire, int>;
using RelInt     = MemOrder<MemOrderTag_v::Release, int>;
using AcqRelInt  = MemOrder<MemOrderTag_v::AcqRel,  int>;
using SeqCstInt  = MemOrder<MemOrderTag_v::SeqCst,  int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr RelaxInt m_default{};
static_assert(m_default.peek() == 0);
static_assert(m_default.tag == MemOrderTag_v::Relaxed);

inline constexpr RelaxInt m_explicit{42};
static_assert(m_explicit.peek() == 42);

inline constexpr RelaxInt m_in_place{std::in_place, 7};
static_assert(m_in_place.peek() == 7);

// ── Pinned tag accessor ──────────────────────────────────────────
static_assert(RelaxInt::tag   == MemOrderTag_v::Relaxed);
static_assert(AcqInt::tag     == MemOrderTag_v::Acquire);
static_assert(RelInt::tag     == MemOrderTag_v::Release);
static_assert(AcqRelInt::tag  == MemOrderTag_v::AcqRel);
static_assert(SeqCstInt::tag  == MemOrderTag_v::SeqCst);

// ── satisfies<RequiredTag> — subsumption-up direction ────────────
//
// Relaxed satisfies every consumer.  THIS IS THE LOAD-BEARING
// POSITIVE TEST: Relaxed-tier values pass every concept gate
// (including the hot-path-only gate at AcqRel).
static_assert(RelaxInt::satisfies<MemOrderTag_v::Relaxed>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::Release>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::AcqRel>);
static_assert(RelaxInt::satisfies<MemOrderTag_v::SeqCst>);

// Chain reminder (per MemOrderLattice):
//   SeqCst (bottom) ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed (top)
// satisfies<R> means leq(R, Self), i.e. R is below or equal to Self.
// So a wrapper at tier T satisfies all tiers ≤ T (self + everything
// further DOWN the chain).

// Acquire (chain index 3) satisfies: Acquire (self) + Release +
// AcqRel + SeqCst (all below).  FAILS on Relaxed (above).
static_assert( AcqInt::satisfies<MemOrderTag_v::Acquire>);   // self
static_assert( AcqInt::satisfies<MemOrderTag_v::Release>);   // below in chain
static_assert( AcqInt::satisfies<MemOrderTag_v::AcqRel>);    // below in chain
static_assert( AcqInt::satisfies<MemOrderTag_v::SeqCst>);    // below in chain (bottom)
static_assert(!AcqInt::satisfies<MemOrderTag_v::Relaxed>);   // above in chain ✗

// Release (chain index 2) satisfies: Release (self) + AcqRel +
// SeqCst (all below).  FAILS on Acquire and Relaxed (above).
static_assert( RelInt::satisfies<MemOrderTag_v::Release>);   // self
static_assert( RelInt::satisfies<MemOrderTag_v::AcqRel>);    // below in chain
static_assert( RelInt::satisfies<MemOrderTag_v::SeqCst>);    // below in chain
static_assert(!RelInt::satisfies<MemOrderTag_v::Acquire>);   // above in chain ✗
static_assert(!RelInt::satisfies<MemOrderTag_v::Relaxed>);   // above in chain ✗

// AcqRel (chain index 1) satisfies: AcqRel (self) + SeqCst (below).
// FAILS on Release / Acquire / Relaxed (all above).
static_assert( AcqRelInt::satisfies<MemOrderTag_v::AcqRel>);  // self
static_assert( AcqRelInt::satisfies<MemOrderTag_v::SeqCst>);  // below in chain
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Release>,  // above in chain ✗
    "AcqRel MUST NOT satisfy Release — Release is above AcqRel in "
    "the chain (Release makes a stronger no-fence claim than the "
    "RMW combined Acquire+Release).  If this fires, AcqRel values "
    "could silently flow into Release-only-admitting call sites.");
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Acquire>); // above ✗
static_assert(!AcqRelInt::satisfies<MemOrderTag_v::Relaxed>); // above ✗

// SeqCst (chain bottom) satisfies only SeqCst — THE LOAD-BEARING
// REJECTION for the CLAUDE.md §VI seq_cst ban.  A SeqCst-rowed
// function cannot pass a hot-path admission gate at AcqRel or above.
static_assert( SeqCstInt::satisfies<MemOrderTag_v::SeqCst>);  // self
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::AcqRel>,
    "SeqCst MUST NOT satisfy AcqRel — this is the load-bearing "
    "rejection that the CLAUDE.md §VI seq_cst-ban depends on. "
    "If this fires, seq_cst-fenced values can silently flow into "
    "hot-path atomic call sites, breaking the per-call shape budget "
    "(seq_cst on x86 = ~30-100ns, AcqRel = ~5-10ns).");
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Release>);
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Acquire>);
static_assert(!SeqCstInt::satisfies<MemOrderTag_v::Relaxed>);

// ── relax<WeakerTag> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_relax_to_acq =
    RelaxInt{42}.relax<MemOrderTag_v::Acquire>();
static_assert(from_relax_to_acq.peek() == 42);
static_assert(from_relax_to_acq.tag == MemOrderTag_v::Acquire);

inline constexpr auto from_relax_to_seqcst =
    RelaxInt{99}.relax<MemOrderTag_v::SeqCst>();
static_assert(from_relax_to_seqcst.peek() == 99);
static_assert(from_relax_to_seqcst.tag == MemOrderTag_v::SeqCst);

inline constexpr auto from_acqrel_to_seqcst =
    AcqRelInt{7}.relax<MemOrderTag_v::SeqCst>();
static_assert(from_acqrel_to_seqcst.peek() == 7);

inline constexpr auto from_acqrel_to_self =
    AcqRelInt{8}.relax<MemOrderTag_v::AcqRel>();   // identity
static_assert(from_acqrel_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, MemOrderTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<RelaxInt,  MemOrderTag_v::Acquire>);    // ✓ down
static_assert( can_relax<RelaxInt,  MemOrderTag_v::SeqCst>);     // ✓ down (full chain)
static_assert( can_relax<RelaxInt,  MemOrderTag_v::Relaxed>);    // ✓ self
static_assert( can_relax<AcqRelInt, MemOrderTag_v::SeqCst>);     // ✓ down
static_assert( can_relax<AcqRelInt, MemOrderTag_v::AcqRel>);     // ✓ self
static_assert(!can_relax<AcqRelInt, MemOrderTag_v::Acquire>,      // ✗ up
    "relax<Acquire> on an AcqRel-pinned wrapper MUST be rejected — "
    "claiming a stronger no-fence claim than the source provides "
    "defeats the hot-path admission gate.");
static_assert(!can_relax<AcqRelInt, MemOrderTag_v::Relaxed>);    // ✗ up
static_assert(!can_relax<SeqCstInt, MemOrderTag_v::AcqRel>,       // ✗ up
    "relax<AcqRel> on a SeqCst-pinned wrapper MUST be rejected — "
    "this is THE LOAD-BEARING REJECTION for the seq_cst ban.  If "
    "this fires, seq_cst-fenced values can silently flow into hot-"
    "path call sites, breaking CLAUDE.md §VI discipline.");
static_assert(!can_relax<SeqCstInt, MemOrderTag_v::Relaxed>);    // ✗ up
// SeqCst reflexivity — chain endpoint admits relax to itself.
static_assert( can_relax<SeqCstInt, MemOrderTag_v::SeqCst>);     // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(RelaxInt::value_type_name().ends_with("int"));
static_assert(RelaxInt::lattice_name()  == "MemOrderLattice::At<Relaxed>");
static_assert(AcqInt::lattice_name()    == "MemOrderLattice::At<Acquire>");
static_assert(RelInt::lattice_name()    == "MemOrderLattice::At<Release>");
static_assert(AcqRelInt::lattice_name() == "MemOrderLattice::At<AcqRel>");
static_assert(SeqCstInt::lattice_name() == "MemOrderLattice::At<SeqCst>");

// ── swap exchanges T values within the same tag pin ──────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tag() noexcept {
    RelaxInt a{10};
    RelaxInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tag());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    RelaxInt a{10};
    RelaxInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    RelaxInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-tag, same-T comparison ─────────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    RelaxInt a{42};
    RelaxInt b{42};
    RelaxInt c{43};
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

static_assert( can_equality_compare<RelaxInt>);
static_assert(!can_equality_compare<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — MemOrder<Relaxed, NoEqualityT>
// must inherit that deletion.
static_assert(!std::is_copy_constructible_v<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>,
    "MemOrder<Tag, T> must transitively inherit T's copy-deletion.");
static_assert(std::is_move_constructible_v<MemOrder<MemOrderTag_v::Relaxed, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    RelaxInt a{99};
    auto b = a.relax<MemOrderTag_v::Relaxed>();
    return b.peek() == 99 && b.tag == MemOrderTag_v::Relaxed;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, MemOrderTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, MemOrderTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using RelaxMoveOnly = MemOrder<MemOrderTag_v::Relaxed, MoveOnlyT>;
static_assert( can_relax_rvalue<RelaxMoveOnly, MemOrderTag_v::Acquire>,
    "relax<>() && MUST work for move-only T.");
static_assert(!can_relax_lvalue<RelaxMoveOnly, MemOrderTag_v::Acquire>,
    "relax<>() const& on move-only T MUST be rejected.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    RelaxMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<MemOrderTag_v::Acquire>();
    return dst.peek().v == 77 && dst.tag == MemOrderTag_v::Acquire;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection ────────────────────────────────────
static_assert(RelaxInt::value_type_name().size() > 0);
static_assert(RelaxInt::lattice_name().size() > 0);
static_assert(RelaxInt::lattice_name().starts_with("MemOrderLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(mem_order::Relaxed<int>::tag == MemOrderTag_v::Relaxed);
static_assert(mem_order::Acquire<int>::tag == MemOrderTag_v::Acquire);
static_assert(mem_order::Release<int>::tag == MemOrderTag_v::Release);
static_assert(mem_order::AcqRel<int>::tag  == MemOrderTag_v::AcqRel);
static_assert(mem_order::SeqCst<int>::tag  == MemOrderTag_v::SeqCst);

static_assert(std::is_same_v<mem_order::Relaxed<double>,
                             MemOrder<MemOrderTag_v::Relaxed, double>>);

// ── Hot-path admission simulation — the LOAD-BEARING scenario ────
//
// The dispatcher's hot-path admission gate (per 28_04 §6.4 + the
// CLAUDE.md §VI seq_cst ban) requires ordering at least as strong
// as AcqRel.  Concrete simulation:
//
//   template <typename T>
//   void hot_path_atomic_site(HotPath<Hot, MemOrder<AcqRel, T>>);
//
// Below: the concept is_hot_path_atomic_admissible proves that
//   Relaxed / Acquire / Release / AcqRel PASS the gate (✓)
//   SeqCst is REJECTED (✓ — the LOAD-BEARING test for the §VI ban)

template <typename W>
concept is_hot_path_atomic_admissible =
    W::template satisfies<MemOrderTag_v::AcqRel>;

static_assert( is_hot_path_atomic_admissible<RelaxInt>,
    "Relaxed-tier value MUST pass the hot-path atomic admission gate.");
static_assert( is_hot_path_atomic_admissible<AcqInt>,
    "Acquire-tier value MUST pass the hot-path atomic admission gate.");
static_assert( is_hot_path_atomic_admissible<RelInt>,
    "Release-tier value MUST pass the hot-path atomic admission gate.");
static_assert( is_hot_path_atomic_admissible<AcqRelInt>,
    "AcqRel-tier value MUST pass the hot-path atomic admission gate "
    "(it's the boundary).");
static_assert(!is_hot_path_atomic_admissible<SeqCstInt>,
    "SeqCst-tier value MUST be REJECTED at the hot-path atomic "
    "admission gate — this is the LOAD-BEARING TEST for CLAUDE.md "
    "§VI's seq_cst-ban discipline.  If this fires, seq_cst-fenced "
    "atomic ops can silently flow into hot-path call sites and "
    "introduce ~30-100ns total-order fences where AcqRel (~5-10ns) "
    "would suffice.");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    RelaxInt a{};
    RelaxInt b{42};
    RelaxInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (RelaxInt::tag != MemOrderTag_v::Relaxed) {
        std::abort();
    }

    RelaxInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    RelaxInt sx{1};
    RelaxInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    RelaxInt source{77};
    auto relaxed_copy = source.relax<MemOrderTag_v::Acquire>();
    auto relaxed_move = std::move(source).relax<MemOrderTag_v::SeqCst>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = RelaxInt::satisfies<MemOrderTag_v::AcqRel>;
    [[maybe_unused]] bool s2 = SeqCstInt::satisfies<MemOrderTag_v::Relaxed>;

    RelaxInt eq_a{42};
    RelaxInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    RelaxInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    mem_order::Relaxed<int> alias_relax{123};
    mem_order::AcqRel<int>  alias_acqrel{456};
    mem_order::SeqCst<int>  alias_seqcst{789};
    [[maybe_unused]] auto rv = alias_relax.peek();
    [[maybe_unused]] auto av = alias_acqrel.peek();
    [[maybe_unused]] auto sv = alias_seqcst.peek();

    [[maybe_unused]] bool can_relax_pass  = is_hot_path_atomic_admissible<RelaxInt>;
    [[maybe_unused]] bool can_acqrel_pass = is_hot_path_atomic_admissible<AcqRelInt>;
    [[maybe_unused]] bool can_seqcst_pass = is_hot_path_atomic_admissible<SeqCstInt>;
}

}  // namespace detail::mem_order_self_test

}  // namespace crucible::safety
