// Assertions embedded in a header are only verified under the
// project's warning flags when some translation unit includes that
// header.  This file exists to be that translation unit for the
// re-exported single-producer single-consumer channel surface, and it
// exercises that surface at runtime as well.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/_Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// The tag is local to this file so that the whole, producer and
// consumer triple is instantiated freshly here rather than reusing a
// triple some other translation unit already forced.
struct V045TestUserTag {};

}  // namespace probes

using TestChannel = fsubstr::spsc::PermissionedSpscChannel<int, 32, probes::V045TestUserTag>;

static_assert(std::is_same_v<TestChannel, cc::PermissionedSpscChannel<int, 32, probes::V045TestUserTag>>,
              "The re-exported channel must denote the original type.");

static_assert(fsubstr::spsc::SpscValue<int>);
static_assert(fsubstr::spsc::SpscValue<int> == cc::SpscValue<int>);
// A non-trivial destructor is what disqualifies this type, and it must
// disqualify it on both paths alike.
struct NonSpscValue {
    ~NonSpscValue() {}
};
static_assert(!fsubstr::spsc::SpscValue<NonSpscValue>);
static_assert(!cc::SpscValue<NonSpscValue>);

static_assert(std::is_same_v<fsubstr::spsc::spsc_tag::Whole<probes::V045TestUserTag>,
                             cc::spsc_tag::Whole<probes::V045TestUserTag>>);
static_assert(std::is_same_v<fsubstr::spsc::spsc_tag::Producer<probes::V045TestUserTag>,
                             cc::spsc_tag::Producer<probes::V045TestUserTag>>);
static_assert(std::is_same_v<fsubstr::spsc::spsc_tag::Consumer<probes::V045TestUserTag>,
                             cc::spsc_tag::Consumer<probes::V045TestUserTag>>);

static_assert(std::is_same_v<typename TestChannel::value_type, int>);
static_assert(std::is_same_v<typename TestChannel::user_tag, probes::V045TestUserTag>);
static_assert(std::is_same_v<typename TestChannel::whole_tag, fsubstr::spsc::spsc_tag::Whole<probes::V045TestUserTag>>);
static_assert(
    std::is_same_v<typename TestChannel::producer_tag, fsubstr::spsc::spsc_tag::Producer<probes::V045TestUserTag>>);
static_assert(
    std::is_same_v<typename TestChannel::consumer_tag, fsubstr::spsc::spsc_tag::Consumer<probes::V045TestUserTag>>);

static_assert(TestChannel::channel_capacity == 32);

static_assert(fsubstr::spsc::SpscChannelSessionSurface<TestChannel>);

// The permission row for this tag tree is empty, because a channel
// handle transfers no permission over the wire.  That is what makes
// the root mint valid here without a context to mint against.
static void test_runtime_construct_and_split() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(cons_perm));
    (void)producer;
    (void)consumer;
}

static void test_runtime_push_pop_roundtrip() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(cons_perm));

    constexpr int N = 8;  // well under capacity, so no push can fail
    for (int i = 0; i < N; ++i) {
        if (!producer.try_push(i * 100 + 7)) std::abort();
    }
    for (int i = 0; i < N; ++i) {
        std::optional<int> r = consumer.try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    if (consumer.try_pop()) std::abort();
}

static void test_runtime_telemetry_passes_through() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(cons_perm));

    if (!producer.empty_approx()) std::abort();
    if (producer.size_approx() != 0) std::abort();

    for (int i = 0; i < 4; ++i) {
        if (!producer.try_push(i)) std::abort();
    }
    if (producer.empty_approx()) std::abort();
    if (producer.size_approx() != 4) std::abort();

    if (producer.capacity() != 32) std::abort();
    if (consumer.capacity() != 32) std::abort();
}

static void test_runtime_capacity_bound() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(cons_perm));

    for (std::size_t i = 0; i < TestChannel::channel_capacity; ++i) {
        if (!producer.try_push(static_cast<int>(i))) std::abort();
    }
    if (producer.try_push(999)) std::abort();
    for (std::size_t i = 0; i < TestChannel::channel_capacity; ++i) {
        std::optional<int> r = consumer.try_pop();
        if (!r) std::abort();
        if (*r != static_cast<int>(i)) std::abort();
    }
    if (consumer.try_pop()) std::abort();
}

// Recombining the two halves back into the whole runs a body that
// takes no arguments.  The caller already holds the channel.
static void test_runtime_recombined_access() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    int sentinel = 0;
    auto recombined = ch.with_recombined_access(std::move(whole), [&sentinel]() { sentinel = 42; });
    (void)recombined;
    if (sentinel != 42) std::abort();
}

// The endpoint mints must return the original handle types, not a
// wrapper introduced by the re-export.
static void test_runtime_endpoint_handle_identity() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestChannel::producer_tag, TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(cons_perm));
    static_assert(std::is_same_v<decltype(producer), typename TestChannel::ProducerHandle>);
    static_assert(std::is_same_v<decltype(consumer), typename TestChannel::ConsumerHandle>);
}

// The protocol aliases must denote the original types too.
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::spsc::ProducerProto<int>;
    using FixyCons = fsubstr::spsc::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::spsc_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::spsc_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

int main() {
    test_runtime_construct_and_split();
    test_runtime_push_pop_roundtrip();
    test_runtime_telemetry_passes_through();
    test_runtime_capacity_bound();
    test_runtime_recombined_access();
    test_runtime_endpoint_handle_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_spsc_permissioned_spsc_channel: "
                "7/7 runtime witnesses passed\n");
    return 0;
}
