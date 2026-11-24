// The checks of the base stay in a unit that defines NDEBUG.
//
// A Release library compiles with NDEBUG.  Each check of the base is a
// contract check or CRUCIBLE_FATAL_INVARIANT, and NDEBUG removes none of
// them.  The target gives this unit NDEBUG on the command line, so each
// include sees it, as a Release library does.  Each violation below ends the
// process, in a child through expect_dies of the Check family:
//
//   EffectMask::from_raw                 a mask with a bit above the atoms
//   EffectMask::set                      an atom past the last enumerator
//   AtomicMonotonic::advance_sole_writer_from
//                                        a current value that is not the
//                                        stored value
//   WriteOnce::set                       a second set of a slot

#include <foundation/Platform.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdint>

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;
namespace fe = ::foundation::effects;

// The value, through a call that the optimizer cannot read.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// The owner of the counter of the test, which makes the proof of its one
// writer (the proof of the one writer of foundation/SoleWriterProof.h).
struct CounterOwner final {
    struct Role final {};
    [[nodiscard]] static constexpr ::fixy::SoleWriterProof<CounterOwner, Role> proof() noexcept { return {}; }
};

// The clean mask and the advance from the stored value are the controls, in
// the test process.
void test_poisoned_effect_mask_ends_the_process(TestRunnerCtx const& test) {
    // The first bit above the last atom names no atom.
    fe::EffectMask::underlying_type const poison = opaque(fe::EffectMask::underlying_type{1} << fe::effect_count);
    expect_dies(
        test, [poison] noexcept { (void)fe::EffectMask::from_raw(poison); },
        ::fixy::TextView{"foundation: fatal invariant failed: sanitized == b"});
    fe::EffectMask::underlying_type const clean = opaque(fe::EffectMask::underlying_type{1});
    (void)fe::EffectMask::from_raw(clean);
}

// A cast of a number past the last enumerator of Effect names no atom.  set()
// refuses it through EffectRowLattice::single, so no mask holds a bit past the
// top of the lattice.  The old set() made bit 14 from the cast of 14, and
// raw() gave the bit out again.  The last atom is the control, in the test
// process.
void test_atom_past_the_last_ends_the_process(TestRunnerCtx const& test) {
    fe::Effect const past = opaque(static_cast<fe::Effect>(14));
    expect_dies(
        test,
        [past] noexcept {
            fe::EffectMask mask{};
            mask.set(past);
            (void)opaque(mask.raw());
        },
        ::fixy::TextView{"static_cast<std::uint8_t>(atom) <= detail::last_enumerator_value_of<Effect>"});
    fe::EffectMask mask{};
    mask.set(opaque(fe::Effect::Div));
    CRUCIBLE_FATAL_INVARIANT(mask.test(fe::Effect::Div) && mask.raw() == fe::EffectRowLattice::single(fe::Effect::Div));
}

void test_stale_current_of_a_sole_writer_ends_the_process(TestRunnerCtx const& test) {
    auto counter =
        ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::SoleWriterProof<CounterOwner, CounterOwner::Role>>(5);
    std::uint64_t const stale = opaque(std::uint64_t{4});
    expect_dies(
        test, [&counter, stale] noexcept { counter.advance_sole_writer_from(CounterOwner::proof(), stale, 6); },
        ::fixy::TextView{"foundation: fatal invariant failed: !Cmp{}(current, stored) && !Cmp{}(stored, current)"});
    std::uint64_t const stored = opaque(std::uint64_t{5});
    counter.advance_sole_writer_from(CounterOwner::proof(), stored, 6);
    CRUCIBLE_FATAL_INVARIANT(counter.get() == 6);
}

void test_second_set_of_a_slot_ends_the_process(TestRunnerCtx const& test) {
    auto slot = ::fixy::mint_write_once<std::uint32_t>();
    static_cast<void>(slot.set(opaque(std::uint32_t{1})));
    expect_dies(
        test, [&slot] noexcept { static_cast<void>(slot.set(opaque(std::uint32_t{2}))); },
        ::fixy::TextView{"foundation: contract violation: !value_.has_value()"});
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_poisoned_effect_mask_ends_the_process(test);
    test_atom_past_the_last_ends_the_process(test);
    test_stale_current_of_a_sole_writer_ends_the_process(test);
    test_second_set_of_a_slot_ends_the_process(test);
    return 0;
}
