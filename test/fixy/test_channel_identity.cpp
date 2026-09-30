// Each channel and each single writer has an identity per instance.
//
// A channel takes the brand of the root that its permissions grow from, so
// two channels with roots from two call sites are two types, and the
// pipeline refuses to join them at compile time.  One call site that runs
// two times mints two roots of one brand, and no type tells those apart.
// The run-time floor there is a claim on each linear role of a channel and
// a check at the pipeline mint that each link joins one channel instance.
// Each attack below runs in a child process, which must end.  The positive
// cases show that a claim comes back when its handle ends, and that a
// pipeline over one channel instance for each link passes the check.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/SwmrSession.h>

#include <foundation/Brand.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;
namespace ses = fixy::concurrent::swmr_session;

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
// refuses a second writer of that session.
void writer_move_keeps_the_source_claim() {
    Swmr first_session{reader_root(), 0};
    Swmr second_session{reader_root(), 0};
    auto target = ses::mint_swmr_writer<Swmr>(first_session, writer_root());
    auto source = ses::mint_swmr_writer<Swmr>(second_session, writer_root());
    target = std::move(source);
    [[maybe_unused]] auto second = ses::mint_swmr_writer<Swmr>(second_session, writer_root());
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

struct Attack {
    const char* name;
    void (*run)();
};

constexpr Attack kAttacks[] = {
    {"a second live producer of one SPSC channel", &second_spsc_producer},
    {"a second live consumer of one SPSC channel", &second_spsc_consumer},
    {"recombined access while a handle of the channel lives", &recombined_access_while_a_handle_lives},
    {"a second live consumer of one MPSC channel", &second_mpsc_consumer},
    {"a second live writer of one SWMR session", &second_swmr_writer},
    {"a second writer of a session after its writer moved into another handle", &writer_move_keeps_the_source_claim},
    {"a pipeline that joins two channels of one type", &pipeline_joins_two_links_of_one_type},
};

[[nodiscard]] bool ends_the_process(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: a refused claim ends the process, so it runs in a
    // child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        attack();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0;
}

// A claim comes back when its handle ends, so a handle made later from a
// second root of the same site takes the role.
[[nodiscard]] int claims_come_back() {
    Link channel{};
    {
        auto [producer, consumer] = link_halves();
        auto held_producer = channel.producer(std::move(producer));
        auto held_consumer = channel.consumer(std::move(consumer));
        auto moved = std::move(held_producer);
        if (!moved.try_push(3)) return 1;
        const std::optional<int> item = held_consumer.try_pop();
        if (!item || *item != 3) return 1;
    }
    auto [producer, consumer] = link_halves();
    auto again = channel.producer(std::move(producer));
    if (!again.try_push(4)) return 1;

    Swmr session{reader_root(), 0};
    { [[maybe_unused]] auto first_writer = ses::mint_swmr_writer<Swmr>(session, writer_root()); }
    auto later_writer = ses::mint_swmr_writer<Swmr>(session, writer_root());
    later_writer.publish(5);
    const auto reader = ses::mint_swmr_reader<Swmr>(session);
    if (!reader || reader->load() != 5) return 1;

    // A move into a writer handle gives the claim of the target session
    // back and keeps the claim of the source session.
    Swmr first_session{reader_root(), 0};
    Swmr second_session{reader_root(), 0};
    auto target = ses::mint_swmr_writer<Swmr>(first_session, writer_root());
    auto source = ses::mint_swmr_writer<Swmr>(second_session, writer_root());
    target = std::move(source);
    auto after = ses::mint_swmr_writer<Swmr>(first_session, writer_root());
    after.publish(6);
    target.publish(7);
    const auto first_reader = ses::mint_swmr_reader<Swmr>(first_session);
    const auto second_reader = ses::mint_swmr_reader<Swmr>(second_session);
    if (!first_reader || first_reader->load() != 6) return 1;
    if (!second_reader || second_reader->load() != 7) return 1;
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
    std::fprintf(stderr, "[expected] each attack below prints the violation report of a child process\n");
    int failures = 0;
    for (const Attack& attack : kAttacks) {
        if (!ends_the_process(attack.run)) {
            std::fprintf(stderr, "FAIL: %s was not refused\n", attack.name);
            ++failures;
        }
    }
    if (claims_come_back() != 0) {
        std::fprintf(stderr, "FAIL: a claim did not come back when its handle ended\n");
        ++failures;
    }
    if (pipeline_over_one_link_runs() != 0) {
        std::fprintf(stderr, "FAIL: a pipeline over one channel for each link did not run\n");
        ++failures;
    }
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_channel_identity: %zu attacks refused\n", std::size(kAttacks));
    return EXIT_SUCCESS;
}
