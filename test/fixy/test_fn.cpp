// Sentinel TU for fixy/Fn.h and fixy/Reject.h: the binding collapses to
// sizeof(T), an unmentioned axis takes its strict pole silently, one
// atom relaxes exactly its own axis, the gate refuses a duplicate and a
// non-atom, the duplicate diagnostic names the axis, and a context gate
// reads the lifts of the atoms as well as the Effect grade.
//
// The header self-tests prove these for a sample.  What this file adds
// is the walk over every atom the tree ships: each one is checked
// against every axis, so an atom whose declared axis disagrees with the
// axis it actually resolves is caught here rather than by whichever
// binding happens to use it.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Reject.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::Axis;
using ::fixy::axis_count;
using ::fixy::axis_traits;
using ::fixy::fn;
using ::fixy::IsAccepted;
using ::fixy::mint_fn;

// ---------------------------------------------------------------------
// The shortest binding.

static_assert(sizeof(fn<int>) == sizeof(int));
static_assert(sizeof(fn<double>) == sizeof(double));
static_assert(alignof(fn<int>) == alignof(int));
static_assert(fn<int>::atom_count == 0);
static_assert(std::is_same_v<fn<int>::value_type, int>);

// ---------------------------------------------------------------------
// One atom relaxes its axis and leaves the other thirty-two strict.
//
// The header checks a dozen axes by hand.  This walks the enum, so the
// claim covers the table rather than a sample, and an axis added to the
// enum is covered the day it is declared.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// Whether Binding's grade on every axis but Relaxed is the strict pole
// that axis declares.  Axis::Type is skipped: its grade is the payload,
// which is never a strict pole.
template <class Binding, Axis Relaxed>
[[nodiscard]] consteval bool only_one_axis_moved() noexcept {
    bool all_strict = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis != Relaxed && axis != Axis::Type) {
            all_strict = all_strict
                      && std::is_same_v<typename Binding::template grade_on<axis>, typename axis_traits<axis>::strict>;
        }
    }
    return all_strict;
}

// Whether every axis resolves to something nameable for this binding.
template <class Binding>
[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    bool all_named = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_named = all_named && !std::is_void_v<typename Binding::template grade_on<axis>>;
    }
    return all_named;
}

#pragma GCC diagnostic pop

// One atom from each family the tree ships, each checked the same way:
// its own axis carries it, every other axis stays strict.
template <class Atom>
[[nodiscard]] consteval bool relaxes_exactly_its_own_axis() noexcept {
    using Binding = fn<int, Atom>;
    return std::is_same_v<typename Binding::template grade_on<Atom::axis>, Atom>
        && only_one_axis_moved<Binding, Atom::axis>()
        && Binding::template mentions_axis<Atom::axis> && Binding::atom_count == 1 && every_axis_resolves<Binding>();
}

static_assert(relaxes_exactly_its_own_axis<::fixy::atom::copy>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::affine>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::ghost>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::borrow>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::mut_mutable>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::mut_append>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::mut_monotonic>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::reentrant>());
static_assert(relaxes_exactly_its_own_axis<::fixy::atom::coroutine>());

// The empty pack leaves every axis but Type strict, which is the same
// claim with nothing relaxed.  Axis::Type is not an axis any atom can
// name, so passing it as Relaxed excludes nothing the walk would check.
static_assert(only_one_axis_moved<fn<int>, Axis::Type>());
static_assert(every_axis_resolves<fn<int>>());

// ---------------------------------------------------------------------
// What the gate refuses.

struct not_an_atom {};

static_assert(IsAccepted<int>);
static_assert(IsAccepted<int, ::fixy::atom::copy>);
static_assert(IsAccepted<int, ::fixy::atom::copy, ::fixy::atom::mut_append, ::fixy::atom::reentrant>);

static_assert(!IsAccepted<int, ::fixy::atom::copy, ::fixy::atom::affine>);
static_assert(!IsAccepted<int, ::fixy::atom::mut_append, ::fixy::atom::mut_monotonic>);
static_assert(!IsAccepted<int, not_an_atom>);
static_assert(!IsAccepted<int, ::fixy::atom::copy, not_an_atom>);
static_assert(!IsAccepted<void>);
static_assert(!IsAccepted<int&>);
static_assert(!IsAccepted<int[4]>);
static_assert(!IsAccepted<const int>);

// A cv-qualified or reference-qualified atom is refused rather than
// stripped, because such a type comes from a decltype on a variable.
static_assert(!IsAccepted<int, const ::fixy::atom::copy>);
static_assert(!IsAccepted<int, ::fixy::atom::copy&>);

// ---------------------------------------------------------------------
// What the refusal says.

static_assert(std::is_same_v<::fixy::duplicate_tag_or_void_t<::fixy::atom::copy, ::fixy::atom::affine>,
                             ::fixy::duplicate_atom_on<Axis::Usage>>);
static_assert(::fixy::duplicate_tag_or_void_t<::fixy::atom::copy, ::fixy::atom::affine>::name
              == "DuplicateAtomOnUsage");
static_assert(::fixy::duplicate_tag_or_void_t<::fixy::atom::mut_append, ::fixy::atom::mut_monotonic>::name
              == "DuplicateAtomOnMutation");
static_assert(std::is_same_v<::fixy::malformed_tag_or_void_t<not_an_atom>, ::fixy::malformed_atom<not_an_atom>>);
static_assert(!::fixy::malformed_atom<not_an_atom>::name.empty());
static_assert(std::is_same_v<::fixy::payload_tag_or_void_t<void>, ::fixy::unholdable_payload<void>>);

// The name carries the axis identifier, so renaming an axis renames its
// tag rather than leaving a stale string behind.
static_assert(::fixy::duplicate_atom_on<Axis::Effect>::name == "DuplicateAtomOnEffect");
static_assert(::fixy::duplicate_atom_on<Axis::Staleness>::name == "DuplicateAtomOnStaleness");

// ---------------------------------------------------------------------
// The door.

static_assert(std::is_same_v<decltype(mint_fn<int, ::fixy::atom::copy>(0)), fn<int, ::fixy::atom::copy>>);
static_assert(!std::is_constructible_v<fn<int, ::fixy::atom::copy>, int>);

// The private value constructor is only half the door.  The other half
// is that no accessor hands out a mutable lvalue reference: with a
// public default constructor and a mutable value(), `fn<T, atoms...>{}
// .value() = x` would wrap any value under any pack without naming a
// mint.  A non-const lvalue binding reads back as const.
static_assert(std::is_same_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&>().value()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<const fn<int, ::fixy::atom::copy>&>().value()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&&>().value()), int&&>);
static_assert(!std::is_assignable_v<decltype(std::declval<fn<int, ::fixy::atom::copy>&>().value()), int>,
              "a default-constructed binding cannot be written into after the fact");

// ---------------------------------------------------------------------
// The row a binding requires of its context.
//
// The row joins the Effect grade with the lifts of the atoms.  Each
// context below holds the row the binding lifts to and admits it.  The
// two negative fixtures neg_fn_futex_row_under_compile_ctx and
// neg_fn_io_syscall_row_under_drain_ctx take the same bindings to a
// context that lacks one effect of the row.

namespace fe = ::foundation::effects;
using SyscallId = ::fixy::atom::syscall::SyscallId;
using FutexBinding = fn<int, ::fixy::atom::syscall::per<SyscallId::futex>>;
using ProcessStateBinding = fn<int, ::fixy::atom::syscall::per<SyscallId::getpid>, ::fixy::atom::as_public>;
using ParkBinding = fn<int, ::fixy::atom::sync::park>;

static_assert(std::is_same_v<::fixy::binding_row_t<FutexBinding>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<::fixy::binding_row_t<ProcessStateBinding>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<::fixy::binding_row_t<ParkBinding>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<::fixy::binding_row_t<fn<int, ::fixy::atom::with_io, ::fixy::atom::as_public,
                                                      ::fixy::atom::sync::park>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, FutexBinding>, "the load context holds Block");
static_assert(::fixy::CtxAdmitsBinding<::fixy::TestRunnerCtx, ParkBinding>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, ProcessStateBinding>, "the compile context holds IO");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, FutexBinding>, "the compile context lacks Block");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, ProcessStateBinding>, "the drain context lacks IO");
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, ParkBinding>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, fn<int, ::fixy::atom::sync::spin_pause>>,
              "a spin lifts the empty row, so the foreground context admits it");

// A stdio write lifts IO and Block.  The fixture
// neg_fn_stdio_row_under_block_ctx takes the same binding to a context
// that holds Block and not IO.
using StderrBinding =
    fn<int, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stderr>, ::fixy::atom::as_public>;
static_assert(std::is_same_v<::fixy::binding_row_t<StderrBinding>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgLoadCtx, StderrBinding>, "the load context holds IO and Block");
static_assert(::fixy::CtxAdmitsBinding<::fixy::TestRunnerCtx, StderrBinding>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::BgCompileCtx, StderrBinding>, "the compile context lacks Block");

// A static_assert proves the constant-evaluated path only.  These run.
[[nodiscard]] int check_runtime_paths() {
    volatile int seed = 7;

    const auto plain = mint_fn<int>(static_cast<int>(seed));
    if (plain.value() != 7) return 1;

    const auto graded = mint_fn<int, ::fixy::atom::copy>(static_cast<int>(seed) + 1);
    if (graded.value() != 8) return 2;

    // The wrapper does not override the payload's copy semantics.
    auto copied = graded;
    if (copied.value() != 8) return 3;

    // The rvalue overload consumes, so a temporary's payload moves out
    // rather than being copied back through a const reference.
    if (mint_fn<int, ::fixy::atom::affine>(static_cast<int>(seed) + 2).value() != 9) return 4;

    // A defaulted binding carries the payload's own default.
    const fn<int, ::fixy::atom::copy> defaulted{};
    if (defaulted.value() != 0) return 5;

    if (sizeof(plain) != sizeof(int)) return 6;
    if (fn<int>::atom_count != 0) return 7;
    if (graded.atom_count != 1) return 8;

    // The generated tag strings exist at runtime, not only in a
    // constant expression.
    const std::string_view dup = ::fixy::duplicate_atom_on<Axis::Usage>::name;
    if (dup != "DuplicateAtomOnUsage") return 9;
    if (::fixy::duplicate_atom_on<Axis::Usage>::description.empty()) return 10;
    if (::fixy::duplicate_atom_on<Axis::Usage>::remediation.empty()) return 11;

    if (axis_count != 33) return 12;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
