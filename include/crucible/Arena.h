#pragma once

#include "effects/_Capabilities.h"
#include "Platform.h"
#include "Saturate.h"
// The umbrella header that re-exports these wrappers pulls in a header that
// includes this one and uses a complete Arena, so including the umbrella here
// cycles and leaves Arena undeclared in every consuming translation unit.
// Instead: include the narrow substrate headers and re-open the wrapper
// namespace below with the using declarations Arena needs. Naming one entity
// from two using declarations in one namespace is not a redeclaration, so the
// umbrella's own declarations stay compatible.
#include "safety/AllocClass.h"
#include "safety/_Decide.h"
#include "safety/_Mutation.h"
#include "safety/_Post.h"
#include "safety/_Pre.h"
#include "safety/Refined.h"

namespace crucible::fixy::wrap {
using ::crucible::safety::AllocClass;
using ::crucible::safety::AllocClassTag_v;
using ::crucible::safety::AppendOnly;
using ::crucible::safety::Monotonic;
using ::crucible::safety::Positive;
using ::crucible::safety::PowerOfTwo;
}  // namespace crucible::fixy::wrap

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace crucible {

class CRUCIBLE_OWNER Arena {
public:
    explicit Arena(size_t block_size = size_t{1} << 20) pre(::crucible::decide::positive(block_size))
        : block_size_{block_size} {
        alloc_new_block_(block_size_);
        // A post clause whose predicate reads a member through `this` is
        // skipped at consteval, so every postcondition in this file routes
        // through the macro instead. The leading 0 is the placeholder return
        // value for a function that returns void.
        CRUCIBLE_POST(0, block_size_ == block_size);
        CRUCIBLE_POST(0, cur_block_ != nullptr);
        CRUCIBLE_POST(0, offset_ == 0u);
        CRUCIBLE_POST(0, end_offset_ == block_size_);
    }

    ~Arena() {
        for (char* block : blocks_)
            std::free(block);
    }

    Arena(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
    Arena& operator=(const Arena&) = delete("Arena is non-copyable: interior pointers would dangle");
    Arena(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");
    Arena& operator=(Arena&&) = delete("Arena is non-movable: interior pointers would dangle");

    // The alignment arithmetic runs on absolute addresses. malloc only
    // guarantees alignof(std::max_align_t), so a stricter alignment cannot be
    // satisfied by rounding the block-relative offset alone.
    //
    // gnu::alloc_size and gnu::alloc_align cannot be applied here: they take
    // the index of a scalar size_t parameter, and both parameters are class
    // wrappers. The size invariant is carried by the parameter type instead.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::malloc, gnu::returns_nonnull]]
    CRUCIBLE_INLINE void* alloc(effects::Alloc, crucible::fixy::wrap::Positive<size_t> size,
                                crucible::fixy::wrap::PowerOfTwo<size_t> align) noexcept CRUCIBLE_LIFETIMEBOUND {
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
            return ptr;
        }
        return alloc_slow_(s, a);
    }

    [[nodiscard, gnu::malloc, gnu::returns_nonnull]] CRUCIBLE_INLINE void*
    alloc(effects::Alloc a, crucible::fixy::wrap::Positive<size_t> size) noexcept CRUCIBLE_LIFETIMEBOUND {
        return alloc(a, size, crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(std::max_align_t)});
    }

    // Returns storage only. A T that needs construction must be
    // placement-new'd by the caller.
    template <typename T>
    [[nodiscard, gnu::returns_nonnull]] CRUCIBLE_INLINE T* alloc_obj(effects::Alloc a) noexcept CRUCIBLE_LIFETIMEBOUND {
        static_assert(sizeof(T) > 0, "alloc_obj<T> requires complete T");
        static_assert(std::has_single_bit(alignof(T)), "alignof(T) must be a power of two");
        return static_cast<T*>(alloc(a, crucible::fixy::wrap::Positive<size_t>{sizeof(T)},
                                     crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(T)}));
    }

    // An overflowing element count saturates to SIZE_MAX, which makes the
    // eventual malloc fail and abort rather than wrap to a small block.
    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE T* alloc_array(effects::Alloc a, size_t n) noexcept CRUCIBLE_LIFETIMEBOUND {
        if (n == 0) [[unlikely]]
            return nullptr;
        const size_t nbytes = crucible::sat::mul_sat(n, sizeof(T));
        return static_cast<T*>(alloc(a, crucible::fixy::wrap::Positive<size_t>{nbytes},
                                     crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(T)}));
    }

    // A caller that already knows the count is nonzero uses this instead of
    // guarding a call to alloc_array. Splitting the guard between caller and
    // callee leaves two places where "nonzero count implies nonnull pointer"
    // has to be maintained, and dropping either one yields a nonzero count
    // paired with null.
    template <typename T>
    [[nodiscard, gnu::returns_nonnull]] CRUCIBLE_INLINE T* alloc_array_nonzero(effects::Alloc a, size_t n) noexcept
        CRUCIBLE_LIFETIMEBOUND pre(::crucible::decide::positive(n)) {
        [[assume(n > 0)]];
        const size_t nbytes = crucible::sat::mul_sat(n, sizeof(T));
        return static_cast<T*>(alloc(a, crucible::fixy::wrap::Positive<size_t>{nbytes},
                                     crucible::fixy::wrap::PowerOfTwo<size_t>{alignof(T)}));
    }

    // The pinned variants exist alongside the raw ones rather than replacing
    // them: only a call site that wants the allocation tier fixed in the type
    // pays for the wrapper, and the raw surface stays available everywhere
    // else.
    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>
    alloc_obj_pinned(effects::Alloc a) noexcept CRUCIBLE_LIFETIMEBOUND {
        return fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>{alloc_obj<T>(a)};
    }

    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>
    alloc_array_pinned(effects::Alloc a, size_t n) noexcept CRUCIBLE_LIFETIMEBOUND {
        return fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>{alloc_array<T>(a, n)};
    }

    template <typename T>
    [[nodiscard]] CRUCIBLE_INLINE fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>
    alloc_array_nonzero_pinned(effects::Alloc a, size_t n) noexcept
        CRUCIBLE_LIFETIMEBOUND pre(::crucible::decide::positive(n)) {
        return fixy::wrap::AllocClass<fixy::wrap::AllocClassTag_v::Arena, T*>{alloc_array_nonzero<T>(a, n)};
    }

    [[nodiscard]] const char* copy_string(effects::Alloc a, const char* src) CRUCIBLE_LIFETIMEBOUND {
        if (src == nullptr) return nullptr;
        const size_t len = std::strlen(src) + 1;
        auto* dst = static_cast<char*>(
            alloc(a, crucible::fixy::wrap::Positive<size_t>{len}, crucible::fixy::wrap::PowerOfTwo<size_t>{1}));
        std::memcpy(dst, src, len);
        return dst;
    }

    // Counts the full size of every block, including the whole of a block
    // that was sized for one oversized request, and subtracts only the unused
    // tail of the current block. A single one-megabyte request served by a
    // thirty-two-byte arena therefore reports about one megabyte.
    //
    // Each new block adds its exact size to the running total and offset_
    // never passes end_offset_, so the chain below holds and the subtraction
    // cannot underflow.
    [[nodiscard, gnu::pure]] size_t total_allocated() const noexcept {
        const std::array<size_t, 3> chain = {offset_, end_offset_, total_block_bytes_.get()};
        CRUCIBLE_PRE(::crucible::decide::weakly_increasing(std::span<const size_t>(chain)));
        const size_t result = total_block_bytes_.get() - (end_offset_ - offset_);
        CRUCIBLE_POST(result, result <= total_block_bytes_.get());
        return result;
    }

    [[nodiscard, gnu::pure]] size_t block_count() const noexcept { return blocks_.size(); }

private:
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[gnu::noinline, gnu::cold, gnu::returns_nonnull]]
    void* alloc_slow_(size_t size, size_t align) {
        // A size near SIZE_MAX saturates instead of wrapping, so the block
        // request stays large and malloc fails rather than handing back a
        // tiny block that the caller would then overrun.
        const size_t needed = crucible::sat::add_sat(size, align);
        const size_t new_size = (needed > block_size_) ? needed : block_size_;
        alloc_new_block_(new_size);

        const uintptr_t base = std::bit_cast<uintptr_t>(cur_block_);
        const uintptr_t aligned_addr = (base + align - 1) & ~(align - 1);
        const size_t aligned = aligned_addr - base;

        void* ptr = cur_block_ + aligned;
        offset_ = aligned + size;
        return ptr;
    }

    [[gnu::cold]]
    void alloc_new_block_(size_t nbytes) pre(::crucible::decide::positive(nbytes)) {
        auto* p = static_cast<char*>(std::malloc(nbytes));
        if (p == nullptr) [[unlikely]]
            std::abort();
        blocks_.append(p);

        cur_block_ = p;
        offset_ = 0;
        end_offset_ = nbytes;

        // advance() rejects a decrease. A saturating add never decreases, so
        // the contract holds for any nbytes, including one that saturates.
        total_block_bytes_.advance(crucible::sat::add_sat(total_block_bytes_.get(), nbytes));
        CRUCIBLE_POST(0, cur_block_ == p);
        CRUCIBLE_POST(0, offset_ == 0u);
        CRUCIBLE_POST(0, end_offset_ == nbytes);
    }

    // Hot fields, read on every alloc, kept first so they share a line.
    char* cur_block_ = nullptr;
    size_t offset_ = 0;
    size_t end_offset_ = 0;

    // Cold fields, read on the slow path and by the size queries only.
    size_t block_size_ = 0;
    crucible::fixy::wrap::Monotonic<size_t> total_block_bytes_{0};
    crucible::fixy::wrap::AppendOnly<char*> blocks_{};
};

static_assert(sizeof(Arena) == 64, "Arena must fit within one cache line");

}  // namespace crucible
