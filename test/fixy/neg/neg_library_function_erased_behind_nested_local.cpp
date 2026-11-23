// The body of with_library keeps a function of the library in a static
// local, and gives its address to a function template.  In the template, a
// class nested in a local class holds the function, and a CallRef of it goes
// into a static local.  The parent of the nested class is a class and not a
// function, and its one template argument is a value.  So only a walk that
// reads the members of each class of a function finds the scope brand.  A
// CallRef refuses each callable that holds a scope brand.

#include <fixy/os/Loader.h>

#include <foundation/core/CallRef.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/Quarantine.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

template <auto const* Function>
[[nodiscard]] int call_through_nested_class() noexcept {
    struct Outer {
        struct Inner {
            decltype(*Function) function;
            int operator()() noexcept { return function(); }
        };
    };
    typename Outer::Inner inner{*Function};
    static ::foundation::core::CallRef<int() noexcept> const kept{inner};
    return kept();
}

[[maybe_unused]] int calls_after_close(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    return ::fixy::os::with_library(
               ctx, "libc.so.6", ::foundation::core::to(diagnostic),
               [&ctx, &diagnostic]<class Brand>(::fixy::os::Library<Brand> const& library) noexcept {
                   CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a static function of the library escapes to the template")
                   static auto const read_id = ::fixy::os::find_function<int() noexcept>(
                                                   ctx, library, "getpid", ::foundation::core::to(diagnostic))
                                                   .expect("the C library has getpid");
                   CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
                   return call_through_nested_class<&read_id>();
               })
        .expect("the test loads the C library");
}

}  // namespace
