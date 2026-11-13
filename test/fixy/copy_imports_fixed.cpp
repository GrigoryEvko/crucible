// The fixed copies of the import check of fixy::copy.
//
// The unit copies each fixed extent of 1 to 64 bytes, from a hot caller and
// from a cold caller, and a View of 5 words and a View of 2 records.  The
// test copy_imports (test/fixy/CMakeLists.txt) reads the undefined symbols
// of this object.  A fixed copy of at most 64 bytes compiles to loads and
// stores in each block of each build, so the object imports no memmove and
// no memcpy.  The functions are [[gnu::used]] in an unnamed namespace, so
// the compiler emits each one, and each takes its Views as parameters, so
// the optimizer cannot fold a copy away.

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace {

// A record of 24 bytes.
struct Entry final {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
    std::uint64_t extra = 0;
};

// Copies the Bytes bytes of source at Offset into the first Bytes bytes of
// target, and then the same for each smaller count down to one byte.  The
// two callers take different offsets, so the code of their instances
// differs, and the linker folds no instance of one caller into the other.
template <std::size_t Offset, std::size_t Bytes>
void copy_each_count(fixy::View<std::byte> target, fixy::View<std::byte const> source) noexcept {
    for (fixy::View<std::byte, Bytes> to : target.window<Bytes>(0)) {
        for (fixy::View<std::byte const, Bytes> from : source.window<Bytes>(Offset)) {
            fixy::copy(to, from);
        }
    }
    if constexpr (Bytes > 1) {
        copy_each_count<Offset, Bytes - 1>(target, source);
    }
}

[[gnu::used]] void copy_bytes(fixy::View<std::byte> target, fixy::View<std::byte const> source) noexcept {
    copy_each_count<0, 64>(target, source);
}

// GCC optimizes a cold function, and each function that only it calls, for
// size.  There GCC calls the library memmove for a memmove builtin that it
// does not fold to one load and one store.
[[gnu::used, gnu::cold]] void copy_bytes_cold(fixy::View<std::byte> target,
                                              fixy::View<std::byte const> source) noexcept {
    copy_each_count<1, 64>(target, source);
}

[[gnu::used]] void copy_words(fixy::View<std::uint32_t, 5> target, fixy::View<std::uint32_t const, 5> source) noexcept {
    fixy::copy(target, source);
}

[[gnu::used]] void copy_entries(fixy::View<Entry, 2> target, fixy::View<Entry const, 2> source) noexcept {
    fixy::copy(target, source);
}

}  // namespace
