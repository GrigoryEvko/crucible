#pragma once

// The environment door of fixy: the value of one variable of the environment
// of the process.
//
// read_variable reads the environment through getenv of the C library.  The
// C library owns the environment, and a setenv of the process changes it
// after the start, so /proc/self/environ does not give it.  The body is in
// src/fixy/os/ExitHooks.cpp, the one includer of <stdlib.h>.
//
// The C library finds a variable with a scan of each variable, so a read
// belongs at the start of a program or in a cold path, and not on the hot
// path.  A setenv, unsetenv or putenv of the same variable ends the
// characters of a TextView that the door gave, and a call of one of them in
// another thread at the same time as the read is a data race in the C
// library.  The base calls none of them.

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>

namespace fixy::env {

namespace detail {

// The characters of the value of the variable name, or no value when the
// environment holds no such variable.
[[nodiscard]] ::foundation::core::Option<::foundation::core::TextView>
read_variable_(::foundation::core::CText name) noexcept;

}  // namespace detail

// The value of the variable name of the environment, or no value when the
// environment holds no such variable.  A variable with an empty value gives
// an empty TextView.  The TextView borrows the storage of the environment
// of the C library.
[[nodiscard]] inline ::foundation::core::Option<::foundation::core::TextView>
read_variable(::foundation::core::CText name) noexcept {
    return detail::read_variable_(name);
}

}  // namespace fixy::env
