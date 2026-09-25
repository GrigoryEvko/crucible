// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR token mint fixture 3 for
// safety::proto::swmr_session::mint_swmr_reader
// (token mint, single-argument overload, sessions/SwmrSession.h).  The
// template-parameter constraint `SwmrSessionSurface Swmr` rejects a
// plain type that exposes none of the required surface (value_type,
// writer_tag, reader_tag, WriterHandle/ReaderHandle, writer()/reader()).
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_reader_exclusive_perm.cpp (#4): there a VALID
// surface is supplied but an exclusive Permission is passed where the
// fractional SharedPermission proof is required (two-argument overload);
// here the single-argument overload is selected and the surface itself
// is the failing gate.
//
// Expected diagnostic: SwmrSessionSurface / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/sessions/SwmrSession.h>

namespace fswmr = ::crucible::safety::proto::swmr_session;

namespace neg_fixy_substr_swmr_reader_non_surface {
// No nested types, no writer()/reader() — SwmrSessionSurface must reject.
struct FakeSurface {};
}  // namespace neg_fixy_substr_swmr_reader_non_surface

int main() {
    neg_fixy_substr_swmr_reader_non_surface::FakeSurface fake{};

    [[maybe_unused]] auto bad = fswmr::mint_swmr_reader(fake);
    return 0;
}
