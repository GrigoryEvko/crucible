// The compile-time checks of fixy/Reject.h: what each gate admits and
// what it refuses.

#include <fixy/Reject.h>

namespace fixy {

namespace detail::reject_self_test {

struct not_an_atom {};

static_assert(IsAcceptedPayload<int>);
static_assert(IsAcceptedPayload<std::string>);
static_assert(!IsAcceptedPayload<void>);
static_assert(!IsAcceptedPayload<int[3]>);
static_assert(!IsAcceptedPayload<int&>);
static_assert(!IsAcceptedPayload<const int>);
static_assert(!IsAcceptedPayload<volatile int>);
static_assert(!IsAcceptedPayload<int()>);

// An empty pack is well formed and unique: it names no axis twice
// because it names no axis at all.
static_assert(AllAtomsWellFormed<>);
static_assert(UniqueAtomPerAxis<>);
static_assert(IsAccepted<int>);

static_assert(!AllAtomsWellFormed<not_an_atom>);
static_assert(!IsAccepted<int, not_an_atom>);
static_assert(!IsAccepted<void>);

// Tier 5 reaches the gate: a pack whose atoms sit on different axes,
// so tier 4 admits it, and which a collision rule still refuses.  Each
// half alone is accepted, which is what says the rule and not the tier
// is what refused the pair.
static_assert(IsAccepted<int, ::fixy::atom::borrow>);
static_assert(IsAccepted<int, ::fixy::atom::coroutine>);
static_assert(!IsAccepted<int, ::fixy::atom::borrow, ::fixy::atom::coroutine>, "R002 and L002 refuse this pair");
static_assert(!IsAccepted<int, ::fixy::atom::capability_usage, ::fixy::atom::trust_unverified>, "T001 refuses it");

// The corpus reaches the gate through the same tier.  An IO row with
// no Security atom is a classified value on an observable channel, and
// the pack that names the public grade is the same binding admitted.
// No collision rule reads this pair, so the refusal is the corpus's.
static_assert(!IsAccepted<int, ::fixy::atom::with_io>, "classified_io_without_declassify refuses this pack");
static_assert(IsAccepted<int, ::fixy::atom::with_io, ::fixy::atom::as_public>);
static_assert(
    std::is_same_v<corpus_tag_or_void_t<int, ::fixy::atom::with_io>, ::fixy::corpus::classified_io_without_declassify>);
static_assert(std::is_same_v<corpus_tag_or_void_t<int, ::fixy::atom::with_io, ::fixy::atom::as_public>, void>);

// The tier-5 message names the corpus entry that refused the pack, the
// collision rules that refused it by code, or both.
static_assert(::fixy::detail::text_contains(detail::reject::tier5_message_<int, ::fixy::atom::with_io>(),
                                            "classified_io_without_declassify"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::borrow, ::fixy::atom::coroutine>(), "L002"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::borrow, ::fixy::atom::coroutine>(), "R002"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::ghost, ::fixy::atom::as_public, ::fixy::atom::with_alloc>(),
    "ghost_runtime_observable"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::ghost, ::fixy::atom::as_public, ::fixy::atom::with_alloc>(),
    "P010"));

// On a pack an earlier tier refuses, the message says so rather than
// consulting the corpus and the rules.  fn instantiates it whatever the
// tier-5 condition concluded, and both of those walks read each atom's
// axis, which is a hard error on a non-atom rather than a false.
static_assert(::fixy::detail::text_contains(detail::reject::tier5_message_<int, not_an_atom>(), "not reached"));
static_assert(::fixy::detail::text_contains(detail::reject::tier5_message_<void>(), "not reached"));

// The tier-2 message names the entry, and for an entry with the shape of
// an atom it names the read that refuses it.
struct shaped_outside_the_catalog final : ::fixy::atom::atom_of<Axis::Usage> {};
static_assert(::fixy::detail::text_contains(detail::reject::tier2_message_<::fixy::atom::copy, not_an_atom>(),
                                            "not_an_atom"));
static_assert(::fixy::detail::text_contains(detail::reject::tier2_message_<shaped_outside_the_catalog>(),
                                            "refused because it is not declared directly in fixy::atom"));
static_assert(::fixy::detail::text_contains(detail::reject::tier2_message_<::fixy::atom::copy>(), "not reached"));
static_assert(::fixy::detail::text_contains(detail::reject::tier2_message_<>(), "not reached"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::copy, ::fixy::atom::affine>(), "tier 4 refused"));

// The tier-4 message names the axis, not just the template that names
// it.  Two packs on two different axes, so a message that spelled one
// axis unconditionally would fail here.
static_assert(::fixy::detail::text_contains(detail::reject::tier4_message_<::fixy::atom::copy, ::fixy::atom::affine>(),
                                            "Usage"));
static_assert(::fixy::detail::text_contains(
    detail::reject::tier4_message_<::fixy::atom::mut_append, ::fixy::atom::mut_monotonic>(), "Mutation"));
// And it says so rather than naming an axis when no axis is doubled.
static_assert(::fixy::detail::text_contains(detail::reject::tier4_message_<>(), "not reached"));
static_assert(::fixy::detail::text_contains(detail::reject::tier4_message_<::fixy::atom::copy>(), "not reached"));

// Which tier refuses which pack, and the tag each selects.  One tier
// fires per pack, so one message reaches the reader.
static_assert(detail::reject::first_failing_tier_<int>() == detail::reject::Tier::Ok);
static_assert(detail::reject::first_failing_tier_<void>() == detail::reject::Tier::Payload);
static_assert(detail::reject::first_failing_tier_<int, not_an_atom>() == detail::reject::Tier::Malformed);
static_assert(detail::reject::first_failing_tier_<int, ::fixy::atom::copy, ::fixy::atom::affine>()
              == detail::reject::Tier::Duplicate);
static_assert(detail::reject::first_failing_tier_<int, ::fixy::atom::with_io>() == detail::reject::Tier::Composition);
static_assert(detail::reject::first_failing_tier_<int, ::fixy::atom::borrow, ::fixy::atom::coroutine>()
              == detail::reject::Tier::Composition);

// A pack that trips an earlier tier AND would trip a later one reports
// the earlier: a non-atom beside two atoms on one axis is malformed,
// not duplicated.
static_assert(detail::reject::first_failing_tier_<int, not_an_atom, ::fixy::atom::copy, ::fixy::atom::affine>()
              == detail::reject::Tier::Malformed);
static_assert(detail::reject::first_failing_tier_<void, not_an_atom>() == detail::reject::Tier::Payload);

// The duplicate walk answers with an axis, and the tag it selects names
// that axis.  Two atoms on one axis is the case; two atoms on two axes
// is not.
static_assert(detail::reject::first_duplicated_axis_<>() == ::fixy::axis_count);
static_assert(std::is_same_v<duplicate_tag_or_void_t<>, void>);
static_assert(std::is_same_v<malformed_tag_or_void_t<>, void>);
static_assert(std::is_same_v<malformed_tag_or_void_t<not_an_atom>, malformed_atom<not_an_atom>>);
static_assert(std::is_same_v<payload_tag_or_void_t<int>, void>);
static_assert(std::is_same_v<payload_tag_or_void_t<void>, unholdable_payload<void>>);

// The generated names carry the axis identifier, so an axis renamed in
// the enum renames its tag with it.
static_assert(duplicate_atom_on<Axis::Usage>::name == "DuplicateAtomOnUsage");
static_assert(duplicate_atom_on<Axis::MemoryScope>::name == "DuplicateAtomOnMemoryScope");
static_assert(duplicate_atom_on<Axis::Usage>::name != duplicate_atom_on<Axis::Effect>::name);
static_assert(!duplicate_atom_on<Axis::Usage>::description.empty());
static_assert(!duplicate_atom_on<Axis::Usage>::remediation.empty());

// Every generated tag is a diagnostic tag, checked across the whole
// enum rather than on a sample, so an axis added later is covered.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval bool every_axis_has_a_duplicate_tag() noexcept {
    bool all_tagged = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_tagged = all_tagged && ::foundation::diag::is_diagnostic_class_v<duplicate_atom_on<axis>>
                  && !duplicate_atom_on<axis>::name.empty();
    }
    return all_tagged;
}
#pragma GCC diagnostic pop
static_assert(every_axis_has_a_duplicate_tag());

static_assert(::foundation::diag::is_diagnostic_class_v<malformed_atom<not_an_atom>>);
static_assert(::foundation::diag::is_diagnostic_class_v<unholdable_payload<void>>);

}  // namespace detail::reject_self_test

}  // namespace fixy
