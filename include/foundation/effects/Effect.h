#pragma once

// What each atom covers:
//
//   Alloc  heap allocation and arena allocation
//   IO     file and socket traffic
//   Block  mutex, sleep, futex, spin-wait
//   Bg     the background-thread context, which holds the three above
//   Init   the initialization context
//   Test   the test-driver context
//
// The catalog is closed at these six.  Four further atoms were
// considered and rejected:
//
//   Async — coroutine reentrancy is a property of how a function
//     suspends, not a capability it exercises.  It is tracked on the
//     reentrancy axis instead.
//   Network — the IO atom already covers socket traffic.  Splitting it
//     would cost per-call bookkeeping to distinguish two atoms that the
//     row algebra treats identically.
//   CT — constant time is a discipline on how a body executes, not a
//     capability it exercises, and its polarity is the opposite of a
//     row's.  A row grows by union: a caller holds every atom of its
//     callees, and a context that admits a row admits each subrow of it.
//     A constant-time claim holds for a composition only when it holds
//     for every part.  As a row atom, Subrow<Row<>, Row<CT>> would be
//     true, so a context that admits CT would admit a body that claims
//     nothing.  That is the wrong direction for a guarantee.  The claim
//     is the Security grade fixy::atom::constant_time instead: constant
//     time has a meaning only for classified data, and every collision
//     rule that reads it reads it on the binding, never through a row.
//   Fail — a failure is a value that a body returns, and the error type
//     is part of that value.  An enumerator is one bit and cannot carry
//     the error type, so Row<Fail> would make two error types one atom,
//     and Subrow could not tell Fail(E1) from Fail(E2).  The tree spells
//     a failure one time, in the type: the payload std::expected<T, E>,
//     or fixy::atom::ctrl::throws<E> for a body that throws.  Two
//     failures compose through std::expected::and_then, which keeps the
//     error types apart, and never through a row.  fixy/Collision.h
//     reads both spellings.

#include <foundation/reflect/EnumName.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>

namespace foundation::effects {

// The underlying values are frozen.  Each one is a bit position in the
// row masks that key the federation cache, so renumbering an atom
// silently re-keys every cache entry already published by every fleet
// that consumed the affected rows.  A new atom takes the next free
// value and leaves the existing ones alone, which confines cache
// invalidation to entries that mention the new atom.
enum class Effect : std::uint8_t {
    Alloc = 0,
    IO = 1,
    Block = 2,
    Bg = 3,
    Init = 4,
    Test = 5,
};

inline constexpr std::size_t effect_count = 6;

// The name of an atom is the identifier its enumerator already
// declares, read by reflection, so a new atom is named the moment it is
// declared and no arm can go missing.  A value outside the enum yields
// "<unknown Effect>", the same sentinel the hand-written switch
// returned.
//
// constexpr rather than consteval so the runtime smoke test can call
// this with a non-constant argument.  Consteval contexts still fold it.
// It is a template on the one type Effect, so that only a unit that asks
// for a name pays for the walk of enum_name.  A function that is not a
// template makes each includer walk the enumerators where it is defined.
template <std::same_as<Effect> E>
[[nodiscard]] constexpr std::string_view effect_name(E e) noexcept {
    return ::foundation::reflect::enum_name(e);
}

// The gate reads the catalog through reflection so that a new atom
// satisfies it without an edit here.  A hand-written disjunction would
// reject every future atom until someone remembered to extend it.
// The answer is a function at namespace scope that is not a template, so
// no translation unit can specialize it to admit a value that the catalog
// does not name.
[[nodiscard]] consteval bool is_effect_atom(Effect effect) {
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^Effect)) {
        if (std::meta::extract<Effect>(std::meta::constant_of(enumerator)) == effect) return true;
    }
    return false;
}

template <Effect E>
concept IsEffect = is_effect_atom(E);

// An atom is observable when ghost-code elision may not silently drop
// a binding tagged with it.  Alloc, IO, Block and Bg reach the outside
// world; Init and Test are scoped to compile time and to the test
// harness.
namespace detail {

// The build requires a default arm on every switch, so the switch
// below cannot itself trap an unclassified new atom: it would quietly
// answer "not observable".  A cardinality pin in the check file of this
// header, test/layer/checks/foundation/effects/Effect.cpp, is the trap
// instead.
template <Effect E>
[[nodiscard]] consteval bool is_observable_effect_atom_() noexcept {
    switch (E) {
        case Effect::Alloc:
        case Effect::IO:
        case Effect::Block:
        case Effect::Bg:
            return true;
        case Effect::Init:
        case Effect::Test:
            return false;
        default:
            return false;
    }
}

}  // namespace detail

template <Effect E>
[[nodiscard]] consteval bool is_observable() noexcept {
    return detail::is_observable_effect_atom_<E>();
}

// The Effect enum is the atom catalog for the row algebra.  These
// types are the value-level markers that route the same distinction
// through function parameters, as in
//
//   void* alloc(cap::Alloc, size_t n);
//
// The explicit noexcept on each special member turns a future throwing
// body into a compile error rather than a silent change of contract.
namespace cap {

struct Alloc {
    // The families of foundation/core take this tag where they allocate.
    // That layer is below this one and names no type of it, so its gate
    // reads this member.
    using grants_allocation = void;

    constexpr Alloc() noexcept = default;
    constexpr Alloc(const Alloc&) noexcept = default;
    constexpr Alloc(Alloc&&) noexcept = default;
    constexpr Alloc& operator=(const Alloc&) noexcept = default;
    constexpr Alloc& operator=(Alloc&&) noexcept = default;
    ~Alloc() = default;
};

struct IO {
    constexpr IO() noexcept = default;
    constexpr IO(const IO&) noexcept = default;
    constexpr IO(IO&&) noexcept = default;
    constexpr IO& operator=(const IO&) noexcept = default;
    constexpr IO& operator=(IO&&) noexcept = default;
    ~IO() = default;
};

struct Block {
    constexpr Block() noexcept = default;
    constexpr Block(const Block&) noexcept = default;
    constexpr Block(Block&&) noexcept = default;
    constexpr Block& operator=(const Block&) noexcept = default;
    constexpr Block& operator=(Block&&) noexcept = default;
    ~Block() = default;
};

}  // namespace cap

// Bg, Init and Test hold these atoms as public fields, so a caller can
// write `arena.alloc(bg.alloc, ...)` instead of minting a tag at each
// call.  That is sound only while the atoms are stateless: a copy out
// of `bg.alloc` is then indistinguishable from `cap::Alloc{}`, which
// anyone can already write, so the public field grants nothing.  Give
// an atom state and the copy becomes an escape from the context's
// intended scope.

using Alloc = cap::Alloc;
using IO = cap::IO;
using Block = cap::Block;

namespace testing {
struct TestWitness;
}  // namespace testing

// Each key below names an owner as a friend, and each owner is defined
// in the header that declares its key.  A translation unit that can name
// a key has included that header, so it sees the definition, and a second
// definition of an owner is a redefinition error.  An owner declared here
// and defined in some other file is a door: the first definition in any
// translation unit is legal C++, and its members build the key.
namespace host {
struct BackgroundOwner;
struct InitOwner;
}  // namespace host

// Each constructor of a key is user-provided, and not defaulted.  A key
// with a trivial copy is trivially copyable, and std::bit_cast then
// builds one from any byte.  A key with any trivial constructor is an
// implicit-lifetime type, and std::start_lifetime_as then builds one
// over a buffer.  Neither route names a constructor, so neither meets
// the access check that the friend list controls.  The copy stays
// public, so a holder can pass a key along.
namespace detail::ctx_mint {

class bg_key {
private:
    constexpr bg_key() noexcept {}

    friend struct ::foundation::effects::host::BackgroundOwner;
    friend struct ::foundation::effects::testing::TestWitness;

public:
    constexpr bg_key(const bg_key&) noexcept {}
};

class init_key {
private:
    constexpr init_key() noexcept {}

    friend struct ::foundation::effects::host::InitOwner;
    friend struct ::foundation::effects::testing::TestWitness;

public:
    constexpr init_key(const init_key&) noexcept {}
};

class test_key {
private:
    constexpr test_key() noexcept {}

    friend struct ::foundation::effects::testing::TestWitness;

public:
    constexpr test_key(const test_key&) noexcept {}
};

}  // namespace detail::ctx_mint

// The background owner and the init owner are defined below, after the
// contexts, because each door returns its context by value.

class Bg;
class Init;
class Test;

// The roster of contexts, and the whole of it.  IsContext reads this
// array and nothing else, so a class is a context because this header
// lists it, not because of what it derives from or what a translation
// unit specializes.  A class template trait would not do: any translation
// unit can specialize a class template explicitly, and a lookalike that
// specialized it would be a capability source to every gate.
namespace detail {

inline constexpr std::meta::info context_roster[] = {^^Bg, ^^Init, ^^Test};

}  // namespace detail

// True when type names one of the rostered contexts, with no qualifier.
// The answer is a function at namespace scope that is not a template, so
// no translation unit can specialize it.
[[nodiscard]] consteval bool is_rostered_context(std::meta::info type) {
    for (const std::meta::info entry : detail::context_roster) {
        if (std::meta::dealias(type) == entry) return true;
    }
    return false;
}

template <class T>
concept IsContext = is_rostered_context(^^T);

// One passkey mints one context.  The binding is the context's own
// key_type, a member each context declares below, so handing the wrong
// key to the factory is a constraint failure at the call, and no
// translation unit can rebind a key by specializing anything.
template <class Ctx, class Key>
concept CanMintContext = IsContext<Ctx> && std::same_as<Key, typename Ctx::key_type>;

// The one door of every context.  It is declared before them so that
// the friend declaration in each names this template and not a fresh
// one in the enclosing namespace.
template <class Ctx, class Key>
    requires CanMintContext<Ctx, Key>
[[nodiscard]] constexpr Ctx mint_context(Key) noexcept;

namespace detail {

// The value atom a context holds, under the field name a caller spells:
// `bg.alloc`, `bg.io`, `bg.block`.  Only the three value atoms have a
// field, so a context that lists a thread atom as a holding fails to
// compile on an incomplete base.
template <Effect E>
struct AtomField;

template <>
struct AtomField<Effect::Alloc> {
    [[no_unique_address]] cap::Alloc alloc{};
};

template <>
struct AtomField<Effect::IO> {
    [[no_unique_address]] cap::IO io{};
};

template <>
struct AtomField<Effect::Block> {
    [[no_unique_address]] cap::Block block{};
};

// What the three contexts share.  A context is its own atom, the value
// atoms it holds as fields, and the key that mints it.  The row it
// permits is the atom followed by the holdings, in that order, and
// Ctx.h reads it through permitted_as instead of restating it.  The door
// is the derived class's own private default constructor: that is the
// one the roster fixture must find private, and a base cannot make it
// so.
//
// The constructors are private, and the context is the one friend.  A
// class that derives from this base cannot build it, so no object other
// than a context holds one, and no downcast from a base reaches a
// context that does not exist.
//
// The default constructor and the copy constructor are user-provided,
// and not defaulted, so no constructor of a context is trivial.  A
// context with a trivial copy is trivially copyable, and std::bit_cast
// builds one from a byte.  A context with a trivial constructor is an
// implicit-lifetime type, and std::start_lifetime_as builds one over a
// buffer.  Each route skips the private door, and every ctx-bound gate
// then admits the forged scope.  An empty class copies with no
// instruction either way, and the destructor stays trivial.
template <class Self, class Key, Effect Own, Effect... Holds>
class ContextBase : public AtomField<Holds>... {
public:
    using key_type = Key;
    static constexpr Effect own_effect = Own;

    template <template <Effect...> class R>
    using permitted_as = R<Own, Holds...>;

private:
    constexpr ContextBase() noexcept {}
    constexpr ContextBase(const ContextBase& other) noexcept : AtomField<Holds>(other)... {}
    constexpr ContextBase& operator=(const ContextBase&) noexcept = default;

    friend Self;
};

}  // namespace detail

// A context names the atoms a thread or scope may exercise.  Init holds
// Block, because process startup waits in the kernel, for example while
// the verifier examines a BPF program.  The hot path stays free of Block,
// because the foreground source permits no atom.  Test is not a superset
// of Bg or Init: a fixture that must drive a background or
// initialization path constructs that context explicitly rather than
// passing a Test one.
//
// Each context has a private default constructor and befriends the one
// factory, whose constraint admits the context's own passkey and no
// other.  The passkey's own default constructor is private too.  Its
// friends are the owner of the context and the test scaffolding.  A
// translation unit that holds neither cannot forge a context.  The init
// owner and the background owner each have a door.  A new entry point
// that calls the init door needs a row in utils/scripts/ctx-init-door-allowlist.txt,
// and one that calls the background door needs a row in
// utils/scripts/ctx-bg-door-allowlist.txt.
//
// The factory is constexpr so a friended caller can build a context
// during constant evaluation.

class Bg final
    : public detail::ContextBase<Bg, detail::ctx_mint::bg_key, Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block> {
    constexpr Bg() noexcept = default;

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

class Init final : public detail::ContextBase<Init, detail::ctx_mint::init_key, Effect::Init, Effect::Alloc, Effect::IO,
                                              Effect::Block> {
    constexpr Init() noexcept = default;

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

class Test final : public detail::ContextBase<Test, detail::ctx_mint::test_key, Effect::Test, Effect::Alloc, Effect::IO,
                                              Effect::Block> {
    constexpr Test() noexcept = default;

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

// The factory body is one of the few scopes where a private default
// constructor is a valid expression, so it is where the noexcept
// property can be pinned at all.
template <class Ctx, class Key>
    requires CanMintContext<Ctx, Key>
[[nodiscard]] constexpr Ctx mint_context(Key) noexcept {
    static_assert(noexcept(Ctx{}), "A context's default constructor must be noexcept.  A capability tag's "
                                   "default member initializer must never throw.");
    return Ctx{};
}

namespace host {

// The one production door of the init context.  The init key has a
// private constructor, and its friends are this owner and the test
// witness.  So production code can get an init context only from this
// door.
//
// The door is a static member, so the name of the owner is in each call.
// utils/scripts/check-ctx-init-door.py rejects a call of the door outside the
// process entry points in utils/scripts/ctx-init-door-allowlist.txt.
struct InitOwner final {
    [[nodiscard]] static constexpr Init mint_init_context() noexcept {
        return mint_context<Init>(detail::ctx_mint::init_key{});
    }
};

// The one production door of the background context.  The background key
// has a private constructor, and its friends are this owner and the test
// witness.  So production code can get a background context only from
// this door.
//
// Only the entry function of a background thread calls the door, on that
// thread, before the thread enters its loop.  A background context permits
// Bg, Alloc, IO and Block, so the foreground thread must never hold one: its
// hot path would then claim the right to block.  The door is a static
// member, so the name of the owner is in each call, and
// utils/scripts/check-ctx-init-door.py rejects a call outside the thread entry
// points in utils/scripts/ctx-bg-door-allowlist.txt.
struct BackgroundOwner final {
    [[nodiscard]] static constexpr Bg mint_background_context() noexcept {
        return mint_context<Bg>(detail::ctx_mint::bg_key{});
    }
};

}  // namespace host

// Naming this namespace outside test and bench code is a review
// rejection.  Its whole purpose is that a grep for it finds every
// translation unit taking the test path.
namespace testing {

struct TestWitness {
    [[nodiscard]] static constexpr Bg bg() noexcept { return mint_context<Bg>(detail::ctx_mint::bg_key{}); }
    [[nodiscard]] static constexpr Init init() noexcept { return mint_context<Init>(detail::ctx_mint::init_key{}); }
    [[nodiscard]] static constexpr Test test() noexcept { return mint_context<Test>(detail::ctx_mint::test_key{}); }
};

[[nodiscard]] inline constexpr Bg bg() noexcept { return TestWitness::bg(); }
[[nodiscard]] inline constexpr Init init() noexcept { return TestWitness::init(); }
[[nodiscard]] inline constexpr Test test() noexcept { return TestWitness::test(); }

}  // namespace testing

}  // namespace foundation::effects
