#pragma once

// A value paired with the two counters that identify the cluster state
// it was produced at: a fleet-wide epoch and a per-node generation.
//
// The only composition is a pointwise join, which keeps the fresher of
// two views.  Epochs and generations do not add, so there is no sum.
//
// Nothing here stops a caller building an older pair after a newer one.
// Forward progress is a property of where the pair comes from: an epoch
// is published only by a committed membership change, and a node
// advances its own generation only when it restarts.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/EpochLattice.h>
#include <crucible/algebra/lattices/GenerationLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::Epoch;
using ::crucible::algebra::lattices::EpochLattice;
using ::crucible::algebra::lattices::Generation;
using ::crucible::algebra::lattices::GenerationLattice;

template <typename T>
class [[nodiscard]] EpochVersioned {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<EpochLattice, GenerationLattice>;
    using version_t = typename lattice_type::element_type;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr version_t pack(Epoch ep, Generation gen) noexcept { return version_t{ep, gen}; }

public:
    constexpr EpochVersioned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    constexpr EpochVersioned(T value, Epoch ep, Generation gen) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(ep, gen)} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr EpochVersioned(std::in_place_t, Epoch ep, Generation gen,
                             Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                      && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(ep, gen)} {}

    [[nodiscard]] static constexpr EpochVersioned
    at_genesis(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return EpochVersioned{std::move(value), Epoch{0}, Generation{0}};
    }

    constexpr EpochVersioned(const EpochVersioned&) = default;
    constexpr EpochVersioned(EpochVersioned&&) = default;
    constexpr EpochVersioned& operator=(const EpochVersioned&) = default;
    constexpr EpochVersioned& operator=(EpochVersioned&&) = default;
    ~EpochVersioned() = default;

    [[nodiscard]] friend constexpr bool operator==(EpochVersioned const& a,
                                                   EpochVersioned const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.epoch() == b.epoch() && a.generation() == b.generation();
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

    [[nodiscard]] constexpr Epoch epoch() const noexcept { return impl_.grade().first; }

    [[nodiscard]] constexpr Generation generation() const noexcept { return impl_.grade().second; }

    [[nodiscard]] constexpr version_t version() const noexcept { return impl_.grade(); }

    constexpr void swap(EpochVersioned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(EpochVersioned& a, EpochVersioned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    [[nodiscard]] constexpr EpochVersioned
    combine_max(EpochVersioned const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return EpochVersioned{this->peek(), EpochLattice::join(this->epoch(), other.epoch()),
                              GenerationLattice::join(this->generation(), other.generation())};
    }

    [[nodiscard]] constexpr EpochVersioned
    combine_max(EpochVersioned const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        Epoch joined_epoch = EpochLattice::join(this->epoch(), other.epoch());
        Generation joined_gen = GenerationLattice::join(this->generation(), other.generation());
        return EpochVersioned{std::move(impl_).consume(), joined_epoch, joined_gen};
    }

    [[nodiscard]] constexpr bool is_at_least(Epoch min_epoch, Generation min_gen) const noexcept {
        return EpochLattice::leq(min_epoch, this->epoch()) && GenerationLattice::leq(min_gen, this->generation());
    }
};

namespace detail::epoch_versioned_layout {

static_assert(sizeof(EpochVersioned<int>) >= sizeof(int) + 16);
static_assert(sizeof(EpochVersioned<double>) >= sizeof(double) + 16);
static_assert(sizeof(EpochVersioned<char>) >= sizeof(char) + 16);

static_assert(sizeof(EpochVersioned<std::uint64_t>) == 24,
              "EpochVersioned<uint64_t> is an 8-byte value plus a 16-byte grade.  "
              "A different size means the product element no longer holds exactly "
              "two 64-bit counters.");

}  // namespace detail::epoch_versioned_layout

static_assert(!std::is_same_v<Epoch, Generation>, "Epoch and Generation are structurally distinct C++ types even "
                                                  "though both wrap a 64-bit counter.  Collapsing them into one "
                                                  "alias removes the fence that stops the two axes being passed "
                                                  "in the wrong order.");

namespace detail::epoch_versioned_self_test {

using EpochVersionedInt = EpochVersioned<int>;
using EpochVersionedDbl = EpochVersioned<double>;

inline constexpr EpochVersionedInt v_default{};
static_assert(v_default.peek() == 0);
static_assert(v_default.epoch() == Epoch{0});
static_assert(v_default.generation() == Generation{0});

inline constexpr EpochVersionedInt v_explicit{42, Epoch{5}, Generation{2}};
static_assert(v_explicit.peek() == 42);
static_assert(v_explicit.epoch() == Epoch{5});
static_assert(v_explicit.generation() == Generation{2});

inline constexpr EpochVersionedInt v_in_place{std::in_place, Epoch{3}, Generation{1}, 7};
static_assert(v_in_place.peek() == 7);
static_assert(v_in_place.epoch() == Epoch{3});
static_assert(v_in_place.generation() == Generation{1});

inline constexpr EpochVersionedInt v_genesis = EpochVersionedInt::at_genesis(99);
static_assert(v_genesis.peek() == 99);
static_assert(v_genesis.epoch() == Epoch{0});
static_assert(v_genesis.generation() == Generation{0});

[[nodiscard]] consteval bool combine_max_takes_pointwise_max() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{1}};
    EpochVersionedInt b{42, Epoch{3}, Generation{4}};
    auto c = a.combine_max(b);
    return c.epoch() == Epoch{5} && c.generation() == Generation{4} && c.peek() == 42;
}
static_assert(combine_max_takes_pointwise_max());

[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{2}};
    auto c = a.combine_max(a);
    return c.epoch() == Epoch{5} && c.generation() == Generation{2};
}
static_assert(combine_max_idempotent());

[[nodiscard]] consteval bool is_at_least_passes_within_threshold() noexcept {
    EpochVersionedInt v{42, Epoch{5}, Generation{2}};
    return v.is_at_least(Epoch{5}, Generation{2}) && v.is_at_least(Epoch{4}, Generation{1})
        && !v.is_at_least(Epoch{6}, Generation{2}) && !v.is_at_least(Epoch{5}, Generation{3});
}
static_assert(is_at_least_passes_within_threshold());

static_assert(!EpochVersionedInt::at_genesis(7).is_at_least(Epoch{1}, Generation{0}));

static_assert(EpochVersionedInt{7, EpochLattice::top(), GenerationLattice::top()}.is_at_least(Epoch{1u << 30},
                                                                                              Generation{1u << 30}));

static_assert(EpochVersionedInt::value_type_name().ends_with("int"));
static_assert(EpochVersionedInt::lattice_name().size() > 0);

template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, Epoch{1}, Generation{2}};
    W b{y, Epoch{3}, Generation{4}};
    a.swap(b);
    return a.peek() == y && b.peek() == x && a.epoch() == Epoch{3} && b.generation() == Generation{2};
}
static_assert(swap_exchanges_within<EpochVersionedInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    EpochVersionedInt a{10, Epoch{1}, Generation{2}};
    EpochVersionedInt b{20, Epoch{3}, Generation{4}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10 && a.epoch() == Epoch{3} && b.generation() == Generation{2};
}
static_assert(free_swap_works());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    EpochVersionedInt a{10, Epoch{5}, Generation{2}};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.epoch() == Epoch{5};
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_and_version() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{2}};
    EpochVersionedInt b{42, Epoch{5}, Generation{2}};
    EpochVersionedInt c{43, Epoch{5}, Generation{2}};
    EpochVersionedInt d{42, Epoch{6}, Generation{2}};
    EpochVersionedInt e{42, Epoch{5}, Generation{3}};
    return (a == b) && !(a == c) && !(a == d) && !(a == e);
}
static_assert(equality_compares_value_and_version());

struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

static_assert(!std::is_copy_constructible_v<EpochVersioned<MoveOnlyT>>,
              "EpochVersioned<T> inherits T's deleted copy constructor.");
static_assert(std::is_move_constructible_v<EpochVersioned<MoveOnlyT>>);

[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    EpochVersioned<MoveOnlyT> a{MoveOnlyT{42}, Epoch{1}, Generation{1}};
    EpochVersioned<MoveOnlyT> b{MoveOnlyT{99}, Epoch{5}, Generation{0}};
    auto c = std::move(a).combine_max(b);
    return c.epoch() == Epoch{5} && c.generation() == Generation{1} && c.peek().v == 42;
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
static_assert(can_combine_max_lvalue<EpochVersionedInt>);
static_assert(can_combine_max_rvalue<EpochVersionedInt>);
static_assert(!can_combine_max_lvalue<EpochVersioned<MoveOnlyT>>,
              "The const-lvalue combine_max is rejected for a move-only T.");
static_assert(can_combine_max_rvalue<EpochVersioned<MoveOnlyT>>);

static_assert(EpochVersionedInt::value_type_name().size() > 0);
static_assert(EpochVersionedInt::lattice_name().size() > 0);

inline void runtime_smoke_test() {
    EpochVersionedInt a{};
    EpochVersionedInt b{42, Epoch{5}, Generation{2}};
    EpochVersionedInt c{std::in_place, Epoch{3}, Generation{1}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto eb = b.epoch();
    [[maybe_unused]] auto gb = b.generation();

    EpochVersionedInt d = EpochVersionedInt::at_genesis(99);
    if (d.epoch() != Epoch{0}) std::abort();

    EpochVersionedInt mutable_b{10, Epoch{1}, Generation{1}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    EpochVersionedInt sx{1, Epoch{1}, Generation{1}};
    EpochVersionedInt sy{2, Epoch{2}, Generation{2}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    EpochVersionedInt left{42, Epoch{5}, Generation{1}};
    EpochVersionedInt right{42, Epoch{3}, Generation{4}};
    auto joined = left.combine_max(right);
    if (joined.epoch() != Epoch{5}) std::abort();
    if (joined.generation() != Generation{4}) std::abort();

    EpochVersionedInt observed{42, Epoch{5}, Generation{2}};
    if (!observed.is_at_least(Epoch{4}, Generation{1})) std::abort();
    if (observed.is_at_least(Epoch{6}, Generation{0})) std::abort();

    EpochVersionedInt eq_a{42, Epoch{1}, Generation{1}};
    EpochVersionedInt eq_b{42, Epoch{1}, Generation{1}};
    if (!(eq_a == eq_b)) std::abort();

    [[maybe_unused]] auto version_pair = b.version();
    if (version_pair.first != Epoch{5}) std::abort();
    if (version_pair.second != Generation{2}) std::abort();

    EpochVersionedInt orig{55, Epoch{1}, Generation{1}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::epoch_versioned_self_test

}  // namespace crucible::safety
