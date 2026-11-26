// A FrameRef of a derived object of a frame on one arena converts to a
// FrameRef of its base of a frame on another arena of the same family.  An
// upcast keeps the frame of the object, so the line does not compile.

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
    fc::BumpArena<TestFamily> outer_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> inner_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(outer_arena, [alloc, &inner_arena](auto& outer) noexcept {
        auto const derived = outer.template make<Derived>(alloc);
        fc::with_arena_frame(inner_arena, [alloc, &derived](auto& inner) noexcept {
            auto const base = inner.template make<Base>(alloc);
            std::remove_const_t<decltype(base)> const up = derived;
            static_cast<void>(up);
        });
    });
    return 0;
}
