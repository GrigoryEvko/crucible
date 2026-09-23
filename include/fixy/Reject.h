#pragma once

// What fixy::fn refuses, and what it says when it refuses.
//
// The gate has four structural questions, asked in this order, because
// a later answer is meaningless once an earlier one is no:
//
//   1. Is the payload an object type a wrapper can hold?
//   2. Is every entry in the pack an atom?
//   3. Does each axis carry at most one atom?
//   4. Is the combination admitted?  (Collision.h rules, Corpus.h entries.)
//
// Each question is one concept here.  fixy/Fn.h asks them in the class
// body, one tier per question, and each tier silences itself when an
// earlier tier already failed, so a reader sees one message and not a
// cascade.
//
// The diagnostics are class templates, not 66 hand-stamped classes.
// The old tree wrote one FixyNotEngaged_* and one FixyDuplicate_* tag
// per axis, so adding an axis meant editing two more places and the
// cardinality pins could not tell a missing tag from a renamed one.  A
// tag parameterised on the axis is generated the moment the axis is
// declared, and its name is the axis identifier read by reflection.
//
// These tags derive foundation::diag::tag_base, which is what makes
// them printable through the diagnostic surface.  They deliberately do
// NOT join foundation::diag::Category: that enum is append-only and its
// ordinals are pinned into federation cache keys, so a per-axis family
// has no business there.
//
// Old spelling: include/crucible/fixy/Reject.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fixy {

// ---------------------------------------------------------------------
// Tier 1: the payload shape.

// A wrapper holds the payload by value, so the payload has to be an
// object type.  void has no storage, an array decays and cannot be
// returned, a reference is not an object, a function is not an object,
// and a cv-qualified payload would make the grade describe a different
// type from the one the caller wrote.  Each is refused rather than
// adjusted, because adjusting one silently changes what the binding
// says about the value.
template <class T>
concept IsAcceptedPayload = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_reference_v<T>
                         && !std::is_function_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

// ---------------------------------------------------------------------
// Tier 2: every pack entry is an atom.

template <class... Atoms>
concept AllAtomsWellFormed = (::fixy::atom::IsAtom<Atoms> && ...);

// ---------------------------------------------------------------------
// Tier 4: at most one atom per axis.
//
// There is no tier 3.  An axis the pack says nothing about resolves to
// its strict pole, silently, so a missing atom is not a rejection and
// needs no check.  The old tree had a tier here that demanded an
// explicit accept_default_strict_for marker on every unmentioned axis;
// with 33 axes that made the common binding unwritable.

namespace detail::reject {

// An expansion statement declares its variable once per expansion, and
// each declaration shadows the one before it.  The warning is correct
// about the shape and wrong about the risk: the walks below read the
// binding and never the outer one.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The pack as reflections.  define_static_array gives each element a
// constant address, which `template for` needs to bind it as constexpr
// and a splice needs to name its type.  An empty pack yields an empty
// array and every walk below iterates zero times.
template <class... Atoms>
[[nodiscard]] consteval auto pack_entries_() {
    return std::define_static_array(std::vector<std::meta::info>{^^Atoms...});
}

// How many atoms in the pack sit on one axis.  Counting rather than
// testing for a second lets the duplicate diagnostic say how many.
template <Axis A, class... Atoms>
[[nodiscard]] consteval std::size_t count_on_axis_() noexcept {
    std::size_t count = 0;
    static constexpr auto entries = pack_entries_<Atoms...>();
    template for (constexpr auto entry : entries) {
        using Candidate = [:entry:];
        if constexpr (Candidate::axis == A) {
            ++count;
        }
    }
    return count;
}

// The first axis carrying more than one atom, or axis_count when every
// axis carries at most one.  The walk is over the axes rather than over
// the pack, so the answer is an axis and the diagnostic can name it.
template <class... Atoms>
[[nodiscard]] consteval std::size_t first_duplicated_axis_() noexcept {
    std::size_t offender = ::fixy::axis_count;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^Axis))) {
        constexpr Axis axis = [:axis_member:];
        if (offender == ::fixy::axis_count && count_on_axis_<axis, Atoms...>() > 1) {
            offender = static_cast<std::size_t>(std::to_underlying(axis));
        }
    }
    return offender;
}

// The first pack entry that is not an atom, as a reflection, or the
// reflection of void when every entry is one.  void is not an atom and
// is not nameable as one, so it cannot collide with a real answer.
template <class... Atoms>
[[nodiscard]] consteval std::meta::info first_malformed_atom_() noexcept {
    std::meta::info offender = ^^void;
    static constexpr auto entries = pack_entries_<Atoms...>();
    template for (constexpr auto entry : entries) {
        using Candidate = [:entry:];
        if constexpr (!::fixy::atom::IsAtom<Candidate>) {
            if (offender == ^^void) {
                offender = entry;
            }
        }
    }
    return offender;
}

#pragma GCC diagnostic pop

}  // namespace detail::reject

template <class... Atoms>
concept UniqueAtomPerAxis = (detail::reject::first_duplicated_axis_<Atoms...>() == ::fixy::axis_count);

// ---------------------------------------------------------------------
// Tier 5: the collision rules and the corpus.
//
// The collision rules, folded in fixy/Collision.h.  A rule reads the
// payload and the pack and nothing else, which is why this delegates
// those two rather than the fn: a rule that completed fn would recurse
// through fn's own assertion of this concept.  The payload is read for
// one premise only, the DetSafe band's replay claim, and it is the fn's
// first template parameter rather than a member of the fn.
template <class T, class... Atoms>
concept ValidComposition = ::fixy::collision::rules_of<T, Atoms...>::valid;

// The refused-combination corpus, folded in fixy/Corpus.h.  An entry
// reads the same resolved grades a rule does, and never fn, for the
// same reason.
template <class T, class... Atoms>
concept NotInCorpus = !::fixy::corpus::is_in_corpus_v<T, Atoms...>;

// ---------------------------------------------------------------------
// The whole gate.  This is what the negative corpus asserts against and
// what the mint factories put in their signatures.

template <class T, class... Atoms>
concept IsAccepted = IsAcceptedPayload<T> && AllAtomsWellFormed<Atoms...> && UniqueAtomPerAxis<Atoms...>
                  && NotInCorpus<T, Atoms...> && ValidComposition<T, Atoms...>;

// ---------------------------------------------------------------------
// The diagnostics.

namespace detail::reject {

template <Axis A>
[[nodiscard]] consteval std::string_view duplicate_name_() {
    std::string text{"DuplicateAtomOn"};
    text += ::fixy::axis_name(A);
    return std::define_static_string(text);
}

template <Axis A>
[[nodiscard]] consteval std::string_view duplicate_description_() {
    std::string text{"Two or more atoms in one fixy::fn pack name the "};
    text += ::fixy::axis_name(A);
    text += " axis.  An axis carries one grade, so the pack does not say "
            "which of them the binding means.";
    return std::define_static_string(text);
}

template <class G>
[[nodiscard]] consteval std::string_view malformed_name_() {
    std::string text{"MalformedAtom<"};
    text += std::meta::display_string_of(^^G);
    text += '>';
    return std::define_static_string(text);
}

template <class G>
[[nodiscard]] consteval std::string_view malformed_description_() {
    std::string text{"The type "};
    text += std::meta::display_string_of(^^G);
    text += " appears in a fixy::fn pack but is not an atom.  An atom is "
            "final, derives fixy::atom::atom_base, and names an axis.";
    return std::define_static_string(text);
}

template <class T>
[[nodiscard]] consteval std::string_view payload_name_() {
    std::string text{"UnholdablePayload<"};
    text += std::meta::display_string_of(^^T);
    text += '>';
    return std::define_static_string(text);
}

}  // namespace detail::reject

// One tag per axis, generated where the axis is declared rather than
// stamped by hand.
template <Axis A>
struct duplicate_atom_on final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = detail::reject::duplicate_name_<A>();
    static constexpr std::string_view description = detail::reject::duplicate_description_<A>();
    static constexpr std::string_view remediation = "Remove every atom on the axis but the one the binding means.  "
                                                    "An axis the pack says nothing about resolves to its strict "
                                                    "pole, so dropping an atom never silently widens the binding.";
};

template <class G>
struct malformed_atom final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = detail::reject::malformed_name_<G>();
    static constexpr std::string_view description = detail::reject::malformed_description_<G>();
    static constexpr std::string_view remediation = "Pass an atom from fixy::atom, or declare one: a final class "
                                                    "deriving fixy::atom::atom_of<Axis>.  A cv-qualified or "
                                                    "reference-qualified atom is refused rather than stripped, "
                                                    "because such a type comes from a decltype on a variable.";
};

template <class T>
struct unholdable_payload final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = detail::reject::payload_name_<T>();
    static constexpr std::string_view description = "A fixy::fn holds its payload by value, so the payload has to "
                                                    "be a non-array, non-reference, non-function, cv-unqualified "
                                                    "object type.";
    static constexpr std::string_view remediation = "Name the object type the binding carries.  For a reference, "
                                                    "wrap the referent instead.  For an array, use a span or a "
                                                    "fixy::FixedArray.  A cv qualifier belongs on the binding that "
                                                    "holds the fn, not inside it.";
};

// ---------------------------------------------------------------------
// The tag a failing pack selects, or void when the pack passes.  The
// class body names these types, which puts the offending tag's class
// name into the compiler's instantiation trail where the reader sees
// it.  void names nothing and stays silent.

namespace detail::reject {

template <class... Atoms>
[[nodiscard]] consteval std::meta::info duplicate_tag_or_void_() noexcept {
    constexpr std::size_t offender = first_duplicated_axis_<Atoms...>();
    if constexpr (offender == ::fixy::axis_count) {
        return ^^void;
    } else {
        return ^^duplicate_atom_on<static_cast<Axis>(offender)>;
    }
}

template <class... Atoms>
[[nodiscard]] consteval std::meta::info malformed_tag_or_void_() noexcept {
    constexpr std::meta::info offender = first_malformed_atom_<Atoms...>();
    if constexpr (offender == ^^void) {
        return ^^void;
    } else {
        // A splice in template-argument position needs a disambiguator.
        // Naming it in an alias first sidesteps that.
        using Offender = [:offender:];
        return ^^malformed_atom<Offender>;
    }
}

}  // namespace detail::reject

template <class... Atoms>
using duplicate_tag_or_void_t = [:detail::reject::duplicate_tag_or_void_<Atoms...>():];

template <class... Atoms>
using malformed_tag_or_void_t = [:detail::reject::malformed_tag_or_void_<Atoms...>():];

template <class T>
using payload_tag_or_void_t = std::conditional_t<IsAcceptedPayload<T>, void, unholdable_payload<T>>;

// The corpus entry a refused pack matched, or void.  The entries are
// diagnostic tags in their own right, so the same mechanism names them.
template <class T, class... Atoms>
using corpus_tag_or_void_t = ::fixy::corpus::matched_entry_or_void_t<T, Atoms...>;

namespace detail::reject {

// ---------------------------------------------------------------------
// Which tier refuses a pack.
//
// The arms below are nested rather than conjoined, and that is
// load-bearing.  A later tier's question is not merely meaningless once
// an earlier one has answered no: asking it is a hard error.  The
// uniqueness walk, every collision rule and every corpus entry read
// each atom's `axis` member, which a non-atom does not have, so a pack
// holding one produced the tier-2 message followed by thirty-eight
// diagnostics from inside the walks.  An `if constexpr` chain leaves
// the later arms uninstantiated.  A conjunction of concept-ids does
// not, because a concept-id in an ordinary expression is checked
// whatever its neighbour says.
//
// The enumerators carry the tier numbers fn asserts under.  There is no
// tier 3: an unmentioned axis is not an error.
enum class Tier : std::uint8_t {
    Ok = 0,
    Payload = 1,
    Malformed = 2,
    Duplicate = 4,
    Composition = 5,
};

template <class T, class... Atoms>
[[nodiscard]] consteval Tier first_failing_tier_() noexcept {
    if constexpr (!IsAcceptedPayload<T>) {
        return Tier::Payload;
    } else if constexpr (!AllAtomsWellFormed<Atoms...>) {
        return Tier::Malformed;
    } else if constexpr (!UniqueAtomPerAxis<Atoms...>) {
        return Tier::Duplicate;
    } else if constexpr (!NotInCorpus<T, Atoms...> || !ValidComposition<T, Atoms...>) {
        return Tier::Composition;
    } else {
        return Tier::Ok;
    }
}

// The tag whose class name fn puts into the compiler's instantiation
// trail: the one the failing tier selects, and void when the pack
// passes.  void names nothing and stays silent.
template <class T, class... Atoms>
[[nodiscard]] consteval std::meta::info tier_tag_() noexcept {
    constexpr Tier failed = first_failing_tier_<T, Atoms...>();
    if constexpr (failed == Tier::Payload) {
        return ^^unholdable_payload<T>;
    } else if constexpr (failed == Tier::Malformed) {
        return first_malformed_atom_<Atoms...>() == ^^void ? ^^void
                                                           : malformed_tag_or_void_<Atoms...>();
    } else if constexpr (failed == Tier::Duplicate) {
        return duplicate_tag_or_void_<Atoms...>();
    } else if constexpr (failed == Tier::Composition) {
        return ::fixy::corpus::detail::first_match_<T, Atoms...>();
    } else {
        return ^^void;
    }
}

// The tier-5 message.  A refused pack can trip the corpus, a collision
// rule, or both, and the message carries every part that applies: the
// corpus entry names itself and its citation, and the rules name their
// codes, which are stable API a negative fixture greps.
//
// The tier-4 message, naming the axis the pack graded twice.
//
// first_duplicated_axis_ walks the axes rather than the pack precisely so
// that the answer is an axis, and this is what spends that: the reader
// gets "an axis carries one grade, and Usage carries two" rather than a
// template whose name they then have to go read.  duplicate_atom_on<A>
// still reaches the diagnostic through fn's refused_tag_, and carries the
// insight provider; this is the sentence beside it.
//
// Reached only through the tier chain in fn, which has already
// established that the payload is holdable and the pack is atoms.  The
// guard here repeats that, because a static_assert message is
// instantiated whatever the condition beside it concluded — that is the
// same reason tier5_message_ opens with one.
template <class... Atoms>
[[nodiscard]] consteval std::string_view tier4_message_() noexcept {
    constexpr std::size_t offender = first_duplicated_axis_<Atoms...>();
    if constexpr (offender == ::fixy::axis_count) {
        return "fixy::fn<Type, Atoms...> [tier 4]: not reached — no axis carries two grades.";
    } else {
        std::string text{"fixy::fn<Type, Atoms...> [tier 4]: an axis carries one grade, so the pack must not "
                         "name an axis twice.  The axis graded twice here is "};
        text += ::fixy::axis_name(static_cast<Axis>(offender));
        text += ", and fixy::duplicate_atom_on<Axis> carries its insight.";
        return std::define_static_string(text);
    }
}

// Reached only through the tier chain in fn, which has already
// established that the pack is atoms and unique per axis.  The guard
// here repeats that, because a static_assert message is instantiated
// whatever the condition beside it concluded.
template <class T, class... Atoms>
[[nodiscard]] consteval std::string_view tier5_message_() noexcept {
    if constexpr (!IsAcceptedPayload<T> || !AllAtomsWellFormed<Atoms...>) {
        return "fixy::fn<Type, Atoms...> [tier 5]: not reached — an earlier tier refused this pack.";
    } else if constexpr (!UniqueAtomPerAxis<Atoms...>) {
        return "fixy::fn<Type, Atoms...> [tier 5]: not reached — tier 4 refused this pack.";
    } else {
        using Entry = corpus_tag_or_void_t<T, Atoms...>;
        constexpr std::string_view codes = ::fixy::collision::rules_of<T, Atoms...>::failing_codes();
        std::string text{"fixy::fn<Type, Atoms...> [tier 5]: the combination is refused.  "};
        if constexpr (!std::is_void_v<Entry>) {
            text += "Corpus entry ";
            text += Entry::name;
            text += ": ";
            text += Entry::cite();
            text += "  ";
        }
        if constexpr (!codes.empty()) {
            text += "Collision rule(s) ";
            text += codes;
            text += ": fixy/Collision.h carries each theorem and its citation.";
        }
        return std::define_static_string(text);
    }
}

}  // namespace detail::reject

// ---------------------------------------------------------------------
// The header proves its own gates here, so a reader sees what each one
// admits and what it refuses without leaving the file.

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
static_assert(std::is_same_v<corpus_tag_or_void_t<int, ::fixy::atom::with_io>,
                             ::fixy::corpus::classified_io_without_declassify>);
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
static_assert(::fixy::detail::text_contains(
    detail::reject::tier5_message_<int, ::fixy::atom::copy, ::fixy::atom::affine>(), "tier 4 refused"));

// The tier-4 message names the axis, not just the template that names
// it.  Two packs on two different axes, so a message that spelled one
// axis unconditionally would fail here.
static_assert(::fixy::detail::text_contains(
    detail::reject::tier4_message_<::fixy::atom::copy, ::fixy::atom::affine>(), "Usage"));
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
