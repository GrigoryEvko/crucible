// A function returns a View of its parameter by a const reference.  The
// parameter carries the mark CRUCIBLE_LIFETIMEBOUND, and the deleted twin
// refuses a temporary list, which would end before the read of the View.  A
// call with a named list compiles.

#include <foundation/Platform.h>
#include <foundation/core/List.h>
#include <foundation/core/Region.h>

namespace fc = ::foundation::core;

namespace {

using List = fc::FixedList<int, 4>;

fc::View<int const> borrow_of(List const& list CRUCIBLE_LIFETIMEBOUND) { return list.view(); }
void borrow_of(List const&&) = delete("the View borrows the list, and a temporary list ends with the full expression");

List make_list() { return List{}; }

}  // namespace

int main() {
    List const named = make_list();
    fc::View<int const> const of_named = borrow_of(named);
    fc::View<int const> const of_temporary = borrow_of(make_list());
    return static_cast<int>(of_named.size() + of_temporary.size());
}
