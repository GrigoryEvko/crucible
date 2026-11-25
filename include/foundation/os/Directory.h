#pragma once

// The door of a directory listing: open one directory, give the name of each
// entry to a visitor in the order of the kernel, and close the directory.
//
// for_each_entry opens the path with O_DIRECTORY, reads the entries with
// getdents64 into a buffer on the stack of the door, and closes the
// descriptor on each path out of the door.  It gives each name other than
// "." and ".." to the visitor as a CText, because the kernel writes each
// name with a zero after it.  The CText borrows the buffer of the door, so it
// is valid only in the call of the visitor.  The visitor answers
// Visit::proceed for the next entry, or Visit::stop to end the listing.
//
// The door allocates no memory.  An error of the open or of a read is the
// Result of the door, with the error Errno.  The names that the visitor got
// before an error of a read stay given.
//
// A door of the base that holds the descriptor of a directory lists it with
// detail::list_open_directory_.  That listing reads the entries of the
// descriptor from its offset to the end of the directory, with the same
// visitor, and it keeps the descriptor open.  The tree removal of
// fixy/os/Fs.h reads each directory of its walk through it.
//
// The calls of the kernel are in src/foundation/os/Directory.cpp, in a
// function that is not a template, so the ledger of the rule
// syscalls names each call one time.

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <type_traits>

namespace foundation::os {

// The answer of a visitor of for_each_entry.
enum class Visit : std::uint8_t {
    proceed,
    stop,
};

namespace detail {

// The visitor of a listing, with its type removed: the address of the
// visitor of the caller, and the function that calls it.
struct EntryVisit {
    void* visitor = nullptr;
    Visit (*call)(void* visitor, core::CText name) noexcept = nullptr;
};

// The door of the listing (Directory.cpp).
[[nodiscard]] core::Result<core::Unit, Errno> list_directory_(core::CText path, EntryVisit visit) noexcept;

// The door of the listing of the open directory DESCRIPTOR (Directory.cpp).
// It gives each entry from the offset of the descriptor to the end of the
// directory, and it keeps the descriptor open.
[[nodiscard]] core::Result<core::Unit, Errno> list_open_directory_(int descriptor, EntryVisit visit) noexcept;

// The EntryVisit of the visitor visit of a caller.  The EntryVisit holds the
// address of visit, so it is valid while visit lives.
template <class Visitor>
    requires(std::is_nothrow_invocable_r_v<Visit, Visitor&, core::CText> && !std::is_const_v<Visitor>)
[[nodiscard]] EntryVisit erase_visitor_(Visitor& visit) noexcept {
    return EntryVisit{
        .visitor = static_cast<void*>(&visit),
        .call = [](void* visitor, core::CText name) noexcept -> Visit {
            return (*static_cast<Visitor*>(visitor))(name);
        },
    };
}

}  // namespace detail

// Gives the name of each entry of the directory path to visit, in the order
// of the kernel, as the comment at the top of this header tells.  The cost
// is one getdents64 call for each buffer of entries, and one call of visit
// for each entry.
// The visitor is not const, because the door calls it through the address
// of a visitor that it can change.  It holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h): the door calls it and ends it,
// and a call is no consume.  It stores no borrow outside the listing
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h), because the name
// of an entry is valid only in its call.
template <class Visitor>
    requires(std::is_nothrow_invocable_r_v<Visit, Visitor&, core::CText>
             && !std::is_const_v<std::remove_reference_t<Visitor>>)
         && ::foundation::linear::HoldsNoLinearValue<Visitor> && core::StoresNoBorrowOutside<Visitor>
[[nodiscard]] core::Result<core::Unit, Errno> for_each_entry(core::CText path, Visitor&& visit) noexcept {
    return detail::list_directory_(path, detail::erase_visitor_(visit));
}

// A visitor that holds a linear value by value binds to this overload, so the
// refusal gives its reason.
template <class Visitor>
    requires std::is_invocable_r_v<Visit, Visitor&, core::CText> && (!::foundation::linear::HoldsNoLinearValue<Visitor>)
void for_each_entry(core::CText path, Visitor&& visit) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A visitor that can store the name of an entry outside the listing binds to
// this overload, so the refusal gives its reason.
template <class Visitor>
    requires std::is_invocable_r_v<Visit, Visitor&, core::CText> && ::foundation::linear::HoldsNoLinearValue<Visitor>
              && (!core::StoresNoBorrowOutside<Visitor>)
void for_each_entry(core::CText path, Visitor&& visit) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

}  // namespace foundation::os
