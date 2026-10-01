// The compile-time checks of foundation/effects/Effect.h.

#include <foundation/effects/Effect.h>

namespace foundation::effects {

static_assert(detail::every_effect_underlying_distinct_(),
              "Two Effect enumerators share an underlying value, or one is >= 64 and so exceeds the "
              "uint64_t row-mask carrier.  Each atom must occupy a distinct bit position below 64.  "
              "Duplicates collapse two atoms into one row bit and make federation cache keys collide.  "
              "Give the new atom the next free underlying value explicitly in the enum.");

namespace detail {

static_assert(effect_count == 6, "A new Effect enumerator needs a deliberate observable-or-not decision in "
                                 "is_observable_effect_atom_ below, and this count raised to match.");

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

static_assert(effect_count == 6, "The Effect catalog has grown or shrunk.  Confirm the change is "
                                 "intended, and check that the name-coverage assertion below still "
                                 "reaches every atom.  CT and Fail are not atoms: the note at the top "
                                 "of this file says where each one lives, and why a row cannot hold it.");

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
