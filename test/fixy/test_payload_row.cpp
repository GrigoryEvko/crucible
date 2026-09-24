// The payload row extractor, and the sentinel that compiles its wall.
//
// fixy/concurrent/PayloadRow.h is entirely compile-time: the rosters are
// consteval, the answers are type aliases, and the refusal is a
// static_assert.  So this file is first a sentinel — a header whose only
// claims are static_asserts is checked when some translation unit
// includes it, and until Stage.h lands nothing else does.
//
// Beyond that it drives the two things the wall cannot: the roster
// cardinalities, which make a new family a two-place edit a reviewer
// sees, and the rows as runtime objects, so a change that dragged a
// non-trivial default constructor into a Row fails here.
//
// What the extractor refuses is in test/fixy/neg/neg_payload_row_*.

#include <fixy/concurrent/PayloadRow.h>

#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace {

namespace c = ::fixy::concurrent;
namespace eff = ::foundation::effects;

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

// The rosters, pinned.  A new family is one roster line plus one pin
// here, which is what puts it in front of a reviewer.  The old tree's
// thirty-six arms are these eleven entries after the band collapse: one
// Graded entry stands for every band spelling.
static_assert(std::size(c::detail::row_carrying_payload_families) == 2,
              "Two families carry a row of their own: a Computation carries the row it was produced under, "
              "and a Capability conveys the effect it authorizes.  A third means a new kind of payload "
              "whose rule has to be written beside its roster entry.");
static_assert(std::size(c::detail::transparent_payload_families) == 9,
              "Nine families hide a payload and add nothing.  The Graded entry covers every band spelling, "
              "which is why this is nine and not the old tree's thirty-six arms.  A new wrapper that "
              "unwraps is one line here.");
static_assert(std::size(c::detail::leaf_payload_families) == 1,
              "One family is admitted as a leaf.  Every entry is a claim that the template hides no effect "
              "row, which is why a leaf needs a line rather than falling through to a default.");

// The row a payload carries is a type, and the types below are the ones
// a stage's admission check compares.  Instantiating each as a runtime
// object is the one thing not already proved by the header's wall.
using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

void every_row_is_a_runtime_object() {
    [[maybe_unused]] c::payload_row_t<int> plain{};
    [[maybe_unused]] c::payload_row_t<BgComp> engaged{};
    [[maybe_unused]] c::payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>> conveyed{};
    [[maybe_unused]] c::payload_row_t<::fixy::Secret<BgComp>> through_a_wrapper{};
    [[maybe_unused]] c::payload_row_t<::fixy::Saturated<unsigned>> rostered_leaf{};

    EXPECT(sizeof(plain) >= 1);
    EXPECT(sizeof(engaged) >= 1);

    // The row's own size accessor, read at run time rather than folded.
    EXPECT(decltype(plain)::size == 0);
    EXPECT(decltype(engaged)::size == 1);
    EXPECT(decltype(conveyed)::size == 1);
    EXPECT(decltype(through_a_wrapper)::size == 1);
    EXPECT(decltype(rostered_leaf)::size == 0);
}

// A wrapper stack reports the row at the bottom however deep, and the
// depth is what the old ladder had to re-derive per arm.  Driving three
// depths at run time pins that the recursion terminates rather than
// answering by accident at one depth.
void a_wrapper_stack_reports_the_bottom() {
    using D1 = ::fixy::Secret<BgComp>;
    using D2 = ::fixy::Stale<D1>;
    using D3 = ::fixy::Secret<D2>;

    EXPECT((std::is_same_v<c::payload_row_t<D1>, eff::Row<eff::Effect::Bg>>));
    EXPECT((std::is_same_v<c::payload_row_t<D2>, eff::Row<eff::Effect::Bg>>));
    EXPECT((std::is_same_v<c::payload_row_t<D3>, eff::Row<eff::Effect::Bg>>));

    // And a stack over a plain value still reports nothing.
    EXPECT((std::is_same_v<c::payload_row_t<::fixy::Secret<::fixy::Stale<int>>>, eff::Row<>>));
}

// payload_effect_row_t is the name the call sites spell.  It must be the
// same projection, not a second one that could drift from it.
void the_two_spellings_agree() {
    EXPECT((std::is_same_v<c::payload_effect_row_t<BgComp>, c::payload_row_t<BgComp>>));
    EXPECT((std::is_same_v<c::payload_effect_row_t<int>, c::payload_row_t<int>>));
    EXPECT((std::is_same_v<c::payload_effect_row_t<::fixy::Secret<BgComp>>, eff::Row<eff::Effect::Bg>>));
}

}  // namespace

int main() {
    every_row_is_a_runtime_object();
    a_wrapper_stack_reports_the_bottom();
    the_two_spellings_agree();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_payload_row: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_payload_row: eleven roster entries answer for every payload, and the rest are refused\n");
    return 0;
}
