// The context-bound entry points of fixy/session/Entry.h: mint_session
// gives the first handle to the caller, and with_session lends the
// Resource to a body by move and gives it back at End.

#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

using BgCtx = eff::detail::ctx_witnesses::BgWitness;

// A move-only Resource that counts the values it carries.  The member
// makes it move-only, so it is an owned SessionResource.
struct Counter {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
    int sent = 0;
};

struct Region {};

using Once = s::Send<int, s::End>;
using Stream = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
using SendsRegion = s::Send<s::Transferable<int, Region>, s::End>;

constexpr auto count_one = [](Counter& counter, int&) noexcept {
    ++counter.sent;
    return true;
};

// The gate reads the context, the Resource and the permission flow.
static_assert(s::CtxFitsSession<BgCtx, Once, Counter>);
static_assert(s::CtxFitsSession<BgCtx, Stream, Counter>);
static_assert(!s::CtxFitsSession<int, Once, Counter>, "an int is not an execution context");
static_assert(!s::CtxFitsSession<BgCtx, Once, Counter*>, "a raw pointer is not a Resource");
static_assert(!s::CtxFitsSession<BgCtx, Once, Counter&>, "a reference to an object that can move is refused");
static_assert(!s::CtxFitsSession<BgCtx, SendsRegion, Counter>, "the empty set holds no token to send");

// mint_session and mint_permissioned_session read one concept: this gate
// is the permissioned gate at the empty set.
static_assert(s::CtxFitsSession<BgCtx, Once, Counter> == s::CtxFitsSessionFrom<BgCtx, Once, Counter>);
static_assert(!s::CtxFitsPermissionedSession<BgCtx, Once, Counter>, "the permissioned mint asks for one tag or more");

// The row of a protocol is the union of the rows of its payloads.  The
// background context holds Bg and Alloc, and the compile context holds
// IO too.
using BgIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using AllocWork = eff::Computation<eff::Row<eff::Effect::Alloc>, int>;

using SendsIo = s::Send<IoWork, s::End>;
using ReceivesIo = s::Recv<IoWork, s::End>;
using BranchRows = s::Loop<s::Select<s::Send<AllocWork, s::Continue>, s::Recv<IoWork, s::End>>>;
using DelegatesIo = s::Send<s::DelegatedSession<SendsIo, Counter, s::DefaultAbandonmentPolicy,
                                                ::foundation::permissions::EmptyPermSet>,
                            s::End>;
using SendsAllocRegion = s::Send<s::Transferable<AllocWork, Region>, s::End>;

static_assert(std::is_same_v<s::protocol_payload_row_t<Once>, eff::Row<>>, "an int carries no effect");
static_assert(std::is_same_v<s::protocol_payload_row_t<SendsIo>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<s::protocol_payload_row_t<ReceivesIo>, eff::Row<eff::Effect::IO>>,
              "a received payload counts as a sent one does");
static_assert(std::is_same_v<s::protocol_payload_row_t<BranchRows>, eff::Row<eff::Effect::Alloc, eff::Effect::IO>>,
              "each branch counts, also the branch that ends the loop");
static_assert(std::is_same_v<s::protocol_payload_row_t<DelegatesIo>, eff::Row<eff::Effect::IO>>,
              "the protocol of a delegated endpoint counts");
static_assert(std::is_same_v<s::protocol_payload_row_t<SendsAllocRegion>, eff::Row<eff::Effect::Alloc>>,
              "a marker carries the row of its value");

static_assert(!s::CtxFitsSession<BgCtx, SendsIo, Counter>, "the background context holds no IO");
static_assert(s::CtxFitsSession<BgIoCtx, SendsIo, Counter>);
static_assert(!s::CtxFitsSession<BgCtx, ReceivesIo, Counter>, "the background context holds no IO");
static_assert(s::CtxFitsSession<BgIoCtx, ReceivesIo, Counter>);
static_assert(!s::CtxFitsSession<BgCtx, BranchRows, Counter>, "the branch that ends the loop receives IO");
static_assert(s::CtxFitsSession<BgIoCtx, BranchRows, Counter>);
static_assert(!s::CtxFitsSession<BgCtx, DelegatesIo, Counter>, "the delegated protocol sends IO");
static_assert(s::CtxFitsSession<BgIoCtx, DelegatesIo, Counter>);

[[nodiscard]] int mint_walks_to_end() {
    const BgCtx ctx{eff::testing::bg()};
    auto head = s::mint_session<Once>(ctx, Counter{});
    static_assert(std::is_same_v<typename decltype(head)::perm_set, ::foundation::permissions::EmptyPermSet>);
    auto at_end = std::move(head).send(7, count_one);
    const Counter back = std::move(at_end).close();
    if (back.sent != 1) {
        std::fprintf(stderr, "mint_walks_to_end: sent %d values, want 1\n", back.sent);
        return 1;
    }
    return 0;
}

[[nodiscard]] int loop_head_is_the_choice() {
    const BgCtx ctx{eff::testing::bg()};
    auto head = s::mint_session<Stream>(ctx, Counter{});
    static_assert(std::is_same_v<typename decltype(head)::protocol, s::Select<s::Send<int, s::Continue>, s::End>>,
                  "a Loop at the head is unrolled one iteration");
    auto at_end = std::move(head).select_local<1>();
    const Counter back = std::move(at_end).close();
    if (back.sent != 0) {
        std::fprintf(stderr, "loop_head_is_the_choice: sent %d values, want 0\n", back.sent);
        return 1;
    }
    return 0;
}

[[nodiscard]] int callback_lends_the_resource() {
    const BgCtx ctx{eff::testing::bg()};
    const Counter back = s::with_session<Stream>(ctx, Counter{}, [](auto head) noexcept {
        for (int value = 0; value < 3; ++value) {
            head = std::move(head).template select_local<0>().send(value, count_one);
        }
        return std::move(head).template select_local<1>();
    });
    if (back.sent != 3) {
        std::fprintf(stderr, "callback_lends_the_resource: sent %d values, want 3\n", back.sent);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = mint_walks_to_end(); rc != 0) return rc;
    if (const int rc = loop_head_is_the_choice(); rc != 0) return rc;
    if (const int rc = callback_lends_the_resource(); rc != 0) return rc;
    std::fprintf(stderr, "test_session_entry: OK\n");
    return 0;
}
