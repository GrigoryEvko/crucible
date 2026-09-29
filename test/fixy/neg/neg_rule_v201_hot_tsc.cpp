// V201: hot x an instruction class at or above the non-deterministic
// timestamp counter.
//
// A serialising timestamp read is both slow and non-deterministic: rdtscp
// drains the pipeline, which is 20-40 cycles against a budget of tens of
// nanoseconds, and the value it returns differs per run.  The rule
// covers the privileged classes with no second clause: PrivilegedMsr
// sits above NonDeterministicTsc, and a tier admits every class below it.
//
// The cost and refinement atoms silence H001 and H002 so this floors on
// V201 alone.  The pack uses the TSC tier rather than PrivilegedMsr
// deliberately: PrivilegedMsr would also trip V202, since the pack names
// no Init row.

#include <fixy/Fn.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct cycle_budget_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::hw::non_deterministic_tsc,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<fixture::cycle_budget_proved>>
        refused{};
    return 0;
}
