// Every boundary harness, driven by Philox-generated inputs.
//
// The coverage-guided runs in test/fuzz/boundary need afl-fuzz.  This test calls
// the same harness functions from ctest, so each harness also runs on every
// build.  Each iteration picks a harness and one of its seeds, then edits
// the seed.  An edited seed passes the magic numbers and the length checks,
// so the input reaches the decoder behind them.  A harness reports a finding
// by a sanitizer report or by an abort, so the property only has to return.
//
// CMake writes boundary_harness_list.h with one CRUCIBLE_FUZZ_HARNESS(name)
// line for each harness object that this binary links.  The list gives the
// count of the harnesses, and a walk of the list calls the harness at one
// position.

#include "property_runner.h"

#include "../boundary/edit_buffer.h"
#include "../boundary/entry.h"

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#define CRUCIBLE_FUZZ_HARNESS(name) CRUCIBLE_FUZZ_DECLARE_HARNESS(name)
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS

namespace {

namespace boundary = ::crucible::fuzz::boundary;

// The count of the harnesses of the list.
constexpr std::size_t kHarnessCount = 0
#define CRUCIBLE_FUZZ_HARNESS(name) +1
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS
    ;

static_assert(kHarnessCount > 0, "the list of the boundary harnesses holds no harness");

using HarnessIndex = ::fixy::Index<kHarnessCount>;

// Feeds input to the harness at position which of the list.
void run_harness(HarnessIndex which, ::fixy::View<std::byte const> input) noexcept {
    std::size_t position = 0;
#define CRUCIBLE_FUZZ_HARNESS(name)                                  \
    if (which.raw() == position) boundary::entry::run_##name(input); \
    ++position;
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS
}

// The seeds of the harness at position which of the list.
[[nodiscard]] boundary::SeedList seeds_of(HarnessIndex which) noexcept {
    boundary::SeedList seeds = boundary::empty_seed_list();
    std::size_t position = 0;
#define CRUCIBLE_FUZZ_HARNESS(name)                                       \
    if (which.raw() == position) seeds = boundary::entry::seeds_##name(); \
    ++position;
#include "boundary_harness_list.h"
#undef CRUCIBLE_FUZZ_HARNESS
    return seeds;
}

// The edge values of an edit that sets a byte.
constexpr ::fixy::FixedArray<std::byte, 8> kEdges{
    std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80},
    std::byte{0xFF}, std::byte{0x2C}, std::byte{0x0A}, std::byte{0x3A},
};

// The bytes that the edits of one input can insert: eight inserts of one byte.
constexpr boundary::ByteCount kEditRoom{8};

struct Input {
    HarnessIndex harness;
    boundary::EditBuffer bytes;
};

// The seed at a random position of the seeds of which, or no bytes for a
// harness with no seed.
[[nodiscard]] boundary::EditBuffer picked_seed(HarnessIndex which, crucible::fuzz::prop::Rng& rng) noexcept {
    boundary::SeedList const seeds = seeds_of(which);
    boundary::EditBuffer picked{::fixy::View<std::byte const>{}, kEditRoom};
    if (seeds.size() == 0) return picked;
    std::size_t const position = rng.next_below(static_cast<std::uint32_t>(seeds.size()));
    seeds.with_view([&picked, position](::fixy::View<boundary::SeedBytes const> all) noexcept {
        ::fixy::View<std::byte const> const seed{all.get(position).expect("a position below the count")->view()};
        picked = boundary::EditBuffer{seed, kEditRoom};
    });
    return picked;
}

// Up to eight edits: flip a bit, write an edge byte, insert or delete a
// byte, or cut the tail.
Input edited_seed(crucible::fuzz::prop::Rng& rng) noexcept {
    HarnessIndex const harness =
        HarnessIndex::check(rng.next_below(static_cast<std::uint32_t>(kHarnessCount))).expect("a harness of the list");
    Input input{harness, picked_seed(harness, rng)};

    const std::uint32_t edits = rng.next_below(9);
    for (std::uint32_t e = 0; e < edits; ++e) {
        boundary::EditBuffer& bytes = input.bytes;
        const boundary::ByteCount at{
            bytes.is_empty() ? 0 : std::size_t{rng.next_below(static_cast<std::uint32_t>(bytes.size()))}};
        switch (rng.next_below(5)) {
            case 0:
                if (!bytes.is_empty()) bytes.xor_at(at, static_cast<std::byte>(1U << rng.next_below(8)));
                break;
            case 1:
                if (!bytes.is_empty()) {
                    bytes.set_at(at, *kEdges.view().get(rng.next_below(8)).expect("an edge of the table"));
                }
                break;
            case 2: {
                ::fixy::FixedArray<std::byte, 1> const inserted{static_cast<std::byte>(rng.next32())};
                bytes.insert_at(at, inserted.view());
                break;
            }
            case 3:
                if (!bytes.is_empty()) bytes.erase_at(at);
                break;
            default:
                bytes.truncate_to(at);
                break;
        }
    }
    return input;
}

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible::fuzz;
    const prop::Config cfg = prop::parse_args(arguments);
    const auto run_one = [](const Input& input) noexcept {
        run_harness(input.harness, input.bytes.view());
        return true;
    };
    return prop::run("every boundary harness on edited seeds", cfg, edited_seed, run_one);
}
