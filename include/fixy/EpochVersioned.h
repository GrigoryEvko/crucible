#pragma once

// A value paired with the two counters that identify the cluster state
// it was produced at: the fleet-wide epoch and the per-node generation.
//
// The version is a claim about the payload.  A gate that asks for at
// least version v admits the value when its version sits at or above v,
// so a version that is too high makes a stale payload read as fresh.
// Every door that could raise the version without new evidence is
// absent here, and each absence is a design choice:
//
//   - There is no default constructor.  A value that was never produced
//     at a version has no version to report, so the type does not invent
//     one.  at_genesis() states the genesis version explicitly.
//   - There is no peek_mut().  Replacing the payload under the current
//     version would let an older payload carry a newer version.
//   - There is no combine that joins two versions and keeps one payload.
//     The old wrapper had one: it kept the left payload and stored the
//     join of both versions, so combining an old payload with a newer
//     version produced an old payload marked with the newer version.
//     select_fresher() below returns the operand whose own version is
//     the higher one, together with that version, and refuses an
//     incomparable pair.
//
// The grade is the order dual of the version order.  Graded reads up as
// the weaker claim, and an older version is the weaker claim, so the
// older version sits higher.  The old combine was an instance of the
// opposite orientation: its join took the newer version.  Under the dual,
// the substrate's own weaken() moves to an older version and its
// compose() reports the older of two, so neither can mark a value fresh.
// A reader who needs the counters in their numeric order reads epoch()
// and generation(), which are the counters themselves.
//
// Three more doors close on the payload side:
//
//   - A payload that is a reference is refused.  The referent can change
//     after construction, and the version would then describe a value it
//     never saw.  A pointer or a view is admitted, and the version then
//     covers the handle and not the referent.
//   - A payload that holds a mutable member anywhere in its by-value
//     structure is refused.  peek() returns a const reference, and a
//     mutable member is writable through one, so the payload could change
//     under the version with no cast and no warning.
//   - A move out of a payload that is not trivially copyable leaves the
//     source at the genesis version.  The moved-from payload holds some
//     unspecified value, and genesis is the weakest claim, so a gate that
//     asks for any real version refuses it.  A trivially copyable payload
//     is copied by a move, so its source keeps a true claim.
//
// Nothing here stops a caller who builds an older version after a newer
// one.  Forward progress is a property of where the version comes from:
// the fleet publishes an epoch only through a committed membership
// change, and a node advances its own generation only when it restarts.
//
// Old spelling: include/crucible/safety/EpochVersioned.h, and the
// detection surface of include/crucible/safety/IsEpochVersioned.h.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::Epoch;
using ::foundation::algebra::lattices::EpochLattice;
using ::foundation::algebra::lattices::Generation;
using ::foundation::algebra::lattices::GenerationLattice;

// The pointwise order on (epoch, generation), turned over so that the
// older version is the higher element.  Two versions where each leads on
// a different counter are incomparable.
using EpochVersionLattice =
    ::foundation::algebra::lattices::ProductLattice<::foundation::algebra::lattices::DualLattice<EpochLattice>,
                                                    ::foundation::algebra::lattices::DualLattice<GenerationLattice>>;

// Why select_fresher() declined to choose.
enum class VersionConflict : std::uint8_t {
    // Each operand leads on one counter, so neither version is at or
    // above the other, and no operand has the version a join reports.
    Incomparable = 1,
    // The two versions are equal and the two payloads are not.  One
    // version names one cluster state, so two different values at the
    // same version mean one of the producers lied or two states shared
    // a version, and neither operand can be preferred.
    Divergent = 2,
};

namespace detail::epoch_versioned {

// True for a class whose own non-static data members include a mutable
// one.  The walk reads members only where the node says it may, so the
// check instantiates nothing the payload did not already need.
inline constexpr auto declares_mutable_member = [](::foundation::reflect::TypeNode node) consteval {
    if (!node.may_read_members) return false;
    std::meta::info const type = ::foundation::reflect::bare_type(node.type);
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return false;
    for (std::meta::info const member :
         std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
        if (std::meta::is_mutable_member(member)) return true;
    }
    return false;
};

}  // namespace detail::epoch_versioned

// A payload the version can describe: an object type, whose structure
// holds no member that a const reference can write.
template <typename T>
concept VersionablePayload =
    std::is_object_v<T>
    && !::foundation::reflect::any_component_satisfies<detail::epoch_versioned::declares_mutable_member>(^^T);

template <VersionablePayload T>
class [[nodiscard]] EpochVersioned
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute, EpochVersionLattice, T> {
public:
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, EpochVersionLattice, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;
    using version_t = typename lattice_type::element_type;

private:
    graded_type impl_;

    // The genesis version, which is the weakest claim.  It is the top of
    // the dual order and the bottom of the numeric one.
    [[nodiscard]] static constexpr version_t genesis_() noexcept {
        return version_t{EpochLattice::bottom(), GenerationLattice::bottom()};
    }

    // Drops the claim of a moved-from source to genesis.  The payload is
    // moved out and back in, because Graded sets its grade only at
    // construction.
    constexpr void relinquish_version_() noexcept(std::is_nothrow_move_constructible_v<T>) {
        T left_behind = std::move(impl_).consume();
        std::destroy_at(&impl_);
        std::construct_at(&impl_, std::move(left_behind), genesis_());
    }

public:
    EpochVersioned() = delete("a value that was never produced at a version has no version to report; "
                              "state one, or use at_genesis()");

    constexpr EpochVersioned(T value, Epoch ep, Generation gen) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), version_t{ep, gen}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr EpochVersioned(std::in_place_t, Epoch ep, Generation gen,
                             Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                      && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), version_t{ep, gen}} {}

    [[nodiscard]] static constexpr EpochVersioned
    at_genesis(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return EpochVersioned{std::move(value), EpochLattice::bottom(), GenerationLattice::bottom()};
    }

    // A copy is a replay, not a duplication: the version records when the
    // payload was produced, and two values with the same payload and the
    // same version are the same event.
    constexpr EpochVersioned(const EpochVersioned&) = default;
    constexpr EpochVersioned& operator=(const EpochVersioned&) = default;

    // A move of a trivially copyable payload is a copy, and the source
    // keeps a claim that is still true.
    constexpr EpochVersioned(EpochVersioned&&)
        requires std::is_trivially_copyable_v<T>
    = default;
    constexpr EpochVersioned& operator=(EpochVersioned&&)
        requires std::is_trivially_copyable_v<T>
    = default;

    // Any other move leaves the source at genesis.
    constexpr EpochVersioned(EpochVersioned&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!std::is_trivially_copyable_v<T>)
        : impl_{std::move(other.impl_)} {
        other.relinquish_version_();
    }
    constexpr EpochVersioned& operator=(EpochVersioned&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                                                         && std::is_nothrow_move_assignable_v<T>)
        requires(!std::is_trivially_copyable_v<T> && std::is_move_assignable_v<T>)
    {
        if (this != &other) {
            impl_ = std::move(other.impl_);
            other.relinquish_version_();
        }
        return *this;
    }

    ~EpochVersioned() = default;

    [[nodiscard]] friend constexpr bool operator==(EpochVersioned const& a,
                                                   EpochVersioned const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek() && a.version() == b.version();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr Epoch epoch() const noexcept { return impl_.grade().first; }
    [[nodiscard]] constexpr Generation generation() const noexcept { return impl_.grade().second; }
    [[nodiscard]] constexpr version_t version() const noexcept { return impl_.grade(); }

    constexpr void swap(EpochVersioned& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(EpochVersioned& a, EpochVersioned& b) noexcept(std::is_nothrow_swappable_v<T>) {
        a.swap(b);
    }

    // The admission query.  Both counters must be at or above the
    // required ones, which in the dual order means the version sits at or
    // below the requirement.
    [[nodiscard]] constexpr bool is_at_least(Epoch min_epoch, Generation min_gen) const noexcept {
        return lattice_type::leq(version(), version_t{min_epoch, min_gen});
    }
};

namespace detail::epoch_versioned {

// Refuses two equal versions whose payloads differ, when the payload can
// say so.  A payload with no equality cannot, and the left operand wins.
template <typename T>
[[nodiscard]] constexpr bool payloads_diverge(EpochVersioned<T> const& a, EpochVersioned<T> const& b) {
    if constexpr (std::equality_comparable<T>) {
        return !(a.peek() == b.peek());
    } else {
        return false;
    }
}

}  // namespace detail::epoch_versioned

// The operand whose own version is at or above the other one, with its
// payload and that version.  Equal versions with equal payloads are one
// event, and the left operand stands for it.  Equal versions with
// different payloads are Divergent.  An incomparable pair has no fresher
// operand, so the result is Incomparable.
//
// In the dual order, "a is at or above b" is leq(a, b).
template <typename T>
    requires std::copy_constructible<T>
[[nodiscard]] constexpr std::expected<EpochVersioned<T>, VersionConflict>
select_fresher(EpochVersioned<T> const& a, EpochVersioned<T> const& b) noexcept(
    std::is_nothrow_copy_constructible_v<T>) {
    using L = EpochVersionLattice;
    bool const a_at_or_above_b = L::leq(a.version(), b.version());
    bool const b_at_or_above_a = L::leq(b.version(), a.version());
    if (a_at_or_above_b && b_at_or_above_a) {
        if (detail::epoch_versioned::payloads_diverge(a, b)) return std::unexpected(VersionConflict::Divergent);
        return a;
    }
    if (a_at_or_above_b) return a;
    if (b_at_or_above_a) return b;
    return std::unexpected(VersionConflict::Incomparable);
}

template <typename T>
[[nodiscard]] constexpr std::expected<EpochVersioned<T>, VersionConflict>
select_fresher(EpochVersioned<T>&& a, EpochVersioned<T>&& b) noexcept(std::is_nothrow_move_constructible_v<T>) {
    using L = EpochVersionLattice;
    bool const a_at_or_above_b = L::leq(a.version(), b.version());
    bool const b_at_or_above_a = L::leq(b.version(), a.version());
    if (a_at_or_above_b && b_at_or_above_a) {
        if (detail::epoch_versioned::payloads_diverge(a, b)) return std::unexpected(VersionConflict::Divergent);
        return std::move(a);
    }
    if (a_at_or_above_b) return std::move(a);
    if (b_at_or_above_a) return std::move(b);
    return std::unexpected(VersionConflict::Incomparable);
}

// The detection surface of the old IsEpochVersioned.h.  One reflection
// query answers it, so no primary and specialization pair has to be kept
// in step with the class.
template <typename T>
concept IsEpochVersioned = ::foundation::reflect::IsInstanceOf<T, ^^EpochVersioned>;

template <typename T>
inline constexpr bool is_epoch_versioned_v = IsEpochVersioned<T>;

template <typename T>
    requires IsEpochVersioned<T>
using epoch_versioned_value_t = typename std::remove_cvref_t<T>::value_type;

namespace detail::epoch_versioned_self_test {

using EV = EpochVersioned<int>;

static_assert(!std::is_default_constructible_v<EV>, "a version must be stated, never defaulted");
static_assert(std::is_copy_constructible_v<EV>);
static_assert(std::is_trivially_copyable_v<EV>, "a trivially copyable payload keeps a trivially copyable wrapper");
static_assert(!std::is_same_v<Epoch, Generation>);
static_assert(!std::is_constructible_v<EV, int, Generation, Epoch>,
              "the two counters are distinct types, so passing them in the wrong order does not compile");
static_assert(!std::is_constructible_v<EV, int, std::uint64_t, std::uint64_t>,
              "a raw integer is not a version");

// The grade is carried per instance: the payload plus two 64-bit
// counters, and no more for a payload that needs no padding.
static_assert(sizeof(EpochVersioned<std::uint64_t>) == 24);

// The payload side of the discipline.
struct HoldsMutable {
    mutable int cache = 0;
};
struct NestsMutable {
    HoldsMutable inner{};
};
struct DerivesMutable : HoldsMutable {};
struct PointsAtMutable {
    HoldsMutable* target = nullptr;
};
static_assert(VersionablePayload<int>);
static_assert(!VersionablePayload<int&>, "a reference payload is refused");
static_assert(!VersionablePayload<int const&>);
static_assert(!VersionablePayload<HoldsMutable>, "a mutable member is writable through peek()");
static_assert(!VersionablePayload<NestsMutable>, "a nested mutable member is writable through peek()");
static_assert(!VersionablePayload<DerivesMutable>, "an inherited mutable member is writable through peek()");
static_assert(!VersionablePayload<PointsAtMutable>,
              "the walk follows a pointer, and a mutable pointee is refused with the rest");

inline constexpr EV v_explicit{42, Epoch{5}, Generation{2}};
static_assert(v_explicit.peek() == 42);
static_assert(v_explicit.epoch() == Epoch{5});
static_assert(v_explicit.generation() == Generation{2});

inline constexpr EV v_in_place{std::in_place, Epoch{3}, Generation{1}, 7};
static_assert(v_in_place.peek() == 7);
static_assert(v_in_place.version() == EV::version_t{Epoch{3}, Generation{1}});

inline constexpr EV v_genesis = EV::at_genesis(99);
static_assert(v_genesis.epoch() == Epoch{0});
static_assert(v_genesis.generation() == Generation{0});
static_assert(!v_genesis.is_at_least(Epoch{1}, Generation{0}));
static_assert(v_genesis.is_at_least(Epoch{0}, Generation{0}));

static_assert(v_explicit.is_at_least(Epoch{5}, Generation{2}));
static_assert(v_explicit.is_at_least(Epoch{4}, Generation{1}));
static_assert(!v_explicit.is_at_least(Epoch{6}, Generation{2}));
static_assert(!v_explicit.is_at_least(Epoch{5}, Generation{3}));

// The fresher operand wins with its own payload and its own version.
inline constexpr EV v_older{10, Epoch{3}, Generation{1}};
inline constexpr EV v_newer{20, Epoch{5}, Generation{2}};
static_assert(select_fresher(v_older, v_newer)->peek() == 20);
static_assert(select_fresher(v_older, v_newer)->version() == v_newer.version());
static_assert(select_fresher(v_newer, v_older)->peek() == 20);

// Equal versions: one event if the payloads agree, a conflict if not.
inline constexpr EV v_same_a{1, Epoch{4}, Generation{4}};
inline constexpr EV v_same_b{2, Epoch{4}, Generation{4}};
inline constexpr EV v_same_a_again{1, Epoch{4}, Generation{4}};
static_assert(select_fresher(v_same_a, v_same_a_again)->peek() == 1);
static_assert(select_fresher(v_same_a, v_same_b).error() == VersionConflict::Divergent);
static_assert(select_fresher(v_same_b, v_same_a).error() == VersionConflict::Divergent);

// Each operand leads on one counter.  The old combine would have
// reported the version (5, 4) for a payload that has neither half of it.
inline constexpr EV v_epoch_ahead{30, Epoch{5}, Generation{1}};
inline constexpr EV v_gen_ahead{40, Epoch{3}, Generation{4}};
static_assert(!select_fresher(v_epoch_ahead, v_gen_ahead).has_value());
static_assert(select_fresher(v_epoch_ahead, v_gen_ahead).error() == VersionConflict::Incomparable);

// The result never carries a version that its payload was not given.
[[nodiscard]] consteval bool result_version_belongs_to_result_payload() noexcept {
    EV const inputs[] = {v_older, v_newer, v_same_a, v_same_b, v_epoch_ahead, v_gen_ahead, v_genesis};
    for (EV const& a : inputs) {
        for (EV const& b : inputs) {
            auto const r = select_fresher(a, b);
            if (!r) continue;
            bool const is_a = r->peek() == a.peek() && r->version() == a.version();
            bool const is_b = r->peek() == b.peek() && r->version() == b.version();
            if (!is_a && !is_b) return false;
        }
    }
    return true;
}
static_assert(result_version_belongs_to_result_payload());

// The substrate's own operations are sound under the dual order: weaken
// moves to an older version, and compose reports the older of two.
using RawGraded = EV::graded_type;
inline constexpr RawGraded g_newer{20, EV::version_t{Epoch{5}, Generation{2}}};
inline constexpr RawGraded g_older{10, EV::version_t{Epoch{3}, Generation{1}}};
static_assert(g_newer.weaken(EV::version_t{Epoch{3}, Generation{1}}).grade().first == Epoch{3});
static_assert(g_older.compose(g_newer).grade() == EV::version_t{Epoch{3}, Generation{1}},
              "composition keeps the older version, so an old payload never reads as new");
static_assert(g_newer.compose(g_older).grade() == EV::version_t{Epoch{3}, Generation{1}});

struct MoveOnly {
    int v{0};
    constexpr explicit MoveOnly(int x) : v{x} {}
    constexpr MoveOnly(MoveOnly&& other) noexcept : v{other.v} { other.v = -1; }
    constexpr MoveOnly& operator=(MoveOnly&& other) noexcept {
        v = other.v;
        other.v = -1;
        return *this;
    }
    MoveOnly(MoveOnly const&) = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
};

static_assert(!std::is_copy_constructible_v<EpochVersioned<MoveOnly>>);
static_assert(std::is_move_constructible_v<EpochVersioned<MoveOnly>>);

[[nodiscard]] consteval bool select_fresher_moves_a_move_only_payload() noexcept {
    EpochVersioned<MoveOnly> a{MoveOnly{1}, Epoch{1}, Generation{1}};
    EpochVersioned<MoveOnly> b{MoveOnly{2}, Epoch{2}, Generation{2}};
    auto r = select_fresher(std::move(a), std::move(b));
    return r.has_value() && r->peek().v == 2;
}
static_assert(select_fresher_moves_a_move_only_payload());

// A moved-from source drops to genesis, so it no longer passes a gate
// that its old version passed.
[[nodiscard]] consteval bool moved_from_source_claims_genesis() noexcept {
    EpochVersioned<MoveOnly> source{MoveOnly{7}, Epoch{9}, Generation{4}};
    EpochVersioned<MoveOnly> target{std::move(source)};
    EpochVersioned<MoveOnly> assigned{MoveOnly{8}, Epoch{1}, Generation{1}};
    assigned = std::move(target);
    return source.version() == EpochVersioned<MoveOnly>::version_t{Epoch{0}, Generation{0}}
        && !source.is_at_least(Epoch{1}, Generation{0}) && target.version() == source.version()
        && assigned.peek().v == 7 && assigned.is_at_least(Epoch{9}, Generation{4});
}
static_assert(moved_from_source_claims_genesis());

// A self-move keeps the value and its version.
[[nodiscard]] consteval bool self_move_keeps_the_claim() noexcept {
    EpochVersioned<MoveOnly> value{MoveOnly{5}, Epoch{3}, Generation{3}};
    EpochVersioned<MoveOnly>& alias = value;
    value = std::move(alias);
    return value.is_at_least(Epoch{3}, Generation{3});
}
static_assert(self_move_keeps_the_claim());

template <typename L, typename R>
concept can_select_lvalues = requires(L const& a, R const& b) { select_fresher(a, b); };
static_assert(can_select_lvalues<EV, EV>);
static_assert(!can_select_lvalues<EpochVersioned<MoveOnly>, EpochVersioned<MoveOnly>>,
              "the lvalue form copies, so a move-only payload leaves it by the constraint");

struct Lookalike {
    using value_type = int;
    using version_t = int;
};

static_assert(is_epoch_versioned_v<EV>);
static_assert(is_epoch_versioned_v<EV const&>);
static_assert(!is_epoch_versioned_v<int>);
static_assert(!is_epoch_versioned_v<Lookalike>);
static_assert(std::is_same_v<epoch_versioned_value_t<EV const&>, int>);

static_assert(EV::value_type_name().ends_with("int"));
static_assert(EV::lattice_name() == "Product<L1xL2>");

}  // namespace detail::epoch_versioned_self_test

}  // namespace fixy
