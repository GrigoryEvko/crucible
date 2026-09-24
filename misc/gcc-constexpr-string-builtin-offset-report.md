# GCC report: constant evaluation of memchr, strchr, strrchr and strstr counts an argument offset two times

Status: prepared, not filed. The user decides when to file the Bugzilla report
and when to send the patch to gcc-patches.

This file holds two texts:

1. The Bugzilla report.
2. The patch submission for gcc-patches, with a checklist of the work that is
   necessary before submission.

---

## 1. Bugzilla report

### Fields

| Field | Value |
|---|---|
| Product / Component | gcc / c++ |
| Keywords | wrong-code |
| Known to work | 9.3 |
| Known to fail | 10.1, 11.1, 12.2, 13.3, 14.2, 15.1, 16.2.1 |
| Trunk | master at 04106ce784b has the same code (read, not run) |
| Host / Target | x86_64-pc-linux-gnu |
| Title | [Regression] constexpr __builtin_memchr/strchr/strrchr/strstr count an offset in the first argument two times |

The title needs the regression marker of the branches that are open on the
day of filing, for example `[14/15/16/17 Regression]`. Do a check of the open
branches before filing.

### Description

The constant evaluator gives a wrong pointer for `__builtin_memchr`,
`__builtin_strchr`, `__builtin_strrchr` and `__builtin_strstr` when the first
argument points into a string literal or a constexpr array at a non-zero
offset. The offset of the argument is added two times.

For `"0,1,2,3" + 2`, the first `','` is at offset 1 from the argument. The
evaluator gives offset 3. The run-time answer is 1.

The defect is not specific to C++26. `strchr` gives the wrong answer in C++20
and in C++23 as well. In C++26, the cast from `void *` is a constant expression
(P2738), so the `memchr` form becomes reachable too. Through `memchr`, the
libstdc++ search routines also become reachable, and the wrong value then goes
into code that runs at run time (testcase 3).

### Testcase 1: strchr, every standard from C++11

```cpp
constexpr long strchr_offset (const char *p) { return __builtin_strchr (p, ',') - p; }
static_assert (strchr_offset ("0,1,2,3" + 2) == 1, "");   // fails: the value is 3
```

```
$ g++ -std=c++20 -fsyntax-only t1.C        # g++ (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)
t1.C:2:46: error: static assertion failed
    2 | static_assert (strchr_offset ("0,1,2,3" + 2) == 1, "");
      |                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~
  • the comparison reduces to '(3 == 1)'
```

The results for 10.1 through 15.1 come from this testcase on Compiler
Explorer. 9.3 gives the correct value there.

### Testcase 2: memchr, C++26

```cpp
#include <cstddef>
#include <cstdio>
constexpr std::size_t comma_at (const char *s, std::size_t n) noexcept {
  const void *hit = __builtin_memchr (s, ',', n);
  return hit ? static_cast<std::size_t> (static_cast<const char *> (hit) - s) : n;
}
constexpr std::size_t kAtTwo = comma_at ("0,1,2,3" + 2, 5);   // expected 1
int main () {
  const char *volatile rt = "0,1,2,3";
  std::printf ("%zu %zu\n", kAtTwo, comma_at (rt + 2, 5));
}
```

```
$ g++ -std=c++26 -O0 t2.C && ./a.out
3 1                                    (expected: 1 1)
```

With `-std=c++23`, this testcase is rejected with "cast from 'const void*' is
not allowed in a constant expression before C++26".

### Testcase 3: wrong value at run time, through std::string_view::find

At `-O1` and above, the front end tries to fold a call with constant
arguments. That fold is not a manifest constant evaluation, so
`std::is_constant_evaluated ()` is false there, and
`std::char_traits<char>::find` calls `__builtin_memchr`. The wrong offset then
becomes a constant in the generated code.

```cpp
#include <cstddef>
#include <cstdio>
#include <string_view>
constexpr std::size_t sum_digits (std::string_view s) noexcept {
  std::size_t total = 0;
  while (!s.empty ()) {
    const std::size_t comma = s.find (',');
    const std::string_view entry = s.substr (0, comma);
    s = (comma == std::string_view::npos) ? std::string_view{} : s.substr (comma + 1);
    for (const char c : entry) total += static_cast<std::size_t> (c - '0') + 1;
  }
  return total;
}
int main () {
  std::printf ("literal=%zu", sum_digits ("0,1,2,3"));
  const char *volatile rt = "0,1,2,3";
  std::printf (" runtime=%zu\n", sum_digits (rt));
}
```

| Command | Output |
|---|---|
| `g++ -std=c++26 -O1 t3.C` | `literal=7 runtime=10` (wrong) |
| `g++ -std=c++26 -O0 t3.C` | `literal=10 runtime=10` |
| `g++ -std=c++23 -O1 t3.C` | `literal=10 runtime=10` |

`-fdump-tree-original` already holds `printf ("literal=%zu", 7)`.

The same path is open through these libstdc++ routines, each when
`!__is_constant_evaluated ()`:

- `std::char_traits<char>::find` (bits/char_traits.h), which
  `std::string_view::find`, `find_first_of`, `find_last_of`, the `_not_of`
  forms and the `std::string` forms use;
- `std::find` on byte types (bits/stl_algo.h);
- `std::ranges::find` (bits/ranges_util.h).

### Scope

All values come from `"0,1,2,3" + 2` in manifest constant evaluation, at
`-O0`.

| Probe | C++20 | C++23 | C++26 | Correct |
|---|---|---|---|---|
| strchr offset | 3 | 3 | 3 | 1 |
| strrchr offset | 5 | 5 | 5 | 3 |
| strstr(",2") offset | 3 | 3 | 3 | 1 |
| memchr offset | rejected | rejected | 3 | 1 |
| strpbrk, strspn, strcspn | 1 | 1 | 1 | 1 |
| strlen, memcmp, strcmp | correct | correct | correct | — |
| strchr, strrchr, strstr on `arr + 2` (constexpr array) | wrong | wrong | wrong | — |
| any of them on `&arr[2]`, `&lit[2]` or `arr` | correct | correct | correct | — |

The middle-end fold is correct. A plain non-constexpr, non-inline function at
`-O1`, `-O2` and `-O3` gives 1 for `memchr`, `strchr` and `__builtin_memchr`
on `"0,1,2,3" + 2`.

### Cause

In `gcc/cp/constexpr.cc`, `cxx_eval_builtin_function_call`:

1. `memchr`, `strchr`, `strrchr` and `strstr` get `strret = 1`: the result
   points into argument 0.
2. If argument 0 is an `ADDR_EXPR` whose operand evaluates to a whole
   `STRING_CST`, the evaluator replaces the argument with `&STRING_CST`. For
   any other argument (a parameter, or `lit + 2`), the folder gets the fully
   evaluated value.
3. After the fold, the evaluator always puts the original argument back as
   operand 0 of the folded `POINTER_PLUS_EXPR`. It does this also when step 2
   did not replace the argument.

`fold_const_call` in `gcc/fold-const-call.cc` returns
`fold_build_pointer_plus_hwi (arg0, r - p0)`, and that call merges an offset
that `arg0` already has into its own offset. For testcase 2, the tree shapes
are these (seen in gdb):

- the argument that the folder gets: `(const char *) "0,1,2,3" + 2`;
- the folder result: `(void *) "0,1,2,3" + 3`;
- the result after step 3: `VIEW_CONVERT_EXPR<const char *>(s) + 3`.

`s` already points at offset 2, so the offset 2 counts two times.

The other results follow from this:

- strlen, strnlen, memcmp and strcmp return an integer, so step 3 does not
  apply.
- strpbrk, strspn, strcspn, index and rindex do not set `strret`.
- `&lit[2]` and `&arr[2]` are correct only because the folder does not merge
  an offset into an `ADDR_EXPR` base.

The unconditional rebase came in with r10-5927 (commit 69dc042f91c7,
"PR c++/80265 - constexpr __builtin_mem*", 2020-01-10). Commit 6f346913f2a8
(PR c++/93331) later added the `oarg` fallback, but did not change the
rebase. GCC 9.3 gives the correct value, and GCC 10.1 through trunk give the
wrong value.

### Related

The Corvid project found the same defect and carries a similar patch:
<https://github.com/stevensudit/Corvid/pull/108>. That project says its
upstream report is drafted and not filed. We found no Bugzilla report for
this defect. The automated Bugzilla search was blocked, so do a manual search
before filing.

---

## 2. Patch for gcc-patches

### Before submission

The patch was tested on a non-bootstrap build of releases/gcc-16. GCC asks
for more before a patch is sent:

- [ ] Rebase on master. One line of context in the first hunk is different
      there: master added `bool bos = false;`.
- [ ] Bootstrap on x86_64-pc-linux-gnu, and run the full `make check` against
      a clean baseline. Record "Bootstrapped and regtested on
      x86_64-pc-linux-gnu" only after this passes.
- [ ] Put the Bugzilla PR number in the subject, as `[PRnnnnn]`, and in both
      ChangeLog entries, as `PR c++/nnnnn`.
- [ ] Decide on the DCO. GCC accepts a patch under the Developer Certificate
      of Origin with a `Signed-off-by:` line, or under an FSF copyright
      assignment. This is the user's decision.
- [ ] Ask for a backport to the open release branches, because the defect is
      a regression since GCC 10.

### Subject

```
[PATCH] c++: constexpr memchr, strchr, strrchr, strstr from an offset pointer [PRnnnnn]
```

### Commit message

```
c++: constexpr memchr, strchr, strrchr, strstr from an offset pointer [PRnnnnn]

cxx_eval_builtin_function_call can replace the first argument of memchr,
strchr, strrchr and strstr with the address of the STRING_CST that is its
value.  The result of the fold then points into that STRING_CST, so the
function puts the original argument back as the base of the result.  It
did so also when it had not replaced the argument.  In that case the
folder gets the evaluated argument, for example "0,1,2,3" + 2, and
fold_build_pointer_plus_hwi merges the offset of the argument into the
offset of the result: ("0,1,2,3" + 2) + 1 becomes "0,1,2,3" + 3.  With the
original argument put back as the base, the result is the argument + 3,
and the offset 2 is counted twice.  The defect is present since
r10-5927 (PR c++/80265).

In C++26 the cast from void * is a constant expression (P2738), so
char_traits<char>::find, std::find on bytes and std::ranges::find reach
__builtin_memchr when the front end folds a call with constant arguments
outside a manifest constant evaluation.  The defect then makes
std::string_view::find give a wrong value at run time at -O1 and above.

Put the original argument back only when it was replaced.

	PR c++/nnnnn

gcc/cp/ChangeLog:

	* constexpr.cc (cxx_eval_builtin_function_call): Rebase the result
	of memchr, strchr, strrchr and strstr on the original first argument
	only when that argument was replaced with the address of a
	STRING_CST.

gcc/testsuite/ChangeLog:

	* g++.dg/ext/constexpr-builtin2.C: New test.
	* g++.dg/ext/constexpr-builtin3.C: New test.
```

### The change to constexpr.cc

The full patch, with the two tests, is the fork commit 9fa33cc9248 on branch
gcc-16.2-patched in the project's GCC fork. `git format-patch -1 9fa33cc9248`
gives the mail body.

```diff
--- a/gcc/cp/constexpr.cc
+++ b/gcc/cp/constexpr.cc
@@ -2608,6 +2608,9 @@ cxx_eval_builtin_function_call (const constexpr_ctx *ctx, tree t, tree fun,
 
   int strops = 0;
   int strret = 0;
+  /* The original argument STRRET-1, if it is replaced below with the
+     address of a STRING_CST.  */
+  tree strret_arg = NULL_TREE;
   if (fndecl_built_in_p (fun, BUILT_IN_NORMAL))
     switch (DECL_FUNCTION_CODE (fun))
       {
@@ -2700,7 +2703,11 @@ cxx_eval_builtin_function_call (const constexpr_ctx *ctx, tree t, tree fun,
 	  if (TREE_CODE (arg) == CONSTRUCTOR)
 	    arg = braced_lists_to_strings (TREE_TYPE (arg), arg);
 	  if (TREE_CODE (arg) == STRING_CST)
-	    arg = build_address (arg);
+	    {
+	      arg = build_address (arg);
+	      if (i == strret - 1)
+		strret_arg = oarg;
+	    }
 	  else
 	    arg = oarg;
 	}
@@ -2780,11 +2787,15 @@ cxx_eval_builtin_function_call (const constexpr_ctx *ctx, tree t, tree fun,
       return t;
     }
 
-  if (strret)
+  if (strret_arg)
     {
       /* memchr returns a pointer into the first argument, but we replaced the
-	 argument above with a STRING_CST; put it back it now.  */
-      tree op = CALL_EXPR_ARG (t, strret-1);
+	 argument above with a STRING_CST; put it back now.  Do this only
+	 when the argument was replaced.  Otherwise NEW_CALL is relative to
+	 the evaluated argument, and fold may have merged an offset in that
+	 argument into the offset of NEW_CALL, so rebasing NEW_CALL on the
+	 original argument would count that offset twice.  */
+      tree op = strret_arg;
       STRIP_NOPS (new_call);
       if (TREE_CODE (new_call) == POINTER_PLUS_EXPR)
 	TREE_OPERAND (new_call, 0) = op;
```

The two new tests:

- `g++.dg/ext/constexpr-builtin2.C` is a compile test for C++11 and later. It
  covers memchr, strchr, strrchr, strstr, index, rindex and strpbrk on array
  and literal offsets. It also covers the forms that were already correct:
  the whole array, and `&x[k]`.
- `g++.dg/ext/constexpr-builtin3.C` is a run test for C++26 at `-O2`. It has
  memchr static_asserts, and run-time checks through `string_view::find`,
  `std::find`, and a function that calls the builtin only outside constant
  evaluation.

Their basenames match two existing files in `g++.dg/cpp0x`, so a `dg.exp=`
pattern selects both. Rename them if a reviewer asks.

### Testing done (non-bootstrap, releases/gcc-16 at 61bc257e5e0 plus the fix)

| Run | Result |
|---|---|
| Testcase 2 | prints `1 1` at -O0 and at -O2 (unfixed: `3 1`) |
| Scope probe | 0 wrong results in C++20, C++23 and C++26 (unfixed: 8, 8 and 12) |
| Testcase 3 at -O1 and -O2 | `literal=10 runtime=10` |
| The new tests on the unfixed compiler | constexpr-builtin2.C fails 21 static assertions per standard. constexpr-builtin3.C fails 4, and its run half aborts at -O2 |
| `check-g++`, constexpr and builtin tests near the new ones | 175 PASS, 0 FAIL, 1 UNSUPPORTED |
| `check-g++` over `constexpr*.C`, `is-constant-evaluated*.C` and the builtin-folding tests | 7,864 PASS, 73 XFAIL, 12 UNSUPPORTED, 0 FAIL |
| libstdc++ string_view find and rfind, char_traits tests, at gnu++26 and gnu++20, -O2 | 122 PASS, 0 FAIL |

Not done: a bootstrap, the full `make check`, and an `--enable-checking=yes`
build.

### Not changed by the patch

A constexpr function that calls `__builtin_strchr (s + 2, ',')` on a local
array `char s[] = "..."` is "not a constant expression" before and after the
patch. That is an older limitation of the evaluator, and it is separate from
this defect.
