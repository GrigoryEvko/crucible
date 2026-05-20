// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074e fixture #1 for fixy::substr::swmr::mint_writer_session:
// the ctx-bound factory's `::crucible::effects::IsExecCtx Ctx`
// template-parameter constraint rejects a non-ExecCtx first argument.
//
// Distinct mismatch class from neg_fixy_substr_swmr_wrong_handle.cpp
// (#2, role swap — passes a ReaderHandle where WriterHandle is
// expected): this supplies a VALID WriterHandle but a plain struct
// that does NOT satisfy IsExecCtx as the ctx argument, failing the
// upstream template-parameter constraint.  Mirrors
// neg_fixy_perm_root_non_exec_ctx.cpp's non-ExecCtx route.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_swmr_writer_session_non_ctx {
struct UserTag {};
struct NotAnExecCtx {};
}  // namespace neg_fixy_substr_swmr_writer_session_non_ctx

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_swmr_writer_session_non_ctx::UserTag>;

    Snap snap{};
    auto writer = snap.writer(
        fsafe::mint_permission_root<typename Snap::writer_tag>());

    // Valid WriterHandle, but NotAnExecCtx fails the IsExecCtx Ctx
    // template-parameter constraint of mint_writer_session.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_writer_session<Snap>(
            neg_fixy_substr_swmr_writer_session_non_ctx::NotAnExecCtx{},
            writer);
    return 0;
}
