// The member nan_rule of a pointwise op has the type NanRule.  A bool does
// not tell which rule it means, so the walk refuses it at compile time.  The
// op states NanRule::KeepBits or NanRule::Canonical.

#include <foundation/core/Array.h>
#include <foundation/core/Pointwise.h>

#include <cstddef>
#include <utility>

namespace {

struct Copy final {
    static constexpr bool nan_rule = true;

    template <class X>
    constexpr X operator()(X value) const noexcept {
        return value;
    }
};

}  // namespace

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<float, 4>);
    ::foundation::core::FixedArray<float, 4> const source{1.0f, 2.0f, 3.0f, 4.0f};
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    auto const input =
        ::foundation::core::mint_strided_view(source.view(), sizes.view(), strides.view(), 0).expect("it fits");
    bool const is_written = values.with_mut(values_key, [&](auto run) noexcept {
        auto out = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                       .expect("a dense row");
        return ::foundation::core::write_pointwise(out, Copy{}, input).is_ok();
    });
    return is_written ? 0 : 1;
}
