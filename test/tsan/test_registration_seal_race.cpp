// Races registration against the seal, on the schema table and on the kernel
// table.
//
// In each round, the writers register through one view that the main thread
// minted before the seal.  A sealer thread seals the table at a different
// point in each round.  A reader thread waits for the seal and then reads the
// table with no lock, as the background thread does.
//
// The rounds of each table run in two chains at the same time, so four
// chains run in all.  Each chain has its own four writers, one sealer and one
// reader.  The threads start one time and run every round of their chain.
// The main thread makes the table of each round before the threads start,
// and it checks each round after the threads end.  The writers and the
// sealer of a round meet at the latch of that round, and the reader waits
// for the seal of that round.
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

using WriterFlags = ::fixy::FixedArray<bool, WRITES_PER_WRITER>;
using RegisteredFlags = ::fixy::FixedArray<WriterFlags, WRITER_COUNT>;

// A distinct, non-zero hash for each write of each writer in each round.
[[nodiscard]] crucible::SchemaHash hash_of(uint32_t round, uint32_t writer, uint32_t index) {
    return crucible::SchemaHash{(uint64_t{round + 1} << 32) | (uint64_t{writer} << 16) | uint64_t{index + 1}};
}

// The number of pause instructions that the sealer waits before the seal.
// The value changes from round to round, so the seal lands before, among and
// after the writes.
[[nodiscard]] uint32_t seal_delay_of(uint32_t round) { return (round * 97u) % 4096u; }

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
template <typename Register>
void run_writer(uint32_t round, uint32_t writer, Register&& register_one, WriterFlags& registered) {
    bool was_refused_before = false;
    for (::fixy::Index<WRITES_PER_WRITER> const index : registered.indices()) {
        const bool was_registered = register_one(hash_of(round, writer, static_cast<uint32_t>(index.raw())));
        CRUCIBLE_TEST_ASSERT(kTest, !(was_refused_before && was_registered));
        was_refused_before = was_refused_before || !was_registered;
        registered.at(index) = was_registered;
    }
}

[[nodiscard]] uint32_t count_true(RegisteredFlags const& registered) {
    uint32_t total = 0;
    for (auto const& per_writer : registered)
        for (const bool was_registered : per_writer)
            total += was_registered ? 1u : 0u;
    return total;
}

// The schema table.  The reader checks that the sealed entries are in hash
// order, and it counts them.
struct SchemaRace {
    using Table = crucible::SchemaTable;

    [[nodiscard]] static bool register_one(Table& table, Table::MutableView const& view, crucible::SchemaHash hash) {
        return table
            .register_name(kTest, ::foundation::effects::testing::test().alloc, view, hash,
                           ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{"aten::race"}))
            .is_ok();
    }

    [[nodiscard]] static uint32_t read_sealed(Table const& table, uint32_t /*round*/) {
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

    [[nodiscard]] static bool register_one(Table& table, Table::MutableView const& view, crucible::SchemaHash hash) {
        return table.register_op(kTest, view, hash, crucible::CKernelId::GEMM_MM);
    }

    [[nodiscard]] static uint32_t read_sealed(Table const& table, uint32_t round) {
        uint32_t found = 0;
        for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer)
            for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index)
                found += is_present(table, hash_of(round, writer, index)) ? 1u : 0u;
        return found;
    }

    [[nodiscard]] static bool is_present(Table const& table, crucible::SchemaHash hash) {
        return table.classify(hash) == crucible::CKernelId::GEMM_MM;
    }
};

// One round of one race: a fresh table, the view that the main thread minted
// before the seal, the latch that starts the writers and the sealer
// together, and what the threads saw.
template <typename Table>
struct Round {
    Table table;
    typename Table::MutableView view =
        table.mint_mutable_view(kVigilForeground).expect("a fresh table mints a mutable view");
    StartLatch start{};
    RegisteredFlags registered{};
    uint32_t count_at_seal = 0;
    uint32_t count_seen_by_reader = 0;
};

template <typename Race>
using Rounds = ::fixy::FixedArray<Round<typename Race::Table>, ROUND_COUNT>;

// The state of the round with the number round, which is below ROUND_COUNT.
template <typename Race>
[[nodiscard]] Round<typename Race::Table>& round_of(Rounds<Race>& rounds, uint32_t round) noexcept {
    return rounds.at(::fixy::Index<ROUND_COUNT>::check(round).expect("a chain runs only the rounds below the count"));
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
[[nodiscard]] auto reader_of(Rounds<Race>& rounds, uint32_t chain) noexcept {
    return [&rounds, chain](::fixy::BgLoadCtx const& ctx) noexcept {
        for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
            auto& state = round_of<Race>(rounds, round);
            state.start.wait(ctx);
            while (!state.table.is_sealed())
                CRUCIBLE_SPIN_PAUSE;
            state.count_seen_by_reader = Race::read_sealed(state.table, round);
        }
    };
}

template <typename Race>
[[nodiscard]] auto sealer_of(Rounds<Race>& rounds, uint32_t chain) noexcept {
    return [&rounds, chain](::fixy::BgLoadCtx const& ctx) noexcept {
        for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
            auto& state = round_of<Race>(rounds, round);
            state.start.arrive_and_wait(ctx);
            pause_for(seal_delay_of(round));
            state.table.seal(kTest);
            state.count_at_seal = state.table.count();
        }
    };
}

template <typename Race>
[[nodiscard]] auto writer_of(Rounds<Race>& rounds, uint32_t chain, ::fixy::Index<WRITER_COUNT> writer) noexcept {
    return [&rounds, chain, writer](::fixy::BgLoadCtx const& ctx) noexcept {
        for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
            auto& state = round_of<Race>(rounds, round);
            state.start.arrive_and_wait(ctx);
            run_writer(
                round, static_cast<uint32_t>(writer.raw()),
                [&state](crucible::SchemaHash hash) { return Race::register_one(state.table, state.view, hash); },
                state.registered.at(writer));
        }
    };
}

// The six bodies of one chain of one table: the reader, the sealer and the
// four writers.
template <typename Race>
[[nodiscard]] auto chain_of(Rounds<Race>& rounds, uint32_t chain) noexcept {
    return ::fixy::Record{reader_of<Race>(rounds, chain),
                          sealer_of<Race>(rounds, chain),
                          writer_of<Race>(rounds, chain, ::fixy::index<0>()),
                          writer_of<Race>(rounds, chain, ::fixy::index<1>()),
                          writer_of<Race>(rounds, chain, ::fixy::index<2>()),
                          writer_of<Race>(rounds, chain, ::fixy::index<3>())};
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
    for (::fixy::Index<ROUND_COUNT> const at : rounds.indices()) {
        const uint32_t round = static_cast<uint32_t>(at.raw());
        auto const& state = rounds.at(at);
        const uint32_t final_count = state.table.count();
        CRUCIBLE_TEST_ASSERT(kTest, state.count_at_seal == final_count);
        CRUCIBLE_TEST_ASSERT(kTest, state.count_seen_by_reader == final_count);
        CRUCIBLE_TEST_ASSERT(kTest, count_true(state.registered) == final_count);
        for (::fixy::Index<WRITER_COUNT> const writer : state.registered.indices()) {
            WriterFlags const& per_writer = state.registered.at(writer);
            for (::fixy::Index<WRITES_PER_WRITER> const index : per_writer.indices()) {
                const bool is_present =
                    Race::is_present(state.table, hash_of(round, static_cast<uint32_t>(writer.raw()),
                                                          static_cast<uint32_t>(index.raw())));
                CRUCIBLE_TEST_ASSERT(kTest, is_present == per_writer.at(index));
            }
        }
        const uint32_t refused_count = WRITER_COUNT * WRITES_PER_WRITER - final_count;
        counts.with_a_refusal += refused_count > 0 ? 1u : 0u;
        counts.with_a_write += refused_count < WRITER_COUNT * WRITES_PER_WRITER ? 1u : 0u;
    }
    return counts;
}

}  // namespace

int main() {
    // The main thread mints each view, so the tables are made before any
    // thread starts.  They are too large for the stack.
    ::fixy::Box<Rounds<SchemaRace>> schema_rounds =
        ::fixy::mint_box<Rounds<SchemaRace>>(::foundation::effects::testing::test().alloc);
    ::fixy::Box<Rounds<KernelRace>> kernel_rounds =
        ::fixy::mint_box<Rounds<KernelRace>>(::foundation::effects::testing::test().alloc);

    // The bodies of the four chains, each of THREAD_COUNT threads.  The door
    // starts each thread before the first round, and it joins each thread
    // before it returns.
    static_assert(CHAIN_COUNT == 2 && WRITER_COUNT == 4 && THREAD_COUNT == 6,
                  "the bodies below name two chains of each table, each of a reader, a sealer and four writers");
    auto [schema_reader_0, schema_sealer_0, schema_writer_00, schema_writer_01, schema_writer_02, schema_writer_03] =
        chain_of<SchemaRace>(*schema_rounds, 0);
    auto [schema_reader_1, schema_sealer_1, schema_writer_10, schema_writer_11, schema_writer_12, schema_writer_13] =
        chain_of<SchemaRace>(*schema_rounds, 1);
    auto [kernel_reader_0, kernel_sealer_0, kernel_writer_00, kernel_writer_01, kernel_writer_02, kernel_writer_03] =
        chain_of<KernelRace>(*kernel_rounds, 0);
    auto [kernel_reader_1, kernel_sealer_1, kernel_writer_10, kernel_writer_11, kernel_writer_12, kernel_writer_13] =
        chain_of<KernelRace>(*kernel_rounds, 1);
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(
        ctx, kTest, "the race of a registration and the seal shares the rounds of each table on purpose",
        ::fixy::thread::ThreadOptions{}, schema_reader_0, schema_sealer_0, schema_writer_00, schema_writer_01,
        schema_writer_02, schema_writer_03, schema_reader_1, schema_sealer_1, schema_writer_10, schema_writer_11,
        schema_writer_12, schema_writer_13, kernel_reader_0, kernel_sealer_0, kernel_writer_00, kernel_writer_01,
        kernel_writer_02, kernel_writer_03, kernel_reader_1, kernel_sealer_1, kernel_writer_10, kernel_writer_11,
        kernel_writer_12, kernel_writer_13)
        .expect("the thread door starts each thread of the four chains");

    // The split between refused and admitted writes depends on the
    // scheduler, so the test reports it and does not assert it.
    const RaceCounts schema_counts = check_rounds<SchemaRace>(*schema_rounds);
    const RaceCounts kernel_counts = check_rounds<KernelRace>(*kernel_rounds);
    ::fixy::report(::fixy::Sink::Out,
                   "test_registration_seal_race: {} races, {} with a refused write, {} with an admitted write\n",
                   2 * ROUND_COUNT, schema_counts.with_a_refusal + kernel_counts.with_a_refusal,
                   schema_counts.with_a_write + kernel_counts.with_a_write);
    return 0;
}
