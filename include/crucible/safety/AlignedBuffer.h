#pragma once

// ── crucible::safety::AlignedBuffer<T, Alignment> ─────────────────
//
// Move-only RAII wrapper over an aligned heap allocation of T[N].  Owns
// the storage; frees in dtor via std::free.  Replaces hand-rolled
// `T* p = static_cast<T*>(std::aligned_alloc(A, n*sizeof(T)));`
// patterns where the buffer's lifetime crosses scopes and the caller
// must pair every alloc with a free.
//
//   Axiom coverage: MemSafe, LeakSafe, NullSafe.
//   Runtime cost:   one pointer + one count (sizeof == 16 on x86_64
//                   under -O3).  No vtable, no virtual dtor.
//
// Compose with safety::Linear<AlignedBuffer<T>> at consumer sites that
// need consume-once semantics on top of move-only RAII.
//
// ── Discipline ────────────────────────────────────────────────────
//
// - Default ctor is empty (no allocation).  Use allocate(n) static
//   factory to construct a sized buffer.
// - Copy is deleted.  Move is defaulted; moved-from buffer is empty.
// - Reset / release semantics modeled after std::unique_ptr.
// - data() returns the raw aligned pointer; size() the element count.
// - span() returns std::span<T> for boundary-typed access.

#include <crucible/Platform.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T, std::size_t Alignment = alignof(T)>
class [[nodiscard]] AlignedBuffer {
    static_assert(Alignment >= alignof(T),
                  "AlignedBuffer Alignment must be >= alignof(T)");
    static_assert((Alignment & (Alignment - 1)) == 0,
                  "AlignedBuffer Alignment must be a power of two");

public:
    using value_type = T;
    using size_type  = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::AlignedBuffer";
    }

    constexpr AlignedBuffer() noexcept = default;

    // Static factory — exactly one allocation path.  Aborts on OOM
    // (CLAUDE.md §II MemSafe — Crucible never runs where OOM is
    // recoverable).
    //
    // TypeSafe/MemSafe (CLAUDE.md §II): the byte-count math is overflow-
    // checked.  A bare `count * sizeof(T)` wraps silently for large
    // `count`, producing a too-small allocation that the caller then
    // overruns (heap-buffer-overflow).  __builtin_mul_overflow detects
    // the product wrap; the subsequent alignment round-up is an
    // __builtin_add_overflow-checked sum.  Either overflow is a fatal
    // caller bug (no valid `count` of T can exceed SIZE_MAX bytes), so
    // we abort exactly as the OOM path does.
    [[nodiscard]] static AlignedBuffer allocate(size_type count) {
        if (count == 0) [[unlikely]] return AlignedBuffer{};
        size_type bytes_raw = 0;
        if (__builtin_mul_overflow(count, sizeof(T), &bytes_raw)) [[unlikely]]
            std::abort();
        // aligned_alloc requires size to be a multiple of alignment.
        size_type bytes = 0;
        if (__builtin_add_overflow(bytes_raw, Alignment - 1, &bytes)) [[unlikely]]
            std::abort();
        bytes &= ~(Alignment - 1);
        void* raw = std::aligned_alloc(Alignment, bytes);
        if (!raw) [[unlikely]] std::abort();
        return AlignedBuffer{static_cast<T*>(raw), count};
    }

    // Like allocate, but value-initializes every element (NSDMI fires).
    // For aggregate-like NSDMI types (PtrSlot { gen=0, port=0, ... },
    // SlotInfo, Edge, etc.) every default value happens to be zero,
    // which matches the historical std::calloc semantics callers depend
    // on for the gen-counter reset path.  Value-init via placement-new
    // is the InitSafe-axiom-correct primitive (CLAUDE.md §II), and
    // avoids the GCC -Werror=class-memaccess on memset of NSDMI types.
    [[nodiscard]] static AlignedBuffer allocate_zeroed(size_type count) {
        AlignedBuffer buf = allocate(count);
        if (buf.data_ != nullptr) {
            for (size_type i = 0; i < count; ++i) {
                ::new (static_cast<void*>(buf.data_ + i)) T{};
            }
        }
        return buf;
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer is move-only");

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data_{other.data_}, size_{other.size_} {
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

    ~AlignedBuffer() noexcept { reset(); }

    void reset() noexcept {
        if (data_ != nullptr) {
            std::free(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    [[nodiscard]] T*       data() noexcept       { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept     { return size_ == 0; }
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
    explicit AlignedBuffer(T* p, size_type n) noexcept
        : data_{p}, size_{n} {}

    T*        data_ = nullptr;
    size_type size_ = 0;
};

// Self-test — sizeof matches pointer + size_t (pointer-aligned struct).
static_assert(sizeof(AlignedBuffer<int>) == sizeof(void*) + sizeof(std::size_t));
static_assert(!std::is_copy_constructible_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer<int>>);

}  // namespace crucible::safety
