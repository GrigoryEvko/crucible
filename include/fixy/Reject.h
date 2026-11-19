#pragma once

// What fixy::fn refuses, and what it says when it refuses.
//
// The gate has four structural questions, asked in this order, because
// a later answer is meaningless once an earlier one is no:
//
//   1. Is the payload an object type a wrapper can hold?
//   2. Is every entry in the pack an atom?
//   3. Does each axis carry one grade?  A Row axis takes the union of its
//      atoms, and each other axis carries at most one atom.
//   4. Is the combination admitted?  (A refinement that the payload can
//      check, the Collision.h rules, the Corpus.h entries.)
//
// Each question is one concept here.  fixy/Fn.h asks them in the class
// body, one tier per question, and each tier silences itself when an
// earlier tier already failed, so a reader sees one message and not a
// cascade.
//
// The diagnostics are class templates, not one hand-stamped class per
// axis.  A hand-stamped tag per axis needs an edit for each new axis,
// and a count of the tags cannot tell a missing tag from a renamed one.
// A tag parameterised on the axis is generated the moment the axis is
// declared, and its name is the axis identifier read by reflection.
//
// These tags derive foundation::diag::tag_base, which is what makes
// them printable through the diagnostic surface.  They deliberately do
// NOT join foundation::diag::Category: that enum is append-only and its
// ordinals are pinned into federation cache keys, so a per-axis family
// has no business there.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <fixy/Predicates.h>
#include <foundation/Platform.h>
#include <foundation/diag/Tag.h>
#include <foundation/reflect/Anchor.h>

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
// Tier 4: one grade on each axis.
//
// The shape of the axis in the table of fixy/Axis.h decides what one grade
// is.  A Row axis takes the union of its atoms, so a pack can name several
// atoms on it, and tier 4 refuses only an atom that the pack names twice.
// An axis of each other shape carries one grade, and tier 4 refuses a
// second atom on it.
//
// There is no tier 3.  An axis the pack says nothing about resolves to
// its strict pole, silently, so a missing atom is not a rejection and
// needs no check.  A tier that demanded a marker on each unmentioned
// axis would make the common binding unwritable, because the table has
// 33 axes.

namespace detail::reject {

// Both answers read the record of the pack in fixy/Collision.h, which one
// walk of the pack gives.  A type that is not an atom states no axis the
// walk may read, so the walk counts no axis for it.  Tier 2 refuses it,
// and tier 4 must not add a second error.

// The first axis that the pack of a key grades twice, or axis_count when the
// pack grades each axis at most one time.  The walk is over the axes rather
// than over the pack, so the answer is an axis and the diagnostic can name
// it.  The function is not a template, and it reads the sealed record with
// the key, so no unit can change an answer.  Complexity: the axis count.
[[nodiscard]] consteval std::size_t first_duplicated_axis_(std::meta::info key) {
    const ::fixy::collision::detail::pack_record& record = ::fixy::collision::detail::record_of_key_(key);
    for (std::size_t place = 0; place < ::fixy::axis_count; ++place) {
        if (record.is_graded_twice[place]) return place;
    }
    return ::fixy::axis_count;
}

// The first entry of the pack of a key that is not an atom, as a
// reflection, or the reflection of void when every entry is one.  void is
// not an atom and is not nameable as one, so it cannot collide with a real
// answer.
[[nodiscard]] consteval std::meta::info first_malformed_atom_(std::meta::info key) {
    return ::fixy::collision::detail::record_of_key_(key).first_malformed;
}

}  // namespace detail::reject

template <class... Atoms>
concept UniqueAtomPerAxis =
    (detail::reject::first_duplicated_axis_(^^::fixy::collision::pack_key<Atoms...>) == ::fixy::axis_count);

// ---------------------------------------------------------------------
// Tier 5: the collision rules and the corpus.
//
// The collision rules, judged in fixy/Collision.h.  A rule reads the
// payload and the pack and nothing else, which is why this delegates
// those two rather than the fn: a rule that completed fn would recurse
// through fn's own assertion of this concept.  The payload is read for the
// claims of its bands, for the failure that it returns and for a session
// handle, and it is the fn's first template parameter rather than a member
// of the fn.
template <class T, class... Atoms>
concept ValidComposition = ::fixy::collision::read_verdict(::fixy::collision::verdict_cell_v<T, Atoms...>,
                                                           ^^::fixy::collision::verdict_key<T, Atoms...>)
                               .is_valid();

// The refused-combination corpus, folded in fixy/Corpus.h.  An entry
// reads the same resolved grades a rule does, and never fn, for the
// same reason.
template <class T, class... Atoms>
concept NotInCorpus = !::fixy::corpus::is_in_corpus_v<T, Atoms...>;

// The payload half of a refinement.  refined_with<Pred> states that Pred
// holds for the payload, and the mint of fixy/Fn.h checks that claim with a
// call of Pred on each value.  So a call of Pred on the payload must run,
// through the sealed call that the checked mint of fixy/Refined.h asks for.
// A predicate with no call operator, or with a call operator for another
// type, refines nothing on this payload.  A pack with no refined_with
// passes.
namespace detail::reject {

template <class T, class... Atoms>
[[nodiscard]] consteval bool is_refinement_defined_on_payload_() noexcept {
    using Grade = typename ::fixy::collision::grades<Atoms...>::template on<Axis::Refinement>;
    constexpr std::meta::info predicate = ::fixy::atom::refinement_predicate_of(^^Grade);
    if constexpr (predicate == ^^void) {
        return true;
    } else {
        using Predicate = [:predicate:];
        return PredicateInvocableOn<Predicate{}, T>;
    }
}

}  // namespace detail::reject

template <class T, class... Atoms>
concept RefinementDefinedOnPayload = detail::reject::is_refinement_defined_on_payload_<T, Atoms...>();

// ---------------------------------------------------------------------
// The whole gate.  This is what the negative corpus asserts against and
// what the mint factories put in their signatures.  The refinement half
// reads one grade on the Refinement axis, so it comes after the tiers that
// make each entry an atom and each axis carry one grade.

template <class T, class... Atoms>
concept IsAccepted =
    IsAcceptedPayload<T> && AllAtomsWellFormed<Atoms...> && UniqueAtomPerAxis<Atoms...>
    && RefinementDefinedOnPayload<T, Atoms...> && NotInCorpus<T, Atoms...> && ValidComposition<T, Atoms...>;

// ---------------------------------------------------------------------
// The diagnostics.

namespace detail::reject {

template <Axis A>
[[nodiscard]] consteval std::string_view duplicate_name_() {
    ::foundation::reflect::anchored_t<std::meta::reflect_constant(A), std::string> text{"DuplicateAtomOn"};
    text += ::fixy::axis_name(A);
    return std::define_static_string(text);
}

// The texts of a Row axis say that the axis takes the union of its atoms
// and refuses one atom stated twice.  The texts of each other axis say that
// the axis carries one grade.
template <Axis A>
[[nodiscard]] consteval std::string_view duplicate_description_() {
    constexpr bool is_row_axis = axis_record(A).shape == Shape::Row;
    ::foundation::reflect::anchored_t<std::meta::reflect_constant(A), std::string> text{
        is_row_axis ? "One fixy::fn pack names one atom of the " : "Two or more atoms in one fixy::fn pack name the "};
    text += ::fixy::axis_name(A);
    text += is_row_axis ? " axis twice.  A Row axis takes the union of its atoms, so the copy adds no effect, and "
                          "the pack does not say what the second atom was meant to state."
                        : " axis.  An axis carries one grade, so the pack does not say which of them the binding "
                          "means.";
    return std::define_static_string(text);
}

template <class G>
[[nodiscard]] consteval std::string_view malformed_name_() {
    ::foundation::reflect::anchored_t<^^G, std::string> text{"MalformedAtom<"};
    text += std::meta::display_string_of(^^G);
    text += '>';
    return std::define_static_string(text);
}

// The text is a std::string, so the tier-2 message can append it.  GCC 16
// does not append a view that define_static_string gave, in a constant
// expression: the append compares the pointer with null, and GCC refuses
// that comparison for such a pointer.
template <class G>
[[nodiscard]] consteval std::string malformed_text_() {
    std::string text{"The type "};
    text += std::meta::display_string_of(^^G);
    text += " appears in a fixy::fn pack but is not an atom.  An atom is final, derives fixy::atom::atom_base, "
            "names an axis, is declared in the closed catalog, and has a name that is its identity.";
    if constexpr (::fixy::atom::detail::HasAtomShape<G>) {
        text += "  It has the shape, and it is refused because ";
        text += ::fixy::atom::detail::atom_refusal_text_(::fixy::atom::detail::atom_refusal_of_(^^G));
        text += '.';
    }
    return text;
}

template <class G>
[[nodiscard]] consteval std::string_view malformed_description_() {
    return std::define_static_string(malformed_text_<G>());
}

template <class T>
[[nodiscard]] consteval std::string_view payload_name_() {
    ::foundation::reflect::anchored_t<^^T, std::string> text{"UnholdablePayload<"};
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
    static constexpr std::string_view remediation =
        axis_record(A).shape == Shape::Row
            ? "Remove the copy of the atom.  A Row axis takes the union of its atoms, so two different atoms on it "
              "state the union of their effects."
            : "Remove every atom on the axis but the one the binding means.  An axis the pack says nothing about "
              "resolves to its strict pole, so dropping an atom never silently widens the binding.";
};

template <class G>
struct malformed_atom final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = detail::reject::malformed_name_<G>();
    static constexpr std::string_view description = detail::reject::malformed_description_<G>();
    static constexpr std::string_view remediation = "Pass an atom from fixy::atom.  A new atom is declared in the "
                                                    "header of its family, beside that family's atom_seal, and a "
                                                    "new family is a line in fixy/Atom.h.  A cv-qualified or "
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
    constexpr std::size_t offender = first_duplicated_axis_(^^::fixy::collision::pack_key<Atoms...>);
    if constexpr (offender == ::fixy::axis_count) {
        return ^^void;
    } else {
        return ^^duplicate_atom_on<static_cast<Axis>(offender)>;
    }
}

template <class... Atoms>
[[nodiscard]] consteval std::meta::info malformed_tag_or_void_() noexcept {
    constexpr std::meta::info offender = first_malformed_atom_(^^::fixy::collision::pack_key<Atoms...>);
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
    } else if constexpr (!RefinementDefinedOnPayload<T, Atoms...> || !NotInCorpus<T, Atoms...>
                         || !ValidComposition<T, Atoms...>) {
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
        return first_malformed_atom_(^^::fixy::collision::pack_key<Atoms...>) == ^^void
                 ? ^^void
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
    constexpr std::size_t offender = first_duplicated_axis_(^^::fixy::collision::pack_key<Atoms...>);
    if constexpr (offender == ::fixy::axis_count) {
        return "fixy::fn<Type, Atoms...> [tier 4]: not reached — no axis carries two grades.";
    } else {
        constexpr bool is_row_axis = axis_record(static_cast<Axis>(offender)).shape == Shape::Row;
        ::foundation::reflect::anchored_t<std::meta::reflect_constant(offender), std::string> text{
            is_row_axis ? "fixy::fn<Type, Atoms...> [tier 4]: a Row axis takes the union of its atoms, so the pack "
                          "must not name one atom on it twice.  The axis that names one atom twice here is "
                        : "fixy::fn<Type, Atoms...> [tier 4]: an axis carries one grade, so the pack must not "
                          "name an axis twice.  The axis graded twice here is "};
        text += ::fixy::axis_name(static_cast<Axis>(offender));
        text += ", and fixy::duplicate_atom_on<Axis> carries its insight.";
        return std::define_static_string(text);
    }
}

// The tier-2 message, naming the first entry that is not an atom.  For an
// entry with the shape of an atom it also names the read that refuses
// it, so the reader learns whether the namespace, the seal, the file or
// the identity of the entry is wrong.  The message is instantiated
// whatever tier refused the pack, so a pack of atoms gets a sentence that
// says tier 2 was not reached.
template <class... Atoms>
[[nodiscard]] consteval std::string_view tier2_message_() noexcept {
    constexpr std::meta::info offender = first_malformed_atom_(^^::fixy::collision::pack_key<Atoms...>);
    if constexpr (offender == ^^void) {
        return "fixy::fn<Type, Atoms...> [tier 2]: not reached — every entry in the pack is an atom.";
    } else {
        using Offender = [:offender:];
        ::foundation::reflect::anchored_t<^^Offender, std::string> text{
            "fixy::fn<Type, Atoms...> [tier 2]: every entry in the pack must be an atom of the closed catalog.  "};
        text += malformed_text_<Offender>();
        return std::define_static_string(text);
    }
}

// Reached only through the tier chain in fn, which has already
// established that the pack is atoms and unique per axis.  The guard
// here repeats that, because a static_assert message is instantiated
// whatever the condition beside it concluded.  A pack that passes each
// tier gets a literal, so each fn type of the tree builds no refusal
// text.
//
// The refusal names the code and the theorem of each collision rule that
// the pack trips, so a reader learns why the combination is refused.
template <class T, class... Atoms>
[[nodiscard]] consteval std::string_view tier5_message_() noexcept {
    if constexpr (!IsAcceptedPayload<T> || !AllAtomsWellFormed<Atoms...>) {
        return "fixy::fn<Type, Atoms...> [tier 5]: not reached — an earlier tier refused this pack.";
    } else if constexpr (!UniqueAtomPerAxis<Atoms...>) {
        return "fixy::fn<Type, Atoms...> [tier 5]: not reached — tier 4 refused this pack.";
    } else if constexpr (first_failing_tier_<T, Atoms...>() == Tier::Ok) {
        return "fixy::fn<Type, Atoms...> [tier 5]: not reached — the pack passes each tier.";
    } else {
        using Entry = corpus_tag_or_void_t<T, Atoms...>;
        constexpr ::fixy::collision::binding_verdict verdict = ::fixy::collision::read_verdict(
            ::fixy::collision::verdict_cell_v<T, Atoms...>, ^^::fixy::collision::verdict_key<T, Atoms...>);
        constexpr std::string_view codes = verdict.failing_codes();
        ::foundation::reflect::anchored_t<^^T, std::string> text{
            "fixy::fn<Type, Atoms...> [tier 5]: the combination is refused.  "};
        if constexpr (!RefinementDefinedOnPayload<T, Atoms...>) {
            text += "The predicate of refined_with has no sealed call on the payload ";
            text += std::meta::display_string_of(^^T);
            text += ", so the mint cannot check the fact that the refinement states.  Give the predicate one call "
                    "operator that takes the payload, as fixy/Predicates.h does.  ";
        }
        if constexpr (!std::is_void_v<Entry>) {
            text += "Corpus entry ";
            text += Entry::name;
            text += ": ";
            text += Entry::cite();
            text += "  ";
        }
        // The theorems are appended from the rows of the rule table, in its
        // order, one character at a time.
        if constexpr (!codes.empty()) {
            text += "Collision rule(s) ";
            text += codes;
            text += ":";
            for (std::size_t place = 0; place < ::fixy::collision::rule_count; ++place) {
                const auto code = static_cast<::fixy::collision::RuleCode>(place);
                if (verdict.admits(code)) continue;
                text += "  ";
                text += ::fixy::collision::rule_row_of(code, verdict.premises).theorem;
            }
        }
        return std::define_static_string(text);
    }
}

}  // namespace detail::reject

}  // namespace fixy
