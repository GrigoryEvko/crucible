// The shared library that reads each process-wide object.  See
// process_wide_libraries.h.

#include "process_wide_libraries.h"
#include "test_abort_probe.h"

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>

#include <thread>
#include <utility>

std::uint8_t process_wide_classify(std::uint64_t schema_hash) noexcept {
    return std::to_underlying(crucible::classify_kernel(crucible::SchemaHash{schema_hash}));
}

const char* process_wide_schema_name(std::uint64_t schema_hash) noexcept {
    return crucible::schema_name(crucible::SchemaHash{schema_hash}).value().data();
}

int process_wide_is_brand_usable() noexcept {
    using Claim = ::foundation::effects::host::ProducerClaim<process_wide_libraries::SharedBrand>;
    return Claim::can_caller_use_brand() ? 1 : 0;
}

// A second brand object, whose claim this library wins on the calling thread.
// The brand is on the heap, so the jump out of the refused claim leaves no
// frame that owns it.
int process_wide_does_second_claim_abort() noexcept {
    auto* second = new process_wide_libraries::SharedBrand();
    const bool did_abort = crucible::test::aborts([second] { (void)second->claim.mint_producer_context(); });
    delete second;
    return did_abort ? 1 : 0;
}
