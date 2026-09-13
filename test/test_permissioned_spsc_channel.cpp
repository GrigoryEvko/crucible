#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

// A whole permission may be minted once per tag per program, so each
// test below gets a tag of its own.

struct ChannelSingle {};
struct ChannelCap {};
struct ChannelXT {};
struct ChannelSize {};

template <typename H>
concept HasTryPushInt = requires(H h) {
    { h.try_push(int{}) } -> std::same_as<bool>;
};

template <typename H>
concept HasTryPopInt = requires(H h) {
    { h.try_pop() };
};

using PChan = PermissionedSpscChannel<int, 64, ChannelSingle>;

static_assert(HasTryPushInt<PChan::ProducerHandle>, "ProducerHandle must expose try_push(T)");
static_assert(!HasTryPopInt<PChan::ProducerHandle>, "ProducerHandle must NOT expose try_pop — role discrimination");
static_assert(!HasTryPushInt<PChan::ConsumerHandle>, "ConsumerHandle must NOT expose try_push — role discrimination");
static_assert(HasTryPopInt<PChan::ConsumerHandle>, "ConsumerHandle must expose try_pop");

static_assert(!std::is_copy_constructible_v<PChan::ProducerHandle>);
static_assert(!std::is_copy_assignable_v<PChan::ProducerHandle>);
static_assert(std::is_move_constructible_v<PChan::ProducerHandle>);
static_assert(!std::is_move_assignable_v<PChan::ProducerHandle>,
              "ProducerHandle move-assign must be deleted, because the handle "
              "binds a reference.");

static_assert(!std::is_copy_constructible_v<PChan::ConsumerHandle>);
static_assert(!std::is_copy_assignable_v<PChan::ConsumerHandle>);
static_assert(std::is_move_constructible_v<PChan::ConsumerHandle>);
static_assert(!std::is_move_assignable_v<PChan::ConsumerHandle>);

static_assert(!std::is_default_constructible_v<PChan::ProducerHandle>,
              "ProducerHandle must be constructible only via channel.producer(), "
              "never by default-construction.");
static_assert(!std::is_default_constructible_v<PChan::ConsumerHandle>);

static_assert(!std::is_copy_constructible_v<PChan>);
static_assert(!std::is_move_constructible_v<PChan>);
static_assert(!std::is_copy_assignable_v<PChan>);
static_assert(!std::is_move_assignable_v<PChan>);

static_assert(splits_into_v<spsc_tag::Whole<ChannelSingle>, spsc_tag::Producer<ChannelSingle>,
                            spsc_tag::Consumer<ChannelSingle>>);
static_assert(splits_into_v<spsc_tag::Whole<ChannelXT>, spsc_tag::Producer<ChannelXT>, spsc_tag::Consumer<ChannelXT>>);

static_assert(!std::is_same_v<spsc_tag::Producer<ChannelSingle>, spsc_tag::Producer<ChannelXT>>);

static_assert(!splits_into_v<spsc_tag::Whole<ChannelSingle>, spsc_tag::Consumer<ChannelSingle>,
                             spsc_tag::Producer<ChannelSingle>>,
              "splits_into must be order-sensitive: Whole → Producer + Consumer "
              "(not Whole → Consumer + Producer)");

// The permission member is empty in both build configurations, so a
// handle costs one pointer and nothing more.

using SizeChan = PermissionedSpscChannel<std::uint64_t, 16, ChannelSize>;

static_assert(sizeof(SizeChan::ProducerHandle) == sizeof(SizeChan*),
              "ProducerHandle must be the size of a channel pointer, because the "
              "permission is an empty class that [[no_unique_address]] collapses.");
static_assert(sizeof(SizeChan::ConsumerHandle) == sizeof(SizeChan*),
              "ConsumerHandle must be the size of a channel pointer.");

int run_single_threaded() {
    PChan channel;
    auto whole = mint_permission_root<spsc_tag::Whole<ChannelSingle>>();
    auto [pp, cp] =
        mint_permission_split<spsc_tag::Producer<ChannelSingle>, spsc_tag::Consumer<ChannelSingle>>(std::move(whole));
    auto p = channel.producer(std::move(pp));
    auto c = channel.consumer(std::move(cp));

    if (!p.try_push(1)) return 1;
    if (!p.try_push(2)) return 2;
    if (!p.try_push(3)) return 3;

    if (channel.size_approx() != 3) return 4;

    auto v1 = c.try_pop();
    auto v2 = c.try_pop();
    auto v3 = c.try_pop();
    auto v4 = c.try_pop();

    if (!v1 || *v1 != 1) return 11;
    if (!v2 || *v2 != 2) return 12;
    if (!v3 || *v3 != 3) return 13;
    if (v4) return 14;  // empty after 3 pops

    if (!c.empty_approx()) return 15;
    return 0;
}

int run_capacity_bound() {
    PermissionedSpscChannel<int, 4, ChannelCap> small;
    auto whole = mint_permission_root<spsc_tag::Whole<ChannelCap>>();
    auto [pp, cp] =
        mint_permission_split<spsc_tag::Producer<ChannelCap>, spsc_tag::Consumer<ChannelCap>>(std::move(whole));
    auto p = small.producer(std::move(pp));

    if (!p.try_push(0)) return 1;
    if (!p.try_push(1)) return 2;
    if (!p.try_push(2)) return 3;
    if (!p.try_push(3)) return 4;
    if (p.try_push(4)) return 5;  // the fifth push meets a full channel

    if (p.size_approx() != 4) return 6;

    // Draining one slot must make room for one more push.
    auto c = small.consumer(std::move(cp));
    auto v0 = c.try_pop();
    if (!v0 || *v0 != 0) return 7;
    if (!p.try_push(4)) return 8;
    return 0;
}

// The handles must survive a move onto another thread, which is how a
// dispatch thread and a drain thread each end up holding one end.  The
// consumer sums what it receives, so the expected total catches a lost
// or duplicated item that a plain count would miss.

int run_cross_thread() {
    PermissionedSpscChannel<int, 256, ChannelXT> channel;
    auto whole = mint_permission_root<spsc_tag::Whole<ChannelXT>>();
    auto [pp, cp] =
        mint_permission_split<spsc_tag::Producer<ChannelXT>, spsc_tag::Consumer<ChannelXT>>(std::move(whole));
    auto p = channel.producer(std::move(pp));
    auto c = channel.consumer(std::move(cp));

    constexpr int kCount = 10000;
    int produced = 0;
    long consumed_sum = 0;

    {
        std::jthread producer_thread{[p = std::move(p), &produced]() mutable {
            for (int i = 0; i < kCount; ++i) {
                while (!p.try_push(i))
                    CRUCIBLE_SPIN_PAUSE;
                ++produced;
            }
        }};

        std::jthread consumer_thread{[c = std::move(c), &consumed_sum]() mutable {
            int n = 0;
            while (n < kCount) {
                if (auto v = c.try_pop()) {
                    consumed_sum += *v;
                    ++n;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        }};
        // The scope exists so that both threads join before the counters
        // below are read.
    }

    constexpr long kExpected = (static_cast<long>(kCount) - 1) * static_cast<long>(kCount) / 2;
    if (produced != kCount) return 1;
    if (consumed_sum != kExpected) return 2;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_single_threaded(); rc != 0) return 100 + rc;
    if (int rc = run_capacity_bound(); rc != 0) return 200 + rc;
    if (int rc = run_cross_thread(); rc != 0) return 300 + rc;
    std::puts("permissioned_spsc_channel: single + capacity + cross-thread OK");
    return 0;
}
