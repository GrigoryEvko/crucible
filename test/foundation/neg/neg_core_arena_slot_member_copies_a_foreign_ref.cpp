// A member function of the class of a holder copies a plain family Ref of
// another arena into a link slot of its own object.  A frame calls no member
// of a holder, and the store of a slot is private to the frame, so the member
// does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Forge {
    std::uint32_t value = 0;
    fc::Option<fc::Ref<Forge, TestFamily>> loose{};
    fc::FrameLink<Forge, TestFamily> next{};

    void copy_loose_link() noexcept { next.set_(loose); }
};

}  // namespace

int main() {
    Forge forge{};
    forge.copy_loose_link();
    return 0;
}
