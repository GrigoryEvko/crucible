# GCC 16 defects with precompiled headers and C++20 header units

Measured on 2026-10-01 with GCC 16.2.1 (the patched compiler of the tree) and with the stock Fedora GCC 16.2.1. Each defect occurs with both compilers. The flags are the flags of the tree (`-std=c++26 -freflection -fcontracts` and the warning set of `CMakeLists.txt`).

Two experiments tried to share the parse of the headers between the 2,062 negative fixtures: one precompiled header for each include prefix, and header units with include translation (`-fmodules`, a module mapper). Both changed results, so the tree uses neither (CLAUDE.md §XV). This file records the defects for a later retry with GCC 17. We do not report them upstream.

## Contracts

**C1. The contract specifiers of a template are lost.** When GCC writes a header unit or a precompiled header, it drops `pre(...)` and `post(...)` of a function template and of a member function of a class template. A constant evaluation that must fail then passes, and a runtime check disappears from the code. A contract of a non-template function, a contract of a member of a non-template class, and a `contract_assert` statement in a body (also in a template) survive.

```cpp
// h.h, built with -fmodules -fmodule-header=user -x c++-header h.h
#pragma once
template <class T> constexpr int fn(T v) noexcept pre(v > 0) { return static_cast<int>(v); }
template <class T> struct Box { constexpr int member(int v) const noexcept pre(v > 0) { return v; } };
constexpr int plain(int v) noexcept pre(v > 0) { return v; }

// use.cpp, compiled with -fmodules
import "h.h";
constexpr int a = fn(0);              // no error (textual include: error)
constexpr int b = Box<int>{}.member(0); // no error (textual include: error)
constexpr int c = plain(0);           // error, as with a textual include
```

After a header unit exists in `gcm.cache`, a plain `#include "h.h"` under `-fmodules` is translated to the import, and it loses the contracts too. With a precompiled header of 8 such templates, 1 kept its contract. The tree uses `CRUCIBLE_PRE` and `CRUCIBLE_POST` only, which expand to `contract_assert` in the body.

## Header units: results that change

**R1. `source_location_of` names a different file.** `fwd.h` declares `template <int N> struct sized_atom;`. `def.h` includes `fwd.h` and defines the template. A unit that includes `def.h` prints `source_location_of(dealias(^^sized_atom<4>)).file_name()`. Textual: `def.h`. Header units: `./fwd.h`. In the tree, `fixy/Atom.h` then refuses `syscall_only<...>`.

**R2. A header unit does not see what its includer declared before the include.** This is the definition of a header unit, not a defect. An eager seal check in a header, which counts the members of a namespace, then counts only the members of the header. In the tree, the seal checks of `fixy/session/Protocol.h` stop catching a registration before the seal (`neg_sess_combinator_registered_twice`, `neg_sess_payload_rule_before_seal` compile).

**R3. The seal count of `fixy::refined::admitted_implications` is different.** Four `neg_refined_*` fixtures fail with "a member stands outside the seal". No minimal repro.

## Header units: GCC defects

| Name | Repro | Result |
|---|---|---|
| A | `mac.h` with two `#define`s, built as a header unit with `-Wunused-macros`; `t.cpp` includes it with the same flag | segmentation fault in `_cpp_warn_if_unused_macro` (`libcpp/macro.cc:529`): `node->value.macro` is null for a macro that the importer did not load |
| B | `x.h`: `template<class T> struct W {};` `y.h`: `struct U { friend struct W<int>; };` both as header units with `-Wmismatched-tags` | internal compiler error in `diag_mismatched_tags` (`cp/parser.cc:38587`). The same error occurs with a precompiled header |
| C | `<time.h>` as a header unit, then `z.h` that includes `<time.h>` and `<sys/time.h>`, with `-Wmismatched-tags` or `-Wredundant-tags` | false "reference to 'timezone' is ambiguous" |
| D | a contract check in a header unit refers to the built-in `terminate_fn` | "conflicting 'noexcept' specifier for imported declaration 'void std::terminate()'" between two module files |
| E | a `constexpr` or `consteval` function in a header unit with a block-scope `using ns::Type;`, where `Type` comes from another header unit | internal compiler error in `import_entity_module` (`cp/module.cc:4367`). `using Type = ns::Type;` avoids it |
| G | in C++26 mode, `streambuf`, `ios`, `ostream`, then `istream` as system header units | "failed to read compiled module cluster ...: Bad file data". C++20 mode works. 167 fixtures failed from this |
| I | a class in a header unit with `Owner(const Owner&) = delete("Owner holds its storage alone");` and a unit that copies it | the text after "use of deleted function ...:" is missing |
| J | one template instance (for example `Refinement<IsPowerOfTwo{}, unsigned long, false>::value()`) from two module files | "conflicting argument types for imported declaration", often followed by "error reporting routines re-entered" |
| M | `fwd.h`: `namespace ns { template<class T> class Box; }` `def.h`: `template<class T> class ns::Box final { ... };` and a use of `ns::Box<int>` | "incomplete type" |
| N | the header-unit compile of `test/test_ledger.cpp` | does not finish (more than 2,990 s of CPU), in `update_effective_level_from_pragmas` and `linemap_compare_locations` from `-Wdangling-reference` |
| O | a header unit with an inline function that has `contract_assert`, and an importing unit with its own `contract_assert` | internal compiler error in `gimplify_expr` (`gimplify.cc:21341`) when GCC compiles `__tu_has_violation` |
| P | one test unit | internal compiler error from `build_type_attribute_qual_variant` |

## Measurements

- Precompiled header for each include prefix: the fixture CPU fell almost by half, but the wall time after a base-header edit rose from 34 s to 58 s at 192 jobs, because each precompiled header takes 1.5 to 3 times one fixture compile and its group waits for it. The run wrote 28 GB of precompiled headers.
- Header units: the fixture CPU fell by 4.2 to 4.5 times (2,989 s to 617 s of user time at 192 jobs). After an edit of `foundation/Platform.h`, the module files of 357 project headers take about 5 s to build again, on a critical path of 19 headers. With that, all fixtures took 19.4 s against 25.5 s at 192 jobs on a loaded host. 318 fixture verdicts changed.

## Other GCC 16 defect found in the same work

**S1. The driver crashes after SIGXCPU.** When `RLIMIT_CPU` stops a compile with SIGXCPU, the driver `g++` crashes with SIGSEGV in `diagnostics::context::action_after_output` and leaves a core dump. The compile launcher of the tree (`utils/scripts/cost_meter.py`) stops a step with SIGKILL for this reason.
