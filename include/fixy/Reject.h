#pragma once

// What fixy::fn refuses, and what it says when it refuses.
//
// The gate has four structural questions, asked in this order, because
// a later answer is meaningless once an earlier one is no:
//
//   1. Is the payload an object type a wrapper can hold?
//   2. Is every entry in the pack an atom?
//   3. Does each axis carry at most one atom?
//   4. Is the combination admitted?  (A11.2 collision rules, A11.3 corpus.)
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
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>

#include <cstddef>
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
// Tier 5 and the collision rules.
//
// A11.2 writes ValidComposition over the collision rules and A11.3
// writes NotInCorpus over the refused-combination corpus.  Both are
// satisfied by everything until then, so the tier exists and is wired
// from the day the class body is written, and landing either header is
// a change to one concept rather than a change to the gate.
//
// TODO(A11.2): replace with the fold over the 54 collision rules.
template <class T, class... Atoms>
concept ValidComposition = true;

// TODO(A11.3): replace with the walk over the refused-combination corpus.
template <class T, class... Atoms>
concept NotInCorpus = true;

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
