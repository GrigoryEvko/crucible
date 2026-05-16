#pragma once

// ── crucible::fixy::stance — Migration.h (FIXY-G13) ───────────────────
//
// Stance migration paths between historical and current versions.
// When a Cipher cold-tier artifact carries `(stance_id, version_v1)`
// and today's binary supports `(stance_id, version_v2)`, the migration
// machinery walks the recorded grade vector through the declared
// `stance_migration<OldS, NewS>` evolution.  If no migration path
// exists, load fails with `MissingMigrationPath<OldS, NewS>`.
//
// **Identity migration (default).**  Every stance has a trivial
// migration from itself to itself.  Cross-version migration is
// OPT-IN via specialization.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   stance_migration<O, N>::migrate_v       — bool — path exists?
//   stance_migration<O, N>::migrate(...)    — actual migration step
//   migrate_t<O, N, GradeT>                 — result type or MissingPath
//   MissingMigrationPath<O, N>              — diagnostic carrier
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §7 G13           — temporal grade stability
//   fixy/stance/Version.h                    — companion version header

#include <crucible/fixy/stance/Version.h>

#include <type_traits>

namespace crucible::fixy::stance {

// ═════════════════════════════════════════════════════════════════════
// ── MissingMigrationPath<OldS, NewS> — diagnostic carrier ──────────
// ═════════════════════════════════════════════════════════════════════

template <typename OldS, typename NewS>
struct MissingMigrationPath {
    static constexpr const char* description =
        "stance::migrate<OldS, NewS> rejects: no migration path declared "
        "between the requested stance versions.  Declare a "
        "stance_migration<OldS, NewS> specialization with `migrate_v = "
        "true` and a `migrate(old_grade)` member that returns the new "
        "stance's grade — OR widen the consumer's accept_versions "
        "window so the historical version is admitted directly.";
};

// ═════════════════════════════════════════════════════════════════════
// ── stance_migration<OldS, NewS> — migration path declaration ──────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: no migration.  Specializations declare both
// `migrate_v = true` and a `migrate(old_grade) -> new_grade` operator.
//
// Identity case (OldS == NewS) is partial-specialized to true.

template <typename OldS, typename NewS>
struct stance_migration {
    static constexpr bool migrate_v = false;
};

template <typename S>
struct stance_migration<S, S> {
    static constexpr bool migrate_v = true;

    // Identity migration: payload passes through unchanged.
    template <typename GradeT>
    [[nodiscard]] static constexpr GradeT migrate(GradeT v) noexcept {
        return v;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── migrate<OldS, NewS> — public entry ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename OldS, typename NewS, typename GradeT>
[[nodiscard]] constexpr auto migrate(GradeT v) noexcept {
    static_assert(stance_migration<OldS, NewS>::migrate_v,
        "stance::migrate<OldS, NewS> rejects: no migration path declared.  "
        "Specialize stance_migration<OldS, NewS> with `migrate_v = true` "
        "and a `migrate(old_grade)` member, OR widen the consumer's "
        "accept_versions window to admit the historical version directly.  "
        "See stance::MissingMigrationPath<OldS, NewS> for the structured "
        "diagnostic carrier.");
    return stance_migration<OldS, NewS>::migrate(v);
}

// ═════════════════════════════════════════════════════════════════════
// ── migrate_t<OldS, NewS> — migration availability carrier ──────────
// ═════════════════════════════════════════════════════════════════════

template <typename OldS, typename NewS>
inline constexpr bool can_migrate_v =
    stance_migration<OldS, NewS>::migrate_v;

template <typename OldS, typename NewS>
using migrate_path_t = std::conditional_t<
    can_migrate_v<OldS, NewS>,
    stance_migration<OldS, NewS>,
    MissingMigrationPath<OldS, NewS>>;

// ═════════════════════════════════════════════════════════════════════
// ── stance_version_witness<S, V> — version witness coupling (G9) ────
// ═════════════════════════════════════════════════════════════════════
//
// Each (stance, version) pair carries a witness tier declaring "this
// version of S has been validated to W tier".  Default: Asserted<>
// (the substrate's trusted-by-default floor).  Production-critical
// pathways tighten the floor by specializing.

template <typename S, std::uint16_t V>
struct stance_version_witness {
    using type = ::crucible::safety::witness::Asserted<>;
};

template <typename S, std::uint16_t V>
using stance_version_witness_t = typename stance_version_witness<S, V>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace migration_self_test {

// Identity migration: every stance tag v1 → v1 trivially succeeds.
static_assert(can_migrate_v<BgWorkerTag, BgWorkerTag>);
static_assert(can_migrate_v<PureLinearTag, PureLinearTag>);
static_assert(can_migrate_v<CipherColdWriterTag, CipherColdWriterTag>);

// Cross-stance migration default = false (no path declared).
static_assert(!can_migrate_v<BgWorkerTag, PureLinearTag>);
static_assert(!can_migrate_v<CipherColdWriterTag, BgWorkerTag>);

// migrate_path_t routes through MissingMigrationPath when no path.
static_assert(std::is_same_v<
    migrate_path_t<BgWorkerTag, PureLinearTag>,
    MissingMigrationPath<BgWorkerTag, PureLinearTag>>);

// Identity migration preserves payload.
static_assert(migrate<BgWorkerTag, BgWorkerTag>(42) == 42);
static_assert(migrate<PureLinearTag, PureLinearTag>(7) == 7);

// stance_version_witness default = Asserted<>.
static_assert(std::is_same_v<
    stance_version_witness_t<BgWorkerTag, 1>,
    ::crucible::safety::witness::Asserted<>>);

}  // namespace migration_self_test

}  // namespace crucible::fixy::stance
