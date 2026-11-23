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
//
// A frame door gives each handle to its body, so an attack opens the second
// handle of a role in the frame of the first one.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedChannel.h>
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

// The tag of Nondet, which the doors of the producer root of a merge take.
constexpr foundation::effects::cap::Nondet kNondet = foundation::effects::testing::test().nondet;

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
auto link_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LinkTag>>(); }
auto mpsc_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, MpscTag>>();
}
auto writer_root() noexcept { return perm::mint_permission_root<WriterTag>(); }
auto reader_root() noexcept { return perm::mint_permission_root<ReaderTag>(); }

// A second root site of the link tag, which gives a second brand.
auto other_link_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, LinkTag>>();
}

using Link = c::channel_t<int, 8, decltype(link_root())>;
using OtherLink = c::channel_t<int, 8, decltype(other_link_root())>;
using Mpsc = c::channel_t<int, 8, decltype(mpsc_root())>;
using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag, foundation::brand::brand_of_t<decltype(reader_root())>,
                              foundation::brand::brand_of_t<decltype(writer_root())>>;

// Two root sites of one tag give two channel types, and their ends name two
// channels.  A handle names the channel of its end.
static_assert(!std::is_same_v<Link, OtherLink>);
static_assert(!std::is_same_v<Link::ProducerEnd::channel_type, OtherLink::ConsumerEnd::channel_type>);
static_assert(!std::is_same_v<Link::brand_type, OtherLink::brand_type>);

auto link_halves() noexcept { return perm::mint_permission_split<Link::producer_tag, Link::consumer_tag>(link_root()); }

auto mpsc_halves() noexcept { return perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(mpsc_root()); }

void second_spsc_producer() {
    Link channel{};
    auto [first_producer, first_consumer] = link_halves();
    auto [second_producer, second_consumer] = link_halves();
    auto token = c::with_producer(
        channel, std::move(first_producer),
        [&channel, second = std::move(second_producer)](auto held) mutable noexcept {
            foundation::linear::drop(c::with_producer(channel, std::move(second),
                                                      [](auto again) noexcept { return std::move(again).release(); }));
            return std::move(held).release();
        });
    foundation::linear::drop(std::move(token));
}

void second_spsc_consumer() {
    Link channel{};
    auto [first_producer, first_consumer] = link_halves();
    auto [second_producer, second_consumer] = link_halves();
    auto token = c::with_consumer(
        channel, std::move(first_consumer),
        [&channel, second = std::move(second_consumer)](auto held) mutable noexcept {
            foundation::linear::drop(c::with_consumer(channel, std::move(second),
                                                      [](auto again) noexcept { return std::move(again).release(); }));
            return std::move(held).release();
        });
    foundation::linear::drop(std::move(token));
}

void recombined_access_while_a_handle_lives() {
    Link channel{};
    auto [producer, consumer] = link_halves();
    auto token = c::with_producer(channel, std::move(producer), [&channel](auto held) noexcept {
        static_cast<void>(channel.with_recombined_access(link_root(), [](auto&) noexcept {}));
        return std::move(held).release();
    });
    foundation::linear::drop(std::move(token));
}

void second_mpsc_consumer() {
    auto [first_producer, first_consumer] = mpsc_halves();
    auto [second_producer, second_consumer] = mpsc_halves();
    Mpsc channel{std::move(first_producer)};
    auto token = c::with_consumer(
        channel, std::move(first_consumer),
        [&channel, second = std::move(second_consumer)](auto held) mutable noexcept {
            foundation::linear::drop(c::with_consumer(channel, std::move(second),
                                                      [](auto again) noexcept { return std::move(again).release(); }));
            return std::move(held).release();
        });
    foundation::linear::drop(std::move(token));
}

// A recombined access of a merge channel needs the root of this channel out of
// its pool.  A second channel of the same site has the same type, so the
// lease of its root fits the door by its type, but it proves nothing about
// the producers of this channel.  A second root of the same site is no lease
// at all, and the door does not compile with it.
void merge_recombined_access_with_the_root_of_another_channel() {
    auto [producer_root, consumer_perm] = mpsc_halves();
    auto [other_root, other_consumer] = mpsc_halves();
    ::foundation::linear::drop(std::move(other_consumer));
    Mpsc channel{std::move(producer_root)};
    Mpsc other{std::move(other_root)};
    auto lease = other.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    auto parts = channel.with_recombined_access(
        Mpsc::RecombinedParts{.root = std::move(lease), .consumer = std::move(consumer_perm)},
        [](Mpsc::RecombinedRing&) noexcept {});
    ::foundation::linear::drop(std::move(parts.root));
    ::foundation::linear::drop(std::move(parts.consumer));
}

// A drain of a merge channel claims with the lease of the root of this channel.
// A second channel of the same site has the same type, so the lease of its root
// fits the claim by its type, but it proves nothing about the producers of this
// channel.  Only the identity of the pool tells the two apart.
void merge_drain_with_the_root_of_another_channel() {
    auto [producer_root, consumer_perm] = mpsc_halves();
    auto [other_root, other_consumer] = mpsc_halves();
    ::foundation::linear::drop(std::move(consumer_perm));
    ::foundation::linear::drop(std::move(other_consumer));
    Mpsc channel{std::move(producer_root)};
    Mpsc other{std::move(other_root)};
    auto lease = other.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    {
        Mpsc::DrainClaim claim = channel.claim_drain(lease);
        channel.drain(claim, [](int) noexcept {});
    }
    other.deposit_producer_root(std::move(lease));
}

// The deposit of the lease of another channel of the same site during a
// recombined access would let the pool lend a producer while the body owns
// the ring.  The claim of the producer side refuses it first.
void deposit_during_a_recombined_access() {
    auto [producer_root, consumer_perm] = mpsc_halves();
    auto [other_root, other_consumer] = mpsc_halves();
    ::foundation::linear::drop(std::move(other_consumer));
    Mpsc channel{std::move(producer_root)};
    Mpsc other{std::move(other_root)};
    auto lease = channel.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    auto parts = channel.with_recombined_access(
        Mpsc::RecombinedParts{.root = std::move(lease), .consumer = std::move(consumer_perm)},
        [&channel, &other](Mpsc::RecombinedRing&) noexcept {
            channel.deposit_producer_root(
                other.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out"));
        });
    ::foundation::linear::drop(std::move(parts.root));
    ::foundation::linear::drop(std::move(parts.consumer));
}

void second_swmr_writer() {
    Swmr session{reader_root(), 0};
    auto token = ses::with_writer(session, writer_root(), [&session](auto held) noexcept {
        foundation::linear::drop(
            ses::with_writer(session, writer_root(), [](auto second) noexcept { return std::move(second).release(); }));
        return std::move(held).release();
    });
    foundation::linear::drop(std::move(token));
}

// The target of a move takes the claim of the source session, so the claim
// refuses a second writer of that session.  A writer handle has no move
// assignment, because the assignment would drop the token of the target.
void writer_move_keeps_the_source_claim() {
    Swmr session{reader_root(), 0};
    auto token = ses::with_writer(session, writer_root(), [&session](auto source) noexcept {
        auto target = std::move(source);
        foundation::linear::drop(
            ses::with_writer(session, writer_root(), [](auto second) noexcept { return std::move(second).release(); }));
        return std::move(target).release();
    });
    foundation::linear::drop(std::move(token));
}

// A stage body that drains one link into the next.  The body does nothing,
// because the cases below check the mint and not the transfer.  Each handle
// names the scope of its frame, so the body is a template over the handles.
template <class In, class Out>
void into_link(In&&, Out&&) noexcept {}

// Two links of one type, from one root site.  The first stage writes the
// first link, and the second stage drains the second link, so no value
// goes from one to the other.  Each handle opens in the frame of the handle
// before it, and the innermost body mints the stages and the pipeline.
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
    c::with_consumer(
        source, std::move(source_consumer),
        [&ctx, &coordinator, &first, &second, &sink, first_producer = std::move(first_producer),
         second_consumer = std::move(second_consumer),
         sink_producer = std::move(sink_producer)](auto source_in) mutable noexcept {
            c::with_producer(
                first, std::move(first_producer),
                [&ctx, &coordinator, &second, &sink, source_in = std::move(source_in),
                 second_consumer = std::move(second_consumer),
                 sink_producer = std::move(sink_producer)](auto first_out) mutable noexcept {
                    c::with_consumer(
                        second, std::move(second_consumer),
                        [&ctx, &coordinator, &sink, source_in = std::move(source_in), first_out = std::move(first_out),
                         sink_producer = std::move(sink_producer)](auto second_in) mutable noexcept {
                            c::with_producer(
                                sink, std::move(sink_producer),
                                [&ctx, &coordinator, source_in = std::move(source_in), first_out = std::move(first_out),
                                 second_in = std::move(second_in)](auto sink_out) mutable noexcept {
                                    auto writes_first =
                                        c::mint_stage<&into_link<decltype(source_in), decltype(first_out)>>(
                                            ctx, std::move(source_in), std::move(first_out));
                                    auto drains_second =
                                        c::mint_stage<&into_link<decltype(second_in), decltype(sink_out)>>(
                                            ctx, std::move(second_in), std::move(sink_out));
                                    [[maybe_unused]] auto crossed = c::mint_pipeline(
                                        coordinator, std::move(writes_first), std::move(drains_second));
                                });
                        });
                });
        });
}

// A refused claim ends the process, so each attack runs in a child through
// expect_dies of the Check family.  The child must end by SIGABRT with the
// text of the refused claim.  The attacks run first, while the test process
// has one thread.
void attacks_end_the_process(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kSecondSwmrWriter{"two live writer handles of one SWMR session"};
    ::fixy::check::expect_dies(test, second_spsc_producer,
                               ::fixy::TextView{"two live producer handles of one wire channel"});
    ::fixy::check::expect_dies(test, second_spsc_consumer,
                               ::fixy::TextView{"two live consumer handles of one channel"});
    ::fixy::check::expect_dies(test, recombined_access_while_a_handle_lives,
                               ::fixy::TextView{"recombined access to a wire channel while its producer handle lives"});
    ::fixy::check::expect_dies(test, second_mpsc_consumer,
                               ::fixy::TextView{"two live consumer handles of one channel"});
    ::fixy::check::expect_dies(
        test, merge_recombined_access_with_the_root_of_another_channel,
        ::fixy::TextView{"recombined access to a merge channel with the producer root of another channel"});
    ::fixy::check::expect_dies(
        test, merge_drain_with_the_root_of_another_channel,
        ::fixy::TextView{"a drain of a merge channel with the producer root of another channel of its type"});
    ::fixy::check::expect_dies(
        test, deposit_during_a_recombined_access,
        ::fixy::TextView{
            "a deposit of the producer root of a merge channel while a recombined access or a drain runs"});
    ::fixy::check::expect_dies(test, second_swmr_writer, kSecondSwmrWriter);
    ::fixy::check::expect_dies(test, writer_move_keeps_the_source_claim, kSecondSwmrWriter);
    ::fixy::check::expect_dies(test, pipeline_joins_two_links_of_one_type,
                               ::fixy::TextView{"a pipeline joins two channels of one type"});
    ::fixy::check::pass(test, "  10 attacks refused\n");
}

// True when the reader door lends a reader of the session and the reader
// loads expected.  The lend of a share and the load take Nondet, which the
// context holds.
[[nodiscard]] bool loads_expected(::fixy::TestRunnerCtx const& spin, Swmr& session, int expected) noexcept {
    return ses::with_reader(spin, session,
                            [&spin, expected](auto reader) noexcept { return reader.load(spin) == expected; })
        .value_or(false);
}

// A claim comes back when its handle ends, so a handle made later from a
// second root of the same site takes the role.  The end of the producer
// closes the channel, and the later producer opens it again.
[[nodiscard]] int claims_come_back() {
    Link channel{};
    {
        auto [producer, consumer] = link_halves();
        bool const is_through = c::with_ends(
            channel, std::move(producer), std::move(consumer), [](auto held_producer, auto held_consumer) noexcept {
                auto moved = std::move(held_producer);
                bool const has_item = moved.try_push(3) && held_consumer.try_pop().value_or(-1) == 3;
                foundation::linear::drop(std::move(moved));
                foundation::linear::drop(std::move(held_consumer));
                return has_item;
            });
        if (!is_through) return 1;
    }
    if (!channel.is_closed()) return 1;
    auto [producer, consumer] = link_halves();
    // The later producer opens the channel, and the later consumer opens in
    // its frame after the push.
    bool const is_open_again = c::with_producer(
        channel, std::move(producer), [&channel, consumer = std::move(consumer)](auto again) mutable noexcept {
            bool const has_pushed = !channel.is_closed() && again.try_push(4);
            bool const has_received = c::with_consumer(channel, std::move(consumer), [](auto later_consumer) noexcept {
                bool const has_item = later_consumer.try_receive().value_or(-1) == 4;
                bool const is_open = later_consumer.try_receive().err().value_or(c::NoItem::Closed) == c::NoItem::Empty;
                foundation::linear::drop(std::move(later_consumer));
                return has_item && is_open;
            });
            foundation::linear::drop(std::move(again));
            return has_pushed && has_received;
        });
    if (!is_open_again) return 1;

    // The context of each reader: the lend of a share and the load take Nondet.
    const ::fixy::TestRunnerCtx spin{::foundation::effects::testing::test()};
    Swmr session{reader_root(), 0};
    ses::with_writer(session, writer_root(),
                     [](auto first_writer) noexcept { foundation::linear::drop(std::move(first_writer)); });
    bool const is_published = ses::with_writer(session, writer_root(), [&spin, &session](auto later_writer) noexcept {
        later_writer.publish(5);
        bool const is_loaded = loads_expected(spin, session, 5);
        foundation::linear::drop(std::move(later_writer));
        return is_loaded;
    });
    if (!is_published) return 1;

    // A move of a writer handle keeps the claim of its session in the
    // target, and the target writes the session.
    Swmr moved_session{reader_root(), 0};
    bool const is_moved_published =
        ses::with_writer(moved_session, writer_root(), [&spin, &moved_session](auto source) noexcept {
            auto target = std::move(source);
            target.publish(7);
            bool const is_loaded = loads_expected(spin, moved_session, 7);
            foundation::linear::drop(std::move(target));
            return is_loaded;
        });
    if (!is_moved_published) return 1;
    return 0;
}

// A chain in which each link is one channel instance passes the check at
// the mint and runs.  The link opens its two ends in one frame.
[[nodiscard]] int pipeline_over_one_link_runs() {
    const fixy::HotFgCtx ctx = foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{foundation::effects::testing::bg()};
    Link source{};
    Link link{};
    Link sink{};
    auto [source_producer, source_consumer] = link_halves();
    auto [link_producer, link_consumer] = link_halves();
    auto [sink_producer, sink_consumer] = link_halves();
    c::with_consumer(
        source, std::move(source_consumer),
        [&ctx, &coordinator, &link, &sink, link_producer = std::move(link_producer),
         link_consumer = std::move(link_consumer),
         sink_producer = std::move(sink_producer)](auto source_in) mutable noexcept {
            c::with_ends(link, std::move(link_producer), std::move(link_consumer),
                         [&ctx, &coordinator, &sink, source_in = std::move(source_in),
                          sink_producer = std::move(sink_producer)](auto link_out, auto link_in) mutable noexcept {
                             c::with_producer(
                                 sink, std::move(sink_producer),
                                 [&ctx, &coordinator, source_in = std::move(source_in), link_out = std::move(link_out),
                                  link_in = std::move(link_in)](auto sink_out) mutable noexcept {
                                     auto writes_link =
                                         c::mint_stage<&into_link<decltype(source_in), decltype(link_out)>>(
                                             ctx, std::move(source_in), std::move(link_out));
                                     auto drains_link =
                                         c::mint_stage<&into_link<decltype(link_in), decltype(sink_out)>>(
                                             ctx, std::move(link_in), std::move(sink_out));
                                     auto pipeline =
                                         c::mint_pipeline(coordinator, std::move(writes_link), std::move(drains_link));
                                     std::move(pipeline).run(coordinator);
                                 });
                         });
        });
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
