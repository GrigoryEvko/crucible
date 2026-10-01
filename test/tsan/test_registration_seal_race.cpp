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

#include "../test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <thread>

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

using RegisteredFlags = std::array<std::array<bool, WRITES_PER_WRITER>, WRITER_COUNT>;

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

// The pause instructions that a thread spins at the latch of a round before
// it sleeps.  A thread that sleeps starts late when the latch opens, and a
// late writer moves the seal ahead of the writes.  With the spin, each
// thread that has a CPU sees the latch open at once.  On a busy host the
// spin is short, compared with the time to wake a thread that sleeps.
constexpr uint32_t LATCH_SPIN_PAUSES = 4096;

void arrive_and_wait(std::latch& start) {
    start.count_down();
    for (uint32_t i = 0; i < LATCH_SPIN_PAUSES && !start.try_wait(); ++i)
        CRUCIBLE_SPIN_PAUSE;
    start.wait();
}

// Keeps the writes of one writer in order and checks that a refusal is final.
template <typename Register>
void run_writer(uint32_t round, uint32_t writer, Register&& register_one,
                std::array<bool, WRITES_PER_WRITER>& registered) {
    bool was_refused_before = false;
    for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index) {
        const bool was_registered = register_one(hash_of(round, writer, index));
        assert(!(was_refused_before && was_registered));
        was_refused_before = was_refused_before || !was_registered;
        registered[index] = was_registered;
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
        return table.register_name(view, hash, ::fixy::mint_tagged<::fixy::tags::source::FromInternal>("aten::race"));
    }

    [[nodiscard]] static uint32_t read_sealed(Table const& table, uint32_t /*round*/) {
        const std::span<const crucible::SchemaEntry> entries = table.entries();
        for (size_t i = 1; i < entries.size(); ++i)
            assert(entries[i - 1].hash < entries[i].hash);
        return static_cast<uint32_t>(entries.size());
    }

    [[nodiscard]] static bool is_present(Table const& table, crucible::SchemaHash hash) {
        return table.lookup(hash).value().data() != nullptr;
    }
};

// The kernel table.  The reader classifies each hash of the round, and it
// counts the hashes that the table holds.
struct KernelRace {
    using Table = crucible::CKernelTable;

    [[nodiscard]] static bool register_one(Table& table, Table::MutableView const& view, crucible::SchemaHash hash) {
        return table.register_op(view, hash, crucible::CKernelId::GEMM_MM);
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
    std::optional<typename Table::MutableView> view = table.mint_mutable_view(kVigilForeground);
    std::latch start{WRITER_COUNT + 1};
    RegisteredFlags registered{};
    uint32_t count_at_seal = 0;
    uint32_t count_seen_by_reader = 0;
};

template <typename Race>
using Rounds = std::array<Round<typename Race::Table>, ROUND_COUNT>;

using ChainThreads = std::array<std::jthread, THREAD_COUNT>;

// Starts the threads of one chain of one table.  Each thread runs the
// rounds of the chain in order.  A writer or the sealer that ends a round
// waits at the latch of the next round of the chain, so the rounds of one
// chain do not overlap.  The reader can still read a sealed table while the
// next round races, as the background thread can.
//
// The reader sleeps until the latch of its round opens, and it does not
// count the latch down.  The latch opens before each write of the round, so
// only the seal orders the writes before the reads.
template <typename Race>
[[nodiscard]] ChainThreads start_chain(Rounds<Race>& rounds, uint32_t chain) {
    ChainThreads threads;
    threads[0] = std::jthread{[&rounds, chain] {
        for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
            auto& state = rounds[round];
            state.start.wait();
            while (!state.table.is_sealed())
                CRUCIBLE_SPIN_PAUSE;
            state.count_seen_by_reader = Race::read_sealed(state.table, round);
        }
    }};
    threads[1] = std::jthread{[&rounds, chain] {
        for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
            auto& state = rounds[round];
            arrive_and_wait(state.start);
            pause_for(seal_delay_of(round));
            state.table.seal();
            state.count_at_seal = state.table.count();
        }
    }};
    for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
        threads[2 + writer] = std::jthread{[&rounds, chain, writer] {
            for (uint32_t round = chain; round < ROUND_COUNT; round += CHAIN_COUNT) {
                auto& state = rounds[round];
                arrive_and_wait(state.start);
                run_writer(
                    round, writer,
                    [&state](crucible::SchemaHash hash) { return Race::register_one(state.table, *state.view, hash); },
                    state.registered[writer]);
            }
        }};
    }
    return threads;
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
    for (uint32_t round = 0; round < ROUND_COUNT; ++round) {
        auto const& state = rounds[round];
        const uint32_t final_count = state.table.count();
        assert(state.count_at_seal == final_count);
        assert(state.count_seen_by_reader == final_count);
        assert(count_true(state.registered) == final_count);
        for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
            for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index) {
                const bool is_present = Race::is_present(state.table, hash_of(round, writer, index));
                assert(is_present == state.registered[writer][index]);
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
    const auto schema_rounds = std::make_unique<Rounds<SchemaRace>>();
    const auto kernel_rounds = std::make_unique<Rounds<KernelRace>>();
    for (uint32_t round = 0; round < ROUND_COUNT; ++round) {
        assert((*schema_rounds)[round].view.has_value());
        assert((*kernel_rounds)[round].view.has_value());
    }
    {
        // Each thread joins at the end of this block.
        std::array<ChainThreads, 2 * CHAIN_COUNT> chains;
        for (uint32_t chain = 0; chain < CHAIN_COUNT; ++chain) {
            chains[2 * chain] = start_chain<SchemaRace>(*schema_rounds, chain);
            chains[2 * chain + 1] = start_chain<KernelRace>(*kernel_rounds, chain);
        }
    }

    // The split between refused and admitted writes depends on the
    // scheduler, so the test reports it and does not assert it.
    const RaceCounts schema_counts = check_rounds<SchemaRace>(*schema_rounds);
    const RaceCounts kernel_counts = check_rounds<KernelRace>(*kernel_rounds);
    std::printf("test_registration_seal_race: %u races, %u with a refused write, %u with an admitted write\n",
                2 * ROUND_COUNT, schema_counts.with_a_refusal + kernel_counts.with_a_refusal,
                schema_counts.with_a_write + kernel_counts.with_a_write);
    return 0;
}
