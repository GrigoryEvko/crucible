// Races registration against the seal, on the schema table and on the kernel
// table.
//
// In each round, the writers register through one view that the main thread
// minted before the seal.  Each registration takes the write section of the
// table, and a writer whose claim finds the table sealed gets false.  A
// sealer thread seals the table at a different point in each round.  A
// reader thread waits for the seal and then reads the table with no lock, as
// the background thread does.
//
// The rounds of each table run in two chains at the same time, so four
// chains run in all.  Each chain has its own four writers, one sealer and one
// reader.  The threads start one time and run every round of their chain.
// The main thread makes the table and the view of each round before the
// threads start, and it checks each round after the threads end.  The writers
// and the sealer of a round meet at the latch of that round, and the reader
// waits for the seal of that round.  The state of a round that the threads
// share holds only parts that two threads can write: the table, which states
// synchronized{}, the latch, and an atomic cell for each fact that a thread
// gives.  The views stand in an array of their own, which the threads read.
//
// On a loaded host, each start and each wake-up of a thread waits for the
// scheduler.  A round starts no thread and joins no thread, and the four
// chains wait for the scheduler at the same time, not one after the other.
//
// Each round checks these facts:
//   - The count that the sealer reads immediately after the seal is the
//     final count.
//   - The final count is the number of registrations that returned true.
//   - Each registration that returned true is in the table, and each one
//     that returned false is not.
//   - A writer that gets false once gets false for every later write.
//
// Under the tsan preset, a write that lands after the seal races the reader,
// and ThreadSanitizer stops the test.

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

constexpr uint32_t WRITER_COUNT = 4;
constexpr uint32_t WRITES_PER_WRITER = 48;
constexpr uint32_t ROUND_COUNT = 64;

// The rounds of one table run in this number of chains at the same time.
// Chain c runs the rounds c, c + CHAIN_COUNT, c + 2 * CHAIN_COUNT and so on.
constexpr uint32_t CHAIN_COUNT = 2;

// The writers, the sealer and the reader of one chain.
constexpr uint32_t THREAD_COUNT = WRITER_COUNT + 2;

static_assert(ROUND_COUNT % CHAIN_COUNT == 0);

static_assert(WRITER_COUNT * WRITES_PER_WRITER <= crucible::CKERNEL_TABLE_CAP);
static_assert(WRITER_COUNT * WRITES_PER_WRITER <= crucible::SCHEMA_TABLE_CAP);

// A mutable view asks for the context of a Vigil's producer claim.  The race
// uses no Vigil, so it takes that context from the test door.
constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

// The writes of one writer that returned true, one bit for each write: bit k
// for write k.
static_assert(WRITES_PER_WRITER <= 64, "one word of a writer holds a bit for each of its writes");
using WriterMask = uint64_t;

using RoundIndex = ::fixy::Index<ROUND_COUNT>;
using ChainIndex = ::fixy::Index<CHAIN_COUNT>;
using WriterIndex = ::fixy::Index<WRITER_COUNT>;
using WriteIndex = ::fixy::Index<WRITES_PER_WRITER>;

// A distinct, non-zero hash for each write of each writer in each round.
[[nodiscard]] crucible::SchemaHash hash_of(RoundIndex round, WriterIndex writer, WriteIndex index) {
    return crucible::SchemaHash{(uint64_t{round.raw() + 1} << 32) | (uint64_t{writer.raw()} << 16)
                                | uint64_t{index.raw() + 1}};
}

// True when the chain runs the round: chain c runs the rounds c,
// c + CHAIN_COUNT, c + 2 * CHAIN_COUNT and so on.
[[nodiscard]] bool is_round_of(RoundIndex round, ChainIndex chain) noexcept {
    return round.raw() % CHAIN_COUNT == chain.raw();
}

// The number of pause instructions that the sealer waits before the seal.
// The value changes from round to round, so the seal lands before, among and
// after the writes.
[[nodiscard]] uint32_t seal_delay_of(RoundIndex round) { return static_cast<uint32_t>((round.raw() * 97u) % 4096u); }

void pause_for(uint32_t pause_count) {
    for (uint32_t i = 0; i < pause_count; ++i)
        CRUCIBLE_SPIN_PAUSE;
}

// The nanoseconds that a thread spins at the latch of a round before it
// sleeps.  A thread that sleeps starts late when the latch opens, and a late
// writer moves the seal ahead of the writes.  With the spin, each thread
// that has a CPU sees the latch open at once.  On a busy host the spin is
// short, compared with the time to wake a thread that sleeps.
constexpr uint32_t LATCH_SPIN_NANOS = 100000;

using LatchWait = ::fixy::concurrent::SpinThenPark<LATCH_SPIN_NANOS>;

// The latch of one round.  The writers and the sealer count it down, and
// each thread of the round waits until the count is zero: it spins, and then
// it sleeps on the event count.  The last count down wakes each sleeper.
class StartLatch {
public:
    // Counts the latch down, and waits until it opens.
    void arrive_and_wait(::fixy::BgLoadCtx const& ctx) noexcept {
        if (remaining_.fetch_sub_acq_rel(1) == 1) {
            opened_.notify_all(ctx);
        }
        wait(ctx);
    }

    // Waits until the latch opens, with no count down.
    void wait(::fixy::BgLoadCtx const& ctx) noexcept {
        ::fixy::concurrent::wait_for<LatchWait>(ctx, opened_,
                                                [this] noexcept { return remaining_.load_acquire() == 0; });
    }

private:
    ::fixy::Atomic<uint32_t> remaining_{WRITER_COUNT + 1};
    ::fixy::concurrent::EventCount opened_{};
};

// Keeps the writes of one writer in order and checks that a refusal is final.
// Gives the mask of the writes that returned true.
template <typename Register>
[[nodiscard]] WriterMask run_writer(RoundIndex round, WriterIndex writer, Register&& register_one) {
    WriterMask registered = 0;
    bool was_refused_before = false;
    for (WriteIndex const index : ::fixy::IndexRange<WRITES_PER_WRITER>{}) {
        const bool was_registered = register_one(hash_of(round, writer, index));
        CRUCIBLE_TEST_ASSERT(kTest, !(was_refused_before && was_registered));
        was_refused_before = was_refused_before || !was_registered;
        if (was_registered) registered |= WriterMask{1} << index.raw();
    }
    return registered;
}

// True when the mask of a writer holds the write at index.
[[nodiscard]] bool holds_write(WriterMask registered, WriteIndex index) noexcept {
    return ((registered >> index.raw()) & WriterMask{1}) != 0;
}

// The schema table.  The reader checks that the sealed entries are in hash
// order, and it counts them.
struct SchemaRace {
    using Table = crucible::SchemaTable;

    // A hash of the run time comes from outside, so the name goes through the
    // validator.  A round writes 192 names, fewer than the names from outside
    // that a table of SCHEMA_TABLE_CAP names takes.
    [[nodiscard]] static bool register_one(Table& table, Table::MutableView const& view, crucible::SchemaHash hash) {
        const crucible::SanitizedSchemaName name =
            crucible::sanitize_schema_name(
                hash, ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::TextView{"aten::race"}))
                .expect("each hash of the race is not zero");
        return crucible::register_schema_name_in(table, kTest, ::foundation::effects::testing::test().alloc, view, name)
            .is_ok();
    }

    [[nodiscard]] static uint32_t read_sealed(Table const& table, RoundIndex /*round*/) {
        const ::fixy::View<const crucible::SchemaEntry> entries = table.entries();
        for (const auto pair : entries.windows<2>())
            CRUCIBLE_TEST_ASSERT(kTest, pair.at(::fixy::index<0>()).hash < pair.at(::fixy::index<1>()).hash);
        return static_cast<uint32_t>(entries.size());
    }

    [[nodiscard]] static bool is_present(Table const& table, crucible::SchemaHash hash) {
        return table.lookup(hash).is_some();
    }
};

// The kernel table.  The reader classifies each hash of the round, and it
// counts the hashes that the table holds.
struct KernelRace {
    using Table = crucible::CKernelTable;

    // The registration takes the write section, or gives false when the claim
    // finds the table sealed.
    [[nodiscard]] static bool register_one(Table& table, Table::MutableView const& view, crucible::SchemaHash hash) {
        return table.try_enter_write_section(kTest).match(
            [&table, &view, hash](crucible::WriteSection<Table> section) {
                table.register_op(section, view, hash, crucible::CKernelId::GEMM_MM);
                return true;
            },
            [] { return false; });
    }

    [[nodiscard]] static uint32_t read_sealed(Table const& table, RoundIndex round) {
        uint32_t found = 0;
        for (WriterIndex const writer : ::fixy::IndexRange<WRITER_COUNT>{})
            for (WriteIndex const index : ::fixy::IndexRange<WRITES_PER_WRITER>{})
                found += is_present(table, hash_of(round, writer, index)) ? 1u : 0u;
        return found;
    }

    [[nodiscard]] static bool is_present(Table const& table, crucible::SchemaHash hash) {
        return table.classify(hash) == crucible::CKernelId::GEMM_MM;
    }
};

// One round of one race: a fresh table, the latch that starts the writers and
// the sealer together, and what the threads saw.  Each writer stores the mask
// of its writes that returned true in its own cell, the sealer stores the
// count that it read after the seal, and the reader stores the count that it
// read from the sealed table.  So two threads can write each part of a round
// at the same time.
template <typename Table>
struct Round {
    Table table;
    StartLatch start{};
    ::fixy::FixedArray<::fixy::Atomic<WriterMask>, WRITER_COUNT> registered{};
    ::fixy::Atomic<uint32_t> count_at_seal{0};
    ::fixy::Atomic<uint32_t> count_seen_by_reader{0};
};

static_assert(::foundation::transfer::Synchronized<Round<crucible::SchemaTable>>);
static_assert(::foundation::transfer::Synchronized<Round<crucible::CKernelTable>>);

// The ROUND_COUNT rounds of one race.  They are too large for the stack.
template <typename Race>
using Rounds = ::fixy::HeapArray<Round<typename Race::Table>>;

// The view of the table of each round, which the main thread mints before
// the threads start, and which the writers read.
template <typename Race>
using Views = ::fixy::HeapArray<typename Race::Table::MutableView>;

// Each round of a race, built in its place.
template <typename Race>
[[nodiscard]] Rounds<Race> make_rounds() {
    return ::fixy::mint_heap_array_with<Round<typename Race::Table>>(
        ::foundation::effects::testing::test().alloc, ROUND_COUNT,
        [](std::size_t) noexcept { return Round<typename Race::Table>{}; });
}

// The view of the table of each round, minted while each table is open.  The
// views borrow the tables, so the rounds outlive them.
template <typename Race>
[[nodiscard]] Views<Race> make_views(Rounds<Race> const& rounds CRUCIBLE_LIFETIMEBOUND) {
    return ::fixy::mint_heap_array_with<typename Race::Table::MutableView>(
        ::foundation::effects::testing::test().alloc, ROUND_COUNT, [&rounds](std::size_t position) noexcept {
            return rounds.get(position)
                .expect("each round is an element of the rounds")
                ->table.mint_mutable_view(kVigilForeground)
                .expect("a fresh table mints a mutable view");
        });
}
template <typename Race>
void make_views(Rounds<Race> const&&) = delete("the views borrow the tables of the rounds, and temporary "
                                               "rounds end with the full expression");

// Lends the state of a round to body in place.  Six threads share the state,
// and two threads can write each of its parts at the same time.
template <typename Race, typename Body>
void with_round(Rounds<Race>& rounds, RoundIndex round, Body&& body) noexcept {
    rounds.with_element(round.raw(), static_cast<Body&&>(body),
                        [] noexcept { ::fixy::fatal("each round is an element of the rounds"); });
}

// The threads of one chain of one table.  Each thread runs the rounds of the
// chain in order.  A writer or the sealer that ends a round waits at the
// latch of the next round of the chain, so the rounds of one chain do not
// overlap.  The reader can still read a sealed table while the next round
// races, as the background thread can.
//
// The reader sleeps until the latch of its round opens, and it does not
// count the latch down.  The latch opens before each write of the round, so
// only the seal orders the writes before the reads.
template <typename Race>
[[nodiscard]] auto reader_of(Rounds<Race>& rounds, ChainIndex chain) noexcept {
    return [&rounds, chain](::fixy::BgLoadCtx const& ctx) noexcept {
        for (RoundIndex const round : ::fixy::IndexRange<ROUND_COUNT>{}) {
            if (!is_round_of(round, chain)) continue;
            // The reader writes only count_seen_by_reader.
            with_round<Race>(rounds, round, [&ctx, round](Round<typename Race::Table>& state) noexcept {
                state.start.wait(ctx);
                while (!state.table.is_sealed())
                    CRUCIBLE_SPIN_PAUSE;
                state.count_seen_by_reader.store_release(Race::read_sealed(state.table, round));
            });
        }
    };
}

template <typename Race>
[[nodiscard]] auto sealer_of(Rounds<Race>& rounds, ChainIndex chain) noexcept {
    return [&rounds, chain](::fixy::BgLoadCtx const& ctx) noexcept {
        for (RoundIndex const round : ::fixy::IndexRange<ROUND_COUNT>{}) {
            if (!is_round_of(round, chain)) continue;
            // The sealer writes only the seal and count_at_seal.
            with_round<Race>(rounds, round, [&ctx, round](Round<typename Race::Table>& state) noexcept {
                state.start.arrive_and_wait(ctx);
                pause_for(seal_delay_of(round));
                state.table.seal(kTest);
                state.count_at_seal.store_release(state.table.count());
            });
        }
    };
}

template <typename Race>
[[nodiscard]] auto writer_of(Rounds<Race>& rounds, Views<Race> const& views, ChainIndex chain,
                             WriterIndex writer) noexcept {
    return [&rounds, &views, chain, writer](::fixy::BgLoadCtx const& ctx) noexcept {
        for (RoundIndex const round : ::fixy::IndexRange<ROUND_COUNT>{}) {
            if (!is_round_of(round, chain)) continue;
            // A writer writes only its own mask and the table, and it reads
            // the view of the round.
            auto const& view_read = *views.get(round.raw()).expect("each round has a view");
            with_round<Race>(
                rounds, round, [&ctx, &view_read, round, writer](Round<typename Race::Table>& state) noexcept {
                    state.start.arrive_and_wait(ctx);
                    auto& table = state.table;
                    const WriterMask registered =
                        run_writer(round, writer, [&table, &view_read](crucible::SchemaHash hash) {
                            return Race::register_one(table, view_read, hash);
                        });
                    state.registered.with_element(writer, [registered](::fixy::Atomic<WriterMask>& cell) noexcept {
                        cell.store_release(registered);
                    });
                });
        }
    };
}

// The six bodies of one chain of one table: the reader, the sealer and the
// four writers.
template <typename Race>
[[nodiscard]] auto chain_of(Rounds<Race>& rounds, Views<Race> const& views, ChainIndex chain) noexcept {
    return ::fixy::Record{reader_of<Race>(rounds, chain),
                          sealer_of<Race>(rounds, chain),
                          writer_of<Race>(rounds, views, chain, ::fixy::index<0>()),
                          writer_of<Race>(rounds, views, chain, ::fixy::index<1>()),
                          writer_of<Race>(rounds, views, chain, ::fixy::index<2>()),
                          writer_of<Race>(rounds, views, chain, ::fixy::index<3>())};
}

// The rounds of one table in which the seal refused a write, and the rounds in
// which a write went in.
struct RaceCounts {
    uint32_t with_a_refusal = 0;
    uint32_t with_a_write = 0;
};

// Checks each round of one table after its threads end.
template <typename Race>
[[nodiscard]] RaceCounts check_rounds(Rounds<Race> const& rounds) {
    RaceCounts counts{};
    for (RoundIndex const round : ::fixy::IndexRange<ROUND_COUNT>{}) {
        auto const& state = *rounds.get(round.raw()).expect("each round is an element of the rounds");
        const uint32_t final_count = state.table.count();
        CRUCIBLE_TEST_ASSERT(kTest, state.count_at_seal.load_acquire() == final_count);
        CRUCIBLE_TEST_ASSERT(kTest, state.count_seen_by_reader.load_acquire() == final_count);
        uint32_t registered_count = 0;
        for (WriterIndex const writer : state.registered.indices()) {
            const WriterMask registered = state.registered.at(writer).load_acquire();
            registered_count += static_cast<uint32_t>(::fixy::popcount(registered));
            for (WriteIndex const index : ::fixy::IndexRange<WRITES_PER_WRITER>{}) {
                const bool is_present = Race::is_present(state.table, hash_of(round, writer, index));
                CRUCIBLE_TEST_ASSERT(kTest, is_present == holds_write(registered, index));
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, registered_count == final_count);
        const uint32_t refused_count = WRITER_COUNT * WRITES_PER_WRITER - final_count;
        counts.with_a_refusal += refused_count > 0 ? 1u : 0u;
        counts.with_a_write += refused_count < WRITER_COUNT * WRITES_PER_WRITER ? 1u : 0u;
    }
    return counts;
}

}  // namespace

int main() {
    // The main thread mints each view, so the tables are made before any
    // thread starts.
    Rounds<SchemaRace> schema_rounds = make_rounds<SchemaRace>();
    Rounds<KernelRace> kernel_rounds = make_rounds<KernelRace>();
    Views<SchemaRace> const schema_views = make_views<SchemaRace>(schema_rounds);
    Views<KernelRace> const kernel_views = make_views<KernelRace>(kernel_rounds);

    // The bodies of the four chains, each of THREAD_COUNT threads.  The door
    // starts each thread before the first round, and it joins each thread
    // before it returns.
    static_assert(CHAIN_COUNT == 2 && WRITER_COUNT == 4 && THREAD_COUNT == 6,
                  "the bodies below name two chains of each table, each of a reader, a sealer and four writers");
    auto [schema_reader_0, schema_sealer_0, schema_writer_00, schema_writer_01, schema_writer_02, schema_writer_03] =
        chain_of<SchemaRace>(schema_rounds, schema_views, ::fixy::index<0>());
    auto [schema_reader_1, schema_sealer_1, schema_writer_10, schema_writer_11, schema_writer_12, schema_writer_13] =
        chain_of<SchemaRace>(schema_rounds, schema_views, ::fixy::index<1>());
    auto [kernel_reader_0, kernel_sealer_0, kernel_writer_00, kernel_writer_01, kernel_writer_02, kernel_writer_03] =
        chain_of<KernelRace>(kernel_rounds, kernel_views, ::fixy::index<0>());
    auto [kernel_reader_1, kernel_sealer_1, kernel_writer_10, kernel_writer_11, kernel_writer_12, kernel_writer_13] =
        chain_of<KernelRace>(kernel_rounds, kernel_views, ::fixy::index<1>());
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(
        ctx, kTest, "the race of a registration and the seal shares the rounds of each table on purpose",
        ::fixy::thread::ThreadOptions{}, std::move(schema_reader_0), std::move(schema_sealer_0),
        std::move(schema_writer_00), std::move(schema_writer_01), std::move(schema_writer_02),
        std::move(schema_writer_03), std::move(schema_reader_1), std::move(schema_sealer_1),
        std::move(schema_writer_10), std::move(schema_writer_11), std::move(schema_writer_12),
        std::move(schema_writer_13), std::move(kernel_reader_0), std::move(kernel_sealer_0),
        std::move(kernel_writer_00), std::move(kernel_writer_01), std::move(kernel_writer_02),
        std::move(kernel_writer_03), std::move(kernel_reader_1), std::move(kernel_sealer_1),
        std::move(kernel_writer_10), std::move(kernel_writer_11), std::move(kernel_writer_12),
        std::move(kernel_writer_13))
        .expect("the thread door starts each thread of the four chains");

    // The split between refused and admitted writes depends on the
    // scheduler, so the test reports it and does not assert it.
    const RaceCounts schema_counts = check_rounds<SchemaRace>(schema_rounds);
    const RaceCounts kernel_counts = check_rounds<KernelRace>(kernel_rounds);
    ::fixy::report(::fixy::Sink::Out,
                   "test_registration_seal_race: {} races, {} with a refused write, {} with an admitted write\n",
                   2 * ROUND_COUNT, schema_counts.with_a_refusal + kernel_counts.with_a_refusal,
                   schema_counts.with_a_write + kernel_counts.with_a_write);
    return 0;
}
