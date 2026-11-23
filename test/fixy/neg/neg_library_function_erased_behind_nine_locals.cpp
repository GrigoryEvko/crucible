// The body of with_library keeps a function of the library in a static
// local, and gives its address to a function template.  The template
// reaches the function through nine local classes, each of which holds the
// class before it by reference, and puts a CallRef of the last class into a
// static local.  The one template argument is a value, so no class names the
// scope brand in a type argument, and only the walk of the members finds it,
// nine steps from the callable.  A CallRef refuses each callable that holds
// a scope brand, at each distance of the walk.

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
[[nodiscard]] int call_through_nine_locals() noexcept {
    struct First {
        decltype(*Function) function;
        int operator()() noexcept { return function(); }
    };
    struct Second {
        First& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Third {
        Second& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Fourth {
        Third& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Fifth {
        Fourth& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Sixth {
        Fifth& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Seventh {
        Sixth& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Eighth {
        Seventh& inner;
        int operator()() noexcept { return inner(); }
    };
    struct Ninth {
        Eighth& inner;
        int operator()() noexcept { return inner(); }
    };
    First first{*Function};
    Second second{first};
    Third third{second};
    Fourth fourth{third};
    Fifth fifth{fourth};
    Sixth sixth{fifth};
    Seventh seventh{sixth};
    Eighth eighth{seventh};
    Ninth ninth{eighth};
    static ::foundation::core::CallRef<int() noexcept> const kept{ninth};
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
                   return call_through_nine_locals<&read_id>();
               })
        .expect("the test loads the C library");
}

}  // namespace
