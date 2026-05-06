#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Vendor.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionGrade.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <cstdio>
#include <type_traits>

namespace {

namespace alg = crucible::algebra::lattices;
namespace proto = crucible::safety::proto;
namespace saf = crucible::safety;

struct ResultTensor {};
struct Ack {};
struct WorkPerm {};
struct RoleA {};
struct InnerPermSet {};

using MultiAxisPayload =
    saf::NumericalTier<
        alg::Tolerance::BITEXACT,
        saf::Vendor<
            alg::VendorBackend::NV,
            saf::CipherTier<
                alg::CipherTierTag::Hot,
                saf::Crash<alg::CrashClass::NoThrow, ResultTensor>>>>;

using MultiAxisProto = proto::Send<MultiAxisPayload, proto::End>;
using MultiAxisExpected = alg::ProductLattice<
    alg::VendorLattice::At<alg::VendorBackend::NV>,
    alg::ToleranceLattice::At<alg::Tolerance::BITEXACT>,
    alg::CipherTierLattice::At<alg::CipherTierTag::Hot>,
    alg::CrashLattice::At<alg::CrashClass::NoThrow>>;

static_assert(std::is_same_v<
    proto::protocol_grade_t<MultiAxisProto>,
    MultiAxisExpected>);
static_assert(std::is_same_v<
    proto::protocol_vendor_t<MultiAxisProto>,
    alg::VendorLattice::At<alg::VendorBackend::NV>>);
static_assert(std::is_same_v<
    proto::protocol_numerical_tier_t<MultiAxisProto>,
    alg::ToleranceLattice::At<alg::Tolerance::BITEXACT>>);
static_assert(std::is_same_v<
    proto::protocol_cipher_tier_t<MultiAxisProto>,
    alg::CipherTierLattice::At<alg::CipherTierTag::Hot>>);
static_assert(std::is_same_v<
    proto::protocol_crash_class_t<MultiAxisProto>,
    alg::CrashLattice::At<alg::CrashClass::NoThrow>>);

static_assert(std::is_same_v<
    proto::grade_for_axis_t<proto::axis::Vendor, MultiAxisPayload>,
    alg::VendorLattice::At<alg::VendorBackend::NV>>);
static_assert(
    proto::grade_for_axis_v<proto::axis::NumericalTier,
                            MultiAxisPayload>
    == alg::Tolerance::BITEXACT);

using MarkerWrappedProto = proto::Recv<
    proto::Transferable<
        proto::ContentAddressed<MultiAxisPayload>,
        WorkPerm>,
    proto::End>;
static_assert(std::is_same_v<
    proto::protocol_grade_t<MarkerWrappedProto>,
    MultiAxisExpected>);

using BranchJoinProto = proto::Offer<
    proto::Sender<RoleA>,
    proto::Recv<
        saf::NumericalTier<alg::Tolerance::ULP_FP16, int>,
        proto::End>,
    proto::Recv<
        saf::NumericalTier<alg::Tolerance::BITEXACT, int>,
        proto::End>>;
static_assert(proto::protocol_grade_numerical_tier_v<BranchJoinProto>
              == alg::Tolerance::BITEXACT);

using VendorPinnedProto = proto::VendorPinned<
    alg::VendorBackend::AMD,
    proto::Send<int, proto::End>>;
static_assert(proto::protocol_grade_vendor_v<VendorPinnedProto>
              == alg::VendorBackend::AMD);

using CrashStopProto = proto::Loop<proto::Select<
    proto::Stop_g<alg::CrashClass::ErrorReturn>,
    proto::Continue>>;
static_assert(proto::protocol_grade_crash_class_v<CrashStopProto>
              == alg::CrashClass::ErrorReturn);

using DelegatedInner = proto::Send<MultiAxisPayload, proto::End>;
using DelegatedPayload = proto::DelegatedSession<
    DelegatedInner,
    InnerPermSet>;
using DelegatedCarrier = proto::Send<DelegatedPayload, proto::End>;
static_assert(std::is_same_v<
    proto::protocol_grade_t<DelegatedCarrier>,
    MultiAxisExpected>);

using DelegateHead = proto::Delegate<
    DelegatedInner,
    proto::Recv<Ack, proto::End>>;
static_assert(std::is_same_v<
    proto::protocol_grade_t<DelegateHead>,
    MultiAxisExpected>);

using Checkpointed = proto::CheckpointedSession<
    proto::Send<
        saf::CipherTier<alg::CipherTierTag::Warm, int>,
        proto::End>,
    proto::Send<
        saf::CipherTier<alg::CipherTierTag::Cold, int>,
        proto::End>>;
static_assert(proto::protocol_grade_cipher_tier_v<Checkpointed>
              == alg::CipherTierTag::Warm);

}  // namespace

int main() {
    std::puts("session_grade: compile-time protocol grade projection OK");
    return 0;
}
