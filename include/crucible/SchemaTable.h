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
#include <utility>

namespace crucible {

// The most names of the global schema table, which several threads read
// after its seal.
inline constexpr uint32_t SCHEMA_TABLE_CAP = 512;

// The most names of the late schema table.  An adapter learns the name of an
// operator when the operator first runs, and a library can define operators
// at run time, so the late table holds many more names than the global one.
// Its entries sit in its static object, which is constant-initialized
// (constinit of late_schema_table()), so the bytes of the object stay zero
// and no constructor writes them.  So a page of entries costs memory only
// when a name first writes it.
inline constexpr uint32_t LATE_SCHEMA_TABLE_CAP = 65536;

// The longest name that the tables hold, in characters: the most that the
// 16-bit length of a name of a trace file holds, so each name of a table
// exports and loads.  A PyTorch operator name with its overload is shorter
// than 256 characters, and a library can define a longer one at run time.
inline constexpr std::size_t SCHEMA_NAME_MAX = 65535;

// The most bytes of the names of one table, each name with its zero.  A name
// can come from outside the process, so this bound, and not the count alone,
// keeps the memory of the names of a table at 64 MiB.
inline constexpr ::fixy::ByteCount SCHEMA_NAME_BYTES_MAX{std::size_t{64} << 20};

// The room for internal names: one part in SCHEMA_INTERNAL_ROOM_SHARE of the
// names and of the name bytes of each table.  A name from outside gets the
// refusal full at the bound minus the room, and an internal name can take the
// whole table.  So no outside name can starve an internal one.  Names from
// outside (the C interface, the operators of a library, a trace file) have no
// bound of their own, and an internal name that finds no place ends the
// process.  The internal names of the program fit the room: the vessel checks
// at compile time that the names of the operators of its generated table fit
// the room of the late table.  The late table keeps 4096 names and 4 MiB.
inline constexpr uint32_t SCHEMA_INTERNAL_ROOM_SHARE = 16;

// The name holds its characters and one zero after them, so a C caller reads
// the name through foreign_c_str.  The name is a HeapText, so no append
// changes it, and a C text of it lives as long as the entry keeps it.  A
// move of the entry moves the owner of the characters, not the characters.
// The text of an entry that no registration wrote holds no storage.
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

namespace testing {

// The test door of a Sanitized name (SanitizedSchemaName), a named friend of
// the name.  This header only declares it.  The one test that defines it
// plants a name that no check read, so the test reaches a refusal of a later
// door.  A search for this name lists each such plant.
class UncheckedSchemaNameDoor;

}  // namespace testing

// The two roles of the parts of an internal schema name: the name, such as
// "aten::add", and the overload, such as "Tensor".
namespace side {
struct SchemaBaseName final {};
struct SchemaOverload final {};
}  // namespace side

namespace detail {

// The consteval constructor of an internal schema name calls one of these
// functions when its hash or a part of the name breaks a rule.  No function
// has a definition, so the call is no constant expression, and the error of
// the compiler names the rule.
void internal_schema_name_has_the_zero_hash() noexcept;
void internal_schema_name_has_no_character() noexcept;
void internal_schema_name_holds_a_zero_byte() noexcept;
void internal_schema_name_is_longer_than_schema_name_max() noexcept;

}  // namespace detail

// The two parts of a schema name: the name, such as "aten::add", and the
// overload, such as "Tensor", which is empty for a name of one text.  The
// full name is the name, and a dot and the overload when the overload has
// characters, as c10::OperatorName spells it.  The parts borrow their
// characters.
class SchemaNameParts final {
public:
    // The name of one text, such as "aten::mm" or "aten::add.Tensor".
    constexpr explicit SchemaNameParts(::fixy::TextView name) noexcept : name_{name} {}

    // The name and the overload of an operator.
    constexpr SchemaNameParts(::fixy::Side<side::SchemaBaseName, ::fixy::TextView> name,
                              ::fixy::Side<side::SchemaOverload, ::fixy::TextView> overload) noexcept
        : name_{name.value()}, overload_{overload.value()} {}

    [[nodiscard]] constexpr ::fixy::TextView name() const noexcept { return name_; }
    [[nodiscard]] constexpr ::fixy::TextView overload() const noexcept { return overload_; }

    // The count of the characters of the full name.  Each part holds at most
    // __PTRDIFF_MAX__ characters, so the sum fits.
    [[nodiscard]] constexpr std::size_t full_size() const noexcept {
        return name_.size() + (overload_.size() == 0 ? 0 : 1 + overload_.size());
    }

    // True when a part holds a zero.  Complexity: the length of the name.
    [[nodiscard]] constexpr bool holds_zero() const noexcept {
        return ::fixy::find(name_, '\0').is_some() || ::fixy::find(overload_, '\0').is_some();
    }

    // The character at a position of the full name, which is below
    // full_size().  Complexity: constant.
    [[nodiscard]] constexpr char full_character_at(std::size_t position) const noexcept {
        const auto first_of = [](::fixy::TextView one) noexcept {
            for (const char character : one)
                return character;
            return '\0';
        };
        if (position < name_.size()) return first_of(name_.window(position, 1).expect("the position is in the name"));
        if (position == name_.size()) return '.';
        return first_of(overload_.window(position - name_.size() - 1, 1).expect("the position is in the overload"));
    }

    // True when the two full names hold the same characters, whatever the
    // split of each into its parts.  Complexity: the length of the name.
    [[nodiscard]] constexpr bool is_same_full_name(SchemaNameParts const& other) const noexcept {
        const std::size_t size = full_size();
        if (other.full_size() != size) return false;
        if (overload_.size() == 0 && other.overload_.size() == 0) return name_ == other.name_;
        for (std::size_t position = 0; position < size; ++position) {
            if (full_character_at(position) != other.full_character_at(position)) return false;
        }
        return true;
    }

private:
    ::fixy::TextView name_{};
    ::fixy::TextView overload_{};
};

// The refusal of the registration of a schema name.
enum class SchemaRefusal : std::uint8_t {
    // The table was sealed first, and it wrote nothing.
    sealed,
    // The table holds a different name of the hash.  The name came from
    // outside the process, so the table keeps its first name and refuses
    // this one.
    collision,
    // The table has no place for a new hash: it holds its capacity of names,
    // or the bytes of the name would take its names past
    // SCHEMA_NAME_BYTES_MAX.  The name came from outside the process, so the
    // table keeps each name that it holds and refuses this one.
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

// The reason that the table refuses the parts of a name of hash, or no value:
// a hash of zero, a name with no character, or a full name of more than
// SCHEMA_NAME_MAX characters.  sanitize_schema_name runs it on a name from
// outside.  The consteval constructor of an internal name checks the same
// parts at compile time.  Complexity: constant.
[[nodiscard]] constexpr ::fixy::Option<SchemaNameError> schema_name_error(SchemaHash hash,
                                                                          SchemaNameParts const& parts) noexcept {
    if (hash.raw() == 0) return ::fixy::some(SchemaNameError::zero_hash);
    if (parts.name().size() == 0) return ::fixy::some(SchemaNameError::empty);
    if (parts.full_size() > SCHEMA_NAME_MAX) return ::fixy::some(SchemaNameError::too_long);
    return ::fixy::none;
}

namespace detail {

// The dot between the name and the overload of a full name, or no character
// for a name with no overload, for a report.
[[nodiscard]] inline ::fixy::TextView schema_name_dot_of(SchemaNameParts const& parts) noexcept {
    return parts.overload().size() == 0 ? ::fixy::TextView{} : ::fixy::TextView{"."};
}

// A second internal name of a held hash is a defect of the program, and the
// process ends with the two names.
[[noreturn, gnu::cold]] inline void report_schema_collision(SchemaHash hash, ::fixy::TextView stored,
                                                            SchemaNameParts const& other) noexcept {
    ::fixy::fatal("crucible: the schema hash {} names two schemas, {} and {}{}{}.  A second name of one hash is a "
                  "collision of the schema hash",
                  hash.raw(), stored, other.name(), schema_name_dot_of(other), other.overload());
}

}  // namespace detail

// A name from outside the process, such as a name of a trace file or of the
// C interface, that passed sanitize_schema_name, with its hash.  The
// constructor is private, and its one friend in the product is that door.  So
// no retag of a boundary name to Sanitized makes a name that a table takes,
// and a name from outside is checked once, at its boundary.  The name holds
// the hash that the door checked and the Sanitized tag that the door gives,
// so a table registers the name only on that hash.
class SanitizedSchemaName final {
public:
    // The hash of the name, which is not zero.
    [[nodiscard]] constexpr SchemaHash hash() const noexcept { return hash_; }

    // The parts of the name: one text, or the name and the overload of an
    // operator of PyTorch.
    [[nodiscard]] constexpr SchemaNameParts parts() const noexcept { return name_.value(); }

private:
    using Checked = ::fixy::Tagged<SchemaNameParts, ::fixy::tags::source::Sanitized>;

    constexpr explicit SanitizedSchemaName(SchemaHash hash, Checked name) noexcept : hash_{hash}, name_{name} {}

    SchemaHash hash_{};
    Checked name_;

    // The friend gives a Result of this class.  The class is incomplete
    // here, and Result checks its payload, so the result type is deduced.
    template <SchemaNameBoundary Tag>
    friend auto sanitize_schema_name(SchemaHash hash, ::fixy::Tagged<SchemaNameParts, Tag> const& name) noexcept;
    friend class ::crucible::testing::UncheckedSchemaNameDoor;
};

// A name that crossed no trust boundary, with its hash: the name of an
// operator of the generated op table, or a literal of the program.  Its hash
// is a constant of the program, and its two parts, the name and the
// overload, are texts of the program image.  The constructors are consteval,
// so no value of the run time reaches them: a text from outside, or a copy of
// one in a buffer, is never an internal name, and the only route from outside
// to a table is sanitize_schema_name.  The check of the hash and the parts
// runs at compile time: the hash zero, which is the sentinel of no schema, a
// name with no character, a part that holds a zero, or a full name of more
// than SCHEMA_NAME_MAX characters stops the build.  The full name is the
// name, and a dot and the overload when the overload has characters.
class InternalSchemaName final {
public:
    // The name of one text on its hash, such as "aten::mm".
    consteval explicit InternalSchemaName(SchemaHash hash, ::fixy::TextView name) noexcept : hash_{hash}, parts_{name} {
        check_();
    }

    // The name and the overload of an operator on its hash, such as the hash
    // and the two parts of a row of the generated table.
    consteval InternalSchemaName(SchemaHash hash, ::fixy::Side<side::SchemaBaseName, ::fixy::TextView> name,
                                 ::fixy::Side<side::SchemaOverload, ::fixy::TextView> overload) noexcept
        : hash_{hash},
          parts_{static_cast<::fixy::Side<side::SchemaBaseName, ::fixy::TextView>&&>(name),
                 static_cast<::fixy::Side<side::SchemaOverload, ::fixy::TextView>&&>(overload)} {
        check_();
    }

    // The hash of the name, which is not zero.
    [[nodiscard]] constexpr SchemaHash hash() const noexcept { return hash_; }

    [[nodiscard]] constexpr SchemaNameParts parts() const noexcept { return parts_; }

private:
    consteval void check_() const noexcept {
        if (hash_.raw() == 0) detail::internal_schema_name_has_the_zero_hash();
        if (parts_.name().size() == 0) detail::internal_schema_name_has_no_character();
        if (parts_.holds_zero()) detail::internal_schema_name_holds_a_zero_byte();
        if (parts_.full_size() > SCHEMA_NAME_MAX) detail::internal_schema_name_is_longer_than_schema_name_max();
    }

    SchemaHash hash_{};
    SchemaNameParts parts_;
};

// A looked-up name is a C text in the storage of a table, and the table is
// the interning owner that made it canonical for its hash.  It claims neither
// provenance of the registration, because the table keeps one copy per hash
// whichever producer wrote it.  A hash keeps its first copy until clear(), so
// a looked-up name lives as long as the table: a second registration of the
// same name writes nothing, and a different name of the same hash writes
// nothing too (SchemaTableOf::register_name).
using InternedSchemaName = ::fixy::Tagged<::fixy::CText, ::fixy::tags::source::Interned>;

// The two types of a name that a table registers: a Sanitized name, which
// crossed a trust boundary (the C ABI, a trace file) and passed
// sanitize_schema_name, and an internal name, which crossed no boundary.
// Each other type is refused, so a raw External name, and a Sanitized tag
// that no check gave, cannot be registered.  A concept has no
// specialization, so no unit can admit a third type.
template <typename NameType>
concept SchemaNameSource = std::same_as<NameType, SanitizedSchemaName> || std::same_as<NameType, InternalSchemaName>;

// The callable that a walk of names calls for each name: its Sanitized name,
// which holds its hash (SchemaTableOf::register_names).
using AddSchemaName = ::fixy::CallRef<void(SanitizedSchemaName const&) noexcept>;

// The one validator of a schema name from outside the process, and the one
// door from a boundary tag to a Sanitized name.  Each door that takes a name
// from outside calls it: the C entry of the vessel and the trace loader.  So a
// Sanitized name means the same at each door.  It refuses a hash of zero, an
// empty name, a full name of more than SCHEMA_NAME_MAX characters, and a name
// that holds a zero.  The name is one text, or the name and the overload of an
// operator of PyTorch, which no caller joins.  The Sanitized name holds the
// hash, so a table registers it only on the hash of this check.  The result is
// a Result<SanitizedSchemaName, SchemaNameError>, and the type is deduced, as
// in the friend declaration of the name.  Complexity: the length of the name.
template <SchemaNameBoundary Tag>
[[nodiscard]] inline auto sanitize_schema_name(SchemaHash hash,
                                               ::fixy::Tagged<SchemaNameParts, Tag> const& name) noexcept {
    using Sanitized = ::fixy::Result<SanitizedSchemaName, SchemaNameError>;
    const SchemaNameParts parts = name.value();
    const ::fixy::Option<SchemaNameError> refusal = schema_name_error(hash, parts);
    return refusal.match(
        [](SchemaNameError const& refused) noexcept { return Sanitized{::fixy::err(refused)}; },
        [hash, parts] noexcept {
            if (parts.holds_zero()) return Sanitized{::fixy::err(SchemaNameError::holds_zero)};
            return Sanitized{SanitizedSchemaName{
                hash, ::fixy::mint_tagged<Tag>(parts).template retag<::fixy::tags::source::Sanitized>()}};
        });
}

// The same for a name of one text.
template <SchemaNameBoundary Tag>
[[nodiscard]] inline auto sanitize_schema_name(SchemaHash hash,
                                               ::fixy::Tagged<::fixy::TextView, Tag> const& name) noexcept {
    return sanitize_schema_name(hash, ::fixy::mint_tagged<Tag>(SchemaNameParts{name.value()}));
}

// The search of the name that a program reserves for a schema hash: the
// name, or no value for a hash that the program reserves for no name.
using SchemaNameSearch = ::fixy::CallRef<::fixy::Option<SchemaNameParts>(SchemaHash) noexcept>;

// The tag of the reserve of the names that the function Search gives.
template <auto Search>
struct SchemaNameSearchOf final {};

// The reserved hashes of a program: each hash that an internal name of the
// program takes, with that name, such as the hash of each operator of the
// generated table of the vessel.  An internal name meets a name from outside
// on its hash only when the two names are the same, because each door of a
// name from outside refuses a name with a reserved hash and a different name
// before it registers a name: the C entry of the vessel and the trace loader.
// The reserve holds a CallRef, so it has no copy and no move, and a door takes
// it by const&.
class SchemaNameReserve final {
public:
    // The reserve of the names that the function Search gives.
    template <auto Search>
        requires ::foundation::core::CallRefFunction<Search, ::fixy::Option<SchemaNameParts>, SchemaHash>
    constexpr explicit SchemaNameReserve(SchemaNameSearchOf<Search> /*search*/) noexcept
        : search_{SchemaNameSearch::of<Search>()} {}

    // The reserve of a program that reserves no hash.
    [[nodiscard]] static constexpr SchemaNameReserve none() noexcept {
        return SchemaNameReserve{SchemaNameSearchOf<&find_no_reserved_name_>{}};
    }

    // True when the program reserves the hash for a full name other than the
    // full name of parts.  The same name on its reserved hash names the same
    // schema.  Complexity: the search and the length of the name.
    [[nodiscard]] constexpr bool is_reserved_for_other_name(SchemaHash hash,
                                                            SchemaNameParts const& parts) const noexcept {
        return search_(hash).match(
            [&parts](SchemaNameParts reserved) noexcept { return !parts.is_same_full_name(reserved); },
            [] noexcept { return false; });
    }

private:
    [[nodiscard]] static constexpr ::fixy::Option<SchemaNameParts>
    find_no_reserved_name_(SchemaHash /*hash*/) noexcept {
        return ::fixy::none;
    }

    SchemaNameSearch search_;
};

// A table of schema names of at most Capacity names, sorted by hash, with at
// most SCHEMA_NAME_BYTES_MAX bytes of names.  The entries, the count and the
// phase are private.  A public field is a write that no view and no seal can
// refuse.  The two tables of the process are SchemaTable, the global table,
// and LateSchemaTable, the late table.
//
// Threads use one table at the same time: the writers, one at a time in the
// write section, the seal, and the readers after the seal.  Each door that
// writes the names takes the write section (crucible/RegistrationSeal.h),
// clear takes the proof of a reopen, and the seal and each read are const
// doors.  So the class states synchronized{} (foundation/Transfer.h).
template <uint32_t Capacity>
    requires(Capacity >= SCHEMA_INTERNAL_ROOM_SHARE)
class[[= ::foundation::transfer::synchronized{}]] SchemaTableOf {
public:
    // The room of the internal names of the table (SCHEMA_INTERNAL_ROOM_SHARE):
    // the names and the name bytes past which a name from outside gets full.
    static constexpr uint32_t kInternalRoomNames = Capacity / SCHEMA_INTERNAL_ROOM_SHARE;
    static constexpr ::fixy::ByteCount kInternalRoomBytes{SCHEMA_NAME_BYTES_MAX.raw() / SCHEMA_INTERNAL_ROOM_SHARE};

    SchemaTableOf() = default;

    SchemaTableOf(const SchemaTableOf&) = delete("owns the storage of its names");
    SchemaTableOf& operator=(const SchemaTableOf&) = delete("owns the storage of its names");
    SchemaTableOf(SchemaTableOf&&) = delete("owns the storage of its names");
    SchemaTableOf& operator=(SchemaTableOf&&) = delete("owns the storage of its names");

    ~SchemaTableOf() = default;

    // Waits for a registration that is in progress, then seals.  A reader
    // that observes the seal observes every registered entry
    // (crucible/RegistrationSeal.h gives the ordering argument).  The wait
    // has no exit while the writer stays in the section, so the context owns
    // Div.
    template <typename Ctx>
        requires CtxMaySpinForSeal<Ctx>
    void seal(Ctx const& ctx) const noexcept {
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
    using MutableView = ::fixy::ScopedView<SchemaTableOf, schema_state::Mutable>;
    using SealedView = ::fixy::ScopedView<SchemaTableOf, schema_state::Sealed>;

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

    // Takes the write section of this table, or gives no value when the
    // table is sealed (crucible/RegistrationSeal.h).  The context of the
    // writer picks the entry: one compare-exchange for the writer of a
    // Vigil's producer claim, and a spin for a context that owns Div.
    //
    // A claim door (the doors of a synchronized class of
    // foundation/Transfer.h): the compare-exchange of the seal takes the
    // phase from Open to Writing and makes the section.  The writes of the
    // entries, the name bytes and the room report happen only through a
    // writer door that takes the section, so only on the thread that holds
    // it, until the section ends with the release store.
    template <typename Ctx>
        requires CtxMayEnterWriteSection<Ctx>
    [[nodiscard]] ::fixy::Option<WriteSection<SchemaTableOf>> try_enter_write_section(Ctx const& writer) noexcept {
        return seal_.template try_enter_write_section<SchemaTableOf>(writer);
    }

    // The view's type is the proof that the table was not sealed when the
    // view was minted.  The section is the proof that no seal and no other
    // writer runs until it ends, so the write finds no seal.  An empty name
    // is not a registration, and it succeeds with no write.  The view and
    // the section each name the table that they prove, and a view or a
    // section of another table proves nothing about this one.  The check of
    // the view runs in every build mode, and the check of the section is a
    // precondition.  The copy of the name allocates through the allocation
    // capability of the caller.
    //
    // A different name of a held hash is a collision of two schemas, and
    // the table keeps its first name.  A new hash finds no place in a table
    // that holds Capacity names, or whose names would pass
    // SCHEMA_NAME_BYTES_MAX with the bytes of the name, and the table keeps
    // each name that it holds.  A Sanitized name finds no place already at
    // the room of internal names (SCHEMA_INTERNAL_ROOM_SHARE), so no name
    // from outside takes the place of an internal one.  A Sanitized name
    // comes from outside the process, such as a trace file or the C
    // interface, and outside input can be wrong, so it gives the refusal
    // collision or full.  An internal name comes from the program itself, so
    // each of the two is a defect, and it ends the process with the reason.
    //
    // The name holds its hash, and the table registers the name on that hash.
    // A Sanitized name passed sanitize_schema_name, so its type proves the
    // check of its hash and its parts.  An internal name passed the check of
    // its consteval constructor at compile time.  So no name of the two types
    // has the hash zero, the sentinel of no schema.
    //
    // The door has one overload for each of the two types of a name, and no
    // template, so the check of the statement synchronized{} reads its
    // parameters (foundation/Transfer.h).  Neither overload gives the
    // refusal sealed: the claim door gives no section for a sealed table.
    [[nodiscard]] SchemaRegistration register_name(WriteSection<SchemaTableOf>& section,
                                                   ::foundation::effects::Alloc allocation, MutableView const& view,
                                                   SanitizedSchemaName const& name) {
        return register_name_(section, allocation, view, name);
    }

    [[nodiscard]] SchemaRegistration register_name(WriteSection<SchemaTableOf>& section,
                                                   ::foundation::effects::Alloc allocation, MutableView const& view,
                                                   InternalSchemaName const& name) {
        return register_name_(section, allocation, view, name);
    }

    // Registers each name of a walk from outside the process, or none, each on
    // its own hash.  The walk calls its AddSchemaName for each name, and it
    // gives the same names in the same order at each call.  The table runs
    // the walk two times in the write section that the caller holds.  The
    // first walk checks each name: a different name of a held hash, or a
    // hash that the walk gives two times, is a collision, and a new hash past
    // the room of the table is a full table.  The second walk writes each
    // name only when the first walk refused none.  So a refusal writes no
    // name, and no seal and no other writer runs between the check and the
    // writes.  An empty name is not a registration, as for register_name.
    // The first walk keeps the hash of each name on the stack, so only a
    // table of at most SCHEMA_TABLE_CAP names takes a walk.  The bound is an
    // assertion of the body, not a constraint: the check of the statement
    // synchronized{} reads the door on each table, and a constraint that is
    // false on the late table leaves the door with no specialization there.
    // Complexity: the walk two times, O(log n) and linear in the names of the
    // walk for each name, and O(n) for each new hash.
    template <typename Walk>
        requires std::is_invocable_r_v<void, Walk&, AddSchemaName const&>
    [[nodiscard]] SchemaRegistration register_names(WriteSection<SchemaTableOf>& section,
                                                    ::foundation::effects::Alloc allocation, MutableView const& view,
                                                    Walk&& walk_names) {
        static_assert(Capacity <= SCHEMA_TABLE_CAP,
                      "the first walk of register_names keeps a hash for each name on the stack, so only a table of "
                      "at most SCHEMA_TABLE_CAP names takes a walk");
        CRUCIBLE_PRE(section.names(seal_));
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        NameCheck check{};
        auto check_name = [this, &check](SanitizedSchemaName const& name) noexcept {
            if (check.refusal.is_some()) return;
            const SchemaNameParts characters = cut_at_zero_(name.parts());
            if (characters.name().size() == 0) return;
            const SchemaHash hash = name.hash();
            switch (judge_name_(check, hash, characters)) {
                case NameVerdict::collision:
                    check.refusal = ::fixy::some(SchemaRefusal::collision);
                    return;
                case NameVerdict::full:
                    check.refusal = ::fixy::some(SchemaRefusal::full);
                    return;
                case NameVerdict::added:
                    ++check.added;
                    check.added_bytes = ::fixy::ByteCount{check.added_bytes.raw() + stored_bytes_of_(characters).raw()};
                    break;
                case NameVerdict::held:
                    break;
                default:
                    ::fixy::unreachable();
            }
            const bool is_kept = check.hashes.append(hash).is_ok();
            CRUCIBLE_FATAL_INVARIANT(is_kept);
        };
        const AddSchemaName add_to_check{check_name};
        walk_names(add_to_check);
        if (check.refusal.is_some()) {
            return check.refusal.match([](SchemaRefusal why) noexcept { return SchemaRegistration{::fixy::err(why)}; },
                                       [] noexcept { return SchemaRegistration{::fixy::Unit{}}; });
        }
        NameCheck const& checked = check;
        std::size_t written = 0;
        auto write_name = [this, &checked, &written, allocation](SanitizedSchemaName const& name) noexcept {
            const SchemaNameParts characters = cut_at_zero_(name.parts());
            if (characters.name().size() == 0) return;
            const SchemaHash hash = name.hash();
            const bool is_checked_name = checked.hashes.get(written).match(
                [hash](auto held) noexcept { return *held == hash; }, [] noexcept { return false; });
            CRUCIBLE_FATAL_INVARIANT(is_checked_name);
            const bool is_written = write_name_(allocation, hash, characters, NameSource::outside).is_none();
            CRUCIBLE_FATAL_INVARIANT(is_written);
            ++written;
        };
        const AddSchemaName add_to_table{write_name};
        walk_names(add_to_table);
        CRUCIBLE_FATAL_INVARIANT(written == checked.hashes.size());
        return SchemaRegistration{::fixy::Unit{}};
    }

    // The unqualified form is safe in either phase. The overload taking a
    // sealed view exists so a caller holding the borrow past the registration
    // phase can say so at the call site.  A hash that the table does not hold
    // gives no value.  Complexity: O(log n) and the length of the name.
    [[nodiscard]] ::fixy::Option<InternedSchemaName> lookup(SchemaHash hash) const noexcept {
        const auto found = entries_.get(position_of_(hash));
        for (const auto& entry : found) {
            if (entry->hash == hash) return ::fixy::some(name_of(*entry));
        }
        return ::fixy::none;
    }

    [[nodiscard]] ::fixy::Option<InternedSchemaName> lookup(SealedView const&, SchemaHash hash) const noexcept {
        return lookup(hash);
    }

    // The name without the "aten::" namespace, in the storage of the same
    // copy.  The rest of a name ends with the same zero.
    [[nodiscard]] ::fixy::Option<InternedSchemaName> short_name(SchemaHash hash) const noexcept {
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
    [[nodiscard]] static InternedSchemaName name_of(SchemaEntry const& entry) noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Interned>(
            ::fixy::mint_c_text(entry.name.view()).expect("the table keeps one zero after each name"));
    }

    [[nodiscard]] uint32_t count() const noexcept { return static_cast<uint32_t>(entries_.size()); }

    // The bytes of the names that the table holds, each name with its zero.
    [[nodiscard]] ::fixy::ByteCount name_bytes() const noexcept { return name_bytes_; }

    // The registered entries, sorted by hash.  Before the seal, only the
    // registering thread may read them, because a write can run at the
    // same time on no other thread.  After the seal, any thread may read.
    [[nodiscard]] ::fixy::View<const SchemaEntry> entries() const noexcept { return entries_.view(); }

    // Empties the table and opens it again.  A sealed table that opens again
    // takes writes, so only a test that reuses one table across cases may
    // do this: it takes the proof of a reopen, which only the test context
    // builds, and code that ships cannot mint that context.  The caller
    // makes sure that no other thread uses the table.
    void clear(TableReopen<SchemaTableOf>&& reopen) {
        entries_.clear();
        name_bytes_ = ::fixy::ByteCount{0};
        room_report_ = RoomReport::pending;
        seal_.reopen(reopen);
        CRUCIBLE_POST(0, count() == 0u);
        CRUCIBLE_POST(0, !is_sealed());
    }

private:
    // The source of a name that a registration writes: outside the process,
    // which can give a wrong name, or the program itself, whose wrong name is
    // a defect.
    enum class NameSource : std::uint8_t {
        outside,
        internal,
    };

    // The body of the two overloads of register_name.
    template <SchemaNameSource NameType>
    [[nodiscard]] SchemaRegistration register_name_(WriteSection<SchemaTableOf>& section,
                                                    ::foundation::effects::Alloc allocation, MutableView const& view,
                                                    NameType const& name) {
        CRUCIBLE_PRE(section.names(seal_));
        CRUCIBLE_FATAL_INVARIANT(&view.carrier() == this);
        const SchemaNameParts parts = name.parts();
        if (parts.name().size() == 0) return ::fixy::Unit{};
        constexpr NameSource source =
            std::same_as<NameType, InternalSchemaName> ? NameSource::internal : NameSource::outside;
        return write_name_(allocation, name.hash(), parts, source)
            .match([](SchemaRefusal why) noexcept { return SchemaRegistration{::fixy::err(why)}; },
                   [] noexcept { return SchemaRegistration{::fixy::Unit{}}; });
    }

    // The report of the first refusal of a name for lack of room: still to
    // give, or given.  The table gives it one time between two clears.
    enum class RoomReport : std::uint8_t {
        pending,
        given,
    };

    // The state of the first walk of register_names: the hash of each name
    // that passed, the count and the bytes of the names that the table does
    // not hold, and the first refusal.
    struct NameCheck {
        ::fixy::FixedList<SchemaHash, Capacity> hashes{};
        std::uint32_t added = 0;
        ::fixy::ByteCount added_bytes{0};
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
    // gives each hash one time.  A new hash finds a place while the table and
    // the new hashes of check leave a place for a name from outside
    // (has_room_for_).  Each hash of check is a held hash or a new hash, so
    // check holds at most Capacity hashes.  Complexity: O(log n) and linear
    // in check.
    [[nodiscard]] NameVerdict judge_name_(NameCheck const& check, SchemaHash hash,
                                          SchemaNameParts const& characters) const noexcept {
        for (const SchemaHash earlier : check.hashes.view()) {
            if (earlier == hash) return NameVerdict::collision;
        }
        const ::fixy::Option<InternedSchemaName> held = lookup(hash);
        if (held.is_some()) {
            const bool is_same_name = held.match(
                [&characters](InternedSchemaName const& found) noexcept {
                    return characters.is_same_full_name(SchemaNameParts{found.value().view()});
                },
                [] noexcept { return false; });
            return is_same_name ? NameVerdict::held : NameVerdict::collision;
        }
        const ::fixy::ByteCount pending_bytes{check.added_bytes.raw() + stored_bytes_of_(characters).raw()};
        if (!has_room_for_(::fixy::ElementCount{std::size_t{check.added} + 1}, pending_bytes, NameSource::outside))
            return NameVerdict::full;
        return NameVerdict::added;
    }

    // True when the table has a place for more names of more bytes from
    // source: the names that it holds and the more names are at most the
    // bound of the names, and the bytes of its names and the more bytes are at
    // most the bound of the bytes.  The bounds of an internal name are
    // Capacity and SCHEMA_NAME_BYTES_MAX.  The bounds of a name from outside
    // leave out the room of internal names.  Complexity: constant.
    [[nodiscard]] bool has_room_for_(::fixy::ElementCount more_names, ::fixy::ByteCount more_bytes,
                                     NameSource source) const noexcept {
        const bool is_internal = source == NameSource::internal;
        const std::size_t names_bound = is_internal ? Capacity : Capacity - kInternalRoomNames;
        const std::size_t bytes_bound =
            is_internal ? SCHEMA_NAME_BYTES_MAX.raw() : SCHEMA_NAME_BYTES_MAX.raw() - kInternalRoomBytes.raw();
        return entries_.size() + more_names.raw() <= names_bound && name_bytes_.raw() + more_bytes.raw() <= bytes_bound;
    }

    // Runs inside the write section, so no seal and no other writer runs
    // at the same time.  It gives the refusal of a name from outside, or no
    // value when the table holds the name.  A hash that the table holds
    // keeps its first copy: the same name again writes nothing, and a
    // different name is a collision of two schemas.  A new hash in a table
    // with no room finds no place.  The check of the room comes before the
    // copy of the name and before the pass below, so a refused name
    // allocates nothing and moves no entry.  An internal name ends the
    // process at a collision and at a table with no room.  A new hash goes
    // before the first entry of a larger hash, and each such entry moves one
    // place up, so the entries keep the order of the hashes.  A move of an
    // entry keeps the address of its characters, so a looked-up name stays.
    // The name of a Sanitized name keeps its characters before its first
    // zero.  The count of the table reports at each power of two past
    // SCHEMA_TABLE_CAP.  Complexity: O(log n) for a held hash and for a hash
    // past each held hash, O(n) for each other new hash.
    [[nodiscard]] ::fixy::Option<SchemaRefusal> write_name_(::foundation::effects::Alloc allocation, SchemaHash hash,
                                                            SchemaNameParts const& parts, NameSource source) {
        const SchemaNameParts characters = cut_at_zero_(parts);
        const std::size_t position = position_of_(hash);
        for (const auto& held : entries_.get(position)) {
            if (held->hash == hash) {
                const ::fixy::TextView stored = stored_characters_of_(*held);
                if (characters.is_same_full_name(SchemaNameParts{stored})) return ::fixy::none;
                if (source == NameSource::internal) detail::report_schema_collision(hash, stored, characters);
                return ::fixy::some(SchemaRefusal::collision);
            }
        }
        const ::fixy::ByteCount name_bytes = stored_bytes_of_(characters);
        if (!has_room_for_(::fixy::ElementCount{1}, name_bytes, source)) [[unlikely]] {
            if (source == NameSource::internal) report_full_(characters);
            report_room_once_();
            return ::fixy::some(SchemaRefusal::full);
        }
        entries_
            .insert(::fixy::Index<Capacity>::check(position).expect("a table with room has a place for the entry"),
                    SchemaEntry{.hash = hash, .name = copy_name_(allocation, characters)})
            .expect("the test of the room above found a free entry");
        name_bytes_ = ::fixy::ByteCount{name_bytes_.raw() + name_bytes.raw()};
        if (count() > SCHEMA_TABLE_CAP && ::fixy::has_single_bit(count())) [[unlikely]]
            report_growth_();
        CRUCIBLE_POST(0, is_registered_(hash));
        return ::fixy::none;
    }

    // The bytes that the table keeps for a name: its characters and one zero.
    [[nodiscard]] static ::fixy::ByteCount stored_bytes_of_(SchemaNameParts const& characters) noexcept {
        return ::fixy::ByteCount{characters.full_size() + 1};
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

    // The parts of a name with the characters of its name before the first
    // zero.  Only a name that no check read, which the test door plants,
    // holds a zero.  Complexity: the length of the name.
    [[nodiscard]] static SchemaNameParts cut_at_zero_(SchemaNameParts const& parts) noexcept {
        return SchemaNameParts{::fixy::side_as<side::SchemaBaseName>(characters_of_(parts.name())),
                               ::fixy::side_as<side::SchemaOverload>(parts.overload())};
    }

    // The characters of the name that held keeps.  The stored name ends with
    // its zero, which is no character of the name.
    [[nodiscard]] static ::fixy::TextView stored_characters_of_(SchemaEntry const& held) noexcept {
        const ::fixy::TextView stored = held.name.view();
        return stored.window(0, stored.size() - 1).expect("the table keeps one zero after each name");
    }

    // A table with no room refuses an internal name.  A quiet return leaves
    // each later schema unregistered, and the fallback dispatch that results
    // shows far from the registration that was lost.
    [[noreturn, gnu::cold]] void report_full_(SchemaNameParts const& name) const noexcept {
        ::fixy::fatal("crucible: the schema table holds {} of {} names and {} of {} bytes of names, so it has no "
                      "place for the schema {}{}{}.  Raise the capacity of the table or SCHEMA_NAME_BYTES_MAX in "
                      "crucible/SchemaTable.h, or register fewer schemas",
                      count(), Capacity, name_bytes_.raw(), SCHEMA_NAME_BYTES_MAX.raw(), name.name(),
                      detail::schema_name_dot_of(name), name.overload());
    }

    // The count of the table at a power of two past SCHEMA_TABLE_CAP: the
    // table grows into pages of its static object that no name wrote before.
    [[gnu::cold, gnu::noinline]] void report_growth_() const noexcept {
        ::fixy::report(::fixy::Sink::Err,
                       "crucible: the schema table holds {} of {} names and {} of {} bytes of names\n", count(),
                       Capacity, name_bytes_.raw(), SCHEMA_NAME_BYTES_MAX.raw());
    }

    // The first refusal of a name for lack of room, one time between two
    // clears.  Each later name from outside gets the same refusal.
    void report_room_once_() noexcept {
        if (room_report_ == RoomReport::given) return;
        room_report_ = RoomReport::given;
        report_room_();
    }

    [[gnu::cold, gnu::noinline]] void report_room_() const noexcept {
        ::fixy::report(::fixy::Sink::Err,
                       "crucible: the schema table holds {} of {} names and {} of {} bytes of names, so it refuses "
                       "each new name from outside.  The last {} names and {} bytes are a room for the internal "
                       "names of the program\n",
                       count(), Capacity, name_bytes_.raw(), SCHEMA_NAME_BYTES_MAX.raw(), kInternalRoomNames,
                       kInternalRoomBytes.raw());
    }

    // The characters of the full name, and one zero after them, frozen into a
    // HeapText.  The copy allocates through the allocation capability of the
    // caller.  Complexity: the length of the name.
    [[nodiscard]] static ::fixy::HeapText copy_name_(::foundation::effects::Alloc allocation,
                                                     SchemaNameParts const& parts) {
        if (parts.full_size() > SCHEMA_NAME_MAX) [[unlikely]] {
            ::fixy::fatal("crucible: a schema name is longer than SCHEMA_NAME_MAX characters.  Raise SCHEMA_NAME_MAX "
                          "in crucible/SchemaTable.h");
        }
        ::fixy::GrowText copy = ::fixy::mint_grow_text(SCHEMA_NAME_MAX + 1);
        copy.append(allocation, parts.name()).expect("the name fits below the maximum of its text");
        if (parts.overload().size() != 0) {
            copy.append(allocation, '.').expect("the dot fits below the maximum of its text");
            copy.append(allocation, parts.overload()).expect("the overload fits below the maximum of its text");
        }
        copy.append(allocation, '\0').expect("the zero fits below the maximum of its text");
        return ::fixy::freeze(std::move(copy));
    }

    // The position of the first entry whose hash is not below hash.  The
    // entries are in the order of their hashes.  Complexity: O(log n).
    [[nodiscard]] std::size_t position_of_(SchemaHash hash) const noexcept {
        return ::fixy::find_lower_bound_by_key(
            entries_.view(), [](const SchemaEntry& entry) noexcept { return entry.hash.raw(); }, hash.raw());
    }

    [[nodiscard]] bool is_registered_(SchemaHash hash) const noexcept { return lookup(hash).is_some(); }

    ::fixy::FixedList<SchemaEntry, Capacity> entries_{};
    ::fixy::ByteCount name_bytes_{0};
    RoomReport room_report_ = RoomReport::pending;
    // The phase of the table.  Its one word is an Atomic with acquire and
    // release steps, so two threads write it at the same time.  It is
    // mutable, so seal is a const door: the check of the statement
    // synchronized{} admits a const door that writes only parts that two
    // threads can write (foundation/Transfer.h).
    mutable RegistrationSeal seal_;
};

// The phase gates of the two view doors of a table, found by
// argument-dependent lookup from mint_view.  They are free functions, not
// friends: the rule proof-routes refuses a friend function of a class that
// states synchronized{}, and is_sealed is public.
template <uint32_t Capacity>
[[nodiscard]] bool view_ok(SchemaTableOf<Capacity> const& table, std::type_identity<schema_state::Mutable>) noexcept {
    return !table.is_sealed();
}

template <uint32_t Capacity>
[[nodiscard]] bool view_ok(SchemaTableOf<Capacity> const& table, std::type_identity<schema_state::Sealed>) noexcept {
    return table.is_sealed();
}

// The global table: the names that arrive before the first Vigil seals it.
using SchemaTable = SchemaTableOf<SCHEMA_TABLE_CAP>;

// The late table: the names that arrive after the seal of the global table.
using LateSchemaTable = SchemaTableOf<LATE_SCHEMA_TABLE_CAP>;

// The global is sealed before any second thread starts, so a registration
// must mint its mutable view before that point.  A view minted after it is
// empty.
//
// One table for the process.  A name registered in one shared library is
// read in another, and the seal that one library puts on the table holds in
// every library.
CRUCIBLE_PROCESS_WIDE [[nodiscard]] inline SchemaTable& global_schema_table() {
    static constinit SchemaTable table;
    return table;
}

// The names that arrive after the global table is sealed.  An adapter learns
// the name of an operator when the operator first runs, and the background
// thread of the first Vigil seals the global table before that.  Nothing
// seals this table, and it takes writes only from the thread that holds the
// producer claim of a Vigil.  So only that thread reads its entries while
// the Vigil records.  One table for the process, as for the global table.
// The table is constant-initialized, so its first call writes no entry, and
// a process that registers no late name holds no page of its entries.
CRUCIBLE_PROCESS_WIDE [[nodiscard]] inline LateSchemaTable& late_schema_table() {
    static constinit LateSchemaTable table;
    return table;
}

// The registration of one name in table, in a write section that the call
// takes and ends (SchemaTableOf::register_name).  It gives the refusal sealed
// when the table was sealed after the view was minted, and then nothing is
// registered.  The copy of the name allocates through the allocation
// capability of the caller.  The context of the writer picks the entry of
// the write section (CtxMayEnterWriteSection).
template <uint32_t Capacity, typename Ctx, SchemaNameSource NameType>
    requires CtxMayEnterWriteSection<Ctx>
[[nodiscard]] inline SchemaRegistration
register_schema_name_in(SchemaTableOf<Capacity>& table, Ctx const& writer, ::foundation::effects::Alloc allocation,
                        typename SchemaTableOf<Capacity>::MutableView const& view, NameType const& name) {
    return table.try_enter_write_section(writer).match(
        [&table, allocation, &view, &name](WriteSection<SchemaTableOf<Capacity>> section) {
            return table.register_name(section, allocation, view, name);
        },
        [] { return SchemaRegistration{::fixy::err(SchemaRefusal::sealed)}; });
}

// The registration in the global table (register_schema_name_in).
template <typename Ctx, SchemaNameSource NameType>
    requires CtxMayEnterWriteSection<Ctx>
[[nodiscard]] inline SchemaRegistration register_schema_name(Ctx const& writer, ::foundation::effects::Alloc allocation,
                                                             SchemaTable::MutableView const& view,
                                                             NameType const& name) {
    return register_schema_name_in(global_schema_table(), writer, allocation, view, name);
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
// process there (SchemaTableOf::register_name).  A hash that the sealed
// global table holds keeps its name there: the late table takes no second
// name of it.  The foreground context holds no allocation capability, so the
// caller gives one.  A Vigil gives the one of its startup context.
template <SchemaNameSource NameType>
[[nodiscard]] inline SchemaRegistration
register_schema_name(VigilFgCtx const& fg, ::foundation::effects::Alloc allocation, NameType const& name) {
    // A sealed table mints no view, which is the refusal of a sealed table.
    SchemaTable& global_table = global_schema_table();
    SchemaRegistration in_global = global_table.mint_mutable_view(fg).match(
        [&fg, &global_table, allocation, &name](SchemaTable::MutableView global_view) {
            return register_schema_name_in(global_table, fg, allocation, global_view, name);
        },
        [] { return SchemaRegistration{::fixy::err(SchemaRefusal::sealed)}; });
    if (!is_sealed_refusal(in_global)) return in_global;
    // The global table is sealed, so this thread reads it with no lock.  The
    // same name of a hash that it holds writes nothing, and a different name
    // is a collision, as in one table.  So lookup gives no name of the global
    // table for an op that recorded another name.
    enum class GlobalHold : std::uint8_t {
        absent,
        same_name,
        other_name,
    };
    const SchemaHash hash = name.hash();
    const GlobalHold global_hold = global_table.lookup(hash).match(
        [hash, &name](InternedSchemaName held) noexcept {
            const ::fixy::TextView stored = held.value().view();
            if (name.parts().is_same_full_name(SchemaNameParts{stored})) return GlobalHold::same_name;
            // The generator of the operator table refuses two rows with one
            // hash, and the door of a name from outside refuses a name with
            // the hash of a row (the reserved hashes of
            // vessel/torch/vessel_api_typed.h).  So an internal name meets no
            // other name of its hash in the global table, and this arm is a
            // defect of the program.
            if constexpr (std::same_as<NameType, InternalSchemaName>)
                detail::report_schema_collision(hash, stored, name.parts());
            return GlobalHold::other_name;
        },
        [] noexcept { return GlobalHold::absent; });
    if (global_hold == GlobalHold::same_name) return SchemaRegistration{::fixy::Unit{}};
    if (global_hold == GlobalHold::other_name) return SchemaRegistration{::fixy::err(SchemaRefusal::collision)};
    // Nothing seals the late table, so its view exists and no seal refuses
    // its write.
    LateSchemaTable& late_table = late_schema_table();
    const LateSchemaTable::MutableView late_view =
        late_table.mint_mutable_view(fg).expect("nothing seals the late schema table, so it mints a mutable view");
    SchemaRegistration in_late = register_schema_name_in(late_table, fg, allocation, late_view, name);
    CRUCIBLE_FATAL_INVARIANT(!is_sealed_refusal(in_late));
    return in_late;
}

// The two lookups read the global table first and the late table second.
// While a Vigil records, call them only from the thread that holds its
// producer claim, because the late table is never sealed.
[[nodiscard]] inline ::fixy::Option<InternedSchemaName> schema_name(SchemaHash hash) {
    ::fixy::Option<InternedSchemaName> global_name = global_schema_table().lookup(hash);
    if (global_name.is_some()) return global_name;
    return late_schema_table().lookup(hash);
}

[[nodiscard]] inline ::fixy::Option<InternedSchemaName> schema_short_name(SchemaHash hash) {
    ::fixy::Option<InternedSchemaName> global_name = global_schema_table().short_name(hash);
    if (global_name.is_some()) return global_name;
    return late_schema_table().short_name(hash);
}

}  // namespace crucible
