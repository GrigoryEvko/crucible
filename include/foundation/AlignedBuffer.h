#pragma once

// An owned allocation of `count` elements of T at an alignment of the
// caller's choice.  The buffer owns the storage alone: a move hands it on and
// leaves the source empty, and the destructor frees it.  T is trivially
// destructible, so freeing the storage ends the elements.
//
// Each element starts its lifetime in one of two ways, and each allocation
// route states what it needs:
//
//   allocate                     the checked start over the bytes, so T is
//                                an implicit-lifetime type throughout
//   allocate_value_initialized   a value initialization of each element, so
//                                T has a default constructor that does not
//                                throw
//
// A proof type, or a class that holds one, has neither.  A class that
// refuses a start over bytes, such as a provenance tag, can still hold a
// value that its own default constructor built.
//
// A huge-page buffer is this buffer at huge_page_bytes alignment.  The
// alignment is necessary for the kernel to back the region with huge pages
// and not sufficient: nothing here advises the kernel.  A caller that wants
// huge pages registers the region and advises it separately.

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/Catalog.h>

#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

namespace foundation {

// The size of one huge page on the supported targets: x86-64, and aarch64
// with 4 KiB base pages.
inline constexpr std::size_t huge_page_bytes = std::size_t{2} << 20;

namespace detail {

template <typename Tag>
CRUCIBLE_COLD inline void print_diagnostic_() noexcept {
    std::fprintf(stderr, "  %.*s: %.*s\n  remediation: %.*s\n", static_cast<int>(Tag::name.size()), Tag::name.data(),
                 static_cast<int>(Tag::description.size()), Tag::description.data(),
                 static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
}

[[noreturn]] CRUCIBLE_COLD inline void aligned_allocation_failed_abort_(std::size_t alloc_bytes,
                                                                        std::size_t alignment) noexcept {
    std::fprintf(stderr, "crucible: fatal: aligned_alloc(alignment=%zu, bytes=%zu) returned nullptr\n", alignment,
                 alloc_bytes);
    if (alignment >= huge_page_bytes) print_diagnostic_<::foundation::diag::HugePageAllocationFailed>();
    std::abort();
}

[[noreturn]] CRUCIBLE_COLD inline void allocation_size_overflow_abort_(std::size_t prefix_bytes, std::size_t count,
                                                                       std::size_t element_bytes,
                                                                       std::size_t alignment) noexcept {
    std::fprintf(stderr,
                 "crucible: fatal: %zu prefix bytes and %zu elements of %zu bytes, rounded to %zu, do not fit in "
                 "size_t\n",
                 prefix_bytes, count, element_bytes, alignment);
    print_diagnostic_<::foundation::diag::AllocationSizeOverflow>();
    std::abort();
}

// The bytes of one allocation: a prefix, then `count` elements of
// `element_bytes` each, rounded up to the alignment, which aligned_alloc
// requires.  Each step wraps on a count that no real allocation has, and
// a wrap would hand back a block smaller than asked for, so a wrap
// aborts.  The checks are plain code and not contract clauses, so they
// hold under every contract evaluation semantic.  AlignedBuffer and
// SwissTableBuffer compute their sizes here.  Complexity: O(1).
template <std::size_t Alignment>
    requires(std::has_single_bit(Alignment))
[[nodiscard]] constexpr std::size_t aligned_allocation_bytes(std::size_t prefix_bytes, std::size_t count,
                                                             std::size_t element_bytes) noexcept {
    std::size_t element_total = 0;
    std::size_t total = 0;
    std::size_t padded = 0;
    const bool wraps = __builtin_mul_overflow(count, element_bytes, &element_total)
                    || __builtin_add_overflow(prefix_bytes, element_total, &total)
                    || __builtin_add_overflow(total, Alignment - 1, &padded);
    if (wraps) [[unlikely]]
        allocation_size_overflow_abort_(prefix_bytes, count, element_bytes, Alignment);
    return padded & ~(Alignment - 1);
}

// Fresh storage of `bytes` at the alignment.  Exhaustion aborts: this runs
// where a failed allocation has no recovery.
[[nodiscard]] inline void* allocate_aligned_storage_(std::size_t alignment, std::size_t bytes) {
    void* const raw = std::aligned_alloc(alignment, bytes);
    if (raw == nullptr) [[unlikely]]
        aligned_allocation_failed_abort_(bytes, alignment);
    return raw;
}

}  // namespace detail

template <typename T, std::size_t Alignment = alignof(T)>
    requires(std::is_object_v<T> && std::is_trivially_destructible_v<T> && std::has_single_bit(Alignment)
             && Alignment >= alignof(T))
class [[nodiscard]] AlignedBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::AlignedBuffer"; }

    constexpr AlignedBuffer() noexcept = default;

    // The bytes the allocation of `count` elements occupies: the element
    // bytes rounded up to the alignment.  A wrap aborts through the
    // AllocationSizeOverflow diagnostic.
    [[nodiscard]] static constexpr size_type allocation_bytes(size_type count) noexcept {
        return detail::aligned_allocation_bytes<Alignment>(0, count, sizeof(T));
    }

    // Starts the lifetime of `count` elements over fresh storage.  An
    // element holds an erroneous value until it is written.  Exhaustion
    // aborts: this runs where a failed allocation has no recovery.
    [[nodiscard]] static AlignedBuffer allocate(size_type count)
        requires lifetime::ImplicitLifetimeThroughout<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        void* const raw = allocate_storage_(count);
        return AlignedBuffer{lifetime::start_as_array<T>(raw, count).data(), count};
    }

    // Value-initializes each element, so a T with member initializers gets
    // the values it declares, and every other T reads zero.  Complexity:
    // linear in count.
    [[nodiscard]] static AlignedBuffer allocate_value_initialized(size_type count)
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        T* const first = static_cast<T*>(allocate_storage_(count));
        for (size_type index = 0; index < count; ++index)
            ::new(static_cast<void*>(first + index)) T();
        return AlignedBuffer{first, count};
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");

    AlignedBuffer(AlignedBuffer&& other) noexcept : data_{other.data_}, size_{other.size_} {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // The destructor and reset() are constexpr, so a class with an empty
    // buffer member stays a literal type.  No constant evaluation allocates,
    // because allocate() is not constexpr, so it never reaches the free.
    constexpr ~AlignedBuffer() noexcept { reset(); }

    constexpr void reset() noexcept {
        if (data_ != nullptr) {
            std::free(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type bytes() const noexcept { return size_ == 0 ? 0 : allocation_bytes(size_); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{data_, size_}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return std::span<const T>{data_, size_}; }

    [[nodiscard]] T& operator[](size_type i) noexcept {
        CRUCIBLE_PRE(i < size_);
        return data_[i];
    }
    [[nodiscard]] const T& operator[](size_type i) const noexcept {
        CRUCIBLE_PRE(i < size_);
        return data_[i];
    }

private:
    explicit AlignedBuffer(T* data, size_type size) noexcept : data_{data}, size_{size} {}

    [[nodiscard]] static void* allocate_storage_(size_type count) {
        return detail::allocate_aligned_storage_(Alignment, allocation_bytes(count));
    }

    T* data_ = nullptr;
    size_type size_ = 0;
};

}  // namespace foundation
