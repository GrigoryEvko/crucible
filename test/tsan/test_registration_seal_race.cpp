// Races registration against the seal, on the schema table and on the kernel
// table.
//
// In each round, the writers register through one view that the main thread
// minted before the seal.  A sealer thread seals the table at a different
// point in each round.  A reader thread waits for the seal and then reads the
// table with no lock, as the background thread does.
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
#include <span>
#include <thread>

namespace {

constexpr uint32_t WRITER_COUNT = 4;
constexpr uint32_t WRITES_PER_WRITER = 48;
constexpr uint32_t ROUND_COUNT = 64;

static_assert(WRITER_COUNT * WRITES_PER_WRITER <= crucible::CKERNEL_TABLE_CAP);
static_assert(WRITER_COUNT * WRITES_PER_WRITER <= crucible::SCHEMA_TABLE_CAP);

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
    for (uint32_t i = 0; i < pause_count; ++i) CRUCIBLE_SPIN_PAUSE;
}

// Keeps the writes of one writer in order and checks that a refusal is final.
template <typename Register>
void run_writer(uint32_t round, uint32_t writer, std::latch& start, Register&& register_one,
                std::array<bool, WRITES_PER_WRITER>& registered) {
    start.arrive_and_wait();
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
        for (const bool was_registered : per_writer) total += was_registered ? 1u : 0u;
    return total;
}

// Each race returns the number of writes that the seal refused.
[[nodiscard]] uint32_t race_schema_table(uint32_t round) {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view();
    assert(view.has_value());

    RegisteredFlags registered{};
    uint32_t count_at_seal = 0;
    uint32_t count_seen_by_reader = 0;
    std::latch start{WRITER_COUNT + 1};
    {
        std::jthread reader{[&] {
            while (!table.is_sealed()) CRUCIBLE_SPIN_PAUSE;
            const std::span<const crucible::SchemaEntry> entries = table.entries();
            for (size_t i = 1; i < entries.size(); ++i) assert(entries[i - 1].hash < entries[i].hash);
            count_seen_by_reader = static_cast<uint32_t>(entries.size());
        }};
        std::jthread sealer{[&] {
            start.arrive_and_wait();
            pause_for(seal_delay_of(round));
            table.seal();
            count_at_seal = table.count();
        }};
        std::array<std::jthread, WRITER_COUNT> writers;
        for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
            writers[writer] = std::jthread{[&, writer] {
                run_writer(round, writer, start,
                           [&](crucible::SchemaHash hash) {
                               return table.register_name(*view, hash,
                                                          crucible::SchemaTable::SanitizedName{"aten::race"});
                           },
                           registered[writer]);
            }};
        }
    }

    const uint32_t final_count = table.count();
    assert(count_at_seal == final_count);
    assert(count_seen_by_reader == final_count);
    assert(count_true(registered) == final_count);
    for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
        for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index) {
            const bool is_present = table.lookup(hash_of(round, writer, index)).value().data() != nullptr;
            assert(is_present == registered[writer][index]);
        }
    }
    return WRITER_COUNT * WRITES_PER_WRITER - final_count;
}

[[nodiscard]] uint32_t race_ckernel_table(uint32_t round) {
    crucible::CKernelTable table;
    const auto view = table.mint_mutable_view();
    assert(view.has_value());

    RegisteredFlags registered{};
    uint32_t count_at_seal = 0;
    uint32_t count_seen_by_reader = 0;
    std::latch start{WRITER_COUNT + 1};
    {
        std::jthread reader{[&] {
            while (!table.is_sealed()) CRUCIBLE_SPIN_PAUSE;
            uint32_t found = 0;
            for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer)
                for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index)
                    found += table.classify(hash_of(round, writer, index)) == crucible::CKernelId::GEMM_MM ? 1u : 0u;
            count_seen_by_reader = found;
        }};
        std::jthread sealer{[&] {
            start.arrive_and_wait();
            pause_for(seal_delay_of(round));
            table.seal();
            count_at_seal = table.count();
        }};
        std::array<std::jthread, WRITER_COUNT> writers;
        for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
            writers[writer] = std::jthread{[&, writer] {
                run_writer(round, writer, start,
                           [&](crucible::SchemaHash hash) {
                               return table.register_op(*view, hash, crucible::CKernelId::GEMM_MM);
                           },
                           registered[writer]);
            }};
        }
    }

    const uint32_t final_count = table.count();
    assert(count_at_seal == final_count);
    assert(count_seen_by_reader == final_count);
    assert(count_true(registered) == final_count);
    for (uint32_t writer = 0; writer < WRITER_COUNT; ++writer) {
        for (uint32_t index = 0; index < WRITES_PER_WRITER; ++index) {
            const bool is_present =
                table.classify(hash_of(round, writer, index)) == crucible::CKernelId::GEMM_MM;
            assert(is_present == registered[writer][index]);
        }
    }
    return WRITER_COUNT * WRITES_PER_WRITER - final_count;
}

}  // namespace

int main() {
    // The split between refused and admitted writes depends on the
    // scheduler, so the test reports it and does not assert it.
    uint32_t races_with_a_refusal = 0;
    uint32_t races_with_a_write = 0;
    for (uint32_t round = 0; round < ROUND_COUNT; ++round) {
        for (const uint32_t refused_count : {race_schema_table(round), race_ckernel_table(round)}) {
            races_with_a_refusal += refused_count > 0 ? 1u : 0u;
            races_with_a_write += refused_count < WRITER_COUNT * WRITES_PER_WRITER ? 1u : 0u;
        }
    }
    std::printf("test_registration_seal_race: %u races, %u with a refused write, %u with an admitted write\n",
                2 * ROUND_COUNT, races_with_a_refusal, races_with_a_write);
    return 0;
}
