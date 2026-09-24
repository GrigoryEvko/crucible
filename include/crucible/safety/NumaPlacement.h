#pragma once

// A value paired with the two placement constraints that say where it
// may be scheduled: a NUMA node and a CPU affinity mask.
//
// The wrapper records a claim.  It does not stop a caller rebinding a
// value to a placement the hardware cannot honour, so derive the pair
// from the measured topology rather than from arbitrary input.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_AffinityLattice.h>
#include <crucible/algebra/lattices/NumaNodeLattice.h>
#include <crucible/algebra/lattices/_ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::AffinityLattice;
using ::crucible::algebra::lattices::AffinityMask;
using ::crucible::algebra::lattices::NumaNodeId;
using ::crucible::algebra::lattices::NumaNodeLattice;

static_assert(!std::is_same_v<NumaNodeId, AffinityMask>, "NumaNodeId and AffinityMask are structurally distinct C++ "
                                                         "types.  Collapsing them removes the fence that stops a "
                                                         "caller passing the two placement axes in the wrong order.");

template <typename T>
class [[nodiscard]] NumaPlacement {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<NumaNodeLattice, AffinityLattice>;
    using placement_t = typename lattice_type::element_type;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr placement_t pack(NumaNodeId node, AffinityMask aff) noexcept {
        return placement_t{node, aff};
    }

public:
    // The default claim is the bottom of both axes, which admits
    // nowhere at all.
    constexpr NumaPlacement() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    constexpr NumaPlacement(T value, NumaNodeId node,
                            AffinityMask aff) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(node, aff)} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr NumaPlacement(std::in_place_t, NumaNodeId node, AffinityMask aff,
                            Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                     && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(node, aff)} {}

    [[nodiscard]] static constexpr NumaPlacement anywhere(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return NumaPlacement{std::move(value), NumaNodeId::Any, AffinityLattice::top()};
    }

    [[nodiscard]] static constexpr NumaPlacement
    pinned(T value, NumaNodeId node, std::uint8_t core) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return NumaPlacement{std::move(value), node, AffinityMask::single(core)};
    }

    constexpr NumaPlacement(const NumaPlacement&) = default;
    constexpr NumaPlacement(NumaPlacement&&) = default;
    constexpr NumaPlacement& operator=(const NumaPlacement&) = default;
    constexpr NumaPlacement& operator=(NumaPlacement&&) = default;
    ~NumaPlacement() = default;

    [[nodiscard]] friend constexpr bool operator==(NumaPlacement const& a,
                                                   NumaPlacement const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.numa_node() == b.numa_node() && a.affinity() == b.affinity();
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

    [[nodiscard]] constexpr NumaNodeId numa_node() const noexcept { return impl_.grade().first; }

    [[nodiscard]] constexpr AffinityMask affinity() const noexcept { return impl_.grade().second; }

    [[nodiscard]] constexpr placement_t placement() const noexcept { return impl_.grade(); }

    constexpr void swap(NumaPlacement& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(NumaPlacement& a, NumaPlacement& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    // Joining two claims widens both axes.  Two different specific
    // nodes have no common specific node, so their join is the wildcard
    // that names every node, not a failure.
    [[nodiscard]] constexpr NumaPlacement
    combine_max(NumaPlacement const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return NumaPlacement{this->peek(), NumaNodeLattice::join(this->numa_node(), other.numa_node()),
                             AffinityLattice::join(this->affinity(), other.affinity())};
    }

    [[nodiscard]] constexpr NumaPlacement
    combine_max(NumaPlacement const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        NumaNodeId joined_node = NumaNodeLattice::join(this->numa_node(), other.numa_node());
        AffinityMask joined_aff = AffinityLattice::join(this->affinity(), other.affinity());
        return NumaPlacement{std::move(impl_).consume(), joined_node, joined_aff};
    }

    // The direction is request below claim on each axis: a slot is
    // admitted when this value's claim covers it, not the reverse.
    [[nodiscard]] constexpr bool admits(NumaNodeId req_node, AffinityMask req_affinity) const noexcept {
        return NumaNodeLattice::leq(req_node, this->numa_node())
            && AffinityLattice::leq(req_affinity, this->affinity());
    }
};

namespace detail::numa_placement_layout {

constexpr std::size_t kAffinityBytes = AffinityMask::kWords * sizeof(std::uint64_t);
// The node identifier is one byte, but the mask's eight-byte alignment
// pads the pair out to a whole word before the mask begins.
constexpr std::size_t kPlacementBytes = 8 + kAffinityBytes;

static_assert(sizeof(NumaPlacement<int>) >= sizeof(int) + kAffinityBytes + 1);
static_assert(sizeof(NumaPlacement<double>) >= sizeof(double) + kAffinityBytes + 1);
static_assert(sizeof(NumaPlacement<char>) >= sizeof(char) + kAffinityBytes + 1);

static_assert(sizeof(NumaPlacement<std::uint64_t>) == 8 + kPlacementBytes,
              "NumaPlacement<uint64_t> is an 8-byte value followed by a grade of "
              "8 bytes plus AffinityMask::kWords words.  Widening the mask means "
              "updating this assertion.");

}  // namespace detail::numa_placement_layout

namespace detail::numa_placement_self_test {

using NumaPlacementInt = NumaPlacement<int>;
using NumaPlacementDbl = NumaPlacement<double>;

inline constexpr NumaPlacementInt p_default{};
static_assert(p_default.peek() == 0);
static_assert(p_default.numa_node() == NumaNodeId::None);
static_assert(p_default.affinity() == AffinityMask{0});

inline constexpr NumaPlacementInt p_explicit{42, NumaNodeId{2}, AffinityMask{0b1100}};
static_assert(p_explicit.peek() == 42);
static_assert(p_explicit.numa_node() == NumaNodeId{2});
static_assert(p_explicit.affinity() == AffinityMask{0b1100});

inline constexpr NumaPlacementInt p_in_place{std::in_place, NumaNodeId{1}, AffinityMask{0b11}, 7};
static_assert(p_in_place.peek() == 7);
static_assert(p_in_place.numa_node() == NumaNodeId{1});

inline constexpr NumaPlacementInt p_anywhere = NumaPlacementInt::anywhere(99);
static_assert(p_anywhere.peek() == 99);
static_assert(p_anywhere.numa_node() == NumaNodeId::Any);
static_assert(p_anywhere.affinity() == AffinityLattice::top());

inline constexpr NumaPlacementInt p_pinned = NumaPlacementInt::pinned(11, NumaNodeId{2}, /*core=*/3);
static_assert(p_pinned.peek() == 11);
static_assert(p_pinned.numa_node() == NumaNodeId{2});
static_assert(p_pinned.affinity() == AffinityMask::single(3));

[[nodiscard]] consteval bool combine_max_same_node() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b001}};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b010}};
    auto c = a.combine_max(b);
    return c.numa_node() == NumaNodeId{2} && c.affinity() == AffinityMask{0b011} && c.peek() == 42;
}
static_assert(combine_max_same_node());

[[nodiscard]] consteval bool combine_max_sibling_nodes() noexcept {
    NumaPlacementInt a{42, NumaNodeId{0}, AffinityMask{0b001}};
    NumaPlacementInt b{42, NumaNodeId{1}, AffinityMask{0b010}};
    auto c = a.combine_max(b);
    return c.numa_node() == NumaNodeId::Any && c.affinity() == AffinityMask{0b011};
}
static_assert(combine_max_sibling_nodes());

[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b1100}};
    auto c = a.combine_max(a);
    return c.numa_node() == NumaNodeId{2} && c.affinity() == AffinityMask{0b1100};
}
static_assert(combine_max_idempotent());

[[nodiscard]] consteval bool admits_within_threshold() noexcept {
    NumaPlacementInt v{42, NumaNodeId{2}, AffinityMask{0b1100}};
    return v.admits(NumaNodeId{2}, AffinityMask{0b0100}) && v.admits(NumaNodeId::None, AffinityMask{0b1100})
        && !v.admits(NumaNodeId{3}, AffinityMask{0b0100}) && !v.admits(NumaNodeId{2}, AffinityMask{0b0010});
}
static_assert(admits_within_threshold());

static_assert(NumaPlacementInt::anywhere(7).admits(NumaNodeId{42}, AffinityMask::single(7)));

static_assert(NumaPlacementInt{}.admits(NumaNodeId::None, AffinityMask{0}));
static_assert(!NumaPlacementInt{}.admits(NumaNodeId{0}, AffinityMask::single(0)));

static_assert(NumaPlacementInt::value_type_name().ends_with("int"));
static_assert(NumaPlacementInt::lattice_name().size() > 0);

template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, NumaNodeId{0}, AffinityMask{0b01}};
    W b{y, NumaNodeId{1}, AffinityMask{0b10}};
    a.swap(b);
    return a.peek() == y && b.peek() == x && a.numa_node() == NumaNodeId{1} && b.affinity() == AffinityMask{0b01};
}
static_assert(swap_exchanges_within<NumaPlacementInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    NumaPlacementInt a{10, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt b{20, NumaNodeId{1}, AffinityMask{0b10}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10 && a.numa_node() == NumaNodeId{1} && b.affinity() == AffinityMask{0b01};
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    NumaPlacementInt a{10, NumaNodeId{2}, AffinityMask{0b1100}};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.numa_node() == NumaNodeId{2};
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_and_placement() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacementInt c{43, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacementInt d{42, NumaNodeId{3}, AffinityMask{0b11}};
    NumaPlacementInt e{42, NumaNodeId{2}, AffinityMask{0b10}};
    return (a == b) && !(a == c) && !(a == d) && !(a == e);
}
static_assert(equality_compares_value_and_placement());

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

static_assert(!std::is_copy_constructible_v<NumaPlacement<MoveOnlyT>>);
static_assert(std::is_move_constructible_v<NumaPlacement<MoveOnlyT>>);

[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    NumaPlacement<MoveOnlyT> a{MoveOnlyT{42}, NumaNodeId{2}, AffinityMask{0b01}};
    NumaPlacement<MoveOnlyT> b{MoveOnlyT{99}, NumaNodeId{2}, AffinityMask{0b10}};
    auto c = std::move(a).combine_max(b);
    return c.numa_node() == NumaNodeId{2} && c.affinity() == AffinityMask{0b11} && c.peek().v == 42;
}
static_assert(combine_max_works_for_move_only());

template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert(can_combine_max_lvalue<NumaPlacementInt>);
static_assert(can_combine_max_rvalue<NumaPlacementInt>);
static_assert(!can_combine_max_lvalue<NumaPlacement<MoveOnlyT>>);
static_assert(can_combine_max_rvalue<NumaPlacement<MoveOnlyT>>);

static_assert(NumaPlacementInt::value_type_name().size() > 0);
static_assert(NumaPlacementInt::lattice_name().size() > 0);

inline void runtime_smoke_test() {
    NumaPlacementInt a{};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b1100}};
    NumaPlacementInt c{std::in_place, NumaNodeId{1}, AffinityMask{0b11}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto nb = b.numa_node();
    [[maybe_unused]] auto ab = b.affinity();

    NumaPlacementInt anyw = NumaPlacementInt::anywhere(99);
    if (anyw.numa_node() != NumaNodeId::Any) std::abort();

    NumaPlacementInt pin = NumaPlacementInt::pinned(11, NumaNodeId{3}, 5);
    if (pin.numa_node() != NumaNodeId{3}) std::abort();
    if (pin.affinity() != AffinityMask::single(5)) std::abort();

    NumaPlacementInt mutable_b{10, NumaNodeId{0}, AffinityMask{0b1}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    NumaPlacementInt sx{1, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt sy{2, NumaNodeId{1}, AffinityMask{0b10}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    NumaPlacementInt left{42, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt right{42, NumaNodeId{1}, AffinityMask{0b10}};
    auto joined = left.combine_max(right);
    if (joined.numa_node() != NumaNodeId::Any) std::abort();
    if (joined.affinity() != AffinityMask{0b11}) std::abort();

    NumaPlacementInt task{42, NumaNodeId{2}, AffinityMask{0b1100}};
    if (!task.admits(NumaNodeId{2}, AffinityMask::single(2))) std::abort();
    if (task.admits(NumaNodeId{3}, AffinityMask::single(2))) std::abort();

    NumaPlacementInt eq_a{42, NumaNodeId{0}, AffinityMask{0b1}};
    NumaPlacementInt eq_b{42, NumaNodeId{0}, AffinityMask{0b1}};
    if (!(eq_a == eq_b)) std::abort();

    [[maybe_unused]] auto pair = b.placement();
    if (pair.first != NumaNodeId{2}) std::abort();
    if (pair.second != AffinityMask{0b1100}) std::abort();

    NumaPlacementInt orig{55, NumaNodeId{0}, AffinityMask{0b1}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::numa_placement_self_test

}  // namespace crucible::safety
