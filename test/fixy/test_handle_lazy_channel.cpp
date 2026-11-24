// A lazily established channel gives one session over its resource.
// These runs cover the refusal before establish, the session after it,
// the refusal of every later request, the race between two observers,
// and the startup that polls from a worker thread.

#include <fixy/channels/LazyEstablishedChannel.h>

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstdio>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace channels = ::fixy::channels;
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The channel is a sink with no peer, so it states the Local network, and
// a choice over it puts no label.
struct VesselChannel : ::foundation::Pinned<VesselChannel> {
    static constexpr s::Network session_network = s::Network::Local;
    int sentinel = 0;
    int call_count = 0;
};

// A loop of sends is the shape of a channel on the dispatch path.
using DispatchProto = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
using Channel = channels::LazyEstablishedChannel<DispatchProto, VesselChannel>;

static_assert(std::is_same_v<typename Channel::protocol, DispatchProto>);
static_assert(std::is_same_v<typename Channel::resource_type, VesselChannel>);
static_assert(sizeof(Channel) == 2 * sizeof(std::atomic<VesselChannel*>));

constexpr auto write_value = [](VesselChannel& channel, int& value) noexcept {
    channel.sentinel = value;
    ++channel.call_count;
    return true;
};

using channels::LazyChannelRefusal;
using Refusal = ::fixy::Option<LazyChannelRefusal>;

// The session that the mint of the channel gives.
using Head = decltype(s::mint_session<DispatchProto, s::DefaultAbandonmentPolicy, BgCtx, VesselChannel&>(
    std::declval<BgCtx const&>(), std::declval<VesselChannel&>()));

// Why the mint gave no session, or no value.  A session that it gave
// detaches.
[[nodiscard]] Refusal refusal_of(::fixy::Result<Head, LazyChannelRefusal>&& minted) noexcept {
    return std::move(minted).match(
        [](Head head) noexcept -> Refusal {
            std::move(head).detach(s::detach_reason::TestInstrumentation{});
            return ::fixy::none;
        },
        [](LazyChannelRefusal refusal) noexcept -> Refusal { return ::fixy::some(refusal); });
}

[[nodiscard]] int refuses_before_establish() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    if (channel.is_established()) return 1;
    if (refusal_of(channel.mint_established_session(ctx)) != ::fixy::some(LazyChannelRefusal::NotEstablished)) return 2;

    // A request before establish claims nothing, so the session is
    // still there after the publish.
    VesselChannel storage{};
    channel.establish(storage);
    if (refusal_of(channel.mint_established_session(ctx)).is_some()) return 3;
    return 0;
}

[[nodiscard]] int drives_the_protocol_to_end() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    VesselChannel storage{};
    storage.sentinel = 42;
    channel.establish(storage);
    if (!channel.is_established()) return 1;

    return channel.mint_established_session(ctx).match(
        [&storage](Head head) noexcept -> int {
            const bool is_storage = &head.resource() == &storage && head.resource().sentinel == 42;
            auto next = std::move(head).select<0>(s::no_label).send(99, write_value);
            const bool is_sent = storage.sentinel == 99 && storage.call_count == 1;
            VesselChannel& back = std::move(next).select<1>(s::no_label).close();
            if (!is_storage) return 3;
            if (!is_sent) return 4;
            if (&back != &storage) return 5;
            return 0;
        },
        [](LazyChannelRefusal) noexcept -> int { return 2; });
}

[[nodiscard]] int refuses_a_second_session() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    VesselChannel storage{};
    channel.establish(storage);

    return channel.mint_established_session(ctx).match(
        [&channel, &ctx](Head first) noexcept -> int {
            int refused = 0;
            for (int attempt = 0; attempt < 3; ++attempt) {
                const Refusal later = refusal_of(channel.mint_established_session(ctx));
                if (later == ::fixy::some(LazyChannelRefusal::AlreadyClaimed)) ++refused;
            }
            std::move(first).detach(s::detach_reason::TestInstrumentation{});
            if (refused != 3) return 2;

            // A detached session does not give the claim back.  The protocol
            // position it held is abandoned, not returned.
            return refusal_of(channel.mint_established_session(ctx)).is_some() ? 0 : 3;
        },
        [](LazyChannelRefusal) noexcept -> int { return 1; });
}

[[nodiscard]] int names_the_protocol() {
    constexpr std::string_view name = Channel::protocol_name();
    if (name.find("Loop") == std::string_view::npos) return 1;
    if (name.find("Select") == std::string_view::npos) return 2;
    if (name.find("Send") == std::string_view::npos) return 3;
    return 0;
}

// Two observers ask at the same time after the publish.  The claim is
// one exchange, so exactly one of them gets the session.
[[nodiscard]] int two_racing_observers_get_one_session() {
    Channel channel;
    VesselChannel storage{};
    channel.establish(storage);

    std::atomic<bool> go{false};
    std::atomic<int> sessions{0};
    std::atomic<int> refusals{0};
    auto observe = [&] {
        const BgCtx ctx{eff::testing::bg()};
        while (!go.load(std::memory_order_acquire))
            CRUCIBLE_SPIN_PAUSE;
        const Refusal refusal = refusal_of(channel.mint_established_session(ctx));
        if (refusal.is_none()) {
            sessions.fetch_add(1, std::memory_order_acq_rel);
        } else if (refusal == ::fixy::some(LazyChannelRefusal::AlreadyClaimed)) {
            refusals.fetch_add(1, std::memory_order_acq_rel);
        }
    };
    {
        std::jthread left{observe};
        std::jthread right{observe};
        go.store(true, std::memory_order_release);
    }
    if (sessions.load(std::memory_order_acquire) != 1) return 1;
    if (refusals.load(std::memory_order_acquire) != 1) return 2;
    return 0;
}

// The startup this models: one thread prepares the resource and
// publishes it, while a worker polls the channel, takes the refusal
// before the publish, and drives the protocol after it.
[[nodiscard]] int worker_polls_until_the_publish() {
    Channel channel;
    VesselChannel storage{};
    std::atomic<int> processed{0};

    std::jthread worker{[&] {
        const BgCtx ctx{eff::testing::bg()};
        // The poll has no budget.  The main thread calls establish
        // unconditionally and then joins, so the loop ends.  A budget
        // would be a timeout that fails under scheduler pressure.
        for (;;) {
            const Refusal refusal = channel.mint_established_session(ctx).match(
                [&processed](Head head) noexcept -> Refusal {
                    auto next = std::move(head).select<0>(s::no_label).send(1, write_value);
                    static_cast<void>(std::move(next).select<1>(s::no_label).close());
                    processed.fetch_add(1, std::memory_order_release);
                    return ::fixy::none;
                },
                [](LazyChannelRefusal refused) noexcept -> Refusal { return ::fixy::some(refused); });
            if (refusal != ::fixy::some(LazyChannelRefusal::NotEstablished)) return;
            CRUCIBLE_SPIN_PAUSE;
        }
    }};

    // The publish is a release and the mint loads with acquire, so the
    // worker sees the sentinel that this thread wrote first.
    storage.sentinel = 7;
    channel.establish(storage);
    worker.join();

    if (processed.load(std::memory_order_acquire) != 1) return 1;
    if (storage.sentinel != 1 || storage.call_count != 1) return 2;
    return 0;
}

}  // namespace

int main() {
    if (const int rc = refuses_before_establish(); rc != 0) return rc;
    if (const int rc = drives_the_protocol_to_end(); rc != 0) return 100 + rc;
    if (const int rc = refuses_a_second_session(); rc != 0) return 200 + rc;
    if (const int rc = names_the_protocol(); rc != 0) return 300 + rc;
    if (const int rc = two_racing_observers_get_one_session(); rc != 0) return 400 + rc;
    if (const int rc = worker_polls_until_the_publish(); rc != 0) return 500 + rc;
    ::fixy::check::pass(kTest, "handle_lazy_channel: one session per resource, refusals, race and startup OK\n");
    return 0;
}
