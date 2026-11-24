// The unique strided view of a tensor takes the ViewMut of a frame by value,
// so it is the one writer of the run.  A const ViewMut writes nothing and
// does not copy, so it gives no unique view.  A reader takes the read view of
// a View of const elements.

#include <crucible/TensorMeta.h>

int main() {
    ::fixy::FixedArray<float, 4> values{};
    ::crucible::TensorMeta const meta{};
    return values.with_mut([&meta](auto run) noexcept -> int {
        auto const& shared = run;
        return static_cast<int>(::crucible::strided_view_of(meta, shared).is_ok());
    });
}
