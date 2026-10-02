// A lazily established channel gives one session over its resource.
// These runs cover the refusal before establish, the session after it,
// the refusal of every later request, the race between two observers,
// and the startup that polls from a worker thread.

#include <fixy/handle/LazyEstablishedChannel.h>

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstdio>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include "../test_assert.h"

namespace {

namespace h = ::fixy::handle;
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
using Channel = h::LazyEstablishedChannel<DispatchProto, VesselChannel>;

static_assert(std::is_same_v<typename Channel::protocol, DispatchProto>);
static_assert(std::is_same_v<typename Channel::resource_type, VesselChannel>);
static_assert(sizeof(Channel) == 2 * sizeof(std::atomic<VesselChannel*>));

constexpr auto write_value = [](VesselChannel& channel, int& value) noexcept {
    channel.sentinel = value;
    ++channel.call_count;
    return true;
};

[[nodiscard]] int refuses_before_establish() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    if (channel.is_established()) return 1;
    auto first = channel.mint_established_session(ctx);
    if (first.has_value() || first.error() != h::LazyChannelRefusal::NotEstablished) return 2;

    // A request before establish claims nothing, so the session is
    // still there after the publish.
    VesselChannel storage{};
    channel.establish(storage);
    auto second = channel.mint_established_session(ctx);
    if (!second.has_value()) return 3;
    std::move(*second).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}

[[nodiscard]] int drives_the_protocol_to_end() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    VesselChannel storage{};
    storage.sentinel = 42;
    channel.establish(storage);
    if (!channel.is_established()) return 1;

    auto head = channel.mint_established_session(ctx);
    if (!head.has_value()) return 2;
    if (&head->resource() != &storage || head->resource().sentinel != 42) return 3;

    auto next = std::move(*head).select<0>(s::no_label).send(99, write_value);
    if (storage.sentinel != 99 || storage.call_count != 1) return 4;
    VesselChannel& back = std::move(next).select<1>(s::no_label).close();
    if (&back != &storage) return 5;
    return 0;
}

[[nodiscard]] int refuses_a_second_session() {
    const BgCtx ctx{eff::testing::bg()};
    Channel channel;
    VesselChannel storage{};
    channel.establish(storage);

    auto first = channel.mint_established_session(ctx);
    if (!first.has_value()) return 1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto later = channel.mint_established_session(ctx);
        if (later.has_value() || later.error() != h::LazyChannelRefusal::AlreadyClaimed) return 2;
    }
    std::move(*first).detach(s::detach_reason::TestInstrumentation{});

    // A detached session does not give the claim back.  The protocol
    // position it held is abandoned, not returned.
    auto after_detach = channel.mint_established_session(ctx);
    if (after_detach.has_value()) return 3;
    return 0;
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
        auto head = channel.mint_established_session(ctx);
        if (head.has_value()) {
            sessions.fetch_add(1, std::memory_order_acq_rel);
            std::move(*head).detach(s::detach_reason::TestInstrumentation{});
        } else if (head.error() == h::LazyChannelRefusal::AlreadyClaimed) {
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
            auto head = channel.mint_established_session(ctx);
            if (!head.has_value()) {
                if (head.error() != h::LazyChannelRefusal::NotEstablished) return;
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            auto next = std::move(*head).select<0>(s::no_label).send(1, write_value);
            static_cast<void>(std::move(next).select<1>(s::no_label).close());
            processed.fetch_add(1, std::memory_order_release);
            return;
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
    crucible::test::pass("handle_lazy_channel: one session per resource, refusals, race and startup OK\n");
    return 0;
}
