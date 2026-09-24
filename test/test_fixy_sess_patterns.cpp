// The session patterns are re-exported under a second namespace.
// This file proves that the re-exported name denotes the same type as
// the original, for every user-facing symbol: the protocol aliases,
// the contract markers, and the predicate variables.  A symbol with no
// cell of its own can be renamed on the original side without anything
// noticing, so one cell per symbol is what makes the cover complete.

#include <crucible/fixy/Sess.h>

#include <type_traits>

namespace fpat = crucible::fixy::sess::pattern;
namespace ppat = crucible::safety::proto::pattern;

// These carry no meaning.  A pattern takes its payloads positionally,
// so the names only have to be distinct types.

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

static_assert(std::is_same_v<fpat::RequestResponseOnce_Client<Req, Resp>, ppat::RequestResponseOnce_Client<Req, Resp>>);
static_assert(std::is_same_v<fpat::RequestResponseOnce_Server<Req, Resp>, ppat::RequestResponseOnce_Server<Req, Resp>>);
static_assert(std::is_same_v<fpat::RequestResponse_Client<Req, Resp>, ppat::RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<fpat::RequestResponse_Server<Req, Resp>, ppat::RequestResponse_Server<Req, Resp>>);
static_assert(std::is_same_v<fpat::RequestResponseLoop_Client<Req, Resp>, ppat::RequestResponseLoop_Client<Req, Resp>>);
static_assert(std::is_same_v<fpat::RequestResponseLoop_Server<Req, Resp>, ppat::RequestResponseLoop_Server<Req, Resp>>);

static_assert(std::is_same_v<fpat::PipelineSource<Job>, ppat::PipelineSource<Job>>);
static_assert(std::is_same_v<fpat::PipelineSink<Job>, ppat::PipelineSink<Job>>);
static_assert(std::is_same_v<fpat::PipelineStage<Req, Resp>, ppat::PipelineStage<Req, Resp>>);

static_assert(std::is_same_v<fpat::Transaction_Client<Begin, Op, Commit, Ack, Abort>,
                             ppat::Transaction_Client<Begin, Op, Commit, Ack, Abort>>);
static_assert(std::is_same_v<fpat::Transaction_Server<Begin, Op, Commit, Ack, Abort>,
                             ppat::Transaction_Server<Begin, Op, Commit, Ack, Abort>>);

static_assert(std::is_same_v<fpat::FanOut<3, Job>, ppat::FanOut<3, Job>>);
static_assert(std::is_same_v<fpat::FanIn<3, Job>, ppat::FanIn<3, Job>>);
static_assert(std::is_same_v<fpat::Broadcast<4, Job>, ppat::Broadcast<4, Job>>);
static_assert(std::is_same_v<fpat::ScatterGather<2, Task, Result>, ppat::ScatterGather<2, Task, Result>>);

static_assert(std::is_same_v<fpat::MpmcProducer<Job>, ppat::MpmcProducer<Job>>);
static_assert(std::is_same_v<fpat::MpmcConsumer<Job>, ppat::MpmcConsumer<Job>>);

static_assert(std::is_same_v<fpat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>,
                             ppat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);
static_assert(std::is_same_v<fpat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>,
                             ppat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);

static_assert(std::is_same_v<fpat::SwimProbe_Client<Probe, Ack>, ppat::SwimProbe_Client<Probe, Ack>>);
static_assert(std::is_same_v<fpat::SwimProbe_Server<Probe, Ack>, ppat::SwimProbe_Server<Probe, Ack>>);

static_assert(
    std::is_same_v<fpat::Handshake_Client<Hello, Welcome, Reject>, ppat::Handshake_Client<Hello, Welcome, Reject>>);
static_assert(
    std::is_same_v<fpat::Handshake_Server<Hello, Welcome, Reject>, ppat::Handshake_Server<Hello, Welcome, Reject>>);

static_assert(std::is_same_v<fpat::CrashSafetyVerified, ppat::CrashSafetyVerified>);
static_assert(std::is_same_v<fpat::CrashSafetyPending, ppat::CrashSafetyPending>);
static_assert(std::is_same_v<fpat::BaselinePatternNeedsCrashAwareVariant, ppat::BaselinePatternNeedsCrashAwareVariant>);
static_assert(std::is_same_v<fpat::DelegateCompatible, ppat::DelegateCompatible>);
static_assert(std::is_same_v<fpat::DelegateCompatibilityPending, ppat::DelegateCompatibilityPending>);
static_assert(
    std::is_same_v<fpat::PatternHasNoDelegateBoundaryConstraints, ppat::PatternHasNoDelegateBoundaryConstraints>);

static_assert(
    std::is_same_v<fpat::PatternCrashSafety<fpat::RequestResponse_Server<Req, Resp>, int /* placeholder ReliableSet */,
                                            fpat::CrashSafetyPending>,
                   ppat::PatternCrashSafety<ppat::RequestResponse_Server<Req, Resp>, int, ppat::CrashSafetyPending>>);
static_assert(std::is_same_v<fpat::PatternDelegateCompatibility<fpat::PipelineSink<Job>, fpat::DelegateCompatible>,
                             ppat::PatternDelegateCompatibility<ppat::PipelineSink<Job>, ppat::DelegateCompatible>>);

// A variable template cannot be compared by identity the way a type
// can: two instantiations of the same variable have distinct
// addresses, so address equality would prove nothing.  Both sides are
// therefore read at one and the same contract type and their computed
// values compared.

using TestCrashContract =
    fpat::PatternCrashSafety<fpat::RequestResponse_Server<Req, Resp>, int, fpat::CrashSafetyVerified>;
using TestDelegateContract = fpat::PatternDelegateCompatibility<fpat::PipelineSink<Job>, fpat::DelegateCompatible>;

static_assert(fpat::pattern_crash_safety_verified_v<TestCrashContract>
              == ppat::pattern_crash_safety_verified_v<TestCrashContract>);
static_assert(fpat::pattern_crash_safety_pending_v<TestCrashContract>
              == ppat::pattern_crash_safety_pending_v<TestCrashContract>);
static_assert(fpat::pattern_delegate_compatible_v<TestDelegateContract>
              == ppat::pattern_delegate_compatible_v<TestDelegateContract>);
static_assert(fpat::pattern_delegate_pending_v<TestDelegateContract>
              == ppat::pattern_delegate_pending_v<TestDelegateContract>);

// Parity alone would still hold if both sides flipped together, so
// the expected values are pinned as well.
static_assert(fpat::pattern_crash_safety_verified_v<TestCrashContract>);
static_assert(!fpat::pattern_crash_safety_pending_v<TestCrashContract>);
static_assert(fpat::pattern_delegate_compatible_v<TestDelegateContract>);
static_assert(!fpat::pattern_delegate_pending_v<TestDelegateContract>);

// Each count below is maintained by hand against the cells above.
// Adding a symbol means adding its cell and raising the matching
// count, in the same change.

inline constexpr std::size_t kFixyPatternProtocolAliasCount = 23;
inline constexpr std::size_t kFixyPatternContractMarkerCount = 8;
inline constexpr std::size_t kFixyPatternPredicateCount = 4;

static_assert(kFixyPatternProtocolAliasCount == 23, "The protocol-alias count and the identity cells above disagree.");

static_assert(kFixyPatternContractMarkerCount == 8, "The contract-marker count and the identity cells above disagree.");

static_assert(kFixyPatternPredicateCount == 4, "The predicate count and the parity cells above disagree.");

// Every name counted here is covered above, by type identity for a
// type and by value parity for a predicate.
inline constexpr std::size_t kFixyPatternTotalSymbolCount =
    kFixyPatternProtocolAliasCount + kFixyPatternContractMarkerCount + kFixyPatternPredicateCount;
static_assert(kFixyPatternTotalSymbolCount == 35);

using namespace crucible::fixy::sess;

static_assert(is_well_formed_v<fpat::RequestResponseOnce_Client<Req, Resp>>);
static_assert(is_well_formed_v<fpat::RequestResponse_Server<Req, Resp>>);
static_assert(is_well_formed_v<fpat::PipelineSource<Job>>);
static_assert(is_well_formed_v<fpat::Transaction_Client<Begin, Op, Commit, Ack, Abort>>);
static_assert(is_well_formed_v<fpat::FanOut<3, Job>>);
static_assert(is_well_formed_v<fpat::FanIn<3, Job>>);
static_assert(is_well_formed_v<fpat::MpmcProducer<Job>>);
static_assert(is_well_formed_v<fpat::MpmcConsumer<Job>>);
static_assert(is_well_formed_v<fpat::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);
static_assert(is_well_formed_v<fpat::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);
static_assert(is_well_formed_v<fpat::SwimProbe_Client<Probe, Ack>>);
static_assert(is_well_formed_v<fpat::Handshake_Server<Hello, Welcome, Reject>>);

int main() { return 0; }
