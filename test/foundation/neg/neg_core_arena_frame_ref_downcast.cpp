// A FrameRef of a base converts to a FrameRef of a derived class.  The object
// of the base can be an object of the base alone, so no conversion goes down,
// and the line does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Base {
    using brand_type = TestFamily;

    explicit constexpr Base(fc::ArenaBuildKey<TestFamily, Base> /*key*/) noexcept {}
    Base(Base const&) = delete("a node lives in its arena");
    Base& operator=(Base const&) = delete("a node lives in its arena");
    ~Base() = default;

    std::uint32_t depth = 0;
    fc::FrameLink<Base, TestFamily> next{};
};

struct Derived : Base {
    explicit constexpr Derived(fc::ArenaBuildKey<TestFamily, Derived> key) noexcept
        : Base{std::move(key).for_base<Base>()} {}

    std::uint32_t extra = 0;
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const base = frame.template make<Base>(alloc);
        auto const derived = frame.template make<Derived>(alloc);
        std::remove_const_t<decltype(derived)> const down = base;
        static_cast<void>(down);
    });
    return 0;
}
