# ════════════════════════════════════════════════════════════════════
# examples/fn/ — five worked bindings of fixy::fn
#
# Each example binds a value of one production shape with fixy::fn
# (include/fixy/Fn.h).  It names the atoms of its pack one time, in an
# alias template, with a reason for each atom, and mints the binding
# through fixy::mint_fn_for:
#
#   1. example_custom_kernel       a user compute kernel
#   2. example_custom_optimizer    a user optimizer step
#   3. example_cntp_frame          a wire frame from an untrusted source,
#                                  and its retag after validation
#   4. example_forge_phase         an internal IR transform with no
#                                  in-place mutation, over a bit-exact
#                                  IR band
#   5. example_mimic_backend_hook  a vendor kernel emitter over a Vendor
#                                  band, with the largest effect row:
#                                  Bg, Alloc and IO
#
# Each example is two things:
#
#   (a) A build target.  It compiles under the full warning matrix of
#       the project, with contracts and reflection, so a change to the
#       interface of fixy/Fn.h shows here at compile time.
#   (b) A ctest entry.  main() runs the binding and returns nonzero when
#       a result is wrong, so a change to the run-time surface of
#       fixy/Fn.h shows in the ctest output.
# ════════════════════════════════════════════════════════════════════

# ── The target of one example ───────────────────────────────────────
#
# The same shape as a test target, without the sanitizer flags: by the
# convention of the project only tests take them, and benches and
# examples do not.  An example links the crucible library as a test
# does.
function(crucible_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name} COMMAND ${name})
  # The examples share the "examples" label, so `ctest -L examples`
  # runs only the worked examples.
  set_tests_properties(${name} PROPERTIES LABELS "examples")
endfunction()

crucible_example(example_custom_kernel)
crucible_example(example_custom_optimizer)
crucible_example(example_cntp_frame)
crucible_example(example_forge_phase)
crucible_example(example_mimic_backend_hook)
