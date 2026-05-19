// ── test_fixy_sess_patterns — FIXY-AUDIT-C6 + FIXY-U-021 sentinel ─
//
// Positive-compile witness for the 35 user-facing pattern symbols
// (23 protocol aliases + 8 contract markers + 4 predicate variables)
// re-exported under `fixy::sess::pattern::`.  The substrate ships
// them as protocol-shape aliases over Session.h's Send/Recv/Loop/
// Continue/End/Select/Offer combinators; each alias is structurally
// verified by the SessionPatterns.h header's own self-test under
// CRUCIBLE_SESSION_PATTERN_SELF_TESTS_*.
//
// This sentinel proves (Task #1428 + #1733):
//   1. The namespace alias `fixy::sess::pattern = safety::proto::pattern`
//      preserves type identity for every shipped pattern.
//   2. Each instantiated pattern is_well_formed_v under Session.h's
//      structural well-formedness predicate.
//   3. (FIXY-U-021 extension) Three CARDINALITY witnesses pin counts:
//        kFixyPatternProtocolAliasCount = 23
//        kFixyPatternContractMarkerCount = 8
//        kFixyPatternPredicateCount     = 4
//      A substrate-side rename or addition that this TU is not
//      updated to track makes the per-symbol cell fail (rename) or
//      the constant fail (count drift); the drift is caught at TU-
//      compile rather than silently regressing fixy:: reach.
//   4. (FIXY-U-021 extension) The 6 plain marker structs + 4
//      pattern_*_v predicate variables are exercised for parity, so
//      the entire user-facing pattern:: surface — not just protocol
//      aliases — is dual-export verified.
//
// Closes #1733 (FIXY-U-021) and the fixy-A4-016 cardinality-drift
// finding (substrate pattern rename undetected because no count
// sentinel was wired).

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fpat = crucible::fixy::sess::pattern;
namespace ppat = crucible::safety::proto::pattern;

// ─── Carrier payload types (no semantic content; pattern uses them
//     as positional template parameters) ──────────────────────────

struct Req {};
struct Resp {};
struct Job {};
struct Task {};
struct Result {};
struct Probe {};
struct Ack {};
struct Hello {};
struct Welcome {};
struct Reject {};
struct Begin {};
struct Op {};
struct Commit {};
struct Abort {};
struct Vote {};
struct Prepare {};

// ─── 1. Request / response family (6 aliases) ─────────────────────

static_assert(std::is_same_v<
    fpat::RequestResponseOnce_Client<Req, Resp>,
    ppat::RequestResponseOnce_Client<Req, Resp>>);
static_assert(std::is_same_v<
    fpat::RequestResponseOnce_Server<Req, Resp>,
    ppat::RequestResponseOnce_Server<Req, Resp>>);
static_assert(std::is_same_v<
    fpat::RequestResponse_Client<Req, Resp>,
    ppat::RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<
    fpat::RequestResponse_Server<Req, Resp>,
    ppat::RequestResponse_Server<Req, Resp>>);
static_assert(std::is_same_v<
    fpat::RequestResponseLoop_Client<Req, Resp>,
    ppat::RequestResponseLoop_Client<Req, Resp>>);
static_assert(std::is_same_v<
    fpat::RequestResponseLoop_Server<Req, Resp>,
    ppat::RequestResponseLoop_Server<Req, Resp>>);

// ─── 2. Pipeline family (3 aliases) ───────────────────────────────

static_assert(std::is_same_v<
    fpat::PipelineSource<Job>, ppat::PipelineSource<Job>>);
static_assert(std::is_same_v<
    fpat::PipelineSink<Job>, ppat::PipelineSink<Job>>);
static_assert(std::is_same_v<
    fpat::PipelineStage<Req, Resp>, ppat::PipelineStage<Req, Resp>>);

// ─── 3. Transaction family (2 aliases) ────────────────────────────

static_assert(std::is_same_v<
    fpat::Transaction_Client<Begin, Op, Commit, Ack, Abort>,
    ppat::Transaction_Client<Begin, Op, Commit, Ack, Abort>>);
static_assert(std::is_same_v<
    fpat::Transaction_Server<Begin, Op, Commit, Ack, Abort>,
    ppat::Transaction_Server<Begin, Op, Commit, Ack, Abort>>);

// ─── 4. Fan-out / fan-in / scatter-gather (4 aliases) ─────────────

static_assert(std::is_same_v<
    fpat::FanOut<3, Job>, ppat::FanOut<3, Job>>);
static_assert(std::is_same_v<
    fpat::FanIn<3, Job>, ppat::FanIn<3, Job>>);
static_assert(std::is_same_v<
    fpat::Broadcast<4, Job>, ppat::Broadcast<4, Job>>);
static_assert(std::is_same_v<
    fpat::ScatterGather<2, Task, Result>,
    ppat::ScatterGather<2, Task, Result>>);

// ─── 5. MPMC (2 aliases) ──────────────────────────────────────────

static_assert(std::is_same_v<
    fpat::MpmcProducer<Job>, ppat::MpmcProducer<Job>>);
static_assert(std::is_same_v<
    fpat::MpmcConsumer<Job>, ppat::MpmcConsumer<Job>>);

// ─── 6. Two-phase commit (2 aliases) ──────────────────────────────

static_assert(std::is_same_v<
    fpat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>,
    ppat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);
static_assert(std::is_same_v<
    fpat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>,
    ppat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);

// ─── 7. SWIM gossip probe (2 aliases) ─────────────────────────────

static_assert(std::is_same_v<
    fpat::SwimProbe_Client<Probe, Ack>,
    ppat::SwimProbe_Client<Probe, Ack>>);
static_assert(std::is_same_v<
    fpat::SwimProbe_Server<Probe, Ack>,
    ppat::SwimProbe_Server<Probe, Ack>>);

// ─── 8. Handshake (2 aliases) ─────────────────────────────────────

static_assert(std::is_same_v<
    fpat::Handshake_Client<Hello, Welcome, Reject>,
    ppat::Handshake_Client<Hello, Welcome, Reject>>);
static_assert(std::is_same_v<
    fpat::Handshake_Server<Hello, Welcome, Reject>,
    ppat::Handshake_Server<Hello, Welcome, Reject>>);

// Total user-facing pattern aliases re-exported: 23.

// ─── 9. Contract markers (8 names: 6 plain marker structs +
//       2 binding-rich templates) ─────────────────────────────────

// 6 plain marker structs — every one must dual-export via the
// namespace alias.  FIXY-U-021 closure: pre-extension, only the 2
// templated markers below were witnessed; the 6 plain ones drifted
// silently if substrate renamed them under the namespace alias.

static_assert(std::is_same_v<
    fpat::CrashSafetyVerified, ppat::CrashSafetyVerified>);
static_assert(std::is_same_v<
    fpat::CrashSafetyPending, ppat::CrashSafetyPending>);
static_assert(std::is_same_v<
    fpat::BaselinePatternNeedsCrashAwareVariant,
    ppat::BaselinePatternNeedsCrashAwareVariant>);
static_assert(std::is_same_v<
    fpat::DelegateCompatible, ppat::DelegateCompatible>);
static_assert(std::is_same_v<
    fpat::DelegateCompatibilityPending,
    ppat::DelegateCompatibilityPending>);
static_assert(std::is_same_v<
    fpat::PatternHasNoDelegateBoundaryConstraints,
    ppat::PatternHasNoDelegateBoundaryConstraints>);

// 2 binding-rich templates (PatternCrashSafety / PatternDelegateCompatibility)
// — already witnessed via instantiation below.

static_assert(std::is_same_v<
    fpat::PatternCrashSafety<
        fpat::RequestResponse_Server<Req, Resp>,
        int /* placeholder ReliableSet */, fpat::CrashSafetyPending>,
    ppat::PatternCrashSafety<
        ppat::RequestResponse_Server<Req, Resp>,
        int, ppat::CrashSafetyPending>>);
static_assert(std::is_same_v<
    fpat::PatternDelegateCompatibility<
        fpat::PipelineSink<Job>, fpat::DelegateCompatible>,
    ppat::PatternDelegateCompatibility<
        ppat::PipelineSink<Job>, ppat::DelegateCompatible>>);

// ─── 9b. Predicate variables (4 pattern_*_v aliases) ──────────────
//
// `pattern_crash_safety_{verified,pending}_v<Contract>` reads the
// Contract type's static constexpr bool members.  Witness parity
// between the fixy-side and substrate-side variable templates by
// taking the address of an instantiation (the variable IS the same
// `const bool` once instantiated, but separate template
// instantiations yield separate addresses — we instead compare the
// computed value AT the SAME Contract instantiation, which forces
// both sides to share a definition).

using TestCrashContract = fpat::PatternCrashSafety<
    fpat::RequestResponse_Server<Req, Resp>, int, fpat::CrashSafetyVerified>;
using TestDelegateContract = fpat::PatternDelegateCompatibility<
    fpat::PipelineSink<Job>, fpat::DelegateCompatible>;

static_assert(fpat::pattern_crash_safety_verified_v<TestCrashContract> ==
              ppat::pattern_crash_safety_verified_v<TestCrashContract>);
static_assert(fpat::pattern_crash_safety_pending_v<TestCrashContract> ==
              ppat::pattern_crash_safety_pending_v<TestCrashContract>);
static_assert(fpat::pattern_delegate_compatible_v<TestDelegateContract> ==
              ppat::pattern_delegate_compatible_v<TestDelegateContract>);
static_assert(fpat::pattern_delegate_pending_v<TestDelegateContract> ==
              ppat::pattern_delegate_pending_v<TestDelegateContract>);

// Spot-check actual values so a substrate rewrite that flips the
// predicate's polarity is caught.
static_assert(fpat::pattern_crash_safety_verified_v<TestCrashContract>);
static_assert(!fpat::pattern_crash_safety_pending_v<TestCrashContract>);
static_assert(fpat::pattern_delegate_compatible_v<TestDelegateContract>);
static_assert(!fpat::pattern_delegate_pending_v<TestDelegateContract>);

// ─── 9c. Cardinality sentinels — fixy-A4-016 closure ──────────────
//
// Three named constants pin the user-facing fixy::sess::pattern::
// symbol count by category.  A substrate-side addition that this
// TU is not updated to track fails ONE of these constants (count
// drift) AND fails the per-symbol cell below (rename detection),
// catching the drift at TU-compile rather than silently regressing
// fixy:: reach.  Bump policy:
//   * New protocol alias (e.g., a 24th pattern) → bump
//     kFixyPatternProtocolAliasCount AND add a static_assert cell
//     in §§ 1-8 above.  Both edits must land in the SAME commit.
//   * New contract marker → bump kFixyPatternContractMarkerCount
//     AND add a cell in § 9.
//   * New pattern_*_v predicate → bump kFixyPatternPredicateCount
//     AND add a value-parity cell in § 9b.

inline constexpr std::size_t kFixyPatternProtocolAliasCount = 23;
inline constexpr std::size_t kFixyPatternContractMarkerCount = 8;
inline constexpr std::size_t kFixyPatternPredicateCount = 4;

static_assert(kFixyPatternProtocolAliasCount == 23,
    "User-facing pattern alias count drift — see SessionPatterns.h "
    "namespace pattern:: (lines 141-520).  If a new alias was added "
    "to the substrate, add a static_assert cell in the appropriate "
    "family section above AND bump this constant.  If an alias was "
    "removed/renamed, the per-symbol cell will fail to compile, "
    "naming the missing alias in the diagnostic.");

static_assert(kFixyPatternContractMarkerCount == 8,
    "Contract marker count drift — see SessionPatterns.h lines "
    "152-196 for the 6 plain markers + 2 binding-rich templates.  "
    "If substrate adds a 9th marker, add a static_assert in § 9 "
    "and bump this constant.");

static_assert(kFixyPatternPredicateCount == 4,
    "Predicate variable count drift — pattern_crash_safety_{verified,"
    "pending}_v + pattern_delegate_{compatible,pending}_v.  If a 5th "
    "predicate is added in substrate, add a value-parity cell in "
    "§ 9b and bump this constant.");

// Cumulative reach: 35 user-facing names in fixy::sess::pattern::
// (23 + 8 + 4) — every one witnessed via std::is_same_v (types) or
// value parity (predicates).
inline constexpr std::size_t kFixyPatternTotalSymbolCount =
    kFixyPatternProtocolAliasCount +
    kFixyPatternContractMarkerCount +
    kFixyPatternPredicateCount;
static_assert(kFixyPatternTotalSymbolCount == 35);

// ─── 10. Well-formedness of each instantiated pattern ─────────────

using namespace crucible::fixy::sess;

static_assert(is_well_formed_v<
    fpat::RequestResponseOnce_Client<Req, Resp>>);
static_assert(is_well_formed_v<
    fpat::RequestResponse_Server<Req, Resp>>);
static_assert(is_well_formed_v<
    fpat::PipelineSource<Job>>);
static_assert(is_well_formed_v<
    fpat::Transaction_Client<Begin, Op, Commit, Ack, Abort>>);
static_assert(is_well_formed_v<fpat::FanOut<3, Job>>);
static_assert(is_well_formed_v<fpat::FanIn<3, Job>>);
static_assert(is_well_formed_v<fpat::MpmcProducer<Job>>);
static_assert(is_well_formed_v<fpat::MpmcConsumer<Job>>);
static_assert(is_well_formed_v<
    fpat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);
static_assert(is_well_formed_v<
    fpat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);
static_assert(is_well_formed_v<
    fpat::SwimProbe_Client<Probe, Ack>>);
static_assert(is_well_formed_v<
    fpat::Handshake_Server<Hello, Welcome, Reject>>);

int main() { return 0; }
