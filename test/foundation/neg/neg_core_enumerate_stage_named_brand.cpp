// A unit of the base builds the root stage of enumerate() over a run of its
// own with a brand that it names, and reads a Ref of that brand from its
// walk.  The enumerate() verbs of a View and of a FixedArray build the stage
// with the fresh brand of their call, so its constructor is private here.

#include <foundation/core/List.h>

struct NamedBrand {};

int main() {
    foundation::core::FixedList<int, 4> list{};
    foundation::core::View<int, 4> const view = list.view().window<4>(0).expect("the list lends four slots");
    foundation::core::Walk const walk{foundation::core::detail::EnumerateStage<int, 4, NamedBrand, 4>{view}};
    int sum = 0;
    for (auto [index, element] : walk) {
        foundation::core::Ref<int, NamedBrand> const named = element;
        sum += *named + static_cast<int>(index.raw());
    }
    return sum;
}
