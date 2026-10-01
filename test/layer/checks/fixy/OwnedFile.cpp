// The compile-time checks of fixy/OwnedFile.h.

#include <fixy/OwnedFile.h>

namespace fixy {

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*), "OwnedFile must be a zero-cost FILE* wrapper");

namespace detail::owned_file_self_test {

static_assert(!std::is_copy_constructible_v<OwnedFile>, "OwnedFile must be move-only — copy would double-close");
static_assert(!std::is_copy_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_default_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_destructible_v<OwnedFile>);

// The construction door, checked from a scope the class does not
// befriend.  A public constructor over a FILE* lets a caller claim a
// stream it never opened, and the destructor closes whatever it holds,
// so `OwnedFile{stdin}` closes standard input on scope exit.  The two
// mints are the only way to a live handle, and each builds one only from
// what libc returned.
static_assert(!std::is_constructible_v<OwnedFile, std::FILE*>,
              "The constructor that claims a stream must not be public.  A caller could hand it stdin, or a "
              "stream another handle owns, and the destructor would fclose it.  Take a handle from "
              "mint_owned_file or mint_temporary_file.");
static_assert(!std::is_constructible_v<OwnedFile, int>,
              "There is no descriptor form either: fdopen over a descriptor owned elsewhere would fclose that "
              "descriptor.  A descriptor is OwnedFd's business, in fixy/os/Fd.h.");
static_assert(std::is_default_constructible_v<OwnedFile>, "The empty handle claims nothing, so it stays reachable.");

// The release door admits a leak atom on an rvalue handle and nothing
// else.  An unrelated type is not a witness, and an lvalue handle keeps
// its stream.
struct NotALeakAtom final {};
struct release_rationale final {};
using SampleLeak = atom::leak::resource<release_rationale>;

template <typename Witness>
concept can_release = requires(OwnedFile handle, Witness witness) { std::move(handle).release(witness); };
template <typename Witness>
concept can_release_lvalue = requires(OwnedFile& handle, Witness witness) { handle.release(witness); };

static_assert(can_release<SampleLeak>);
static_assert(!can_release<NotALeakAtom>);
static_assert(!can_release<int>);
static_assert(!can_release<std::FILE*>);
static_assert(!can_release_lvalue<SampleLeak>, "A release binds only to an rvalue handle.");

namespace fe = ::foundation::effects;
static_assert(CtxFitsFileOpen<fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>>);
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>>,
              "A context that owns IO but not Block cannot wait on the file system.");
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>>,
              "The drain context owns no IO.");
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>, "The foreground context owns no IO.");
static_assert(!CtxFitsFileOpen<int>, "Only an execution context passes the gate.");

// The door has no object, and its opens are not reachable from here.
static_assert(!std::is_default_constructible_v<OwnedFileDoor>);
template <typename Door>
concept CanOpenThroughTheDoor = requires { Door::open_temporary_(); };
static_assert(!CanOpenThroughTheDoor<OwnedFileDoor>, "Only the two mints reach the opens of the door.");

}  // namespace detail::owned_file_self_test

}  // namespace fixy
