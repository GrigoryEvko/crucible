// The table of the handles of the Vigils of the vessel (vessel/torch/vigil_handles.h).
//
//   full       a table of two slots takes two Vigils, and a third gives full
//   stale      a zero, a forged and an old word give stale, and an op of such a
//              word finds no Vigil
//   generation the end of a Vigil gives its slot the next generation, so the old
//              word stays stale after a new Vigil takes the slot
//   thread     another thread gets wrong_thread while this thread holds the
//              slot, and it holds the slot after a release
//   busy       an open nest of the Vigil refuses the end and the release
//   brand      a create on a second thread while this thread holds a live
//              Vigil gives wrong_thread, and the Vigil of the create ends
//   op claim   the first op of a free slot claims it, and another thread then
//              finds no Vigil
//   race       one thread ends the Vigil while a second thread claims it,
//              reads it and releases it in a loop.  ThreadSanitizer runs the
//              case too
//
// Each Vigil comes from mint_box, as crucible_create makes it.

#include "../vessel/torch/vigil_handles.h"

#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using crucible::Vigil;
using crucible::vessel::HandleRefusal;
using crucible::vessel::HeldVigil;
using crucible::vessel::VigilHandle;
using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// A table of two slots, so that a test reaches full with two Vigils.
using Table = crucible::vessel::VigilHandleTable<2>;

// The word of the slot and the generation.
[[nodiscard]] constexpr VigilHandle word_of(std::uint32_t slot, std::uint32_t generation) noexcept {
    return VigilHandle{(std::uint64_t{generation} << crucible::vessel::kGenerationShift) | slot};
}

static_assert(crucible::vessel::next_generation(1U) == 2U);
static_assert(crucible::vessel::next_generation(::fixy::max_of<std::uint32_t>) == 1U,
              "a wrap of the generation skips 0, which names no Vigil");

// A new Vigil on the heap.
[[nodiscard]] ::fixy::Box<Vigil> make_vigil() noexcept {
    return ::fixy::mint_box_built<Vigil>(::foundation::effects::testing::test().alloc, []() noexcept {
        return crucible::mint_vigil(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, Vigil::Config{});
    });
}

// The handle of a new Vigil that the table admits.
[[nodiscard]] VigilHandle admit(Table& table) noexcept {
    return table.admit(make_vigil()).expect("the table has a free slot and the thread holds no other Vigil");
}

// The refusal of a hold of the handle, or no value when the table gives the
// proof.
[[nodiscard]] ::fixy::Option<HandleRefusal> refusal_of_hold(Table& table, VigilHandle handle) noexcept {
    const ::fixy::Result<HeldVigil, HandleRefusal> held = table.mint_held_vigil(handle);
    return held.match([](HeldVigil const&) noexcept { return ::fixy::Option<HandleRefusal>{::fixy::none}; },
                      [](HandleRefusal refused) noexcept { return ::fixy::Option<HandleRefusal>::some(refused); });
}

// True when a hold of the handle gives the refusal.
[[nodiscard]] bool is_refused_as(Table& table, VigilHandle handle, HandleRefusal expected) noexcept {
    return refusal_of_hold(table, handle)
        .match([expected](HandleRefusal refused) noexcept { return refused == expected; },
               [] noexcept { return false; });
}

// True when the calling thread holds the Vigil of the handle.
[[nodiscard]] bool is_held(Table& table, VigilHandle handle) noexcept {
    return refusal_of_hold(table, handle).is_none();
}

// The compiled bit of the Vigil of the handle, read through the proof of the
// calling thread, or no value when the thread gets no proof.
[[nodiscard]] ::fixy::Option<bool> compiled_of(Table& table, VigilHandle handle) noexcept {
    const ::fixy::Result<HeldVigil, HandleRefusal> held = table.mint_held_vigil(handle);
    return held.match(
        [&table](HeldVigil const& proof) noexcept {
            return ::fixy::Option<bool>::some(table.vigil_of(proof)->is_compiled());
        },
        [](HandleRefusal) noexcept { return ::fixy::Option<bool>{::fixy::none}; });
}

// The refusal of the end of the Vigil of the handle, or no value when the
// Vigil ended.
[[nodiscard]] ::fixy::Option<HandleRefusal> refusal_of_end(Table& table, VigilHandle handle) noexcept {
    ::fixy::Result<HeldVigil, HandleRefusal> held = table.mint_held_vigil(handle);
    return std::move(held).match(
        [&table](HeldVigil proof) noexcept {
            return table.withdraw(std::move(proof))
                .match(
                    [](::fixy::Box<Vigil> ended) noexcept {
                        static_cast<void>(ended);
                        return ::fixy::Option<HandleRefusal>{::fixy::none};
                    },
                    [](HandleRefusal refused) noexcept { return ::fixy::Option<HandleRefusal>::some(refused); });
        },
        [](HandleRefusal refused) noexcept { return ::fixy::Option<HandleRefusal>::some(refused); });
}

// Ends the Vigil of the handle, which the calling thread can hold.
void end_vigil(Table& table, VigilHandle handle) noexcept {
    CRUCIBLE_FATAL_INVARIANT(refusal_of_end(table, handle).is_none());
}

// The refusal of a release of the handle, or no value when the thread gave
// the handle up.
[[nodiscard]] ::fixy::Option<HandleRefusal> refusal_of_release(Table& table, VigilHandle handle) noexcept {
    ::fixy::Result<HeldVigil, HandleRefusal> held = table.mint_held_vigil(handle);
    return std::move(held).match(
        [&table](HeldVigil proof) noexcept {
            return table.release(std::move(proof))
                .match([](::fixy::Unit) noexcept { return ::fixy::Option<HandleRefusal>{::fixy::none}; },
                       [](HandleRefusal refused) noexcept { return ::fixy::Option<HandleRefusal>::some(refused); });
        },
        [](HandleRefusal refused) noexcept { return ::fixy::Option<HandleRefusal>::some(refused); });
}

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body))
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

void test_full(TestRunnerCtx const& test) {
    Table table{};
    CRUCIBLE_FATAL_INVARIANT(table.has_free_slot());
    const VigilHandle first = admit(table);
    const VigilHandle second = admit(table);
    CRUCIBLE_FATAL_INVARIANT(first == word_of(0, 1));
    CRUCIBLE_FATAL_INVARIANT(second == word_of(1, 1));
    CRUCIBLE_FATAL_INVARIANT(!table.has_free_slot());
    const bool is_full = table.admit(make_vigil())
                             .match([](VigilHandle) noexcept { return false; },
                                    [](HandleRefusal refused) noexcept { return refused == HandleRefusal::full; });
    CRUCIBLE_FATAL_INVARIANT(is_full);
    CRUCIBLE_FATAL_INVARIANT(is_held(table, first) && is_held(table, second));
    end_vigil(table, first);
    end_vigil(table, second);
    pass(test, "  test_full: PASSED\n");
}

void test_stale_and_forged_words(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle live = admit(table);
    // The word 0, the next generation, a bit between the index and the
    // generation, a slot past the table, a free slot, generation 0 of slot 1,
    // and each bit.
    const ::fixy::FixedArray<VigilHandle, 7> forged{
        VigilHandle{0},
        word_of(0, 2),
        word_of(5, 1),
        VigilHandle{live.raw() | (std::uint64_t{1} << 8)},
        word_of(1, 1),
        VigilHandle{1},
        VigilHandle{~std::uint64_t{0}},
    };
    for (const VigilHandle word : forged) {
        CRUCIBLE_FATAL_INVARIANT(is_refused_as(table, word, HandleRefusal::stale));
        CRUCIBLE_FATAL_INVARIANT(table.find_recording_vigil(word).is_none());
        CRUCIBLE_FATAL_INVARIANT(refusal_of_end(table, word).is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(is_held(table, live));
    CRUCIBLE_FATAL_INVARIANT(table.find_recording_vigil(live).is_some());
    end_vigil(table, live);
    pass(test, "  test_stale_and_forged_words: PASSED\n");
}

void test_generation(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle old_word = admit(table);
    end_vigil(table, old_word);
    CRUCIBLE_FATAL_INVARIANT(is_refused_as(table, old_word, HandleRefusal::stale));
    const bool is_second_end_stale =
        refusal_of_end(table, old_word)
            .match([](HandleRefusal refused) noexcept { return refused == HandleRefusal::stale; },
                   [] noexcept { return false; });
    CRUCIBLE_FATAL_INVARIANT(is_second_end_stale);
    const VigilHandle new_word = admit(table);
    CRUCIBLE_FATAL_INVARIANT(new_word == word_of(0, 2));
    CRUCIBLE_FATAL_INVARIANT(is_refused_as(table, old_word, HandleRefusal::stale));
    CRUCIBLE_FATAL_INVARIANT(table.find_recording_vigil(old_word).is_none());
    CRUCIBLE_FATAL_INVARIANT(is_held(table, new_word));
    end_vigil(table, new_word);
    pass(test, "  test_generation: PASSED\n");
}

void test_wrong_thread_and_release(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle handle = admit(table);
    const bool is_refused_beside = on_other_thread([&table, handle] noexcept {
        return is_refused_as(table, handle, HandleRefusal::wrong_thread) && table.find_recording_vigil(handle).is_none()
            && refusal_of_end(table, handle).is_some() && refusal_of_release(table, handle).is_some();
    });
    CRUCIBLE_FATAL_INVARIANT(is_refused_beside);
    CRUCIBLE_FATAL_INVARIANT(is_held(table, handle));

    CRUCIBLE_FATAL_INVARIANT(refusal_of_release(table, handle).is_none());
    const bool is_taken_beside = on_other_thread([&table, handle] noexcept {
        const bool is_taken = is_held(table, handle) && compiled_of(table, handle).is_some();
        return is_taken && refusal_of_release(table, handle).is_none();
    });
    CRUCIBLE_FATAL_INVARIANT(is_taken_beside);
    CRUCIBLE_FATAL_INVARIANT(is_held(table, handle));
    end_vigil(table, handle);
    pass(test, "  test_wrong_thread_and_release: PASSED\n");
}

void test_busy(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle handle = admit(table);
    {
        Vigil& vigil = *table.find_recording_vigil(handle).expect("this thread holds the Vigil");
        const crucible::VigilFgCtx fg = vigil.mint_producer_context();
        const auto is_busy = [](::fixy::Option<HandleRefusal> refused) noexcept {
            return refused.match([](HandleRefusal reason) noexcept { return reason == HandleRefusal::busy; },
                                 [] noexcept { return false; });
        };
        vigil.with_op_nest(fg, [&](crucible::OpDepth) noexcept {
            CRUCIBLE_FATAL_INVARIANT(is_busy(refusal_of_end(table, handle)));
            CRUCIBLE_FATAL_INVARIANT(is_busy(refusal_of_release(table, handle)));
            CRUCIBLE_FATAL_INVARIANT(is_held(table, handle));
        });
    }
    end_vigil(table, handle);
    pass(test, "  test_busy: PASSED\n");
}

void test_brand_of_another_thread(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle handle = admit(table);
    const bool is_create_refused = on_other_thread([&table] noexcept {
        return table.admit(make_vigil())
            .match([](VigilHandle) noexcept { return false; },
                   [](HandleRefusal refused) noexcept { return refused == HandleRefusal::wrong_thread; });
    });
    CRUCIBLE_FATAL_INVARIANT(is_create_refused);
    CRUCIBLE_FATAL_INVARIANT(table.has_free_slot());

    // After the release, no thread holds a live claim of the brand, so the
    // other thread makes a Vigil, and it ends it.
    CRUCIBLE_FATAL_INVARIANT(refusal_of_release(table, handle).is_none());
    const bool is_create_taken = on_other_thread([&table] noexcept {
        return table.admit(make_vigil())
            .match([&table](VigilHandle made) noexcept { return refusal_of_end(table, made).is_none(); },
                   [](HandleRefusal) noexcept { return false; });
    });
    CRUCIBLE_FATAL_INVARIANT(is_create_taken);
    end_vigil(table, handle);
    pass(test, "  test_brand_of_another_thread: PASSED\n");
}

void test_op_claims_a_free_slot(TestRunnerCtx const& test) {
    Table table{};
    const VigilHandle handle = admit(table);
    CRUCIBLE_FATAL_INVARIANT(refusal_of_release(table, handle).is_none());
    CRUCIBLE_FATAL_INVARIANT(table.find_recording_vigil(handle).is_some());
    const bool is_refused_beside = on_other_thread([&table, handle] noexcept {
        return table.find_recording_vigil(handle).is_none()
            && is_refused_as(table, handle, HandleRefusal::wrong_thread);
    });
    CRUCIBLE_FATAL_INVARIANT(is_refused_beside);
    end_vigil(table, handle);
    pass(test, "  test_op_claims_a_free_slot: PASSED\n");
}

// The counts of the second thread of the race: the claims that read the
// Vigil, and the claims that another thread refused.
struct RaceCounts {
    std::uint32_t reads = 0;
    std::uint32_t refusals = 0;
};

// The iterations of the race.  Each one makes a Vigil and ends it.
constexpr std::uint32_t kRaceIterations = 32;

// The reads of the second thread before the first thread tries the end, so
// that the end comes while the second thread claims and reads in its loop.
constexpr std::uint32_t kReadsBeforeEnd = 16;

// The loop of the second thread of the race: a claim of the Vigil, a read of
// it and a release, until the handle is stale.  Each read counts in
// reads_seen, which the first thread waits on.
[[nodiscard]] RaceCounts read_until_stale(Table& table, ::fixy::Atomic<std::uint32_t>& reads_seen,
                                          VigilHandle handle) noexcept {
    RaceCounts counts{};
    for (;;) {
        const ::fixy::Result<HeldVigil, HandleRefusal> held = table.mint_held_vigil(handle);
        const bool is_stale = held.match(
            [&table, &counts](HeldVigil const& proof) noexcept {
                const Vigil& vigil = *table.vigil_of(proof);
                if (!vigil.is_compiled() && vigil.diverged_count() == 0) ++counts.reads;
                return false;
            },
            [&counts](HandleRefusal refused) noexcept {
                if (refused == HandleRefusal::wrong_thread) ++counts.refusals;
                return refused == HandleRefusal::stale;
            });
        if (is_stale) return counts;
        static_cast<void>(reads_seen.fetch_add_acq_rel(1U));
        static_cast<void>(refusal_of_release(table, handle));
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The loop of the first thread of the race: after kReadsBeforeEnd reads of
// the second thread, the end of the Vigil, until a claim of the slot ends it.
// Gives the refusals of the end.
[[nodiscard]] std::uint32_t end_after_reads(Table& table, ::fixy::Atomic<std::uint32_t> const& reads_seen,
                                            VigilHandle handle) noexcept {
    std::uint32_t refusals = 0;
    while (reads_seen.load_acquire() < kReadsBeforeEnd)
        CRUCIBLE_SPIN_PAUSE;
    for (;;) {
        if (refusal_of_end(table, handle).is_none()) return refusals;
        ++refusals;
        CRUCIBLE_SPIN_PAUSE;
    }
}

void test_end_against_queries(TestRunnerCtx const& test) {
    Table table{};
    RaceCounts total{};
    std::uint32_t end_refusals = 0;
    for (std::uint32_t iteration = 0; iteration < kRaceIterations; ++iteration) {
        const VigilHandle handle = admit(table);
        CRUCIBLE_FATAL_INVARIANT(refusal_of_release(table, handle).is_none());
        ::fixy::Atomic<std::uint32_t> reads_seen{0U};
        ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
        auto results =
            ::fixy::thread::run_beside(
                bg, ::fixy::thread::ThreadOptions{},
                [&table, &reads_seen, handle] noexcept { return read_until_stale(table, reads_seen, handle); },
                [&table, &reads_seen, handle] noexcept { return end_after_reads(table, reads_seen, handle); })
                .expect("the host starts the thread of the test");
        const RaceCounts counts = results.at(::fixy::index<0>());
        total.reads += counts.reads;
        total.refusals += counts.refusals;
        end_refusals += results.at(::fixy::index<1>());
        CRUCIBLE_FATAL_INVARIANT(counts.reads >= kReadsBeforeEnd);
        CRUCIBLE_FATAL_INVARIANT(is_refused_as(table, handle, HandleRefusal::stale));
    }
    ::fixy::report(::fixy::Sink::Out,
                   "  test_end_against_queries: {} reads and {} refusals of the reader, {} refusals of the end, in {} "
                   "iterations\n",
                   total.reads, total.refusals, end_refusals, kRaceIterations);
    pass(test, "  test_end_against_queries: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_full(test);
    test_stale_and_forged_words(test);
    test_generation(test);
    test_wrong_thread_and_release(test);
    test_busy(test);
    test_brand_of_another_thread(test);
    test_op_claims_a_free_slot(test);
    test_end_against_queries(test);
    return 0;
}
