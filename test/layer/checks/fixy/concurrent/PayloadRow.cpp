// The compile-time checks of fixy/concurrent/PayloadRow.h.

#include <fixy/concurrent/PayloadRow.h>

namespace fixy::concurrent {

namespace detail {

static_assert(rosters_are_disjoint(), "A payload family is on two of the three rosters, so the order in which "
                                      "payload_row checks them decides the answer.  Each family belongs to "
                                      "exactly one: it carries a row, it hides a payload, or it is a leaf.");

}  // namespace detail

namespace detail::payload_row_self_test {

namespace eff = ::foundation::effects;

// ── leaves by construction: not specializations ──────────────────────
static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<double>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<char>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<void*>, eff::Row<>>);

struct UserPod {
    int x = 0;
    double y = 0.0;
};
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

// ── a computation carries its own row ───────────────────────────────
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<>, int>>, eff::Row<>>);
static_assert(
    std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, int>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Alloc, eff::Effect::IO>, int>>,
                             eff::Row<eff::Effect::Alloc, eff::Effect::IO>>);

// ── a capability conveys the effect ─────────────────────────────────
static_assert(
    std::is_same_v<payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>>, eff::Row<eff::Effect::Alloc>>);
static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>>, eff::Row<eff::Effect::IO>>);

// ── a transparent wrapper reports what it hides ─────────────────────
//
// One Graded entry covers every band, so no band needs an arm of its
// own.
using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

// A FixedArray reports the row of its element, also with a key brand (the
// loan key of foundation/core/Frame.h).
struct PayloadKeyBrand final {};
static_assert(std::is_same_v<payload_row_t<::foundation::core::FixedArray<BgComp, 2>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::foundation::core::FixedArray<BgComp, 2, PayloadKeyBrand>>,
                             eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>, eff::Row<>>);
static_assert(
    std::is_same_v<payload_row_t<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, BgComp>>, eff::Row<eff::Effect::Bg>>,
    "A band over an engaged computation must report the computation's row.  A band that reported "
    "the empty row would hide the effect it wraps.");
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::fixy::Stale<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<int>>, eff::Row<>>);

// A stack of wrappers reports the row at the bottom, however deep.
static_assert(std::is_same_v<payload_row_t<::fixy::Secret<::fixy::Stale<BgComp>>>, eff::Row<eff::Effect::Bg>>);
static_assert(
    std::is_same_v<payload_row_t<::fixy::Stale<::fixy::Secret<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, BgComp>>>>,
                   eff::Row<eff::Effect::Bg>>);

// ── an Option hides its payload, and refuses an address ─────────────
//
// An Option holds its payload by value, so an Option of an owner answers for
// what the owner holds.  An Option of an address is refused.  An Option of a
// borrow of the base is refused where the walk reaches the borrow, whose
// family is on no roster.
static_assert(std::is_same_v<payload_row_t<::foundation::core::Option<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::foundation::core::Option<::foundation::core::Box<BgComp>>>,
                             eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::foundation::core::Option<int>>, eff::Row<>>);
static_assert(!payload_row_is_classified<::foundation::core::Option<int const*>>(),
              "an Option of an address holds a borrow of an object that no part of the payload owns");
struct borrowed_brand {};
static_assert(!payload_row_is_classified<::foundation::core::Option<::foundation::core::Ref<int, borrowed_brand>>>(),
              "an Option of a Ref holds a borrow of an object that no part of the payload owns");

// ── a run of an arena hides its elements, as a RunSet does ──────────
//
// A run reports the row of its element.  It opens the walk no wider than a
// RunSet: the walk refuses an element that it refuses alone, such as a
// borrow, also through a run and through a class that holds the run, as a
// BuiltRegion holds its runs.
struct run_brand {};
static_assert(
    std::is_same_v<payload_row_t<::foundation::core::ArenaRun<BgComp, run_brand>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<::foundation::core::ArenaRun<int const, run_brand>>, eff::Row<>>);
using RunOfBorrows = ::foundation::core::ArenaRun<::foundation::core::Ref<int, run_brand>, run_brand>;
static_assert(!payload_row_is_classified<RunOfBorrows>(),
              "a run of Refs holds borrows of objects that no part of the payload owns");
struct HoldsRunOfBorrows {
    RunOfBorrows run{};
};
static_assert(!payload_row_is_classified<HoldsRunOfBorrows>(),
              "a class that holds a run of Refs holds borrows of objects that no part of the payload owns");

// ── a rostered leaf reports nothing, and says so deliberately ───────
static_assert(std::is_same_v<payload_row_t<::fixy::Saturated<unsigned>>, eff::Row<>>);

// A packed word holds one unsigned word, and its field tags are names only.
struct LowField final {};
struct HighField final {};
using TwoFieldWord = ::foundation::core::PackedWord<unsigned short, ::foundation::core::BitField<LowField, 4>,
                                                    ::foundation::core::BitField<HighField, 4>>;
static_assert(std::is_same_v<payload_row_t<TwoFieldWord>, eff::Row<>>);
static_assert(payload_row_is_classified<TwoFieldWord>());

// A plain class that holds a packed word answers for the word.
struct HoldsPackedWord {
    TwoFieldWord word{};
};
static_assert(std::is_same_v<payload_row_t<HoldsPackedWord>, eff::Row<>>);

// ── the gate itself ─────────────────────────────────────────────────
//
// What the gate admits, and what it refuses.  The refusals are the
// property the roster exists for: the primary template is not an answer.
static_assert(payload_row_is_classified<int>());
static_assert(payload_row_is_classified<UserPod>());
static_assert(payload_row_is_classified<eff::Computation<eff::Row<>, int>>());
static_assert(payload_row_is_classified<eff::Capability<eff::Effect::Alloc, eff::Bg>>());
static_assert(payload_row_is_classified<::fixy::Secret<int>>());
static_assert(payload_row_is_classified<::fixy::Saturated<unsigned>>());

// ── a plain class answers for what it holds ─────────────────────────
struct HoldsEngaged {
    int tag = 0;
    BgComp held;
};
struct DerivesEngaged : BgComp {};
struct PointsAtCapability {
    eff::Capability<eff::Effect::IO, eff::Init> const* held = nullptr;
};
struct HoldsTwoRows {
    HoldsEngaged first;
    ::fixy::Secret<eff::Capability<eff::Effect::Alloc, eff::Bg>> second[2];
};
struct SelfReferential {
    SelfReferential* next = nullptr;
    int value = 0;
};
static_assert(std::is_same_v<payload_row_t<HoldsEngaged>, eff::Row<eff::Effect::Bg>>,
              "A member's row is the class's row.  The empty row here is the fail-open answer this walk "
              "replaced.");
static_assert(std::is_same_v<payload_row_t<DerivesEngaged>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<PointsAtCapability>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<HoldsTwoRows>, eff::Row<eff::Effect::Alloc, eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<SelfReferential>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>*>, eff::Row<eff::Effect::IO>>);

// A carrier at the empty row still conveys what its payload conveys.
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<>, eff::Capability<eff::Effect::IO, eff::Bg>>>,
                             eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, HoldsEngaged>>,
                             eff::Row<eff::Effect::Bg>>);

// ── what the walk refuses outside the rosters ───────────────────────
struct OnlyDeclared;
static_assert(!payload_row_is_classified<OnlyDeclared*>(),
              "a class that is only declared cannot say what it holds, so a pointer to it is refused.");
struct HoldsOnlyAnUnnamedBitField {
    unsigned : 8;
};
static_assert(!payload_row_is_classified<HoldsOnlyAnUnnamedBitField>(),
              "a class with state that the walk cannot read is refused.");

// A lambda is a class whose captures are its members, so it conveys the row
// of what it captures.
inline constexpr auto captures_state = [held = 7] { return held; };
inline constexpr auto captures_nothing = [] { return 7; };
inline auto capture_capability(eff::Capability<eff::Effect::IO, eff::Bg>&& capability) {
    return [held = std::move(capability)] { static_cast<void>(held); };
}
static_assert(std::is_same_v<payload_row_t<decltype(captures_state)>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<decltype(captures_nothing)>, eff::Row<>>);
static_assert(std::is_same_v<
              payload_row_t<decltype(capture_capability(std::declval<eff::Capability<eff::Effect::IO, eff::Bg>&&>()))>,
              eff::Row<eff::Effect::IO>>);

// A template the rosters do not name is refused, whatever it holds.
template <class T>
struct UnclassifiedWrapper {
    using value_type = T;
};
static_assert(!payload_row_is_classified<UnclassifiedWrapper<int>>(),
              "A class template on none of the three rosters must be refused.  Admitting it as a leaf is "
              "the fail-open shape: it would report the empty row and satisfy every context, including "
              "when it hides an engaged computation.");
struct HoldsUnclassified {
    UnclassifiedWrapper<int> held;
};
static_assert(!payload_row_is_classified<HoldsUnclassified>(),
              "An unclassified wrapper in a member is refused as it is at the root.");
static_assert(!payload_row_is_classified<UnclassifiedWrapper<BgComp>>(),
              "The refusal must not depend on what the unclassified wrapper holds.  A wrapper hiding an "
              "engaged computation is the case that matters, and it is refused for the same reason as one "
              "hiding an int: nothing said what it hides.");

// ── a family with a rule of a layer above ───────────────────────────
//
// The rule names argument 0.  The walk reads it, and it does not read
// argument 1, which the rosters would refuse.
template <class Carried, class Named>
struct LayerCarrier {};
inline constexpr payload_family_rule stand_in_layer_rules[] = {
    {.family = ^^LayerCarrier, .carried_arguments = std::uint64_t{1}},
};
static_assert(std::is_same_v<payload_row_under_t<LayerCarrier<BgComp, UnclassifiedWrapper<int>>, stand_in_layer_rules>,
                             eff::Row<eff::Effect::Bg>>,
              "A family with a rule reports the row of the arguments that the rule names, and no other.");
static_assert(std::is_same_v<payload_row_under_t<HoldsTwoRows, stand_in_layer_rules>, payload_row_t<HoldsTwoRows>>,
              "A rule of a layer does not change the answer for a type that the rosters classify.");
static_assert(detail::walk_payload_row(^^LayerCarrier<int, int>).is_refused,
              "Without the rule of its layer, the family is on no roster, and the walk refuses it.");
inline constexpr payload_family_rule overlapping_layer_rules[] = {
    {.family = ^^::fixy::Secret, .carried_arguments = std::uint64_t{0}},
};
static_assert(!detail::layer_rules_are_disjoint(overlapping_layer_rules),
              "A rule that names a rostered family would change an answer of the rosters, so it is refused.");

}  // namespace detail::payload_row_self_test

}  // namespace fixy::concurrent
