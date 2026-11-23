// The cold bodies of the core of the arenas of the Ref family
// (foundation/core/BumpArena.h).
//
// The file is in the door layer, because the end of an epoch calls the poison
// door of foundation/os/Poison.h.  The blocks come from the allocation door
// of foundation/core/Heap.h, so the file calls no function of the C library.

#include <foundation/core/BumpArena.h>

#include <foundation/core/Heap.h>
#include <foundation/core/Report.h>
#include <foundation/os/Poison.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

namespace {

// value << shift, or the largest size_t when the result does not fit.
[[nodiscard]] constexpr std::size_t saturating_shift_(std::size_t value, std::uint32_t shift) noexcept {
    if (shift >= 64u || value > (~std::size_t{0} >> shift)) {
        return ~std::size_t{0};
    }
    return value << shift;
}

// The layout of a block of bytes bytes, which are above zero, at the
// alignment of each block.  A size that saturated passes the largest layout
// and ends the process.
[[nodiscard]] ::foundation::alloc::Layout block_layout_(std::size_t bytes) noexcept {
    return ::foundation::alloc::mint_layout(ByteCount{bytes},
                                            ::foundation::alloc::Alignment{detail::BumpArenaCore::block_alignment})
        .expect("a block of an arena is a layout: its bytes are above zero and fit the largest object");
}

}  // namespace

namespace detail {

#if defined(__SANITIZE_ADDRESS__)
void bump_unpoison_(void const* first, std::size_t count) noexcept { ::foundation::os::unpoison_bytes(first, count); }
#endif

void refuse_arena_array_(std::size_t count, std::size_t element_bytes) noexcept {
    fatal("an array of {} elements of {} bytes in an arena is longer than the largest View", count, element_bytes);
}

void refuse_arena_address_(std::size_t alignment) noexcept {
    fatal("the next address of an arena does not fit in the address space at the alignment {}", alignment);
}

BumpArenaCore::~BumpArenaCore() { free_blocks_from_(0); }

void* BumpArenaCore::bump_slow_(std::size_t bytes, std::size_t alignment) noexcept {
    // The block holds the bytes at each address that the alignment can give.
    std::size_t const needed =
        checked_add(bytes, alignment).expect("the bytes of a request below the largest View fit in size_t");
    enter_block_(cur_block_ == nullptr ? 0u : cur_index_ + 1, needed);

    std::uintptr_t const base = __builtin_bit_cast(std::uintptr_t, cur_block_);
    std::uintptr_t const aligned_address = detail::arena_aligned_(base, alignment);
    std::size_t const aligned = aligned_address - base;
    std::byte* const storage = cur_block_ + aligned;
    offset_ = aligned + bytes;
    ::foundation::os::unpoison_bytes(storage, bytes);
    return storage;
}

// Block k holds at least block_size << k bytes, which bounds the count of
// blocks.  A new block is poisoned, so each of its bytes that the bump did
// not give out is not addressable, also before the first epoch ends.  A block
// of an earlier epoch is poisoned already.  Complexity: O(max_blocks).
void BumpArenaCore::enter_block_(std::uint32_t first_index, std::size_t needed) noexcept {
    std::uint32_t index = first_index;
    while (index < block_count_ && block_bytes_[index] < needed) {
        ++index;
    }
    if (index == block_count_) {
        if (index >= max_blocks) [[unlikely]] {
            fatal("an arena needs more than its {} blocks: each block doubles, so the size saturated", max_blocks);
        }
        std::size_t const standard = saturating_shift_(block_size_, index);
        std::size_t const wanted = needed > standard ? needed : standard;
        blocks_[index] = static_cast<std::byte*>(detail::heap_allocate_(block_layout_(wanted)));
        block_bytes_[index] = wanted;
        block_count_ = index + 1;
        retained_bytes_ += wanted;
        ::foundation::os::poison_bytes(blocks_[index], wanted);
    }
    cur_index_ = index;
    cur_block_ = blocks_[index];
    offset_ = 0;
    end_offset_ = block_bytes_[index];
}

// The end of an epoch can leave a block poisoned, so each block becomes
// addressable before it goes back to the door.
void BumpArenaCore::free_blocks_from_(std::uint32_t first_index) noexcept {
    for (std::uint32_t index = first_index; index < block_count_; ++index) {
        ::foundation::os::unpoison_bytes(blocks_[index], block_bytes_[index]);
        detail::heap_release_(blocks_[index], block_layout_(block_bytes_[index]));
        retained_bytes_ -= block_bytes_[index];
        blocks_[index] = nullptr;
        block_bytes_[index] = 0;
    }
    if (first_index < block_count_) {
        block_count_ = first_index;
    }
}

void BumpArenaCore::end_epoch_() noexcept {
    if (retained_bytes_ > retained_limit_) {
        free_blocks_from_(1);
    }
    for (std::uint32_t index = 0; index < block_count_; ++index) {
        ::foundation::os::poison_bytes(blocks_[index], block_bytes_[index]);
    }
    cur_index_ = 0;
    offset_ = 0;
    if (block_count_ > 0) {
        cur_block_ = blocks_[0];
        end_offset_ = block_bytes_[0];
    }
}

}  // namespace detail

}  // namespace foundation::core
