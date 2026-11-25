// Negative compile test.  This file must fail to compile.
//
// Fixture of the writer of an image (detail_ser::Writer of
// crucible/Serialize.h).
//
// Premise: a Writer holds the ViewMut of a frame, so its type names the
// brand of that frame.  The result of a frame body leaves the frame, and it
// names no brand of the frame.  So a body that gives its Writer back through
// a return binds to the refused twin of the frame door.  The same body with
// no return compiles: the Writer writes the header and stays in the body.

#include <crucible/Serialize.h>

#include <cstddef>
#include <utility>

int main() {
    auto [image, image_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::byte, 32>);
    // Must fail: the Writer leaves the body of the frame.
    auto escaped = image.with_mut(image_key, [](auto run) noexcept {
        using Run = decltype(run);
        crucible::detail_ser::Writer<typename Run::brand_type, Run::extent> writer{.image = std::move(run)};
        crucible::detail_ser::write_header(writer, crucible::TraceNodeKind::REGION, crucible::MerkleHash{},
                                           crucible::ContentHash{});
        return writer;
    });
    return escaped.pos == 32 ? 0 : 1;
}
