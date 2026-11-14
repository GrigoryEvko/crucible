#pragma once

#include <crucible/ForegroundCtx.h>
#include <crucible/RegistrationSeal.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible {

static constexpr uint32_t SCHEMA_TABLE_CAP = 512;

// The longest name that the table holds, in characters.  A PyTorch operator
// name with its overload is shorter than 256 characters.
inline constexpr std::size_t SCHEMA_NAME_MAX = 4096;

// The name holds its characters and one zero after them, so a C caller reads
// the name through foreign_c_str.  The text of an entry that no registration
// wrote holds no storage.
struct SchemaEntry {
    SchemaHash hash;
    ::fixy::GrowText name = ::fixy::mint_grow_text(SCHEMA_NAME_MAX + 1);
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

    SchemaTable(const SchemaTable&) = delete("owns the storage of its names");
    SchemaTable& operator=(const SchemaTable&) = delete("owns the storage of its names");
    SchemaTable(SchemaTable&&) = delete("owns the storage of its names");
    SchemaTable& operator=(SchemaTable&&) = delete("owns the storage of its names");

    ~SchemaTable() = default;

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
    [[nodiscard]] ::fixy::Option<MutableView> mint_mutable_view(VigilFgCtx const& fg) const noexcept {
        ::foundation::effects::host::require_brand_thread(fg);
        if (is_sealed()) return ::fixy::none;
        return ::fixy::Option<MutableView>::some(::fixy::mint_view<schema_state::Mutable>(*this));
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
    using Name = ::fixy::Tagged<::fixy::TextView, Tag>;
    using SanitizedName = Name<::fixy::tags::source::Sanitized>;
    using InternalName = Name<::fixy::tags::source::FromInternal>;

    // A looked-up name is a C text in the storage of the table, and the
    // table is the interning owner that made it canonical for its hash.  It
    // claims neither provenance of the registration, because the table keeps
    // one copy per hash whichever producer wrote it.  A registration of the
    // same hash again ends the storage of the old copy.
    using LookupName = ::fixy::Tagged<::fixy::CText, ::fixy::tags::source::Interned>;

    // The view's type is the proof that the table was not sealed when the
    // view was minted.  The seal can still land between the mint and the
    // write, so the write runs inside the section of the seal.  It returns
    // false, and writes nothing, when the table was sealed first.  An empty
    // name is not a registration, and it returns true without a write.
    // The view names the table it proves, and a view of another table
    // proves nothing about this one, so that is checked in every build mode.
    template <SchemaNameSource Tag>
    [[nodiscard]] bool register_name(MutableView const& view, SchemaHash hash, Name<Tag> const& name_tag) {
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        const ::fixy::TextView name = name_tag.value();
        if (name.size() == 0) return true;
        return seal_.with_write_section([&] { write_name_(hash, name); });
    }

    // The unqualified form is safe in either phase. The overload taking a
    // sealed view exists so a caller holding the borrow past the registration
    // phase can say so at the call site.  A hash that the table does not hold
    // gives no value.  Complexity: O(log n) and the length of the name.
    [[nodiscard]] ::fixy::Option<LookupName> lookup(SchemaHash hash) const noexcept {
        const auto found = entries_.get(position_of_(hash));
        for (const auto& entry : found) {
            if (entry->hash == hash) return ::fixy::some(name_of(*entry));
        }
        return ::fixy::none;
    }

    [[nodiscard]] ::fixy::Option<LookupName> lookup(SealedView const&, SchemaHash hash) const noexcept {
        return lookup(hash);
    }

    // The name without the "aten::" namespace, in the storage of the same
    // copy.  The rest of a name ends with the same zero.
    [[nodiscard]] ::fixy::Option<LookupName> short_name(SchemaHash hash) const noexcept {
        constexpr ::fixy::TextView kAtenPrefix = "aten::";
        const auto found = entries_.get(position_of_(hash));
        for (const auto& entry : found) {
            if (entry->hash != hash) break;
            const ::fixy::TextView stored = entry->name.view();
            if (!::fixy::starts_with(stored, kAtenPrefix)) return ::fixy::some(name_of(*entry));
            const ::fixy::TextView rest = stored.window(kAtenPrefix.size(), stored.size() - kAtenPrefix.size())
                                              .expect("the prefix is a part of the name");
            return ::fixy::some(::fixy::mint_tagged<::fixy::tags::source::Interned>(
                ::fixy::mint_c_text(rest).expect("the table keeps one zero after each name")));
        }
        return ::fixy::none;
    }

    // The name of an entry as a C text.  The table keeps one zero after the
    // characters of each name, so the C text always exists.  Complexity:
    // the length of the name.
    [[nodiscard]] static LookupName name_of(SchemaEntry const& entry) noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(
            ::fixy::mint_c_text(entry.name.view()).expect("the table keeps one zero after each name"));
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
        entries_.clear();
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
    void write_name_(SchemaHash hash, ::fixy::TextView name) {
        SchemaEntry carried{.hash = hash, .name = copy_name_(name)};
        for (SchemaEntry& entry : entries_) {
            // Before the first change of place, each entry has a smaller hash
            // than the new one, so an equal hash is the entry of this hash.
            if (entry.hash == carried.hash) {
                ::fixy::swap(entry.name, carried.name);
                CRUCIBLE_POST(0, is_registered_(hash));
                return;
            }
            if (carried.hash < entry.hash) ::fixy::swap(entry, carried);
        }
        // A full table ends the process.  A quiet return leaves each later
        // schema unregistered, and the fallback dispatch that results shows
        // far from the registration that was lost.
        if (entries_.push(static_cast<SchemaEntry&&>(carried)).is_err()) [[unlikely]] {
            ::fixy::fatal("crucible: the schema table holds SCHEMA_TABLE_CAP names.  Raise SCHEMA_TABLE_CAP in "
                          "crucible/SchemaTable.h, or register fewer schemas");
        }
        CRUCIBLE_POST(0, is_registered_(hash));
    }

    // The characters of name before its first zero, and one zero after them.
    // A registration is cold, and its context holds no allocation capability,
    // so the copy takes the bare allocation tag.  Complexity: the length of
    // the name.
    [[nodiscard]] static ::fixy::GrowText copy_name_(::fixy::TextView name) {
        const ::fixy::Option<std::size_t> zero = ::fixy::find(name, '\0');
        const ::fixy::TextView characters = zero.match(
            [name](std::size_t position) noexcept {
                return name.window(0, position).expect("the zero is a character of the name");
            },
            [name] noexcept { return name; });
        if (characters.size() > SCHEMA_NAME_MAX) [[unlikely]] {
            ::fixy::fatal("crucible: a schema name is longer than SCHEMA_NAME_MAX characters.  Raise SCHEMA_NAME_MAX "
                          "in crucible/SchemaTable.h");
        }
        const ::foundation::effects::Alloc alloc{};
        ::fixy::GrowText copy = ::fixy::mint_grow_text(SCHEMA_NAME_MAX + 1);
        copy.append(alloc, characters).expect("the name fits below the maximum of its text");
        copy.append(alloc, '\0').expect("the zero fits below the maximum of its text");
        return copy;
    }

    // The position of the first entry whose hash is not below hash.  The
    // entries are in the order of their hashes.  Complexity: O(log n).
    [[nodiscard]] std::size_t position_of_(SchemaHash hash) const noexcept {
        return ::fixy::lower_bound_by_key(
            entries_.view(), [](const SchemaEntry& entry) noexcept { return entry.hash.raw(); }, hash.raw());
    }

    [[nodiscard]] bool is_registered_(SchemaHash hash) const noexcept { return lookup(hash).is_some(); }

    ::fixy::FixedList<SchemaEntry, SCHEMA_TABLE_CAP> entries_{};
    RegistrationSeal seal_;
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
    for (const SchemaTable::MutableView& global_view : global_table.mint_mutable_view(fg)) {
        if (global_table.register_name(global_view, hash, name)) return;
    }
    // Nothing seals the late table, so its view exists and its write lands.
    SchemaTable& late_table = late_schema_table();
    const SchemaTable::MutableView late_view =
        late_table.mint_mutable_view(fg).expect("nothing seals the late schema table, so it mints a mutable view");
    const bool was_registered_late = late_table.register_name(late_view, hash, name);
    CRUCIBLE_FATAL_INVARIANT(was_registered_late);
}

// The two lookups read the global table first and the late table second.
// While a Vigil records, call them only from the thread that holds its
// producer claim, because the late table is never sealed.
[[nodiscard]] inline ::fixy::Option<SchemaTable::LookupName> schema_name(SchemaHash hash) {
    ::fixy::Option<SchemaTable::LookupName> global_name = global_schema_table().lookup(hash);
    if (global_name.is_some()) return global_name;
    return late_schema_table().lookup(hash);
}

[[nodiscard]] inline ::fixy::Option<SchemaTable::LookupName> schema_short_name(SchemaHash hash) {
    ::fixy::Option<SchemaTable::LookupName> global_name = global_schema_table().short_name(hash);
    if (global_name.is_some()) return global_name;
    return late_schema_table().short_name(hash);
}

}  // namespace crucible
