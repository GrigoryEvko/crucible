// The escape audit walks a carrier's members under
// access_context::unchecked(), so a view stored in a PRIVATE member is
// seen.  Under access_context::current() this carrier audited clean,
// because the walk saw only the members its own namespace may name;
// this fixture is the witness that the private member is reached.  The
// static_assert inside no_scoped_view_field_check fires with the
// audit's own message.

#include <fixy/ScopedView.h>

#include <type_traits>

struct Carrier {
    int v = 0;
};
struct Tag {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept { return true; }

using View = fixy::ScopedView<Carrier, Tag>;

// The view hides behind private access, which is encapsulation and not
// evasion, and is an escape all the same.
class Holder {
    View view_;

public:
    explicit Holder(View v) noexcept : view_{v} {}
    [[nodiscard]] int value() const noexcept { return view_->v; }
};

static_assert(fixy::no_scoped_view_field_check<Holder>());

int main() { return 0; }
