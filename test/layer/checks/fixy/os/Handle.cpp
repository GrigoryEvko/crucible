// The compile-time checks of fixy/os/Handle.h.

#include <fixy/os/Handle.h>

#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace fixy::os::detail::handle_invariants {

inline constexpr RightSet kReadSeek = RightSet::read | RightSet::seek;
inline constexpr RightSet kReadWrite = RightSet::read | RightSet::write;

using ReadFile = Handle<kind::File, kReadSeek>;
using ReadFileRef = HandleRef<kind::File, kReadSeek>;

// The rights: the union and the subset test.
static_assert((RightSet::read | RightSet::write) == kReadWrite && (RightSet::write | RightSet::read) == kReadWrite);
static_assert(HoldsRights<kReadWrite, RightSet::read> && HoldsRights<kReadWrite, kReadWrite>);
static_assert(HoldsRights<RightSet::read, RightSet::none>, "each set holds the empty set");
static_assert(!HoldsRights<RightSet::read, RightSet::write> && !HoldsRights<RightSet::read, kReadWrite>);
static_assert(!HoldsRights<RightSet::none, RightSet::read>);
static_assert(!std::is_convertible_v<std::uint64_t, RightSet> && !std::is_convertible_v<int, RightSet>,
              "no number becomes a set of rights");
static_assert(std::to_underlying(RightSet::ioctl_ptp) >> 32U == 1U, "the ioctl families are the high 32 bits");

// The kinds: the closed table, with a row for each class of namespace kind and
// for the template of the socket kinds.
struct NotAKind final {};
template <class Tag>
struct NotAFamily final {};
static_assert(HandleKind<kind::File> && HandleKind<kind::Directory> && HandleKind<kind::PtpClock>
              && HandleKind<kind::SealedMemory> && HandleKind<kind::CharacterDevice>);
static_assert(HandleKind<kind::Socket<NotAKind>>, "a socket kind takes the tag of its socket");
static_assert(!HandleKind<NotAKind> && !HandleKind<int> && !HandleKind<kind::File const>);
static_assert(!HandleKind<NotAFamily<kind::File>>, "a family of classes has no row of its own");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<
                  ^^kind, [](std::meta::info tag) consteval { return kind_row_of_(tag) < kind_count; }>(),
              "each kind has a row in the table of the kinds, with its proof");

// The proof of each kind that a path can name: the type of its opened number.
using ::fixy::fs::OpenedDescriptor;
using ::fixy::fs::OpenedType;
static_assert(opened_type_of<kind::File>() == OpenedType::regular_file);
static_assert(opened_type_of<kind::CharacterDevice>() == OpenedType::character_device
              && opened_type_of<kind::PtpClock>() == OpenedType::character_device);
static_assert(opened_type_of<kind::Directory>() == OpenedType::of_call
              && opened_type_of<kind::Socket<NotAKind>>() == OpenedType::of_call);

// The handle: one owner, and no descriptor from a number.
static_assert(!std::is_copy_constructible_v<ReadFile> && !std::is_copy_assignable_v<ReadFile>);
static_assert(std::is_nothrow_move_constructible_v<ReadFile> && std::is_nothrow_move_assignable_v<ReadFile>);
static_assert(!std::is_constructible_v<ReadFile, int>, "a public constructor would close a descriptor of a caller");
static_assert(!std::is_constructible_v<ReadFile, OpenedDescriptor<OpenedType::regular_file>&&>,
              "only the doors of an open give an opened number to a handle");
static_assert(!std::is_trivially_copyable_v<ReadFile>, "std::bit_cast must not build a handle");
static_assert(std::is_default_constructible_v<ReadFile>, "the empty handle claims nothing, so it stays reachable");
static_assert(sizeof(ReadFile) == sizeof(int), "a handle is the descriptor and no more");
static_assert(ReadFile::rights == kReadSeek && std::is_same_v<ReadFile::kind_type, kind::File>);

// The borrow: a borrow of the base, with no right that its handle lacks.
template <class Ref, RightSet Fewer>
concept CanRestrict = requires(Ref borrow) { borrow.template restrict<Fewer>(); };

static_assert(std::is_same_v<decltype(std::declval<ReadFile const&>().ref()), ReadFileRef>);
static_assert(!std::is_trivially_copyable_v<ReadFileRef> && std::is_trivially_copy_constructible_v<ReadFileRef>,
              "std::bit_cast must not build a borrow, and the ABI still passes a borrow in a register");
static_assert(sizeof(ReadFileRef) == sizeof(::fixy::fs::DescriptorStamp), "the seal of a borrow adds no byte");
static_assert(sizeof(::fixy::fs::DescriptorStamp) == sizeof(int) + sizeof(std::uint32_t),
              "a stamp is the number and its generation, and no address of a handle");
static_assert(!::foundation::core::HoldsNoBorrow<ReadFileRef>,
              "a borrow of a descriptor states refers, so no frame result carries it out");
static_assert(!::foundation::transfer::Sendable<ReadFileRef> && !::foundation::transfer::Shareable<ReadFileRef>
                  && !::foundation::transfer::SendableBeside<ReadFileRef>
                  && !::foundation::transfer::SendableOwned<ReadFileRef>,
              "a borrow of a descriptor stays on its thread, so no door of a second thread acts on its number");

// The opened number: one owner, and no number of a caller.
static_assert(!std::is_copy_constructible_v<OpenedDescriptor<OpenedType::regular_file>>
                  && !std::is_constructible_v<OpenedDescriptor<OpenedType::regular_file>, int>,
              "an opened number comes only from the door of an open, and no copy gives it to two handles");
static_assert(
    !std::is_constructible_v<OpenedDescriptor<OpenedType::regular_file>, OpenedDescriptor<OpenedType::of_call>&&>,
    "no conversion changes the type that an opened number proves");
static_assert(CanRestrict<ReadFileRef, RightSet::read> && CanRestrict<ReadFileRef, RightSet::none>);
static_assert(!CanRestrict<ReadFileRef, RightSet::write>, "a borrow gives no right that its handle lacks");
static_assert(!std::is_constructible_v<ReadFileRef, int const&>, "no descriptor of a caller becomes a borrow");
static_assert(!std::is_constructible_v<HandleRef<kind::File, kReadWrite>, ReadFileRef>,
              "no conversion adds a right to a borrow");
static_assert(!std::is_default_constructible_v<HandleDoor> && !std::is_copy_constructible_v<HandleDoor>,
              "no object of the door of a descriptor exists");

}  // namespace fixy::os::detail::handle_invariants
