#pragma once

// The combinatorial half of the gate's coverage, generated rather than
// written out.
//
// A file for each combination cannot be read, cannot be kept in step
// with the atom catalog, and says nothing about the cases that no file
// covers.  What actually needs asserting is universal over the catalog,
// so it is one walk here and the catalog is its input.
//
// Three claims, each over every atom the tree ships — the join of the
// seven family rosters, which each family separately proves covers its
// own namespace:
//
//   1. Every atom, alone, passes the STRUCTURAL tiers.  It is an atom
//      (tier 2) and it names its axis once (tier 4).  Whether tier 5
//      then refuses it is the rules' and the corpus's business and is
//      reported below as data, not asserted: many atoms alone ARE
//      refused, because reject-by-default means a strict pole can be
//      half of a contradiction.  The run prints which ones, so this
//      comment names no count that can drift.  Each atom that lifts IO
//      is one of them, a stated with<IO>, an IO system call and a stdio
//      write alike: Security's strict pole is classified, so the corpus
//      reads a classified value on an observable channel.
//
//   2. Any two atoms on ONE axis are refused, and refused BY TIER 4.
//      Asserting the tier rather than the bare rejection is what makes
//      the claim non-vacuous: a pair that happened to trip a collision
//      rule would satisfy `!IsAccepted` while saying nothing about the
//      one-grade-per-axis rule this walk exists to check.
//
//   3. Every atom that lifts to an effect row is admitted by a context
//      exactly when that row is empty.  The foreground context claims
//      Row<>, so the biconditional below says: a lifting atom with a
//      non-empty row cannot run there, and one that lifts to nothing
//      can.  The atoms that lift are the SyscallSurface families of
//      fixy/atoms/Os.h and fixy/atoms/Syscall.h, the wait strategies of
//      fixy/atoms/Sync.h, the writes of fixy/atoms/Stdio.h, and
//      atom::with<Es...>, the Effect axis's own atom.  The last of those
//      is what lets a context be gated on a binding's declared effects.
//      Without that lift, the row of a binding would be the empty row,
//      and the empty row is admitted everywhere.
//
// A gate bug that admitted a duplicate, or an atom that stopped being
// one, fails the build here rather than going unnoticed for want of the
// file nobody wrote.
//
// The walk has part_count parts, because one translation unit that walks
// all of the catalog compiles for too long.  Part k walks a range of the
// roster for claims 1 and 3, and a range of the Axis enumerators for claim
// 2.  part_begin gives the ranges.  rejections_generated_<k>.cpp compiles
// part k.  rejections_generated.cpp holds main, the proof that the ranges
// cover each input one time, and the floors over the full catalog.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <fixy/Ctx.h>
#include <fixy/Reject.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rejections_generated {

namespace fe = ::foundation::effects;
// `Axis` is spelled ::fixy::Axis at every `^^` below rather than through
// this using-declaration: GCC 16.2 refuses "‘^^’ cannot be applied to a
// using-declaration", so a reflection of an imported name has to name the
// entity, not the import.
using ::fixy::Axis;
using ::fixy::IsAccepted;
using ::fixy::detail::reject::first_failing_tier_;
using ::fixy::detail::reject::Tier;

// The join of every family roster, and the reflections of its members.
// Collision.h already builds the join for its own atomless-axis walk,
// and reading the same one keeps the two from disagreeing about what
// "every atom" means.
using every_atom = ::fixy::collision::all_atom_roster;
inline constexpr auto atom_members = ::fixy::atom::detail::roster_members_v<every_atom>;

// The reflections of the Axis enumerators, the input of claim 2.
inline constexpr auto axis_members = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));

// ---------------------------------------------------------------------
// The parts.

inline constexpr std::size_t part_count = 8;

// The first index of a part of an input that has `total` items.  Part k
// holds the items from part_begin(k, total) to part_begin(k + 1, total).
// part_begin(0, total) is 0, and part_begin(part_count, total) is total.
[[nodiscard]] consteval std::size_t part_begin(std::size_t part, std::size_t total) noexcept {
    return total * part / part_count;
}

// The atoms from index First to index End of the roster, and the axes from
// index First to index End of the Axis enumerators.
template <std::size_t First, std::size_t End>
inline constexpr auto atom_range = atom_members.subspan(First, End - First);

template <std::size_t First, std::size_t End>
inline constexpr auto axis_range = axis_members.subspan(First, End - First);

// What one part found.  main adds the counts of the parts, and it prints
// the names of the parts in part order, which is roster order.
struct PartReport {
    std::size_t part = 0;
    std::size_t atoms_walked = 0;
    std::size_t accepted_alone = 0;
    std::size_t refused_alone = 0;
    std::size_t lifting_atoms = 0;
    std::size_t axes_walked = 0;
    std::size_t axes_with_a_pair = 0;
    std::string_view refused_names{};
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// ---------------------------------------------------------------------
// Claim 1: every atom, alone, clears the structural tiers.

template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::size_t atoms_clearing_the_structural_tiers() noexcept {
    std::size_t cleared = 0;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        constexpr Tier failed = first_failing_tier_<int, A>();
        if (failed != Tier::Malformed && failed != Tier::Duplicate && failed != Tier::Payload) ++cleared;
    }
    return cleared;
}

// How many atoms tier 5 refuses on their own.  This is reported rather
// than pinned to a literal: it moves whenever a rule or a corpus entry
// is written, and the count is not the property — the property is that
// each refusal has a named reason, which the hand-written rejection
// fixtures carry one by one.
template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::size_t atoms_refused_alone() noexcept {
    std::size_t refused = 0;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        if (first_failing_tier_<int, A>() == Tier::Composition) ++refused;
    }
    return refused;
}

// The names of the atoms tier 5 refuses on their own, so the reader does
// not have to take the header comment's word for which four they are.
// Reported at run time rather than pinned: the set moves whenever a rule
// or a corpus entry is written, and the hand-written rejection fixtures
// are what pin each refusal to its reason.
//
// display_string_of rather than identifier_of because a roster member can
// be a class-template specialization — atom::stale_to<N> is one — and
// identifier_of throws `reflection with has_identifier false` on those.
// The spelling is toolchain-specific, which is why nothing here hashes
// it; it is printed for a human and nothing else reads it.
//
// The names are joined one character at a time through a vector<char>,
// which is neither the obvious spelling nor an aesthetic choice.  Two
// GCC 16.2 walls stand in the way of the obvious ones:
//
//   std::string{sv} + ", "  — refused with "‘(((const char*)(&"..."))
//     == 0)’ is not a constant expression", because basic_string's null
//     check on a pointer into a static string is not foldable there.
//     fixy/Axis.h carries the same workaround for string_view::find.
//
//   define_static_array of std::string_view — refused because
//     string_view is not a structural type, so a list of views cannot
//     cross out of the constant evaluation at all.
//
// A vector<char> walked by index dodges both, and define_static_string
// gives the result static storage so main can print it.
template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::string_view refused_alone_names() {
    std::vector<char> joined;
    bool first = true;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        if constexpr (first_failing_tier_<int, A>() == Tier::Composition) {
            if (!first) {
                joined.push_back(',');
                joined.push_back(' ');
            }
            first = false;
            const std::string_view name = std::meta::display_string_of(std::meta::dealias(member));
            for (std::size_t i = 0; i < name.size(); ++i)
                joined.push_back(name[i]);
        }
    }
    return std::string_view{std::define_static_string(joined)};
}

template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::size_t atoms_accepted_alone() noexcept {
    std::size_t accepted = 0;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        if constexpr (IsAccepted<int, A>) ++accepted;
    }
    return accepted;
}

// ---------------------------------------------------------------------
// Claim 2: two atoms on one axis are refused, and refused by tier 4.
//
// The pairs are generated: for each axis, the walk takes the first two
// rostered atoms that name it.  Picking from the roster rather than from
// a hand list is what makes an atom added to a family covered the day it
// is declared.

// The reflection of the Nth rostered atom on one axis, or void when the
// axis has fewer than N+1.  The search reads the full roster, so a pair
// is the same pair in each part.
template <Axis A, std::size_t N>
[[nodiscard]] consteval std::meta::info nth_atom_on_axis() noexcept {
    std::size_t seen = 0;
    std::meta::info found = ^^void;
    template for (constexpr auto member : atom_members) {
        using Candidate = [:member:];
        if constexpr (Candidate::axis == A) {
            if (seen == N && found == ^^void) found = member;
            ++seen;
        }
    }
    return found;
}

template <Axis A>
[[nodiscard]] consteval bool axis_has_two_atoms() noexcept {
    return nth_atom_on_axis<A, 1>() != ^^void;
}

// For one axis: its first two atoms in a pack are refused by tier 4, and
// each alone clears tier 4.  The second half is what stops the first
// from passing because the atoms were malformed.
template <Axis A>
[[nodiscard]] consteval bool axis_refuses_its_own_pair() noexcept {
    if constexpr (!axis_has_two_atoms<A>()) {
        return true;  // an axis with one atom has no pair to refuse
    } else {
        using First = [:nth_atom_on_axis<A, 0>():];
        using Second = [:nth_atom_on_axis<A, 1>():];
        return first_failing_tier_<int, First, Second>() == Tier::Duplicate
            && first_failing_tier_<int, Second, First>() == Tier::Duplicate
            && first_failing_tier_<int, First>() != Tier::Duplicate
            && first_failing_tier_<int, Second>() != Tier::Duplicate
            && !IsAccepted<int, First, Second> && !IsAccepted<int, Second, First>;
    }
}

template <std::size_t First, std::size_t End>
[[nodiscard]] consteval bool every_axis_refuses_its_own_pair() noexcept {
    bool all_refused = true;
    template for (constexpr auto axis_member : axis_range<First, End>) {
        constexpr Axis axis = [:axis_member:];
        all_refused = all_refused && axis_refuses_its_own_pair<axis>();
    }
    return all_refused;
}

// How many axes the pair check actually exercised.  An axis with one
// atom, or none, contributes a vacuous true above, so this is the number
// that carried weight.
template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::size_t axes_with_a_pair() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto axis_member : axis_range<First, End>) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis_has_two_atoms<axis>()) ++counted;
    }
    return counted;
}

// ---------------------------------------------------------------------
// Claim 3: a lifting atom runs in a context exactly when its row is
// empty.

template <std::size_t First, std::size_t End>
[[nodiscard]] consteval std::size_t lifting_atoms() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        if constexpr (fe::LiftsToRow<A>) ++counted;
    }
    return counted;
}

template <std::size_t First, std::size_t End>
[[nodiscard]] consteval bool every_lifting_atom_needs_its_row() noexcept {
    bool all_gated = true;
    template for (constexpr auto member : atom_range<First, End>) {
        using A = [:member:];
        if constexpr (fe::LiftsToRow<A>) {
            using Lifted = fe::lift_row_t<A>;
            constexpr bool empty_row = std::is_same_v<Lifted, fe::Row<>>;
            constexpr bool foreground_admits = fe::CtxAdmits<::fixy::HotFgCtx, Lifted>;
            all_gated = all_gated && (foreground_admits == empty_row);
        }
    }
    return all_gated;
}

#pragma GCC diagnostic pop

// ---------------------------------------------------------------------
// One part: the claims over its atoms and its axes, and its report.  Only
// the file of the part instantiates this template, so each translation
// unit walks one part.

template <std::size_t Part>
[[nodiscard]] PartReport report_of_part() noexcept {
    static_assert(Part < part_count);
    constexpr std::size_t first_atom = part_begin(Part, atom_members.size());
    constexpr std::size_t end_atom = part_begin(Part + 1, atom_members.size());
    constexpr std::size_t first_axis = part_begin(Part, axis_members.size());
    constexpr std::size_t end_axis = part_begin(Part + 1, axis_members.size());
    constexpr std::size_t atoms_walked = end_atom - first_atom;

    static_assert(atoms_clearing_the_structural_tiers<first_atom, end_atom>() == atoms_walked,
                  "an atom in a family roster does not clear fixy::fn's structural tiers on its own: it is not an "
                  "atom (tier 2), or the walk reads its axis twice (tier 4).  A roster member that cannot be "
                  "written alone cannot be written at all.");

    constexpr std::size_t refused = atoms_refused_alone<first_atom, end_atom>();
    static constexpr std::string_view refused_names = refused_alone_names<first_atom, end_atom>();

    // The list and the count have to agree about the same roster: an empty
    // list exactly when nothing is refused.
    static_assert(refused_names.empty() == (refused == 0),
                  "the named list of atoms refused alone and the count of them disagree about whether anything is "
                  "refused, so the two walks read different rosters.");

    // Each atom of the part alone is either accepted or refused at tier 5,
    // and no structural tier refuses it.  The two counts partition the
    // atoms of the part.  This is the claim of the first assertion, from
    // the other side.
    static_assert(refused <= atoms_walked);
    constexpr std::size_t accepted = atoms_accepted_alone<first_atom, end_atom>();
    static_assert(accepted + refused == atoms_walked,
                  "every atom alone must be either accepted or refused at tier 5.  A member counted in neither "
                  "was refused by a structural tier, which the assertion above already forbids, so the two walks "
                  "disagree about the roster.");

    static_assert(every_axis_refuses_its_own_pair<first_axis, end_axis>(),
                  "two atoms naming one axis must be refused by tier 4, in either order, while each alone clears "
                  "it.  A pair that reaches tier 5 means the one-grade-per-axis rule is not what refused it.");

    static_assert(every_lifting_atom_needs_its_row<first_atom, end_atom>(),
                  "an atom that lifts to a non-empty effect row was admitted by the foreground context, whose "
                  "row is empty, or one that lifts to nothing was refused by it.  The lift is what os mints fold "
                  "into the row a context has to admit, so either direction breaks that gate.");

    return PartReport{
        .part = Part,
        .atoms_walked = atoms_walked,
        .accepted_alone = accepted,
        .refused_alone = refused,
        .lifting_atoms = lifting_atoms<first_atom, end_atom>(),
        .axes_walked = end_axis - first_axis,
        .axes_with_a_pair = axes_with_a_pair<first_axis, end_axis>(),
        .refused_names = refused_names,
    };
}

// The report of each part.  rejections_generated_<k>.cpp defines
// report_part_<k>.
[[nodiscard]] PartReport report_part_0() noexcept;
[[nodiscard]] PartReport report_part_1() noexcept;
[[nodiscard]] PartReport report_part_2() noexcept;
[[nodiscard]] PartReport report_part_3() noexcept;
[[nodiscard]] PartReport report_part_4() noexcept;
[[nodiscard]] PartReport report_part_5() noexcept;
[[nodiscard]] PartReport report_part_6() noexcept;
[[nodiscard]] PartReport report_part_7() noexcept;

}  // namespace rejections_generated
