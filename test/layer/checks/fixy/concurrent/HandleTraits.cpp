// The compile-time checks of fixy/concurrent/HandleTraits.h.

#include <fixy/concurrent/HandleTraits.h>

namespace fixy::concurrent {

namespace detail::handle_traits_self_test {

// ── the consumer and producer poles ─────────────────────────────────

struct synthetic_consumer {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

struct synthetic_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_hybrid {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_bool_pop {
    [[nodiscard]] bool try_pop() noexcept { return false; }
};

struct synthetic_value_pop {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synthetic_overloaded_pop {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] std::optional<int> try_pop(int) noexcept { return {}; }
};

struct synthetic_double_consumer {
    [[nodiscard]] std::optional<double> try_pop() noexcept { return {}; }
};

struct synthetic_void_push {
    void try_push(int const&) noexcept {}
};

struct synthetic_overloaded_push {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

struct synthetic_non_const_ref_push {
    [[nodiscard]] bool try_push(int&) noexcept { return true; }
};

struct synthetic_by_value_push {
    [[nodiscard]] bool try_push(int) noexcept { return true; }
};

struct synthetic_double_producer {
    [[nodiscard]] bool try_push(double const&) noexcept { return true; }
};

static_assert(is_consumer_handle_v<synthetic_consumer>);
static_assert(IsConsumerHandle<synthetic_consumer>);
static_assert(is_consumer_handle_v<synthetic_double_consumer>);

static_assert(is_consumer_handle_v<synthetic_consumer&>);
static_assert(is_consumer_handle_v<synthetic_consumer&&>);
static_assert(is_consumer_handle_v<synthetic_consumer const&>);

static_assert(!is_consumer_handle_v<int>);
static_assert(!is_consumer_handle_v<int*>);
static_assert(!is_consumer_handle_v<void>);
static_assert(!is_consumer_handle_v<synthetic_producer>);
static_assert(!is_consumer_handle_v<synthetic_hybrid>);
static_assert(!is_consumer_handle_v<synthetic_bool_pop>);
static_assert(!is_consumer_handle_v<synthetic_value_pop>);
static_assert(!is_consumer_handle_v<synthetic_overloaded_pop>);
static_assert(!is_consumer_handle_v<synthetic_consumer*>);

static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer>, int>);
static_assert(std::is_same_v<consumer_handle_value_t<synthetic_double_consumer>, double>);
static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer&>, int>);
static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer const&>, int>);

static_assert(is_producer_handle_v<synthetic_producer>);
static_assert(IsProducerHandle<synthetic_producer>);
static_assert(is_producer_handle_v<synthetic_double_producer>);

static_assert(is_producer_handle_v<synthetic_producer&>);
static_assert(is_producer_handle_v<synthetic_producer&&>);
static_assert(is_producer_handle_v<synthetic_producer const&>);

static_assert(!is_producer_handle_v<int>);
static_assert(!is_producer_handle_v<int*>);
static_assert(!is_producer_handle_v<void>);
static_assert(!is_producer_handle_v<synthetic_consumer>);
static_assert(!is_producer_handle_v<synthetic_hybrid>);
static_assert(!is_producer_handle_v<synthetic_void_push>);
static_assert(!is_producer_handle_v<synthetic_overloaded_push>);
static_assert(!is_producer_handle_v<synthetic_non_const_ref_push>);
static_assert(!is_producer_handle_v<synthetic_by_value_push>);
static_assert(!is_producer_handle_v<synthetic_producer*>);

static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer>, int>);
static_assert(std::is_same_v<producer_handle_value_t<synthetic_double_producer>, double>);
static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer&>, int>);
static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer const&>, int>);

// ── the single-writer poles ─────────────────────────────────────────

struct synthetic_writer {
    void publish(int const&) noexcept {}
};

struct synthetic_reader {
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synthetic_swmr_hybrid {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synthetic_bool_publish {
    [[nodiscard]] bool publish(int const&) noexcept { return true; }
};

struct synthetic_by_value_publish {
    void publish(int) noexcept {}
};

struct synthetic_non_const_load {
    [[nodiscard]] int load() noexcept { return 0; }
};

struct synthetic_void_load {
    void load() const noexcept {}
};

struct synthetic_double_writer {
    void publish(double const&) noexcept {}
};

struct synthetic_double_reader {
    [[nodiscard]] double load() const noexcept { return 0.0; }
};

static_assert(is_swmr_writer_v<synthetic_writer>);
static_assert(IsSwmrWriter<synthetic_writer>);
static_assert(is_swmr_writer_v<synthetic_double_writer>);
static_assert(is_swmr_writer_v<synthetic_writer&>);
static_assert(is_swmr_writer_v<synthetic_writer&&>);
static_assert(is_swmr_writer_v<synthetic_writer const&>);

static_assert(!is_swmr_writer_v<int>);
static_assert(!is_swmr_writer_v<void>);
static_assert(!is_swmr_writer_v<synthetic_reader>);
static_assert(!is_swmr_writer_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_writer_v<synthetic_bool_publish>);
static_assert(!is_swmr_writer_v<synthetic_by_value_publish>);
static_assert(!is_swmr_writer_v<synthetic_producer>);
static_assert(!is_swmr_writer_v<synthetic_value_pop>);
static_assert(!is_swmr_writer_v<synthetic_writer*>);

static_assert(is_swmr_reader_v<synthetic_reader>);
static_assert(IsSwmrReader<synthetic_reader>);
static_assert(is_swmr_reader_v<synthetic_double_reader>);
static_assert(is_swmr_reader_v<synthetic_reader&>);
static_assert(is_swmr_reader_v<synthetic_reader&&>);
static_assert(is_swmr_reader_v<synthetic_reader const&>);

static_assert(!is_swmr_reader_v<int>);
static_assert(!is_swmr_reader_v<void>);
static_assert(!is_swmr_reader_v<synthetic_writer>);
static_assert(!is_swmr_reader_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_reader_v<synthetic_non_const_load>);
static_assert(!is_swmr_reader_v<synthetic_void_load>);
static_assert(!is_swmr_reader_v<synthetic_producer>);
static_assert(!is_swmr_reader_v<synthetic_value_pop>);
static_assert(!is_swmr_reader_v<synthetic_reader*>);

static_assert(std::is_same_v<swmr_writer_value_t<synthetic_writer>, int>);
static_assert(std::is_same_v<swmr_writer_value_t<synthetic_double_writer>, double>);
static_assert(std::is_same_v<swmr_writer_value_t<synthetic_writer const&>, int>);

static_assert(std::is_same_v<swmr_reader_value_t<synthetic_reader>, int>);
static_assert(std::is_same_v<swmr_reader_value_t<synthetic_double_reader>, double>);
static_assert(std::is_same_v<swmr_reader_value_t<synthetic_reader const&>, int>);

// ── the four poles are mutually exclusive ───────────────────────────
//
// Each synthetic above answers yes to exactly one predicate.  The four
// predicates share one header, so the whole matrix is one claim.

static_assert(is_consumer_handle_v<synthetic_consumer> && !is_producer_handle_v<synthetic_consumer>
              && !is_swmr_writer_v<synthetic_consumer> && !is_swmr_reader_v<synthetic_consumer>);
static_assert(!is_consumer_handle_v<synthetic_producer> && is_producer_handle_v<synthetic_producer>
              && !is_swmr_writer_v<synthetic_producer> && !is_swmr_reader_v<synthetic_producer>);
static_assert(!is_consumer_handle_v<synthetic_writer> && !is_producer_handle_v<synthetic_writer>
              && is_swmr_writer_v<synthetic_writer> && !is_swmr_reader_v<synthetic_writer>);
static_assert(!is_consumer_handle_v<synthetic_reader> && !is_producer_handle_v<synthetic_reader>
              && !is_swmr_writer_v<synthetic_reader> && is_swmr_reader_v<synthetic_reader>);

// ── the channel a handle names ──────────────────────────────────────

struct channel_a {};
struct channel_b {};

struct producer_on_a {
    using channel_type = channel_a;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};
struct consumer_on_a {
    using channel_type = channel_a;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};
struct consumer_on_b {
    using channel_type = channel_b;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

// A consumer and a producer on channel a that report their identity.
struct reporting_consumer_on_a {
    using channel_type = channel_a;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] ::foundation::ChannelIdentity<channel_a> channel_identity() const noexcept { return {}; }
};
struct reporting_producer_on_a {
    using channel_type = channel_a;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] ::foundation::ChannelIdentity<channel_a> channel_identity() const noexcept { return {}; }
};

static_assert(NamesItsChannel<producer_on_a> && NamesItsChannel<consumer_on_b const&>);
static_assert(!NamesItsChannel<synthetic_producer>);
static_assert(HandlesShareChannel<producer_on_a, consumer_on_a>);
static_assert(!HandlesShareChannel<producer_on_a, consumer_on_b>, "one payload, two channels");
static_assert(!HandlesShareChannel<synthetic_producer, synthetic_consumer>,
              "a handle that names no channel meets none");
static_assert(ReportsChannelIdentity<reporting_consumer_on_a> && !ReportsChannelIdentity<consumer_on_a>);
static_assert(HandlesShareChannel<reporting_producer_on_a, reporting_consumer_on_a>);
static_assert(!HandlesShareChannel<producer_on_a, reporting_consumer_on_a>,
              "a handle that reports its identity meets no handle that hides it");

}  // namespace detail::handle_traits_self_test

}  // namespace fixy::concurrent
