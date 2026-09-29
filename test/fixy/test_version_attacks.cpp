// Attacks on counts, versions and version stamps.
//
// A version is a claim about a payload, and the claim comes from the
// VersionSource of the program, not from the producer.  Each attack below
// is legal C++ that tries to make a payload claim a version that no
// source vouched for.  An attack that the types refuse is pinned by a
// static assertion or a run-time check.  An attack that still succeeds is
// an entry of the ledger at the foot of this file, and the ledger only
// shrinks.

#include <fixy/EpochVersioned.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string_view>
#include <type_traits>

namespace version_attacks {

namespace fe = ::foundation::effects;
using InitCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using fixy::EpochLattice;
using fixy::GenerationLattice;
using fixy::VersionStamp;
using EV = fixy::EpochVersioned<int>;

// ── Attacks that the types refuse ────────────────────────────────────

// A stamp is built only by a source: no default, no counts, no bytes.
static_assert(!std::is_default_constructible_v<VersionStamp>);
static_assert(!std::is_constructible_v<VersionStamp, fixy::Epoch, fixy::Generation>);
static_assert(!std::is_trivially_copyable_v<VersionStamp>);

// A versioned value is built from a stamp, not from counts.
static_assert(!std::is_constructible_v<EV, int, fixy::Epoch, fixy::Generation>);
static_assert(!std::is_default_constructible_v<EV>);

// A count is not built from an integer, and one axis is not another.
static_assert(!std::is_constructible_v<fixy::Epoch, std::uint64_t>);
static_assert(!std::is_constructible_v<fixy::Epoch, fixy::Generation>);

// A source vouches only for a version it has reached, so a received
// version above its own is refused.
[[nodiscard]] inline bool source_refuses_a_version_it_has_not_reached() {
    InitCtx const init{fe::testing::init()};
    fixy::VersionSource source = fixy::mint_version_source(init);
    (void)source.advance_epoch();
    const auto ahead =
        source.stamp_received(EpochLattice::successor(source.stamp().epoch()), GenerationLattice::bottom());
    const auto reached = source.stamp_received(source.stamp().epoch(), GenerationLattice::bottom());
    return !ahead.has_value() && ahead.error() == fixy::VersionConflict::AheadOfSource && reached.has_value();
}

// ── The ledger ───────────────────────────────────────────────────────

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"a second source minted in one Init scope stamps a version that the first source never reached",
     "a stamp names no source, so a gate cannot tell which source vouched for it; only a source identity carried "
     "in every stamp and every versioned value would refuse this, at a word for each value"},
    {"top() of a count lattice builds the largest count with no successor steps",
     "a count is a number of steps and not an event, successor() reaches every count a program can wait for, and "
     "the claim that events happened lives in the VersionSource, which a count alone never answers"},
};
static_assert(std::size(kLedger) <= 2, "the ledger only shrinks");

// The first ledger entry reproduces: a value stamped by a rogue source
// claims a version that the real source has not reached.
[[nodiscard]] inline bool a_second_source_outruns_the_first() {
    InitCtx const init{fe::testing::init()};
    fixy::VersionSource real = fixy::mint_version_source(init);
    fixy::VersionSource rogue = fixy::mint_version_source(init);
    for (int step = 0; step < 5; ++step)
        (void)rogue.advance_epoch();
    const EV forged{1, rogue.stamp()};
    return !EpochLattice::leq(forged.epoch(), real.stamp().epoch());
}

}  // namespace version_attacks

int main() {
    using namespace version_attacks;
    int failures = 0;
    const auto expect = [&failures](bool condition, char const* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };

    expect(source_refuses_a_version_it_has_not_reached(), "a source vouched for a version it has not reached");
    expect(EpochLattice::top().raw() == std::numeric_limits<std::uint64_t>::max(),
           "stale ledger entry: top() no longer builds the largest count, delete it");
    expect(a_second_source_outruns_the_first(),
           "stale ledger entry: a second source no longer outruns the first, delete it");

    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_version_attacks: every refused attack refused, %zu ledger entries reproduce\n",
                std::size(kLedger));
    return EXIT_SUCCESS;
}
