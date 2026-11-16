// Each channel and each single writer has an identity per instance.
//
// A channel takes the brand of the root that its permissions grow from, so
// two channels with roots from two call sites are two types, and the
// pipeline refuses to join them at compile time.  One call site that runs
// two times mints two roots of one brand, and no type tells those apart.
// The run-time floor there is a claim on each linear role of a channel and
// a check at the pipeline mint that each link joins one channel instance.
// Each attack below runs in a child process, which must end by SIGABRT with
// the text of its refusal.  The positive
// cases show that a claim comes back when its handle ends, and that a
// pipeline over one channel instance for each link passes the check.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;
namespace ses = fixy::channels::swmr_session;

namespace {

struct LinkTag {};
struct MpscTag {};
struct WriterTag {
    using permission_row = foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = foundation::effects::Row<>;
};

// One call site for every root of each channel type.  Each call mints a
// new root, and every root of one site has one brand.
auto link_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<LinkTag>>(); }
auto mpsc_root() noexcept { return perm::mint_permission_root<c::mpsc_tag::Whole<MpscTag>>(); }
auto writer_root() noexcept { return perm::mint_permission_root<WriterTag>(); }
auto reader_root() noexcept { return perm::mint_permission_root<ReaderTag>(); }

// A second root site of the link tag, which gives a second brand.
auto other_link_root() noexcept { return perm::mint_permission_root<c::spsc_tag::Whole<LinkTag>>(); }

using Link = c::spsc_channel_t<int, 8, decltype(link_root())>;
using OtherLink = c::spsc_channel_t<int, 8, decltype(other_link_root())>;
using Mpsc = c::mpsc_channel_t<int, 8, decltype(mpsc_root())>;
using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag, foundation::brand::brand_of_t<decltype(reader_root())>,
                              foundation::brand::brand_of_t<decltype(writer_root())>>;

// Two root sites of one tag give two channel types, and their handles
// name two channels.
static_assert(!std::is_same_v<Link, OtherLink>);
static_assert(!std::is_same_v<Link::ProducerHandle::channel_type, OtherLink::ConsumerHandle::channel_type>);
static_assert(!std::is_same_v<Link::brand_type, OtherLink::brand_type>);

auto link_halves() noexcept { return perm::mint_permission_split<Link::producer_tag, Link::consumer_tag>(link_root()); }

auto mpsc_halves() noexcept { return perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(mpsc_root()); }

void second_spsc_producer() {
    Link channel{};
    auto [first_producer, first_consumer] = link_halves();
    auto [second_producer, second_consumer] = link_halves();
    [[maybe_unused]] auto held = channel.producer(std::move(first_producer));
    [[maybe_unused]] auto second = channel.producer(std::move(second_producer));
}

void second_spsc_consumer() {
    Link channel{};
    auto [first_producer, first_consumer] = link_halves();
    auto [second_producer, second_consumer] = link_halves();
    [[maybe_unused]] auto held = channel.consumer(std::move(first_consumer));
    [[maybe_unused]] auto second = channel.consumer(std::move(second_consumer));
}

void recombined_access_while_a_handle_lives() {
    Link channel{};
    auto [producer, consumer] = link_halves();
    [[maybe_unused]] auto held = channel.producer(std::move(producer));
    (void)channel.with_recombined_access(link_root(), [](auto&) noexcept {});
}

void second_mpsc_consumer() {
    auto [first_producer, first_consumer] = mpsc_halves();
    auto [second_producer, second_consumer] = mpsc_halves();
    Mpsc channel{std::move(first_producer)};
    [[maybe_unused]] auto held = channel.consumer(std::move(first_consumer));
    [[maybe_unused]] auto second = channel.consumer(std::move(second_consumer));
}

void second_swmr_writer() {
    Swmr session{reader_root(), 0};
    [[maybe_unused]] auto held = ses::mint_swmr_writer<Swmr>(session, writer_root());
    [[maybe_unused]] auto second = ses::mint_swmr_writer<Swmr>(session, writer_root());
}

// The target of a move takes the claim of the source session, so the claim
// refuses a second writer of that session.  A writer handle has no move
// assignment, because the assignment would drop the token of the target.
void writer_move_keeps_the_source_claim() {
    Swmr session{reader_root(), 0};
    auto source = ses::mint_swmr_writer<Swmr>(session, writer_root());
    [[maybe_unused]] auto target = std::move(source);
    [[maybe_unused]] auto second = ses::mint_swmr_writer<Swmr>(session, writer_root());
}

// A stage body that drains one link into the next.  The body does nothing,
// because the cases below check the mint and not the transfer.
inline void into_link(Link::ConsumerHandle&&, Link::ProducerHandle&&) noexcept {}

// Two links of one type, from one root site.  The first stage writes the
// first link, and the second stage drains the second link, so no value
// goes from one to the other.
void pipeline_joins_two_links_of_one_type() {
    const fixy::HotFgCtx ctx = foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{foundation::effects::testing::bg()};
    Link source{};
    Link first{};
    Link second{};
    Link sink{};
    auto [source_producer, source_consumer] = link_halves();
    auto [first_producer, first_consumer] = link_halves();
    auto [second_producer, second_consumer] = link_halves();
    auto [sink_producer, sink_consumer] = link_halves();
    auto writes_first = c::mint_stage<&into_link>(ctx, source.consumer(std::move(source_consumer)),
                                                  first.producer(std::move(first_producer)));
    auto drains_second = c::mint_stage<&into_link>(ctx, second.consumer(std::move(second_consumer)),
                                                   sink.producer(std::move(sink_producer)));
    [[maybe_unused]] auto crossed = c::mint_pipeline(coordinator, std::move(writes_first), std::move(drains_second));
}

// A refused claim ends the process, so each attack runs in a child through
// expect_dies of the Check family.  The child must end by SIGABRT with the
// text of the refused claim.  The attacks run first, while the test process
// has one thread.
void attacks_end_the_process(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kSecondSwmrWriter{"two live writer handles of one SWMR session"};
    ::fixy::check::expect_dies(test, second_spsc_producer,
                               ::fixy::TextView{"two live producer handles of one SPSC channel"});
    ::fixy::check::expect_dies(test, second_spsc_consumer,
                               ::fixy::TextView{"two live consumer handles of one SPSC channel"});
    ::fixy::check::expect_dies(
        test, recombined_access_while_a_handle_lives,
        ::fixy::TextView{"recombined access to an SPSC channel while its producer handle lives"});
    ::fixy::check::expect_dies(test, second_mpsc_consumer,
                               ::fixy::TextView{"two live consumer handles of one MPSC channel"});
    ::fixy::check::expect_dies(test, second_swmr_writer, kSecondSwmrWriter);
    ::fixy::check::expect_dies(test, writer_move_keeps_the_source_claim, kSecondSwmrWriter);
    ::fixy::check::expect_dies(test, pipeline_joins_two_links_of_one_type,
                               ::fixy::TextView{"a pipeline joins two channels of one type"});
    ::fixy::check::pass(test, "  7 attacks refused\n");
}

// A claim comes back when its handle ends, so a handle made later from a
// second root of the same site takes the role.  The end of the producer
// closes the channel, and the later producer opens it again.
[[nodiscard]] int claims_come_back() {
    Link channel{};
    {
        auto [producer, consumer] = link_halves();
        auto held_producer = channel.producer(std::move(producer));
        auto held_consumer = channel.consumer(std::move(consumer));
        auto moved = std::move(held_producer);
        if (!moved.try_push(3)) return 1;
        if (held_consumer.try_pop().value_or(-1) != 3) return 1;
    }
    if (!channel.is_closed()) return 1;
    auto [producer, consumer] = link_halves();
    auto again = channel.producer(std::move(producer));
    if (channel.is_closed()) return 1;
    if (!again.try_push(4)) return 1;
    auto later_consumer = channel.consumer(std::move(consumer));
    bool const has_item = later_consumer.try_receive().value_or(-1) == 4;
    bool const is_open = later_consumer.try_receive().err().value_or(c::NoItem::Closed) == c::NoItem::Empty;
    ::foundation::linear::drop(std::move(later_consumer));
    if (!has_item || !is_open) return 1;

    Swmr session{reader_root(), 0};
    { [[maybe_unused]] auto first_writer = ses::mint_swmr_writer<Swmr>(session, writer_root()); }
    auto later_writer = ses::mint_swmr_writer<Swmr>(session, writer_root());
    later_writer.publish(5);
    // True when the reader exists and loads the expected value.
    const auto loads = [](auto const& reader, int expected) noexcept {
        return reader.match([expected](auto const& handle) noexcept { return handle.load() == expected; },
                            []() noexcept { return false; });
    };
    const auto reader = ses::mint_swmr_reader<Swmr>(session);
    if (!loads(reader, 5)) return 1;

    // A move of a writer handle keeps the claim of its session in the
    // target, and the target writes the session.
    Swmr moved_session{reader_root(), 0};
    auto source = ses::mint_swmr_writer<Swmr>(moved_session, writer_root());
    auto target = std::move(source);
    target.publish(7);
    const auto moved_reader = ses::mint_swmr_reader<Swmr>(moved_session);
    if (!loads(moved_reader, 7)) return 1;
    return 0;
}

// A chain in which each link is one channel instance passes the check at
// the mint and runs.
[[nodiscard]] int pipeline_over_one_link_runs() {
    const fixy::HotFgCtx ctx = foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{foundation::effects::testing::bg()};
    Link source{};
    Link link{};
    Link sink{};
    auto [source_producer, source_consumer] = link_halves();
    auto [link_producer, link_consumer] = link_halves();
    auto [sink_producer, sink_consumer] = link_halves();
    auto writes_link = c::mint_stage<&into_link>(ctx, source.consumer(std::move(source_consumer)),
                                                 link.producer(std::move(link_producer)));
    auto drains_link = c::mint_stage<&into_link>(ctx, link.consumer(std::move(link_consumer)),
                                                 sink.producer(std::move(sink_producer)));
    auto pipeline = c::mint_pipeline(coordinator, std::move(writes_link), std::move(drains_link));
    std::move(pipeline).run(coordinator);
    return 0;
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    attacks_end_the_process(test);
    CRUCIBLE_TEST_ASSERT(test, claims_come_back() == 0 && "a claim did not come back when its handle ended");
    CRUCIBLE_TEST_ASSERT(test,
                         pipeline_over_one_link_runs() == 0 && "a pipeline over one channel for each link did not run");
    ::fixy::check::pass(test, "test_channel_identity: PASSED\n");
    return 0;
}
