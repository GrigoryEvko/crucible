#pragma once

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Post.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>

namespace crucible {

static constexpr uint32_t SCHEMA_TABLE_CAP = 512;

struct SchemaEntry {
    SchemaHash hash;
    const char* name = nullptr;  // owned: malloc'd copy, freed in clear()
    uint32_t name_len = 0;  // cached byte length; excludes trailing NUL
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaEntry);

namespace schema_state {
struct Mutable {};
struct Sealed {};
}  // namespace schema_state

struct SchemaTable {
    using SizeCounter = ::crucible::fixy::wrap::BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>;

    SchemaEntry entries[SCHEMA_TABLE_CAP]{};
    SizeCounter size{0u};

    // The release store here pairs with the acquire load in is_sealed(). A
    // thread that observes the sealed state therefore also observes every
    // entries[] write made before the seal, which is what lets a reader run
    // concurrently with nothing but a plain load.
    std::atomic<bool> sealed_{false};

    SchemaTable() = default;

    SchemaTable(const SchemaTable&) = delete("owns malloc'd name strings");
    SchemaTable& operator=(const SchemaTable&) = delete("owns malloc'd name strings");
    SchemaTable(SchemaTable&&) = delete("owns malloc'd name strings");
    SchemaTable& operator=(SchemaTable&&) = delete("owns malloc'd name strings");

    ~SchemaTable() { clear(); }

    void seal() noexcept {
        sealed_.store(true, std::memory_order_release);
        CRUCIBLE_POST(0, sealed_.load(std::memory_order_acquire));
    }

    [[nodiscard]] bool is_sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }

    using MutableView = crucible::fixy::wrap::ScopedView<SchemaTable, schema_state::Mutable>;
    using SealedView = crucible::fixy::wrap::ScopedView<SchemaTable, schema_state::Sealed>;

    [[nodiscard]] MutableView mint_mutable_view() const noexcept {
        CRUCIBLE_PRE(!is_sealed());
        return crucible::fixy::wrap::mint_view<schema_state::Mutable>(*this);
    }

    [[nodiscard]] SealedView mint_sealed_view() const noexcept {
        CRUCIBLE_PRE(is_sealed());
        return crucible::fixy::wrap::mint_view<schema_state::Sealed>(*this);
    }

    // Found by argument-dependent lookup from the view-minting template.
    [[nodiscard]] friend constexpr bool view_ok(SchemaTable const& t,
                                                std::type_identity<schema_state::Mutable>) noexcept {
        return !t.is_sealed();
    }
    [[nodiscard]] friend constexpr bool view_ok(SchemaTable const& t,
                                                std::type_identity<schema_state::Sealed>) noexcept {
        return t.is_sealed();
    }

    using SanitizedName = crucible::fixy::wrap::Tagged<const char*, crucible::fixy::tags::source::Sanitized>;
    using BorrowedName = crucible::fixy::wrap::Borrowed<const char, SchemaTable>;
    using LookupName = crucible::fixy::wrap::Tagged<BorrowedName, crucible::fixy::tags::source::Sanitized>;

    static_assert(sizeof(LookupName) == sizeof(BorrowedName));
    static_assert(std::is_trivially_copy_constructible_v<LookupName>);

    // The view parameter is unnamed and unread. Its type is the proof that
    // the table is not sealed, so no runtime phase check is needed.
    void register_name(MutableView const&, SchemaHash hash, SanitizedName name_tag) {
        const char* name = name_tag.value();
        if (!name) return;

        for (uint32_t i = 0; i < size.get(); i++) {
            if (entries[i].hash == hash) {
                const auto dup = duplicate_name_(name);
                std::free(std::bit_cast<char*>(entries[i].name));
                entries[i].name = dup.name;
                entries[i].name_len = dup.name_len;
                CRUCIBLE_POST(0, lookup_raw_(hash) != nullptr);
                return;
            }
        }
        // Exhausting the cap aborts rather than returning quietly. A quiet
        // return leaves every later schema unregistered, and the resulting
        // fallback dispatch shows up far from the registration that was lost.
        if (size.get() >= SCHEMA_TABLE_CAP) [[unlikely]] {
            std::fprintf(stderr,
                         "crucible: SchemaTable full (%u/%u entries); bump "
                         "SCHEMA_TABLE_CAP or audit Vessel schema registrations\n",
                         size.get(), SCHEMA_TABLE_CAP);
            std::abort();
        }
        const auto dup = duplicate_name_(name);
        entries[size.get()] = {
            .hash = hash,
            .name = dup.name,
            .name_len = dup.name_len,
        };
        size.bump();
        // The sort is what lookup's binary search depends on. Dropping it
        // leaves the new entry findable only by luck, which is why the first
        // postcondition below reads the entry back.
        std::ranges::sort(std::span<SchemaEntry>{entries, size.get()}, {}, &SchemaEntry::hash);
        CRUCIBLE_POST(0, lookup_raw_(hash) != nullptr);
        CRUCIBLE_POST(0, size.get() <= SCHEMA_TABLE_CAP);
    }

    // The unqualified form is safe in either phase. The overload taking a
    // sealed view exists so a caller holding the borrow past the registration
    // phase can say so at the call site.
    [[nodiscard]] LookupName lookup(SchemaHash hash) const noexcept { return lookup_from_entry_(lookup_entry_(hash)); }

    [[nodiscard]] LookupName lookup(SealedView const&, SchemaHash hash) const noexcept { return lookup(hash); }

private:
    [[nodiscard]] const char* lookup_raw_(SchemaHash hash) const noexcept {
        const SchemaEntry* entry = lookup_entry_(hash);
        return entry ? entry->name : nullptr;
    }

    [[nodiscard]] const SchemaEntry* lookup_entry_(SchemaHash hash) const noexcept {
        uint32_t lo = 0, hi = size.get();
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (entries[mid].hash == hash) return &entries[mid];
            if (entries[mid].hash < hash)
                lo = mid + 1;
            else
                hi = mid;
        }
        return nullptr;
    }

    [[nodiscard]] static LookupName lookup_from_entry_(const SchemaEntry* entry) noexcept {
        if (!entry || !entry->name) return LookupName{BorrowedName{}};
        return LookupName{BorrowedName{entry->name, entry->name_len}};
    }

public:
    [[nodiscard]] LookupName short_name(SchemaHash hash) const noexcept {
        LookupName full = lookup(hash);
        const BorrowedName& view = full.value();
        constexpr char prefix[] = "aten::";
        constexpr uint32_t prefix_len = 6;
        if (view.size() >= prefix_len && std::memcmp(view.data(), prefix, prefix_len) == 0) {
            return LookupName{view.subview(prefix_len, view.size() - prefix_len)};
        }
        return full;
    }

    [[nodiscard]] uint32_t count() const { return size.get(); }

    // This is the one place the size counter runs backwards, so it cannot be
    // assigned. Constructing a fresh counter in place re-establishes the
    // monotonicity and bound invariants from a known floor.
    void clear() {
        for (uint32_t i = 0; i < size.get(); i++) {
            // The bit_cast drops const so free() accepts the pointer. The
            // allocation is our own mutable storage.
            std::free(std::bit_cast<char*>(entries[i].name));
            entries[i].name = nullptr;
            entries[i].name_len = 0;
        }
        std::construct_at(&size, SizeCounter{0u});
        sealed_.store(false, std::memory_order_release);
    }

private:
    struct DuplicatedName {
        const char* name = nullptr;
        uint32_t name_len = 0;
    };

    [[nodiscard]] static DuplicatedName duplicate_name_(const char* s) {
        const auto len = std::strlen(s);
        if (len > std::numeric_limits<uint32_t>::max()) [[unlikely]]
            std::abort();
        auto* p = static_cast<char*>(std::malloc(len + 1));
        if (!p) [[unlikely]]
            std::abort();
        std::memcpy(p, s, len + 1);
        return DuplicatedName{
            .name = p,
            .name_len = static_cast<uint32_t>(len),
        };
    }
};

static_assert(crucible::fixy::wrap::no_scoped_view_field_check<SchemaTable>());

// The global is sealed before any second thread starts, so a registration
// must mint its mutable view before that point.
[[nodiscard]] inline SchemaTable& global_schema_table() {
    static SchemaTable table;
    return table;
}

inline void register_schema_name(SchemaTable::MutableView const& view, SchemaHash hash,
                                 SchemaTable::SanitizedName name) {
    global_schema_table().register_name(view, hash, name);
}

[[nodiscard]] inline SchemaTable::LookupName schema_name(SchemaHash hash) { return global_schema_table().lookup(hash); }

[[nodiscard]] inline SchemaTable::LookupName schema_short_name(SchemaHash hash) {
    return global_schema_table().short_name(hash);
}

}  // namespace crucible
