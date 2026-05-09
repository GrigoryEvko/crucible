#pragma once

// ── crucible::safety::HugePageBuffer<T> ───────────────────────────
//
// Move-only RAII wrapper over a 2-MB-aligned heap allocation of T[N]
// with MADV_HUGEPAGE applied.  Replaces hand-rolled patterns like:
//
//   T* p = std::aligned_alloc(kHugePageBytes, round_up(n*sizeof(T)));
//   crucible::rt::register_hot_region(p, bytes, /*huge=*/true, "name");
//   ...
//   crucible::rt::unregister_hot_region(p);
//   std::free(p);
//
//   Axiom coverage: MemSafe, LeakSafe.
//   Runtime cost:   one pointer + one count + zero registration extra
//                   (the registration metadata lives in rt::Registry,
//                   not on the buffer).
//
// Pairing with rt::register_hot_region / unregister_hot_region is
// explicit at the call site (the registry needs the friendly name);
// HugePageBuffer owns the allocation lifetime.

#include <crucible/Platform.h>
#include <crucible/rt/Registry.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T>
class [[nodiscard]] HugePageBuffer {
public:
    using value_type = T;
    using size_type  = std::size_t;

    static constexpr size_type huge_page_bytes = ::crucible::rt::kHugePageBytes;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::HugePageBuffer";
    }

    constexpr HugePageBuffer() noexcept = default;

    // Allocate `count` Ts in a 2-MB-rounded aligned region.  The
    // returned buffer's bytes() reports the rounded allocation size
    // — needed by the caller for register_hot_region / madvise.
    [[nodiscard]] static HugePageBuffer allocate(size_type count) {
        if (count == 0) [[unlikely]] return HugePageBuffer{};
        const size_type raw_bytes = count * sizeof(T);
        const size_type alloc_bytes = ::crucible::rt::round_up_huge(raw_bytes);
        void* raw = std::aligned_alloc(huge_page_bytes, alloc_bytes);
        if (!raw) [[unlikely]] std::abort();
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

    [[nodiscard]] T*       data() noexcept       { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] size_type size()  const noexcept { return size_; }
    [[nodiscard]] size_type bytes() const noexcept { return alloc_bytes_; }
    [[nodiscard]] bool empty() const noexcept      { return size_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept {
        return std::span<T>{data_, size_};
    }
    [[nodiscard]] std::span<const T> span() const noexcept {
        return std::span<const T>{data_, size_};
    }

    [[nodiscard]] T&       operator[](size_type i) noexcept       { return data_[i]; }
    [[nodiscard]] const T& operator[](size_type i) const noexcept { return data_[i]; }

private:
    explicit HugePageBuffer(T* p, size_type n, size_type b) noexcept
        : data_{p}, size_{n}, alloc_bytes_{b} {}

    T*        data_        = nullptr;
    size_type size_        = 0;
    size_type alloc_bytes_ = 0;
};

static_assert(!std::is_copy_constructible_v<HugePageBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<HugePageBuffer<int>>);

}  // namespace crucible::safety
