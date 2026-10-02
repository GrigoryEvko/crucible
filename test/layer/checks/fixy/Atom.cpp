// The compile-time checks of fixy/Atom.h.

#include <fixy/Atom.h>

namespace fixy::atom {

static_assert(std::is_same_v<reentrancy::coroutine, coroutine>,
              "atom::reentrancy::coroutine must alias the top-level "
              "atom::coroutine — the sub-namespace is a re-export, not a "
              "separate type.");
static_assert(std::is_same_v<reentrancy::reentrant, reentrant>, "atom::reentrancy::reentrant must alias the top-level "
                                                                "atom::reentrant — the sub-namespace is a re-export.");

namespace detail::atom_self_test {

static_assert(every_roster_member_is_atom_<core_atom_roster>(),
              "fixy/Atom.h: a member of core_atom_roster is not an atom, is not one empty byte, or names "
              "an axis that is not a fixy::Axis enumerator.");

// ── The ladder walk refuses each broken roster ───────────────────────
//
// The families pass the walk, so these samples show that it can answer
// no.  The walk reads a static data member by type, so plain classes
// stand in for atoms here.
namespace ladder_walk_witness {
enum class rung : std::uint8_t {
    low = 0,
    high = 1,
};
struct low_rung {
    static constexpr rung grade = rung::low;
};
struct high_rung {
    static constexpr rung grade = rung::high;
};
struct low_rung_again {
    static constexpr rung grade = rung::low;
};
struct two_rungs {
    static constexpr rung first = rung::low;
    static constexpr rung second = rung::high;
};
struct forged_rung {
    static constexpr rung grade = static_cast<rung>(7);
};
struct no_rung {};

static_assert(every_enumerator_has_exactly_one_atom_<std::tuple<low_rung, high_rung>, rung>());
static_assert(!every_enumerator_has_exactly_one_atom_<std::tuple<low_rung>, rung>(), "an enumerator with no atom");
static_assert(!every_enumerator_has_exactly_one_atom_<std::tuple<low_rung, high_rung, low_rung_again>, rung>(),
              "an enumerator with two atoms");
static_assert(!every_enumerator_has_exactly_one_atom_<std::tuple<two_rungs, high_rung>, rung>(),
              "a member that names two enumerators");
static_assert(!every_enumerator_has_exactly_one_atom_<std::tuple<low_rung, high_rung, forged_rung>, rung>(),
              "a member that names a value outside the enumerators");
static_assert(!every_enumerator_has_exactly_one_atom_<std::tuple<low_rung, high_rung, no_rung>, rung>(),
              "a member that names no enumerator");

// Two enumerators that hold one value are one rung.  The member that
// names the value claims the two enumerators.
enum class aliased_rung : std::uint8_t {
    low = 0,
    bottom = 0,
    high = 1,
};
struct aliased_low_rung {
    static constexpr aliased_rung grade = aliased_rung::low;
};
struct aliased_high_rung {
    static constexpr aliased_rung grade = aliased_rung::high;
};
static_assert(every_enumerator_has_exactly_one_atom_<std::tuple<aliased_low_rung, aliased_high_rung>, aliased_rung>(),
              "a member claims each enumerator that holds its value");
}  // namespace ladder_walk_witness

// ── Every Security atom has a class ──────────────────────────────────
//
// The count of Security atoms in the roster, and the count that the
// closed relation above answers for.  An atom that reaches the axis and
// not the relation is counted as unproven, so the two counts differ and
// the assertion names the gap.  There is no else branch: a roster member
// whose shape the walk does not expect cannot be counted as proven.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::size_t security_atoms_rostered_() noexcept {
    std::size_t found = 0;
    template for (constexpr auto member : roster_members_v<core_atom_roster>) {
        using A = [:member:];
        if constexpr (IsAtom<A>) {
            if constexpr (A::axis == Axis::Security) ++found;
        }
    }
    return found;
}

[[nodiscard]] consteval std::size_t security_atoms_classified_() noexcept {
    std::size_t proven = 0;
    template for (constexpr auto member : roster_members_v<core_atom_roster>) {
        using A = [:member:];
        if constexpr (IsAtom<A>) {
            if constexpr (A::axis == Axis::Security && IsSecurityGrade<A>) ++proven;
        }
    }
    return proven;
}

#pragma GCC diagnostic pop

static_assert(security_atoms_rostered_() > 0, "fixy/Atom.h: the roster holds no Security atom, so the walk below "
                                              "proves nothing.  Either the atoms left core_atom_roster, or they "
                                              "stopped naming Axis::Security.");
static_assert(security_atoms_classified_() == security_atoms_rostered_(),
              "fixy/Atom.h: a Security atom in core_atom_roster has no entry in security_class_answer_of_.  "
              "Every reader of the Security grade asks that relation, so an atom without an entry cannot be "
              "used in a binding.  Give it a SecurityClass, next to the others.");
static_assert(IsSecurityGrade<typename axis_traits<Axis::Security>::strict>,
              "fixy/Atom.h: the strict Security pole must have a class, because a binding that names no "
              "Security atom resolves to it.");

// The relation answers what it must and refuses what it must.
static_assert(security_class_of(^^axis_traits<Axis::Security>::strict) == SecurityClass::Classified);
static_assert(security_class_of(^^constant_time) == SecurityClass::ConstantTime);
static_assert(security_class_of(^^as_secret) == SecurityClass::Classified);
static_assert(security_class_of(^^as_public) == SecurityClass::Public);
static_assert(security_class_of(^^as_unclassified) == SecurityClass::Public);
static_assert(security_class_of(^^as_internal) == SecurityClass::Internal);
static_assert(security_class_of(^^declassify<::fixy::tags::secret_policy::AuditedLogging>)
              == SecurityClass::Declassified);
static_assert(IsClassifiedCarrier<constant_time> && IsConstantTime<constant_time>);
static_assert(IsClassifiedCarrier<as_classified> && !IsConstantTime<as_classified>);
static_assert(!IsClassifiedCarrier<as_internal> && !IsClassifiedCarrier<as_public>);
static_assert(!IsClassifiedCarrier<declassify<::fixy::tags::secret_policy::AuditedLogging>>,
              "a declassified grade is not a carrier. fixy/Corpus.h reads its policy for each channel");
static_assert(!IsClassifiedCarrier<int> && !IsConstantTime<int>, "a type that is not a grade makes no claim");
static_assert(!IsSecurityGrade<int>, "a type that is not a Security grade has no class");
static_assert(!IsSecurityGrade<affine>, "an atom on another axis has no Security class");
static_assert(!IsSecurityGrade<const as_public>, "a qualified atom is refused rather than stripped");
static_assert(constant_time::axis == Axis::Security);
static_assert(sizeof(constant_time) == 1 && std::is_empty_v<constant_time>);

static_assert(IsRefinementPredicate<atom_axis_witness_predicate>,
              "A named empty default-constructible predicate must satisfy IsRefinementPredicate.");
static_assert(!IsRefinementPredicate<::fixy::pole::pred::True>,
              "The strict pole of Refinement proves nothing, so it is not a witness.");

// What the Refinement gates admit and refuse, on four shapes: the two
// that carry state or a constructor argument, which the predicate gate
// refuses, and the empty default-constructible class that a capture-less
// lambda produces, which the predicate gate admits and IsAtom refuses.
namespace refinement_predicate_shape_witness {
struct StatefulPredicate {
    int threshold = 0;
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > threshold; }
};
static_assert(!IsRefinementPredicate<StatefulPredicate>, "A stateful predicate makes a claim its type does not state.");

struct NonDefaultConstructiblePredicate {
    constexpr NonDefaultConstructiblePredicate(int) noexcept {}
    [[nodiscard]] constexpr bool operator()(int v) const noexcept { return v > 0; }
};
static_assert(!IsRefinementPredicate<NonDefaultConstructiblePredicate>,
              "A predicate that the type alone cannot construct makes a claim its type does not state.");

using CaptureLessLambdaType = decltype([](int v) noexcept { return v > 0; });
static_assert(std::is_empty_v<CaptureLessLambdaType>);
static_assert(std::is_default_constructible_v<CaptureLessLambdaType>);
static_assert(IsRefinementPredicate<CaptureLessLambdaType>, "a closure has the shape of a predicate");
static_assert(!IsAtom<refined_with<CaptureLessLambdaType>>,
              "The name of a closure type is not a function of the type, so the atom has no key.");
static_assert(atom_refusal_v<refined_with<CaptureLessLambdaType>> == atom_refusal::no_stable_identity);
static_assert(!IsAtom<from_source<decltype([] {})>>, "the identity read covers every parametric atom");
static_assert(!IsAtom<protocol<decltype([] {})>>);
static_assert(IsAtom<refined_with<atom_axis_witness_predicate>>);
}  // namespace refinement_predicate_shape_witness

// A forge-phase source carries its phase as a non-type parameter and stays
// empty, so the empty-marker gate admits every instantiation.  A refactor
// that gave it state reds here rather than at the use sites.  The twelve
// phases A thru L are the range fixy/Tags.h pins on ForgePhase.
template <char... Offsets>
[[nodiscard]] consteval bool every_forge_phase_is_a_source_(std::integer_sequence<char, Offsets...>) noexcept {
    return (IsProvenanceSource<::fixy::tags::source::ForgePhase<static_cast<char>('A' + Offsets)>> && ...);
}
static_assert(every_forge_phase_is_a_source_(std::make_integer_sequence<char, 'L' - 'A' + 1>{}));

// The policy gate admits only the closed set in fixy/Tags.h.
struct ad_hoc_policy final {};
struct open_policy : ::fixy::tags::secret_policy::secret_policy_base {};
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuditedLogging>);
static_assert(IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(!IsDeclassificationPolicy<ad_hoc_policy>, "A policy outside the closed set must be rejected.");
static_assert(!IsDeclassificationPolicy<open_policy>, "A policy that is not final must be rejected.");
struct forged_policy final : ::fixy::tags::secret_policy::secret_policy_base {};
static_assert(!IsDeclassificationPolicy<forged_policy>,
              "A final policy with the base, declared outside fixy::tags::secret_policy, must be rejected.");

// The gate rejects a cv-qualified or reference-qualified atom rather than
// stripping it, and rejects the two halves of the recipe on their own.
struct not_final : atom_of<Axis::Usage> {};
struct no_axis final : atom_base {};

static_assert(!IsAtom<const affine>);
static_assert(!IsAtom<volatile affine>);
static_assert(!IsAtom<const volatile affine>);
static_assert(!IsAtom<affine&>);
static_assert(!IsAtom<const affine&>);
static_assert(!IsAtom<affine&&>);
static_assert(!IsAtom<not_final>);
static_assert(!IsAtom<no_axis>);
static_assert(!IsAtom<atom_base>);
static_assert(!IsAtom<int>);

// The shape alone is not an atom.  A type with every part of the recipe,
// declared here in detail, is refused because detail is not a family.
struct shaped_outside_the_catalog final : atom_of<Axis::Usage> {};
static_assert(detail::HasAtomShape<shaped_outside_the_catalog>);
static_assert(!IsAtom<shaped_outside_the_catalog>);
static_assert(atom_refusal_v<shaped_outside_the_catalog> == atom_refusal::outside_the_catalog);

// The core namespace and each family carry one seal, in this file for
// the core.  A shipped atom passes each of the four reads.
static_assert(atom_refusal_v<affine> == atom_refusal::none);
static_assert(atom_refusal_v<with_io> == atom_refusal::none, "an alias is read through to the atom it names");
static_assert(atom_refusal_v<declassify<::fixy::tags::secret_policy::AuditedLogging>> == atom_refusal::none);
static_assert(same_file_(^^affine, ^^::fixy::atom::atom_namespace_seal));
static_assert(is_admitted_atom_namespace_(^^::fixy::atom) && is_admitted_atom_namespace_(^^::fixy::atom::sync));
static_assert(!is_admitted_atom_namespace_(^^::fixy::atom::detail), "detail holds rosters and probes, not atoms");
static_assert(!is_admitted_atom_namespace_(^^::fixy), "the parent of the catalog is not the catalog");

// Each family in the list is a namespace directly in fixy::atom.  A
// family that moved would leave a list entry that names nothing.
[[nodiscard]] consteval bool every_family_is_a_child_of_the_catalog() {
    for (const std::meta::info family : atom_families) {
        if (!std::meta::is_namespace(family) || std::meta::parent_of(family) != ^^::fixy::atom) return false;
    }
    return true;
}
static_assert(every_family_is_a_child_of_the_catalog());

// ── The Effect grade reads the same both ways ────────────────────────
//
// `with` lifts, so foundation/effects/Lift.h can fold it into a
// required row alongside the syscall and wait atoms.  The closed
// relation above answers for the same atom AND for the bare Row the
// strict pole leaves behind.  The two readings have to agree wherever
// both apply, or a gate written against one would admit what a gate
// written against the other refuses.
namespace fe_ = ::foundation::effects;

static_assert(fe_::LiftsToRow<with<>>);
static_assert(fe_::LiftsToRow<with_io>);
static_assert(std::is_same_v<fe_::lift_row_t<with<>>, fe_::Row<>>);
static_assert(std::is_same_v<fe_::lift_row_t<with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<fe_::lift_row_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>,
                             fe_::Row<fe_::Effect::Bg, fe_::Effect::Alloc>>);

static_assert(std::is_same_v<effect_row_of_t<with<>>, fe_::lift_row_t<with<>>>);
static_assert(std::is_same_v<effect_row_of_t<with_io>, fe_::lift_row_t<with_io>>);
static_assert(std::is_same_v<effect_row_of_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>,
                             fe_::lift_row_t<with<fe_::Effect::Bg, fe_::Effect::Alloc>>>);

// The strict pole's shape, which no lift can answer for because a bare
// row is not an atom and declares no `lifts_to`.  This arm is the whole
// reason the relation exists beside the lift rather than instead of it.
static_assert(IsEffectGrade<fe_::Row<>>);
static_assert(!fe_::LiftsToRow<fe_::Row<>>);
static_assert(std::is_same_v<effect_row_of_t<fe_::Row<>>, fe_::Row<>>);
static_assert(std::is_same_v<effect_row_of_t<fe_::Row<fe_::Effect::IO>>, fe_::Row<fe_::Effect::IO>>);

// Closed.  A grade of any other shape is refused rather than answered
// with the empty row, because the empty row is a Subrow of every
// context's and a quiet default would admit the binding everywhere.
static_assert(!IsEffectGrade<int>);
static_assert(!IsEffectGrade<affine>);
static_assert(!IsEffectGrade<with_io&>);
static_assert(!IsEffectGrade<const with_io>);

// `with` stays an atom, and stays empty: the lift is a static member,
// so nothing about the pack's size or its axis moves.
static_assert(IsAtom<with_io>);
static_assert(with_io::axis == Axis::Effect);
static_assert(sizeof(with_io) == 1 && std::is_empty_v<with_io>);

// ── The row of a pack, and the row of a binding ──────────────────────
//
// The union of the lifts in the pack.  An atom with no lift adds nothing,
// and the result is the canonical row, so two orders give one type.
static_assert(std::is_same_v<lifted_row_of_t<>, fe_::Row<>>);
static_assert(std::is_same_v<lifted_row_of_t<affine>, fe_::Row<>>);
static_assert(std::is_same_v<lifted_row_of_t<affine, with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<lifted_row_of_t<with<fe_::Effect::IO, fe_::Effect::Bg>>,
                             lifted_row_of_t<with<fe_::Effect::Bg, fe_::Effect::IO>>>);

// The row of a binding joins the grade and the lifts.  The strict pole
// states no effect, so a binding at the pole requires what its atoms
// lift to and nothing more.
static_assert(std::is_same_v<binding_row_of_t<fe_::Row<>, affine>, fe_::Row<>>);
static_assert(std::is_same_v<binding_row_of_t<with_io, with_io>, fe_::Row<fe_::Effect::IO>>);
static_assert(std::is_same_v<binding_row_of_t<fe_::Row<>, with_io>, fe_::Row<fe_::Effect::IO>>);

}  // namespace detail::atom_self_test

}  // namespace fixy::atom
