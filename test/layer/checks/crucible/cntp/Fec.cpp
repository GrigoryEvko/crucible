// The compile-time checks of crucible/cntp/Fec.h.

#include <crucible/cntp/Fec.h>

namespace crucible::cntp {

static_assert(ReedSolomonShape<10, 2>);
static_assert(!ReedSolomonShape<0, 2>);
static_assert(!ReedSolomonShape<10, 0>);
static_assert(!ReedSolomonShape<255, 2>);
static_assert(sizeof(LinearShardBuffer<::fixy::View<std::byte>>) == sizeof(::fixy::View<std::byte>));

}  // namespace crucible::cntp
