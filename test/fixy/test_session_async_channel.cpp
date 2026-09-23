// Tests of fixy/session/AsyncChannel.h: a forked channel whose two sides
// are related by asynchronous subtyping, at the capacity that the two
// Resources state.
//
// The compile-time half pins the gate: a pair that sends one message
// ahead needs a capacity of one, a pair that sends two ahead needs two,
// and a Resource that states no capacity, or a different one from its
// peer, is refused.  The runtime half runs the first pair over a channel
// that holds one message in each direction.  Each side sends before it
// receives, so a channel with no buffer would deadlock.  Every wait has a
// deadline, so a bug aborts with a diagnostic and does not hang the run.

#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>

namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
namespace eff = ::foundation::effects;

namespace async_tags {
struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};
}  // namespace async_tags

template <>
struct foundation::permissions::splits_into_pack<async_tags::Whole, async_tags::Left, async_tags::Right>
    : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<async_tags::Whole, async_tags::Left,
                                                                    async_tags::Right> : std::true_type {};

namespace {

using BgCtx = eff::detail::ctx_witnesses::BgWitness;

struct Ping {
    int value = 0;
};
struct Pong {
    int value = 0;
};

// ── The channel: one slot in each direction ─────────────────────────

constexpr auto kDeadline = std::chrono::milliseconds{2000};

struct Mailbox {
    std::atomic<int> value{0};
    std::atomic<bool> full{false};
};

struct Pipe : ::foundation::Pinned<Pipe> {
    Mailbox to_left;
    Mailbox to_right;
};

[[noreturn]] void deadline_passed(const char* where) noexcept {
    std::fprintf(stderr, "test_session_async_channel: %s waited past its deadline\n", where);
    std::abort();
}

void put(Mailbox& box, int value, const char* where) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (box.full.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) deadline_passed(where);
        std::this_thread::yield();
    }
    box.value.store(value, std::memory_order_relaxed);
    box.full.store(true, std::memory_order_release);
}

[[nodiscard]] int take(Mailbox& box, const char* where) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (!box.full.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) deadline_passed(where);
        std::this_thread::yield();
    }
    const int value = box.value.load(std::memory_order_relaxed);
    box.full.store(false, std::memory_order_release);
    return value;
}

// The two ends state the capacity of the pipe: one message each way.
struct LeftEnd {
    static constexpr std::size_t channel_capacity = 1;
    Pipe* pipe = nullptr;
};
struct RightEnd {
    static constexpr std::size_t channel_capacity = 1;
    Pipe* pipe = nullptr;
};

// Ends of a channel that holds two messages each way, and an end that
// states nothing.  Only the gate reads them.
struct WideEnd {
    static constexpr std::size_t channel_capacity = 2;
};
struct SilentEnd {};

// ── The protocols ────────────────────────────────────────────────────

// Each side sends before it receives: one message ahead.
using LeftProto = s::Send<Ping, s::Recv<Pong, s::End>>;
using RightProto = s::Send<Pong, s::Recv<Ping, s::End>>;

// The left side sends two messages before it receives.
using EagerLeft = s::Send<Ping, s::Send<Ping, s::Recv<Pong, s::End>>>;
using PatientRight = s::Send<Pong, s::Recv<Ping, s::Recv<Ping, s::End>>>;

static_assert(!s::is_subtype_sync_v<LeftProto, s::dual_of_t<RightProto>>,
              "the pair is not exact duals, so only the asynchronous relation can admit it");
static_assert(s::is_subtype_async_v<LeftProto, s::dual_of_t<RightProto>, 1>);
static_assert(!s::is_subtype_async_v<EagerLeft, s::dual_of_t<PatientRight>, 1>);
static_assert(s::is_subtype_async_v<EagerLeft, s::dual_of_t<PatientRight>, 2>);

// ── The gate ─────────────────────────────────────────────────────────

template <typename Self, typename Peer, typename ResourceSelf, typename ResourcePeer>
inline constexpr bool gate_admits_v =
    s::CtxFitsAsyncForkedChannel<BgCtx, Self, Peer, async_tags::Whole, async_tags::Left, async_tags::Right,
                                 ResourceSelf, ResourcePeer>;

static_assert(gate_admits_v<LeftProto, RightProto, LeftEnd, RightEnd>);
static_assert(!gate_admits_v<EagerLeft, PatientRight, LeftEnd, RightEnd>,
              "two messages ahead do not fit a channel of capacity one");
static_assert(gate_admits_v<EagerLeft, PatientRight, WideEnd, WideEnd>);
static_assert(!gate_admits_v<LeftProto, RightProto, SilentEnd, SilentEnd>, "a Resource must state its capacity");
static_assert(!gate_admits_v<LeftProto, RightProto, LeftEnd, WideEnd>, "the two ends must state one capacity");
static_assert(!gate_admits_v<LeftProto, s::Send<Ping, s::End>, LeftEnd, RightEnd>,
              "a pair that the relation refuses is refused at every capacity");
static_assert(s::channel_capacity_v<LeftEnd const&> == 1, "the capacity is read through a reference");

// ── The runtime pair ─────────────────────────────────────────────────

std::atomic<int> g_left_received{0};
std::atomic<int> g_right_received{0};

struct LeftBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<async_tags::Left>, BgCtx const&) noexcept {
        auto waiting = std::move(head).send(Ping{11}, [](LeftEnd& e, Ping&& p) noexcept {
            put(e.pipe->to_right, p.value, "the left send");
        });
        auto [pong, done] = std::move(waiting).recv([](LeftEnd& e) noexcept {
            return Pong{take(e.pipe->to_left, "the left receive")};
        });
        g_left_received.store(pong.value, std::memory_order_relaxed);
        return std::move(done);
    }
};

struct RightBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<async_tags::Right>, BgCtx const&) noexcept {
        auto waiting = std::move(head).send(Pong{22}, [](RightEnd& e, Pong&& p) noexcept {
            put(e.pipe->to_left, p.value, "the right send");
        });
        auto [ping, done] = std::move(waiting).recv([](RightEnd& e) noexcept {
            return Ping{take(e.pipe->to_right, "the right receive")};
        });
        g_right_received.store(ping.value, std::memory_order_relaxed);
        return std::move(done);
    }
};

[[nodiscard]] int run_pair_on_one_slot_channel() {
    Pipe pipe{};
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<LeftProto, RightProto, async_tags::Left, async_tags::Right>(
        ctx, perm::mint_permission_root<async_tags::Whole>(), LeftEnd{&pipe}, RightEnd{&pipe}, LeftBody{},
        RightBody{});
    perm::permission_drop(std::move(back));
    if (g_left_received.load(std::memory_order_relaxed) != 22 || g_right_received.load(std::memory_order_relaxed) != 11) {
        std::fprintf(stderr, "test_session_async_channel: the pair did not exchange its two messages\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = run_pair_on_one_slot_channel(); rc != 0) return rc;
    std::puts("test_session_async_channel: all checks passed");
    return 0;
}
