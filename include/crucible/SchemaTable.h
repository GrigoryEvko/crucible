#pragma once

#include <crucible/ForegroundCtx.h>
#include <crucible/Platform.h>
#include <crucible/RegistrationSeal.h>
#include <crucible/Types.h>
#include <fixy/Borrowed.h>
#include <fixy/Mutation.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
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

// The two provenances a registered name may carry.  A name that crossed a
// trust boundary (the C ABI, a trace file) is Sanitized, and that tag is
// earned: only a retag from the boundary tag reaches it, after the
// boundary's check ran.  A name that crossed no boundary (the generated op
// table, the operator schema of the libtorch this process links) is
// FromInternal.  Every other tag is refused, so a raw External name cannot
// be registered.
template <typename Tag>
concept SchemaNameSource =
    std::same_as<Tag, ::fixy::tags::source::Sanitized> || std::same_as<Tag, ::fixy::tags::source::FromInternal>;

// The entries, the count and the phase are private.  A public field is a
// write that no view and no seal can refuse.
class SchemaTable {
public:
    using SizeCounter = ::fixy::BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>;

    SchemaTable() = default;

    SchemaTable(const SchemaTable&) = delete("owns malloc'd name strings");
    SchemaTable& operator=(const SchemaTable&) = delete("owns malloc'd name strings");
    SchemaTable(SchemaTable&&) = delete("owns malloc'd name strings");
    SchemaTable& operator=(SchemaTable&&) = delete("owns malloc'd name strings");

    ~SchemaTable() { release_names_(); }

    // Waits for a registration that is in progress, then seals.  A reader
    // that observes the seal observes every registered entry
    // (crucible/RegistrationSeal.h gives the ordering argument).
    void seal() noexcept {
        seal_.seal();
        CRUCIBLE_POST(0, seal_.is_sealed());
    }

    // Not constexpr: the phase is an atomic, and no constant evaluation can
    // read it.  The same holds for every function below that calls it.
    [[nodiscard]] bool is_sealed() const noexcept { return seal_.is_sealed(); }

    using MutableView = ::fixy::ScopedView<SchemaTable, schema_state::Mutable>;
    using SealedView = ::fixy::ScopedView<SchemaTable, schema_state::Sealed>;

    // A registration writes entries that the background thread reads with no
    // lock once the table is sealed.  So the view is minted only before the
    // seal.  Past the seal it is empty, in every build mode.  The names come
    // from the ops that a Vigil records, so the view asks for the context of
    // a Vigil's producer claim.  The sealed view is for readers, and a
    // reader on the background thread holds no such claim.  A registration
    // is cold, so the view also checks the calling thread at run time.
    [[nodiscard]] std::optional<MutableView> mint_mutable_view(VigilFgCtx const& fg) const noexcept {
        ::foundation::effects::host::require_brand_thread(fg);
        if (is_sealed()) return std::nullopt;
        return ::fixy::mint_view<schema_state::Mutable>(*this);
    }

    [[nodiscard]] SealedView mint_sealed_view() const noexcept {
        CRUCIBLE_PRE(is_sealed());
        return ::fixy::mint_view<schema_state::Sealed>(*this);
    }

    // Found by argument-dependent lookup from mint_view.
    [[nodiscard]] friend bool view_ok(SchemaTable const& t, std::type_identity<schema_state::Mutable>) noexcept {
        return !t.is_sealed();
    }
    [[nodiscard]] friend bool view_ok(SchemaTable const& t, std::type_identity<schema_state::Sealed>) noexcept {
        return t.is_sealed();
    }

    template <SchemaNameSource Tag>
    using Name = ::fixy::Tagged<const char*, Tag>;
    using SanitizedName = Name<::fixy::tags::source::Sanitized>;
    using InternalName = Name<::fixy::tags::source::FromInternal>;

    // A looked-up name is a borrow of the table's own copy, and the table is
    // the interning owner that made it canonical for its hash.  It claims
    // neither provenance of the registration, because the table keeps one
    // copy per hash whichever producer wrote it.
    using BorrowedName = ::fixy::Borrowed<const char, SchemaTable>;
    using LookupName = ::fixy::Tagged<BorrowedName, ::fixy::tags::source::Interned>;

    static_assert(sizeof(LookupName) == sizeof(BorrowedName));
    static_assert(std::is_trivially_copy_constructible_v<LookupName>);

    // The view's type is the proof that the table was not sealed when the
    // view was minted.  The seal can still land between the mint and the
    // write, so the write runs inside the section of the seal.  It returns
    // false, and writes nothing, when the table was sealed first.  A null
    // name is not a registration, and it returns true without a write.
    // The view names the table it proves, and a view of another table
    // proves nothing about this one, so that is checked in every build mode.
    template <SchemaNameSource Tag>
    [[nodiscard]] bool register_name(MutableView const& view, SchemaHash hash, Name<Tag> const& name_tag) {
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        const char* name = name_tag.value();
        if (!name) return true;
        return seal_.with_write_section([&] { write_name_(hash, name); });
    }

    // The unqualified form is safe in either phase. The overload taking a
    // sealed view exists so a caller holding the borrow past the registration
    // phase can say so at the call site.
    [[nodiscard]] LookupName lookup(SchemaHash hash) const noexcept { return lookup_from_entry_(lookup_entry_(hash)); }

    [[nodiscard]] LookupName lookup(SealedView const&, SchemaHash hash) const noexcept { return lookup(hash); }

    // The name without the "aten::" namespace, as a borrow of the same copy.
    [[nodiscard]] LookupName short_name(SchemaHash hash) const noexcept {
        constexpr std::string_view kAtenPrefix = "aten::";
        const LookupName full = lookup(hash);
        const BorrowedName& view = full.value();
        if (!std::string_view{view.data(), view.size()}.starts_with(kAtenPrefix)) return full;
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(
            view.subview(kAtenPrefix.size(), view.size() - kAtenPrefix.size()));
    }

    [[nodiscard]] uint32_t count() const noexcept { return size_.get(); }

    // The registered entries, sorted by hash.  Before the seal, only the
    // registering thread may read them, because a write can run at the
    // same time on no other thread.  After the seal, any thread may read.
    [[nodiscard]] std::span<const SchemaEntry> entries() const noexcept { return {entries_.data(), size_.get()}; }

    // Empties the table and opens it again.  A sealed table that opens again
    // takes writes, so only a test that reuses one table across cases may
    // do this: it takes the test context, which code that ships cannot mint.
    // The caller makes sure that no other thread uses the table.
    void clear(::foundation::effects::Test const& test) {
        release_names_();
        seal_.reopen(test);
        CRUCIBLE_POST(0, count() == 0u);
        CRUCIBLE_POST(0, !is_sealed());
    }

private:
    // Runs inside the write section, so no seal and no other writer runs
    // at the same time.
    void write_name_(SchemaHash hash, const char* name) {
        if (SchemaEntry* existing = lookup_entry_(hash)) {
            const auto dup = duplicate_name_(name);
            std::free(std::bit_cast<char*>(existing->name));
            existing->name = dup.name;
            existing->name_len = dup.name_len;
            CRUCIBLE_POST(0, lookup_entry_(hash) != nullptr);
            return;
        }
        // Exhausting the cap aborts rather than returning quietly. A quiet
        // return leaves every later schema unregistered, and the resulting
        // fallback dispatch shows up far from the registration that was lost.
        if (size_.get() >= SCHEMA_TABLE_CAP) [[unlikely]] {
            std::fprintf(stderr,
                         "crucible: SchemaTable full (%u/%u entries); bump "
                         "SCHEMA_TABLE_CAP or audit Vessel schema registrations\n",
                         size_.get(), SCHEMA_TABLE_CAP);
            std::abort();
        }
        const auto dup = duplicate_name_(name);
        entries_[size_.get()] = {
            .hash = hash,
            .name = dup.name,
            .name_len = dup.name_len,
        };
        size_.bump();
        // The sort is what lookup's binary search depends on. Dropping it
        // leaves the new entry findable only by luck, which is why the first
        // postcondition below reads the entry back.
        std::ranges::sort(std::span<SchemaEntry>{entries_.data(), size_.get()}, {}, &SchemaEntry::hash);
        CRUCIBLE_POST(0, lookup_entry_(hash) != nullptr);
        CRUCIBLE_POST(0, size_.get() <= SCHEMA_TABLE_CAP);
    }

    // Binary search over the sorted live prefix.  One body serves the writer,
    // which gets a mutable entry, and the readers, which get a const one.
    template <typename Self>
    [[nodiscard]] auto lookup_entry_(this Self& self, SchemaHash hash) noexcept
        -> std::conditional_t<std::is_const_v<Self>, const SchemaEntry*, SchemaEntry*> {
        const std::span live{self.entries_.data(), self.size_.get()};
        const auto it = std::ranges::lower_bound(live, hash, {}, &SchemaEntry::hash);
        return (it != live.end() && it->hash == hash) ? &*it : nullptr;
    }

    [[nodiscard]] static LookupName lookup_from_entry_(const SchemaEntry* entry) noexcept {
        if (!entry || !entry->name) return ::fixy::mint_tagged<::fixy::tags::source::Interned>(BorrowedName{});
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(BorrowedName{entry->name, entry->name_len});
    }

    // This is the one place the size counter runs backwards, so it cannot be
    // assigned. Constructing a fresh counter in place re-establishes the
    // monotonicity and bound invariants from a known floor.
    void release_names_() noexcept {
        for (auto& entry : std::span<SchemaEntry>{entries_.data(), size_.get()}) {
            // The bit_cast drops const so free() accepts the pointer. The
            // allocation is our own mutable storage.
            std::free(std::bit_cast<char*>(entry.name));
            entry.name = nullptr;
            entry.name_len = 0;
        }
        std::construct_at(&size_, ::fixy::mint_bounded_monotonic<uint32_t, SCHEMA_TABLE_CAP>(0u));
    }

    std::array<SchemaEntry, SCHEMA_TABLE_CAP> entries_{};
    SizeCounter size_ = ::fixy::mint_bounded_monotonic<uint32_t, SCHEMA_TABLE_CAP>(0u);
    RegistrationSeal seal_;

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

static_assert(::fixy::no_scoped_view_field_check<SchemaTable>());

// The global is sealed before any second thread starts, so a registration
// must mint its mutable view before that point.  A view minted after it is
// empty.
[[nodiscard]] inline SchemaTable& global_schema_table() {
    static SchemaTable table;
    return table;
}

// False when the global table was sealed after the view was minted, and
// then nothing is registered.
template <SchemaNameSource Tag>
[[nodiscard]] inline bool register_schema_name(SchemaTable::MutableView const& view, SchemaHash hash,
                                               SchemaTable::Name<Tag> const& name) {
    return global_schema_table().register_name(view, hash, name);
}

[[nodiscard]] inline SchemaTable::LookupName schema_name(SchemaHash hash) { return global_schema_table().lookup(hash); }

[[nodiscard]] inline SchemaTable::LookupName schema_short_name(SchemaHash hash) {
    return global_schema_table().short_name(hash);
}

}  // namespace crucible
