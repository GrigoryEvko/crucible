// FIXY-V-249 sentinel TU: CollisionCatalog rule G002 (thread_local × atomic).
//
// G002 is the catalog codification of Scenario E (bench_smoke.cpp:78): a
// grant::global::thread_local_<Tag> PAIRED with an atomic memory-order
// wrapper is a category error — an atomic op on a per-thread object orders
// against no peer (one instance per thread). Marker-driven (the grant layer
// specializes marks_thread_local_atomic when both are present), exactly as
// G001/D001/D002/S004 (V-243) are.
//
// Sentinel witnesses:
//   (a) catalog cardinality floor (>= 37) + G002 rule_bijection cell.
//   (b) CollisionDiagnosticByRule<F, G002>::rule_code() string identity.
//   (c) marker default-SAFE: a Fn WITHOUT the marker trips no rule.
//   (d) POSITIVE — a plain thread_local-tag Fn and a plain atomic-tag Fn,
//       absent the paired marker, both pass ValidComposition.
//   (e) NEGATIVE — the marker firing G002 is covered by the 2 HS14
//       neg-compile fixtures; see the closing note.

#include <crucible/safety/Fn.h>   // pulls the CollisionCatalog body

#include <atomic>
#include <string_view>
#include <type_traits>

namespace csfn = ::crucible::safety::fn;
namespace csc  = ::crucible::safety::fn::collision;

namespace {

// ── (a) Catalog cardinality floor + bijection ──────────────────────
static_assert(csc::catalog_size >= 37,
              "FIXY-V-249 floor: catalog must include G002");
static_assert(csc::rule_bijection_v<csc::RuleCode::G002>);

// ── (b) Diagnostic-string identity ─────────────────────────────────
using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::G002>::rule_code()
              == std::string_view{"G002"});

// ── (c) Marker is default-SAFE (opt-in, grant-driven) ──────────────
static_assert(!csc::marks_thread_local_atomic<DefaultFn>::value);
static_assert(csc::G002_OK<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// ── (d) POSITIVE compositions — neither leg alone trips G002 ───────
//
// G002 needs BOTH thread_local storage AND atomic synchronization; a
// carrier with only one of them (marker unset) must pass. These witness
// that the rule is not spuriously firing on a lone thread_local or a lone
// atomic — only the nonsensical pairing is rejected.
struct ThreadLocalOnlyTag {};   // models "thread_local, no atomic"
struct AtomicOnlyTag {};        // models "atomic, no thread_local"
using ThreadLocalOnlyFn = csfn::Fn<ThreadLocalOnlyTag>;
using AtomicOnlyFn      = csfn::Fn<AtomicOnlyTag>;
static_assert(csfn::ValidComposition<ThreadLocalOnlyFn>);
static_assert(csfn::ValidComposition<AtomicOnlyFn>);
static_assert(csc::G002_OK<ThreadLocalOnlyFn>);
static_assert(csc::G002_OK<AtomicOnlyFn>);
static_assert(csc::first_failure_v<AtomicOnlyFn> == csc::RuleCode::None);

// A real std::atomic carrier (no thread_local marker) also passes — the
// atomic type by itself is not a hazard.
using RealAtomicFn = csfn::Fn<std::atomic<std::uint64_t>>;
static_assert(csc::G002_OK<RealAtomicFn>);

// ── (e) NEGATIVE compositions covered by HS14 neg-compile fixtures ─
//
// A sentinel TU cannot positively assert first_failure_v<F> == G002 for an
// Fn carrier that genuinely trips it: instantiating F runs Fn<>'s own
// static_assert(ValidComposition<Fn>) before the query. HS14 covers this
// with 2 fixtures (distinct mismatch classes):
//   neg_collision_G002_thread_local_atomic.cpp  (production Fn validate path)
//   neg_collision_G002_concept_direct.cpp       (standalone G002_OK concept)

}  // namespace

int main() { return 0; }
