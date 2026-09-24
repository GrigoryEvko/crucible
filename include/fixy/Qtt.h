#pragma once

// Move-only wrapper over one QTT usage grade.  Linear<T> enforces
// "consumed exactly once" and Affine<T> enforces "consumed at most
// once"; the two are Qtt<Grade, T> at the grades One and Zero.
//
// Wrap every resource-carrying type in Linear: a file handle, an mmap
// region, an arena-owned object with drop semantics, a channel
// endpoint.
//
// An Affine value is consumed once or never.  Letting it go out of
// scope unconsumed is a first-class outcome, not an error.  Use it
// where the value is a resource that must not be duplicated but whose
// owner is free to abandon it: a speculative result the primary path
// obviates, a prefetch that is never observed, a branch that gets
// pruned.
//
// The grade is a static property of the wrapper and is not derived
// from the bytes of T, so peek_mut and swap cannot violate it.
//
// The deleted copy and the rvalue-qualified consume are half the
// linearity guarantee, and only half.  They make a copy unspellable and
// make a second consume of the same lvalue require an explicit
// std::move, which is the at-most-once discipline.  That is Affine.
// Exactly-once needs two facts no signature carries: a second consume
// through std::move must be refused, and a value that reaches the end
// of its scope with the obligation still open must be reported.  Both
// read one bit of state per wrapper.
//
// That bit used to be rejected here, and the reason given was sound as
// far as it went.  A member keyed on NDEBUG changes sizeof(Linear<T>),
// the release preset builds the library with NDEBUG and builds the
// tests without it, and the two then disagree about the layout with
// nothing to say so.  The repair is not to drop the bit.  It is to
// stop keying on NDEBUG and to make the disagreement loud.
//
//   - CRUCIBLE_QTT_TRACK_CONSUME is the key, and the build system sets
//     it once per preset for every target in it, so a library and the
//     tests that link it cannot differ inside one build.
//   - CRUCIBLE_QTT_ABI puts [[gnu::abi_tag]] on the class while the
//     bit is on, so a Qtt that crosses a translation-unit boundary
//     between a tracked build and an untracked one is an undefined
//     reference rather than a silent layout mismatch.  Measured on GCC
//     16.2.1: two objects compiled with the key at different settings
//     fail to link, naming the tagged type, and two compiled with it at
//     the same setting link.
//
// With the bit off the wrapper is what it was.  sizeof(Linear<T>) ==
// sizeof(T), the move is trivial and the destructor is trivial, because
// the state lives in a base that is empty and collapses.
//
// What the bit reads, once it is on:
//
//   - A second consume, through std::move or through a source the move
//     already emptied.
//   - A read or a write through a wrapper that has been consumed.
//   - A Linear reaching the end of its scope with the obligation open.
//   - Move assignment over a live Linear.  `a = std::move(b)` releases
//     whatever `a` held through T's own move assignment, and that is a
//     discharge by neither consume nor drop.  It is the scope end
//     reached one line earlier, so it is reported the same way.
//
// And what it still does not see, stated rather than implied:
//
//   - A T whose own destructor releases the resource.  Destruction is a
//     real discharge there, and the tracker cannot tell it from an
//     abandoned obligation.  Such a value is Affine, and spelling it
//     Linear is what the destructor check surfaces rather than what it
//     accuses.
//   - A consume whose RESULT is discarded.  The obligation moves into
//     the returned T, and the tracker stops following it there.
//   - Anything at all in a build with the key off, which is every build
//     that ships.  The key arms one test target today.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/contracts/Armed.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdlib>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

// Declared, not included.  The two recognisers below name these
// templates and instantiate neither, so a declaration is the whole
// dependency and foundation/permissions/Permission.h does not become a
// prerequisite of every file that wraps a value.  The old tree declared
// them the same way, in include/crucible/safety/_Linear.h:29.  A change
// to either template's parameter list breaks this declaration at the
// compile that follows, which is the intended failure.
// The declarations live in foundation/permissions/Fwd.h, which carries
// the defaulted brand and nothing else.

// The consume tracker's key.  A preset sets it for every target at
// once; a bare include that never sees it gets the off build, and the
// ABI tag below is what keeps the two from meeting quietly.
#ifndef CRUCIBLE_QTT_TRACK_CONSUME
#define CRUCIBLE_QTT_TRACK_CONSUME 0
#endif

#if CRUCIBLE_QTT_TRACK_CONSUME
#define CRUCIBLE_QTT_ABI [[gnu::abi_tag("qtt_tracked")]]
#else
#define CRUCIBLE_QTT_ABI
#endif

namespace fixy {

namespace detail {

// Only the two bounded grades name a consume discipline.  A value at
// Omega may be used freely, and that is the bare T.
template <auto Grade>
concept IsConsumeBound = std::same_as<decltype(Grade), ::foundation::algebra::lattices::QttGrade>
                      && (Grade == ::foundation::algebra::lattices::QttGrade::One
                          || Grade == ::foundation::algebra::lattices::QttGrade::Zero);

// The reporter, out of line and cold so the checked build carries one
// predictable branch on the hot path and nothing else.  The site string
// is __PRETTY_FUNCTION__ from the caller, which names the grade and the
// wrapped type, so the message identifies the wrapper rather than this
// header.
[[noreturn]] CRUCIBLE_COLD inline void fail_consume(const char* what, const char* site, int line) noexcept {
    ::foundation::detail::fail_invariant(what, site, "include/fixy/Qtt.h", line);
}

// The consume state, carried as a base rather than a member so that
// Qtt's own special members stay defaulted in both builds and the off
// build keeps its trivial move and its trivial destructor.
//
// The base is what makes the two builds one body of code: every check
// below is a call on the base, and the off base answers every call with
// nothing.  Neither Qtt nor any caller reads
// CRUCIBLE_QTT_TRACK_CONSUME, and the on arm is a template rather than
// a preprocessor arm, so it is parsed in every build and cannot rot
// behind a key nobody sets.
template <auto Grade, class T, bool Tracked>
    requires IsConsumeBound<Grade>
class ConsumeTrackerImpl {
protected:
    // A wrapper that cannot see its own state answers that it is live,
    // because an unchecked build must never refuse a legitimate use.
    [[nodiscard]] static constexpr bool consume_live() noexcept { return true; }
    static constexpr void mark_consumed(const char*, int) noexcept {}
    static constexpr void require_live(const char*, const char*, int) noexcept {}
    static constexpr void swap_consume_state(ConsumeTrackerImpl&) noexcept {}
};

template <auto Grade, class T>
    requires IsConsumeBound<Grade>
class ConsumeTrackerImpl<Grade, T, true> {
protected:
    constexpr ConsumeTrackerImpl() noexcept = default;

    // A move carries the obligation across and leaves none behind, so
    // the source's destructor is silent and a later use of the source
    // is caught.
    constexpr ConsumeTrackerImpl(ConsumeTrackerImpl&& other) noexcept : live_{other.live_} { other.live_ = false; }

    // Assignment over a live wrapper ends that wrapper's obligation
    // through T's move assignment, which discharges nothing.  It is the
    // destructor case reached one line earlier.
    constexpr ConsumeTrackerImpl& operator=(ConsumeTrackerImpl&& other) noexcept {
        report_open_obligation_(__PRETTY_FUNCTION__, __LINE__);
        live_ = other.live_;
        other.live_ = false;
        return *this;
    }

    ConsumeTrackerImpl(const ConsumeTrackerImpl&) = delete("a linearity token is not copyable");
    ConsumeTrackerImpl& operator=(const ConsumeTrackerImpl&) = delete("a linearity token is not copyable");

    constexpr ~ConsumeTrackerImpl() { report_open_obligation_(__PRETTY_FUNCTION__, __LINE__); }

    [[nodiscard]] constexpr bool consume_live() const noexcept { return live_; }

    constexpr void mark_consumed(const char* site, int line) noexcept {
        require_live("consumed twice", site, line);
        live_ = false;
    }

    constexpr void require_live(const char* what, const char* site, int line) const noexcept {
        if (!live_) [[unlikely]] fail_consume(what, site, line);
    }

    constexpr void swap_consume_state(ConsumeTrackerImpl& other) noexcept {
        const bool mine = live_;
        live_ = other.live_;
        other.live_ = mine;
    }

private:
    // Only the exactly-once grade owes a discharge.  An Affine value
    // that is never consumed is a first-class outcome, so the check is
    // the grade's and not the tracker's.
    constexpr void report_open_obligation_(const char* site, int line) const noexcept {
        if constexpr (Grade == ::foundation::algebra::lattices::QttGrade::One) {
            if (live_) [[unlikely]] fail_consume("linear obligation never discharged", site, line);
        } else {
            (void)site;
            (void)line;
        }
    }

    bool live_ = true;
};

template <auto Grade, class T>
using ConsumeTracker = ConsumeTrackerImpl<Grade, T, CRUCIBLE_QTT_TRACK_CONSUME != 0>;

}  // namespace detail

template <auto Grade, class T>
    requires detail::IsConsumeBound<Grade>
class CRUCIBLE_QTT_ABI Qtt;

template <class T>
using Linear = Qtt<::foundation::algebra::lattices::QttGrade::One, T>;

template <class T>
using Affine = Qtt<::foundation::algebra::lattices::QttGrade::Zero, T>;

// A type is already linear when its own discipline encodes the
// exactly-once obligation, and consume-disciplined when it already
// encodes a use bound at least as tight as at-most-once.  Wrapping the
// first in Linear adds no guarantee; wrapping the second in Affine
// makes a required consume optional.
//
// Both answers used to come from a table with a false primary and one
// specialization per token.  That shape is what this file is now
// written against: the port carried both gates and neither table's
// arms, so for a release Affine<Linear<int>> compiled and an
// exactly-once obligation became at-most-once.  A table with no arms
// and a table whose arms all refuse are the same text, and nothing read
// the difference.
//
// So neither answer is a table any more.  Qtt IS the discipline, and
// one reflection query recognises every grade of it without an arm per
// instantiation.  The two permission tokens are a family written once,
// inside a concept body, and a concept cannot be specialized.  The
// witnesses at the foot of this header pin both recognisers in both
// directions, so an arm cannot be removed silently again.
//
// A new token family whose own type encodes an exactly-once obligation
// is added to IsPermissionToken below and to the witnesses beside it.
// Both edits are in this file, and the second is what fails the build
// when the first is forgotten.
//
// The cv-ref strip lives inside the reflection query, so `Linear<const
// Token>` answers as `Linear<Token>` does.
namespace detail {

// Qtt of either grade, recognised structurally.
template <typename T>
concept IsQttInstance = ::foundation::reflect::IsInstanceOf<T, ^^Qtt>;

// Qtt at grade One: the exactly-once half of the family.  The grade is
// read off the wrapper's own member rather than from a second table.
template <typename T>
concept IsExactlyOnceQtt =
    IsQttInstance<T>
    && (std::remove_cvref_t<T>::usage_grade == ::foundation::algebra::lattices::QttGrade::One);

// The permission tokens: empty, move-only, and consumed exactly once by
// the operation they authorize.
template <typename T>
concept IsPermissionToken =
    ::foundation::reflect::IsInstanceOfAny<T, ^^::foundation::permissions::Permission,
                                           ^^::foundation::permissions::SharedPermission>;

}  // namespace detail

template <typename T>
struct is_already_linear : std::bool_constant<detail::IsExactlyOnceQtt<T> || detail::IsPermissionToken<T>> {};

template <typename T>
inline constexpr bool is_already_linear_v = is_already_linear<T>::value;

template <typename T>
struct is_already_consume_disciplined
    : std::bool_constant<detail::IsQttInstance<T> || detail::IsPermissionToken<T>> {};

template <typename T>
inline constexpr bool is_already_consume_disciplined_v = is_already_consume_disciplined<T>::value;

// The constructors are private, so these two are the only door.
template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <auto Grade, class T>
    requires detail::IsConsumeBound<Grade>
class CRUCIBLE_QTT_ABI [[nodiscard]] Qtt
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                           ::foundation::algebra::lattices::QttSemiring::At<Grade>, T>,
      private detail::ConsumeTracker<Grade, T> {
    // Placed in the class body so the diagnostic surfaces at the user's
    // instantiation site rather than inside the substrate.
    static_assert(Grade != ::foundation::algebra::lattices::QttGrade::One || !is_already_linear_v<T>,
                  "Linear<Token> over an already-linear token is redundant: the token IS already a "
                  "move-only linearity token (deleted copy, [[nodiscard]], sizeof = 1, EBO-collapsible).  "
                  "Wrapping it stacks two disciplines without adding a new bug class.  Use the token "
                  "directly.");
    static_assert(Grade != ::foundation::algebra::lattices::QttGrade::Zero || !is_already_consume_disciplined_v<T>,
                  "Affine<Token> over a consume-disciplined token is unsound: the token carries an "
                  "EXACTLY-ONCE obligation; wrapping in Affine downgrades that to at-most-once, making "
                  "the consume OPTIONAL when it is REQUIRED.  Use the token directly.");

public:
    // The grade, carried as a member rather than read back out of the
    // lattice type by a second table.  is_already_linear reads this to
    // tell the exactly-once half of the family from the at-most-once
    // half, and a caller reasoning about a Qtt it was handed reads the
    // same member.
    static constexpr ::foundation::algebra::lattices::QttGrade usage_grade = Grade;

    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::QttSemiring::At<Grade>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr typename lattice_type::element_type pinned_grade() noexcept {
        return typename lattice_type::element_type{};
    }

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Qtt(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                     && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pinned_grade()} {}

    template <class U, class... Args>
        requires std::is_constructible_v<U, Args...>
    friend constexpr Linear<U> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>);

    template <class U, class... Args>
        requires std::is_constructible_v<U, Args...>
    friend constexpr Affine<U> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>);

public:
    Qtt(const Qtt&) = delete("Linear<T> and Affine<T> are move-only; use std::move or drop()");
    Qtt& operator=(const Qtt&) = delete("Linear<T> and Affine<T> are move-only; use std::move or drop()");
    Qtt(Qtt&&) = default;
    Qtt& operator=(Qtt&&) = default;
    ~Qtt() = default;

    // The usage the grade counts is spent here, and only here.  In a
    // tracked build the state is read before it is spent, so a second
    // consume through std::move is reported rather than moving out of
    // an already moved-from value.
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        this->mark_consumed(__PRETTY_FUNCTION__, __LINE__);
        return std::move(impl_).consume();
    }

    // Reading a consumed wrapper is use-after-consume in the plainest
    // form, so both accessors ask the same question the consume does.
    [[nodiscard]] constexpr const T& peek() const& noexcept {
        this->require_live("read after consume", __PRETTY_FUNCTION__, __LINE__);
        return impl_.peek();
    }

    // Prefer consume and reconstruct over this when the change is
    // semantic rather than incidental.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        this->require_live("written after consume", __PRETTY_FUNCTION__, __LINE__);
        return impl_.peek_mut();
    }

    // The obligations travel with the values, so the state swaps too.
    // Neither side has to be live: a swap of a consumed wrapper against
    // a live one is how a caller hands an obligation on.
    constexpr void swap(Qtt& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
        this->swap_consume_state(other);
    }

    friend constexpr void swap(Qtt& a, Qtt& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // A Linear is consumed on the way out.  For an Affine this is a
    // no-op that records the deliberate choice not to consume, so that
    // a search distinguishes intentional discards from consume sites.
    friend constexpr void drop(Qtt&& x) noexcept(Grade != ::foundation::algebra::lattices::QttGrade::One
                                                 || std::is_nothrow_move_constructible_v<T>) {
        if constexpr (Grade == ::foundation::algebra::lattices::QttGrade::One) {
            (void)std::move(x).consume();
        } else {
            // A deliberate discard is a discharge, so the state records
            // it and a use after this drop reads as use-after-consume.
            x.mark_consumed(__PRETTY_FUNCTION__, __LINE__);
        }
    }
};

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Linear<T>{std::in_place, std::forward<Args>(args)...};
}

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Affine<T>{std::in_place, std::forward<Args>(args)...};
}

// What the tracker costs, named once so a caller and a test can read
// the same answer instead of each testing the macro.
inline constexpr bool qtt_consume_tracked = CRUCIBLE_QTT_TRACK_CONSUME != 0;

// Both builds are pinned, and each pin is written so the other build
// cannot satisfy it by accident.
//
// Off: the grade is empty, the tracker is empty, and the wrapper is the
// value.  This is the shape every hot path was measured against.
static_assert(qtt_consume_tracked || sizeof(Linear<int>) == sizeof(int));
static_assert(qtt_consume_tracked || sizeof(Linear<void*>) == sizeof(void*));
static_assert(qtt_consume_tracked || sizeof(Linear<long long>) == sizeof(long long));
static_assert(qtt_consume_tracked || sizeof(Affine<int>) == sizeof(int));
static_assert(qtt_consume_tracked || sizeof(Affine<void*>) == sizeof(void*));
static_assert(qtt_consume_tracked || sizeof(Affine<long long>) == sizeof(long long));
static_assert(qtt_consume_tracked || std::is_trivially_destructible_v<Linear<int>>);
static_assert(qtt_consume_tracked || std::is_trivially_move_constructible_v<Linear<int>>);

// On: the state is one bool, so the wrapper grows by at most the
// alignment it has to round up to.  A tracker that quietly grew past
// one byte fails here rather than in a cache-miss profile.
static_assert(!qtt_consume_tracked || sizeof(Linear<int>) <= sizeof(int) + alignof(int));
static_assert(!qtt_consume_tracked || sizeof(Linear<void*>) <= sizeof(void*) + alignof(void*));
static_assert(!qtt_consume_tracked || sizeof(Affine<long long>) <= sizeof(long long) + alignof(long long));

// And on, the wrapper is no longer trivial, which is the fact that
// makes the destructor able to say anything at all.
static_assert(!qtt_consume_tracked || !std::is_trivially_destructible_v<Linear<int>>);
static_assert(!qtt_consume_tracked || !std::is_trivially_destructible_v<Affine<int>>);

static_assert(Linear<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(Affine<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(std::is_same_v<Linear<int>::lattice_type, ::foundation::algebra::lattices::qtt::LinearGrade>);
static_assert(std::is_same_v<Affine<int>::lattice_type, ::foundation::algebra::lattices::qtt::Erased>);

// Qtt carries a non-type parameter, which is the case the reflection
// form in foundation/reflect/Instance.h exists for.  The cv-ref strip
// and the non-template rejection are pinned beside it.
static_assert(::foundation::reflect::is_instance_of_v<Linear<int>, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Affine<int>, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Linear<int> const&, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Linear<int>&&, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<int, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<void, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<std::unique_ptr<int>, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<Linear<int>, ^^std::unique_ptr>);

// The grade is readable off the wrapper, which is what tells the
// exactly-once half of the family from the at-most-once half.
static_assert(Linear<int>::usage_grade == ::foundation::algebra::lattices::QttGrade::One);
static_assert(Affine<int>::usage_grade == ::foundation::algebra::lattices::QttGrade::Zero);

namespace detail::qtt_witness {

// Incomplete on purpose: the recognisers name the templates and
// instantiate nothing, so an incomplete argument is the honest witness.
struct tag;

using ExclusiveToken = ::foundation::permissions::Permission<tag>;
using SharedToken = ::foundation::permissions::SharedPermission<tag>;

}  // namespace detail::qtt_witness

// Both recognisers, pinned in both directions.  This is the cell that
// was missing: a port can carry a gate and drop its arms, and until
// something asserts that the arms still answer, the gate reads as a
// gate and admits everything.  Each line names types the recogniser
// must accept and types it must refuse, and an empty recogniser fails
// the first, a universal one the second.
static_assert(::foundation::contracts::predicate_accepts<
              is_already_linear, Linear<int>, Linear<int> const&, Linear<int>&&, Linear<void*>,
              detail::qtt_witness::ExclusiveToken, detail::qtt_witness::SharedToken const&>());
static_assert(::foundation::contracts::predicate_refuses<is_already_linear, int, void*, Affine<int>,
                                                         std::unique_ptr<int>>());

static_assert(::foundation::contracts::predicate_accepts<
              is_already_consume_disciplined, Linear<int>, Affine<int>, Affine<int> const&, Affine<void*>&&,
              detail::qtt_witness::ExclusiveToken, detail::qtt_witness::SharedToken>());
static_assert(::foundation::contracts::predicate_refuses<is_already_consume_disciplined, int, void*,
                                                         std::unique_ptr<int>>());

// The two rejections the gates exist for, stated as the facts they
// rest on.  The refusals themselves are compile errors, so they are
// proven by the fixtures test/fixy/neg/neg_qtt_*.cpp rather than here.
static_assert(is_already_linear_v<Linear<int>>, "Linear<Linear<T>> must be refused: the inner wrapper already "
                                                "carries the exactly-once obligation.");
static_assert(is_already_consume_disciplined_v<Linear<int>>,
              "Affine<Linear<T>> must be refused: wrapping an exactly-once obligation in an at-most-once one "
              "downgrades a required consume to an optional one.");

}  // namespace fixy
