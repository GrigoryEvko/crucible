#pragma once

// The atoms that reach the operating system: the io_uring ring, the
// zero-copy transfers, the file open modes and flags, the durability
// and atomic-commit intents, the mapping protections and share modes,
// the executable-mapping licence, and the deliberate leak.  Every atom
// here engages Axis::SyscallSurface and lifts to the effect row its
// operation exercises, so a gate can fold an atom pack into the row the
// binding's context has to admit.
//
// The tags the atoms take as arguments live beside the family that
// reads them, under fixy::io, fixy::fs and fixy::mmap.  The bit maps
// from a tag to its O_*, PROT_*, MAP_* or IORING_SETUP_* value need the
// OS headers and belong to the headers under fixy/os/, so this header
// includes no <sys/*.h>, <linux/*.h> or <fcntl.h>.
//
// Each tag here reaches a real operation.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Instance.h>

#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::io {

// io_uring_setup(2), Linux 5.1 and later.  The plain read and write
// path needs no ring and so no engine tag.
namespace engine {
struct IoUring final {};
}  // namespace engine

// The two primitives with a uniform fd-to-fd signature.
namespace zerocopy {
struct Sendfile final {};  // sendfile(2), fd to fd, no userspace copy
struct CopyFileRange final {};  // copy_file_range(2), same filesystem or NFSv4.2
}  // namespace zerocopy

// These are bits of one setup word, so a pack may engage several of
// different kinds and they fold together.
namespace ring_flag {
struct IoPoll final {};  // IORING_SETUP_IOPOLL, busy-poll completion
struct SqPoll final {};  // IORING_SETUP_SQPOLL, kernel-side submission thread
struct SingleIssuer final {};  // IORING_SETUP_SINGLE_ISSUER, Linux 6.0 and later
struct CoopTaskrun final {};  // IORING_SETUP_COOP_TASKRUN, Linux 5.19 and later
struct DeferTaskrun final {};  // IORING_SETUP_DEFER_TASKRUN, Linux 6.1 and later
}  // namespace ring_flag

}  // namespace fixy::io

namespace fixy::fs {

namespace open_mode {
struct ReadOnly final {};
struct WriteCreate final {};
struct WriteAppend final {};
struct WriteTruncate final {};
struct ReadWrite final {};
}  // namespace open_mode

namespace flag {
struct CloseOnExec final {};
struct NoFollow final {};
struct DataSync final {};
struct FullSync final {};
struct Direct final {};
}  // namespace flag

namespace sync_op {
struct None final {};  // no syscall, page cache only
struct Fdatasync final {};
struct Fsync final {};
struct FsyncParentDir final {};
}  // namespace sync_op

// Rename uses ::rename, where the last writer wins.  RenameAt2NoReplace uses
// renameat2(RENAME_NOREPLACE), which fails when the target exists instead of
// overwriting it.
namespace atomicity {
struct None final {};
struct Rename final {};
struct RenameAt2NoReplace final {};
}  // namespace atomicity

}  // namespace fixy::fs

namespace fixy::mmap {

// Write and execute are never both set: Exec carries read and execute only.
// A JIT that has to stage writes maps the same pages twice, once writable for
// code generation and once executable to run them, which is the discipline
// hardware execute-only memory assumes anyway.
//
// WriteCopy and ReadWrite carry identical bits and differ only in the share
// mode they are meant to accompany.
namespace prot {
struct ReadOnly final {};
struct WriteCopy final {};  // copy-on-write; goes with share::Private
struct ReadWrite final {};  // goes with share::Shared
struct Exec final {};  // reachable only with the trusted_jit atom
}  // namespace prot

// The first three are primary modes and a mapping has exactly one.  The last
// three are flags that stack on any primary.
namespace share {
struct Private final {};
struct Shared final {};
struct Anonymous final {};  // zero-filled, no file behind it
struct Locked final {};  // keeps pages off swap, against RLIMIT_MEMLOCK
struct Populate final {};  // prefaults every page
struct HugeTLB final {};  // 2 MiB pages
}  // namespace share

// The madvise tags.  Nothing lifts them — advise takes an Advice
// directly, never an atom — so they are tags and not atoms, and they live
// here rather than in fixy/os/Mmap.h beside their only consumer so that
// the one walk of this header covers all ten tag namespaces.
// Their MADV_* values are in fixy/os/Mmap.h, which is the only place that
// can name them, and so is the clause that every tag here has one.
namespace advice {
struct HugePage final {};
struct NoHugePage final {};
struct Collapse final {};
struct Sequential final {};
struct Random final {};
struct WillNeed final {};
struct DontNeed final {};
struct Free final {};
struct WipeOnFork final {};
struct DontDump final {};
}  // namespace advice

}  // namespace fixy::mmap

namespace fixy::atom {

namespace detail {

// Every syscall reached from the io atoms can park the caller — on
// page-cache pressure, on fd-lock contention, on a NUMA-remote page
// fault — so Block is required alongside IO, as for any other
// filesystem-touching call.
using io_row = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// A filesystem syscall crosses the kernel boundary and can park the
// caller until the disk responds, so the context must admit both
// effects.
using fs_row = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// Mapping and unmapping can both park the caller — on page-cache
// pressure, on a NUMA-remote page fault, on write-back — so Block is
// required alongside IO.
using mmap_row = ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// A deliberate leak is the absence of the matching unmap call.  It
// reaches no operation, so it lifts to the empty row: a binding that
// only leaks needs no admission from its context.
using leak_row = ::foundation::effects::Row<>;

using io_atom_of = lifting_atom_of<Axis::SyscallSurface, io_row>;
using fs_atom_of = lifting_atom_of<Axis::SyscallSurface, fs_row>;
using mmap_atom_of = lifting_atom_of<Axis::SyscallSurface, mmap_row>;
using leak_atom_of = lifting_atom_of<Axis::SyscallSurface, leak_row>;

}  // namespace detail

namespace io {

inline constexpr atom_seal atom_namespace_seal{};

template <typename E>
struct engine final : detail::io_atom_of {};

template <typename Z>
struct zerocopy final : detail::io_atom_of {};

template <typename F>
struct ring_flag final : detail::io_atom_of {};

template <std::uint32_t N>
struct sq_entries final : detail::io_atom_of {
    static constexpr std::uint32_t value = N;
};

template <std::uint32_t N>
struct cq_entries final : detail::io_atom_of {
    static constexpr std::uint32_t value = N;
};

}  // namespace io

namespace fs {

inline constexpr atom_seal atom_namespace_seal{};

template <typename Mode>
struct mode final : detail::fs_atom_of {};

template <typename Flag>
struct with_flag final : detail::fs_atom_of {};

template <typename SyncOp>
struct durable final : detail::fs_atom_of {};

template <typename Atomicity>
struct atomic_write final : detail::fs_atom_of {};

}  // namespace fs

namespace mmap {

inline constexpr atom_seal atom_namespace_seal{};

template <typename Prot>
struct with_prot final : detail::mmap_atom_of {};

template <typename Share>
struct with_share final : detail::mmap_atom_of {};

// This atom is what admits an executable mapping.  It asserts that the
// caller has audited the bytes that will run and holds to the discipline
// that keeps writing and executing in separate mappings.
struct trusted_jit final : detail::mmap_atom_of {};

}  // namespace mmap

// This atom is what lets a caller give up a mapped region without
// unmapping it.  Its tag names why that is acceptable — the region was
// handed to a kernel ring buffer, to a socket memory pool, and so on.  Each
// call site declares its own tag, so the reason is in the source the
// reviewer reads and every such site is findable by its tag.
//
// The axis tracks which syscall surface a site engages, so the absence
// of the unmap call belongs on the same axis.
namespace leak {

inline constexpr atom_seal atom_namespace_seal{};

template <typename RationaleTag>
struct resource final : detail::leak_atom_of {};

}  // namespace leak

// Names the types that may authorize a deliberate leak.  Only the leak
// atom satisfies it, so a resource that takes an IsLeakAtom witness
// cannot be talked out of its release by any other type.  fixy::OwnedMmap
// and fixy::OwnedFile read the concept by name.
//
// One reflection query answers it, and the query is written inside the
// concept.  A concept cannot be specialized, so no translation unit can
// add a class to the set that it admits.  A class template or a variable
// template that a gate reads is a door: a specialization for a class of
// a caller mints an authorization for that class.
template <typename G>
concept IsLeakAtom = ::foundation::reflect::IsInstanceOf<G, ^^leak::resource>;

namespace detail {

struct leak_sample_rationale final {};

using io_atom_roster =
    std::tuple<io::engine<::fixy::io::engine::IoUring>, io::zerocopy<::fixy::io::zerocopy::Sendfile>,
               io::zerocopy<::fixy::io::zerocopy::CopyFileRange>, io::ring_flag<::fixy::io::ring_flag::IoPoll>,
               io::ring_flag<::fixy::io::ring_flag::SqPoll>, io::ring_flag<::fixy::io::ring_flag::SingleIssuer>,
               io::ring_flag<::fixy::io::ring_flag::CoopTaskrun>, io::ring_flag<::fixy::io::ring_flag::DeferTaskrun>,
               io::sq_entries<8>, io::cq_entries<16>>;

using fs_atom_roster =
    std::tuple<fs::mode<::fixy::fs::open_mode::ReadOnly>, fs::mode<::fixy::fs::open_mode::WriteCreate>,
               fs::mode<::fixy::fs::open_mode::WriteAppend>, fs::mode<::fixy::fs::open_mode::WriteTruncate>,
               fs::mode<::fixy::fs::open_mode::ReadWrite>, fs::with_flag<::fixy::fs::flag::CloseOnExec>,
               fs::with_flag<::fixy::fs::flag::NoFollow>, fs::with_flag<::fixy::fs::flag::DataSync>,
               fs::with_flag<::fixy::fs::flag::FullSync>, fs::with_flag<::fixy::fs::flag::Direct>,
               fs::durable<::fixy::fs::sync_op::None>, fs::durable<::fixy::fs::sync_op::Fdatasync>,
               fs::durable<::fixy::fs::sync_op::Fsync>, fs::durable<::fixy::fs::sync_op::FsyncParentDir>,
               fs::atomic_write<::fixy::fs::atomicity::None>, fs::atomic_write<::fixy::fs::atomicity::Rename>,
               fs::atomic_write<::fixy::fs::atomicity::RenameAt2NoReplace>>;

using mmap_atom_roster =
    std::tuple<mmap::with_prot<::fixy::mmap::prot::ReadOnly>, mmap::with_prot<::fixy::mmap::prot::WriteCopy>,
               mmap::with_prot<::fixy::mmap::prot::ReadWrite>, mmap::with_prot<::fixy::mmap::prot::Exec>,
               mmap::with_share<::fixy::mmap::share::Private>, mmap::with_share<::fixy::mmap::share::Shared>,
               mmap::with_share<::fixy::mmap::share::Anonymous>, mmap::with_share<::fixy::mmap::share::Locked>,
               mmap::with_share<::fixy::mmap::share::Populate>, mmap::with_share<::fixy::mmap::share::HugeTLB>,
               mmap::trusted_jit>;

using leak_atom_roster = std::tuple<leak::resource<leak_sample_rationale>>;

using os_atom_roster = roster_cat_t<io_atom_roster, fs_atom_roster, mmap_atom_roster, leak_atom_roster>;

// The ten namespaces that the OS tags live in.  A check that walks a
// hand list cannot see what the list omits: a tag added to one of these
// namespaces and forgotten in the list is never checked.  So the walk
// below reads each namespace, and a new tag is checked the moment it is
// declared.
//
// The shape is the one fail_closed::every_class_in_has_edge uses at
// foundation/diag/FailClosed.h: a member that is not a type, is a type
// alias, or is not a class is not a tag declared here and is skipped.
// A class template answers false to is_class_type, which is what keeps
// the parametric atoms out.
//
// What the walk sees is fixed where it runs.  members_of answers about
// the namespace as it stands at that point, and the walk below is a
// template instantiated once, so a class added to one of these
// namespaces after the check that runs the walk is not visible to it.
// Every tag is declared above, which is the case that matters, and
// neg_os_tag_namespace_holds_a_non_tag plants one ahead of the header to
// witness that the walk reads members no roster listed.
inline constexpr std::meta::info os_tag_namespaces[] = {
    ^^::fixy::io::engine,  ^^::fixy::io::zerocopy, ^^::fixy::io::ring_flag, ^^::fixy::fs::open_mode,
    ^^::fixy::fs::flag,    ^^::fixy::fs::sync_op,  ^^::fixy::fs::atomicity, ^^::fixy::mmap::prot,
    ^^::fixy::mmap::share, ^^::fixy::mmap::advice,
};

// Every member of the roster lifts to exactly the row given.
template <class Roster, class ExpectedRow>
[[nodiscard]] consteval bool every_roster_member_lifts_to_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<Roster>) {
        using A = [:member:];
        if constexpr (!::foundation::effects::LiftsToRow<A>) {
            return false;
        } else {
            if constexpr (!std::is_same_v<::foundation::effects::lift_row_t<A>, ExpectedRow>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Every class declared directly in Ns is an empty final type that is
// not an atom: the shape of a tag.
template <std::meta::info Ns>
[[nodiscard]] consteval bool every_class_in_is_tag_() noexcept {
    static_assert(std::meta::is_namespace(Ns),
                  "fixy/atoms/Os.h: every_class_in_is_tag_<Ns> takes a reflection of a namespace, "
                  "written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if constexpr (!std::is_empty_v<T> || !std::is_final_v<T> || IsAtom<T>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// How many tags a namespace declares.  The walk above answers about
// the tags that are there; this is what notices one going missing.
// It never splices, so it takes the namespace as an argument rather
// than as a template parameter.
[[nodiscard]] consteval std::size_t tag_count_in_(std::meta::info ns) noexcept {
    std::size_t count = 0;
    for (const auto member : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(member) || std::meta::is_type_alias(member) || !std::meta::is_class_type(member))
            continue;
        ++count;
    }
    return count;
}

// Every one of the ten namespaces, walked.
[[nodiscard]] consteval bool every_os_tag_namespace_holds_only_tags_() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ns : os_tag_namespaces) {
        if constexpr (!every_class_in_is_tag_<ns>()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval std::size_t os_tag_count_() noexcept {
    std::size_t total = 0;
    for (const auto ns : os_tag_namespaces)
        total += tag_count_in_(ns);
    return total;
}

}  // namespace detail

}  // namespace fixy::atom
