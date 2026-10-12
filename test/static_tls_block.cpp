// One shared library with an initial-exec thread-local block of
// STATIC_TLS_BLOCK_BYTES bytes, for the run-time witness of the rule static-tls
// in test_process_wide_across_libraries.cpp.  The build makes two libraries from
// this file.  The block of the control holds the error threshold of the rule.
// The block of the plant is larger than the static TLS that glibc keeps free.
//
// An initial-exec access makes the dynamic loader put the full TLS block of the
// library in the static TLS of the process when dlopen loads it.  dlopen refuses
// a library whose block does not fit in the free part.

#include <cstddef>
#include <cstdint>

namespace {

// A block of Words words with no C array, because the quarantine rule refuses a
// C array.  Each level divides the words into two halves.
template <std::size_t Words>
struct TlsWords {
    TlsWords<Words / 2> low;
    TlsWords<Words - Words / 2> high;
};

template <>
struct TlsWords<1> {
    std::uint64_t word = 0;
};

static_assert(STATIC_TLS_BLOCK_BYTES % sizeof(std::uint64_t) == 0, "the block holds whole words");
using Block = TlsWords<STATIC_TLS_BLOCK_BYTES / sizeof(std::uint64_t)>;
static_assert(sizeof(Block) == STATIC_TLS_BLOCK_BYTES, "the block has no padding");

[[gnu::tls_model("initial-exec")]] thread_local Block static_tls_block;

// The first word of a block.
template <std::size_t Words>
[[nodiscard]] std::uint64_t& first_word(TlsWords<Words>& block) noexcept {
    if constexpr (Words == 1) {
        return block.word;
    } else {
        return first_word(block.low);
    }
}

}  // namespace

// The access gives the library its initial-exec relocation.  The test only
// loads the library, and it does not call this function.
extern "C" [[gnu::visibility("default")]] std::uint64_t static_tls_block_touch(std::uint64_t value) noexcept;

extern "C" std::uint64_t static_tls_block_touch(std::uint64_t value) noexcept {
    std::uint64_t& word = first_word(static_tls_block);
    word += value;
    return word;
}
