// The body of a walk of rows gives no value, so a RowIndex, a RowStep or a
// RowOffset of the walk does not leave it and cannot be used after its loop.
// A body that returns the index of its row is refused at compile time.

#include <foundation/core/Array.h>
#include <foundation/core/StridedRows.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<float, 4> values{};
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    // The outer lambda states its result, so with_mut reads no body of it,
    // and the walk of rows gives the only error.
    bool const is_written = values.with_mut([&](auto run) noexcept -> bool {
        auto out = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                       .expect("a dense row");
        return ::foundation::core::mint_strided_rows(out,
                                                     [](auto const& row, auto out_view) noexcept {
                                                         for (auto const step : row.steps()) {
                                                             out_view.at(row.offset(out_view), step) = 1.0f;
                                                         }
                                                         return row.index();
                                                     })
            .is_ok();
    });
    return is_written ? 0 : 1;
}
