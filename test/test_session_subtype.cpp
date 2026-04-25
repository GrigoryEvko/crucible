// Tests for Gay-Hole synchronous session-type subtyping.
//
// The bulk of coverage is in-header self-test static_asserts (runs at
// include time).  This file holds a small runtime harness so the test
// integrates with ctest and produces a green tick on CI.  All heavy
// lifting is compile-time.

#include <crucible/sessions/SessionSubtype.h>

#include <cstdio>
#include <cstdlib>

namespace {

using namespace crucible::safety::proto;

// Sample payload types for the test protocols
struct Query   {};
struct Result  {};
struct CloseOp {};
struct PingOp  {};

// Sample old protocol
using ServerV1 = Loop<Offer<
    Recv<Query,   Send<Result, Continue>>,
    Recv<CloseOp, End>
>>;

// Sample new protocol — handles one additional kind of request (PingOp).
// v2 is a subtype of v1 per Offer widening: v2 handles strictly more.
using ServerV2 = Loop<Offer<
    Recv<Query,   Send<Result, Continue>>,
    Recv<CloseOp, End>,
    Recv<PingOp,  Send<Result, Continue>>
>>;

// Compile-time check: v2 is a valid substitution for v1.
static_assert(is_subtype_sync_v<ServerV2, ServerV1>,
    "ServerV2 should be a subtype of ServerV1 (handles strictly more).");

// Reverse is false — v1 doesn't handle PingOp.
static_assert(!is_subtype_sync_v<ServerV1, ServerV2>,
    "ServerV1 must NOT be a subtype of ServerV2.");

// Client-side dual-subtype: Client of v2 is a subtype of Client of v1
// ONLY if the dual relation also respects subtyping.  Check:
using ClientV1 = dual_of_t<ServerV1>;
using ClientV2 = dual_of_t<ServerV2>;

// Expected:
//   ClientV1 = Loop<Select<
//       Send<Query,   Recv<Result, Continue>>,
//       Send<CloseOp, End>>>
//   ClientV2 = Loop<Select<
//       Send<Query,   Recv<Result, Continue>>,
//       Send<CloseOp, End>,
//       Send<PingOp,  Recv<Result, Continue>>>>
//
// Under Select narrowing: ClientV1 (2 branches) ⩽ ClientV2 (3 branches).
// The "newer" client v2 has MORE options available; the v1 client,
// picking from only 2, is a safe substitution.
static_assert(is_subtype_sync_v<ClientV1, ClientV2>,
    "ClientV1 should be a subtype of ClientV2 (picks from fewer options).");

// Subsort propagation — demonstrate value-type subtyping lifting to
// payload positions.
struct IntLike {};
struct LongerIntLike {};

}  // anonymous namespace

// Register the subsort relation in the proto namespace (users do this
// at point of use; this test exercises the mechanism).
namespace crucible::safety::proto {
template <>
struct is_subsort<::IntLike, ::LongerIntLike> : std::true_type {};
}  // namespace crucible::safety::proto

namespace {

// With IntLike ⩽ LongerIntLike at value-type level:
//   Send covariance:   Send<IntLike, End> ⩽ Send<LongerIntLike, End>
//   Recv contravariance: Recv<LongerIntLike, End> ⩽ Recv<IntLike, End>
static_assert(is_subtype_sync_v<Send<IntLike, End>,
                                 Send<LongerIntLike, End>>);
static_assert(!is_subtype_sync_v<Send<LongerIntLike, End>,
                                  Send<IntLike, End>>);
static_assert(is_subtype_sync_v<Recv<LongerIntLike, End>,
                                 Recv<IntLike, End>>);
static_assert(!is_subtype_sync_v<Recv<IntLike, End>,
                                  Recv<LongerIntLike, End>>);

}  // anonymous namespace

int main() {
    // Runtime: everything is already verified at compile time.  Emit
    // one line to assert the harness ran.
    std::puts("session_subtype: all compile-time subtype relations verified");
    return 0;
}
