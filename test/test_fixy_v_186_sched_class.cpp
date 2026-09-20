// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-wrapper checks no
// single wrapper header can state about itself.

#include <crucible/safety/SchedClass.h>
#include <crucible/safety/IsSchedClass.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace sf = ::crucible::safety;
namespace ex = ::crucible::safety::extract;
namespace dg = ::crucible::safety::diag;
using Sp_t = sf::SchedulerPolicy_v;
using Cs_t = sf::ClockSource_v;

static_assert(sizeof(sf::SchedClass<Sp_t::Other, int>) == sizeof(int));
static_assert(sizeof(sf::SchedClass<Sp_t::Fifo, double>) == sizeof(double));
static_assert(sizeof(sf::SchedClass<Sp_t::Idle, char>) == sizeof(char));
static_assert(sizeof(sf::SchedClass<Sp_t::Deadline, int, 5000, 10000, 20000>) == sizeof(int));

static_assert(sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Fifo>);
static_assert(sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Deadline>);
static_assert(!sf::SchedClass<Sp_t::Fifo, int>::runnable_on<Sp_t::Other>,
              "A SCHED_FIFO task MUST NOT run on a SCHED_OTHER pool (Fifo ⋤ Other).");
static_assert(sf::SchedClass<Sp_t::Other, int>::runnable_on<Sp_t::Fifo>);
static_assert(!sf::SchedClass<Sp_t::Other, int>::runnable_on<Sp_t::Batch>);

static_assert(sf::SchedClass<Sp_t::Deadline, int, 5000, 10000, 20000>::deadline_ns == 10000);
static_assert(sf::SchedClass<Sp_t::Fifo, int>::runtime_ns == 0);

static_assert(ex::IsSchedClass<sf::SchedClass<Sp_t::Fifo, int>>);
static_assert(!ex::IsSchedClass<int>);
static_assert(std::is_same_v<ex::sched_class_value_t<sf::SchedClass<Sp_t::Other, double>>, double>);
static_assert(ex::sched_class_policy_v<sf::SchedClass<Sp_t::Deadline, int, 5000, 10000, 20000>> == Sp_t::Deadline);

static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>
                  != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Other, int>>,
              "SchedClass<Fifo,int> and <Other,int> MUST hash differently — the "
              "policy salt discriminates federation-cache slots.");
static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>> != dg::row_hash_contribution_v<int>,
              "SchedClass<Fifo,int> MUST hash differently from bare int — the wrapper "
              "tag (0x31) discriminates the wrapped value.");

static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Deadline, int, 5000, 10000, 20000>>
                  != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Deadline, int, 5000, 10000, 30000>>,
              "Two SCHED_DEADLINE tasks with different periods MUST hash to distinct "
              "slots — the budget NTTPs are folded into the row_hash.");

static_assert(dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, int>>
                  != dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, int>>,
              "SchedClass (0x31) and ClockSource (0x30) are distinct wrappers — their "
              "per-wrapper salts MUST discriminate even at adjacent high bytes.");

static_assert(dg::row_hash_contribution_v<sf::ClockSource<Cs_t::Boot, sf::SchedClass<Sp_t::Fifo, int>>>
                  != dg::row_hash_contribution_v<sf::SchedClass<Sp_t::Fifo, sf::ClockSource<Cs_t::Boot, int>>>,
              "ClockSource<Boot, SchedClass<Fifo,int>> and SchedClass<Fifo, "
              "ClockSource<Boot,int>> MUST hash differently — row_hash is "
              "order-sensitive per the canonical wrapper-nesting discipline.");

}  // namespace

int main() {
    ::crucible::safety::detail::sched_class_self_test::runtime_smoke_test();
    if (!::crucible::safety::extract::is_sched_class_smoke_test()) return 1;
    return 0;
}
