// A moved-from channel handle fails closed.
//
// Each endpoint of fixy/concurrent/PermissionedSpscChannel.h and
// fixy/concurrent/PermissionedMpscChannel.h holds its channel through
// foundation/ChannelBinding.h, which the move clears.  The move gives the
// linear Permission or the pool share to the new handle, so a use of the
// source would act through a token it no longer holds.  Each attack below
// runs in a child process, which must end.
//
// Two runs check the positive half.  The moved-into handle keeps working,
// and a handle moved into a thread carries the channel there with no data
// race, which the thread sanitizer preset checks.

#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <foundation/ChannelBinding.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <thread>
#include <type_traits>
#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {

struct SpscTag {};
struct MpscTag {};
struct ThreadedTag {};

using Spsc = c::PermissionedSpscChannel<int, 8, SpscTag>;
using Mpsc = c::PermissionedMpscChannel<int, 8, MpscTag>;

// The binding costs one pointer, and it keeps the handle move-only.
static_assert(sizeof(Spsc::ProducerHandle) == sizeof(void*));
static_assert(sizeof(Spsc::ConsumerHandle) == sizeof(void*));
static_assert(sizeof(Mpsc::ConsumerHandle) == sizeof(void*));
static_assert(!std::is_copy_constructible_v<Spsc::ProducerHandle>);
static_assert(std::is_nothrow_move_constructible_v<Spsc::ProducerHandle>);
static_assert(!std::is_move_assignable_v<Spsc::ProducerHandle>);
static_assert(!std::is_move_assignable_v<Mpsc::ConsumerHandle>);

// The binding on its own: a move leaves the source empty, and the moved-into
// binding reaches the object.
[[nodiscard]] int binding_moves_its_pointer() {
    int target = 5;
    foundation::ChannelBinding<int> first{target};
    if (!first.is_bound() || *first != 5) return 1;
    foundation::ChannelBinding<int> second{std::move(first)};
    if (first.is_bound() || !second.is_bound() || *second != 5) return 1;
    second.unbind();
    if (second.is_bound()) return 1;
    return 0;
}

[[nodiscard]] auto spsc_handles(Spsc& channel) {
    auto whole = perm::mint_permission_root<Spsc::whole_tag>();
    auto [producer, consumer] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(whole));
    return std::pair{channel.producer(std::move(producer)), channel.consumer(std::move(consumer))};
}

// Each attack reaches the moved-from handle through a call that the
// optimizer cannot see into.  Inlined, the empty binding is visible at
// compile time, and -Wstringop-overflow refuses the null access that the
// attack makes on purpose.
template <typename Handle>
[[gnu::noipa]] Handle& opaque_ref(Handle& handle) {
    return handle;
}

void spsc_producer() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_push(1);
}

void spsc_consumer() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void mpsc_producer() {
    Mpsc channel{};
    auto producer = channel.producer();
    [[maybe_unused]] auto moved = std::move(*producer);
    (void)opaque_ref(*producer).try_push(1);
}

// The moved-from producer holds no pool share, so the drained window opens.
// A push from it inside that window is the push the window forbids.
void mpsc_producer_in_drained_window() {
    Mpsc channel{};
    auto producer = channel.producer();
    { [[maybe_unused]] auto moved = std::move(*producer); }
    (void)channel.with_drained_access([&producer] { (void)opaque_ref(*producer).try_push(1); });
}

void mpsc_consumer() {
    Mpsc channel{};
    auto consumer = channel.consumer(perm::mint_permission_root<Mpsc::consumer_tag>());
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_pop();
}

void binding_after_move() {
    int target = 5;
    foundation::ChannelBinding<int> first{target};
    [[maybe_unused]] foundation::ChannelBinding<int> second{std::move(first)};
    (void)*opaque_ref(first);
}

struct Attack {
    const char* name;
    void (*run)();
};

constexpr Attack kAttacks[] = {
    {"spsc producer", &spsc_producer},
    {"spsc consumer", &spsc_consumer},
    {"mpsc producer", &mpsc_producer},
    {"mpsc producer in the drained window", &mpsc_producer_in_drained_window},
    {"mpsc consumer", &mpsc_consumer},
    {"bare binding", &binding_after_move},
};

[[nodiscard]] bool ends_the_process(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: a use the binding catches ends the process, so it
    // runs in a child that the parent observes.
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

[[nodiscard]] int moved_into_handles_work() {
    Spsc channel{};
    auto [producer, consumer] = spsc_handles(channel);
    auto producer_moved = std::move(producer);
    auto consumer_moved = std::move(consumer);
    if (!producer_moved.try_push(7)) return 1;
    const auto item = consumer_moved.try_pop();
    if (!item || *item != 7) return 1;
    return 0;
}

// The producer handle is moved into the thread that pushes, and the consumer
// stays here.  The thread sanitizer preset runs this with no report.
[[nodiscard]] int handle_moved_into_a_thread() {
    using Channel = c::PermissionedSpscChannel<int, 64, ThreadedTag>;
    constexpr int kItems = 2000;
    Channel channel{};
    auto whole = perm::mint_permission_root<Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    long long sum = 0;
    int received = 0;
    {
        std::jthread pusher{[handle = std::move(producer)]() mutable noexcept {
            for (int i = 1; i <= kItems;) {
                if (handle.try_push(i)) ++i;
            }
        }};
        while (received < kItems) {
            if (const auto item = consumer.try_pop()) {
                sum += *item;
                ++received;
            }
        }
    }
    const long long expected = static_cast<long long>(kItems) * (kItems + 1) / 2;
    return sum == expected ? 0 : 1;
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] each case below prints the contract report of a child process\n");
    int failures = 0;
    for (const Attack& attack : kAttacks) {
        if (!ends_the_process(attack.run)) {
            std::fprintf(stderr, "FAIL: a moved-from %s still acted on its channel\n", attack.name);
            ++failures;
        }
    }
    if (binding_moves_its_pointer() != 0) {
        std::fprintf(stderr, "FAIL: the binding did not move its pointer\n");
        ++failures;
    }
    if (moved_into_handles_work() != 0) {
        std::fprintf(stderr, "FAIL: a moved-into handle lost its channel\n");
        ++failures;
    }
    if (handle_moved_into_a_thread() != 0) {
        std::fprintf(stderr, "FAIL: a handle moved into a thread lost items\n");
        ++failures;
    }
    if (failures != 0) return 1;
    std::printf("test_channel_moved_from: %zu attacks refused\n", std::size(kAttacks));
    return 0;
}
