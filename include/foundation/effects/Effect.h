#pragma once

// What each atom covers:
//
//   Alloc   heap allocation and arena allocation
//   IO      file and socket traffic
//   Block   a wait in the kernel: a futex wait, a sleep, a blocking system
//           call.  A spin never carries Block.  A spin whose fallback is a
//           kernel wait carries Block through the fallback
//   Div     a body that can run with no end: a spin with no exit until the
//           value arrives, a loop with no bound
//   Bg      the background-thread context, which holds the value atoms
//   Init    the initialization context
//   Test    the test-driver context
//   Nondet  a read of a source whose value can change from run to run
//           with the same inputs: a clock or a counter, a merge of many
//           producers (an MPSC pop), a receive from a peer
//
// A body whose row has no Nondet gives the same result on each run, each
// schedule and each host, for the same inputs.  Thus a kernel context and
// a replay context, which have no Nondet, prove their determinism in the
// type.  Nondet is an atom of input, not of output: a clock read changes
// nothing outside the process, so ghost-code elision can drop it.
//
// The effect of a wait follows what its caller can observe.  A spin that
// can report a give-up to its caller, such as an expired budget of time or
// of turns, carries Nondet, because the outcome depends on the timing of
// another thread.  A spin with no exit carries Div.  A kernel wait carries
// Block.
//
// The catalog is closed at these eight.  Div is the eighth: a body that
// can run with no end is a fact of its own, apart from a kernel wait.  The
// hot path spins, and a kernel, foreground or replay context must still
// refuse a body that can hang.  So Div is not Block, and DivRow of
// fixy/Aliases.h is Row<Div>.  Four further atoms were considered and
// rejected:
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
//     a failure one time, in the type: the payload Result<T, E> or
//     Option<T> of foundation/core/Choice.h, its std spelling
//     std::expected<T, E>, or fixy::atom::ctrl::throws<E> for a body that
//     throws.  Two failures compose through CRUCIBLE_TRY, which keeps the
//     error types apart, and never through a row.  fixy/Collision.h
//     reads each spelling.

#include <foundation/core/Heap.h>
#include <foundation/core/Text.h>
#include <foundation/diag/RowHash.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <utility>
#include <vector>

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
    Nondet = 6,
    Div = 7,
};

inline constexpr std::size_t effect_count = 8;

// The name of an atom is the identifier its enumerator already
// declares, read by reflection, so a new atom is named the moment it is
// declared and no arm can go missing.  A value outside the enum yields
// "<unknown Effect>", the same sentinel the hand-written switch
// returned.  The name is a TextView of the base (enum_name_text of
// foundation/core/Text.h), with static storage.
//
// constexpr rather than consteval so the runtime smoke test can call
// this with a non-constant argument.  Consteval contexts still fold it.
// It is a template on the one type Effect, so that only a unit that asks
// for a name pays for the walk of the enumerators.  A function that is not
// a template makes each includer walk the enumerators where it is defined.
template <std::same_as<Effect> E>
[[nodiscard]] constexpr ::foundation::core::TextView effect_name(E e) noexcept {
    return ::foundation::core::enum_name_text(e);
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
// world.  Div is observable too: a drop of a body that can run with no end
// changes whether the program ends.  Init and Test are scoped to compile
// time and to the test harness.  Nondet reads the outside world and writes
// nothing there, so a ghost binding that only reads a clock can go.
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
        case Effect::Div:
            return true;
        case Effect::Init:
        case Effect::Test:
        case Effect::Nondet:
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

// The owner of the bare value atoms, defined below.  foundation/core/Heap.h
// declares it too, because the allocation tag of that layer befriends it.
namespace host {
struct AtomOwner;
}  // namespace host

// The Effect enum is the atom catalog for the row algebra.  These
// types are the value-level markers that route the same distinction
// through function parameters, as in
//
//   void* alloc(cap::Alloc, size_t n);
//
// A marker is an authority, so no code makes one from nothing.  The
// default constructor of each marker is private, and its one friend is the
// owner of the bare value atoms below.  Each constructor is user-provided,
// for the reason foundation/core/Heap.h gives for the allocation tag: a
// trivial constructor lets std::bit_cast or std::start_lifetime_as build a
// marker with no access check.  The explicit noexcept on each special
// member turns a future throwing body into a compile error rather than a
// silent change of contract.
namespace cap {

// The allocation doors of foundation/core take the tag of that layer and no
// other type, so this name is an alias of it and not a type of its own.  The
// alias is a member of this namespace, so the bare tag of Effect::Alloc
// below finds it by its identifier.  A using-declaration is no member, and
// that search would not find it.
using Alloc = ::foundation::core::Alloc;

class IO {
public:
    constexpr IO(const IO&) noexcept {}
    constexpr IO& operator=(const IO&) noexcept { return *this; }
    ~IO() = default;

private:
    constexpr IO() noexcept {}

    friend struct ::foundation::effects::host::AtomOwner;
};

class Block {
public:
    constexpr Block(const Block&) noexcept {}
    constexpr Block& operator=(const Block&) noexcept { return *this; }
    ~Block() = default;

private:
    constexpr Block() noexcept {}

    friend struct ::foundation::effects::host::AtomOwner;
};

// The right to read a source of nondeterminism: a door that takes no
// context and reads a clock, a counter, a merge or a peer takes this tag.
class Nondet {
public:
    constexpr Nondet(const Nondet&) noexcept {}
    constexpr Nondet& operator=(const Nondet&) noexcept { return *this; }
    ~Nondet() = default;

private:
    constexpr Nondet() noexcept {}

    friend struct ::foundation::effects::host::AtomOwner;
};

}  // namespace cap

// Bg, Init and Test hold these atoms as public fields, so a caller can
// write `arena.alloc(bg.alloc, ...)` instead of minting a tag at each
// call.  A copy out of `bg.alloc` is the authority of the context, and its
// holder can pass it on.  An execution context of a narrower row does not
// lend its context (foundation/effects/Ctx.h), so no field gives an atom
// that the row of such a context does not claim.

using Alloc = cap::Alloc;
using IO = cap::IO;
using Block = cap::Block;
using Nondet = cap::Nondet;

// The bare tag of an atom is the type in namespace cap whose identifier
// is the enumerator's own, which is how Alloc, IO, Block and Nondet each
// reach theirs without an arm written here.  The identifier is load-bearing:
// a new value atom gets a bare tag by declaring a type of the same name
// in cap, and a type in cap whose name matches no atom is reached by
// nothing.  The three thread atoms and Div declare no such type, and the
// reflection is then not a type.
namespace detail {

template <Effect E>
[[nodiscard]] consteval std::meta::info bare_tag_info_() noexcept {
    static constexpr auto members = std::define_static_array(
        static_cast<::foundation::reflect::anchored_t<std::meta::reflect_constant(E), std::vector<std::meta::info>>>(
            std::meta::members_of(^^cap, std::meta::access_context::current())));
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member)) {
            if (std::meta::identifier_of(member) == ::foundation::reflect::enum_name(E)) return member;
        }
    }
#pragma GCC diagnostic pop
    return std::meta::info{};
}

}  // namespace detail

template <Effect E>
concept HasBareTag = std::meta::is_type(detail::bare_tag_info_<E>());

template <Effect E>
    requires HasBareTag<E>
using bare_tag_t = [:detail::bare_tag_info_<E>():];

class Bg;
class Init;
class Test;

template <Effect Cap, class Source>
class Capability;

// The bridge for a function that takes a bare tag by value: the caller
// mints a Capability, and trades it for the tag.  The owner below names
// this function as a friend, so it is declared here and defined below the
// owner.  The return type trails, so that the friend declaration can name
// the function by its qualified name.
template <Effect E, class Source>
    requires HasBareTag<E>
[[nodiscard]] constexpr auto extract_bare(Capability<E, Source>&& capability) noexcept -> bare_tag_t<E>;

namespace host {

// The one owner of the bare value atoms Alloc, IO, Block and Nondet.  The
// default constructor of each atom is private, and this owner is its one
// friend.
// An owner builds atoms, and only its friends build an owner: the three
// contexts, which hold their atoms as fields, and extract_bare, which
// trades a spent Capability for its bare tag.  So a holder of an atom got
// it from a context, or from a capability that a context minted.
//
// The copy is private too, so a context hands an owner to its fields only
// by reference, and no field keeps one.  Each constructor is user-provided,
// for the reason Effect.h gives for the keys of the contexts.  The rule home
// admits the definition of this owner only in this header.
struct AtomOwner final {
    // One atom of the catalog.
    template <class Atom>
        requires std::same_as<Atom, cap::Alloc> || std::same_as<Atom, cap::IO> || std::same_as<Atom, cap::Block>
              || std::same_as<Atom, cap::Nondet>
    [[nodiscard]] constexpr Atom mint_atom() const noexcept {
        return Atom{};
    }

private:
    constexpr AtomOwner() noexcept {}
    constexpr AtomOwner(AtomOwner const&) noexcept {}

    friend class ::foundation::effects::Bg;
    friend class ::foundation::effects::Init;
    friend class ::foundation::effects::Test;

    template <Effect E, class Source>
        requires HasBareTag<E>
    friend constexpr auto ::foundation::effects::extract_bare(Capability<E, Source>&& capability) noexcept
        -> bare_tag_t<E>;
};

}  // namespace host

// The consume spends the token, so a spent token gives no tag.  A thread
// atom leaves HasBareTag unsatisfied, so this drops out of the overload set
// by substitution failure rather than by a hard error in the body.  The
// Capability is complete where a caller holds one, which is where this
// body is instantiated.
template <Effect E, class Source>
    requires HasBareTag<E>
[[nodiscard]] constexpr auto extract_bare(Capability<E, Source>&& capability) noexcept -> bare_tag_t<E> {
    std::move(capability).consume();
    return host::AtomOwner{}.mint_atom<bare_tag_t<E>>();
}

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
// `bg.alloc`, `bg.io`, `bg.block`, `bg.nondet`.  Only the four value atoms
// have a field, and Div has an empty holding, so a context that lists a
// thread atom as a holding fails to compile on an incomplete base.  A field takes its atom from the owner
// of the atoms, which only a context builds.
template <Effect E>
struct AtomField;

// The field names the tag by its own name in foundation/core, so a
// diagnostic about a copy of it names the class that the doors take.
template <>
struct AtomField<Effect::Alloc> {
    constexpr explicit AtomField(host::AtomOwner const& owner) noexcept
        : alloc{owner.mint_atom<::foundation::core::Alloc>()} {}

    [[no_unique_address]] ::foundation::core::Alloc alloc;
};

template <>
struct AtomField<Effect::IO> {
    constexpr explicit AtomField(host::AtomOwner const& owner) noexcept : io{owner.mint_atom<cap::IO>()} {}

    [[no_unique_address]] cap::IO io;
};

template <>
struct AtomField<Effect::Block> {
    constexpr explicit AtomField(host::AtomOwner const& owner) noexcept : block{owner.mint_atom<cap::Block>()} {}

    [[no_unique_address]] cap::Block block;
};

template <>
struct AtomField<Effect::Nondet> {
    constexpr explicit AtomField(host::AtomOwner const& owner) noexcept : nondet{owner.mint_atom<cap::Nondet>()} {}

    [[no_unique_address]] cap::Nondet nondet;
};

// Div has no token: a body that can run with no end takes nothing from the
// context.  The holding puts Div in the row that the context permits.
template <>
struct AtomField<Effect::Div> {
    constexpr explicit AtomField(host::AtomOwner const& /*owner*/) noexcept {}
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
// The constructors are user-provided, and not defaulted, so no
// constructor of a context is trivial.  A context with a trivial copy is
// trivially copyable, and std::bit_cast builds one from a byte.  A
// context with a trivial constructor is an implicit-lifetime type, and
// std::start_lifetime_as builds one over a buffer.  Each route skips the
// private door, and every ctx-bound gate then admits the forged scope.
// An empty class copies with no instruction either way, and the
// destructor stays trivial.
//
// The context builds the owner of the atoms, and the base hands it to
// each field.  The owner names the three contexts as friends, and not
// this template, so no specialization of the base builds an owner.
template <class Self, class Key, Effect Own, Effect... Holds>
class ContextBase : public AtomField<Holds>... {
public:
    using key_type = Key;
    static constexpr Effect own_effect = Own;

    template <template <Effect...> class R>
    using permitted_as = R<Own, Holds...>;

private:
    constexpr explicit ContextBase(host::AtomOwner const& owner) noexcept : AtomField<Holds>(owner)... {}
    constexpr ContextBase(const ContextBase& other) noexcept : AtomField<Holds>(other)... {}
    constexpr ContextBase& operator=(const ContextBase&) noexcept = default;

    friend Self;
};

}  // namespace detail

// A context names the atoms a thread or scope may exercise.  Init holds
// Block, because process startup waits in the kernel, for example while
// the verifier examines a BPF program.  Each of the three holds Nondet,
// because startup, the background stages and the tests read clocks.  Each
// of the three holds Div, because a background stage and a test can wait
// with no bound.  The hot path stays free of Block, Nondet and Div, because
// the foreground source permits no atom.  Test is not a superset
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
// that calls a door must have a row of the kind init or background, for
// the door that it calls, in utils/ledgers/owners.txt.  The rule owners
// reads that file.
//
// The factory is constexpr so a friended caller can build a context
// during constant evaluation.

class Bg final : public detail::ContextBase<Bg, detail::ctx_mint::bg_key, Effect::Bg, Effect::Alloc, Effect::IO,
                                            Effect::Block, Effect::Nondet, Effect::Div> {
    constexpr Bg() noexcept : ContextBase(host::AtomOwner{}) {}

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

class Init final : public detail::ContextBase<Init, detail::ctx_mint::init_key, Effect::Init, Effect::Alloc, Effect::IO,
                                              Effect::Block, Effect::Nondet, Effect::Div> {
    constexpr Init() noexcept : ContextBase(host::AtomOwner{}) {}

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

class Test final : public detail::ContextBase<Test, detail::ctx_mint::test_key, Effect::Test, Effect::Alloc, Effect::IO,
                                              Effect::Block, Effect::Nondet, Effect::Div> {
    constexpr Test() noexcept : ContextBase(host::AtomOwner{}) {}

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
// The rule owners of the quarantine plugin rejects a call of the door
// outside the process entry points that utils/ledgers/owners.txt admits.
//
// The door also writes the report of the fallback rows of the kernel floor
// that the running kernel lacks, one time in the process
// (report_kernel_fallbacks_once of foundation/os/KernelFloor.h).  The report
// is a write to standard error, an effect of IO, which the init context
// holds.  A later call of the door, in any library of the process, writes
// nothing, and a constant evaluation of the door writes nothing.
struct InitOwner final {
    [[nodiscard]] static constexpr Init mint_init_context() noexcept {
        if !consteval {
            ::foundation::os::report_kernel_fallbacks_once(::foundation::os::running_kernel());
        }
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
// Bg, Alloc, IO, Block, Nondet and Div, so the foreground thread must never hold
// one: its hot path would then claim the right to block.  The door is a static
// member, so the name of the owner is in each call, and the rule
// owners of the quarantine plugin rejects a call outside the thread
// entry points that utils/ledgers/owners.txt admits.
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

// The carriers of the effect layer.  Each one has its definition in its
// own header, and that header includes this one.  Capability is declared
// above, before the owner of the atoms.
template <Effect... Es>
struct Row;
template <typename R, typename T>
class Computation;
template <class Cap, class R>
class ExecCtx;

}  // namespace foundation::effects

// ── The row hash of each effect carrier ─────────────────────────────
//
// foundation/diag/RowHash.h names no effect, so this header gives the fold
// of each carrier of the effect layer.  Each header that declares such a
// carrier includes this header.  So the fold of a carrier is in each unit
// that can name the carrier.  A unit without the fold of a carrier gets the
// zero of the primary template, and no diagnostic.
//
// The two identities keep the names that the published hashes fold, so a
// change of their namespace moves each hash of an execution context and of
// a capability.
namespace foundation::diag {

namespace row_discipline {
template <class Cap>
struct exec_ctx;
template <class Source>
struct capability;
}  // namespace row_discipline

// A row is a set of atoms, and the fold reads the underlying value of each
// atom (atom_set_row_hash_v).
template <::foundation::effects::Effect... Es>
struct row_hash_contribution<::foundation::effects::Row<Es...>> {
    static constexpr std::uint64_t value = atom_set_row_hash_v<static_cast<std::uint64_t>(Es)...>;
};

// The carrier folds its row and then its payload, and the combiner is
// sensitive to that order.  Three properties follow, and
// test/foundation/test_row_hash.cpp pins them:
//   - A carrier and its own bare row have different hashes, because the
//     combiner of a row and a zero payload does not give the row
//   - A bare payload does not change the hash.  The identity of a payload
//     is in the content hash, the other half of the cache key
//   - A carrier nested in a carrier keeps the inner row in the outer hash,
//     so a nested form and its flat form have different hashes.
template <typename R, typename T>
struct row_hash_contribution<::foundation::effects::Computation<R, T>> {
    static constexpr std::uint64_t value =
        ::foundation::reflect::combine_ids(row_hash_contribution_v<R>, row_hash_contribution_v<T>);
};

// A context has no payload, so it is its own identity, and its payload is
// the row that it permits.  Two contexts that permit one row are two
// claims, because one of them names the background thread.
template <typename C>
    requires ::foundation::effects::IsContext<C>
struct row_hash_contribution<C> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<C, typename C::template permitted_as<::foundation::effects::Row>>;
};

// An execution context claims a capability source and a row under it.
// The source changes the claim, so it is in the identity, and the row is
// the payload.
template <class Cap, class R>
struct row_hash_contribution<::foundation::effects::ExecCtx<Cap, R>> {
    static constexpr std::uint64_t value = discipline_row_hash_v<row_discipline::exec_ctx<Cap>, R>;
};

// A capability is a token for one use of one effect, minted from a source.
// The effect folds as the row of one atom that it grants, so a capability
// and a context that permit the same atom agree on that half.
template <::foundation::effects::Effect E, class Source>
struct row_hash_contribution<::foundation::effects::Capability<E, Source>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<row_discipline::capability<Source>, ::foundation::effects::Row<E>>;
};

}  // namespace foundation::diag
