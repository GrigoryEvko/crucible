#pragma once

// A bump allocator over a list of blocks, and the epoch that ends its bytes.
//
// An allocation is a bump of an offset in the current block.  When the block
// is full, the arena moves to the next block that holds the request.  Block k
// holds at least block_size << k bytes (saturating), so 64 blocks hold more
// bytes than an address space has, and the list is a fixed array with no heap
// of its own.  Each block comes from std::malloc.  The path of an allocation
// frees no block, so a unit that only allocates from an arena does not
// reference std::free.
//
// An arena frees no single allocation.  Its bytes die all together, in one
// of two ways:
//   - The arena ends.  Its destructor frees each block.
//   - An epoch ends.  ArenaEpoch is the right to end the bytes of one arena:
//     open_epoch() gives it, and only on an empty arena, so the epoch covers
//     each byte that the arena gives out until std::move(epoch).end().  The
//     end keeps the blocks for the next epoch, so a loop that ends an epoch
//     in each turn allocates no block in its steady state.  An arena that
//     holds more than RETAINED_BYTES_LIMIT bytes at the end frees each block
//     but the first.  No other code resets an arena.
//
// In a build with AddressSanitizer, the end of an epoch poisons each block,
// and each allocation unpoisons the bytes that it gives
// (foundation/os/Poison.h).  A read through a pointer from an ended epoch
// then ends the process with a report.

#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/FixedArray.h>
#include <fixy/Refined.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Poison.h>
#include <foundation/Platform.h>
#include <foundation/Saturate.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace crucible {

class ArenaEpoch;

class CRUCIBLE_OWNER Arena {
public:
    // The bytes of blocks that an arena keeps when an epoch ends.  An arena
    // that holds more frees each block but the first, so one epoch with an
    // unusual peak does not pin its peak for the life of the arena.  The value
    // is a conservative choice: it keeps the usual epochs of a scratch arena
    // in their blocks.
    static constexpr size_t RETAINED_BYTES_LIMIT = size_t{64} << 20;

    // The count of blocks that one arena can hold.  Block k holds at least
    // block_size << k bytes, so the last block cannot be reached before an
    // allocation of the size fails.
    static constexpr uint32_t MAX_BLOCKS = 64;

    // The alignment of each block, which std::malloc gives.  A larger
    // alignment costs slack bytes in the block.
    static constexpr size_t BLOCK_ALIGNMENT = alignof(std::max_align_t);

    explicit Arena(size_t block_size = size_t{1} << 20) : block_size_{block_size} {
        CRUCIBLE_PRE(::foundation::decide::positive(block_size));
        enter_block_(0, block_size_);
        // The leading 0 is the placeholder return value for a function that
        // returns void.
        CRUCIBLE_POST(0, block_size_ == block_size);
        CRUCIBLE_POST(0, cur_block_ != nullptr);
        CRUCIBLE_POST(0, offset_ == 0u);
        CRUCIBLE_POST(0, end_offset_ == block_size_);
    }

    // An open epoch holds a reference to its arena, so the arena must not end
    // first.
    ~Arena() {
        if (has_open_epoch_) [[unlikely]] {
            ::fixy::fatal("an arena ended while an epoch of it is open: end the epoch before the arena");
        }
        free_blocks_from_(0);
    }

    Arena(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
    Arena& operator=(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
    Arena(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");
    Arena& operator=(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");

    // The alignment arithmetic runs on absolute addresses, so an alignment
    // larger than the alignment of the block is satisfied too.
    //
    // gnu::alloc_size and gnu::alloc_align cannot be applied here: they take
    // the index of a scalar size_t parameter, and both parameters are class
    // wrappers. The size invariant is carried by the parameter type instead.
    //
    // An allocation has the lifetime of its arena, and a temporary arena
    // releases its memory at the end of the statement.  Each allocating
    // member is `&`-qualified, and its deleted `&&` twin refuses a call on a
    // temporary arena.  No compiler of this tree reads a lifetime attribute,
    // but each compiler reads this pair.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::malloc, gnu::returns_nonnull]]
    CRUCIBLE_INLINE void* alloc(::foundation::effects::Alloc, ::fixy::Positive<size_t> size,
                                ::fixy::PowerOfTwo<size_t> align) & noexcept {
        const size_t s = size.value();
        const size_t a = align.value();
        // The parameter types already hold these. Restating them as
        // assumptions lets the alignment arithmetic below fold to a single
        // mask rather than a test of `(a & (a - 1)) == 0`.
        [[assume(s > 0)]];
        [[assume(a != 0 && (a & (a - 1)) == 0)]];
        const uintptr_t base = std::bit_cast<uintptr_t>(cur_block_);
        const uintptr_t aligned_addr = (base + offset_ + a - 1) & ~(a - 1);
        const size_t aligned = aligned_addr - base;

        if (aligned + s <= end_offset_) [[likely]] {
            void* ptr = cur_block_ + aligned;
            offset_ = aligned + s;
            ::foundation::os::unpoison_bytes(ptr, s);
            return ptr;
        }
        return alloc_slow_(s, a);
    }

    void* alloc(::foundation::effects::Alloc, ::fixy::Positive<size_t>, ::fixy::PowerOfTwo<size_t>) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    [[nodiscard, gnu::malloc, gnu::returns_nonnull]] CRUCIBLE_INLINE void*
    alloc(::foundation::effects::Alloc a, ::fixy::Positive<size_t> size) & noexcept {
        return alloc(a, size, ::fixy::mint_refined<::fixy::power_of_two>(size_t{alignof(std::max_align_t)}));
    }

    void* alloc(::foundation::effects::Alloc, ::fixy::Positive<size_t>) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    // Returns storage only. A T that needs construction must be
    // placement-new'd by the caller.
    template <typename T>
    [[nodiscard, gnu::returns_nonnull]] CRUCIBLE_INLINE T* alloc_obj(::foundation::effects::Alloc a) & noexcept {
        static_assert(sizeof(T) > 0, "alloc_obj<T> requires complete T");
        static_assert(std::has_single_bit(alignof(T)), "alignof(T) must be a power of two");
        return static_cast<T*>(alloc(a, ::fixy::mint_refined<::fixy::positive>(size_t{sizeof(T)}),
                                     ::fixy::mint_refined<::fixy::power_of_two>(size_t{alignof(T)})));
    }

    template <typename T>
    T* alloc_obj(::foundation::effects::Alloc) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    // An overflowing element count saturates to SIZE_MAX, which makes the
    // eventual allocation fail and abort rather than wrap to a small block.
    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE T* alloc_array(::foundation::effects::Alloc a, size_t n) & noexcept {
        if (n == 0) [[unlikely]]
            return nullptr;
        const size_t nbytes = ::foundation::sat::mul_sat(n, sizeof(T));
        return static_cast<T*>(alloc(a, ::fixy::mint_refined<::fixy::positive>(nbytes),
                                     ::fixy::mint_refined<::fixy::power_of_two>(size_t{alignof(T)})));
    }

    template <typename T>
    T* alloc_array(::foundation::effects::Alloc, size_t) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    // A caller that already knows the count is nonzero uses this instead of
    // guarding a call to alloc_array. Splitting the guard between caller and
    // callee leaves two places where "nonzero count implies nonnull pointer"
    // has to be maintained, and dropping either one yields a nonzero count
    // paired with null.
    template <typename T>
    [[nodiscard, gnu::returns_nonnull]] CRUCIBLE_INLINE T* alloc_array_nonzero(::foundation::effects::Alloc a,
                                                                               size_t n) & noexcept {
        CRUCIBLE_PRE(::foundation::decide::positive(n));
        const size_t nbytes = ::foundation::sat::mul_sat(n, sizeof(T));
        return static_cast<T*>(alloc(a, ::fixy::mint_refined<::fixy::positive>(nbytes),
                                     ::fixy::mint_refined<::fixy::power_of_two>(size_t{alignof(T)})));
    }

    template <typename T>
    T* alloc_array_nonzero(::foundation::effects::Alloc, size_t) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    // The pinned variants exist alongside the raw ones rather than replacing
    // them: only a call site that wants the allocation tier fixed in the type
    // pays for the wrapper, and the raw surface stays available everywhere
    // else.
    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>
    alloc_obj_pinned(::foundation::effects::Alloc a) & noexcept {
        return ::fixy::mint_band<::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>>(alloc_obj<T>(a));
    }

    template <typename T>
    ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*> alloc_obj_pinned(::foundation::effects::Alloc) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>
    alloc_array_pinned(::foundation::effects::Alloc a, size_t n) & noexcept {
        return ::fixy::mint_band<::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>>(alloc_array<T>(a, n));
    }

    template <typename T>
    ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*> alloc_array_pinned(::foundation::effects::Alloc, size_t) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>
    alloc_array_nonzero_pinned(::foundation::effects::Alloc a, size_t n) & noexcept {
        CRUCIBLE_PRE(::foundation::decide::positive(n));
        return ::fixy::mint_band<::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*>>(alloc_array_nonzero<T>(a, n));
    }

    template <typename T>
    ::fixy::AllocClass<::fixy::AllocClassTag_v::Arena, T*> alloc_array_nonzero_pinned(::foundation::effects::Alloc,
                                                                                      size_t) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    [[nodiscard]] const char* copy_string(::foundation::effects::Alloc a, const char* src) & {
        if (src == nullptr) return nullptr;
        const size_t len = std::strlen(src) + 1;
        auto* dst = static_cast<char*>(alloc(a, ::fixy::mint_refined<::fixy::positive>(len),
                                             ::fixy::mint_refined<::fixy::power_of_two>(size_t{1})));
        std::memcpy(dst, src, len);
        return dst;
    }

    // The twin deduces the type of the source, and it declares no raw
    // pointer parameter.
    template <typename Source>
    const char* copy_string(::foundation::effects::Alloc, Source&&) && =
        delete("a temporary arena releases this memory at the end of the statement. Use an arena that has a name");

    // Gives the right to end the bytes of this arena.  The arena must be
    // empty: it is new, or the last epoch ended, so the epoch covers each
    // byte that the arena gives out until the end.  A second epoch while one
    // is open ends the process.  The body is after ArenaEpoch.
    [[nodiscard]] ArenaEpoch open_epoch() & noexcept;

    ArenaEpoch open_epoch() && = delete("a temporary arena releases its blocks at the end of the statement");

    // The bytes that the arena gave out since its start or since the last
    // epoch ended: the full size of each block before the current block,
    // and the offset in the current block.  A block that served one
    // oversized request counts in full.
    [[nodiscard, gnu::pure]] size_t total_allocated() const noexcept {
        const size_t result = ::foundation::sat::add_sat(bytes_before_cur_, offset_);
        CRUCIBLE_POST(result, result <= retained_bytes_);
        return result;
    }

    // The bytes of each block that the arena holds, also the blocks that it
    // keeps for the next epoch.
    [[nodiscard, gnu::pure]] size_t retained_bytes() const noexcept { return retained_bytes_; }

    [[nodiscard, gnu::pure]] size_t block_count() const noexcept { return block_count_; }

    [[nodiscard, gnu::pure]] bool has_open_epoch() const noexcept { return has_open_epoch_; }

private:
    friend class ArenaEpoch;

    // value << shift, or SIZE_MAX when the result does not fit.
    [[nodiscard, gnu::const]] static constexpr size_t saturating_shift_(size_t value, uint32_t shift) noexcept {
        if (shift >= 64u || value > (SIZE_MAX >> shift)) return SIZE_MAX;
        return value << shift;
    }

    CRUCIBLE_UNSAFE_BUFFER_USAGE [[gnu::noinline, gnu::cold, gnu::returns_nonnull]]
    void* alloc_slow_(size_t size, size_t align) {
        // A size near SIZE_MAX saturates instead of wrapping, so the block
        // request stays large and the allocation fails rather than handing
        // back a tiny block that the caller would then overrun.
        enter_block_(cur_index_ + 1, ::foundation::sat::add_sat(size, align));

        const uintptr_t base = std::bit_cast<uintptr_t>(cur_block_);
        const uintptr_t aligned_addr = (base + align - 1) & ~(align - 1);
        const size_t aligned = aligned_addr - base;

        void* ptr = cur_block_ + aligned;
        offset_ = aligned + size;
        ::foundation::os::unpoison_bytes(ptr, size);
        return ptr;
    }

    // Makes the first block from first_index that holds needed bytes the
    // current block.  A block that an earlier epoch left and that holds
    // needed bytes serves again, and a smaller one stays for a later epoch.
    // When no such block is in the list, a new block goes at its end.  Block
    // k holds at least block_size << k bytes, which bounds the count of
    // blocks.  The constructor calls this on each path, so it has no cold
    // attribute: a cold callee there makes GCC compile each function that
    // makes an arena as an unlikely function.  alloc_slow_ keeps the slow
    // path cold.
    void enter_block_(uint32_t first_index, size_t needed) {
        uint32_t index = first_index;
        while (index < block_count_ && block_bytes_[index] < needed) {
            ++index;
        }
        if (index == block_count_) {
            if (index >= MAX_BLOCKS) [[unlikely]] {
                ::fixy::fatal("an arena needs more than its 64 blocks: each block doubles, so the size saturated");
            }
            const size_t standard = saturating_shift_(block_size_, index);
            const size_t wanted = needed > standard ? needed : standard;
            blocks_[index] = static_cast<std::byte*>(std::malloc(wanted));
            if (blocks_[index] == nullptr) [[unlikely]] {
                ::fixy::fatal("an arena could not allocate a block: the host has no memory left");
            }
            block_bytes_[index] = wanted;
            block_count_ = index + 1;
            retained_bytes_ = ::foundation::sat::add_sat(retained_bytes_, wanted);
        }
        if (first_index > 0) {
            bytes_before_cur_ = ::foundation::sat::add_sat(bytes_before_cur_, end_offset_);
        }
        cur_index_ = index;
        cur_block_ = static_cast<char*>(static_cast<void*>(blocks_[index]));
        offset_ = 0;
        end_offset_ = block_bytes_[index];
        CRUCIBLE_POST(0, end_offset_ >= needed);
        CRUCIBLE_POST(0, offset_ == 0u);
    }

    // Frees each block from first_index to the end of the list.  The epoch
    // can leave a block poisoned, so the block becomes addressable first.
    void free_blocks_from_(uint32_t first_index) noexcept {
        for (uint32_t index = first_index; index < block_count_; ++index) {
            unpoison_block_(index);
            std::free(blocks_[index]);
            retained_bytes_ -= block_bytes_[index];
            blocks_[index] = nullptr;
            block_bytes_[index] = 0;
        }
        if (first_index < block_count_) block_count_ = first_index;
    }

    void poison_block_(uint32_t index) noexcept { ::foundation::os::poison_bytes(blocks_[index], block_bytes_[index]); }

    void unpoison_block_(uint32_t index) noexcept {
        ::foundation::os::unpoison_bytes(blocks_[index], block_bytes_[index]);
    }

    // The body of ArenaEpoch::end: each byte of the arena dies.  Above the
    // limit, each block but the first frees its storage.  The blocks that
    // stay are poisoned, and the first block becomes the current block.
    void end_epoch_() noexcept {
        if (retained_bytes_ > RETAINED_BYTES_LIMIT) free_blocks_from_(1);
        for (uint32_t index = 0; index < block_count_; ++index) {
            poison_block_(index);
        }
        cur_index_ = 0;
        cur_block_ = static_cast<char*>(static_cast<void*>(blocks_[0]));
        offset_ = 0;
        end_offset_ = block_bytes_[0];
        bytes_before_cur_ = 0;
        has_open_epoch_ = false;
        CRUCIBLE_POST(0, total_allocated() == 0u);
        CRUCIBLE_POST(0, retained_bytes_ <= RETAINED_BYTES_LIMIT || block_count_ == 1u);
    }

    // Hot fields, read on every alloc, kept first so they share a line.
    char* cur_block_ = nullptr;
    size_t offset_ = 0;
    size_t end_offset_ = 0;

    // Cold fields, read on the slow path, by the epoch and by the size
    // queries only.
    size_t block_size_ = 0;
    size_t bytes_before_cur_ = 0;
    size_t retained_bytes_ = 0;
    uint32_t cur_index_ = 0;
    uint32_t block_count_ = 0;
    bool has_open_epoch_ = false;
    // The blocks in the order of their allocation, and the bytes of each.
    ::fixy::FixedArray<std::byte*, MAX_BLOCKS> blocks_{};
    ::fixy::FixedArray<size_t, MAX_BLOCKS> block_bytes_{};
};

// The right to end the bytes of one arena (a linear capability).
//
// Arena::open_epoch() is the one way to get it.  It cannot be copied, and a
// move leaves the source with no epoch.  std::move(epoch).end() is the one
// way to use it: each byte that the arena gave out in the epoch dies, and the
// arena keeps its blocks for the next epoch.  A token that still holds an
// open epoch when it ends stops the process, because the code that dropped
// it can still hold pointers into the epoch.  The token holds a reference to
// its arena, so it must not outlive the arena.
class [[nodiscard]] ArenaEpoch {
public:
    ArenaEpoch(ArenaEpoch&& other) noexcept : arena_{other.arena_}, is_open_{other.is_open_} { other.is_open_ = false; }

    ArenaEpoch(const ArenaEpoch&) = delete("an epoch ends one time: a copy would end it two times");
    ArenaEpoch& operator=(const ArenaEpoch&) = delete("an epoch ends one time: a copy would end it two times");
    ArenaEpoch& operator=(ArenaEpoch&&) = delete("an epoch binds one arena for life: build a new token instead");

    // The end runs before the report, so the arena is whole while the report
    // runs.
    ~ArenaEpoch() {
        if (is_open_) [[unlikely]] {
            is_open_ = false;
            arena_.end_epoch_();
            ::fixy::fatal("an epoch of an arena was dropped with no end: call std::move(epoch).end()");
        }
    }

    // Ends the epoch.  Each pointer that the arena gave out in the epoch is
    // dead after the call.
    void end() && noexcept {
        if (!is_open_) [[unlikely]] {
            ::fixy::fatal("end of an epoch that holds no open epoch: the token was moved from");
        }
        is_open_ = false;
        arena_.end_epoch_();
    }

    void end() & = delete("an epoch ends one time: write std::move(epoch).end()");

private:
    friend class Arena;

    explicit ArenaEpoch(Arena& arena) noexcept : arena_{arena} {}

    Arena& arena_;
    bool is_open_ = true;
};

inline ArenaEpoch Arena::open_epoch() & noexcept {
    if (has_open_epoch_) [[unlikely]] {
        ::fixy::fatal("open_epoch of an arena with an open epoch: end the epoch before the next one opens");
    }
    if (cur_index_ != 0 || offset_ != 0) [[unlikely]] {
        ::fixy::fatal("open_epoch of an arena that holds bytes: open the epoch before the first allocation");
    }
    has_open_epoch_ = true;
    return ArenaEpoch{*this};
}

}  // namespace crucible
