// Each role with the crash annotation is in the crashed set (Definition
// 4.15, WA2).  G says that P crashed, and the state lists no crashed
// role, so the state is not well-annotated.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_annotation_not_crashed_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using Crashed = g::state_step_t<g::State<g::Roles<>, Guarded>, g::CrashAction<P>, OnlyQ>;
using CrashedCtx = c::step_t<c::crash_projected_context_t<Guarded, OnlyQ>, g::CrashAction<P>, OnlyQ>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<CrashedCtx, g::State<g::Roles<>, typename Crashed::type>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_annotation_not_crashed_types

using namespace neg_sess_crash_assoc_annotation_not_crashed_types;

int main() { return check_context(); }
