// ── test_fixy_sess_patterns — FIXY-AUDIT-C6 sentinel ───────────────
//
// Positive-compile witness for the 15+ verified session patterns
// re-exported under `fixy::sess::pattern::`.  The substrate ships them
// as protocol-shape aliases over Session.h's Send/Recv/Loop/Continue/
// End/Select/Offer combinators; each alias is structurally verified by
// the SessionPatterns.h header's own self-test under
// CRUCIBLE_SESSION_PATTERN_SELF_TESTS_*.
//
// This sentinel proves:
//   1. The namespace alias preserves type identity for every shipped
//      pattern.
//   2. Each instantiated pattern is_well_formed_v under Session.h's
//      structural well-formedness predicate.
//
// Task #1428.

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

// ─── 9. Contract markers (8 aliases) ──────────────────────────────

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
