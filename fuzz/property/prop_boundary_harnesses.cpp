// Every boundary harness, driven by Philox-generated inputs.
//
// The coverage-guided runs in fuzz/boundary need afl-fuzz.  This test calls
// the same harness functions from ctest, so each harness also runs on every
// build.  Each iteration picks a harness and one of its seeds, then edits
// the seed.  An edited seed passes the magic numbers and the length checks,
// so the input reaches the decoder behind them.  A harness reports a finding
// by a sanitizer report or by an abort, so the property only has to return.
//
// CMake writes boundary_harness_list.h with one CRUCIBLE_FUZZ_HARNESS(name)
// line for each harness object that this binary links.

#include "property_runner.h"

#include "../boundary/entry.h"

#include <array>
#include <span>
#include <vector>

#define CRUCIBLE_FUZZ_HARNESS(name) CRUCIBLE_FUZZ_DECLARE_HARNESS(name)
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Harness {
    const char* name;
    void (*run)(std::span<const std::uint8_t>);
    std::vector<Bytes> (*seeds)();
};

constexpr Harness kHarnesses[] = {
#define CRUCIBLE_FUZZ_HARNESS(name) Harness{#name, &CRUCIBLE_FUZZ_ENTRY_RUN(name), &CRUCIBLE_FUZZ_ENTRY_SEEDS(name)},
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS
};

struct Input {
    std::size_t harness = 0;
    Bytes bytes;
};

// Up to eight edits: flip a bit, write an edge byte, insert or delete a
// byte, or cut the tail.
Input edited_seed(crucible::fuzz::prop::Rng& rng) {
    static constexpr std::uint8_t kEdges[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0x2C, 0x0A, 0x3A};
    Input input;
    input.harness = rng.next_below(static_cast<std::uint32_t>(std::size(kHarnesses)));
    const auto seeds = kHarnesses[input.harness].seeds();
    if (!seeds.empty()) input.bytes = seeds[rng.next_below(static_cast<std::uint32_t>(seeds.size()))];

    const std::uint32_t edits = rng.next_below(9);
    for (std::uint32_t e = 0; e < edits; ++e) {
        Bytes& bytes = input.bytes;
        const std::size_t at = bytes.empty() ? 0 : rng.next_below(static_cast<std::uint32_t>(bytes.size()));
        switch (rng.next_below(5)) {
            case 0:
                if (!bytes.empty()) bytes[at] = static_cast<std::uint8_t>(bytes[at] ^ (1U << rng.next_below(8)));
                break;
            case 1:
                if (!bytes.empty()) bytes[at] = kEdges[rng.next_below(std::size(kEdges))];
                break;
            case 2:
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(rng.next32()));
                break;
            case 3:
                if (!bytes.empty()) bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            default:
                bytes.resize(at);
                break;
        }
    }
    return input;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz;
    const prop::Config cfg = prop::parse_args(argc, argv);
    const auto run_harness = [](const Input& input) {
        kHarnesses[input.harness].run(std::span<const std::uint8_t>{input.bytes});
        return true;
    };
    return prop::run("every boundary harness on edited seeds", cfg, edited_seed, run_harness);
}
