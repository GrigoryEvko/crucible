// Attacks on private storage through a splice.
//
// A proof type keeps its state in private members, and its doors are the
// only code that writes them.  A splice of a data member is not checked
// for access, and std::meta::access_context::unchecked() lists private
// members, so legal C++ can write the state of a proof it holds.  Naming
// a private member with ^^ is checked, and access_context::current()
// lists no private member, so unchecked() is the one door.  Each attack
// that still succeeds is an entry of the ledger at the foot of this file,
// and the ledger only shrinks.

#include <fixy/EpochVersioned.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <meta>
#include <string_view>

namespace splice_access_attacks {

namespace fe = ::foundation::effects;
using InitCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using fixy::EpochLattice;

// The first data member of a type, private or not.
consteval std::meta::info first_field(std::meta::info type) {
    return std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())[0];
}

// ── The door that is closed ──────────────────────────────────────────

// access_context::current() lists no private member of a proof.
static_assert(std::meta::nonstatic_data_members_of(^^EpochLattice::element_type,
                                                   std::meta::access_context::current())
                  .empty());
static_assert(std::meta::nonstatic_data_members_of(^^fixy::VersionStamp, std::meta::access_context::current())
                  .empty());

// ── The ledger ───────────────────────────────────────────────────────

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"a splice of a member that access_context::unchecked() lists writes the private count of a counter",
     "a splice is not checked for access, and no type can hide a data member from unchecked(); the refusal is a "
     "guard on unchecked() outside the reviewed reflection code, which the guard owner has"},
    {"a splice of the private epoch of a version stamp puts a forged count on a stamp its source made",
     "the same door as the first entry: a stamp is a proof only while no code writes its private members"},
};
static_assert(std::size(kLedger) <= 2, "the ledger only shrinks");

// The first ledger entry reproduces: a count reads a number that no
// successor step reached.
[[nodiscard]] inline bool a_splice_forges_a_count() {
    EpochLattice::element_type count = EpochLattice::bottom();
    count.[:first_field(^^EpochLattice::element_type):] = 12345;
    return count.raw() == 12345;
}

// The second ledger entry reproduces: a stamp from a source at genesis
// claims a later epoch.
[[nodiscard]] inline bool a_splice_forges_a_stamp() {
    InitCtx const init{fe::testing::init()};
    fixy::VersionSource source = fixy::mint_version_source(init);
    fixy::VersionStamp stamp = source.stamp();
    EpochLattice::element_type later = EpochLattice::successor(EpochLattice::bottom());
    stamp.[:first_field(^^fixy::VersionStamp):] = later;
    return !EpochLattice::leq(stamp.epoch(), source.stamp().epoch());
}

}  // namespace splice_access_attacks

int main() {
    using namespace splice_access_attacks;
    int failures = 0;
    const auto expect = [&failures](bool condition, char const* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };
    expect(a_splice_forges_a_count(), "stale ledger entry: a splice no longer forges a count, delete it");
    expect(a_splice_forges_a_stamp(), "stale ledger entry: a splice no longer forges a stamp, delete it");
    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_splice_access_attacks: %zu ledger entries reproduce\n", std::size(kLedger));
    return EXIT_SUCCESS;
}
