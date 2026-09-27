// Sentinel TU for fixy/OwnedFile.h: the handle is move-only, the empty
// state answers every query, close_explicit reports the flush result the
// destructor cannot, and the only way to a live handle is one of the two
// mints that perform the open.
//
// The empty state is the only one reachable without a file, so it is
// checked first.  Then this TU takes real streams through
// mint_temporary_file and mint_owned_file, so the close path, the release
// path and the move-assign-over-a-live-handle path all run for real.  No
// cell here holds a raw FILE* it did not first receive back through
// release.

#include <fixy/Ctx.h>
#include <fixy/OwnedFile.h>

#include <cerrno>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::OwnedFile;

// Each open takes a context that owns IO and Block.
constexpr ::fixy::TestRunnerCtx kTestIo{::foundation::effects::testing::test()};

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*));
static_assert(!std::is_copy_constructible_v<OwnedFile>);
static_assert(!std::is_copy_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_default_constructible_v<OwnedFile>);

// The construction door, from a scope the class does not befriend.  A
// public FILE* constructor let `OwnedFile{stdin}` close standard input
// on scope exit; there is no such constructor, and no descriptor form
// that could reach a borrowed descriptor through fdopen.
static_assert(!std::is_constructible_v<OwnedFile, std::FILE*>);
static_assert(!std::is_constructible_v<OwnedFile, int>);
static_assert(std::is_default_constructible_v<OwnedFile>);

int check_live_stream() {
    auto opened = ::fixy::mint_temporary_file(kTestIo);
    if (!opened) return 0;  // no temp file available; nothing to check

    OwnedFile f = std::move(*opened);
    if (!f.is_open()) return 10;
    if (!static_cast<bool>(f)) return 11;
    if (f.get() == nullptr) return 12;

    // close_explicit reports success and empties the handle, so the
    // destructor does not close a second time.
    if (f.close_explicit() != 0) return 13;
    if (f.is_open()) return 14;
    if (f.close_explicit() != 0) return 15;

    return 0;
}

int check_move_transfers_ownership() {
    auto opened = ::fixy::mint_temporary_file(kTestIo);
    if (!opened) return 0;

    OwnedFile src = std::move(*opened);
    std::FILE* const raw = src.get();
    OwnedFile dst = std::move(src);
    if (src.is_open()) return 20;
    if (!dst.is_open()) return 21;
    if (dst.get() != raw) return 22;

    // Move-assigning over a live handle closes the one being replaced.
    auto second = ::fixy::mint_temporary_file(kTestIo);
    if (!second) return 0;
    OwnedFile other = std::move(*second);
    other = std::move(dst);
    if (dst.is_open()) return 23;
    if (other.get() != raw) return 24;

    return 0;
}

// The tag that names why a stream leaves a handle without a close here:
// this test closes it by hand.
struct ClosedByTheTest final {};
using ReleaseWitness = ::fixy::atom::leak::resource<ClosedByTheTest>;

int check_release_hands_the_handle_back() {
    auto opened = ::fixy::mint_temporary_file(kTestIo);
    if (!opened) return 0;

    OwnedFile f = std::move(*opened);
    std::FILE* const raw = f.get();
    std::FILE* out = std::move(f).release(ReleaseWitness{});
    if (out != raw) return 30;
    if (f.is_open()) return 31;

    // Ownership left the wrapper, so the close is ours.  This is the one
    // raw FILE* the TU holds, and it came back through the inverse door.
    if (std::fclose(out) != 0) return 32;
    return 0;
}

// The named-path mint hands back the errno when the open fails and a
// live handle when it succeeds, and no handle is built on failure.
int check_open_path() {
    auto missing = ::fixy::mint_owned_file(kTestIo, "/nonexistent-owned-file-test-dir/none", "r");
    if (missing) return 50;
    if (missing.error() != ENOENT) return 51;

    auto null_device = ::fixy::mint_owned_file(kTestIo, "/dev/null", "r");
    if (!null_device) return 0;  // no /dev/null on this host; nothing to check
    OwnedFile f = std::move(*null_device);
    if (!f.is_open()) return 52;
    if (f.close_explicit() != 0) return 53;
    return 0;
}

// The empty state is reachable without a real stream, and every query
// answers on it.  Closing an empty handle reports success, because there
// was nothing whose flush could fail.
int check_empty_state() {
    OwnedFile empty{};
    if (empty.is_open()) return 40;
    if (static_cast<bool>(empty)) return 41;
    if (empty.get() != nullptr) return 42;
    if (empty.close_explicit() != 0) return 44;
    OwnedFile released{};
    if (std::move(released).release(ReleaseWitness{}) != nullptr) return 43;

    OwnedFile moved = std::move(empty);
    if (moved.is_open()) return 45;

    OwnedFile assigned{};
    assigned = std::move(moved);
    if (assigned.is_open()) return 46;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_empty_state(); rc != 0) return rc;
    if (int rc = check_live_stream(); rc != 0) return rc;
    if (int rc = check_move_transfers_ownership(); rc != 0) return rc;
    if (int rc = check_release_hands_the_handle_back(); rc != 0) return rc;
    if (int rc = check_open_path(); rc != 0) return rc;

    return 0;
}
