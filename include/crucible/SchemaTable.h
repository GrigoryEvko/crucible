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
// the name through foreign_c_str.  The name is a HeapText, so no append
// changes it, and a C text of it lives as long as the entry keeps it.  The
// text of an entry that no registration wrote holds no storage.
struct SchemaEntry {
    SchemaHash hash;
    ::fixy::HeapText name = ::fixy::freeze(::fixy::mint_grow_text(0));
};

namespace schema_state {
struct Mutable {};
struct Sealed {};
}  // namespace schema_state

// The tags of a name that crosses a trust boundary: a trace file gives an
// External name, and the C interface gives an ABIBoundary name.
template <typename Tag>
concept SchemaNameBoundary =
    std::same_as<Tag, ::fixy::tags::source::External> || std::same_as<Tag, ::fixy::tags::source::ABIBoundary>;

// The two types of a name that a table registers (SchemaNameSource).  The
// table is a parameter, so the class SchemaTable can read the concept before
// it is complete.  A concept has no specialization, so no unit can admit a
// third type.
template <typename NameType, typename Table>
concept SchemaNameOf =
    std::same_as<NameType, typename Table::SanitizedName> || std::same_as<NameType, typename Table::InternalName>;

namespace testing {

// The test door of a Sanitized name (SchemaTable::SanitizedName), a named
// friend of the name.  This header only declares it.  The one test that
// defines it plants a name that no check read, so the test reaches a
// refusal of a later door.  A search for this name lists each such plant.
class UncheckedSchemaNameDoor;

}  // namespace testing

// The refusal of the registration of a schema name.
enum class SchemaRefusal : std::uint8_t {
    // The table was sealed first, and it wrote nothing.
    sealed,
    // The table holds a different name of the hash.  The name came from
    // outside the process, so the table keeps its first name and refuses
    // this one.
    collision,
    // The table holds SCHEMA_TABLE_CAP names, and the hash is not one of
    // them.  The name came from outside the process, so the table keeps each
    // name that it holds and refuses this one.
    full,
};

// The result of the registration of a schema name.
using SchemaRegistration = ::fixy::Result<::fixy::Unit, SchemaRefusal>;

// The reason that the table refuses a schema name.
enum class SchemaNameError : std::uint8_t {
    // The hash is zero, the sentinel of no schema, on which the content hash
    // and the region cache key.
    zero_hash,
    // The name has no character.
    empty,
    // The name has more than SCHEMA_NAME_MAX characters.
    too_long,
    // The name holds a zero.  The table keeps the characters before the
    // first zero, so it would keep other characters than the name.
    holds_zero,
};

// The reason that the table refuses the characters of a name of hash, or no
// value: a hash of zero, no character, or more than SCHEMA_NAME_MAX
// characters.  It is the part of the check that a name of each provenance
// passes: sanitize_schema_name runs it on a name from outside, and
// SchemaTable::register_name on an internal name.  Complexity: constant.
[[nodiscard]] constexpr ::fixy::Option<SchemaNameError> schema_name_error(SchemaHash hash,
                                                                          ::fixy::TextView characters) noexcept {
    if (hash.raw() == 0) return ::fixy::some(SchemaNameError::zero_hash);
    if (characters.size() == 0) return ::fixy::some(SchemaNameError::empty);
    if (characters.size() > SCHEMA_NAME_MAX) return ::fixy::some(SchemaNameError::too_long);
    return ::fixy::none;
}

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
    // (crucible/RegistrationSeal.h gives the ordering argument).  The wait
    // has no exit while the writer stays in the section, so the context owns
    // Div.
    template <typename Ctx>
        requires CtxMaySpinForSeal<Ctx>
    void seal(Ctx const& ctx) noexcept {
        seal_.seal(ctx);
        CRUCIBLE_POST(0, seal_.is_sealed());
    }

    // Not constexpr: the phase is an atomic, and no constant evaluation can
    // read it.  The same holds for every function below that calls it.
    [[nodiscard]] bool is_sealed() const noexcept { return seal_.is_sealed(); }

    // The two view doors below give their views on the erased identity
    // through erase_brand, so the table gives one view type for each state.
    // The brand of mint_view names the door and not the caller, so each table
    // gets the same brand, and the rule brand-escape refuses a fresh brand
    // that leaves the door.  A brand for each table waits for the brand of an
    // arena or a store.
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
        return ::fixy::Option<MutableView>::some(::fixy::erase_brand(::fixy::mint_view<schema_state::Mutable>(*this)));
    }

    [[nodiscard]] SealedView mint_sealed_view() const noexcept {
        CRUCIBLE_PRE(is_sealed());
        return ::fixy::erase_brand(::fixy::mint_view<schema_state::Sealed>(*this));
    }

    // Found by argument-dependent lookup from mint_view.
    [[nodiscard]] friend bool view_ok(SchemaTable const& t, std::type_identity<schema_state::Mutable>) noexcept {
        return !t.is_sealed();
    }
    [[nodiscard]] friend bool view_ok(SchemaTable const& t, std::type_identity<schema_state::Sealed>) noexcept {
        return t.is_sealed();
    }

    // A name from outside the process, such as a name of a trace file or of
    // the C interface, that passed sanitize_schema_name.  The constructor is
    // private, and its one friend in the product is that door.  So no retag
    // of a boundary name to Sanitized makes a name that the table takes, and
    // a name from outside is checked once, at its boundary.  The name holds
    // the Sanitized tag that the door gives.
    class SanitizedName final {
    public:
        [[nodiscard]] constexpr ::fixy::TextView value() const noexcept { return name_.value(); }

    private:
        using Checked = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::Sanitized>;

        constexpr explicit SanitizedName(Checked name) noexcept : name_{name} {}

        Checked name_;

        // The friend gives a Result of this class.  The class is incomplete
        // here, and Result checks its payload, so the result type is deduced.
        template <SchemaNameBoundary Tag>
        friend auto sanitize_schema_name(SchemaHash hash, ::fixy::Tagged<::fixy::TextView, Tag> const& name) noexcept;
        friend class ::crucible::testing::UncheckedSchemaNameDoor;
    };

    // A name that crossed no trust boundary: the generated op table, or the
    // operator schema of the libtorch that this process links.
    using InternalName = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::FromInternal>;

    // A looked-up name is a C text in the storage of the table, and the
    // table is the interning owner that made it canonical for its hash.  It
    // claims neither provenance of the registration, because the table keeps
    // one copy per hash whichever producer wrote it.  A hash keeps its first
    // copy until clear(), so a looked-up name lives as long as the table: a
    // second registration of the same name writes nothing, and a different
    // name of the same hash writes nothing too (register_name).
    using LookupName = ::fixy::Tagged<::fixy::CText, ::fixy::tags::source::Interned>;

    // The view's type is the proof that the table was not sealed when the
    // view was minted.  The seal can still land between the mint and the
    // write, so the write runs inside the section of the seal.  It gives the
    // refusal sealed, and writes nothing, when the table was sealed first.
    // An empty name is not a registration, and it succeeds with no write.
    // The view names the table it proves, and a view of another table
    // proves nothing about this one, so that is checked in every build mode.
    // The copy of the name allocates through the allocation capability of
    // the caller.
    //
    // A different name of a held hash is a collision of two schemas, and
    // the table keeps its first name.  A new hash in a table that holds
    // SCHEMA_TABLE_CAP names finds no place, and the table keeps each name
    // that it holds.  A Sanitized name comes from outside the process, such
    // as a trace file or the C interface, and outside input can be wrong, so
    // it gives the refusal collision or full.  An internal name comes from
    // the program itself, so each of the two is a defect, and it ends the
    // process with the reason.
    //
    // A Sanitized name passed sanitize_schema_name, so its type proves its
    // check.  An internal name has no such door, so the table runs the same
    // check on its characters, the characters before its first zero
    // (schema_name_error): a hash of zero, no character, or more than
    // SCHEMA_NAME_MAX characters is a defect of the program, and it ends the
    // process with the reason.  The table would otherwise keep an empty name,
    // or a name on the sentinel of no schema.
    //
    // The context of the writer picks the entry of the write section
    // (RegistrationSeal::with_write_section): the context of a Vigil's
    // producer claim, or a context that owns Div.
    template <typename Ctx, typename NameType>
        requires CtxMayEnterWriteSection<Ctx> && SchemaNameOf<NameType, SchemaTable>
    [[nodiscard]] SchemaRegistration register_name(Ctx const& writer, ::foundation::effects::Alloc allocation,
                                                   MutableView const& view, SchemaHash hash, NameType const& name_tag) {
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        const ::fixy::TextView name = name_tag.value();
        if (name.size() == 0) return ::fixy::Unit{};
        constexpr bool is_internal_name = std::same_as<NameType, InternalName>;
        if constexpr (is_internal_name) {
            schema_name_error(hash, characters_of_(name))
                .match([hash](SchemaNameError refused) noexcept { report_refused_internal_name_(hash, refused); },
                       [] noexcept {});
        }
        ::fixy::Option<SchemaRefusal> refusal = ::fixy::none;
        const bool was_open =
            seal_.with_write_section(writer, [&] { refusal = write_name_(allocation, hash, name, is_internal_name); });
        if (!was_open) return ::fixy::err(SchemaRefusal::sealed);
        return refusal.match([](SchemaRefusal why) noexcept { return SchemaRegistration{::fixy::err(why)}; },
                             [] noexcept { return SchemaRegistration{::fixy::Unit{}}; });
    }

    // The callable that a walk of names calls for each name: its hash and its
    // Sanitized name.
    using AddName = ::fixy::CallRef<void(SchemaHash, SanitizedName const&) noexcept>;

    // Registers each name of a walk from outside the process, or none.  The
    // walk calls its AddName for each name, and it gives the same names in
    // the same order at each call.  The table runs the walk two times in one
    // write section.  The first walk checks each name: a different name of a
    // held hash, or a hash that the walk gives two times, is a collision, and
    // a new hash past SCHEMA_TABLE_CAP names is a full table.  The second walk
    // writes each name only when the first walk refused none.  So a refusal
    // writes no name, and no seal and no other writer runs between the check
    // and the writes.  It gives sealed, and writes nothing, when the table
    // was sealed first.  An empty name is not a registration, as for
    // register_name.  Complexity: the walk two times, O(log n) and linear in
    // the names of the walk for each name, and O(n) for each new hash.
    template <typename Ctx, typename Walk>
        requires CtxMayEnterWriteSection<Ctx> && std::is_invocable_r_v<void, Walk&, AddName const&>
    [[nodiscard]] SchemaRegistration register_names(Ctx const& writer, ::foundation::effects::Alloc allocation,
                                                    MutableView const& view, Walk&& walk_names) {
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        ::fixy::Option<SchemaRefusal> refusal = ::fixy::none;
        const bool was_open = seal_.with_write_section(writer, [&] {
            NameCheck check{};
            auto check_name = [this, &check](SchemaHash hash, SanitizedName const& name) noexcept {
                if (check.refusal.is_some()) return;
                const ::fixy::TextView characters = characters_of_(name.value());
                if (characters.size() == 0) return;
                switch (judge_name_(check, hash, characters)) {
                    case NameVerdict::collision:
                        check.refusal = ::fixy::some(SchemaRefusal::collision);
                        return;
                    case NameVerdict::full:
                        check.refusal = ::fixy::some(SchemaRefusal::full);
                        return;
                    case NameVerdict::added:
                        ++check.added;
                        break;
                    case NameVerdict::held:
                        break;
                    default:
                        ::fixy::unreachable();
                }
                const bool is_kept = check.hashes.append(hash).is_ok();
                CRUCIBLE_FATAL_INVARIANT(is_kept);
            };
            const AddName add_to_check{check_name};
            walk_names(add_to_check);
            if (check.refusal.is_some()) {
                refusal = check.refusal;
                return;
            }
            NameCheck const& checked = check;
            std::size_t written = 0;
            auto write_name = [this, &checked, &written, allocation](SchemaHash hash,
                                                                     SanitizedName const& name) noexcept {
                const ::fixy::TextView characters = characters_of_(name.value());
                if (characters.size() == 0) return;
                const bool is_checked_name = checked.hashes.get(written).match(
                    [hash](auto held) noexcept { return *held == hash; }, [] noexcept { return false; });
                CRUCIBLE_FATAL_INVARIANT(is_checked_name);
                const bool is_written = write_name_(allocation, hash, characters, false).is_none();
                CRUCIBLE_FATAL_INVARIANT(is_written);
                ++written;
            };
            const AddName add_to_table{write_name};
            walk_names(add_to_table);
            CRUCIBLE_FATAL_INVARIANT(written == checked.hashes.size());
        });
        if (!was_open) return ::fixy::err(SchemaRefusal::sealed);
        return refusal.match([](SchemaRefusal why) noexcept { return SchemaRegistration{::fixy::err(why)}; },
                             [] noexcept { return SchemaRegistration{::fixy::Unit{}}; });
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
    // The state of the first walk of register_names: the hash of each name
    // that passed, the count of the hashes that the table does not hold, and
    // the first refusal.
    struct NameCheck {
        ::fixy::FixedList<SchemaHash, SCHEMA_TABLE_CAP> hashes{};
        std::uint32_t added = 0;
        ::fixy::Option<SchemaRefusal> refusal = ::fixy::none;
    };

    // The verdict of one name of a walk: the table holds it, it takes a new
    // place, or the table refuses it.
    enum class NameVerdict : std::uint8_t {
        held,
        added,
        collision,
        full,
    };

    // The verdict of the name of hash with characters, against the table and
    // the names of check.  A hash of check is a collision, because a walk
    // gives each hash one time.  A new hash finds a place while the table
    // and the new hashes of check hold fewer than SCHEMA_TABLE_CAP names.  Each
    // hash of check is a held hash or a new hash, so check holds at most
    // SCHEMA_TABLE_CAP hashes.  Complexity: O(log n) and linear in check.
    [[nodiscard]] NameVerdict judge_name_(NameCheck const& check, SchemaHash hash,
                                          ::fixy::TextView characters) const noexcept {
        for (const SchemaHash earlier : check.hashes.view()) {
            if (earlier == hash) return NameVerdict::collision;
        }
        const ::fixy::Option<LookupName> held = lookup(hash);
        if (held.is_some()) {
            const bool is_same_name = held.match(
                [characters](LookupName const& found) noexcept { return found.value().view() == characters; },
                [] noexcept { return false; });
            return is_same_name ? NameVerdict::held : NameVerdict::collision;
        }
        if (entries_.size() + check.added >= SCHEMA_TABLE_CAP) return NameVerdict::full;
        return NameVerdict::added;
    }

    // Runs inside the write section, so no seal and no other writer runs
    // at the same time.  It gives the refusal of a name from outside, or no
    // value when the table holds the name.  A hash that the table holds
    // keeps its first copy: the same name again writes nothing, and a
    // different name is a collision of two schemas.  A new hash in a full
    // table finds no place.  The check of the room comes before the copy of
    // the name and before the pass below, so a refused name allocates nothing
    // and moves no entry.  An internal name ends the process at a collision
    // and at a full table.  A new hash takes one pass in the order of the
    // hashes, which keeps that order: the carried entry changes place with
    // each entry of a larger hash, so each such entry moves one place up, and
    // the last one goes to the end.  Complexity: O(log n) for a held hash,
    // O(n) for a new one.
    [[nodiscard]] ::fixy::Option<SchemaRefusal> write_name_(::foundation::effects::Alloc allocation, SchemaHash hash,
                                                            ::fixy::TextView name, bool is_internal_name) {
        const ::fixy::TextView characters = characters_of_(name);
        for (const auto& held : entries_.get(position_of_(hash))) {
            if (held->hash == hash) {
                const ::fixy::TextView stored = stored_characters_of_(*held);
                if (stored == characters) return ::fixy::none;
                if (is_internal_name) report_collision_(hash, stored, characters);
                return ::fixy::some(SchemaRefusal::collision);
            }
        }
        if (entries_.size() == SCHEMA_TABLE_CAP) [[unlikely]] {
            if (is_internal_name) report_full_(characters);
            return ::fixy::some(SchemaRefusal::full);
        }
        SchemaEntry carried{.hash = hash, .name = copy_name_(allocation, characters)};
        for (SchemaEntry& entry : entries_) {
            if (carried.hash < entry.hash) ::fixy::swap(entry, carried);
        }
        const bool is_pushed = entries_.append(static_cast<SchemaEntry&&>(carried)).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_pushed);
        CRUCIBLE_POST(0, is_registered_(hash));
        return ::fixy::none;
    }

    // The characters of name before its first zero.  Complexity: the length
    // of the name.
    [[nodiscard]] static ::fixy::TextView characters_of_(::fixy::TextView name) noexcept {
        const ::fixy::Option<std::size_t> zero = ::fixy::find(name, '\0');
        return zero.match(
            [name](std::size_t position) noexcept {
                return name.window(0, position).expect("the zero is a character of the name");
            },
            [name] noexcept { return name; });
    }

    // The characters of the name that held keeps.  The stored name ends with
    // its zero, which is no character of the name.
    [[nodiscard]] static ::fixy::TextView stored_characters_of_(SchemaEntry const& held) noexcept {
        const ::fixy::TextView stored = held.name.view();
        return stored.window(0, stored.size() - 1).expect("the table keeps one zero after each name");
    }

    [[noreturn, gnu::cold]] static void report_collision_(SchemaHash hash, ::fixy::TextView stored,
                                                          ::fixy::TextView other) noexcept {
        ::fixy::fatal("crucible: the schema hash {} names two schemas, {} and {}.  A second name of one hash is a "
                      "collision of the schema hash",
                      hash.raw(), stored, other);
    }

    // An internal name that the check of schema_name_error refuses is a defect
    // of the program: a hash of zero, a name that starts with a zero, or a
    // name of more than SCHEMA_NAME_MAX characters.
    [[noreturn, gnu::cold]] static void report_refused_internal_name_(SchemaHash hash,
                                                                      SchemaNameError refused) noexcept {
        switch (refused) {
            case SchemaNameError::zero_hash:
                ::fixy::fatal("crucible: an internal schema name has the hash zero, the sentinel of no schema.  The "
                              "hash of a schema is never zero");
            case SchemaNameError::empty:
                ::fixy::fatal("crucible: the internal schema name of the hash {} has no character before its first "
                              "zero, so the table would keep an empty name",
                              hash.raw());
            case SchemaNameError::too_long:
                ::fixy::fatal("crucible: the internal schema name of the hash {} is longer than SCHEMA_NAME_MAX "
                              "characters.  Raise SCHEMA_NAME_MAX in crucible/SchemaTable.h",
                              hash.raw());
            case SchemaNameError::holds_zero:
            default:
                ::fixy::unreachable();
        }
    }

    // A full table refuses an internal name.  A quiet return leaves each later
    // schema unregistered, and the fallback dispatch that results shows far
    // from the registration that was lost.
    [[noreturn, gnu::cold]] static void report_full_(::fixy::TextView name) noexcept {
        ::fixy::fatal("crucible: the schema table holds {} names, so it has no place for the schema {}.  Raise "
                      "SCHEMA_TABLE_CAP in crucible/SchemaTable.h, or register fewer schemas",
                      SCHEMA_TABLE_CAP, name);
    }

    // The characters of a name, and one zero after them, frozen into a
    // HeapText.  The copy allocates through the allocation capability of the
    // caller.  Complexity: the length of the name.
    [[nodiscard]] static ::fixy::HeapText copy_name_(::foundation::effects::Alloc allocation,
                                                     ::fixy::TextView characters) {
        if (characters.size() > SCHEMA_NAME_MAX) [[unlikely]] {
            ::fixy::fatal("crucible: a schema name is longer than SCHEMA_NAME_MAX characters.  Raise SCHEMA_NAME_MAX "
                          "in crucible/SchemaTable.h");
        }
        ::fixy::GrowText copy = ::fixy::mint_grow_text(SCHEMA_NAME_MAX + 1);
        copy.append(allocation, characters).expect("the name fits below the maximum of its text");
        copy.append(allocation, '\0').expect("the zero fits below the maximum of its text");
        return ::fixy::freeze(static_cast<::fixy::GrowText&&>(copy));
    }

    // The position of the first entry whose hash is not below hash.  The
    // entries are in the order of their hashes.  Complexity: O(log n).
    [[nodiscard]] std::size_t position_of_(SchemaHash hash) const noexcept {
        return ::fixy::find_lower_bound_by_key(
            entries_.view(), [](const SchemaEntry& entry) noexcept { return entry.hash.raw(); }, hash.raw());
    }

    [[nodiscard]] bool is_registered_(SchemaHash hash) const noexcept { return lookup(hash).is_some(); }

    ::fixy::FixedList<SchemaEntry, SCHEMA_TABLE_CAP> entries_{};
    RegistrationSeal seal_;
};

// The two types of a name that the table registers: a Sanitized name, which
// crossed a trust boundary (the C ABI, a trace file) and passed
// sanitize_schema_name, and an internal name, which crossed no boundary.
// Each other type is refused, so a raw External name, and a Sanitized tag
// that no check gave, cannot be registered.
template <typename NameType>
concept SchemaNameSource = SchemaNameOf<NameType, SchemaTable>;

// The one validator of a schema name from outside the process, and the one
// door from a boundary tag to a Sanitized name.  Each door that takes a name
// from outside calls it: the C entry of the vessel, the trace loader, and
// the export, which writes no name that the loader refuses.  So a Sanitized
// name means the same at each door.  It refuses a hash of zero, an empty
// name, a name of more than SCHEMA_NAME_MAX characters, and a name that holds
// a zero.  The result is a Result<SchemaTable::SanitizedName,
// SchemaNameError>, and the type is deduced, as in the friend declaration of
// the name.  Complexity: the length of the name.
template <SchemaNameBoundary Tag>
[[nodiscard]] inline auto sanitize_schema_name(SchemaHash hash,
                                               ::fixy::Tagged<::fixy::TextView, Tag> const& name) noexcept {
    using Sanitized = ::fixy::Result<SchemaTable::SanitizedName, SchemaNameError>;
    const ::fixy::TextView characters = name.value();
    const ::fixy::Option<SchemaNameError> refusal = schema_name_error(hash, characters);
    return refusal.match(
        [](SchemaNameError const& refused) noexcept { return Sanitized{::fixy::err(refused)}; },
        [characters] noexcept {
            if (::fixy::find(characters, '\0').is_some()) {
                return Sanitized{::fixy::err(SchemaNameError::holds_zero)};
            }
            return Sanitized{SchemaTable::SanitizedName{
                ::fixy::mint_tagged<Tag>(characters).template retag<::fixy::tags::source::Sanitized>()}};
        });
}

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

// The registration in the global table (SchemaTable::register_name).  It
// gives the refusal sealed when the global table was sealed after the view
// was minted, and then nothing is registered.  The copy of the name
// allocates through the allocation capability of the caller.  The context of
// the writer picks the entry of the write section (CtxMayEnterWriteSection).
template <typename Ctx, SchemaNameSource NameType>
    requires CtxMayEnterWriteSection<Ctx>
[[nodiscard]] inline SchemaRegistration register_schema_name(Ctx const& writer, ::foundation::effects::Alloc allocation,
                                                             SchemaTable::MutableView const& view, SchemaHash hash,
                                                             NameType const& name) {
    return global_schema_table().register_name(writer, allocation, view, hash, name);
}

// True when a registration gave the refusal of a collision.
[[nodiscard]] inline bool is_collision(SchemaRegistration const& registration) noexcept {
    return registration.is_err() && registration.err().value_or(SchemaRefusal::sealed) == SchemaRefusal::collision;
}

// True when a registration gave the refusal of a full table.
[[nodiscard]] inline bool is_full(SchemaRegistration const& registration) noexcept {
    return registration.is_err() && registration.err().value_or(SchemaRefusal::sealed) == SchemaRefusal::full;
}

// True when a registration gave the refusal of a sealed table.
[[nodiscard]] inline bool is_sealed_refusal(SchemaRegistration const& registration) noexcept {
    return registration.is_err() && registration.err().value_or(SchemaRefusal::collision) == SchemaRefusal::sealed;
}

// Registers the name of one schema from the thread that holds the producer
// claim of a Vigil.  The name goes to the global table while that table takes
// writes, and to the late table after the seal, so no seal drops it.  Each
// other refusal of the global table is the result, so a full global table
// puts no name in the late table.  A collision or a full table in the table
// that takes the name gives its refusal, and an internal name ends the
// process there (SchemaTable::register_name).  The foreground context holds
// no allocation capability, so the caller gives one.  A Vigil gives the one
// of its startup context.
template <SchemaNameSource NameType>
[[nodiscard]] inline SchemaRegistration register_schema_name(VigilFgCtx const& fg,
                                                             ::foundation::effects::Alloc allocation, SchemaHash hash,
                                                             NameType const& name) {
    // A sealed table mints no view, which is the refusal of a sealed table.
    SchemaTable& global_table = global_schema_table();
    SchemaRegistration in_global = global_table.mint_mutable_view(fg).match(
        [&fg, &global_table, allocation, hash, &name](SchemaTable::MutableView global_view) {
            return global_table.register_name(fg, allocation, global_view, hash, name);
        },
        [] { return SchemaRegistration{::fixy::err(SchemaRefusal::sealed)}; });
    if (!is_sealed_refusal(in_global)) return in_global;
    // Nothing seals the late table, so its view exists and no seal refuses
    // its write.
    SchemaTable& late_table = late_schema_table();
    const SchemaTable::MutableView late_view =
        late_table.mint_mutable_view(fg).expect("nothing seals the late schema table, so it mints a mutable view");
    SchemaRegistration in_late = late_table.register_name(fg, allocation, late_view, hash, name);
    CRUCIBLE_FATAL_INVARIANT(!is_sealed_refusal(in_late));
    return in_late;
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
