// The compile-time checks of foundation/core/Philox.h.

#include <foundation/core/Philox.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::philox_checks {

using Counter = Philox4x32::Counter;
using Key = Philox4x32::Key;

// A counter and a key are their words, and a block passes in registers.
static_assert(sizeof(Counter) == 16 && sizeof(Key) == 8);
static_assert(std::is_trivially_copyable_v<Counter> && std::is_trivially_copyable_v<Key>);

// The published reference vectors of Philox4x32-10 for the zero counter and
// key, and for each bit set.
static_assert(Philox4x32::generate(Counter{0u, 0u, 0u, 0u}, Key{0u, 0u})
              == Counter{0x6627E8D5u, 0xE169C58Du, 0xBC57AC4Cu, 0x9B00DBD8u});
static_assert(Philox4x32::generate(Counter{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
                                   Key{0xFFFFFFFFu, 0xFFFFFFFFu})
              == Counter{0x408F276Du, 0x41C83B0Eu, 0xA20BC7C6u, 0x6D5451FDu});

// The offset fills the two low words of the counter, low word first, and the
// key of 64 bits fills the two words of the key in the same sequence.
static_assert(Philox4x32::generate(0x0000000200000001u, 0x0000000400000003u)
              == Philox4x32::generate(Counter{1u, 2u, 0u, 0u}, Key{3u, 4u}));

}  // namespace foundation::core::detail::philox_checks
