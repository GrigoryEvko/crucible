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
// The calls of the kernel are in src/foundation/os/Directory.cpp, in a
// function that is not a template, so the ledger of the rule
// syscall-capability names each call one time.

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

}  // namespace detail

// Gives the name of each entry of the directory path to visit, in the order
// of the kernel, as the comment at the top of this header tells.  The cost
// is one getdents64 call for each buffer of entries, and one call of visit
// for each entry.
// The visitor is not const, because the door calls it through the address
// of a visitor that it can change.
template <class Visitor>
    requires(std::is_nothrow_invocable_r_v<Visit, Visitor&, core::CText>
             && !std::is_const_v<std::remove_reference_t<Visitor>>)
[[nodiscard]] core::Result<core::Unit, Errno> for_each_entry(core::CText path, Visitor&& visit) noexcept {
    using Held = std::remove_reference_t<Visitor>;
    detail::EntryVisit const entry_visit{
        .visitor = static_cast<void*>(&visit),
        .call = [](void* visitor, core::CText name) noexcept -> Visit { return (*static_cast<Held*>(visitor))(name); },
    };
    return detail::list_directory_(path, entry_visit);
}

}  // namespace foundation::os
