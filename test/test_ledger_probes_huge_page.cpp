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
    //
    // Each region is one huge page, so the huge-page region costs one
    // huge-page fault.  With the defrag mode `madvise`, that fault reclaims
    // and compacts memory before it returns.  On a host with no free 2 MiB
    // block and a full swap, one such fault can take seconds.
    const std::size_t bytes = ledger::ProbeRegion::kHugePageBytes;

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
        // The region holds at most one huge page, so the test pays at most
        // one huge-page fault.
        assert(ledger::probes::huge_page_detail::anon_huge_kib_for(huge->data())
               <= ledger::ProbeRegion::kHugePageBytes / 1024u);
        std::printf("    (hugepage advice %s on this host)\n", took_huge ? "took" : "was refused");
    }

    std::printf("  test_page_policy_is_verified_against_the_kernel: PASSED\n");
}

}  // namespace

namespace crucible::ledger::probes {

namespace huge_page_detail::self_test {

static_assert(!HugePageMeasurement{}.is_usable());
static_assert(HugePageMeasurement{}.fault == LedgerError::NotApplicableOnThisHost);

// The chase region must be far past what a base-page TLB covers, or the
// payback measurement cannot see a page size at all.
static_assert(kChaseRegionBytes / 4096u > 60000u, "the chase must outrun any plausible base-page TLB");
// The fault region must be a whole number of huge pages, or a sample
// averages over a fractional one.
static_assert(kFaultRegionBytes % (2ull * 1024ull * 1024ull) == 0u);
static_assert(kFaultSampleCount >= kMinSampleCount);

}  // namespace huge_page_detail::self_test

}  // namespace crucible::ledger::probes

int main() {
    std::printf("test_ledger_probes_huge_page:\n");
    test_page_policy_is_verified_against_the_kernel();
    std::printf("test_ledger_probes_huge_page: 1 group, all passed\n");
    return 0;
}
