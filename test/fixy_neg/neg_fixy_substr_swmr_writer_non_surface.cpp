// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074i fixture #2 for fixy::substr::swmr::mint_swmr_writer
// (token mint, SwmrSession.h:199).  The template-parameter constraint
// `SwmrSessionSurface Swmr` rejects a type that exposes a `writer_tag`
// (so the second parameter `Permission<typename Swmr::writer_tag>&&`
// substitutes cleanly) but is MISSING the rest of the surface
// (value_type, reader_tag, WriterHandle/ReaderHandle, writer()/reader()).
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_writer_wrong_perm_tag.cpp (#1): there the
// permission tag was wrong on a valid surface; here the permission tag
// is CORRECT (FakeWriterTag) and binds, so the ONLY reason for
// rejection is the SwmrSessionSurface concept being unsatisfied.
//
// Expected diagnostic: SwmrSessionSurface / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_swmr_writer_non_surface {
struct FakeWriterTag {};
// Exposes writer_tag so the second parameter substitutes, but lacks the
// remaining SwmrSessionSurface requirements — concept must reject it.
struct FakeSurface {
    using writer_tag = FakeWriterTag;
};
}  // namespace neg_fixy_substr_swmr_writer_non_surface

int main() {
    namespace ns = neg_fixy_substr_swmr_writer_non_surface;
    ns::FakeSurface fake{};

    // Permission tag matches FakeSurface::writer_tag, so the parameter
    // binds; SwmrSessionSurface<FakeSurface> is the failing gate.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_swmr_writer(
            fake,
            fsafe::mint_permission_root<ns::FakeWriterTag>());
    return 0;
}
