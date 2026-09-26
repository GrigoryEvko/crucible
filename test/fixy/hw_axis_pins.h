// The compiler check for the hardware-axis blocks.
//
// A hardware-axis block is a namespace named `<site>_hw` that restates a
// hardware construct as fixy atoms: a SIMD ISA, a cache instruction or a
// memory fence that the preprocessor selects.  Each atom is a type alias
// in the block.  One row names a block and the axes it states.  The row
// is `static_assert(hw_axis_pins::pinned<^^block, Axis...>);` and the
// compiler walks the block by reflection when it evaluates the row.
//
// The row fails to compile in four cases:
//   - the block does not exist, because the reflection does not name
//     anything.  A block that a preprocessor conditional removes from this
//     build fails here as well.
//   - an axis of the row has no atom alias in the block
//   - an atom alias of the block engages an axis that the row does not list
//   - the row lists no axis, so it states nothing
//
// A type alias that is not an atom, such as the lattice alias of the
// deque block, is not an axis claim, so the check skips it.
// scripts/check-fixy-hw-discipline.py reads the rows from the parse tree
// of test/test_hardware_axis_pins.cpp and holds every `*_hw` namespace under
// include/ and src/ to them.  That script also refuses a block inside a
// preprocessor conditional, which the compiler sees only for the arm that
// one build selects.
//
// This header names only the fixy and foundation layers, so a negative
// fixture under test/fixy/neg/ can instantiate a row on a planted block.

#pragma once

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <utility>

namespace hw_axis_pins {

using AxisSet = std::array<bool, ::fixy::axis_count>;

// The axes that the atom aliases of the block engage.  Complexity: linear
// in the member count of the block.
template <std::meta::info Block>
[[nodiscard]] consteval AxisSet engaged_axes() {
    AxisSet engaged{};
    static constexpr auto block_members =
        std::define_static_array(std::meta::members_of(Block, std::meta::access_context::current()));
    template for (constexpr auto member : block_members) {
        if constexpr (std::meta::is_type_alias(member)) {
            using Alias = [:member:];
            if constexpr (::fixy::atom::IsAtom<Alias>) {
                engaged[std::to_underlying(Alias::axis)] = true;
            }
        }
    }
    return engaged;
}

template <::fixy::Axis... Listed>
[[nodiscard]] consteval AxisSet listed_axes() {
    AxisSet listed{};
    ((listed[std::to_underlying(Listed)] = true), ...);
    return listed;
}

// The names of the axes in `want` and not in `have`, each after a space.
[[nodiscard]] consteval std::string axes_missing_from(AxisSet const& want, AxisSet const& have) {
    std::string names;
    for (std::size_t index = 0; index < want.size(); ++index) {
        if (want[index] && !have[index]) {
            names += ' ';
            names += ::foundation::reflect::enum_name(static_cast<::fixy::Axis>(index));
        }
    }
    return names;
}

// Append one finding to the report, two spaces after the one before it.
consteval void add_finding(std::string& report, std::string const& finding) {
    if (!report.empty()) {
        report += "  ";
    }
    report += finding;
}

// An empty text when the block states exactly the listed axes.  Otherwise
// one finding for each mismatch, which the row prints as its failure.
template <std::meta::info Block, ::fixy::Axis... Listed>
[[nodiscard]] consteval std::string_view verdict() {
    AxisSet const engaged = engaged_axes<Block>();
    AxisSet const listed = listed_axes<Listed...>();
    std::string const block = std::string{std::meta::display_string_of(Block)};
    std::string report;
    if constexpr (sizeof...(Listed) == 0) {
        add_finding(report, "the row of the hardware-axis block " + block + " lists no axis, so it states nothing.");
    }
    if (std::string const lost = axes_missing_from(listed, engaged); !lost.empty()) {
        add_finding(report, "the hardware-axis block " + block + " holds no atom alias of the listed axis" + lost +
                                ".  Restore the alias, or take the axis off its row.");
    }
    if (std::string const extra = axes_missing_from(engaged, listed); !extra.empty()) {
        add_finding(report, "the hardware-axis block " + block + " holds an atom alias of the unlisted axis" + extra +
                                ".  Add the axis to its row.");
    }
    return std::string_view{std::define_static_string(report)};
}

template <std::meta::info Block, ::fixy::Axis... Listed>
struct pinned_row {
    static constexpr std::string_view report = verdict<Block, Listed...>();
    static_assert(report.empty(), report);
    static constexpr bool holds = true;
};

// The row itself.  Reading it instantiates pinned_row, so its assertion
// runs.  A row is a static_assert and not an explicit instantiation,
// because an explicit instantiation turns off the access check for the
// names in it, and scripts/check-proof-routes.py refuses that shape.
template <std::meta::info Block, ::fixy::Axis... Listed>
inline constexpr bool pinned = pinned_row<Block, Listed...>::holds;

}  // namespace hw_axis_pins
