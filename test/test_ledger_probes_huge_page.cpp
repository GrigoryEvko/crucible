// The huge-page probe family of the hardware-capability ledger: whether the
// kernel honors a page policy.  test_ledger_probes tests the contract that
// every probe family shares.

#include <crucible/ledger/probes/HugePage.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <cstddef>
#include <cstdio>

using namespace crucible;
using ledger_probe_fixtures::probe_ctx;

namespace {

void test_page_policy_is_verified_against_the_kernel() {
    // The claim: a probe finds out what the kernel actually did rather
    // than trusting that madvise recording the advice means the fault path
    // honoured it.
    const std::size_t bytes = 8u * 1024u * 1024u;

    auto base = ledger::ProbeRegion::create(probe_ctx, bytes, ledger::PagePolicy::BasePages);
    assert(base.has_value());
    (void)base->fault_in(1u);
    assert(ledger::probes::huge_page_detail::verify_page_policy_took(*base, ledger::PagePolicy::BasePages));
    // Break it: the same base-page region must NOT pass the huge-page
    // check, or the verifier is answering true to everything.
    assert(!ledger::probes::huge_page_detail::verify_page_policy_took(*base, ledger::PagePolicy::HugePages));

    if (::fixy::concurrent::Topology::instance().hugepage_2mb_available()) {
        auto huge = ledger::ProbeRegion::create(probe_ctx, bytes, ledger::PagePolicy::HugePages);
        assert(huge.has_value());
        (void)huge->fault_in(1u);
        // Not asserted as true: a host whose memory is fragmented enough
        // can refuse the advice, and that refusal is precisely what the
        // probe checks for before reporting a hugepage number. What IS
        // asserted is that the two policies cannot both be reported for
        // one region.
        const bool took_huge =
            ledger::probes::huge_page_detail::verify_page_policy_took(*huge, ledger::PagePolicy::HugePages);
        const bool took_base =
            ledger::probes::huge_page_detail::verify_page_policy_took(*huge, ledger::PagePolicy::BasePages);
        assert(!(took_huge && took_base));
        std::printf("    (hugepage advice %s on this host)\n", took_huge ? "took" : "was refused");
    }

    std::printf("  test_page_policy_is_verified_against_the_kernel: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_ledger_probes_huge_page:\n");
    test_page_policy_is_verified_against_the_kernel();
    std::printf("test_ledger_probes_huge_page: 1 group, all passed\n");
    return 0;
}
