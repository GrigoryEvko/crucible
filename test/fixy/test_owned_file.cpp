// Sentinel TU for fixy/OwnedFile.h: the handle is move-only, the empty
// state answers every query, and close_explicit reports the flush result
// the destructor cannot.
//
// The header's own self-test reaches only the empty state, because a
// real stream needs a file.  This TU opens one through tmpfile, so the
// close path, the release path and the move-assign-over-a-live-handle
// path all run for real.

#include <fixy/OwnedFile.h>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::OwnedFile;

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*));
static_assert(!std::is_copy_constructible_v<OwnedFile>);
static_assert(!std::is_copy_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_default_constructible_v<OwnedFile>);

// A null handle is accepted rather than rejected, so a failed open can
// be handed straight in.
static_assert(std::is_constructible_v<OwnedFile, std::FILE*>);
static_assert(!std::is_convertible_v<std::FILE*, OwnedFile>, "the FILE* constructor is explicit");

int check_live_stream() {
    std::FILE* raw = std::tmpfile();
    if (raw == nullptr) return 0;  // no temp file available; nothing to check

    OwnedFile f{raw};
    if (!f.is_open()) return 10;
    if (!static_cast<bool>(f)) return 11;
    if (f.get() != raw) return 12;

    // close_explicit reports success and empties the handle, so the
    // destructor does not close a second time.
    if (f.close_explicit() != 0) return 13;
    if (f.is_open()) return 14;
    if (f.close_explicit() != 0) return 15;

    return 0;
}

int check_move_transfers_ownership() {
    std::FILE* raw = std::tmpfile();
    if (raw == nullptr) return 0;

    OwnedFile src{raw};
    OwnedFile dst = std::move(src);
    if (src.is_open()) return 20;
    if (!dst.is_open()) return 21;
    if (dst.get() != raw) return 22;

    // Move-assigning over a live handle closes the one being replaced.
    std::FILE* second = std::tmpfile();
    if (second == nullptr) return 0;
    OwnedFile other{second};
    other = std::move(dst);
    if (dst.is_open()) return 23;
    if (other.get() != raw) return 24;

    return 0;
}

int check_release_hands_the_handle_back() {
    std::FILE* raw = std::tmpfile();
    if (raw == nullptr) return 0;

    OwnedFile f{raw};
    std::FILE* out = f.release();
    if (out != raw) return 30;
    if (f.is_open()) return 31;

    // Ownership left the wrapper, so the close is ours.
    if (std::fclose(out) != 0) return 32;
    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::owned_file_self_test::runtime_smoke_test();

    if (int rc = check_live_stream(); rc != 0) return rc;
    if (int rc = check_move_transfers_ownership(); rc != 0) return rc;
    if (int rc = check_release_hands_the_handle_back(); rc != 0) return rc;

    return 0;
}
