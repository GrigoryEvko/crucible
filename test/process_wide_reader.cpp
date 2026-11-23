// The shared library that reads each process-wide object.  See
// process_wide_libraries.h.

#include "process_wide_libraries.h"

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>

#include <thread>
#include <utility>

std::uint8_t process_wide_classify(std::uint64_t schema_hash) noexcept {
    return std::to_underlying(crucible::classify_kernel(crucible::SchemaHash{schema_hash}));
}

::fixy::TextView process_wide_schema_name(std::uint64_t schema_hash) noexcept {
    const auto name = crucible::schema_name(crucible::SchemaHash{schema_hash});
    return name.match([](crucible::InternedSchemaName const& found) noexcept { return found.value().view(); },
                      [] noexcept { return ::fixy::TextView{}; });
}

int process_wide_is_brand_usable() noexcept {
    using Claim = ::foundation::effects::host::ProducerClaim<process_wide_libraries::SharedBrand>;
    return Claim::can_caller_use_brand() ? 1 : 0;
}

// A second brand object, whose claim this library makes on the calling thread.
// The value initialization runs the implicit constructor of the brand, which
// the claim admits.
void process_wide_claim_a_second_brand() noexcept {
    auto second = process_wide_libraries::SharedBrand();
    static_cast<void>(second.claim.mint_producer_context());
}
