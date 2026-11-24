// A const ViewMut writes nothing, so it gives no writer to erase_brand.  A
// door that erased it would give a writer of a run that its holder only reads.

#include <foundation/core/Array.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    auto const count = values.with_mut([](auto run) noexcept -> std::size_t {
        auto const& reader = run;
        return ::foundation::core::erase_brand(reader).size();
    });
    return static_cast<int>(count);
}
