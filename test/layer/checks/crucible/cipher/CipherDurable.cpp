// The compile-time checks of crucible/cipher/CipherDurable.h.

#include <crucible/cipher/CipherDurable.h>

namespace crucible::cipher::durable::detail::durable_surface_invariants {

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

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

// An extra that engages no axis of the stance passes the gate, and an
// extra that engages the mode, the durability or the atomicity is refused.
static_assert(CtxFitsDurableMint<IoBlockCtx, warm_writer_stance>);
static_assert(CtxFitsDurableMint<IoBlockCtx, warm_writer_stance, atom_fs::with_flag<flag::NoFollow>>);
static_assert(CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::with_flag<flag::NoFollow>>);
static_assert(CtxFitsDurableMint<IoBlockCtx, head_advance_stance, atom_fs::with_flag<flag::NoFollow>>);
static_assert(!CtxFitsDurableMint<IoOnlyCtx, warm_writer_stance>, "a context without Block cannot open.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, warm_writer_stance, atom_fs::mode<open_mode::WriteCreate>>,
              "an extra that engages the mode the stance pins is refused.");
static_assert(!CtxFitsDurableMint<IoBlockCtx, cold_writer_stance, atom_fs::durable<sync_op::Fdatasync>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, head_advance_stance, atom_fs::atomic_write<atomicity::Rename>>);
static_assert(!CtxFitsDurableMint<IoBlockCtx, head_advance_stance, atom_fs::with_flag<flag::NoFollow>,
                                  atom_fs::durable<sync_op::Fsync>>,
              "one engaging extra among others is refused.");
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
static_assert(!std::is_constructible_v<WarmHandle, StanceFile<warm_writer_stance>&&>,
              "A file handle is evidence of a descriptor, not of the flags it was opened with, and the stance is a "
              "claim about those flags.  Only the mint that chose them can build the handle.");

// The handle holds the descriptor of a file, so it syncs the file.  The
// stance of the head advance flushes a parent directory, which takes the
// handle of the directory, so its handle has no sync.  The probe is a concept over the handle type, so
// a refused call is false and not an error of this file.
template <typename Handle>
concept SyncsAlone = requires(Handle const& handle, IoBlockCtx const& ctx) { handle.sync(ctx); };
static_assert(SyncsAlone<WarmHandle> && SyncsAlone<CipherDurableHandle<cold_writer_stance>>);
static_assert(!SyncsAlone<CipherDurableHandle<head_advance_stance>>,
              "an fsync of the descriptor of a file flushes no entry of its parent directory.");

}  // namespace crucible::cipher::durable::detail::durable_surface_invariants
