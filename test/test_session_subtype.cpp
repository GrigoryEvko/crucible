#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionPayloadSubsort.h>

#include <cstdio>
#include <cstdlib>

namespace {

using namespace crucible::safety::proto;

struct Query {};
struct Result {};
struct CloseOp {};
struct PingOp {};

using ServerV1 = Loop<Offer<Recv<Query, Send<Result, Continue>>, Recv<CloseOp, End>>>;

using ServerV2 =
    Loop<Offer<Recv<Query, Send<Result, Continue>>, Recv<CloseOp, End>, Recv<PingOp, Send<Result, Continue>>>>;

static_assert(is_subtype_sync_v<ServerV2, ServerV1>,
              "ServerV2 should be a subtype of ServerV1 (handles strictly more).");

// The reverse fails because ServerV1 does not handle PingOp.
static_assert(!is_subtype_sync_v<ServerV1, ServerV2>, "ServerV1 must NOT be a subtype of ServerV2.");

using ClientV1 = dual_of_t<ServerV1>;
using ClientV2 = dual_of_t<ServerV2>;

// Duality flips the direction.  Select narrowing makes the two-branch ClientV1
// a subtype of the three-branch ClientV2, because a client that picks from
// fewer options is a safe substitution.
static_assert(is_subtype_sync_v<ClientV1, ClientV2>,
              "ClientV1 should be a subtype of ClientV2 (picks from fewer options).");

// The types below carry value-type subtyping into payload positions.
struct IntLike {};
struct LongerIntLike {};

struct TensorTile {};
using BitexactTile = crucible::safety::NumericalTier<crucible::algebra::lattices::Tolerance::BITEXACT, TensorTile>;
using RelaxedTile = crucible::safety::NumericalTier<crucible::algebra::lattices::Tolerance::RELAXED, TensorTile>;

}  // anonymous namespace

// A user registers the subsort relation at the point of use.  This test reopens
// the namespace to exercise that mechanism.
namespace crucible::safety::proto {
template <>
struct is_subsort<::IntLike, ::LongerIntLike> : std::true_type {};
}  // namespace crucible::safety::proto

namespace {

// With IntLike ⩽ LongerIntLike at the value-type level, Send is covariant in
// its payload and Recv is contravariant.
static_assert(is_subtype_sync_v<Send<IntLike, End>, Send<LongerIntLike, End>>);
static_assert(!is_subtype_sync_v<Send<LongerIntLike, End>, Send<IntLike, End>>);
static_assert(is_subtype_sync_v<Recv<LongerIntLike, End>, Recv<IntLike, End>>);
static_assert(!is_subtype_sync_v<Recv<IntLike, End>, Recv<LongerIntLike, End>>);

static_assert(is_subtype_sync_v<Send<BitexactTile, End>, Send<RelaxedTile, End>>);
static_assert(!is_subtype_sync_v<Send<RelaxedTile, End>, Send<BitexactTile, End>>);
static_assert(is_subtype_sync_v<Recv<RelaxedTile, End>, Recv<BitexactTile, End>>);
static_assert(!is_subtype_sync_v<Recv<BitexactTile, End>, Recv<RelaxedTile, End>>);

}  // anonymous namespace

int main() {
    // Every relation is verified at compile time.  The output line shows that
    // the harness ran.
    std::puts("session_subtype: all compile-time subtype relations verified");
    return 0;
}
