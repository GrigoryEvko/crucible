// The compile-time checks of crucible/cntp/Integrity.h.

#include <crucible/cntp/Integrity.h>

namespace crucible::cntp {

static_assert(sizeof(IntegrityHash) == sizeof(std::uint64_t));
static_assert(sizeof(IntegrityOwnedPayload<::fixy::View<std::byte const>>) == sizeof(::fixy::View<std::byte const>));
static_assert(sizeof(IntegrityWrappedMessage<::fixy::View<std::byte const>>)
              == sizeof(::fixy::View<std::byte const>) + sizeof(std::uint64_t));

}  // namespace crucible::cntp
