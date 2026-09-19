// Sentinel TU for fixy/Atom.h and the six family headers under
// fixy/atoms/: every family's roster walks as atoms, every atom's axis
// is an enumerator of fixy::Axis, every OS atom lifts to an effect row,
// the halves of the recipe on their own are not atoms, and the atoms
// construct at runtime under the sanitizers.

#include <fixy/Atom.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/Axis.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace {

namespace fa = ::fixy::atom;
namespace fad = ::fixy::atom::detail;
namespace fe = ::foundation::effects;
using ::fixy::Axis;

// One roster per family, joined into one list.  The family rosters are
// the only hand lists; everything below derives from them.
using every_atom_roster =
    fad::roster_cat_t<fad::core_atom_roster, fad::ctrl_atom_roster, fad::dispatch_atom_roster, fad::global_atom_roster,
                      fad::stack_atom_roster, fad::stdio_atom_roster, fad::os_atom_roster>;

static_assert(std::tuple_size_v<every_atom_roster>
              == std::tuple_size_v<fad::core_atom_roster> + std::tuple_size_v<fad::ctrl_atom_roster>
                     + std::tuple_size_v<fad::dispatch_atom_roster> + std::tuple_size_v<fad::global_atom_roster>
                     + std::tuple_size_v<fad::stack_atom_roster> + std::tuple_size_v<fad::stdio_atom_roster>
                     + std::tuple_size_v<fad::os_atom_roster>);

// IsAtom for each family, and for the union.
static_assert(fad::every_roster_member_is_atom_<fad::core_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::ctrl_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::dispatch_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::global_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::stack_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::stdio_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<fad::os_atom_roster>());
static_assert(fad::every_roster_member_is_atom_<every_atom_roster>());

// Each single-axis family sits on its axis.
static_assert(fad::every_roster_member_on_axis_<fad::ctrl_atom_roster, Axis::ControlFlow>());
static_assert(fad::every_roster_member_on_axis_<fad::dispatch_atom_roster, Axis::CallShape>());
static_assert(fad::every_roster_member_on_axis_<fad::global_atom_roster, Axis::GlobalState>());
static_assert(fad::every_roster_member_on_axis_<fad::stack_atom_roster, Axis::StackUse>());
static_assert(fad::every_roster_member_on_axis_<fad::stdio_atom_roster, Axis::Stdio>());
static_assert(fad::every_roster_member_on_axis_<fad::os_atom_roster, Axis::SyscallSurface>());

// Every atom's axis is a real enumerator: a second walk over the enum,
// independent of the one inside the roster check.
[[nodiscard]] consteval bool every_atom_axis_is_an_enumerator() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : fad::roster_members_v<every_atom_roster>) {
        using A = [:member:];
        bool found = false;
        template for (constexpr auto en : axes) {
            if (A::axis == [:en:]) found = true;
        }
        if (!found) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_atom_axis_is_an_enumerator());

// The atom is the sole surviving piece of a binding's axis claim, so
// the atom of a spelled axis and the axis's own name agree.
[[nodiscard]] consteval bool every_atom_axis_has_a_name() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : fad::roster_members_v<every_atom_roster>) {
        using A = [:member:];
        if (::fixy::axis_name(A::axis) == std::string_view{"<unknown Axis>"}) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_atom_axis_has_a_name());

// An OS atom's lift is a Row, and the three families lift to IO and
// Block.  The core families do not lift at all.
static_assert(fad::every_roster_member_lifts_<fad::os_atom_roster>());
static_assert(std::is_same_v<fe::lift_row_t<fa::io::engine<::fixy::io::engine::IoUring>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<fa::fs::mode<::fixy::fs::open_mode::ReadOnly>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<fa::mmap::trusted_jit>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<fa::leak::resource<fad::leak_sample_rationale>>, fe::Row<>>);
static_assert(!fe::LiftsToRow<fa::affine>);
static_assert(!fe::LiftsToRow<fa::ctrl::unreachable_ok>);
static_assert(!fad::every_roster_member_lifts_<fad::core_atom_roster>());

// The halves of the recipe on their own are not atoms.
struct not_final : fa::atom_of<Axis::Usage> {};
struct no_axis final : fa::atom_base {};
struct wrong_axis_type final : fa::atom_base {
    static constexpr int axis = 2;
};
struct axis_but_no_base final {
    [[maybe_unused]] static constexpr Axis axis = Axis::Usage;
};
static_assert(!fa::IsAtom<not_final>);
static_assert(!fa::IsAtom<no_axis>);
static_assert(!fa::IsAtom<wrong_axis_type>);
static_assert(!fa::IsAtom<axis_but_no_base>);
static_assert(!fa::IsAtom<fa::atom_base>);
static_assert(!fa::IsAtom<fa::atom_of<Axis::Usage>>);

// The gate rejects a cv-qualified or reference-qualified atom rather
// than stripping it.
static_assert(fa::IsAtom<fa::affine>);
static_assert(!fa::IsAtom<const fa::affine>);
static_assert(!fa::IsAtom<volatile fa::affine>);
static_assert(!fa::IsAtom<fa::affine&>);
static_assert(!fa::IsAtom<const fa::affine&>);
static_assert(!fa::IsAtom<fa::affine&&>);

// A roster that lists a non-atom fails the walk, so the walk is a
// real check and not a restatement.
static_assert(!fad::every_roster_member_is_atom_<std::tuple<fa::affine, not_final>>());
static_assert(!fad::every_roster_member_on_axis_<std::tuple<fa::affine, fa::as_public>, Axis::Usage>());

// The parametric atoms keep their parameter in the type.
static_assert(!std::is_same_v<fa::with<fe::Effect::IO>, fa::with<fe::Effect::Bg>>);
static_assert(std::is_same_v<fa::with_io, fa::with<fe::Effect::IO>>);
static_assert(!std::is_same_v<fa::version<1>, fa::version<2>>);
static_assert(!std::is_same_v<fa::ctrl::abort<"a">, fa::ctrl::abort<"b">>);

// Every atom constructs at runtime, is one byte, and names an axis the
// runtime name walk resolves.
[[nodiscard]] bool every_atom_constructs_at_runtime() noexcept {
    bool all_named = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : fad::roster_members_v<every_atom_roster>) {
        using A = [:member:];
        [[maybe_unused]] A atom{};
        [[maybe_unused]] A copied = atom;
        volatile Axis axis = A::axis;
        if (::fixy::axis_name(axis) == std::string_view{"<unknown Axis>"}) all_named = false;
        if (sizeof(A) != 1) all_named = false;
    }
#pragma GCC diagnostic pop
    return all_named;
}

}  // namespace

int main() {
    if (!every_atom_constructs_at_runtime()) return 1;
    volatile Axis axis = fa::ctrl::unreachable_ok::axis;
    if (::fixy::axis_name(axis) != "ControlFlow") return 2;
    return 0;
}
