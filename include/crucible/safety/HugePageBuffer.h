#pragma once

// This owns the allocation and its huge-page alignment, and nothing else.  It
// does not advise the kernel that the region wants huge pages.  A caller that
// needs that registers the region and unregisters it around the buffer's
// lifetime, which is left explicit because the registry wants a name for it.

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/warden/Registry.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

[[noreturn]] CRUCIBLE_COLD inline void huge_page_allocation_failed_abort_(std::size_t alloc_bytes,
                                                                          std::size_t alignment) noexcept {
    using Tag = diag::HugePageAllocationFailed;
    std::fprintf(stderr,
                 "crucible: fatal contract violation: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n"
                 "  context: aligned_alloc(alignment=%zu, bytes=%zu) returned nullptr\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data(), alignment,
                 alloc_bytes);
    std::abort();
}

template <typename T>
class [[nodiscard]] HugePageBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type huge_page_bytes = ::crucible::warden::kHugePageBytes;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::HugePageBuffer"; }

    constexpr HugePageBuffer() noexcept = default;

    [[nodiscard]] static HugePageBuffer allocate(size_type count) {
        if (count == 0) [[unlikely]]
            return HugePageBuffer{};
        // Both byte computations wrap silently on a large count, and each wrap
        // hands back a buffer smaller than the caller asked for.  The round-up
        // is the subtler of the two: it adds one page less than a page before
        // masking, so a raw size within a page of the maximum adds to zero and
        // yields a zero-byte allocation.  round_probe exists only to catch that
        // add.  Neither can happen for a real count of T, so both abort.
        size_type raw_bytes = 0;
        if (__builtin_mul_overflow(count, sizeof(T), &raw_bytes)) [[unlikely]]
            std::abort();
        size_type round_probe = 0;
        if (__builtin_add_overflow(raw_bytes, huge_page_bytes - 1, &round_probe)) [[unlikely]]
            std::abort();
        const size_type alloc_bytes = ::crucible::warden::round_up_huge(raw_bytes);
        void* raw = std::aligned_alloc(huge_page_bytes, alloc_bytes);
        if (!raw) [[unlikely]] {
            huge_page_allocation_failed_abort_(alloc_bytes, huge_page_bytes);
        }
        return HugePageBuffer{static_cast<T*>(raw), count, alloc_bytes};
    }

    HugePageBuffer(const HugePageBuffer&) = delete("HugePageBuffer is move-only");
    HugePageBuffer& operator=(const HugePageBuffer&) = delete("HugePageBuffer is move-only");

    HugePageBuffer(HugePageBuffer&& other) noexcept
        : data_{other.data_}, size_{other.size_}, alloc_bytes_{other.alloc_bytes_} {
        other.data_ = nullptr;
        other.size_ = 0;
        other.alloc_bytes_ = 0;
    }

    HugePageBuffer& operator=(HugePageBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            size_ = other.size_;
            alloc_bytes_ = other.alloc_bytes_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.alloc_bytes_ = 0;
        }
        return *this;
    }

    ~HugePageBuffer() noexcept { reset(); }

    void reset() noexcept {
        if (data_ != nullptr) {
            std::free(data_);
            data_ = nullptr;
            size_ = 0;
            alloc_bytes_ = 0;
        }
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type bytes() const noexcept { return alloc_bytes_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{data_, size_}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return std::span<const T>{data_, size_}; }

    [[nodiscard]] T& operator[](size_type i) noexcept { return data_[i]; }
    [[nodiscard]] const T& operator[](size_type i) const noexcept { return data_[i]; }

private:
    explicit HugePageBuffer(T* p, size_type n, size_type b) noexcept : data_{p}, size_{n}, alloc_bytes_{b} {}

    T* data_ = nullptr;
    size_type size_ = 0;
    size_type alloc_bytes_ = 0;
};

static_assert(!std::is_copy_constructible_v<HugePageBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<HugePageBuffer<int>>);

}  // namespace crucible::safety
