// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR runtime session mint fixture for
// safety::proto::swmr_session::mint_reader_runtime_session:
// the `::crucible::effects::IsExecCtx Ctx` template-parameter
// constraint rejects a non-ExecCtx first argument.
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_reader_runtime_session_wrong_handle.cpp: this
// supplies the CORRECT ReaderHandle (snap.reader()) but a plain struct
// that does NOT satisfy IsExecCtx as the ctx argument.  Mirrors
// neg_fixy_perm_root_non_exec_ctx.cpp.
//
// Expected diagnostic: IsExecCtx / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/sessions/SwmrSession.h>

namespace fswmr = ::crucible::safety::proto::swmr_session;
namespace conc = crucible::concurrent;

namespace neg_fixy_substr_swmr_reader_runtime_session_non_ctx {
struct UserTag {};
struct NotAnExecCtx {};
}  // namespace neg_fixy_substr_swmr_reader_runtime_session_non_ctx

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_reader_runtime_session_non_ctx::UserTag>;

    Snap snap{};
    auto reader = snap.reader();

    [[maybe_unused]] auto bad = fswmr::mint_reader_runtime_session<Snap>(
        neg_fixy_substr_swmr_reader_runtime_session_non_ctx::NotAnExecCtx{}, reader);
    return 0;
}
