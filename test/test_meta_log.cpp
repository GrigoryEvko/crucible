#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Aliases.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

// A descriptor of the sizes [128, 256], whose data pointer is the synthetic
// address.  No code reads through the pointer.
static TensorMeta make_meta(SyntheticAddress address) {
    TensorMeta m{};
    m.data_ptr = mint_synthetic_data_ptr(address);
    m.ndim = 2;
    m.sizes[0] = ::crucible::tensor_dim(128);
    m.sizes[1] = ::crucible::tensor_dim(256);
    m.strides[0] = ::crucible::tensor_dim(256);
    m.strides[1] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx = 0;
    return m;
}

static void test_empty_state() {
    crucible::test::OpenLog log{};
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, !log.try_append(::fixy::View<const TensorMeta>{}).is_valid());
    ::fixy::check::pass(kTest, "  test_empty:                     PASSED\n");
}

static void test_single_append_returns_index_zero() {
    crucible::test::OpenLog log{};
    TensorMeta m = make_meta(SyntheticAddress{0x1000});
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    auto idx = log.try_append(record.view());
    CRUCIBLE_TEST_ASSERT(kTest, idx.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, idx.raw() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 1);
    const TensorMeta got = log.copy_record(idx);
    CRUCIBLE_TEST_ASSERT(kTest, got.data_ptr.value() == m.data_ptr.value());
    CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(got.sizes[0]) == 128);
    CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(got.sizes[1]) == 256);
    // A copy of a record and the record of the caller lie outside the buffer.
    CRUCIBLE_TEST_ASSERT(kTest, !log.holds_record(got) && !log.holds_record(m));
    ::fixy::check::pass(kTest, "  test_single_append:             PASSED\n");
}

static void test_batch_append_and_monotonic() {
    crucible::test::OpenLog log{};
    ::fixy::FixedArray<TensorMeta, 8> batch{};
    for (const auto index : batch.indices()) {
        const TensorMeta meta = make_meta(SyntheticAddress{0x1000 + index.raw() * 16});
        batch.set(index, meta);
    }
    const ::fixy::View<const TensorMeta> records{batch.view()};
    auto idx1 = log.try_append(records.window(0, 3).expect("the first run fits the batch"));
    auto idx2 = log.try_append(records.window(3, 5).expect("the second run fits the batch"));
    CRUCIBLE_TEST_ASSERT(kTest, idx1.is_valid() && idx2.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, idx1.raw() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, idx2.raw() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 8);

    // The copy of the two appends is the eight records in their order.
    auto const& log_read = log;
    ::fixy::FixedArray<TensorMeta, 8> const copied = ::fixy::build_fixed_array<TensorMeta, 8>(
        [&log_read, idx1](auto run) noexcept { log_read.copy_run(idx1, std::move(run)); });
    for (size_t i = 0; i < 8; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            copied.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value()
                == batch.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value());
    }
    ::fixy::check::pass(kTest, "  test_batch_monotonic:           PASSED\n");
}

static void test_tail_advance_frees_capacity() {
    crucible::test::OpenLog log{};
    TensorMeta m = make_meta(SyntheticAddress{0x2000});
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    auto idx = log.try_append(record.view());
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 1);
    log.release_to(::crucible::MetaIndex{idx.raw() + 1});
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 0);
    // Freeing capacity does not rewind the index.  Indices keep
    // counting, so the next append is 1 and not 0.
    auto idx2 = log.try_append(record.view());
    CRUCIBLE_TEST_ASSERT(kTest, idx2.raw() == 1);
    ::fixy::check::pass(kTest, "  test_tail_advance:              PASSED\n");
}

static void test_reset_zeroes_both_pointers() {
    crucible::test::OpenLog log{};
    TensorMeta m = make_meta(SyntheticAddress{0x3000});
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    for (int i = 0; i < 10; ++i)
        (void)log.try_append(record.view());
    log.release_to(::crucible::MetaIndex{5});
    log.reset();
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 0);
    // A reset does rewind the index, where advancing the tail does not.
    auto idx = log.try_append(record.view());
    CRUCIBLE_TEST_ASSERT(kTest, idx.raw() == 0);
    // The reset rewinds the copy of the tail of the consumer end too, so a
    // release after it moves the tail from zero.
    log.release_to(::crucible::MetaIndex{1});
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == ::crucible::MetaIndex{1} && log.size().peek() == 0);
    ::fixy::check::pass(kTest, "  test_reset:                     PASSED\n");
}

static void test_copy_run_across_the_end() {
    // The counters start three slots from the end of the ring.  The
    // next append then passes the end.  A fill and a drain of the ring hold a
    // second buffer of 176 MB, and the ring holds 176 MB too.
    const uint32_t near_end = MetaLog::CAPACITY - 3;
    crucible::test::OpenLog log{MetaIndex{near_end}};
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 0);

    // Five entries into three remaining slots: the batch straddles the
    // end of the ring.
    ::fixy::FixedArray<TensorMeta, 5> tail5{};
    for (const auto index : tail5.indices()) {
        const TensorMeta meta = make_meta(SyntheticAddress{0x5000 + index.raw() * 0x100});
        tail5.set(index, meta);
    }
    auto idx = log.try_append(tail5.view());
    CRUCIBLE_TEST_ASSERT(kTest, idx.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, idx.raw() == near_end);

    // The copy joins the three records at the end of the buffer and the two
    // at its start, in the order of the append.
    auto const& log_read = log;
    ::fixy::FixedArray<TensorMeta, 5> const copied = ::fixy::build_fixed_array<TensorMeta, 5>(
        [&log_read, idx](auto run) noexcept { log_read.copy_run(idx, std::move(run)); });
    for (uint32_t i = 0; i < 5; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            copied.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value()
                == tail5.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value());
    }

    // A run that does not pass the end is one copy.
    ::fixy::FixedArray<TensorMeta, 2> const head2 = ::fixy::build_fixed_array<TensorMeta, 2>(
        [&log_read, near_end](auto run) noexcept { log_read.copy_run(MetaIndex{near_end + 3}, std::move(run)); });
    CRUCIBLE_TEST_ASSERT(kTest, head2.at(::fixy::index<0>()).data_ptr.value()
                                    == tail5.at(::fixy::index<3>()).data_ptr.value());
    CRUCIBLE_TEST_ASSERT(kTest, head2.at(::fixy::index<1>()).data_ptr.value()
                                    == tail5.at(::fixy::index<4>()).data_ptr.value());
    ::fixy::check::pass(kTest, "  test_copy_run_across_the_end:   PASSED\n");
}

// The head and the tail count each record that the log held, and the index of
// a record is its count.  Appends and releases pass 2^32 records with no wrap.
// In 32 bits the record at 2^32 - 1 takes the index of none, and the append
// after it moves the head back.
static void test_counters_pass_two_to_the_32() {
    // The log starts sixteen records before the wrap of 32 bits.
    constexpr uint32_t BELOW_WRAP = 0xFFFFFFF0u;
    crucible::test::OpenLog log{MetaIndex{BELOW_WRAP}};

    ::fixy::FixedArray<TensorMeta, 32> run{};
    for (const auto index : run.indices()) {
        const TensorMeta meta = make_meta(SyntheticAddress{0x6000 + index.raw() * 0x100});
        run.set(index, meta);
    }
    const ::fixy::View<const TensorMeta> records{run.view()};
    const MetaIndex first = log.try_append(records.window(0, 15).expect("the first run fits the array"));
    CRUCIBLE_TEST_ASSERT(kTest, first.is_valid() && first.raw() == uint64_t{BELOW_WRAP});
    const MetaIndex last_below = log.try_append(records.window(15, 1).expect("the record fits the array"));
    CRUCIBLE_TEST_ASSERT(kTest, last_below.is_valid() && last_below.raw() == uint64_t{BELOW_WRAP} + 15);
    const MetaIndex across = log.try_append(records.window(16, 16).expect("the last run fits the array"));
    CRUCIBLE_TEST_ASSERT(kTest, across.is_valid() && across.raw() == (uint64_t{1} << 32));
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 32);

    // The records keep their order across the boundary, in the copy of the
    // run and in the copy of one record at each index.
    auto const& log_read = log;
    ::fixy::FixedArray<TensorMeta, 32> const copied = ::fixy::build_fixed_array<TensorMeta, 32>(
        [&log_read, first](auto cells) noexcept { log_read.copy_run(first, std::move(cells)); });
    for (uint32_t i = 0; i < 32; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            copied.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value()
                == run.get(i).expect("a test reads an element at or past the count of its array")->data_ptr.value());
    }
    const TensorMeta read_below = log.copy_record(last_below);
    const TensorMeta read_across = log.copy_record(across);
    CRUCIBLE_TEST_ASSERT(kTest, read_below.data_ptr.value() == run.at(::fixy::index<15>()).data_ptr.value());
    CRUCIBLE_TEST_ASSERT(kTest, read_across.data_ptr.value() == run.at(::fixy::index<16>()).data_ptr.value());

    log.release_to(::crucible::MetaIndex{across.raw() + 16});
    CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, log.total_released() == ::crucible::MetaIndex{(uint64_t{1} << 32) + 16});
    ::fixy::check::pass(kTest, "  test_counters_pass_2_to_the_32: PASSED\n");
}

// A run longer than the buffer holds records that the producer wrote over.
// The copy refuses such a run before it reads a slot.  The copy runs in a
// child through expect_dies of the Check family.
static void test_copy_run_refuses_a_run_longer_than_the_buffer(::fixy::TestRunnerCtx const& test) {
    crucible::test::OpenLog log{};
    ::fixy::HeapArray<TensorMeta> destination =
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, MetaLog::CAPACITY + 1, TensorMeta{});
    auto const& log_read = log;
    ::fixy::check::expect_dies(
        test,
        [&log_read, &destination] noexcept {
            destination = ::fixy::with_mut(std::move(destination), [&log_read](auto run) noexcept {
                log_read.copy_run(MetaIndex{0}, std::move(run));
            });
        },
        ::fixy::TextView{"foundation: contract violation: dst.size() <= CAPACITY"});
    ::fixy::check::pass(test, "  test_copy_run_refuses_long_run: PASSED\n");
}

// advance_meta_index gives a position past a run only from a position of the
// log.  The index none is no position, and a sum past the width of a counter
// is no position either.  Each refusal runs in a child.
static void test_advance_meta_index_refuses_no_position(::fixy::TestRunnerCtx const& test) {
    constexpr crucible::ValidMetaAppendCount kThree =
        ::fixy::mint_refined<crucible::ValidMetaAppendCount::predicate_type{},
                             crucible::ValidMetaAppendCount::value_type>(3u);
    CRUCIBLE_TEST_ASSERT(test, crucible::advance_meta_index(crucible::MetaIndex{4}, kThree) == crucible::MetaIndex{7});
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)crucible::advance_meta_index(crucible::MetaIndex::none(), kThree); },
        ::fixy::TextView{"foundation: contract violation: start.is_valid()"});
    ::fixy::check::expect_dies(
        test, [] noexcept { (void)crucible::advance_meta_index(crucible::MetaIndex{~uint64_t{0} - 1}, kThree); },
        ::fixy::TextView{"foundation: fatal: strict_add got 18446744073709551614 and 3"});
    ::fixy::check::pass(test, "  test_advance_meta_index_refuses_no_position: PASSED\n");
}

// release_to moves the tail to a floor past it, and a floor at or before the
// tail moves nothing.  A floor past the head ends the process, because the
// producer wrote no record at the slots before it.  The refusal runs in a
// child.
static void test_release_to_moves_the_tail_forward_only(::fixy::TestRunnerCtx const& test) {
    crucible::test::OpenLog log{};
    const ::fixy::FixedArray<TensorMeta, 4> records{
        make_meta(SyntheticAddress{0x5000}), make_meta(SyntheticAddress{0x6000}), make_meta(SyntheticAddress{0x7000}),
        make_meta(SyntheticAddress{0x8000})};
    CRUCIBLE_TEST_ASSERT(test, log.try_append(records.view()) == ::crucible::MetaIndex{0});
    log.release_to(::crucible::MetaIndex{3});
    CRUCIBLE_TEST_ASSERT(test, log.total_released() == ::crucible::MetaIndex{3});
    log.release_to(::crucible::MetaIndex{1});
    CRUCIBLE_TEST_ASSERT(test, log.total_released() == ::crucible::MetaIndex{3} && "a floor before the tail moved it");
    log.release_to(::crucible::MetaIndex{3});
    CRUCIBLE_TEST_ASSERT(test, log.total_released() == ::crucible::MetaIndex{3} && "a floor at the tail moved it");
    log.release_to(::crucible::MetaIndex{4});
    CRUCIBLE_TEST_ASSERT(test, log.total_released() == ::crucible::MetaIndex{4} && log.size().peek() == 0);
    ::fixy::check::expect_dies(
        test, [&log] noexcept { log.release_to(::crucible::MetaIndex{5}); },
        ::fixy::TextView{"foundation: fatal invariant failed: is_at_or_after(produced(consumer), floor)"});
    ::fixy::check::pass(test, "  test_release_to_forward_only:   PASSED\n");
}

// The producer reads its own head relaxed and publishes with a release
// store.  On a total-store-order machine a missing release would still
// appear to work, so this test exists to pin the ordering: a regression
// reddens here rather than only on a weakly ordered target.
//
// The producer encodes the sequence number into data_ptr, shifted clear
// of small sentinel values, so the consumer can check order, loss and
// duplication from the payload alone.
static void test_spsc_concurrent_integrity() {
    constexpr uint32_t N = 100000;
    crucible::test::OpenLog log{};

    ::fixy::Atomic<bool> producer_done{false};
    ::fixy::Atomic<uint32_t, ::fixy::Counter> lost_spin{0};  // diagnostic only

    auto producer = [&] noexcept {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(SyntheticAddress{uint64_t{i + 1} << 16});
            const ::fixy::FixedArray<TensorMeta, 1> record{m};
            auto idx = log.try_append(record.view());
            if (idx.is_valid()) [[likely]] {
                ++i;
            } else {
                // The buffer is full, so back off and let the consumer
                // drain before trying the same index again.
                CRUCIBLE_SPIN_PAUSE;
                static_cast<void>(lost_spin.fetch_add_relaxed(1));
            }
        }
        producer_done.store_release(true);
    };

    auto consumer = [&] noexcept {
        uint32_t next = 0;  // next expected sequence number (0-based)
        while (next < N) {
            const uint32_t avail = log.size().peek();
            if (avail == 0) {
                // The size is re-read after observing the flag.  A
                // producer that appended and then set the flag between
                // the two reads would otherwise be missed.
                if (producer_done.load_acquire() && log.size().peek() == 0) {
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            for (uint32_t k = 0; k < avail; ++k) {
                const TensorMeta m = log.copy_record(MetaIndex{next + k});
                const ExternalDataPtr expected =
                    mint_synthetic_data_ptr(SyntheticAddress{uint64_t{next + k + 1} << 16});
                CRUCIBLE_TEST_ASSERT(kTest, m.data_ptr.value() == expected.value());
                // The remaining fields are identical in every record,
                // so they carry no sequence information.  They are
                // checked anyway: a torn read shows up here.
                CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(m.sizes[0]) == 128);
                CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(m.sizes[1]) == 256);
                CRUCIBLE_TEST_ASSERT(kTest, m.dtype == ScalarType::Float);
                CRUCIBLE_TEST_ASSERT(kTest, m.device_type == DeviceType::CUDA);
            }
            log.release_to(::crucible::MetaIndex{next + avail});
            next += avail;
        }
        CRUCIBLE_TEST_ASSERT(kTest, next == N);  // nothing was lost and nothing repeated
    };

    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the producer and the consumer share the log on purpose",
                                         ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
        .expect("the thread door starts the producer and the consumer");
    ::fixy::check::pass(kTest,
                        "  test_spsc_integrity:            PASSED "
                        "(N={}, producer_spins={})\n",
                        N, lost_spin.load_acquire());
}

// Only the accepting legs of the row-typed facade run here.  The facade
// reads the row and the source of the context of the caller.  A context whose
// row is not empty, or whose source permits an atom, fails to compile, in the
// concept below and in the fixtures.

constexpr ::fixy::HotFgCtx kPure = ::foundation::effects::testing::foreground();

template <typename Ctx>
concept AppendsPure = requires(MetaLog::Producer& log, Ctx const& ctx, ::fixy::View<const TensorMeta> metas) {
    MetaLog::try_append_pure(ctx, log, metas);
};
static_assert(AppendsPure<::fixy::HotFgCtx>);
static_assert(!AppendsPure<::fixy::TestRunnerCtx>);
static_assert(!AppendsPure<::fixy::BgDrainCtx>);
static_assert(!AppendsPure<::foundation::effects::ExecCtx<::foundation::effects::Init, ::foundation::effects::Row<>>>);
static_assert(!AppendsPure<::foundation::effects::ExecCtx<::foundation::effects::Test, ::foundation::effects::Row<>>>);

static void test_try_append_pure() {
    namespace eff = ::fixy;

    {
        crucible::test::OpenLog log{};
        TensorMeta m = make_meta(SyntheticAddress{0xA0});
        const ::fixy::FixedArray<TensorMeta, 1> record{m};
        auto idx = log.try_append_pure(kPure, record.view());
        CRUCIBLE_TEST_ASSERT(kTest, idx.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, idx.raw() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 1);
        const TensorMeta got = log.copy_record(idx);
        CRUCIBLE_TEST_ASSERT(kTest, got.data_ptr.value() == m.data_ptr.value());
    }

    {
        crucible::test::OpenLog log{};
        TensorMeta m1 = make_meta(SyntheticAddress{0xB0});
        TensorMeta m2 = make_meta(SyntheticAddress{0xC0});
        const ::fixy::FixedArray<TensorMeta, 1> first_record{m1};
        const ::fixy::FixedArray<TensorMeta, 1> second_record{m2};
        auto idx1 = log.try_append_pure(kPure, first_record.view());
        auto idx2 = log.try_append_pure(kPure, second_record.view());
        CRUCIBLE_TEST_ASSERT(kTest, idx1.is_valid() && idx2.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, idx1.raw() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, idx2.raw() == 1);
        CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 2);
    }

    // Interleaved calls share one index sequence with no gap and no
    // repeat, which is what shows the facade forwards into the same
    // storage rather than keeping a second one.
    {
        crucible::test::OpenLog log{};
        TensorMeta m = make_meta(SyntheticAddress{0xD0});
        const ::fixy::FixedArray<TensorMeta, 1> record{m};
        auto i0 = log.try_append(record.view());  // index 0
        auto i1 = log.try_append_pure(kPure, record.view());  // index 1
        auto i2 = log.try_append(record.view());  // index 2
        auto i3 = log.try_append_pure(kPure, record.view());  // index 3
        CRUCIBLE_TEST_ASSERT(kTest, i0.raw() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, i1.raw() == 1);
        CRUCIBLE_TEST_ASSERT(kTest, i2.raw() == 2);
        CRUCIBLE_TEST_ASSERT(kTest, i3.raw() == 3);
        CRUCIBLE_TEST_ASSERT(kTest, log.size().peek() == 4);
    }

    // The aliases below are what production callers name, so each is
    // pinned on the side of the purity fence it belongs on.  A change
    // that moved one across would otherwise surface only as a
    // negative-compile fixture flipping.
    static_assert(eff::IsPure<eff::Row<>>);
    static_assert(eff::IsPure<eff::PureRow>);
    static_assert(!eff::IsPure<eff::DivRow>);  // carries Div
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);

    ::fixy::check::pass(kTest, "  test_try_append_pure: PASSED\n");
}

// The interleaving check above is single-threaded, which cannot see a
// hidden atomic or a weaker ordering inside the facade.  This repeats
// the concurrent scenario with the producer driven entirely through the
// row-typed entry point; the consumer side is unchanged.

static void test_try_append_pure_concurrent() {
    constexpr uint32_t N = 50000;
    crucible::test::OpenLog log{};

    ::fixy::Atomic<bool> producer_done{false};
    ::fixy::Atomic<uint32_t, ::fixy::Counter> lost_spin{0};

    auto producer = [&] noexcept {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(SyntheticAddress{uint64_t{i + 1} << 16});
            const ::fixy::FixedArray<TensorMeta, 1> record{m};
            auto idx = log.try_append_pure(kPure, record.view());
            if (idx.is_valid()) [[likely]] {
                ++i;
            } else {
                CRUCIBLE_SPIN_PAUSE;
                static_cast<void>(lost_spin.fetch_add_relaxed(1));
            }
        }
        producer_done.store_release(true);
    };

    auto consumer = [&] noexcept {
        uint32_t next = 0;
        while (next < N) {
            const uint32_t avail = log.size().peek();
            if (avail == 0) {
                if (producer_done.load_acquire() && log.size().peek() == 0) {
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            for (uint32_t k = 0; k < avail; ++k) {
                const TensorMeta m = log.copy_record(MetaIndex{next + k});
                const ExternalDataPtr expected =
                    mint_synthetic_data_ptr(SyntheticAddress{uint64_t{next + k + 1} << 16});
                CRUCIBLE_TEST_ASSERT(kTest, m.data_ptr.value() == expected.value());
                CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(m.sizes[0]) == 128);
                CRUCIBLE_TEST_ASSERT(kTest, ::crucible::raw_tensor_dim(m.sizes[1]) == 256);
                CRUCIBLE_TEST_ASSERT(kTest, m.dtype == ScalarType::Float);
                CRUCIBLE_TEST_ASSERT(kTest, m.device_type == DeviceType::CUDA);
            }
            log.release_to(::crucible::MetaIndex{next + avail});
            next += avail;
        }
        CRUCIBLE_TEST_ASSERT(kTest, next == N);
    };

    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the producer and the consumer share the log on purpose",
                                         ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
        .expect("the thread door starts the producer and the consumer");
    ::fixy::check::pass(kTest,
                        "  test_try_append_pure_concurrent: "
                        "PASSED (N={}, producer_spins={})\n",
                        N, lost_spin.load_acquire());
}

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    // First, while the test process has one thread: the refused copy runs in
    // a child process.
    test_copy_run_refuses_a_run_longer_than_the_buffer(test);
    test_advance_meta_index_refuses_no_position(test);
    test_release_to_moves_the_tail_forward_only(test);
    test_empty_state();
    test_single_append_returns_index_zero();
    test_batch_append_and_monotonic();
    test_tail_advance_frees_capacity();
    test_reset_zeroes_both_pointers();
    test_copy_run_across_the_end();
    test_counters_pass_two_to_the_32();
    test_spsc_concurrent_integrity();
    test_try_append_pure();
    test_try_append_pure_concurrent();
    ::fixy::check::pass(test, "test_meta_log: 12 groups, all passed\n");
    return 0;
}
