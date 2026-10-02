// The compile-time checks of foundation/effects/Effect.h.

#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>

namespace foundation::effects {

namespace detail {

// `effect_count` counts enumerator NAMES.  Two names can still share
// one underlying value, which would raise the count while leaving the
// atoms indistinguishable as bit positions: rows claiming one atom
// would silently satisfy a gate that demands the other, and two
// federation cache keys would collide.  This witness tracks each
// observed value in a bitmask and refuses a repeat.  The first check
// below calls it.
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

// Instantiating the classifier for every atom catches a contributor
// who adds case arms whose default semantics disagree with the rest.
// It does not catch an unclassified atom on its own.  The pin of
// effect_count below does that.
consteval bool every_effect_observability_classified_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
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
    static_assert(effect_name(C::own_effect) == std::meta::identifier_of(^^C),
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

static_assert(effect_count == std::meta::enumerators_of(^^Effect).size(),
              "effect_count is a literal count of the enumerators of Effect, so that no includer of the header "
              "walks the enum.  A new Effect enumerator needs a deliberate observable-or-not decision in "
              "is_observable_effect_atom_, a name pin and a value pin below, and this count raised to match.  "
              "CT and Fail are not atoms: the note at the top of the header says where each one lives, and "
              "why a row cannot hold it.");

static_assert(every_effect_observability_classified_(),
              "Every Effect atom must reach a case arm of is_observable_effect_atom_.");

}  // namespace detail

static_assert(std::is_empty_v<cap::Alloc>,
              "cap::Alloc must remain a stateless empty struct.  Bg, Init and Test expose public fields of "
              "this type, and a caller can copy one out.  Either privatize those fields behind a friended "
              "accessor, or carry the state on the linear capability token instead.");
static_assert(std::is_empty_v<cap::IO>, "cap::IO must remain stateless.  Bg, Init and Test expose a public "
                                        "field of this type.");
static_assert(std::is_empty_v<cap::Block>, "cap::Block must remain stateless.  Bg, Init and Test expose a "
                                           "public field of this type.");

namespace detail {

static_assert(every_context_invariant_holds_());

}  // namespace detail

static_assert(sizeof(cap::Alloc) == 1);
static_assert(sizeof(cap::IO) == 1);
static_assert(sizeof(cap::Block) == 1);

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
