// The run of rejections_generated.  rejections_generated.h gives the walk
// over the atom catalog and its parts, and rejections_generated_<k>.cpp
// walks part k.  This file holds main, the proof that the parts cover
// each input one time, and the floors over the full catalog.

#include "rejections_generated.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace rejections_generated {
namespace {

// The parts of an input of `total` items follow each other with no gap
// and no overlap, and together they hold the `total` items.  Each claim
// then reads each item of its input in one part, and in no other part.
[[nodiscard]] consteval bool parts_cover(std::size_t total) noexcept {
    std::size_t covered = 0;
    for (std::size_t part = 0; part < part_count; ++part) {
        const std::size_t first = part_begin(part, total);
        const std::size_t end = part_begin(part + 1, total);
        if (first != covered || end < first) return false;
        covered = end;
    }
    return covered == total;
}

static_assert(parts_cover(atom_members.size()),
              "the parts of the roster do not cover each atom one time, so claims 1 and 3 lose or repeat a case.");
static_assert(parts_cover(axis_members.size()),
              "the parts of the Axis enumerators do not cover each axis one time, so claim 2 loses or repeats a case.");

inline constexpr std::size_t census_axes_with_a_pair = axes_with_a_pair<0, axis_members.size()>();
inline constexpr std::size_t census_lifting_atoms = lifting_atoms<0, atom_members.size()>();

// The walk would be vacuous if no axis had two atoms.  Eight axes have
// no atom at all (collision::pending_axes) and Type has none by design,
// so this floor says the generated pairs cover most of the rest.
static_assert(census_axes_with_a_pair >= 15,
              "fewer than fifteen axes have two rostered atoms, so the generated-pair walk covers much less "
              "than it did.  Either the rosters shrank or the join lost a family.  The floor is deliberately "
              "below the current count (19) so that reworking one family does not red this TU, and deliberately "
              "not an equality so that ADDING an atom does not either.");

// The lift walk is only as strong as the number of atoms that lift.
static_assert(census_lifting_atoms >= 30, "fewer than thirty rostered atoms carry a lift, so the context-fit walk "
                                          "covers much less than it did.  The SyscallSurface families of "
                                          "fixy/atoms/Os.h and fixy/atoms/Syscall.h are most of what lifts, and "
                                          "the run prints the current count.");

}  // namespace
}  // namespace rejections_generated

int main() {
    namespace rg = ::rejections_generated;
    // The claims are constant-evaluated in the files of the parts.
    // Printing the shape of the input set is what a reader needs to judge
    // whether the walk covered anything, and it runs, so a build that
    // somehow folded the walks away still reports a count.
    const std::array<rg::PartReport, rg::part_count> reports{
        rg::report_part_0(), rg::report_part_1(), rg::report_part_2(), rg::report_part_3(),
        rg::report_part_4(), rg::report_part_5(), rg::report_part_6(), rg::report_part_7(),
    };

    rg::PartReport total{};
    bool is_each_part_in_its_place = true;
    for (std::size_t index = 0; index < reports.size(); ++index) {
        const rg::PartReport& report = reports[index];
        is_each_part_in_its_place = is_each_part_in_its_place && report.part == index;
        total.atoms_walked += report.atoms_walked;
        total.accepted_alone += report.accepted_alone;
        total.refused_alone += report.refused_alone;
        total.lifting_atoms += report.lifting_atoms;
        total.axes_walked += report.axes_walked;
        total.axes_with_a_pair += report.axes_with_a_pair;
    }

    std::printf("rejections_generated: %zu atoms walked, %zu accepted alone, %zu refused at tier 5,\n",
                rg::atom_members.size(), total.accepted_alone, total.refused_alone);
    std::printf("                      %zu axes contributed a same-axis pair, %zu atoms carry an effect lift\n",
                total.axes_with_a_pair, total.lifting_atoms);
    std::printf("                      refused alone: ");
    bool is_first_name = true;
    for (const rg::PartReport& report : reports) {
        if (report.refused_names.empty()) continue;
        if (!is_first_name) std::printf(", ");
        is_first_name = false;
        std::printf("%.*s", static_cast<int>(report.refused_names.size()), report.refused_names.data());
    }
    std::printf("\n");

    if (rg::atom_members.empty()) return 1;
    if (total.accepted_alone + total.refused_alone != rg::atom_members.size()) return 2;
    if (total.axes_with_a_pair < 15) return 3;
    if (total.lifting_atoms < 30) return 4;
    // The parts walked every atom and every axis, each part in its own
    // place, and the counts of the parts agree with the counts over the
    // full catalog.
    if (!is_each_part_in_its_place || total.atoms_walked != rg::atom_members.size()
        || total.axes_walked != rg::axis_members.size() || total.axes_with_a_pair != rg::census_axes_with_a_pair
        || total.lifting_atoms != rg::census_lifting_atoms)
        return 5;
    return 0;
}
