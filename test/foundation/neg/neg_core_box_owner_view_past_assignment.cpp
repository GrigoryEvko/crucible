// A class that holds its state in a private Box gives a view of the state
// through the owner door of the Box.  The view is a borrow of the holder, so
// the kind stale-borrow of the rule view-escape refuses a read of the view
// after a move assignment to the holder, which ends the state.

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
    holder = Holder{::foundation::core::mint_box<State>(::foundation::effects::testing::test().alloc)};
    return kept.at(::foundation::core::index<0>());
}
