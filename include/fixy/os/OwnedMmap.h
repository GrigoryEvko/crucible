#pragma once

// Exclusive ownership of one mmap'd region, unmapped on destruction.
//
// The empty sentinel is nullptr.  The map door gives a failed map as an
// error and no address, and a map with no fixed address is never at address
// zero.  The stored length is exactly the length given to the map, because
// the unmap is called with it verbatim.
//
// The door file src/fixy/os/Mmap.cpp holds the map, the unmap and the
// advice of a region.  It issues them with the raw primitive of
// foundation/os/Syscall.h, so this header includes no system header.
//
// Prot and Share are claims about the region, and the type makes them
// true.  The one door that maps, mint_region, calculates the PROT_* word
// from Prot and the MAP_* word from Share at compile time.  No caller
// gives it a bit.  So a region typed prot::ReadOnly holds a page that the
// kernel mapped read-only, and a region typed share::Anonymous holds a
// private anonymous range.  fixy::numa::mint_numa_placement and the W^X
// rule of prot::Exec each read those claims off the type.
//
// Tag gives each region its own type, so two unrelated mappings cannot be
// swapped at a call boundary.  Brand names one instance of the tag, and
// foundation/Brand.h states the rules of a brand.  mint_region takes the
// exclusive permission of the tag and the brand, so a mapping has the
// identity of that permission.  fixy::mmap::advise_release_aware, the one
// door that discards pages, admits only a permission of that identity.
//
// The constructor over an address is private, and the map doors are its
// only callers.  Each door performs the ::mmap itself, so a region exists
// only over an address that the kernel returned.  Its gate asks for a
// context that owns IO and Block, because a mapping can park the caller.
// The atom-pack mints of fixy/os/Mmap.h build their regions through these
// doors.
//
// Source of a map
//   mint_region has two forms.  A region of the share Anonymous maps no
//   file and takes no descriptor.  A region of a file takes the borrow of
//   the Handle of the file, which holds the right map, and also read and
//   write for a shared map that writes (file_map_rights).  The open mode of
//   the file gives those rights, so a map of a file that the open cannot
//   read does not compile.  map_foreign_region maps a descriptor that a C
//   library opened, such as the map of a BPF object.  It takes the int of
//   the C library and refuses a negative one with ebadf.  Each call of the
//   product code is inside an opt-out region of the class ABI (the ABI
//   descriptor of fixy/os/Handle.h).
//
// The address a mapping lands at is randomized by the kernel and is
// deliberately not reproducible.  No replay path may observe it.

#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Handle.h>
#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// The owner of a mapping, below.  The shared words of fixy::mmap name it as
// the friend that builds them.
template <typename Tag, typename Prot, typename Share, typename Brand>
class OwnedMmap;

}  // namespace fixy

namespace fixy::mmap {

// The PROT_* word of each protection tag and the MAP_* bits of each share
// tag, with the values of foundation/os/MapWords.h.  A tag reaches mmap
// only through a row of these tables, and fixy/os/AtomPack.h says why a
// table is closed: a class template map takes a specialization for a class
// of the caller, and that class can then bring PROT_WRITE | PROT_EXEC to
// the door.  No row is PROT_NONE or a zero share word, so a tag with no row
// is refused rather than mapped as a page that nobody may touch.
inline constexpr ::fixy::atom_pack::tag_row<int> prot_table[] = {
    {^^prot::ReadOnly, ::foundation::os::kProtectRead},
    {^^prot::WriteCopy, ::foundation::os::kProtectRead | ::foundation::os::kProtectWrite},
    {^^prot::ReadWrite, ::foundation::os::kProtectRead | ::foundation::os::kProtectWrite},
    {^^prot::Exec, ::foundation::os::kProtectRead | ::foundation::os::kProtectExecute},
};

inline constexpr ::fixy::atom_pack::tag_row<int> share_table[] = {
    {^^share::Private, ::foundation::os::kMapPrivate},
    {^^share::Shared, ::foundation::os::kMapShared},
    {^^share::Anonymous, ::foundation::os::kMapPrivate | ::foundation::os::kMapAnonymous},
    {^^share::Locked, ::foundation::os::kMapLocked},
    {^^share::Populate, ::foundation::os::kMapPopulate},
    {^^share::HugeTLB, ::foundation::os::kMapHugeTlb | ::foundation::os::kMapHuge2Mb},
};

// A mapping has exactly one primary share mode.  The other share tags are
// flags that stack on a primary.
inline constexpr std::meta::info primary_share_tags[] = {^^share::Private, ^^share::Shared, ^^share::Anonymous};

// A tag is known when its table has a row.  The walk at the foot of
// fixy/os/Mmap.h reads fixy::mmap::prot and fixy::mmap::share and fails if
// a declared tag has no row.
template <typename Prot>
concept MappedProt = ::fixy::atom_pack::has_row(prot_table, ^^Prot);

template <typename Share>
concept MappedShare = ::fixy::atom_pack::has_row(share_table, ^^Share);

// The bits of a tag that has a row.  Each lookup is a function and not a
// variable template, because a caller can specialize a variable template
// for one tag and give it bits of its own.  A function over a reflection
// takes no specialization.  A call for a tag with no row is not a
// constant expression, so it fails the build.
[[nodiscard]] consteval int prot_bits_of(std::meta::info prot_tag) noexcept {
    return ::fixy::atom_pack::value_for(prot_table, prot_tag);
}

[[nodiscard]] consteval int share_flags_of(std::meta::info share_tag) noexcept {
    return ::fixy::atom_pack::value_for(share_table, share_tag);
}

template <typename Share>
concept PrimaryShare = MappedShare<Share> && ::fixy::atom_pack::names_tag(primary_share_tags, ^^Share);

// A modifier of a region is a share flag that stacks on the primary, or
// the trusted_jit atom, which licenses an executable page.
template <typename Modifier>
concept RegionModifier =
    (MappedShare<Modifier> && !PrimaryShare<Modifier>) || std::is_same_v<Modifier, ::fixy::atom::mmap::trusted_jit>;

namespace detail {

// The calls of the door file src/fixy/os/Mmap.cpp.  Each call gives the
// error of the kernel in its Result.

// Maps length bytes with the PROT_* word protection and the MAP_* word
// flags, from offset of the file of descriptor, or memory with no file for a
// descriptor of -1.  The kernel picks the address, and the result is the
// address of the map.
[[nodiscard]] ::foundation::core::Result<void*, ::foundation::os::Errno>
map_region_(std::size_t length, int protection, int flags, int descriptor, std::int64_t offset) noexcept;

// Unmaps the length bytes at address, which a call of map_region_ gave.  The
// kernel refuses an unmap only for an address or a length that no map
// gave, so the door reports no error.
void unmap_region_(void* address, std::size_t length) noexcept;

// Gives the kernel the advice, a MADV_* word, for the length bytes at
// address.  A null address, the address of a region that is not mapped, gives
// EINVAL and makes no system call.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
advise_region_(void* address, std::size_t length, int advice) noexcept;

// The MAP_* bits of a modifier.  The licence adds no bit.
template <typename Modifier>
[[nodiscard]] consteval int modifier_flags() noexcept {
    if constexpr (std::is_same_v<Modifier, ::fixy::atom::mmap::trusted_jit>) {
        return 0;
    } else {
        return share_flags_of(^^Modifier);
    }
}

// The atom that states the modifier, so the row of the mapping is read
// off the atoms as the atom-pack mints read it.
template <typename Modifier>
struct modifier_atom {
    using type = ::fixy::atom::mmap::with_share<Modifier>;
};
template <>
struct modifier_atom<::fixy::atom::mmap::trusted_jit> {
    using type = ::fixy::atom::mmap::trusted_jit;
};

template <typename Prot, typename Share, typename... Modifiers>
using mapping_row_t =
    ::fixy::atom_pack::atoms_row_t<::fixy::atom::mmap::with_prot<Prot>, ::fixy::atom::mmap::with_share<Share>,
                                   typename modifier_atom<Modifiers>::type...>;

}  // namespace detail

}  // namespace fixy::mmap

namespace fixy::detail::owner {

// The fields of an OwnedMmap: the address and the length of the mapping, or
// null and zero when it holds no mapping.  The state is a member of the home
// of the owner states of fixy, which the roster of foundation/core/Owner.h
// names, so the probe of the owners holds it in its roster.  The unmap takes
// the length that the map took.
struct MappingState {
    using owner_state_mark = ::foundation::core::OwnerStateMark;

    void* address = nullptr;
    std::size_t length = 0;

    [[nodiscard]] static constexpr MappingState emptied(MappingState const& /*state*/) noexcept {
        return MappingState{};
    }

    // Unmaps the mapping.  The empty state unmaps nothing.
    static constexpr void release(MappingState const& state) noexcept {
        if (state.address != nullptr) {
            ::fixy::mmap::detail::unmap_region_(state.address, state.length);
        }
    }
};

}  // namespace fixy::detail::owner

namespace fixy {

// The home of the owner states of fixy, for an OwnerHomes list of the probe
// of the owners.
inline constexpr std::meta::info owner_state_home = ^^detail::owner;

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
template <typename Prot, typename Share>
struct owned_mmap;
}  // namespace row_discipline

namespace mmap {

// The gate of mint_region.  Each clause is its own concept, so a refusal
// names the clause that failed.
//
// A mapping can park the caller on page-cache pressure, on a NUMA-remote
// page fault and on write-back, so the atoms of the mapping lift to IO and
// Block, and the context must admit that row.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxAdmitsMapping = ::foundation::effects::IsExecCtx<Ctx>
                        && ::foundation::effects::CtxAdmits<Ctx, detail::mapping_row_t<Prot, Share, Modifiers...>>;

// W^X, read off the bits.  A protection that writes and executes is
// refused under every licence, and a protection that executes needs the
// trusted_jit atom, which states that the caller audited the bytes that
// will run.  The rule reads the PROT_* word and not the tag, so it holds
// for each row of the table.
template <typename Prot, typename... Modifiers>
concept ExecIsLicensed = MappedProt<Prot>
                      && ((prot_bits_of(^^Prot) & (::foundation::os::kProtectWrite | ::foundation::os::kProtectExecute))
                          != (::foundation::os::kProtectWrite | ::foundation::os::kProtectExecute))
                      && ((prot_bits_of(^^Prot) & ::foundation::os::kProtectExecute) == 0
                          || (std::is_same_v<Modifiers, ::fixy::atom::mmap::trusted_jit> || ...));

// Two copies of one modifier fold to the same word, and a repeat names
// nothing new, so the gate refuses it.
template <typename... Modifiers>
concept ModifiersAreDistinct = !(::fixy::atom_pack::OccursMoreThanOnce<Modifiers, Modifiers...> || ...);

template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsRegionMint =
    MappedProt<Prot> && PrimaryShare<Share> && (RegionModifier<Modifiers> && ...) && ModifiersAreDistinct<Modifiers...>
    && ExecIsLicensed<Prot, Modifiers...> && CtxAdmitsMapping<Ctx, Prot, Share, Modifiers...>;

// The rights that a map of a file needs from the handle of the file: map,
// and also read and write for a shared map that writes, because its stores
// reach the file (the source of a map).
template <typename Prot, typename Share>
    requires MappedProt<Prot>
[[nodiscard]] consteval ::fixy::os::RightSet file_map_rights() noexcept {
    bool const is_shared_write =
        std::is_same_v<Share, share::Shared> && (prot_bits_of(^^Prot) & ::foundation::os::kProtectWrite) != 0;
    return is_shared_write ? ::fixy::os::RightSet::map | ::fixy::os::RightSet::read | ::fixy::os::RightSet::write
                           : ::fixy::os::RightSet::map;
}

// The gate of the map of no file: a region of the share Anonymous.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsAnonRegionMint =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && std::is_same_v<Share, share::Anonymous>;

// The gate of the map of a file through a handle with the rights Held.
template <typename Ctx, typename Prot, typename Share, ::fixy::os::RightSet Held, typename... Modifiers>
concept CtxFitsFileRegionMint =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && !std::is_same_v<Share, share::Anonymous>
    && ::fixy::os::HoldsRights<Held, file_map_rights<Prot, Share>()>;

// The gate of the map of a descriptor that a C library opened.
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
concept CtxFitsForeignRegionMap =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...> && !std::is_same_v<Share, share::Anonymous>;

// The gate of OwnedMmap::view.  The view starts the lifetime of its
// elements over the bytes of the mapping, so each subobject of an element
// is an implicit-lifetime type.  A page is aligned to at least 4096 bytes,
// so an element of a larger alignment is refused.  A view that writes
// needs a protection that writes.
template <typename Prot, typename T>
concept MappingViewElement =
    MappedProt<Prot> && ::foundation::core::ViewElement<T>
    && ::foundation::lifetime::ImplicitLifetimeThroughout<std::remove_const_t<T>> && (alignof(T) <= 4096)
    && (std::is_const_v<T> || (prot_bits_of(^^Prot) & ::foundation::os::kProtectWrite) != 0);

// A word that another writer of a shared mapping stores and a reader loads
// whole: an unsigned integer of at most eight bytes whose atomic load needs
// no lock.
template <typename T>
concept SharedWord = ::foundation::core::UnsignedInteger<T> && (sizeof(T) <= sizeof(std::uint64_t))
                  && __atomic_always_lock_free(sizeof(T), 0);

// The byte offset of the first word of a run in a mapping.
using MappingOffset = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

// A record that another writer of a shared mapping stores: a class of whole
// words of eight bytes, which a reader copies one word at a time (the shared
// view below).  Each subobject is an implicit-lifetime type, so a copy of its
// words makes a record.
template <typename T>
concept SharedRecord =
    std::is_class_v<T> && std::is_trivially_copyable_v<T> && ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    && (sizeof(T) % sizeof(std::uint64_t) == 0) && (alignof(T) % alignof(std::uint64_t) == 0) && (sizeof(T) <= 256);

// An element of a shared view: a word or a record.
template <typename T>
concept SharedElement = SharedWord<T> || SharedRecord<T>;

namespace detail {

// A word of a record, for the copy of a record one word at a time.  The type
// can alias each other type, as a char can, so a load of a word reads the
// bytes of the fields of the record with no breach of the type rules.  GCC
// drops the attribute from a template argument, so each form has its own
// name and no template takes it.
using record_word_ [[gnu::may_alias]] = std::uint64_t;
using const_record_word_ [[gnu::may_alias]] = std::uint64_t const;

// The class and the member type of a pointer to a data member.
template <class Member>
struct member_of_;
template <class Class, class Member>
struct member_of_<Member Class::*> {
    using class_type = Class;
    using member_type = Member;
};

}  // namespace detail

// A field of the record T: a pointer to a data member of T whose type is a
// word.
template <auto Field, typename T>
concept SharedField = std::is_member_object_pointer_v<decltype(Field)>
                   && std::same_as<typename detail::member_of_<decltype(Field)>::class_type, T>
                   && SharedWord<typename detail::member_of_<decltype(Field)>::member_type>;

// The commit word of the record T (the commit word of the shared view): a
// field of eight bytes, whose alignment puts it on one whole word.
template <auto Field, typename T>
concept CommitField =
    SharedField<Field, T> && sizeof(typename detail::member_of_<decltype(Field)>::member_type) == sizeof(std::uint64_t);

// Shared view
//   A SharedView is a run of elements of T in a mapping of share::Shared,
//   which another writer stores: the kernel, a BPF program, another process
//   or another thread.  A View reads with plain loads, which the compiler can
//   tear, merge or move, so a reader of such a run gets a data race.  Each
//   access of a SharedView is atomic:
//     load_acquire(index)             a word element
//     load_acquire<Field>(index)      a word field of a record element
//     read_committed<Commit>(index)   a copy of a record element through its
//                                     commit word
//     store_release(index, value), store_release<Field>(index, value) and
//     write_committed<Commit>(index, record), for a run that can be written.
//   An acquire load makes visible each store that the writer made before
//   its store of the word.
//
// Commit word
//   The commit word is a field of eight bytes of the record.  The writer of
//   the commit word exchanges it to zero, stores each other word of the
//   record, and then stores the commit with a release store, and a commit is
//   never zero (write_committed).  read_committed loads the commit word, copies
//   each word of the record with acquire loads, and loads the commit word
//   again.  A zero commit, or two loads that differ, give no value.  So with
//   such a writer the copy never mixes two writes.  With a writer that does
//   not clear the commit word before it writes a record again, each load is
//   still atomic, and no field is torn, but the copy can mix two writes: the
//   whole record is consistent only after that writer clears the commit word
//   first.
//
//   A run of a fixed extent takes the indexed doors, and the Index proves the
//   bound.  A run of the dynamic extent gives size() and window<Count>().
//   The view states refers{}, so it borrows its mapping as a View does.
//   view() and view_at() of a mapping of share::Shared give it.
template <typename T, std::size_t Extent = ::foundation::core::dynamic_extent>
    requires SharedElement<std::remove_const_t<T>>
class [[nodiscard]][[= ::foundation::transfer::refers{}]] SharedView final {
    using Value = std::remove_const_t<T>;
    static constexpr std::size_t kWords = (sizeof(Value) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
    using Words = ::foundation::core::FixedArray<std::uint64_t, kWords>;
    using Word = ::foundation::core::Index<kWords>;
    // The index of an element of a run of a fixed extent, and the range of
    // them.  A run of the dynamic extent has no indexed door, and the count 1
    // only gives the names a type.
    static constexpr std::size_t kIndexCount = Extent != ::foundation::core::dynamic_extent ? Extent : 1;
    using Position = ::foundation::core::Index<kIndexCount>;
    using Indices = ::foundation::core::IndexRange<kIndexCount>;

    T* first_ = nullptr;
    [[no_unique_address]] ::foundation::core::detail::ViewCount<Extent> count_{};
    // No copy of bytes builds a view (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <typename, typename, typename, typename>
    friend class ::fixy::OwnedMmap;

    template <typename U, std::size_t OtherExtent>
        requires SharedElement<std::remove_const_t<U>>
    friend class SharedView;

    // The run of count elements from first, which a mapping proves.
    constexpr SharedView(T* first, std::size_t count) noexcept
        requires(Extent == ::foundation::core::dynamic_extent)
        : first_{first}, count_{count} {}

    constexpr explicit SharedView(T* first) noexcept
        requires(Extent != ::foundation::core::dynamic_extent)
        : first_{first} {}

    // The index of the word that holds the commit word Commit of record.
    template <auto Commit>
    [[nodiscard]] static std::size_t commit_word_of_(T* record) noexcept {
        using Bytes = std::conditional_t<std::is_const_v<T>, unsigned char const, unsigned char>;
        using Raw = std::conditional_t<std::is_const_v<T>, void const, void>;
        Bytes* const start = static_cast<Bytes*>(static_cast<Raw*>(record));
        Bytes* const field = static_cast<Bytes*>(static_cast<Raw*>(&(record->*Commit)));
        return static_cast<std::size_t>(field - start) / sizeof(std::uint64_t);
    }

    // Loads each word of record with an acquire load.  Complexity: one load
    // for each word, at most 32.
    static void copy_words_(T* record, Words& words) noexcept {
        detail::const_record_word_* const first =
            static_cast<detail::const_record_word_*>(static_cast<void const*>(record));
        for (Word const word : Words::indices()) {
            words.with_element(word, [first, word](std::uint64_t& held) noexcept {
                held = __atomic_load_n(first + word.raw(), __ATOMIC_ACQUIRE);
            });
        }
    }

    // Stores each word of words, other than the word skipped, with a release
    // store.  Complexity: one store for each word, at most 32.
    static void store_words_(T* record, Words const& words, std::size_t skipped) noexcept
        requires(!std::is_const_v<T>)
    {
        detail::record_word_* const first = static_cast<detail::record_word_*>(static_cast<void*>(record));
        for (Word const word : Words::indices()) {
            if (word.raw() != skipped) __atomic_store_n(first + word.raw(), words.at(word), __ATOMIC_RELEASE);
        }
    }

public:
    using element_type = T;
    static constexpr std::size_t extent = Extent;

    // The empty run, which names no element.
    constexpr SharedView() noexcept
        requires(Extent == ::foundation::core::dynamic_extent)
    = default;

    // Adds a const to the element, or drops the fixed extent, or both.
    template <typename U, std::size_t OtherExtent>
        requires std::same_as<std::remove_const_t<U>, Value> && (std::is_const_v<T> || !std::is_const_v<U>)
              && (Extent == ::foundation::core::dynamic_extent || Extent == OtherExtent)
              && (!std::same_as<U, T> || Extent != OtherExtent)
    constexpr SharedView(SharedView<U, OtherExtent> other) noexcept : first_{other.first_} {
        if constexpr (Extent == ::foundation::core::dynamic_extent) count_.count = other.size();
    }

    // The count of the elements.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_.value(); }

    // Each index of a run of a fixed extent, in order.
    [[nodiscard]] static constexpr Indices indices() noexcept
        requires(Extent != ::foundation::core::dynamic_extent)
    {
        return Indices{};
    }

    // The Window elements from offset, as a run of the fixed extent Window,
    // or no value when they do not fit.
    template <std::size_t Window>
        requires(Window != ::foundation::core::dynamic_extent) && (Window > 0)
    [[nodiscard]] constexpr ::foundation::core::Option<SharedView<T, Window>>
    window(std::size_t offset) const noexcept {
        using Result = ::foundation::core::Option<SharedView<T, Window>>;
        if (offset > size() || Window > size() - offset) [[unlikely]] {
            return Result{::foundation::core::none};
        }
        T* const first = first_ + offset;
        [[assume(first != nullptr)]];
        return Result::some(SharedView<T, Window>{first});
    }

    // The word element at index, with an acquire load.
    [[nodiscard]] Value load_acquire(Position index) const noexcept
        requires(Extent != ::foundation::core::dynamic_extent) && SharedWord<Value>
    {
        return __atomic_load_n(first_ + index.raw(), __ATOMIC_ACQUIRE);
    }

    // The word field Field of the record at index, with an acquire load.
    template <auto Field>
        requires(Extent != ::foundation::core::dynamic_extent) && SharedField<Field, Value>
    [[nodiscard]] auto load_acquire(Position index) const noexcept {
        return __atomic_load_n(&((first_ + index.raw())->*Field), __ATOMIC_ACQUIRE);
    }

    // A copy of the record at index through its commit word Commit, or no
    // value when no write committed the record or a write overlapped the
    // copy (the commit word).  Complexity: two loads of the commit word and
    // one acquire load for each word of the record.
    template <auto Commit>
        requires(Extent != ::foundation::core::dynamic_extent) && SharedRecord<Value> && CommitField<Commit, Value>
    [[nodiscard]] ::foundation::core::Option<Value> read_committed(Position index) const noexcept {
        T* const record = first_ + index.raw();
        auto const first = __atomic_load_n(&(record->*Commit), __ATOMIC_ACQUIRE);
        if (first == 0) return ::foundation::core::none;
        Words words{};
        copy_words_(record, words);
        if (__atomic_load_n(&(record->*Commit), __ATOMIC_ACQUIRE) != first) return ::foundation::core::none;
        Value copy = __builtin_bit_cast(Value, words);
        copy.*Commit = first;
        return ::foundation::core::some(copy);
    }

    // Stores value in the word element at index, with a release store.
    void store_release(Position index, Value value) const noexcept
        requires(Extent != ::foundation::core::dynamic_extent) && SharedWord<Value> && (!std::is_const_v<T>)
    {
        __atomic_store_n(first_ + index.raw(), value, __ATOMIC_RELEASE);
    }

    // Stores value in the word field Field of the record at index, with a
    // release store.
    template <auto Field>
        requires(Extent != ::foundation::core::dynamic_extent) && SharedField<Field, Value> && (!std::is_const_v<T>)
    void store_release(Position index, typename detail::member_of_<decltype(Field)>::member_type value) const noexcept {
        __atomic_store_n(&((first_ + index.raw())->*Field), value, __ATOMIC_RELEASE);
    }

    // Writes record at index through its commit word Commit (the commit
    // word): an exchange of the commit word to zero, a release store of each
    // other word, and a release store of the commit of record, which is not
    // zero.  Complexity: one exchange and one store for each word.
    template <auto Commit>
        requires(Extent != ::foundation::core::dynamic_extent)
             && SharedRecord<Value> && CommitField<Commit, Value> && (!std::is_const_v<T>)
    void write_committed(Position index, Value const& record) const noexcept {
        CRUCIBLE_PRE(record.*Commit != 0);
        T* const target = first_ + index.raw();
        static_cast<void>(__atomic_exchange_n(&(target->*Commit), 0, __ATOMIC_ACQ_REL));
        Words const words = __builtin_bit_cast(Words, record);
        store_words_(target, words, commit_word_of_<Commit>(target));
        __atomic_store_n(&(target->*Commit), record.*Commit, __ATOMIC_RELEASE);
    }
};

namespace detail {

// The view of the elements of a mapping of the share Share: a SharedView of
// share::Shared, and a View of each other share.
template <bool IsShared, typename T, std::size_t Extent>
struct mapping_view_ {
    using type = ::foundation::core::View<T, Extent>;
};
template <typename T, std::size_t Extent>
struct mapping_view_<true, T, Extent> {
    using type = SharedView<T, Extent>;
};

}  // namespace detail

template <typename Share, typename T, std::size_t Extent = ::foundation::core::dynamic_extent>
using mapping_view_t = typename detail::mapping_view_<std::is_same_v<Share, share::Shared>, T, Extent>::type;

// The gate of a view of a mapping of the share Share: a view element of Prot,
// and a word or a record for share::Shared.
template <typename Prot, typename Share, typename T>
concept MappingViewOf =
    MappingViewElement<Prot, T> && (!std::is_same_v<Share, share::Shared> || SharedElement<std::remove_const_t<T>>);

}  // namespace mmap

// The core of foundation/core/Owner.h gives the moves and the end of a
// mapping.  A move assignment reads the source and empties it before it
// unmaps the old mapping of the target, so a source inside that mapping is
// read before the unmap.
template <typename Tag, typename Prot, typename Share, typename Brand = ::foundation::brand::DefaultBrand>
class [[nodiscard]] OwnedMmap final
    : public ::foundation::core::OwnerCore<::fixy::detail::owner::MappingState, OwnedMmap<Tag, Prot, Share, Brand>> {
    using State = ::fixy::detail::owner::MappingState;
    using Core = ::foundation::core::OwnerCore<State, OwnedMmap>;

    static_assert(::foundation::brand::IsBrand<Brand>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Brand must be an empty class type: the brand of the "
                  "permission the mapping was minted with, or DefaultBrand.");
    static_assert(mmap::MappedProt<Prot>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Prot must be a tag of fixy::mmap::prot.  The type is the "
                  "claim of the protection, so a tag with no PROT_* bits claims nothing.");
    static_assert(mmap::PrimaryShare<Share>,
                  "OwnedMmap<Tag, Prot, Share, Brand>: Share must be a primary share tag of fixy::mmap::share: "
                  "Private, Shared or Anonymous.  The type is the claim of the share mode.");

    // Private, and mint_region is its only caller.  A public one lets any
    // address be claimed, and the destructor unmaps whatever it was
    // claimed over.
    explicit OwnedMmap(void* address, std::size_t length) noexcept : Core{State{address, length}} {}

    // The map of the doors below.  It maps with the bits of Prot, Share and
    // the modifiers, and it builds a region only from an address the kernel
    // returned.  On failure it returns the error of the kernel and builds no
    // region.  The map reads the file of descriptor from the byte at offset,
    // and an anonymous map takes the descriptor -1 and the offset 0.  The
    // body deduces the return type Result<OwnedMmap, Errno>, for the reason
    // that the doors below give.
    template <typename... Modifiers>
    [[nodiscard]] static auto map_(int descriptor, std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        // The body deduces its return type, and CRUCIBLE_TRY returns the error
        // as a type of its own.  So the lambda, which names the return type,
        // reads the Result once.  A region holds the address of its map, so it
        // is no payload of map or of the rvalue match.
        using Mapped = ::foundation::core::Result<OwnedMmap, ::foundation::os::Errno>;
        constexpr int protection = mmap::prot_bits_of(^^Prot);
        constexpr int flags = (mmap::share_flags_of(^^Share) | ... | mmap::detail::modifier_flags<Modifiers>());
        auto const map_region = [descriptor, length, offset]() noexcept -> Mapped {
            CRUCIBLE_TRY(address, mmap::detail::map_region_(length, protection, flags, descriptor, offset.raw()));
            return OwnedMmap{address, length};
        };
        return map_region();
    }

public:
    using tag_type = Tag;
    using prot_type = Prot;
    using share_type = Share;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::owned_mmap<Prot, Share>;
    using row_payload = ::foundation::diag::row_payloads<>;

    // The empty region owns nothing, releases nothing, and claims
    // nothing, so it stays public.
    OwnedMmap() noexcept = default;

    // The map of no file: a region of the share Anonymous (the source of a
    // map).
    //
    // The permission is read and not consumed.  The caller keeps it, and
    // presents it again to fixy::mmap::advise_release_aware.
    //
    // The return type is Result<OwnedMmap, Errno>, and the body deduces it.
    // A declaration of a member is part of the class, so a Result of the
    // class in the declaration names a class that is not yet complete.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsAnonRegionMint<Ctx, Prot, Share, Modifiers...>
    [[nodiscard]] static auto mint_region(Ctx const&,
                                          ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                                          std::size_t length) noexcept {
        return map_<Modifiers...>(-1, length, ::fixy::fs::FileOffset{0});
    }

    // The map of the file of a handle from the byte at offset.  The handle
    // holds the rights that file_map_rights names (the source of a map).
    // The borrow of an empty handle gives ebadf.  The permission is read as
    // in the anonymous form.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx, ::fixy::os::RightSet Rights>
        requires mmap::CtxFitsFileRegionMint<Ctx, Prot, Share, Rights, Modifiers...>
    [[nodiscard]] static auto mint_region(Ctx const&,
                                          ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                                          ::fixy::os::HandleRef<::fixy::os::kind::File, Rights> file,
                                          std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        return map_<Modifiers...>(::fixy::os::detail::HandleDoor::descriptor_of(file), length, offset);
    }

    // The map of a descriptor that a C library opened, such as the map of a
    // BPF object, from the byte at offset (the source of a map).  The
    // descriptor is the int of the C library, and a negative descriptor
    // gives ebadf with no call to the kernel.  The permission is read as in
    // the anonymous form.
    template <typename... Modifiers, ::foundation::effects::IsExecCtx Ctx>
        requires mmap::CtxFitsForeignRegionMap<Ctx, Prot, Share, Modifiers...>
    [[nodiscard]] static auto
    map_foreign_region(Ctx const&, ::foundation::permissions::Permission<Tag, Brand> const& /*owner*/,
                       int abi_descriptor, std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
        using Mapped = ::foundation::core::Result<OwnedMmap, ::foundation::os::Errno>;
        if (abi_descriptor < 0) {
            return Mapped{::foundation::core::err(::foundation::os::Errno::ebadf)};
        }
        return map_<Modifiers...>(abi_descriptor, length, offset);
    }

    OwnedMmap(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");

    // The assignment unmaps the old mapping of this owner after it takes the
    // mapping of other.
    OwnedMmap(OwnedMmap&&) noexcept = default;
    OwnedMmap& operator=(OwnedMmap&&) noexcept = default;

    // A borrow.  Ownership stays here, so the caller must not unmap
    // the returned pointer.
    [[nodiscard]] void* data() const noexcept { return this->state_.address; }
    [[nodiscard]] std::size_t size() const noexcept { return this->state_.length; }
    [[nodiscard]] bool is_mapped() const noexcept { return this->state_.address != nullptr; }

    // The bytes of the mapping as a View of T, with the count from the
    // length of the mapping, or no value when the length is not a whole
    // number of elements.  The call starts the lifetime of the elements
    // over the bytes that the mapping holds, so each element has the value
    // that its bytes give.  An empty region gives an empty View.
    //
    // The View borrows the mapping and must not outlive it.  The overloads
    // for an rvalue are deleted, so no View of a temporary mapping exists.
    // A const mapping gives only a View of const elements.  A mapping of
    // share::Shared gives a SharedView of fixy::mmap in place of a View (the
    // shared view), because another writer stores its bytes.
    template <typename T>
        requires mmap::MappingViewOf<Prot, Share, T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>> view() & noexcept {
        return view_of_<T>();
    }

    template <typename T>
        requires mmap::MappingViewOf<Prot, Share, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>> view() const& noexcept {
        return view_of_<T>();
    }

    template <typename T>
    void view() && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T>
    void view() const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");

    // The count elements of T from the byte offset of the mapping, as a
    // View, or no value when they do not fit in the mapping or when the
    // offset is not a multiple of the alignment of T.  A mapping starts on a
    // page, and the gate refuses an alignment above 4096, so an aligned
    // offset gives an aligned address.  The call starts the lifetime of the
    // count elements over their bytes, and of no other byte, so a mapping can
    // hold a header and then an array of a different type.  A count of zero
    // at an offset inside the mapping gives an empty View.
    //
    // The View borrows the mapping, as the View of view() does, and a mapping
    // of share::Shared gives a SharedView.  The checks are two compares and
    // one test of the low bits of the offset.
    template <typename T>
        requires mmap::MappingViewOf<Prot, Share, T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>> view_at(std::size_t offset,
                                                                                     std::size_t count) & noexcept {
        return view_at_<T>(offset, count);
    }

    template <typename T>
        requires mmap::MappingViewOf<Prot, Share, T> && std::is_const_v<T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>>
    view_at(std::size_t offset, std::size_t count) const& noexcept {
        return view_at_<T>(offset, count);
    }

    // The Count elements of T from the byte offset, as a View of the fixed
    // extent Count, with the checks of view_at(offset, count).
    template <typename T, std::size_t Count>
        requires mmap::MappingViewOf<Prot, Share, T> && (Count != ::foundation::core::dynamic_extent)
              && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T, Count>>
    view_at(std::size_t offset) & noexcept {
        return view_at_<T, Count>(offset);
    }

    template <typename T, std::size_t Count>
        requires mmap::MappingViewOf<Prot, Share, T> && std::is_const_v<T>
              && (Count != ::foundation::core::dynamic_extent) && ::foundation::core::ViewExtent<T, Count>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T, Count>>
    view_at(std::size_t offset) const& noexcept {
        return view_at_<T, Count>(offset);
    }

    template <typename T>
    void view_at(std::size_t, std::size_t) && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T>
    void view_at(std::size_t,
                 std::size_t) const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T, std::size_t Count>
    void view_at(std::size_t) && = delete("a View of a temporary mapping dangles when the mapping unmaps");
    template <typename T, std::size_t Count>
    void view_at(std::size_t) const&& = delete("a View of a temporary mapping dangles when the mapping unmaps");

    // Hands the region to something that will unmap it on its own
    // schedule, such as a subsystem the kernel takes over.  Almost
    // every transfer of ownership is a move instead.
    //
    // Two gates keep this from being reached by accident.  The witness
    // parameter has to be a leak atom, which rejects an unrelated
    // argument at overload resolution.  And the method binds only to an
    // rvalue, so releasing twice needs a second explicit move; together
    // with the sentinel swap on the way out, a double unmap cannot be
    // written.
    //
    // The witness is taken by value and is empty, so the rationale
    // lives in the type and costs nothing at run time.  The owner is empty
    // when the call returns, and no unmap runs.
    template <atom::IsLeakAtom LeakAtom>
    [[nodiscard]] std::pair<void*, std::size_t> release(LeakAtom) && noexcept {
        State held = this->take_state_();
        return {held.address, held.length};
    }

private:
    static constexpr bool is_shared_ = std::is_same_v<Share, mmap::share::Shared>;

    // The view of count elements from first, which a fit proved: a
    // SharedView of a mapping of share::Shared, and a View of each other.
    template <typename T>
    [[nodiscard]] mmap::mapping_view_t<Share, T> run_of_(T* first, std::size_t count) const noexcept {
        if constexpr (is_shared_) {
            return mmap::SharedView<T>{first, count};
        } else {
            return ::foundation::core::View<T>{this->run_key_(), first, count};
        }
    }

    template <typename T, std::size_t Count>
    [[nodiscard]] mmap::mapping_view_t<Share, T, Count> run_of_(T* first) const noexcept {
        if constexpr (is_shared_) {
            return mmap::SharedView<T, Count>{first};
        } else {
            return ::foundation::core::View<T, Count>{this->run_key_(), first};
        }
    }

    template <typename T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>> view_of_() const noexcept {
        using Result = ::foundation::core::Option<mmap::mapping_view_t<Share, T>>;
        if (!is_mapped()) return Result::some(mmap::mapping_view_t<Share, T>{});
        if (this->state_.length % sizeof(T) != 0) return Result{::foundation::core::none};
        std::size_t const count = this->state_.length / sizeof(T);
        return Result::some(run_of_<T>(start_at_<T>(0, count), count));
    }

    // True when count elements of T from the byte offset lie inside the
    // mapping, and the offset is a multiple of the alignment of T.  The
    // compares take no sum, so no count overflows.
    template <typename T>
    [[nodiscard]] bool fits_at_(std::size_t offset, std::size_t count) const noexcept {
        std::size_t const length = this->state_.length;
        return offset <= length && count <= (length - offset) / sizeof(T) && (offset & (alignof(T) - 1)) == 0;
    }

    // Starts the lifetime of count elements of T at the byte offset, which
    // fits_at_ accepted, and gives the first of them.
    template <typename T>
    [[nodiscard]] T* start_at_(std::size_t offset, std::size_t count) const noexcept {
        unsigned char* const bytes = static_cast<unsigned char*>(this->state_.address) + offset;
        return ::foundation::lifetime::start_as_array<std::remove_const_t<T>>(bytes, count);
    }

    template <typename T>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T>>
    view_at_(std::size_t offset, std::size_t count) const noexcept {
        using Result = ::foundation::core::Option<mmap::mapping_view_t<Share, T>>;
        if (!fits_at_<T>(offset, count)) return Result{::foundation::core::none};
        return Result::some(run_of_<T>(start_at_<T>(offset, count), count));
    }

    // A View of a fixed extent holds at least one element, so a fit gives
    // a mapping with an address that is not null.
    template <typename T, std::size_t Count>
    [[nodiscard]] ::foundation::core::Option<mmap::mapping_view_t<Share, T, Count>>
    view_at_(std::size_t offset) const noexcept {
        using Result = ::foundation::core::Option<mmap::mapping_view_t<Share, T, Count>>;
        if (!fits_at_<T>(offset, Count)) return Result{::foundation::core::none};
        return Result::some(run_of_<T, Count>(start_at_<T>(offset, Count)));
    }
};

namespace detail {

// A sample mapping type.  The check file of this header and
// test/fixy/row_hash_census.h read it.
struct DummyTag {
    using permission_row = ::foundation::effects::Row<>;
};
using SmokeOwnedMmap = OwnedMmap<DummyTag, mmap::prot::ReadOnly, mmap::share::Private>;

}  // namespace detail

}  // namespace fixy
