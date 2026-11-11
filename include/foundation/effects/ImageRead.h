#pragma once

// The checked read of the image of a count or a clock.
//
// A count of StrongCounterLattice.h and a clock of HappensBefore.h come
// only from their own doors.  An image that crossed a wire or came back
// from storage becomes a count or a clock again only through
// mint_from_image here.  The read needs a context that owns IO, so a pure
// function or the foreground dispatch path cannot build a count from bytes.
//
// The lattices are below the effect layer, so they cannot name a context.
// Each one gives a read that takes a CountImageKey, and the image reader
// below is the one class that builds such a key.  The reader gives the key
// only to mint_from_image, its one friend.  The rule home admits the
// definition of the reader only in this header, so no other unit defines a
// reader that builds the key.

#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace foundation::effects {

// A lattice whose elements have an image: a counter or a clock whose tag
// has a source path.
template <class Lattice>
concept ImageLattice = requires(::foundation::algebra::lattices::CountImageKey key,
                                typename Lattice::image_type const& image) { Lattice::read_image(key, image); };

// The read of an image admits a context that owns IO, and a lattice whose
// elements have an image.
template <class Lattice, class Ctx>
concept CtxFitsImageRead = CtxOwnsCapability<Ctx, Effect::IO> && ImageLattice<Lattice>;

// The checked read.  It gives the element, or the reason that the lattice
// refused the image: an image of another axis is a value of that axis.
template <class Lattice, class Ctx>
    requires CtxFitsImageRead<Lattice, Ctx>
[[nodiscard]] constexpr auto mint_from_image(Ctx const& ctx, typename Lattice::image_type const& image) noexcept;

namespace host {

// The one owner of the key of an image read.  The key door is private, and
// mint_from_image is its one friend, so the key reaches a read only through
// a context that owns IO.  The reader holds no state, and no code builds a
// reader.
//
// Each constructor is private and user-provided, as for the keys of
// foundation/effects/Effect.h.  A class with a trivial copy is trivially
// copyable, also when its copies are deleted, and std::bit_cast then builds
// one from any byte.
struct ImageReader final {
private:
    constexpr ImageReader() noexcept {}
    constexpr ImageReader(ImageReader const&) noexcept {}

    [[nodiscard]] static constexpr ::foundation::algebra::lattices::CountImageKey key() noexcept { return {}; }

    template <class Lattice, class Ctx>
        requires CtxFitsImageRead<Lattice, Ctx>
    friend constexpr auto ::foundation::effects::mint_from_image(Ctx const& ctx,
                                                                 typename Lattice::image_type const& image) noexcept;
};

}  // namespace host

template <class Lattice, class Ctx>
    requires CtxFitsImageRead<Lattice, Ctx>
[[nodiscard]] constexpr auto mint_from_image(Ctx const&, typename Lattice::image_type const& image) noexcept {
    return Lattice::read_image(host::ImageReader::key(), image);
}

}  // namespace foundation::effects
