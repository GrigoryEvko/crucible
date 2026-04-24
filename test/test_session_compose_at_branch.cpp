// Compile-time witnesses for safety/Session.h's compose_at_branch_t
// (#378) — branch-asymmetric protocol composition.
//
// `compose_at_branch_t<P, I, Q>` walks P's spine to the first
// Select<Bs...> or Offer<Bs...> and replaces ONLY branch I's
// continuation with `compose_t<branch_I, Q>` — leaving the other
// branches untouched.  Useful for protocol patterns where one branch
// loops back / enters a sub-session / extends differently from the
// others (the common server-with-error-path shape, the partial-update
// pipeline shape, etc.).
//
// All checks are static_assert; the runtime main() just prints a
// confirmation so the test harness records "PASSED".

#include <crucible/safety/Session.h>

#include <cstdio>

namespace {

using namespace crucible::safety::proto;

// ── Fixtures ─────────────────────────────────────────────────────

struct Request {};
struct Ok {};
struct Err {};
struct Followup {};

// Server-side request/reply with success + error branches.
using ServerOnce = Recv<Request,
                       Select<
                           Send<Ok,  End>,         // branch 0
                           Send<Err, End>>>;        // branch 1

// ── 1. Compose at branch 0 (success path gets a follow-up) ───────

using ServerWithFollowup0 = compose_at_branch_t<
    ServerOnce, /*branch=*/0, Recv<Followup, End>>;

using ExpectedShape0 = Recv<Request,
                            Select<
                                Send<Ok,  Recv<Followup, End>>,    // branch 0 extended
                                Send<Err, End>>>;                    // branch 1 unchanged

static_assert(std::is_same_v<ServerWithFollowup0, ExpectedShape0>,
    "compose_at_branch<_, 0, _>: branch 0 must have its End "
    "replaced; branch 1 must stay End-terminated.");

// ── 2. Compose at branch 1 (error path gets a logging step) ──────

using ServerWithFollowup1 = compose_at_branch_t<
    ServerOnce, /*branch=*/1, Send<Followup, End>>;

using ExpectedShape1 = Recv<Request,
                            Select<
                                Send<Ok,  End>,                       // branch 0 unchanged
                                Send<Err, Send<Followup, End>>>>;     // branch 1 extended

static_assert(std::is_same_v<ServerWithFollowup1, ExpectedShape1>,
    "compose_at_branch<_, 1, _>: branch 1 must have its End "
    "replaced; branch 0 must stay End-terminated.");

// ── 3. Choice is below an Offer — same mechanic ──────────────────

using ClientPickReply = Send<Request,
                            Offer<
                                Recv<Ok,  End>,
                                Recv<Err, End>>>;

using ClientPickReplyExt0 = compose_at_branch_t<
    ClientPickReply, 0, Send<Followup, End>>;

using ExpectedClient0 = Send<Request,
                            Offer<
                                Recv<Ok,  Send<Followup, End>>,
                                Recv<Err, End>>>;

static_assert(std::is_same_v<ClientPickReplyExt0, ExpectedClient0>,
    "compose_at_branch on Offer behaves the same as Select.");

// ── 4. Loop pass-through — choice inside a Loop body ─────────────

using LoopedSelect = Loop<Select<
    Send<Ok,  Continue>,
    Send<Err, End>>>;

using LoopedSelectExt = compose_at_branch_t<
    LoopedSelect, 1, Recv<Followup, End>>;

using ExpectedLooped = Loop<Select<
    Send<Ok,  Continue>,
    Send<Err, Recv<Followup, End>>>>;

static_assert(std::is_same_v<LoopedSelectExt, ExpectedLooped>,
    "compose_at_branch passes through Loop and recurses into the "
    "body; only the named branch's End is composed.");

// ── 5. Multiple Send/Recv pre-head — recurse through the spine ──

using DeepSpine = Send<Request,
                      Recv<Ok,
                          Select<
                              Send<Followup, End>,
                              End>>>;

using DeepSpineExt = compose_at_branch_t<
    DeepSpine, 0, Recv<Followup, End>>;

using ExpectedDeep = Send<Request,
                         Recv<Ok,
                             Select<
                                 Send<Followup, Recv<Followup, End>>,
                                 End>>>;

static_assert(std::is_same_v<DeepSpineExt, ExpectedDeep>,
    "compose_at_branch walks past Send/Recv pre-head to find the "
    "first Select/Offer.");

// ── 6. compose_at_branch composes UNIFORMLY within the chosen branch
//      (i.e., every End within branch I is replaced, not just the top one)

using BranchWithNestedEnd = Select<
    Send<Ok, Select<Send<Followup, End>, End>>,    // branch 0 has TWO ends
    End>;                                             // branch 1

using BranchExt = compose_at_branch_t<
    BranchWithNestedEnd, 0, Recv<Followup, End>>;

using ExpectedBranch = Select<
    Send<Ok, Select<Send<Followup, Recv<Followup, End>>,
                    Recv<Followup, End>>>,
    End>;  // branch 1 untouched

static_assert(std::is_same_v<BranchExt, ExpectedBranch>,
    "Within branch I, EVERY reachable End is replaced (compose_t "
    "semantics on the chosen branch); other branches stay as-is.");

}  // anonymous namespace

int main() {
    std::puts("session_compose_at_branch: branch-asymmetric "
              "composition (compose_at_branch_t) — Select / Offer / "
              "deep-spine / Loop / nested-End all OK");
    return 0;
}
