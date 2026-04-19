# Submitting this bug to GCC Bugzilla

Step-by-step. Do this when ready to report.

## 1. Create a Bugzilla account (once)

https://gcc.gnu.org/bugzilla/createaccount.cgi — any valid email, no
approval queue, takes 30 seconds.

## 2. Search for duplicates first

- https://gcc.gnu.org/bugzilla/buglist.cgi?quicksearch=<terms>
- Also the gcc-bugs archive: https://gcc.gnu.org/pipermail/gcc-bugs/

If you find an existing PR with the same symptom, comment there
instead of opening a new one.

Search terms for this bug:
- `ipa-modref wrong-code`
- `contracts do_not_optimize`
- `asm +m,r inline miscompile`

## 3. File new bug

https://gcc.gnu.org/bugzilla/enter_bug.cgi?product=gcc

| Field         | What to put |
| ---           | --- |
| Component     | `tree-optimization` (for middle-end passes). Use `c++` for frontend, `middle-end` if unsure, `rtl-optimization` for backend. Reviewers reassign if wrong. |
| Version       | `16.0` (GCC labels its development trunk as the next release's `.0`). |
| Severity      | Leave default (`normal`). The `wrong-code` keyword is what bumps priority. |
| Keywords      | `wrong-code` (this one is important). Also add `regression` if it used to work on GCC 15. |
| Summary       | Imperative, pass-tagged, flag-tagged. Template: `[16 Regression] wrong-code: ipa-modref + early inlining miscompile contract-checked inlined method accessed via do_not_optimize(T&)-clobbered pointer at -O3` |
| Description   | Paste `REPORT.md` contents. Keep the reproducer inline in the description — reviewers scan descriptions before clicking attachments. |

## 4. Generate preprocessed sources

Bugzilla expects `.ii` (preprocessed C++) attachments. Run from this
directory:

```bash
~/.local/gcc16/usr/bin/g++ -std=c++26 -fcontracts -O3 -march=native -E bug.cpp > bug.ii
~/.local/gcc16/usr/bin/g++ -std=c++26 -fcontracts -O3 -march=native -E handler.cpp > handler.ii
```

These are committed alongside `bug.cpp` / `handler.cpp` so they're
always in sync — regenerate only if you change the sources or the
flag set.

Don't try to minimize the `.ii` further; GCC wants the exact
preprocessed form that reproduces the miscompile.

## 5. Attach files to the PR

Use the "Add an attachment" button AFTER creating the bug (Bugzilla
only accepts attachments post-creation). Upload in this order:

1. `bug.cpp`       (source)
2. `handler.cpp`   (contract-violation handler — required by libstdc++ when `-fcontracts` on)
3. `bug.ii`        (preprocessed)
4. `handler.ii`    (preprocessed)
5. `REPORT.md`     (full writeup, already in description too)

Also paste the output of `g++ --version` and `g++ -v 2>&1 | tail -10`
in a comment — helps pin down exact build config.

## 6. CC the pass maintainers

Pass owners triage faster than the general queue. For this bug:

- `hubicka@ucw.cz` — Jan Hubička (ipa-modref, ipa-*, -O3 cloning/inlining)
- `rguenther@suse.de` — Richard Biener (tree-ssa, general middle-end)
- `jwakely@redhat.com` — Jonathan Wakely (libstdc++, contracts frontend integration)
- `ppalka@redhat.com` — Patrick Palka (C++ frontend, concepts, contracts)

Add them in the CC field during bug creation.

## 7. After filing

1. Bugzilla assigns a PR number like `PR tree-optimization/12345`. Note it.
2. Optional: post the link in `#gcc` on libera.chat for visibility.
3. If no response in a week, post a pointer to the PR on gcc@gcc.gnu.org
   mailing list (subscribe first at gcc-request@gcc.gnu.org).
4. Watch the PR. Maintainers may ask for further reduction or for you
   to test a fix patch.

## 8. Etiquette

- One self-contained reproducer per PR. We have two flavors (see
  `REPORT.md`). Ship Flavor A (the minimal one). Mention Flavor B as
  "we also see a related but harder-to-reduce crash in our production
  code that we'll add as a comment once minimized."
- Don't set severity above `normal` yourself — reviewers do that
  during triage.
- Respond to clarification requests within a week; stale PRs get
  auto-closed after ~6 months of inactivity.
- When a patch is proposed, test it with the real Crucible bench
  (restore `bench::do_not_optimize(pool_ptr)` temporarily) and report
  pass/fail on the PR.

## After GCC fixes it

1. Verify the fix with the real bench_pool_allocator (restore the
   clobber, re-run at -O3, should not crash).
2. Record which GCC 16.x release carries the fix.
3. Once our CI toolchain moves past that release, remove the
   "No bench::do_not_optimize(pool_ptr)" comments in bench/ and
   delete this `bugs/gcc-modref-miscompile/` directory.
4. Also remove `project_gcc16_miscompile.md` from the Claude memory
   project directory.
