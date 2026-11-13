#pragma once

#include <crucible/ForegroundCtx.h>
#include <crucible/RegistrationSeal.h>
#include <crucible/Types.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <type_traits>

namespace crucible {

static constexpr uint32_t SCHEMA_TABLE_CAP = 512;

// pad holds the four bytes after the length, so the entry has no padding
// byte.  With -ftrivial-auto-var-init=zero, GCC writes zero to each padding
// hole of each entry of an automatic table, one store for each hole.
// test/test_schema_table.cpp makes sure that the entry has no padding byte.
struct SchemaEntry {
    SchemaHash hash;
    const char* name = nullptr;  // owned: malloc'd copy, freed in clear()
    uint32_t name_len = 0;  // cached byte length; excludes trailing NUL
    uint32_t pad = 0;
};

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
    // phase can say so at the call site.  A hash that the table does not hold
    // gives the empty borrow.  Complexity: O(log n).
    [[nodiscard]] LookupName lookup(SchemaHash hash) const noexcept {
        const auto found = entries_.get(position_of_(hash));
        for (const auto& entry : found) {
            if (entry->hash == hash) {
                return ::fixy::mint_tagged<::fixy::tags::source::Interned>(BorrowedName{entry->name, entry->name_len});
            }
        }
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(BorrowedName{});
    }

    [[nodiscard]] LookupName lookup(SealedView const&, SchemaHash hash) const noexcept { return lookup(hash); }

    // The name without the "aten::" namespace, as a borrow of the same copy.
    [[nodiscard]] LookupName short_name(SchemaHash hash) const noexcept {
        constexpr ::fixy::TextView kAtenPrefix = "aten::";
        const LookupName full = lookup(hash);
        const BorrowedName& name = full.value();
        if (name.size() < kAtenPrefix.size()) return full;
        std::size_t position = 0;
        for (const char expected : kAtenPrefix) {
            if (name[position] != expected) return full;
            ++position;
        }
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(
            name.subview(kAtenPrefix.size(), name.size() - kAtenPrefix.size()));
    }

    [[nodiscard]] uint32_t count() const noexcept { return static_cast<uint32_t>(entries_.size()); }

    // The registered entries, sorted by hash.  Before the seal, only the
    // registering thread may read them, because a write can run at the
    // same time on no other thread.  After the seal, any thread may read.
    [[nodiscard]] ::fixy::View<const SchemaEntry> entries() const noexcept { return entries_.view(); }

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
    // at the same time.  One pass in the order of the hashes keeps that
    // order: the carried entry changes place with each entry of a larger
    // hash, so each such entry moves one place up, and the last one goes to
    // the end.  Complexity: O(n).
    void write_name_(SchemaHash hash, const char* name) {
        const DuplicatedName copy = duplicate_name_(name);
        SchemaEntry carried{.hash = hash, .name = copy.name, .name_len = copy.name_len};
        for (SchemaEntry& entry : entries_) {
            // Before the first change of place, each entry has a smaller hash
            // than the new one, so an equal hash is the entry of this hash.
            if (entry.hash == carried.hash) {
                release_name_(entry);
                entry.name = carried.name;
                entry.name_len = carried.name_len;
                CRUCIBLE_POST(0, is_registered_(hash));
                return;
            }
            if (carried.hash < entry.hash) ::fixy::swap(entry, carried);
        }
        // A full table ends the process.  A quiet return leaves each later
        // schema unregistered, and the fallback dispatch that results shows
        // far from the registration that was lost.
        if (entries_.push(carried).is_err()) [[unlikely]] {
            ::fixy::fatal("crucible: the schema table holds SCHEMA_TABLE_CAP names.  Raise SCHEMA_TABLE_CAP in "
                          "crucible/SchemaTable.h, or register fewer schemas");
        }
        CRUCIBLE_POST(0, is_registered_(hash));
    }

    // The position of the first entry whose hash is not below hash.  The
    // entries are in the order of their hashes.  Complexity: O(log n).
    [[nodiscard]] std::size_t position_of_(SchemaHash hash) const noexcept {
        return ::fixy::lower_bound_by_key(
            entries_.view(), [](const SchemaEntry& entry) noexcept { return entry.hash.raw(); }, hash.raw());
    }

    [[nodiscard]] bool is_registered_(SchemaHash hash) const noexcept { return lookup(hash).value().data() != nullptr; }

    // The bit_cast drops const so free() accepts the pointer.  The
    // allocation is the own mutable storage of the table.
    static void release_name_(SchemaEntry& entry) noexcept { std::free(std::bit_cast<char*>(entry.name)); }

    void release_names_() noexcept {
        for (SchemaEntry& entry : entries_) {
            release_name_(entry);
        }
        entries_.clear();
    }

    ::fixy::FixedList<SchemaEntry, SCHEMA_TABLE_CAP> entries_{};
    RegistrationSeal seal_;

    struct DuplicatedName {
        const char* name = nullptr;
        uint32_t name_len = 0;
    };

    [[nodiscard]] static DuplicatedName duplicate_name_(const char* s) {
        const auto len = std::strlen(s);
        if (len > ::fixy::max_of<uint32_t>) [[unlikely]] {
            ::fixy::fatal("crucible: a schema name is longer than 4 GiB, and the schema table refuses it");
        }
        auto* p = static_cast<char*>(std::malloc(len + 1));
        if (!p) [[unlikely]] {
            ::fixy::fatal("crucible: the schema table cannot allocate the copy of a schema name");
        }
        std::memcpy(p, s, len + 1);
        return DuplicatedName{
            .name = p,
            .name_len = static_cast<uint32_t>(len),
        };
    }
};

// The global is sealed before any second thread starts, so a registration
// must mint its mutable view before that point.  A view minted after it is
// empty.
//
// One table for the process.  A name registered in one shared library is
// read in another, and the seal that one library puts on the table holds in
// every library.
CRUCIBLE_PROCESS_WIDE [[nodiscard]] inline SchemaTable& global_schema_table() {
    static SchemaTable table;
    return table;
}

// The names that arrive after the global table is sealed.  An adapter learns
// the name of an operator when the operator first runs, and the background
// thread of the first Vigil seals the global table before that.  Nothing
// seals this table, and it takes writes only from the thread that holds the
// producer claim of a Vigil.  So only that thread reads its entries while
// the Vigil records.  One table for the process, as for the global table.
CRUCIBLE_PROCESS_WIDE [[nodiscard]] inline SchemaTable& late_schema_table() {
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

// Registers the name of one schema from the thread that holds the producer
// claim of a Vigil.  The name goes to the global table while that table takes
// writes, and to the late table after the seal, so it is never dropped.
template <SchemaNameSource Tag>
inline void register_schema_name(VigilFgCtx const& fg, SchemaHash hash, SchemaTable::Name<Tag> const& name) {
    SchemaTable& global_table = global_schema_table();
    if (const auto global_view = global_table.mint_mutable_view(fg)) {
        if (global_table.register_name(*global_view, hash, name)) return;
    }
    // Nothing seals the late table, so its view exists and its write lands.
    SchemaTable& late_table = late_schema_table();
    const auto late_view = late_table.mint_mutable_view(fg);
    CRUCIBLE_FATAL_INVARIANT(late_view.has_value());
    const bool was_registered_late = late_table.register_name(*late_view, hash, name);
    CRUCIBLE_FATAL_INVARIANT(was_registered_late);
}

// The two lookups read the global table first and the late table second.
// While a Vigil records, call them only from the thread that holds its
// producer claim, because the late table is never sealed.
[[nodiscard]] inline SchemaTable::LookupName schema_name(SchemaHash hash) {
    const SchemaTable::LookupName global_name = global_schema_table().lookup(hash);
    if (global_name.value().data() != nullptr) return global_name;
    return late_schema_table().lookup(hash);
}

[[nodiscard]] inline SchemaTable::LookupName schema_short_name(SchemaHash hash) {
    const SchemaTable::LookupName global_name = global_schema_table().short_name(hash);
    if (global_name.value().data() != nullptr) return global_name;
    return late_schema_table().short_name(hash);
}

}  // namespace crucible
