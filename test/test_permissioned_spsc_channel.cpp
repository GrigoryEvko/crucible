// Runtime + compile-time harness for PermissionedSpscChannel<T, N, Tag>
// — the SPSC worked example shipped to back task SEPLOG-INT-1 (#384,
// TraceRing wiring) and any future K-series production integration
// that needs a typed SPSC channel with role-discriminated endpoints.
//
// Coverage:
//   * Compile-time: role-discrimination concepts (try_push only on
//     Producer, try_pop only on Consumer); handles are move-only;
//     sizeof matches the EBO promise; splits_into specialisations
//     fire for arbitrary UserTag.
//   * Runtime: single-threaded push/pop FIFO; capacity-bound
//     enforcement; cross-thread handoff via std::jthread move.

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/safety/Permission.h>

#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::concurrent;
using namespace crucible::safety;

// ── Distinct UserTags per test (one Whole<Tag> mint per program) ──

struct ChannelSingle {};
struct ChannelCap    {};
struct ChannelXT     {};
struct ChannelSize   {};

// ── Compile-time: role discrimination via concept matching ────────

template <typename H>
concept HasTryPushInt = requires(H h) { { h.try_push(int{}) } -> std::same_as<bool>; };

template <typename H>
concept HasTryPopInt = requires(H h) { { h.try_pop() }; };

using PChan = PermissionedSpscChannel<int, 64, ChannelSingle>;

static_assert( HasTryPushInt<PChan::ProducerHandle>,
    "ProducerHandle must expose try_push(T)");
static_assert(!HasTryPopInt< PChan::ProducerHandle>,
    "ProducerHandle must NOT expose try_pop — role discrimination");
static_assert(!HasTryPushInt<PChan::ConsumerHandle>,
    "ConsumerHandle must NOT expose try_push — role discrimination");
static_assert( HasTryPopInt< PChan::ConsumerHandle>,
    "ConsumerHandle must expose try_pop");

// ── Compile-time: handles are move-only (NOT copyable, NOT
//    move-assignable per the reference-bound design) ──

static_assert(!std::is_copy_constructible_v<PChan::ProducerHandle>);
static_assert(!std::is_copy_assignable_v<   PChan::ProducerHandle>);
static_assert( std::is_move_constructible_v<PChan::ProducerHandle>);
static_assert(!std::is_move_assignable_v<   PChan::ProducerHandle>,
    "ProducerHandle move-assign must be deleted (reference member + "
    "explicit `= delete`).");

static_assert(!std::is_copy_constructible_v<PChan::ConsumerHandle>);
static_assert(!std::is_copy_assignable_v<   PChan::ConsumerHandle>);
static_assert( std::is_move_constructible_v<PChan::ConsumerHandle>);
static_assert(!std::is_move_assignable_v<   PChan::ConsumerHandle>);

// ── Compile-time: handles are NOT default-constructible (factory-only) ──

static_assert(!std::is_default_constructible_v<PChan::ProducerHandle>,
    "ProducerHandle must be constructible only via channel.producer(), "
    "never by default-construction.");
static_assert(!std::is_default_constructible_v<PChan::ConsumerHandle>);

// ── Compile-time: channel is Pinned (no copy, no move) ────────────

static_assert(!std::is_copy_constructible_v<PChan>);
static_assert(!std::is_move_constructible_v<PChan>);
static_assert(!std::is_copy_assignable_v<   PChan>);
static_assert(!std::is_move_assignable_v<   PChan>);

// ── Compile-time: splits_into specialisation fires per UserTag ────

static_assert(splits_into_v<spsc_tag::Whole<ChannelSingle>,
                            spsc_tag::Producer<ChannelSingle>,
                            spsc_tag::Consumer<ChannelSingle>>);
static_assert(splits_into_v<spsc_tag::Whole<ChannelXT>,
                            spsc_tag::Producer<ChannelXT>,
                            spsc_tag::Consumer<ChannelXT>>);

// Tags are STRUCTURALLY distinct: Producer<A> ≠ Producer<B>.
static_assert(!std::is_same_v<spsc_tag::Producer<ChannelSingle>,
                              spsc_tag::Producer<ChannelXT>>);

// Negative — splits_into is NOT specialised for swapped or extra tags.
static_assert(!splits_into_v<spsc_tag::Whole<ChannelSingle>,
                             spsc_tag::Consumer<ChannelSingle>,
                             spsc_tag::Producer<ChannelSingle>>,
    "splits_into must be order-sensitive: Whole → Producer + Consumer "
    "(not Whole → Consumer + Producer)");

// ── Compile-time: sizeof matches the EBO promise ──────────────────
//
// In RELEASE, the Permission member is empty (per #366); reference
// member is one pointer-sized internal storage; total handle sizeof
// equals sizeof(channel-pointer).  In DEBUG, the consumed_tracker
// inside Permission adds 1 byte + alignment padding — but Permission
// itself is empty in this header's usage (the consumed_tracker lives
// in SessionHandle, not in Permission).  So Permission stays empty
// in both modes here.

using SizeChan = PermissionedSpscChannel<std::uint64_t, 16, ChannelSize>;

static_assert(sizeof(SizeChan::ProducerHandle) == sizeof(SizeChan*),
    "ProducerHandle should be sizeof(channel-pointer) — Permission is "
    "an empty class collapsed by [[no_unique_address]].");
static_assert(sizeof(SizeChan::ConsumerHandle) == sizeof(SizeChan*),
    "ConsumerHandle should be sizeof(channel-pointer).");

// ── Runtime: single-threaded push/pop FIFO ────────────────────────

int run_single_threaded() {
    PChan channel;
    auto whole = permission_root_mint<spsc_tag::Whole<ChannelSingle>>();
    auto [pp, cp] = permission_split<spsc_tag::Producer<ChannelSingle>,
                                      spsc_tag::Consumer<ChannelSingle>>(
                                          std::move(whole));
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
    if ( v4)             return 14;  // empty after 3 pops

    if (!c.empty_approx()) return 15;
    return 0;
}

// ── Runtime: capacity bound enforcement ───────────────────────────

int run_capacity_bound() {
    PermissionedSpscChannel<int, 4, ChannelCap> small;
    auto whole = permission_root_mint<spsc_tag::Whole<ChannelCap>>();
    auto [pp, cp] = permission_split<spsc_tag::Producer<ChannelCap>,
                                      spsc_tag::Consumer<ChannelCap>>(
                                          std::move(whole));
    auto p = small.producer(std::move(pp));

    if (!p.try_push(0)) return 1;
    if (!p.try_push(1)) return 2;
    if (!p.try_push(2)) return 3;
    if (!p.try_push(3)) return 4;
    if ( p.try_push(4)) return 5;  // should fail — full at capacity 4

    if (p.size_approx() != 4) return 6;

    // Drain one slot — push should succeed again.
    auto c = small.consumer(std::move(cp));
    auto v0 = c.try_pop();
    if (!v0 || *v0 != 0) return 7;
    if (!p.try_push(4)) return 8;
    return 0;
}

// ── Runtime: cross-thread handoff via jthread move ────────────────
//
// Verifies the typed handles survive cross-thread move (the natural
// production pattern: dispatch thread holds Producer, drain thread
// holds Consumer).  Sums 0..N-1 on each side; producer count vs
// consumer sum cross-checks.

int run_cross_thread() {
    PermissionedSpscChannel<int, 256, ChannelXT> channel;
    auto whole = permission_root_mint<spsc_tag::Whole<ChannelXT>>();
    auto [pp, cp] = permission_split<spsc_tag::Producer<ChannelXT>,
                                      spsc_tag::Consumer<ChannelXT>>(
                                          std::move(whole));
    auto p = channel.producer(std::move(pp));
    auto c = channel.consumer(std::move(cp));

    constexpr int kCount = 10000;
    int produced     = 0;
    long consumed_sum = 0;

    {
        std::jthread producer_thread{
            [p = std::move(p), &produced]() mutable {
                for (int i = 0; i < kCount; ++i) {
                    while (!p.try_push(i)) std::this_thread::yield();
                    ++produced;
                }
            }
        };

        std::jthread consumer_thread{
            [c = std::move(c), &consumed_sum]() mutable {
                int n = 0;
                while (n < kCount) {
                    if (auto v = c.try_pop()) {
                        consumed_sum += *v;
                        ++n;
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
        };
        // jthread destructors auto-join here.
    }

    constexpr long kExpected = (static_cast<long>(kCount) - 1)
                             * static_cast<long>(kCount) / 2;
    if (produced != kCount)         return 1;
    if (consumed_sum != kExpected)  return 2;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_single_threaded(); rc != 0) return 100 + rc;
    if (int rc = run_capacity_bound();  rc != 0) return 200 + rc;
    if (int rc = run_cross_thread();    rc != 0) return 300 + rc;
    std::puts("permissioned_spsc_channel: single + capacity + cross-thread OK");
    return 0;
}
