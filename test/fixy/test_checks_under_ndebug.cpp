// The checks of the base stay in a unit that defines NDEBUG.
//
// A Release library compiles with NDEBUG.  Each check of the base is a
// contract check or CRUCIBLE_FATAL_INVARIANT, and NDEBUG removes none of
// them.  The target gives this unit NDEBUG on the command line, so the first
// include of foundation/Platform.h sees it, as a Release library does.  The
// unit removes NDEBUG before the other headers, because the abort probe
// needs the assert of the C library.  Each violation below then ends the
// process:
//
//   EffectMask::from_raw                 a mask with a bit above the atoms
//   AtomicMonotonic::advance_sole_writer_from
//                                        a current value that is not the
//                                        stored value
//   WriteOnce::get                       a read of an empty slot

#include <foundation/Platform.h>
#undef NDEBUG

#include <fixy/Mutation.h>
#include <foundation/effects/Row.h>

#include "../foundation/abort_probe.h"

#include <cstdint>

namespace {

using ::crucible::test::aborts;
namespace fe = ::foundation::effects;

// The value, through a call that the optimizer cannot read.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

void test_poisoned_effect_mask_ends_the_process() {
    fe::EffectMask::underlying_type const poison = opaque(fe::EffectMask::underlying_type{1} << 6);
    CRUCIBLE_FATAL_INVARIANT(aborts([poison] { (void)fe::EffectMask::from_raw(poison); }));
    fe::EffectMask::underlying_type const clean = opaque(fe::EffectMask::underlying_type{1});
    CRUCIBLE_FATAL_INVARIANT(!aborts([clean] { (void)fe::EffectMask::from_raw(clean); }));
}

void test_stale_current_of_a_sole_writer_ends_the_process() {
    auto counter = ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(5);
    std::uint64_t const stale = opaque(std::uint64_t{4});
    CRUCIBLE_FATAL_INVARIANT(aborts([&counter, stale] { counter.advance_sole_writer_from(stale, 6); }));
    std::uint64_t const stored = opaque(std::uint64_t{5});
    CRUCIBLE_FATAL_INVARIANT(!aborts([&counter, stored] { counter.advance_sole_writer_from(stored, 6); }));
    CRUCIBLE_FATAL_INVARIANT(counter.get() == 6);
}

void test_read_of_an_empty_slot_ends_the_process() {
    auto const slot = ::fixy::mint_write_once<std::uint32_t>();
    CRUCIBLE_FATAL_INVARIANT(aborts([&slot] { (void)slot.get(); }));
}

}  // namespace

int main() {
    test_poisoned_effect_mask_ends_the_process();
    test_stale_current_of_a_sole_writer_ends_the_process();
    test_read_of_an_empty_slot_ends_the_process();
    return 0;
}
