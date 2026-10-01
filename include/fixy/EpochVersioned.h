#pragma once

// A value paired with the two counters that identify the cluster state
// it was produced at: the fleet-wide epoch and the per-node generation.
//
// The version is a claim about the payload.  A gate that asks for at
// least version v admits the value when its version sits at or above v,
// so a version that is too high makes a stale payload read as fresh.
// The claim therefore does not come from the producer.  It comes from a
// VersionSource, the one owner of the version counters of a program:
//
//   - A VersionSource is minted only with a context that owns Init, the
//     capability of process startup, so a producer running on a
//     background or foreground thread cannot make one.
//   - Its version only rises.  The owner advances the epoch on a committed
//     membership change and the generation on a restart, and adopts a
//     newer version that it learns from consensus or from storage.
//   - It hands out a VersionStamp, a proof type that no caller can build:
//     its constructor is private, and its copy is user-provided, so
//     neither std::bit_cast nor a lifetime start over bytes makes one.
//   - Every stamp names a version at or below the version of its source
//     when the stamp was taken.  stamp() gives the current version, and
//     stamp_received() gives the version of a value that came from a peer,
//     and refuses one that the source has not reached.  An old stamp is a
//     weaker claim, never a false one.
//
// EpochVersioned is built only from a stamp, or at the genesis version,
// which is the weakest claim.  The rules of the wrapper follow:
//
//   - There is no default constructor.  A value that was never produced
//     at a version has no version to report, so the type does not invent
//     one.  at_genesis() states the genesis version explicitly.
//   - There is no peek_mut().  Replacing the payload under the current
//     version would let an older payload carry a newer version.
//   - There is no combine that joins two versions and keeps one payload.
//     select_fresher() below returns the operand whose own version is the
//     higher one, together with that version, and refuses an incomparable
//     pair.  Two equal versions are one event only when the two payloads
//     are the same, and the payloads are compared by the equality derived
//     from their members (SelfContained.h).  A hand-written operator== is
//     refused, because one that holds for values a reader tells apart
//     would hide a conflict at one version.
//
// The grade is the order dual of the version order.  Graded reads up as
// the weaker claim, and an older version is the weaker claim, so the
// older version sits higher.  Under the dual, the substrate's own weaken()
// moves to an older version and its compose() reports the older of two,
// so neither can mark a value fresh.  A reader who needs the counters in
// their numeric order reads epoch() and generation().
//
// Two more doors close on the payload side:
//
//   - A payload that reaches state outside itself, or that a const
//     reference can write, is refused (SelfContained.h).
//   - A move out of a payload that is not trivially copyable leaves the
//     source at the genesis version.  The moved-from payload holds some
//     unspecified value, and genesis is the weakest claim.
//
// A VersionSource is owned by one thread.  Share its versions by passing
// stamps, which are values.

#include <fixy/GradedFacade.h>
#include <fixy/SelfContained.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Instance.h>

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
using ::foundation::algebra::lattices::EpochBound;
using ::foundation::algebra::lattices::EpochLattice;
using ::foundation::algebra::lattices::Generation;
using ::foundation::algebra::lattices::GenerationBound;
using ::foundation::algebra::lattices::GenerationLattice;

// The pointwise order on (epoch, generation), turned over so that the
// older version is the higher element.  Two versions where each leads on
// a different counter are incomparable.
using EpochVersionLattice =
    ::foundation::algebra::lattices::ProductLattice<::foundation::algebra::lattices::DualLattice<EpochLattice>,
                                                    ::foundation::algebra::lattices::DualLattice<GenerationLattice>>;

// Why select_fresher() declined to choose, or why a source declined to
// stamp.
enum class VersionConflict : std::uint8_t {
    // Each operand leads on one counter, so neither version is at or
    // above the other, and no operand has the version a join reports.
    Incomparable = 1,
    // The two versions are equal and the two payloads are not.  One
    // version names one cluster state, so two different values at the
    // same version mean one of the producers lied or two states shared
    // a version, and neither operand can be preferred.
    Divergent = 2,
    // The version is above the version of the source, so the source
    // cannot vouch for it.  The owner adopts the version first.
    AheadOfSource = 3,
};

class VersionSource;

// A version that a VersionSource vouches for.  No caller can build one.
class VersionStamp {
public:
    VersionStamp() = delete("a stamp comes only from a VersionSource");

    // User-provided, so that the stamp is neither trivially copyable nor
    // implicit-lifetime, and no route builds one from bytes.
    constexpr VersionStamp(VersionStamp const& other) noexcept : epoch_{other.epoch_}, generation_{other.generation_} {}
    constexpr VersionStamp& operator=(VersionStamp const& other) noexcept {
        epoch_ = other.epoch_;
        generation_ = other.generation_;
        return *this;
    }
    ~VersionStamp() = default;

    [[nodiscard]] constexpr Epoch epoch() const noexcept { return epoch_; }
    [[nodiscard]] constexpr Generation generation() const noexcept { return generation_; }

private:
    friend class VersionSource;
    constexpr VersionStamp(Epoch epoch, Generation generation) noexcept : epoch_{epoch}, generation_{generation} {}

    Epoch epoch_;
    Generation generation_;
};

template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr VersionSource mint_version_source(Ctx const& ctx) noexcept;

// The owner of the version counters.  Its address is its identity, so it
// neither copies nor moves.
class [[nodiscard]] VersionSource {
public:
    VersionSource(VersionSource const&) = delete("the source is the one owner of its version; a copy would be a "
                                                 "second owner that advances on its own");
    VersionSource(VersionSource&&) = delete("the source is the one owner of its version, so it stays in place");
    VersionSource& operator=(VersionSource const&) = delete("the version of a source only rises");
    VersionSource& operator=(VersionSource&&) = delete("the version of a source only rises");
    ~VersionSource() = default;

    // The current version.
    [[nodiscard]] constexpr VersionStamp stamp() const noexcept { return VersionStamp{epoch_, generation_}; }

    // The version of a value that came from a peer.  A version above the
    // source's own is refused: a node vouches only for what it has reached.
    [[nodiscard]] constexpr std::expected<VersionStamp, VersionConflict>
    stamp_received(Epoch epoch, Generation generation) const noexcept {
        if (!EpochLattice::leq(epoch, epoch_) || !GenerationLattice::leq(generation, generation_)) {
            return std::unexpected(VersionConflict::AheadOfSource);
        }
        return VersionStamp{epoch, generation};
    }

    // A committed membership change.
    constexpr VersionStamp advance_epoch() noexcept {
        epoch_ = EpochLattice::successor(epoch_);
        return stamp();
    }

    // A restart of this node.
    constexpr VersionStamp advance_generation() noexcept {
        generation_ = GenerationLattice::successor(generation_);
        return stamp();
    }

    // A version learned from consensus or read back from storage.  Each
    // counter rises to the larger of the two and never falls.
    constexpr VersionStamp adopt(Epoch epoch, Generation generation) noexcept {
        epoch_ = EpochLattice::join(epoch_, epoch);
        generation_ = GenerationLattice::join(generation_, generation);
        return stamp();
    }

private:
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
    friend constexpr VersionSource mint_version_source(Ctx const& ctx) noexcept;

    constexpr VersionSource() noexcept = default;

    Epoch epoch_{};
    Generation generation_{};
};

// A source at the genesis version.  The context must own Init, which only
// process startup and the test witness hold.
template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr VersionSource mint_version_source(Ctx const&) noexcept {
    return VersionSource{};
}

template <SelfContained T>
class [[nodiscard]] EpochVersioned
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute, EpochVersionLattice, T> {
public:
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, EpochVersionLattice, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;
    using version_t = typename lattice_type::element_type;

private:
    graded_type impl_;

    using key_ = ::foundation::algebra::grade_key<EpochVersioned>;

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
        std::construct_at(&impl_, key_{}, std::move(left_behind), genesis_());
    }

    constexpr EpochVersioned(T value, version_t version) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(value), version} {}

public:
    EpochVersioned() = delete("a value that was never produced at a version has no version to report; "
                              "state one, or use at_genesis()");

    constexpr EpochVersioned(T value, VersionStamp const& stamp) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(value), version_t{stamp.epoch(), stamp.generation()}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr EpochVersioned(std::in_place_t, VersionStamp const& stamp,
                             Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                      && std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, T(std::forward<Args>(args)...), version_t{stamp.epoch(), stamp.generation()}} {}

    [[nodiscard]] static constexpr EpochVersioned
    at_genesis(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return EpochVersioned{std::move(value), genesis_()};
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

    // Two values are equal when their versions are equal and their payloads
    // are equal by their members.
    [[nodiscard]] friend constexpr bool operator==(EpochVersioned const& a, EpochVersioned const& b) noexcept
        requires EqualityByMembers<T>
    {
        return a.version() == b.version() && equal_by_members(a.peek(), b.peek());
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
    // required ones.
    [[nodiscard]] constexpr bool is_at_least(EpochBound min_epoch, GenerationBound min_gen) const noexcept {
        return EpochLattice::is_at_least(epoch(), min_epoch) && GenerationLattice::is_at_least(generation(), min_gen);
    }
};

// The operand whose own version is at or above the other one, with its
// payload and that version.  Equal versions with payloads equal by their
// members are one event, and the left operand stands for it.  Equal
// versions with different payloads are Divergent.  An incomparable pair
// has no fresher operand, so the result is Incomparable.
//
// In the dual order, "a is at or above b" is leq(a, b).
template <typename T>
    requires std::copy_constructible<T> && EqualityByMembers<T>
[[nodiscard]] constexpr std::expected<EpochVersioned<T>, VersionConflict>
select_fresher(EpochVersioned<T> const& a,
               EpochVersioned<T> const& b) noexcept(std::is_nothrow_copy_constructible_v<T>) {
    using L = EpochVersionLattice;
    bool const a_at_or_above_b = L::leq(a.version(), b.version());
    bool const b_at_or_above_a = L::leq(b.version(), a.version());
    if (a_at_or_above_b && b_at_or_above_a) {
        if (!equal_by_members(a.peek(), b.peek())) return std::unexpected(VersionConflict::Divergent);
        return a;
    }
    if (a_at_or_above_b) return a;
    if (b_at_or_above_a) return b;
    return std::unexpected(VersionConflict::Incomparable);
}

template <EqualityByMembers T>
[[nodiscard]] constexpr std::expected<EpochVersioned<T>, VersionConflict>
select_fresher(EpochVersioned<T>&& a, EpochVersioned<T>&& b) noexcept(std::is_nothrow_move_constructible_v<T>) {
    using L = EpochVersionLattice;
    bool const a_at_or_above_b = L::leq(a.version(), b.version());
    bool const b_at_or_above_a = L::leq(b.version(), a.version());
    if (a_at_or_above_b && b_at_or_above_a) {
        if (!equal_by_members(a.peek(), b.peek())) return std::unexpected(VersionConflict::Divergent);
        return std::move(a);
    }
    if (a_at_or_above_b) return std::move(a);
    if (b_at_or_above_a) return std::move(b);
    return std::unexpected(VersionConflict::Incomparable);
}

// True when T, with its references and qualifiers removed, is an
// EpochVersioned.  One reflection query answers it, so no primary and
// specialization pair has to be kept in step with the class.
template <typename T>
concept IsEpochVersioned = ::foundation::reflect::IsInstanceOf<T, ^^EpochVersioned>;

}  // namespace fixy
