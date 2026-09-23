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
#include <type_traits>

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

inline constexpr std::size_t effect_count = std::meta::enumerators_of(^^Effect).size();

// `effect_count` counts enumerator NAMES.  Two names can still share
// one underlying value, which would raise the count while leaving the
// atoms indistinguishable as bit positions: rows claiming one atom
// would silently satisfy a gate that demands the other, and two
// federation cache keys would collide.  This witness tracks each
// observed value in a bitmask and refuses a repeat.
namespace detail {

[[nodiscard]] consteval bool every_effect_underlying_distinct_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
    using U = std::underlying_type_t<Effect>;
    std::uint64_t seen = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto u = static_cast<U>([:en:]);
        if constexpr (static_cast<unsigned>(u) >= 64u) {
            return false;
        } else {
            const std::uint64_t bit = std::uint64_t{1} << static_cast<unsigned>(u);
            if (seen & bit) {
                return false;
            }
            seen |= bit;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

static_assert(detail::every_effect_underlying_distinct_(),
              "Two Effect enumerators share an underlying value, or one is >= 64 and so exceeds the "
              "uint64_t row-mask carrier.  Each atom must occupy a distinct bit position below 64.  "
              "Duplicates collapse two atoms into one row bit and make federation cache keys collide.  "
              "Give the new atom the next free underlying value explicitly in the enum.");

// The name of an atom is the identifier its enumerator already
// declares, read by reflection, so a new atom is named the moment it is
// declared and no arm can go missing.  A value outside the enum yields
// "<unknown Effect>", the same sentinel the hand-written switch
// returned.
//
// constexpr rather than consteval so the runtime smoke test can call
// this with a non-constant argument.  Consteval contexts still fold it.
[[nodiscard]] constexpr std::string_view effect_name(Effect e) noexcept { return ::foundation::reflect::enum_name(e); }

// The gate reads the catalog through reflection so that a new atom
// satisfies it without an edit here.  A hand-written disjunction would
// reject every future atom until someone remembered to extend it.
namespace detail {

template <Effect E>
[[nodiscard]] consteval bool is_effect_atom_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (E == [:en:]) return true;
    }
#pragma GCC diagnostic pop
    return false;
}

}  // namespace detail

template <Effect E>
concept IsEffect = detail::is_effect_atom_<E>();

// An atom is observable when ghost-code elision may not silently drop
// a binding tagged with it.  Alloc, IO, Block and Bg reach the outside
// world; Init and Test are scoped to compile time and to the test
// harness.
namespace detail {

// The build requires a default arm on every switch, so the switch
// below cannot itself trap an unclassified new atom: it would quietly
// answer "not observable".  This cardinality pin is the trap instead.
static_assert(effect_count == 6, "A new Effect enumerator needs a deliberate observable-or-not decision in "
                                 "is_observable_effect_atom_ below, and this count raised to match.");

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

// Instantiating the classifier for every atom catches a contributor
// who adds case arms whose default semantics disagree with the rest.
// It does not catch an unclassified atom on its own; the cardinality
// pin above does that.
consteval bool every_effect_observability_classified_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) { (void)is_observable_effect_atom_<([:en:])>(); }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_effect_observability_classified_(),
              "Every Effect atom must reach a case arm of is_observable_effect_atom_.");

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
static_assert(std::is_empty_v<cap::Alloc>,
              "cap::Alloc must remain a stateless empty struct.  Bg, Init and Test expose public fields of "
              "this type, and a caller can copy one out.  Either privatize those fields behind a friended "
              "accessor, or carry the state on the linear capability token instead.");
static_assert(std::is_empty_v<cap::IO>, "cap::IO must remain stateless.  Bg, Init and Test expose a public "
                                        "field of this type.");
static_assert(std::is_empty_v<cap::Block>, "cap::Block must remain stateless.  Bg and Test expose a public "
                                           "field of this type.  Init omits it because an init context must "
                                           "not block.");

using Alloc = cap::Alloc;
using IO = cap::IO;
using Block = cap::Block;

namespace testing {
struct TestWitness;
}  // namespace testing

// The production entry points that may start a context belong to the
// layer that stands up the thread or the phase, and this layer cannot
// name them.  Each host type below is declared here and defined by the
// owner of the corresponding entry point (the background-thread header
// defines BackgroundOwner, the initialization owner defines InitOwner).
// That definition is the one place a production key is built, and it
// decides who may call it.  The forgery surface is the one a friend
// naming the host class directly already had: a second definition of
// the host type is an ODR violation, as a fake host class would be.
namespace host {
struct BackgroundOwner;
struct InitOwner;
}  // namespace host

namespace detail::ctx_mint {

class bg_key {
private:
    constexpr bg_key() noexcept = default;

    friend struct ::foundation::effects::host::BackgroundOwner;
    friend struct ::foundation::effects::testing::TestWitness;
};

class init_key {
private:
    constexpr init_key() noexcept = default;

    friend struct ::foundation::effects::host::InitOwner;
    friend struct ::foundation::effects::host::BackgroundOwner;
    friend struct ::foundation::effects::testing::TestWitness;
};

class test_key {
private:
    constexpr test_key() noexcept = default;

    friend struct ::foundation::effects::testing::TestWitness;
};

}  // namespace detail::ctx_mint

class Bg;
class Init;
class Test;

// The roster of contexts, and the whole of it.  IsContext reads this
// array and nothing else, so a class is a context because this header
// lists it, not because of what it derives from or what a translation
// unit specializes.  The old is_cap_type was a class template with one
// specialization per context, and a class template can be explicitly
// specialized from any translation unit, so a lookalike that
// specialized it was a capability source to every gate.
namespace detail {

inline constexpr std::meta::info context_roster[] = {^^Bg, ^^Init, ^^Test};

template <class T>
[[nodiscard]] consteval bool is_rostered_context_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : context_roster) {
        if constexpr (std::is_same_v<T, typename [:entry:]>) return true;
    }
#pragma GCC diagnostic pop
    return false;
}

}  // namespace detail

template <class T>
concept IsContext = detail::is_rostered_context_<T>();

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
// Ctx.h reads it through permitted_as instead of restating it.  The
// constructor is protected because the door is the derived class's own
// private default constructor: that is the one the roster fixture must
// find private, and a base cannot make it so.
template <class Self, class Key, Effect Own, Effect... Holds>
class ContextBase : public AtomField<Holds>... {
public:
    using key_type = Key;
    static constexpr Effect own_effect = Own;

    template <template <Effect...> class R>
    using permitted_as = R<Own, Holds...>;

protected:
    constexpr ContextBase() noexcept = default;
};

}  // namespace detail

// A context names the atoms a thread or scope may exercise.  Init
// omits Block because an initialization scope must never wait on a
// synchronization primitive.  Test is not a superset of Bg or Init: a
// fixture that must drive a background or initialization path
// constructs that context explicitly rather than passing a Test one.
//
// Each context has a private default constructor and befriends the one
// factory, whose constraint admits the context's own passkey and no
// other.  The passkey's own default constructor is private too,
// friended only to the entry points allowed to start a context and to
// the test scaffolding.  A translation unit that holds neither cannot
// forge a context.  Adding a privileged entry point is one friend
// declaration on the relevant passkey.
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

class Init final : public detail::ContextBase<Init, detail::ctx_mint::init_key, Effect::Init, Effect::Alloc, Effect::IO> {
    constexpr Init() noexcept = default;

    template <class Ctx, class Key>
        requires CanMintContext<Ctx, Key>
    friend constexpr Ctx mint_context(Key) noexcept;
};

class Test final
    : public detail::ContextBase<Test, detail::ctx_mint::test_key, Effect::Test, Effect::Alloc, Effect::IO, Effect::Block> {
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

// The family's invariants, read off the roster so that a context is
// pinned the moment it is listed.  Each context is one byte and empty,
// because its atoms are; it is built only through the door; it is
// named after its own atom, which keeps the effect table and the
// context table one table; and no two contexts share a key, so a key
// is the name of the context it mints.
namespace detail {

template <class C>
[[nodiscard]] consteval bool context_invariants_hold_() noexcept {
    static_assert(sizeof(C) == 1, "A context must be 1 byte.  Its capability members are empty and "
                                  "collapse into the object's own byte.");
    static_assert(std::is_empty_v<C>, "A context must be an empty class, so that ExecCtx holds it at no cost.");
    static_assert(std::is_trivially_copyable_v<C>, "A context is passed by value and copied into ExecCtx.");
    static_assert(!std::is_default_constructible_v<C>,
                  "A context's default constructor must stay private.  Build one through a friended entry "
                  "point, or through the test witness.");
    static_assert(effect_name(C::own_effect) == std::meta::identifier_of(^^C), "A context is named after its own atom.");
    return true;
}

[[nodiscard]] consteval bool every_context_invariant_holds_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : context_roster) {
        (void)context_invariants_hold_<typename [:entry:]>();
        template for (constexpr auto other : context_roster) {
            if constexpr (entry != other) {
                static_assert(!std::is_same_v<typename [:entry:]::key_type, typename [:other:]::key_type>,
                              "Two contexts share a passkey, so one key would mint either.");
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_context_invariant_holds_());

}  // namespace detail

static_assert(sizeof(cap::Alloc) == 1);
static_assert(sizeof(cap::IO) == 1);
static_assert(sizeof(cap::Block) == 1);

namespace detail::capabilities_self_test {

static_assert(effect_count == 6, "The Effect catalog has grown or shrunk.  Confirm the change is "
                                 "intended, and check that the name-coverage assertion below still "
                                 "reaches every atom.  CT and Fail are not atoms: the note at the top "
                                 "of this file says where each one lives, and why a row cannot hold it.");

// The walk that used to sit here asked whether any atom reported the
// unknown sentinel.  It policed a hand-written switch.  effect_name now
// reads the enumerator, so that walk answered true by construction and
// is gone.  What replaces it is the stronger statement: each atom
// renders as exactly the identifier it declares, and a value outside
// the enum still reaches the sentinel.
static_assert(effect_name(Effect::Alloc) == "Alloc");
static_assert(effect_name(Effect::IO) == "IO");
static_assert(effect_name(Effect::Block) == "Block");
static_assert(effect_name(Effect::Bg) == "Bg");
static_assert(effect_name(Effect::Init) == "Init");
static_assert(effect_name(Effect::Test) == "Test");
static_assert(effect_name(static_cast<Effect>(200)) == "<unknown Effect>",
              "A value outside the catalog must reach the unknown-atom sentinel, so a corrupt byte "
              "prints as one rather than as an empty name.");

// A new atom takes the next free value and adds one pin below.
static_assert(static_cast<std::uint8_t>(Effect::Alloc) == 0,
              "The value of Effect::Alloc changed, which invalidates every federation cache key that "
              "mentions it.  Restore the value, or run the major-version migration.");
static_assert(static_cast<std::uint8_t>(Effect::IO) == 1,
              "The value of Effect::IO changed, which invalidates federation cache keys.");
static_assert(static_cast<std::uint8_t>(Effect::Block) == 2,
              "The value of Effect::Block changed, which invalidates federation cache keys.");
static_assert(static_cast<std::uint8_t>(Effect::Bg) == 3,
              "The value of Effect::Bg changed, which invalidates federation cache keys.");
static_assert(static_cast<std::uint8_t>(Effect::Init) == 4,
              "The value of Effect::Init changed, which invalidates federation cache keys.");
static_assert(static_cast<std::uint8_t>(Effect::Test) == 5,
              "The value of Effect::Test changed, which invalidates federation cache keys.");

// Widening the underlying type is invisible to the row hash, which
// reads only the value, but it changes the layout of every struct that
// holds an Effect by value.
static_assert(std::is_same_v<std::underlying_type_t<Effect>, std::uint8_t>,
              "The Effect underlying type is no longer uint8_t, which changes the ABI.");

static_assert(IsEffect<Effect::Alloc>);
static_assert(IsEffect<Effect::IO>);
static_assert(IsEffect<Effect::Block>);
static_assert(IsEffect<Effect::Bg>);
static_assert(IsEffect<Effect::Init>);
static_assert(IsEffect<Effect::Test>);

[[nodiscard]] consteval std::size_t count_accepted_effects_() noexcept {
    static constexpr auto enums = std::define_static_array(std::meta::enumerators_of(^^Effect));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enums) {
        constexpr Effect e = [:en:];
        if (IsEffect<e>) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}
static_assert(count_accepted_effects_() == effect_count, "IsEffect rejects an atom that is in the Effect catalog.");

// A cast from an unnamed value is a well-formed Effect, because the
// underlying type admits 0 through 255.  The gate must still reject it.
static_assert(!IsEffect<static_cast<Effect>(99)>);
static_assert(!IsEffect<static_cast<Effect>(255)>);
static_assert(!IsEffect<static_cast<Effect>(6)>);

static_assert(!effect_name(Effect::Alloc).empty());
static_assert(!effect_name(Effect::IO).empty());
static_assert(!effect_name(Effect::Block).empty());
static_assert(!effect_name(Effect::Bg).empty());
static_assert(!effect_name(Effect::Init).empty());
static_assert(!effect_name(Effect::Test).empty());

static_assert(effect_name(Effect::Alloc) != "<unknown Effect>");
static_assert(effect_name(Effect::IO) != "<unknown Effect>");
static_assert(effect_name(Effect::Block) != "<unknown Effect>");
static_assert(effect_name(Effect::Bg) != "<unknown Effect>");
static_assert(effect_name(Effect::Init) != "<unknown Effect>");
static_assert(effect_name(Effect::Test) != "<unknown Effect>");

static_assert(effect_name(Effect::Alloc) != effect_name(Effect::IO));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Block));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Bg));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Init));
static_assert(effect_name(Effect::Alloc) != effect_name(Effect::Test));
static_assert(effect_name(Effect::IO) != effect_name(Effect::Block));
static_assert(effect_name(Effect::Bg) != effect_name(Effect::Init));
static_assert(effect_name(Effect::Init) != effect_name(Effect::Test));

static_assert(std::is_default_constructible_v<cap::Alloc>);
static_assert(std::is_default_constructible_v<cap::IO>);
static_assert(std::is_default_constructible_v<cap::Block>);
static_assert(std::is_trivially_copyable_v<cap::Alloc>);
static_assert(std::is_trivially_copyable_v<cap::IO>);
static_assert(std::is_trivially_copyable_v<cap::Block>);
static_assert(std::is_trivially_destructible_v<cap::Alloc>);
static_assert(std::is_trivially_destructible_v<cap::IO>);
static_assert(std::is_trivially_destructible_v<cap::Block>);

// A throwing default constructor on an atom would also break the
// contexts that hold it and the factories that mint them.  Pinning it
// here names the atom that caused it.
static_assert(noexcept(cap::Alloc{}));
static_assert(noexcept(cap::IO{}));
static_assert(noexcept(cap::Block{}));
static_assert(std::is_nothrow_default_constructible_v<cap::Alloc>);
static_assert(std::is_nothrow_default_constructible_v<cap::IO>);
static_assert(std::is_nothrow_default_constructible_v<cap::Block>);

static_assert(std::is_same_v<Alloc, cap::Alloc>);
static_assert(std::is_same_v<IO, cap::IO>);
static_assert(std::is_same_v<Block, cap::Block>);

// The contexts can only be built through a friended factory, so the
// nothrow property is asserted through the witness that holds a key.
static_assert(noexcept(::foundation::effects::testing::TestWitness::bg()));
static_assert(noexcept(::foundation::effects::testing::TestWitness::init()));
static_assert(noexcept(::foundation::effects::testing::TestWitness::test()));
static_assert(noexcept(::foundation::effects::testing::bg()));
static_assert(noexcept(::foundation::effects::testing::init()));
static_assert(noexcept(::foundation::effects::testing::test()));

}  // namespace detail::capabilities_self_test

}  // namespace foundation::effects
