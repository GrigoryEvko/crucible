#pragma once

// A value paired with a staleness grade drawn from the natural numbers
// extended with infinity.  The grade is a position in time, not an
// ownership claim, so two instances carrying the same value and grade
// are the same event and copying one is replay rather than duplication.
//
// The grade is a claim about the value: a grade that is too low makes an
// old value read as fresh.  So the claim does not come from the caller.
// It comes from a SequenceSource, the owner of the sequence of one
// producer:
//
//   - A SequenceSource is minted only with a context that owns Init, the
//     capability of process startup.  The producer then owns it on one
//     thread, and advances it at each sample.
//   - It hands out a SequenceStamp, a proof type that no caller can build:
//     its constructor is private, and its copy is user-provided, so
//     neither std::bit_cast nor a lifetime start over bytes makes one.
//   - A stamp holds a position and no age.  The grade of a value is the
//     distance from its stamp to the current position of the source, so a
//     later read of an old stamp gives a larger grade.  An old stamp is a
//     weaker claim, never a false one.
//
// fresh, at and at_in_place take the grade only from a source.  The
// default value and at_infinity hold the infinite grade, which is the
// weakest claim and needs no source.  weaken, advance_by and the
// compositions only move a grade to a weaker claim, or keep the grade of
// a value with that value.

#include <fixy/GradedFacade.h>
#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

class SequenceSource;

// A position in the sequence of one producer, which a SequenceSource
// vouches for.  No caller can build one.
class SequenceStamp {
public:
    SequenceStamp() = delete("a stamp comes only from a SequenceSource");

    // User-provided, so that the stamp is neither trivially copyable nor
    // implicit-lifetime, and no route builds one from bytes.
    constexpr SequenceStamp(SequenceStamp const& other) noexcept : position_{other.position_} {}
    constexpr SequenceStamp& operator=(SequenceStamp const& other) noexcept {
        position_ = other.position_;
        return *this;
    }
    ~SequenceStamp() = default;

    [[nodiscard]] constexpr std::uint64_t position() const noexcept { return position_; }

private:
    friend class SequenceSource;
    explicit constexpr SequenceStamp(std::uint64_t position) noexcept : position_{position} {}

    std::uint64_t position_ = 0;
};

template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr SequenceSource mint_sequence_source(Ctx const& ctx) noexcept;

// The owner of the sequence of one producer, and the one authority of a
// finite grade of Stale.  Its address is its identity, so it neither
// copies nor moves.  One thread owns it.
//
// A stamp of a second source can reach this source, as a stamp of a second
// VersionSource can.  A stamp ahead of this source gets the infinite grade.
class [[nodiscard]] SequenceSource {
public:
    using staleness_t = ::foundation::algebra::lattices::StalenessSemiring::element_type;

    // The last position.  It is one below the value that encodes the
    // infinite grade, so each distance from a stamp is a finite grade.
    static constexpr std::uint64_t last_position =
        ::foundation::algebra::lattices::StalenessSemiring::element_type::infinity().value - 1;

    SequenceSource(SequenceSource const&) = delete("the source is the one owner of its sequence, and a copy would be "
                                                   "a second owner that advances on its own");
    SequenceSource(SequenceSource&&) = delete("the source is the one owner of its sequence, so it stays in place");
    SequenceSource& operator=(SequenceSource const&) = delete("the position of a source only rises");
    SequenceSource& operator=(SequenceSource&&) = delete("the position of a source only rises");
    ~SequenceSource() = default;

    // The current position.
    [[nodiscard]] constexpr SequenceStamp stamp() const noexcept { return SequenceStamp{position_}; }

    // The stamp of a sample that the producer took at an earlier position.
    // A position that the source has not reached gets no stamp: a producer
    // vouches only for what it has reached.
    [[nodiscard]] constexpr ::foundation::core::Option<SequenceStamp> stamp_at(std::uint64_t position) const noexcept {
        if (position > position_) return ::foundation::core::none;
        return ::foundation::core::Option<SequenceStamp>::some(SequenceStamp{position});
    }

    // The next sample.  The source does not pass its last position.
    constexpr SequenceStamp advance() noexcept {
        CRUCIBLE_PRE(position_ < last_position);
        ++position_;
        return stamp();
    }

    // The grade of a value with the stamp: the number of positions from the
    // stamp to the current position.  A stamp ahead of the source gets the
    // infinite grade, which is the weakest claim.
    [[nodiscard]] constexpr staleness_t steps_behind(SequenceStamp const& stamp) const noexcept {
        if (stamp.position() > position_) return ::foundation::algebra::lattices::StalenessSemiring::top();
        return ::foundation::algebra::lattices::staleness::at(position_ - stamp.position());
    }

private:
    template <typename Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
    friend constexpr SequenceSource mint_sequence_source(Ctx const& ctx) noexcept;

    constexpr SequenceSource() noexcept = default;

    std::uint64_t position_ = 0;
    // GCC counts a class whose copies and moves are all deleted as trivially
    // copyable.  The seal makes the source not trivially copyable, so
    // std::bit_cast builds no source from bytes.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A source at position zero.  The context must own Init, which only
// process startup and the test witness hold.
template <typename Ctx>
    requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
[[nodiscard]] constexpr SequenceSource mint_sequence_source(Ctx const&) noexcept {
    return SequenceSource{};
}

template <typename T>
class [[nodiscard]] Stale : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                                 ::foundation::algebra::lattices::StalenessSemiring, T> {
public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the names this class
    // body uses unqualified are re-declared here rather than found by
    // lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::StalenessSemiring, T>;
    using typename facade_::graded_type;
    // The grade is both a semiring and a chain lattice.  `lattice_type`
    // is what the family-wide introspection surface matches on, and the
    // two spellings beside it are what this header's own readers use.
    using typename facade_::lattice_type;
    using semiring_type = lattice_type;
    using semiring_t = semiring_type;
    using staleness_t = typename semiring_type::element_type;

private:
    graded_type impl_;

    using key_ = ::foundation::algebra::grade_key<Stale>;

    // A constructor that takes a grade is private, so a grade comes only
    // through a named door: fresh, at, at_in_place, at_infinity, weaken,
    // advance_by, or a composition of two graded values.
    constexpr Stale(T value, staleness_t tau) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(value), tau} {}

    // The tag of the in-place constructor.  Only at_in_place names it.
    struct in_place_tag_ final {};

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Stale(in_place_tag_, staleness_t tau, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, T(std::forward<Args>(args)...), tau} {}

public:
    // The weakest claim, because nothing measured the age of this value.
    constexpr Stale() noexcept(std::is_nothrow_default_constructible_v<T>) : impl_{key_{}, T{}, semiring_type::top()} {}

    // A value that the producer took at the current position of its source.
    [[nodiscard]] static constexpr Stale
    fresh(T value, SequenceSource const& source) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), source.steps_behind(source.stamp())};
    }

    // The weakest claim, which needs no source.
    [[nodiscard]] static constexpr Stale at_infinity(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), semiring_type::top()};
    }

    // A value that the producer took at the position of the stamp.  The
    // grade is the distance from the stamp to the current position of the
    // source.
    [[nodiscard]] static constexpr Stale
    at(T value, SequenceSource const& source,
       SequenceStamp const& stamp) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Stale{std::move(value), source.steps_behind(stamp)};
    }

    // The grade of at, with the value built in place from the arguments.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    [[nodiscard]] static constexpr Stale
    at_in_place(SequenceSource const& source, SequenceStamp const& stamp,
                Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                         && std::is_nothrow_move_constructible_v<T>) {
        return Stale{in_place_tag_{}, source.steps_behind(stamp), std::forward<Args>(args)...};
    }

    constexpr Stale(const Stale&) = default;
    constexpr Stale(Stale&&) = default;
    constexpr Stale& operator=(const Stale&) = default;
    constexpr Stale& operator=(Stale&&) = default;
    ~Stale() = default;

    [[nodiscard]] friend constexpr bool
    operator==(Stale const& a,
               Stale const& b) noexcept(noexcept(a.peek() == b.peek()) && noexcept(a.staleness() == b.staleness())) {
        return a.peek() == b.peek() && a.staleness() == b.staleness();
    }

    constexpr void swap(Stale& other) noexcept(std::is_nothrow_swappable_v<T>
                                               && std::is_nothrow_swappable_v<staleness_t>) {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Stale& a, Stale& b) noexcept(std::is_nothrow_swappable_v<T>
                                                            && std::is_nothrow_swappable_v<staleness_t>) {
        a.swap(b);
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr staleness_t staleness() const noexcept { return impl_.grade(); }

    [[nodiscard]] constexpr bool is_fresh() const noexcept { return staleness() == semiring_type::bottom(); }
    [[nodiscard]] constexpr bool is_finite() const noexcept { return staleness().is_finite(); }
    [[nodiscard]] constexpr bool is_infinite() const noexcept { return staleness().is_infinite(); }

    // Mutating the payload leaves the grade valid, because the grade
    // records when the value was produced and not what it holds.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(key_{}); }

    [[nodiscard]] constexpr bool fresher_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness()) && !(staleness() == other.staleness());
    }

    [[nodiscard]] constexpr bool no_staler_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness());
    }

    // The one direction the grade may move by fiat is towards more
    // stale: a value known to be n steps behind is also at most n + k
    // steps behind, so the claim stays true.  A fresher grade is new
    // evidence and needs a new Stale.  The substrate's own weaken
    // carries this rule as a native pre() clause, which a stock GCC 16
    // skips during constant evaluation; the in-body CRUCIBLE_PRE fires
    // there too, and the negative-compile fixture for this method runs
    // in a constant expression.  The result is built before the check;
    // the order is immaterial, since the constructor does nothing
    // beyond its own bounds check.
    [[nodiscard]] constexpr Stale weaken(staleness_t tau) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        Stale result{this->peek(), tau};
        CRUCIBLE_PRE(semiring_type::leq(staleness(), tau));
        return result;
    }

    [[nodiscard]] constexpr Stale weaken(staleness_t tau) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t const was = staleness();
        Stale result{std::move(impl_).consume(), tau};
        CRUCIBLE_PRE(semiring_type::leq(was, tau));
        return result;
    }

    // The payload always comes from `*this`.  The other operand
    // contributes only its grade.
    [[nodiscard]] constexpr Stale
    combine_max(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(), semiring_type::join(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale combine_max(Stale const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t maxed = semiring_type::join(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), maxed};
    }

    // The fresher of the two values, with its own grade.  A tie keeps the
    // value of `*this`.  A grade measures the value that it came with, so
    // the result never gives one value the grade of the other.
    [[nodiscard]] constexpr Stale
    combine_min(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return no_staler_than(other) ? *this : other;
    }

    [[nodiscard]] constexpr Stale combine_min(Stale other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (no_staler_than(other)) return std::move(*this);
        return other;
    }

    // Multiplication in this semiring is saturating addition, which is
    // how staleness travels along a dependency chain.  A sum past the
    // representable range saturates to infinity, and infinity absorbs,
    // so a chain containing one unbounded stage stays unbounded.
    [[nodiscard]] constexpr Stale
    compose_add(Stale const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(), semiring_type::mul(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale compose_add(Stale const& other) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t composed = semiring_type::mul(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), composed};
    }

    // A delta of zero is the multiplicative identity and leaves the
    // grade untouched.
    [[nodiscard]] constexpr Stale
    advance_by(std::uint64_t delta) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::mul(this->staleness(), ::foundation::algebra::lattices::staleness::at(delta))};
    }

    [[nodiscard]] constexpr Stale advance_by(std::uint64_t delta) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        staleness_t advanced =
            semiring_type::mul(this->staleness(), ::foundation::algebra::lattices::staleness::at(delta));
        return Stale{std::move(impl_).consume(), advanced};
    }
};

// The detection surface of Stale: a concept that asks if a type is a
// Stale, and the associated types of one.  One reflection query answers
// it, and the associated types are read off the wrapper's own typedefs,
// so there is no primary-plus-specialization ladder to keep in step
// with the class.  The concept is the question.  The value spelling is
// derived from it, and nothing that gates reads it.

template <typename T>
concept IsStale = ::foundation::reflect::IsInstanceOf<T, ^^Stale>;

template <typename T>
inline constexpr bool is_stale_v = IsStale<T>;

template <typename T>
    requires IsStale<T>
using stale_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_stale_v<T>
using stale_semiring_t = typename std::remove_cvref_t<T>::semiring_type;

template <typename T>
    requires is_stale_v<T>
using stale_staleness_t = typename std::remove_cvref_t<T>::staleness_t;

}  // namespace fixy
