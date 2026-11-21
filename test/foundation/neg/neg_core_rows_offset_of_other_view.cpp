// A RowOffset carries the brand of its view.  The offset of the input does
// not read the output, so the walk of rows refuses the body at compile time.
// The body takes the offset of the view that it reads: row.offset(out_view).

#include <foundation/core/Array.h>
#include <foundation/core/StridedRows.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<float, 4> values{};
    ::foundation::core::FixedArray<float, 4> const source{1.0f, 2.0f, 3.0f, 4.0f};
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    auto const input =
        ::foundation::core::mint_strided_view(source.view(), sizes.view(), strides.view(), 0).expect("it fits");
    bool const is_written = values.with_mut([&](auto run) noexcept {
        auto out = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                       .expect("a dense row");
        return ::foundation::core::mint_strided_rows(
                   out,
                   [](auto const& row, auto out_view, auto const& input_view) noexcept {
                       for (auto const step : row.steps()) {
                           out_view.at(row.offset(input_view), step) = input_view.at(row.offset(input_view), step);
                       }
                   },
                   input)
            .is_ok();
    });
    return is_written ? 0 : 1;
}
