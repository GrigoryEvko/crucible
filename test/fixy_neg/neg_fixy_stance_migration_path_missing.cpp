// ── neg_fixy_stance_migration_path_missing (FIXY-G13 HS14) ────────────
//
// Pin temporal grade stability: stance::migrate<OldS, NewS> rejects
// when no migration path is declared.  Cross-stance migration without
// an explicit `stance_migration<Old, New>` specialization fires the
// static_assert in `migrate<>`'s body.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

namespace cs = crucible::fixy::stance;

namespace {

// BgWorker → PureLinear is NOT a declared migration path.
// Sanity: can_migrate_v reports false.
static_assert(!cs::can_migrate_v<cs::BgWorkerTag, cs::PureLinearTag>);

// THE DISCIPLINE: calling migrate<> fires the static_assert.  A
// templated function instantiation is needed at consteval to force
// the body's static_assert to fire.  Use the result of migrate<> in
// a constant-expression context — the static_assert is in a non-
// dependent code path so the compiler fires it on template
// instantiation.
constexpr int kMigrated = cs::migrate<cs::BgWorkerTag, cs::PureLinearTag>(42);
[[maybe_unused]] constexpr int kForce = kMigrated;

}  // namespace

int main() { return 0; }
