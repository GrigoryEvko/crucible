// The compile-time checks of fixy/os/CipherDurable.h.

#include <fixy/os/CipherDurable.h>

namespace fixy::cipher::durable::detail::durable_surface_invariants {

static_assert(DurableStance<warm_writer_stance>);
static_assert(DurableStance<cold_writer_stance>);
static_assert(DurableStance<head_advance_stance>);

static_assert(std::is_same_v<cold_writer_stance::atomicity_type, atomicity::Rename>,
              "cold_writer_stance must commit with Rename, so that a cold commit replaces an existing target.");
static_assert(std::is_same_v<warm_writer_stance::atomicity_type, atomicity::RenameAt2NoReplace>);
static_assert(std::is_same_v<head_advance_stance::sync_op_type, sync_op::FsyncParentDir>);
static_assert(std::is_same_v<cold_writer_stance::flag_type, flag::FullSync>);

// A stance missing an axis, or naming a tag the file surface does not
// know, is not a stance.
struct NoAtomicity final {
    using mode_type = open_mode::WriteCreate;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::Fsync;
};
struct UnknownMode final {};
struct StanceOverUnknownMode final {
    using mode_type = UnknownMode;
    using flag_type = flag::CloseOnExec;
    using sync_op_type = sync_op::Fsync;
    using atomicity_type = atomicity::Rename;
};
static_assert(!DurableStance<NoAtomicity>);
static_assert(!DurableStance<StanceOverUnknownMode>);

static_assert(detail::extras_engage_mode_v<atom_fs::mode<open_mode::WriteCreate>>);
static_assert(!detail::extras_engage_mode_v<>);
static_assert(!detail::extras_engage_mode_v<atom_fs::with_flag<flag::NoFollow>>);
static_assert(detail::extras_engage_durable_v<atom_fs::durable<sync_op::Fsync>>);
static_assert(!detail::extras_engage_durable_v<atom_fs::with_flag<flag::Direct>>);
static_assert(detail::extras_engage_atomic_write_v<atom_fs::atomic_write<atomicity::Rename>>);
static_assert(!detail::extras_engage_atomic_write_v<atom_fs::with_flag<flag::NoFollow>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsDurableMint<IoBlockCtx, warm_writer_stance>);
static_assert(CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::with_flag<flag::NoFollow>>);
static_assert(!CtxFitsDurableMint<IoOnlyCtx, warm_writer_stance>, "a context without Block cannot open.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, warm_writer_stance, atom_fs::mode<open_mode::WriteCreate>>,
              "an extra that engages the mode the stance pins is refused.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::durable<sync_op::Fdatasync>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, head_advance_stance, atom_fs::atomic_write<atomicity::Rename>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, StanceOverUnknownMode>);

// The handle's constructor over a descriptor is private and the three
// mints are its sole friends.  Checked from a scope none of them
// befriends.
using WarmHandle = CipherDurableHandle<warm_writer_stance>;
static_assert(std::is_default_constructible_v<WarmHandle>);
static_assert(std::is_move_constructible_v<WarmHandle>);
static_assert(!std::is_copy_constructible_v<WarmHandle>);
static_assert(!std::is_copy_assignable_v<WarmHandle>);
static_assert(std::is_nothrow_destructible_v<WarmHandle>);
static_assert(!std::is_constructible_v<WarmHandle, ::fixy::fs::OwnedFd&&>,
              "An OwnedFd is evidence of a descriptor, not of the flags it was opened with, and the stance is a "
              "claim about those flags.  Only the mint that chose them can build the handle.");

}  // namespace fixy::cipher::durable::detail::durable_surface_invariants
