// Tests of fixy/session/AsyncChannel.h: a forked channel whose two sides
// are related by asynchronous subtyping, at the capacity that the two
// Resources state.
//
// The compile-time half pins the gate: a pair that sends one message
// ahead needs a capacity of one, a pair that sends two ahead needs two,
// and a Resource that states no capacity, or a different one from its
// peer, is refused.  A pair in which one side drops an exit is refused in
// each order.  The runtime half runs the first pair over a channel that
// holds one message in each direction.  Each side sends before it
// receives, so a channel with no buffer deadlocks.  Every wait has a
// deadline, so a bug aborts with a diagnostic and does not hang the run.

#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
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
struct foundation::permissions::can_split_into_pack<async_tags::Whole, async_tags::Left, async_tags::Right>
    : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<async_tags::Whole, async_tags::Left,
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

// The deadline of one transport.  The handle waits through the watch and
// tries the transport again, and each try that finds nothing reads the
// deadline.
struct Deadline {
    const char* where = "";
    std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now() + kDeadline;

    void check() const noexcept {
        if (std::chrono::steady_clock::now() > at) deadline_passed(where);
    }
};

// Puts `value` into `box` if the box is empty.
[[nodiscard]] bool try_put(Mailbox& box, int value, const Deadline& deadline) noexcept {
    if (box.full.load(std::memory_order_acquire)) {
        deadline.check();
        return false;
    }
    box.value.store(value, std::memory_order_relaxed);
    box.full.store(true, std::memory_order_release);
    return true;
}

// Takes the value in `box` if the box is full.
[[nodiscard]] std::optional<int> try_take(Mailbox& box, const Deadline& deadline) noexcept {
    if (!box.full.load(std::memory_order_acquire)) {
        deadline.check();
        return std::nullopt;
    }
    const int value = box.value.load(std::memory_order_relaxed);
    box.full.store(false, std::memory_order_release);
    return value;
}

// The two ends state the capacity of the pipe: one message each way.
struct LeftEnd {
    static constexpr std::size_t channel_capacity = 1;
    Pipe* pipe = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct RightEnd {
    static constexpr std::size_t channel_capacity = 1;
    Pipe* pipe = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
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
static_assert(s::is_subtype_async_v<LeftProto, s::dual_of_t<RightProto>, LeftEnd>);
static_assert(!s::is_subtype_async_v<EagerLeft, s::dual_of_t<PatientRight>, LeftEnd>);
static_assert(s::is_subtype_async_v<EagerLeft, s::dual_of_t<PatientRight>, WideEnd>);

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

// A side that waits for a stop refines the dual of a peer that never
// sends it.  The peer drops the exit, so it does not refine the dual of
// that side.  The gate asks for the two directions, and it refuses the
// pair in each order.
struct Job {};
struct StopCmd {};
using NeverStops = s::Loop<s::Select<s::Send<Job, s::Continue>>>;
using AwaitsStop = s::Loop<s::Offer<s::Recv<Job, s::Continue>, s::Recv<StopCmd, s::End>>>;

static_assert(s::is_subtype_async_v<AwaitsStop, s::dual_of_t<NeverStops>, LeftEnd>,
              "the check in one direction admits the pair");
static_assert(!s::is_subtype_async_v<NeverStops, s::dual_of_t<AwaitsStop>, RightEnd>,
              "the peer that never stops does not refine the dual of the side that waits for a stop");
static_assert(!gate_admits_v<AwaitsStop, NeverStops, LeftEnd, RightEnd>);
static_assert(!gate_admits_v<NeverStops, AwaitsStop, LeftEnd, RightEnd>);
static_assert(gate_admits_v<RightProto, LeftProto, RightEnd, LeftEnd>, "a compatible pair is admitted in each order");

// ── The runtime pair ─────────────────────────────────────────────────

std::atomic<int> g_left_received{0};
std::atomic<int> g_right_received{0};

// The live records of fixy/session/Watch.h when the left body starts.  The
// right side cannot reach End before the left side sends, so the two
// records of the channel are live then.
std::atomic<std::uint32_t> g_live_in_left_body{0};

struct LeftBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<async_tags::Left>, BgCtx const&) noexcept {
        g_live_in_left_body.store(s::watch::live_count(), std::memory_order_relaxed);
        const auto send_ping = [deadline = Deadline{"the left send"}](LeftEnd& e, Ping& p) noexcept {
            return try_put(e.pipe->to_right, p.value, deadline);
        };
        auto waiting = std::move(head).send(Ping{11}, send_ping);
        auto [pong, done] = std::move(waiting).recv([deadline = Deadline{"the left receive"}](LeftEnd& e) noexcept {
            return try_take(e.pipe->to_left, deadline).transform([](int value) noexcept { return Pong{value}; });
        });
        g_left_received.store(pong.value, std::memory_order_relaxed);
        return std::move(done);
    }
};

struct RightBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<async_tags::Right>, BgCtx const&) noexcept {
        const auto send_pong = [deadline = Deadline{"the right send"}](RightEnd& e, Pong& p) noexcept {
            return try_put(e.pipe->to_left, p.value, deadline);
        };
        auto waiting = std::move(head).send(Pong{22}, send_pong);
        auto [ping, done] = std::move(waiting).recv([deadline = Deadline{"the right receive"}](RightEnd& e) noexcept {
            return try_take(e.pipe->to_right, deadline).transform([](int value) noexcept { return Ping{value}; });
        });
        g_right_received.store(ping.value, std::memory_order_relaxed);
        return std::move(done);
    }
};

[[nodiscard]] int run_pair_on_one_slot_channel() {
    Pipe pipe{};
    const BgCtx ctx{eff::testing::bg()};
    const std::uint32_t live_before = s::watch::live_count();
    auto back = s::mint_forked_async_channel<LeftProto, RightProto, async_tags::Left, async_tags::Right>(
        ctx, perm::mint_permission_root<async_tags::Whole>(), LeftEnd{&pipe}, RightEnd{&pipe}, LeftBody{},
        RightBody{});
    perm::permission_drop(std::move(back));
    if (g_left_received.load(std::memory_order_relaxed) != 22 || g_right_received.load(std::memory_order_relaxed) != 11) {
        std::fprintf(stderr, "test_session_async_channel: the pair did not exchange its two messages\n");
        return 1;
    }
    // The mint claims a record for each side, and End releases it.
    if (g_live_in_left_body.load(std::memory_order_relaxed) != live_before + 2
        || s::watch::live_count() != live_before) {
        std::fprintf(stderr, "test_session_async_channel: the channel did not hold one record for each side\n");
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
