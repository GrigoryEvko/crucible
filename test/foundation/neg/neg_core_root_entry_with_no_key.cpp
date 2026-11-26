// A unit names the entry of the root doors with no door that starts a thread,
// inside a frame of an owner that the first call of the entry opened.  Each
// call would mint a key of one brand on one thread.  The entry takes the key of
// the frame doors first, which only a door that starts a thread or a stage
// gets, so a call with no key does not compile.

#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>

namespace {

struct NamedBrand final {};

int forge() noexcept {
    return foundation::core::detail::run_root_body_<0>([](foundation::core::LoanKey<NamedBrand>& outer) noexcept {
        foundation::core::FixedArray<int, 4, NamedBrand> log{};
        log.with_mut(outer, [](auto run) noexcept {
            auto element = run.get_mut(0).expect("the array holds the element");
            *element = 1;
        });
        return log.at(foundation::core::index<0>());
    });
}

}  // namespace

int main() { return forge(); }
