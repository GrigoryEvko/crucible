// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074d HS14 fixture #2 for fixy::substr::mint_substrate_session:
// the ctx-bound factory's template-parameter constraint
// `::crucible::effects::IsExecCtx Ctx` rejects when the supplied first
// argument's type does not satisfy IsExecCtx.
//
// Distinct mismatch class from
// neg_fixy_substr_substrate_session_non_bridgeable.cpp (#1): that
// supplies a valid HotFgCtx but a non-bridgeable direction (fails
// IsBridgeableDirection); this supplies a bridgeable (Snap,
// SwmrWriter) — so the handle parameter type is well-formed — but a
// plain struct that does NOT satisfy IsExecCtx as the ctx argument,
// failing the upstream template-parameter constraint before the
// residency / protocol conjuncts are reached.  Mirrors
// neg_fixy_perm_root_non_exec_ctx.cpp's non-ExecCtx route.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;

namespace neg_fixy_substr_substrate_session_non_ctx {
struct UserTag {};

// A plain struct deliberately lacking every ExecCtx machinery —
// no row_type, no Cap, no residency / heat / workload tags.
struct NotAnExecCtx {};
}  // namespace neg_fixy_substr_substrate_session_non_ctx

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_substrate_session_non_ctx::UserTag>;

    // SwmrWriter IS a bridgeable Snapshot direction, so handle_for_t<Snap,
    // SwmrWriter> = WriterHandle is well-formed; the rejection is solely
    // on the non-ExecCtx first argument.
    typename Snap::WriterHandle* fake_handle = nullptr;

    [[maybe_unused]] auto bad =
        fsubstr::mint_substrate_session<Snap, conc::Direction::SwmrWriter>(
            neg_fixy_substr_substrate_session_non_ctx::NotAnExecCtx{},
            *fake_handle);
    return 0;
}
