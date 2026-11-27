// Tests of the Sealed of the Ref family: a thread beside its owner reads a
// sealed list while the owner reads it too, a Box of a Sealed moves and keeps
// the address of its object, and each element of a sealed list ends one time.
// ThreadSanitizer runs the same binary.

#include <foundation/core/Sealed.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Ref.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace eff = ::foundation::effects;

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

constexpr eff::Alloc kAlloc = eff::testing::test().alloc;

constexpr std::uint64_t kCount = 5000;
constexpr std::uint64_t kSum = kCount * (kCount - 1) / 2;

// A list of the numbers below kCount.
[[nodiscard]] ::fixy::SegmentList<std::uint64_t> make_list() noexcept {
    ::fixy::SegmentList<std::uint64_t> list;
    for (std::uint64_t value = 0; value != kCount; ++value) {
        list.append(kAlloc, value);
    }
    return list;
}

// The sum of a list through get, and the sum through the loop.
[[nodiscard]] std::uint64_t sum_by_get(::fixy::SegmentList<std::uint64_t> const& list) noexcept {
    std::uint64_t total = 0;
    for (std::size_t position = 0; position != list.size(); ++position) {
        total += *list.get(position).expect("the position is below the count");
    }
    return total;
}

[[nodiscard]] std::uint64_t sum_by_loop(::fixy::SegmentList<std::uint64_t> const& list) noexcept {
    std::uint64_t total = 0;
    for (std::uint64_t const& value : list) {
        total += value;
    }
    return total;
}

// A thread reads the sealed list through a reference, and the continuation
// reads it on the calling thread at the same time.
void test_a_thread_reads_beside_the_owner() {
    ::fixy::Sealed const sealed{make_list()};
    auto reader = [&sealed]() noexcept { return sum_by_get(*sealed); };
    auto const sums =
        ::fixy::thread::run_beside(::fixy::BgLoadCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                   std::move(reader), [&sealed]() noexcept { return sum_by_loop(*sealed); })
            .expect("the test starts one thread");
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<0>()) == kSum && sums.at(::fixy::index<1>()) == kSum);
}

// Each of two threads keeps a Ref of an element while the other one reads each
// element of the sealed list, and reads through its Ref after that.
void test_a_ref_lives_over_the_reads_of_the_other_thread() {
    ::fixy::Sealed const sealed{make_list()};
    ::foundation::core::Atomic<bool> has_thread_read{false};
    ::foundation::core::Atomic<bool> has_owner_read{false};
    auto thread = [&sealed, &has_thread_read, &has_owner_read]() noexcept {
        auto const kept = sealed->get(17).expect("the list holds 5000 elements");
        std::uint64_t const total = sum_by_loop(*sealed);
        has_thread_read.store_release(true);
        while (!has_owner_read.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        return total + *kept;
    };
    auto owner = [&sealed, &has_thread_read, &has_owner_read]() noexcept {
        auto const kept = sealed->last().expect("the list holds 5000 elements");
        while (!has_thread_read.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        std::uint64_t const total = sum_by_get(*sealed);
        has_owner_read.store_release(true);
        return total + *kept;
    };
    auto const sums = ::fixy::thread::run_beside(::fixy::BgLoadCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                                 std::move(thread), std::move(owner))
                          .expect("the test starts one thread");
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<0>()) == kSum + 17);
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<1>()) == kSum + kCount - 1);
}

// The threads of the counted door read one sealed list, and the continuation
// reads it too.
void test_many_threads_read_beside_the_owner() {
    ::fixy::Sealed const sealed{make_list()};
    ::foundation::core::Atomic<std::uint64_t> total{0};
    auto reader = [&sealed, &total](::fixy::thread::BesideIndex /*index*/) noexcept {
        static_cast<void>(total.fetch_add_acq_rel(sum_by_loop(*sealed)));
    };
    auto const threads = ::fixy::thread::BesideCount<3>::check(3).expect("3 is at most 3");
    std::uint64_t const own =
        ::fixy::thread::run_beside_each(::fixy::BgLoadCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, threads,
                                        reader, [&sealed]() noexcept { return sum_by_get(*sealed); })
            .expect("the test starts three threads");
    CRUCIBLE_TEST_ASSERT(kTest, own == kSum && total.load_acquire() == 3 * kSum);
}

// A Box of a Sealed moves, and the sealed object keeps its address.  The read
// frames of the Box read the address and the elements.
void test_a_box_of_a_sealed_list_moves() {
    using SealedList = ::fixy::Sealed<::fixy::SegmentList<std::uint64_t>>;
    auto const address_in = [](::fixy::Box<SealedList> const& held) noexcept {
        return ::foundation::core::with_view(held, [](SealedList const& sealed) noexcept {
            return ::foundation::core::address_of(::foundation::core::to(*sealed));
        });
    };
    ::fixy::Box<SealedList> box = ::fixy::mint_box<SealedList>(kAlloc, make_list());
    ::foundation::core::Address const before = address_in(box);
    std::uint64_t const element = ::foundation::core::with_view(box, [](SealedList const& sealed) noexcept {
        return *(*sealed).get(4999).expect("the list holds 5000 elements");
    });
    ::fixy::Box<SealedList> const moved{std::move(box)};
    std::uint64_t const sum =
        ::foundation::core::with_view(moved, [](SealedList const& sealed) noexcept { return sum_by_get(*sealed); });
    CRUCIBLE_TEST_ASSERT(kTest, element == 4999 && sum == kSum && address_in(moved) == before);
}

// An element that counts its ends through a Ref to an atomic counter, so two
// threads can read it.  The brand of the Ref is a template argument, so no
// fresh brand leaves its scope.  A moved-from element is not live.
template <class CounterRef>
struct Counted final {
    CounterRef ends;
    int label = 0;
    bool is_live = true;
    Counted(CounterRef counter, int from) noexcept : ends{counter}, label{from} {}
    Counted(Counted&& other) noexcept : ends{other.ends}, label{other.label} { other.is_live = false; }
    Counted(Counted const&) = delete;
    Counted& operator=(Counted&&) = delete;
    Counted& operator=(Counted const&) = delete;
    ~Counted() {
        if (is_live) {
            static_cast<void>(ends->fetch_add_acq_rel(1));
        }
    }
};

// A seal moves the list and ends none of its elements, and the end of the
// Sealed ends each element one time.
void test_each_element_ends_once() {
    ::foundation::core::Atomic<int> ends{0};
    auto const counter = ::fixy::to(ends);
    using Element = Counted<std::remove_const_t<decltype(counter)>>;
    {
        ::fixy::SegmentList<Element> list;
        for (int label = 0; label != 300; ++label) {
            list.append(kAlloc, Element{counter, label});
        }
        ::fixy::Sealed const sealed{std::move(list)};
        int expected = 0;
        for (Element const& element : *sealed) {
            CRUCIBLE_TEST_ASSERT(kTest, element.is_live && element.label == expected);
            ++expected;
        }
        CRUCIBLE_TEST_ASSERT(kTest, expected == 300 && sealed->size() == 300 && ends.load_acquire() == 0);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends.load_acquire() == 300);
}

}  // namespace

int main() {
    test_a_thread_reads_beside_the_owner();
    test_a_ref_lives_over_the_reads_of_the_other_thread();
    test_many_threads_read_beside_the_owner();
    test_a_box_of_a_sealed_list_moves();
    test_each_element_ends_once();
    return 0;
}
