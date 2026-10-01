// The compile-time checks of crucible/cntp/Integrity.h.

#include <crucible/cntp/Integrity.h>

namespace crucible::cntp {

static_assert(sizeof(IntegrityHash) == sizeof(std::uint64_t));
static_assert(sizeof(IntegrityOwnedPayload<std::span<const std::byte>>) == sizeof(std::span<const std::byte>));
static_assert(sizeof(IntegrityWrappedMessage<std::span<const std::byte>>)
              == sizeof(std::span<const std::byte>) + sizeof(std::uint64_t));

}  // namespace crucible::cntp
