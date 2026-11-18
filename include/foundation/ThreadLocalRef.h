#pragma once

// Stateless handle onto a cell of each thread, keyed by (Tag, T).
//
// Every handle naming the same Tag and T reads and writes one cell per
// thread, also when two shared libraries of the process name it, and copying
// a handle does not create a second cell.  A distinct Tag buys a distinct
// cell.  Nothing here can check that a Tag is unique to one logical slot, so
// choosing it is the caller's obligation.
//
// The handle grants nothing that its type does not name, so building one
// authorizes nothing, and the public default constructor is the one door.
// There is no mint.
//
// THE CELLS.  A thread_local of a template has a copy in each shared library,
// because the hidden visibility of the tree hides each instance.  So no
// template holds a cell.  Each thread has one table of thread_local_cell_count
// cells of thread_local_cell_bytes bytes.  The table is an inline thread_local
// variable with CRUCIBLE_PROCESS_WIDE, which the dynamic linker makes one
// object for the process.  A process-wide registry gives each (Tag, T) one
// index of the table at its first use, the same index in each library.
//
// THE KEY.  The key of the registry has 128 bits, two independent words of
// the stable name of the handle type: its stable type id (FNV-1a and fmix64,
// foundation/reflect/Hash.h), and its SipHash-2-4 under a fixed public key
// (foundation/core/SipHash.h).  The registry is inside one process and no
// peer chooses its input, so the second word guards against an accident
// and needs no secret key.  A key that meets the type id of a different
// second key ends the process at its first use, so no two handles share a
// cell silently.  A tag with no stable identity, such as a class with
// internal linkage, is a different type in each unit, so the handle refuses
// it.
//
// THE STEADY PATH.  Each handle type keeps its index in a cache of its own,
// which holds thread_local_cell_count until the first use.  After the first
// use, one access is the acquire load of the cache, which is a plain load on
// x86-64, its test, and the address of the table of the thread.  The
// registry publishes each slot with release and reads it with acquire, on
// the first use only.
//
// THE VALUES.  A cell holds bytes, so peek() copies the value out and store()
// copies it in.  A value is trivially copyable, trivially destructible and
// trivially default constructible, with a size and an alignment of at most
// thread_local_cell_bytes.  A cell starts as zero bytes, which is the value
// T{} of such a type.  A pointer to a member is not a value of a cell,
// because its null value is not zero bytes.

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Report.h>
#include <foundation/core/SipHash.h>
#include <foundation/core/StableIdentity.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation {

// The cells of each thread.  The table is static TLS of each unit that uses
// a handle, 128 bytes, half of the limit of the rule static-tls.  A raise of
// the count needs a row of that rule.
inline constexpr std::uint32_t thread_local_cell_count = 16;
inline constexpr std::size_t thread_local_cell_bytes = 8;

template <typename T>
concept ThreadLocalValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>
                        && std::is_trivially_default_constructible_v<T> && !std::is_member_pointer_v<T>
                        && sizeof(T) <= thread_local_cell_bytes && alignof(T) <= thread_local_cell_bytes;

namespace detail {

// The cells of one thread.
struct ThreadLocalTable {
    alignas(thread_local_cell_bytes) unsigned char cells[thread_local_cell_count][thread_local_cell_bytes]{};
};

// The state of a slot of the registry.
inline constexpr std::uint32_t thread_local_slot_free = 0;
inline constexpr std::uint32_t thread_local_slot_claiming = 1;
inline constexpr std::uint32_t thread_local_slot_published = 2;

// Slot I holds the key of cell I.  A thread that claims a free slot writes
// the key and then publishes the slot with release, so a reader that sees
// the slot published with acquire sees the key.
struct ThreadLocalRegistry {
    ::foundation::core::Atomic<std::uint32_t> states[thread_local_cell_count]{};
    ::foundation::core::Atomic<std::uint64_t> type_ids[thread_local_cell_count]{};
    ::foundation::core::Atomic<std::uint64_t> name_hashes[thread_local_cell_count]{};
};

CRUCIBLE_PROCESS_WIDE inline constinit ThreadLocalRegistry thread_local_registry_{};
// The initial-exec model makes the address of the table one load relative
// to the thread pointer, with no call.  The table then takes static TLS in
// each library that holds it, which the rule static-tls bounds.
CRUCIBLE_PROCESS_WIDE
[[gnu::tls_model("initial-exec")]] inline constinit thread_local ThreadLocalTable thread_local_table_{};

// The key of one handle type.
struct ThreadLocalTypeIds {
    std::uint64_t type_id = 0;
    std::uint64_t name_hash = 0;
};

// The fixed public key of the second key: the 16 characters of the text
// "ThreadLocalRef 2", one byte each.
[[nodiscard]] consteval ::foundation::core::SipKeyBytes thread_local_second_key_() noexcept {
    constexpr ::foundation::core::TextView text{"ThreadLocalRef 2"};
    ::foundation::core::SipKeyBytes key{};
    auto letter = text.begin();
    for (std::byte& byte : key) {
        byte = static_cast<std::byte>(*letter);
        ++letter;
    }
    return key;
}

// The second key of a handle type: the SipHash-2-4 of its stable name under
// the fixed key, as one little-endian word.  It is a function of the name
// that is independent of the first key, so it catches an accidental
// collision of the first key at the first use.
template <typename Handle>
[[nodiscard]] consteval std::uint64_t thread_local_second_id_() noexcept {
    ::foundation::core::SipTag const tag =
        ::foundation::core::sip_hash(thread_local_second_key_(), ::foundation::core::stable_name_text<Handle>());
    std::uint64_t word = 0;
    int shift = 0;
    for (std::byte const byte : tag) {
        word |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte)) << shift;
        shift += 8;
    }
    return word;
}

// The index of the cell of one handle type, or thread_local_cell_count before
// its first use.  The cache is a copy in each library, and the registry gives
// each copy the same index, so two threads that fill one copy at once write
// one value.
template <typename Handle>
constinit inline ::foundation::core::Atomic<std::uint32_t> thread_local_index_cache_{thread_local_cell_count};

// The index of the key in the registry.  The first use of a key claims the
// first free slot.  A thread that finds a slot in the middle of its claim
// waits for its publication, which is a few stores away.  Complexity: linear
// in thread_local_cell_count, once for each key in each library.
[[nodiscard, gnu::cold, gnu::noinline]] inline std::uint32_t
claim_thread_local_index_(ThreadLocalTypeIds key) noexcept {
    ThreadLocalRegistry& registry = thread_local_registry_;
    for (std::uint32_t index = 0; index < thread_local_cell_count; ++index) {
        ::foundation::core::Atomic<std::uint32_t>& state = registry.states[index];
        if (state.load_acquire() == thread_local_slot_free
            && state.cas_acq_rel(thread_local_slot_free, thread_local_slot_claiming).is_ok()) {
            registry.type_ids[index].store_release(key.type_id);
            registry.name_hashes[index].store_release(key.name_hash);
            state.store_release(thread_local_slot_published);
            return index;
        }
        while (state.load_acquire() != thread_local_slot_published) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (registry.type_ids[index].load_acquire() != key.type_id) continue;
        if (registry.name_hashes[index].load_acquire() != key.name_hash) {
            ::foundation::core::fatal(
                "two ThreadLocalRef types have the stable type id {} and two names, so they cannot "
                "share a cell.  Rename one tag.",
                key.type_id);
        }
        return index;
    }
    ::foundation::core::fatal(
        "the ThreadLocalRef of the stable type id {} finds no free cell: the process already has "
        "thread_local_cell_count cells.  Raise thread_local_cell_count of foundation/ThreadLocalRef.h, and add a "
        "row of the rule static-tls for the bytes of the larger table.",
        key.type_id);
}

}  // namespace detail

template <typename Tag, typename T>
    requires ThreadLocalValue<T> && ::foundation::reflect::HasStableIdentity<Tag>
class [[nodiscard]] ThreadLocalRef {
public:
    using tag_type = Tag;
    using value_type = T;

private:
    static constexpr detail::ThreadLocalTypeIds key_{
        .type_id = ::foundation::reflect::stable_type_id(^^ThreadLocalRef<Tag, T>),
        .name_hash = detail::thread_local_second_id_<ThreadLocalRef<Tag, T>>(),
    };

    [[nodiscard, gnu::always_inline]] static std::uint32_t index_() noexcept {
        ::foundation::core::Atomic<std::uint32_t>& cache = detail::thread_local_index_cache_<ThreadLocalRef>;
        std::uint32_t index = cache.load_acquire();
        if (index == thread_local_cell_count) [[unlikely]] {
            index = detail::claim_thread_local_index_(key_);
            cache.store_release(index);
        }
        [[assume(index < thread_local_cell_count)]];
        return index;
    }

    [[nodiscard, gnu::always_inline]] static unsigned char* cell_() noexcept {
        return detail::thread_local_table_.cells[index_()];
    }

public:
    // Constructing a handle does not claim a cell.  The first access does.
    constexpr ThreadLocalRef() noexcept = default;

    // The two words of the key of the cell of this handle type (THE KEY).
    [[nodiscard]] static consteval detail::ThreadLocalTypeIds cell_ids() noexcept { return key_; }

    constexpr ThreadLocalRef(ThreadLocalRef const&) noexcept = default;
    constexpr ThreadLocalRef(ThreadLocalRef&&) noexcept = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef const&) noexcept = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef&&) noexcept = default;
    ~ThreadLocalRef() = default;

    // The writes are const on the handle because the handle owns no state.
    // What they write is the cell of the thread, which a const handle is
    // still entitled to write.
    [[nodiscard]] T peek() const noexcept {
        T value{};
        __builtin_memcpy(&value, cell_(), sizeof(T));
        return value;
    }

    template <typename U>
        requires std::is_assignable_v<T&, U&&>
    void store(U&& v) const noexcept(std::is_nothrow_assignable_v<T&, U&&>) {
        T value{};
        value = std::forward<U>(v);
        __builtin_memcpy(cell_(), &value, sizeof(T));
    }

    void reset() const noexcept { store(T{}); }

    [[nodiscard]] static consteval std::string_view tag_name() noexcept { return std::meta::display_string_of(^^Tag); }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }
};

}  // namespace foundation
