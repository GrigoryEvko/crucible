#pragma once

// DetSafe<Tier, T> pins a value to how deterministic its source is.
//
// The tiers form a chain from the weakest source to the strongest:
// NonDeterministicSyscall, FilesystemMtime, EntropyRead,
// WallClockRead, MonotonicClockRead, PhiloxRng, then Pure.  A value
// that replays bit-identically sits high in the chain, and one that
// depends on the moment it ran sits low.
//
// satisfies<Required> asks whether the pinned tier covers what a
// consumer demands: stronger satisfies weaker.  A Pure value is
// admissible wherever PhiloxRng is required.  A clock read is not.
//
// relax<Weaker> moves down the chain and never up.  There is no
// tighten(): the only way to hold a Pure or a PhiloxRng value is to
// build one at a site that genuinely is that deterministic.  Moving
// up would let a clock read claim a replay safety it does not have,
// so the substrate's weaken() is deliberately not exposed here.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/DetSafeLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::DetSafeLattice;
using DetSafeTier_v = ::crucible::algebra::lattices::DetSafeTier;

template <DetSafeTier_v Tier, typename T>
class [[nodiscard]] DetSafe {
public:
    using value_type = T;
    using lattice_type = DetSafeLattice::At<Tier>;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    static constexpr DetSafeTier_v tier = Tier;

private:
    graded_type impl_;

public:
    // The default constructor pins T{} to a tier no source claimed.
    // For a trivially-zero T the claim is vacuously true, because
    // zero replays identically.  Deleting it would be the truthful
    // choice for every other T, but it is kept so the wrapper can sit
    // in an array element or a default-initialized struct field.  A
    // site that knows its source uses the explicit constructor.
    constexpr DetSafe() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit DetSafe(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit DetSafe(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                         && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr DetSafe(const DetSafe&) = default;
    constexpr DetSafe(DetSafe&&) = default;
    constexpr DetSafe& operator=(const DetSafe&) = default;
    constexpr DetSafe& operator=(DetSafe&&) = default;
    ~DetSafe() = default;

    [[nodiscard]] friend constexpr bool operator==(DetSafe const& a,
                                                   DetSafe const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(DetSafe& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(DetSafe& a, DetSafe& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    template <DetSafeTier_v RequiredTier>
    static constexpr bool satisfies = DetSafeLattice::leq(RequiredTier, Tier);

    template <DetSafeTier_v WeakerTier>
        requires(DetSafeLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr DetSafe<WeakerTier, T> relax() const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return DetSafe<WeakerTier, T>{this->peek()};
    }

    template <DetSafeTier_v WeakerTier>
        requires(DetSafeLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr DetSafe<WeakerTier, T> relax() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return DetSafe<WeakerTier, T>{std::move(impl_).consume()};
    }
};

namespace det_safe {
template <typename T>
using Pure = DetSafe<DetSafeTier_v::Pure, T>;
template <typename T>
using PhiloxRng = DetSafe<DetSafeTier_v::PhiloxRng, T>;
template <typename T>
using MonoClock = DetSafe<DetSafeTier_v::MonotonicClockRead, T>;
template <typename T>
using WallClock = DetSafe<DetSafeTier_v::WallClockRead, T>;
template <typename T>
using EntropyRead = DetSafe<DetSafeTier_v::EntropyRead, T>;
template <typename T>
using FsMtime = DetSafe<DetSafeTier_v::FilesystemMtime, T>;
template <typename T>
using NDS = DetSafe<DetSafeTier_v::NonDeterministicSyscall, T>;
}  // namespace det_safe

namespace detail::det_safe_layout {

template <typename T>
using PureD = DetSafe<DetSafeTier_v::Pure, T>;
template <typename T>
using PhiloxD = DetSafe<DetSafeTier_v::PhiloxRng, T>;
template <typename T>
using NdsD = DetSafe<DetSafeTier_v::NonDeterministicSyscall, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureD, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxD, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxD, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsD, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsD, double);

}  // namespace detail::det_safe_layout

static_assert(sizeof(DetSafe<DetSafeTier_v::Pure, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::PhiloxRng, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::MonotonicClockRead, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::WallClockRead, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::EntropyRead, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::FilesystemMtime, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>) == sizeof(int));
static_assert(sizeof(DetSafe<DetSafeTier_v::Pure, double>) == sizeof(double));

namespace detail::det_safe_self_test {

using PureInt = DetSafe<DetSafeTier_v::Pure, int>;
using PhiloxInt = DetSafe<DetSafeTier_v::PhiloxRng, int>;
using MonoInt = DetSafe<DetSafeTier_v::MonotonicClockRead, int>;
using NdsInt = DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>;

inline constexpr PureInt d_default{};
static_assert(d_default.peek() == 0);
static_assert(d_default.tier == DetSafeTier_v::Pure);

inline constexpr PureInt d_explicit{42};
static_assert(d_explicit.peek() == 42);

static_assert(PureInt::tier == DetSafeTier_v::Pure);
static_assert(PhiloxInt::tier == DetSafeTier_v::PhiloxRng);
static_assert(MonoInt::tier == DetSafeTier_v::MonotonicClockRead);
static_assert(NdsInt::tier == DetSafeTier_v::NonDeterministicSyscall);

static_assert(PureInt::satisfies<DetSafeTier_v::Pure>);
static_assert(PureInt::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(PureInt::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::EntropyRead>);
static_assert(PureInt::satisfies<DetSafeTier_v::FilesystemMtime>);
static_assert(PureInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);

static_assert(PhiloxInt::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(PhiloxInt::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(PhiloxInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert(PhiloxInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!PhiloxInt::satisfies<DetSafeTier_v::Pure>);

static_assert(!MonoInt::satisfies<DetSafeTier_v::PhiloxRng>,
              "MonotonicClockRead must not satisfy PhiloxRng.  A clock read "
              "does not reproduce on replay, so it must not reach a consumer "
              "that requires a value which does.");
static_assert(MonoInt::satisfies<DetSafeTier_v::MonotonicClockRead>);
static_assert(MonoInt::satisfies<DetSafeTier_v::WallClockRead>);
static_assert(MonoInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);

static_assert(NdsInt::satisfies<DetSafeTier_v::NonDeterministicSyscall>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::FilesystemMtime>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::PhiloxRng>);
static_assert(!NdsInt::satisfies<DetSafeTier_v::Pure>);

inline constexpr auto from_pure_to_philox = PureInt{42}.relax<DetSafeTier_v::PhiloxRng>();
static_assert(from_pure_to_philox.peek() == 42);
static_assert(from_pure_to_philox.tier == DetSafeTier_v::PhiloxRng);

inline constexpr auto from_pure_to_nds = PureInt{99}.relax<DetSafeTier_v::NonDeterministicSyscall>();
static_assert(from_pure_to_nds.peek() == 99);
static_assert(from_pure_to_nds.tier == DetSafeTier_v::NonDeterministicSyscall);

inline constexpr auto from_philox_to_mono = PhiloxInt{7}.relax<DetSafeTier_v::MonotonicClockRead>();
static_assert(from_philox_to_mono.peek() == 7);

inline constexpr auto from_philox_to_self = PhiloxInt{8}.relax<DetSafeTier_v::PhiloxRng>();
static_assert(from_philox_to_self.peek() == 8);

template <typename W, DetSafeTier_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert(can_relax<PureInt, DetSafeTier_v::PhiloxRng>);
static_assert(can_relax<PureInt, DetSafeTier_v::NonDeterministicSyscall>);
static_assert(can_relax<PhiloxInt, DetSafeTier_v::MonotonicClockRead>);
static_assert(can_relax<PhiloxInt, DetSafeTier_v::PhiloxRng>);
static_assert(!can_relax<PhiloxInt, DetSafeTier_v::Pure>,
              "relax<Pure> on a PhiloxRng value must be rejected.  It would "
              "claim a determinism the source does not provide.");
static_assert(!can_relax<MonoInt, DetSafeTier_v::PhiloxRng>);
static_assert(!can_relax<NdsInt, DetSafeTier_v::FilesystemMtime>);
static_assert(can_relax<NdsInt, DetSafeTier_v::NonDeterministicSyscall>);

static_assert(PureInt::value_type_name().ends_with("int"));
static_assert(PureInt::lattice_name() == "DetSafeLattice::At<Pure>");
static_assert(PhiloxInt::lattice_name() == "DetSafeLattice::At<PhiloxRng>");
static_assert(MonoInt::lattice_name() == "DetSafeLattice::At<MonotonicClockRead>");
static_assert(NdsInt::lattice_name() == "DetSafeLattice::At<NonDeterministicSyscall>");

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

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    PureInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    PureInt a{42};
    PureInt b{42};
    PureInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

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

static_assert(can_equality_compare<PureInt>);
static_assert(!can_equality_compare<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>,
              "DetSafe<Tier, T> must inherit deletion of T's copy "
              "constructor.  Otherwise the wrapper supplies a copy that "
              "bypasses the move-only discipline of T.");
static_assert(std::is_move_constructible_v<DetSafe<DetSafeTier_v::Pure, NoEqualityT>>);

[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    PureInt a{99};
    auto b = a.relax<DetSafeTier_v::Pure>();
    return b.peek() == 99 && b.tier == DetSafeTier_v::Pure;
}
static_assert(relax_to_self_is_identity());

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};
static_assert(!std::is_copy_constructible_v<MoveOnlyT>);
static_assert(std::is_move_constructible_v<MoveOnlyT>);

template <typename W, DetSafeTier_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, DetSafeTier_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using PureMoveOnly = DetSafe<DetSafeTier_v::Pure, MoveOnlyT>;
static_assert(can_relax_rvalue<PureMoveOnly, DetSafeTier_v::PhiloxRng>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume(), so it must not carry a "
              "copy_constructible requirement.");
static_assert(!can_relax_lvalue<PureMoveOnly, DetSafeTier_v::PhiloxRng>,
              "relax on a const lvalue must reject a move-only T.  Without "
              "the copy_constructible requirement the rejection surfaces as a "
              "deleted-copy error inside the substrate instead.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    PureMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<DetSafeTier_v::PhiloxRng>();
    return dst.peek().v == 77 && dst.tier == DetSafeTier_v::PhiloxRng;
}
static_assert(relax_move_only_works());

static_assert(PureInt::value_type_name().size() > 0);
static_assert(PureInt::lattice_name().size() > 0);
static_assert(PureInt::lattice_name().starts_with("DetSafeLattice::At<"));

static_assert(det_safe::Pure<int>::tier == DetSafeTier_v::Pure);
static_assert(det_safe::PhiloxRng<int>::tier == DetSafeTier_v::PhiloxRng);
static_assert(det_safe::MonoClock<int>::tier == DetSafeTier_v::MonotonicClockRead);
static_assert(det_safe::WallClock<int>::tier == DetSafeTier_v::WallClockRead);
static_assert(det_safe::EntropyRead<int>::tier == DetSafeTier_v::EntropyRead);
static_assert(det_safe::FsMtime<int>::tier == DetSafeTier_v::FilesystemMtime);
static_assert(det_safe::NDS<int>::tier == DetSafeTier_v::NonDeterministicSyscall);

static_assert(std::is_same_v<det_safe::Pure<double>, DetSafe<DetSafeTier_v::Pure, double>>);

// A gate that writes into a replay log admits PhiloxRng and stronger.
template <typename W>
concept can_pass_replay_log_fence = W::template satisfies<DetSafeTier_v::PhiloxRng>;

static_assert(can_pass_replay_log_fence<PureInt>, "A Pure value must pass a gate that requires PhiloxRng.");
static_assert(can_pass_replay_log_fence<PhiloxInt>, "A PhiloxRng value must pass a gate that requires PhiloxRng.  "
                                                    "It sits exactly at the boundary.");
static_assert(!can_pass_replay_log_fence<MonoInt>, "A MonotonicClockRead value must not pass a gate that requires "
                                                   "PhiloxRng.  A clock read written into a replay log makes the "
                                                   "replay diverge from the original run.");
static_assert(!can_pass_replay_log_fence<NdsInt>, "A NonDeterministicSyscall value must not pass a gate that "
                                                  "requires PhiloxRng.");

inline void runtime_smoke_test() {
    PureInt a{};
    PureInt b{42};
    PureInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (PureInt::tier != DetSafeTier_v::Pure) {
        std::abort();
    }

    PureInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    PureInt sx{1};
    PureInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    PureInt source{77};
    auto relaxed_copy = source.relax<DetSafeTier_v::PhiloxRng>();
    auto relaxed_move = std::move(source).relax<DetSafeTier_v::MonotonicClockRead>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = PureInt::satisfies<DetSafeTier_v::PhiloxRng>;
    [[maybe_unused]] bool s2 = MonoInt::satisfies<DetSafeTier_v::PhiloxRng>;

    PureInt eq_a{42};
    PureInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    PureInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    det_safe::Pure<int> alias_pure{123};
    det_safe::PhiloxRng<int> alias_philox{456};
    [[maybe_unused]] auto av = alias_pure.peek();
    [[maybe_unused]] auto pv = alias_philox.peek();

    [[maybe_unused]] bool can_pure_pass = can_pass_replay_log_fence<PureInt>;
    [[maybe_unused]] bool can_philox_pass = can_pass_replay_log_fence<PhiloxInt>;
    [[maybe_unused]] bool can_mono_pass = can_pass_replay_log_fence<MonoInt>;
}

}  // namespace detail::det_safe_self_test

}  // namespace crucible::safety
