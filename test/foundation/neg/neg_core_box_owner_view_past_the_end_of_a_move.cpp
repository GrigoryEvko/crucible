// A class that holds its state in a private Box gives a view of the state
// through the owner door of the Box.  The view is a borrow of the holder.  A
// move of the holder moves the Box, and the state stays alive in the
// destination of the move.  So the kind stale-borrow of the rule view-escape
// follows the borrow to the destination, and it refuses a read of the view
// after the end of the destination.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

struct State {
    ::foundation::core::FixedArray<int, 4> values{};
};

class Holder {
public:
    explicit Holder(::foundation::core::Box<State> state) noexcept : state_{std::move(state)} {}

    [[nodiscard]] ::foundation::core::View<int const, 4> values(this Holder const& self
                                                                CRUCIBLE_LIFETIMEBOUND) noexcept {
        return ::foundation::core::borrow_owned(Key{}, self.state_).values.view();
    }
    void values(this Holder const&& self) =
        delete("the view borrows the state of the holder, which a temporary holder ends");

private:
    using Key = ::foundation::core::box_owner_key<Holder>;
    ::foundation::core::Box<State> state_;
};

int main() {
    Holder holder{::foundation::core::mint_box<State>(::foundation::effects::testing::test().alloc)};
    ::foundation::core::View<int const, 4> const kept = holder.values();
    {
        Holder const moved{std::move(holder)};
        static_cast<void>(moved.values());
    }
    return kept.at(::foundation::core::index<0>());
}
