// The compile-time checks of foundation/effects/Effect.h.

#include <foundation/effects/Effect.h>

#include <foundation/reflect/ReflectionList.h>

#include <cstdint>
#include <type_traits>

namespace foundation::effects {

namespace detail {

// `effect_count` counts enumerator names.  Two names can still share
// one underlying value, which would raise the count while leaving the
// atoms indistinguishable as bit positions: rows claiming one atom
// would silently satisfy a gate that demands the other, and two
// federation cache keys would collide.  This witness tracks each
// observed value in a bitmask and refuses a repeat.  The first check
// below calls it.
[[nodiscard]] consteval bool every_effect_underlying_distinct_() noexcept {
    static constexpr ::foundation::reflect::ReflectionList enumerators =
        ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^Effect));
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

// Instantiating the classifier for every atom catches a contributor
// who adds case arms whose default semantics disagree with the rest.
// It does not catch an unclassified atom on its own.  The pin of
// effect_count below does that.
consteval bool every_effect_observability_classified_() noexcept {
    static constexpr ::foundation::reflect::ReflectionList enumerators =
        ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^Effect));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) { (void)is_observable_effect_atom_<([:en:])>(); }
#pragma GCC diagnostic pop
    return true;
}

// The invariants of the family, read off the roster so that a context
// is pinned the moment it is listed.  A check below calls the walk.
// Each context obeys these rules:
//
//   - It is one byte and empty, because its atoms are.
//   - It is built only through the door.
//   - No route builds it or its key without a constructor.
//   - It has the name of its own atom, so the effect table and the
//     context table are one table.
//   - It shares its key with no other context, so a key is the name of
//     the context it mints.
template <class C>
[[nodiscard]] consteval bool context_invariants_hold_() noexcept {
    static_assert(sizeof(C) == 1, "A context must be 1 byte.  Its capability members are empty and "
                                  "collapse into the object's own byte.");
    static_assert(std::is_empty_v<C>, "A context must be an empty class, so that ExecCtx holds it at no cost.");
    static_assert(std::is_nothrow_copy_constructible_v<C> && std::is_trivially_destructible_v<C>,
                  "A context is passed by value and copied into ExecCtx, so its copy must not throw and its "
                  "destructor must stay trivial.");
    static_assert(!std::is_trivially_copyable_v<C> && !std::is_implicit_lifetime_v<C>,
                  "A context must have no trivial constructor.  A trivially copyable context is built by "
                  "std::bit_cast from a byte, and an implicit-lifetime context by std::start_lifetime_as over "
                  "a buffer.  Keep the constructors of ContextBase user-provided.");
    static_assert(!std::is_trivially_copyable_v<typename C::key_type>
                      && !std::is_implicit_lifetime_v<typename C::key_type>,
                  "A context key must have no trivial constructor, or std::bit_cast and std::start_lifetime_as "
                  "build the key that mints the context.  Keep the constructors of the key user-provided.");
    static_assert(!std::is_default_constructible_v<C>,
                  "A context's default constructor must stay private.  Build one through a friended entry "
                  "point, or through the test witness.");
    static_assert(effect_name(C::own_effect) == ::foundation::core::identifier_text(^^C),
                  "A context is named after its own atom.");
    return true;
}

[[nodiscard]] consteval bool every_context_invariant_holds_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : context_roster) {
        (void)context_invariants_hold_<typename[:entry:]>();
        template for (constexpr auto other : context_roster) {
            if constexpr (entry != other) {
                static_assert(!std::is_same_v<typename[:entry:] ::key_type, typename[:other:] ::key_type>,
                              "Two contexts share a passkey, so one key would mint either.");
            }
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

namespace detail {

static_assert(effect_count == ::foundation::reflect::enum_count<Effect>,
              "effect_count is a literal count of the enumerators of Effect, so that no includer of the header "
              "walks the enum.  A new Effect enumerator needs a deliberate observable-or-not decision in "
              "is_observable_effect_atom_, a name pin and a value pin below, and this count raised to match.  "
              "CT and Fail are not atoms: the note at the top of the header says where each one lives, and "
              "why a row cannot hold it.");

static_assert(every_effect_observability_classified_(),
              "Every Effect atom must reach a case arm of is_observable_effect_atom_.");

}  // namespace detail

static_assert(std::is_empty_v<cap::Alloc>,
              "cap::Alloc must remain an empty class.  Bg, Init and Test hold it as a field with no storage, "
              "and a caller copies it out.  Carry state on the capability token instead.");
static_assert(std::is_empty_v<cap::IO>, "cap::IO must remain empty.  Bg, Init and Test hold it as a field.");
static_assert(std::is_empty_v<cap::Block>, "cap::Block must remain empty.  Bg, Init and Test hold it as a field.");
static_assert(std::is_empty_v<cap::Nondet>, "cap::Nondet must remain empty.  Bg, Init and Test hold it as a field.");

namespace detail {

static_assert(every_context_invariant_holds_());

}  // namespace detail

static_assert(sizeof(cap::Alloc) == 1);
static_assert(sizeof(cap::IO) == 1);
static_assert(sizeof(cap::Block) == 1);
static_assert(sizeof(cap::Nondet) == 1);

// Nondet is an atom of input.  A ghost binding that only reads a clock
// changes nothing outside the process, so elision can drop it.
static_assert(!is_observable<Effect::Nondet>());
static_assert(is_observable<Effect::IO>());

// Div is observable: a drop of a body that can run with no end changes
// whether the program ends.  It has no token, so no door takes a bare tag
// of it.
static_assert(is_observable<Effect::Div>());
static_assert(!HasBareTag<Effect::Div>);

// Each context holds the bare tag of Nondet, so a context of a full row
// lends it, and a door that takes the tag takes it from such a context.
static_assert(std::is_same_v<decltype(testing::bg().nondet), cap::Nondet>);
static_assert(std::is_same_v<decltype(testing::init().nondet), cap::Nondet>);
static_assert(std::is_same_v<decltype(testing::test().nondet), cap::Nondet>);
static_assert(std::is_same_v<bare_tag_t<Effect::Nondet>, cap::Nondet>);

namespace detail::capabilities_self_test {

// effect_name reads the enumerator by reflection.  Each atom renders as
// exactly the identifier it declares, and a value outside the enum
// reaches the sentinel.
static_assert(effect_name(Effect::Alloc) == "Alloc");
static_assert(effect_name(Effect::IO) == "IO");
static_assert(effect_name(Effect::Block) == "Block");
static_assert(effect_name(Effect::Bg) == "Bg");
static_assert(effect_name(Effect::Init) == "Init");
static_assert(effect_name(Effect::Test) == "Test");
static_assert(effect_name(Effect::Nondet) == "Nondet");
static_assert(effect_name(Effect::Div) == "Div");
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
static_assert(static_cast<std::uint8_t>(Effect::Nondet) == 6,
              "The value of Effect::Nondet changed, which invalidates federation cache keys.");
static_assert(Effect::Div == Effect{7}, "The value of Effect::Div changed, which invalidates federation cache keys.");

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
static_assert(IsEffect<Effect::Nondet>);
static_assert(IsEffect<Effect::Div>);

[[nodiscard]] consteval std::size_t count_accepted_effects_() noexcept {
    static constexpr ::foundation::reflect::ReflectionList enums =
        ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^Effect));
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
static_assert(!IsEffect<static_cast<Effect>(8)>);

static_assert(effect_name(Effect::Alloc).size() != 0);
static_assert(effect_name(Effect::IO).size() != 0);
static_assert(effect_name(Effect::Block).size() != 0);
static_assert(effect_name(Effect::Bg).size() != 0);
static_assert(effect_name(Effect::Init).size() != 0);
static_assert(effect_name(Effect::Test).size() != 0);

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

// An atom is an authority, so no code outside its owner builds one.  No
// route builds one with no constructor either: an atom is not trivially
// copyable, so std::bit_cast refuses it, and it is not an implicit-lifetime
// type, so std::start_lifetime_as refuses it.  The copy stays public.
static_assert(!std::is_default_constructible_v<cap::Alloc>);
static_assert(!std::is_default_constructible_v<cap::IO>);
static_assert(!std::is_default_constructible_v<cap::Block>);
static_assert(!std::is_default_constructible_v<cap::Nondet>);
static_assert(!std::is_trivially_copyable_v<cap::Nondet> && !std::is_implicit_lifetime_v<cap::Nondet>);
static_assert(!std::is_trivially_copyable_v<cap::Alloc> && !std::is_implicit_lifetime_v<cap::Alloc>);
static_assert(!std::is_trivially_copyable_v<cap::IO> && !std::is_implicit_lifetime_v<cap::IO>);
static_assert(!std::is_trivially_copyable_v<cap::Block> && !std::is_implicit_lifetime_v<cap::Block>);
static_assert(std::is_trivially_destructible_v<cap::Alloc>);
static_assert(std::is_trivially_destructible_v<cap::IO>);
static_assert(std::is_trivially_destructible_v<cap::Block>);
static_assert(std::is_nothrow_copy_constructible_v<cap::Alloc> && std::is_nothrow_copy_assignable_v<cap::Alloc>);
static_assert(std::is_nothrow_copy_constructible_v<cap::IO> && std::is_nothrow_copy_assignable_v<cap::IO>);
static_assert(std::is_nothrow_copy_constructible_v<cap::Block> && std::is_nothrow_copy_assignable_v<cap::Block>);

// The owner of the atoms has no public constructor and no public copy, so
// only its friends hold one.
static_assert(!std::is_default_constructible_v<host::AtomOwner>);
static_assert(!std::is_copy_constructible_v<host::AtomOwner>);
static_assert(!std::is_trivially_copyable_v<host::AtomOwner> && !std::is_implicit_lifetime_v<host::AtomOwner>);

// A throwing atom would also break the contexts that hold it and the
// factories that mint them.  The mint of a context pins its own noexcept,
// and this pin names the context that holds the atoms.
static_assert(noexcept(testing::test()));
static_assert(noexcept(testing::bg()));
static_assert(noexcept(testing::init()));

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
