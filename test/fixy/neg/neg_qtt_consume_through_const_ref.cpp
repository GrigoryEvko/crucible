// consume is rvalue-qualified and non-const, so a borrower holding a
// const reference cannot take the value out, not even through
// std::move.  The compiler reports the discarded qualifier on the call
// to consume() &&.

#include <fixy/Qtt.h>

#include <utility>

namespace {

int take_from_borrow(fixy::Linear<int> const& borrowed) { return std::move(borrowed).consume(); }

}  // namespace

int main() {
    fixy::Linear<int> owned = fixy::mint_linear<int>(42);
    return take_from_borrow(owned);
}
