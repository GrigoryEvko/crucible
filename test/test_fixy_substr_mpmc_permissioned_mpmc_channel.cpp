// The include below forces the header's own static_asserts to be
// evaluated under the project warnings-as-errors flags, which only
// happens when a translation unit in the build graph pulls the header
// in.  The runtime witnesses are layered on top of those sentinels.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A tag local to this translation unit, so the channel's generic
// specialization mints a fresh Whole, Producer and Consumer triple
// rather than sharing one with another test.
struct V046TestUserTag {};

}  // namespace probes

using TestChannel = fsubstr::mpmc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>;

static_assert(std::is_same_v<TestChannel, cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>>,
              "fixy::substr::mpmc::PermissionedMpmcChannel must alias the substrate.");

static_assert(fsubstr::mpmc::MpmcValue<int>);
static_assert(fsubstr::mpmc::MpmcValue<int> == cc::MpmcValue<int>);
// The user-declared destructor is what makes this fail the concept.
struct NonMpmcValue {
    ~NonMpmcValue() {}
};
static_assert(!fsubstr::mpmc::MpmcValue<NonMpmcValue>);
static_assert(!cc::MpmcValue<NonMpmcValue>);

static_assert(std::is_same_v<fsubstr::mpmc::mpmc_tag::Whole<probes::V046TestUserTag>,
                             cc::mpmc_tag::Whole<probes::V046TestUserTag>>);
static_assert(std::is_same_v<fsubstr::mpmc::mpmc_tag::Producer<probes::V046TestUserTag>,
                             cc::mpmc_tag::Producer<probes::V046TestUserTag>>);
static_assert(std::is_same_v<fsubstr::mpmc::mpmc_tag::Consumer<probes::V046TestUserTag>,
                             cc::mpmc_tag::Consumer<probes::V046TestUserTag>>);

static_assert(std::is_same_v<typename TestChannel::value_type, int>);
static_assert(std::is_same_v<typename TestChannel::user_tag, probes::V046TestUserTag>);
static_assert(std::is_same_v<typename TestChannel::whole_tag, fsubstr::mpmc::mpmc_tag::Whole<probes::V046TestUserTag>>);
static_assert(
    std::is_same_v<typename TestChannel::producer_tag, fsubstr::mpmc::mpmc_tag::Producer<probes::V046TestUserTag>>);
static_assert(
    std::is_same_v<typename TestChannel::consumer_tag, fsubstr::mpmc::mpmc_tag::Consumer<probes::V046TestUserTag>>);

static_assert(TestChannel::channel_capacity == 64);

static_assert(fsubstr::mpmc::MpmcChannelSessionSurface<TestChannel>);

// The no-context mint is the right one here because the MPMC tag
// tree's permission row is empty.  Handles carry no wire permission,
// and the fractional pool state lives inside the channel rather than
// in the permission tokens.
static void test_runtime_construct_and_split() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    // The factories return an optional because the channel's pool can
    // be exhausted.
    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    if (!p_opt) std::abort();
    if (!c_opt) std::abort();
    (void)prod_perm;
    (void)cons_perm;
}

static void test_runtime_push_pop_roundtrip() {
    TestChannel ch{};
    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    if (!p_opt) std::abort();
    if (!c_opt) std::abort();

    // Well under the capacity, so no push can fail for want of room.
    constexpr int N = 16;
    for (int i = 0; i < N; ++i) {
        if (!p_opt->try_push(i * 100 + 7)) std::abort();
    }
    for (int i = 0; i < N; ++i) {
        std::optional<int> r = c_opt->try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    if (c_opt->try_pop()) std::abort();
}

static void test_runtime_capacity_constant() {
    static_assert(TestChannel::channel_capacity == 64);
    volatile std::size_t cap = TestChannel::channel_capacity;
    if (cap != 64) std::abort();
}

// The alias must resolve to the substrate type itself, not to a
// wrapper around it.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<TestChannel, cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>>);
    TestChannel ch{};
    cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>* via_sub = &ch;
    // The implicit pointer conversion compiles only if the two spellings
    // name one type.
    TestChannel* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::mpmc::ProducerProto<int>;
    using FixyCons = fsubstr::mpmc::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::mpmc_channel_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::mpmc_channel_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

int main() {
    test_runtime_construct_and_split();
    test_runtime_push_pop_roundtrip();
    test_runtime_capacity_constant();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_mpmc_permissioned_mpmc_channel: "
                "5/5 runtime witnesses passed\n");
    return 0;
}
