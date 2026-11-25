"""gate — build and test the commit of a landing in a warm tree, in each configuration that the change selects.

Configurations
    CONFIGURATIONS gives each configuration of the gate: each configure
    preset with the tier gate (assay.loop.tiers), the parse presets first.  A
    configuration has the name of its preset and its build directory in the
    warm tree.  The block of the preset gives the name of the configuration
    of its build (assay.core.configuration).  A parse configuration is a
    preset that sets CRUCIBLE_SYNTAX_ONLY.  These configurations are the one
    list of the configurations that a counted ledger holds (the reduce).
    The gate reads the presets of the tree of the package, and it refuses
    presets with a problem.

Counted rules
    A counted rule is a rule of the engine plugin or elf with the policy
    count or admit and a ledger with a row (counted_rules()).  Each one reads
    the objects of the target all of a build, or the reports of a parse
    build.  Its ledger holds one set of rows, and these rows judge each build
    of the rule.  The run of a configuration of objects checks each rule.
    The run of a parse configuration checks only the rules of COUNTED_RULES
    of assay.guard.engines.buildsys.parse_builds (is_checked()).

Selection
    The gate holds one invariant: a landing cannot make a test red in a
    configuration of the gate when the test can see the change.  Each
    configuration of objects then runs for each change, in one of three
    modes (Mode):
      * The full suite, `loop test BUILD --all`.  select() gives these
        configurations:
          - default for each change
          - release-v3 and tsan for a change of a C++ file or of a build
            file.  The configuration release compiles for the host level
            (x86-64-v4 on luks-a), and it runs only with --v4, with a
            lower, and for a change of EVERY_PREFIXES (ON_REQUEST).
            --all-configurations does not add it.  The code of v3 runs on
            v4, and release-v3 holds the Release flags.  A change of the
            plugin or of the rule table can change the count of a finding
            in a code arm of v4, and its round lowers the counted ledgers,
            so it builds release too.  ThreadSanitizer gives
            incorrect errors of
            -Wmaybe-uninitialized that the other builds do not give, also
            for a change of a test.  release-v3 compiles for x86-64-v3, so
            each test runs the AVX2 arms at the width of 256 bits.  A
            header of the Simd family and a body in the inline namespace
            CRUCIBLE_SIMD_TARGET are C++ files, so each change of them
            selects release-v3
          - tsan and ubsan-strict for a change of include/, src/ or cmake/,
            or of a build file at the top of the tree
          - bench-fuzz for a change of bench/ or test/fuzz/, and for a change
            of an input of the test fuzz_afl_probe (AFL_PROBE_INPUTS), which
            only that build holds.  bench-fuzz runs only the tests of
            OWN_TEST_LABELS, the benches and the fuzz harnesses, and each
            check that reads its build, such as guard_plugin
            (checks_the_build() of assay.loop.affected).  Such a check
            reads the units of the benches, which no other build compiles.
            The other tests of bench-fuzz run with the flags of default,
            which runs them
          - each configuration other than those of ON_REQUEST for a change
            of the quarantine plugin or of the rule table, and for
            --all-configurations.  Also for a
            candidate whose composition summed two changes of one count of a
            ledger (assay.loop.land, the merge), so the reduce writes the
            measured count.
      * The tests of the changed inputs, `loop test BUILD`, in each other
        configuration of objects, when the change can change each build.
        Such a change holds a C++ file or a build file.  It can also change
        an input of the test launcher or of the fixture driver
        (meter_inputs() of assay.loop.affected).  The build then compiles
        again what the change touches.  `loop test` runs each test with a
        changed input, each script test with no usable read set, each
        fixture, and each test that reads the build.
      * The touched tests, `loop test BUILD --only FILE`, in each other
        configuration of objects, for each other change.  A touched test
        reads the build, and the change touches one of its inputs.
        build_tests() of assay.loop.affected gives the tests that read the
        build from the build of default.  The run of default first builds
        the target build.ninja of its build, which configures the build
        again with the change (the threads).  Inputs.is_touched() of
        assay.loop.inputs judges each one (the touch).  A test that reads no
        build gives the same result in each configuration, and default runs
        it.  When the change touches no such test, these configurations do
        not run.
    A build file is a file CMakeLists.txt, a file *.cmake or
    CMakePresets.json.

    A test of the master guard that reads no build gives the same result in
    each configuration (the tests that read the build of assay.loop.affected).
    default runs the full suite for each change.  The gate then gives each
    other configuration of the first two modes the option --build-only of
    `loop test`, which skips such a test.  The run of the base can have no
    default, and its configurations do not get the option.

    A parse configuration runs when `loop syntax --changed` of the warm tree
    names it for a change of an arm.  It also runs when a path of the change
    touches the inputs of a rule of COUNTED_RULES of
    assay.guard.engines.buildsys.parse_builds.  That path is no C or C++ file
    and no build file.  A change of such a file changes the result of a parse
    build only through an arm.  Each parse configuration also runs for a
    change of the plugin or of the rule table, and for --all-configurations.

    The configurations of the last two modes run at the same time as the
    configurations of the full suite (the threads).  The model holds no
    inputs of a script test that reads a build, such as the parts of the
    test of the fixture store.  Such a test runs only in the first two
    modes.

    An executable test reads the build too.  Its inputs in the source tree
    are each file that its binary names and each key part of its read set
    (assay.loop.readset).  build_tests() does not give such a test.  Such a
    test then also runs only in the first two modes.  On 2026-10-14, one executable
    test of the warm tree had such an input: bench/baselines/record_leaf.json
    in the bench and fuzz build.  A change of bench/ selects the full suite
    of that build.

    Each configuration compiles the files of include/ and src/.  The bench
    targets also compile the arms of CRUCIBLE_BENCH in them.  An arm in
    include/crucible/BackgroundThread.h is an example.  An arm that only one
    configuration compiles then gets its verdict in the landing of the
    change, and a count above its row fails there.  A landing that builds
    each configuration writes each count below its row (the reduce).

    A change that no unit of a configuration reads makes its build do
    nothing.  A parse build then keeps the report of each unit (the kept
    reports of assay.loop.syntax).

    The vessel runs too, for a change that can change it (the vessel).

Vessel
    The preset VESSEL builds the vessel against the patched PyTorch.  Its
    tier is nightly, and `loop nightly` builds and tests each target of its
    build.  No configuration of the gate compiles crucible_fallback.cpp and
    the shards of register.cpp of vessel/torch, because they need PyTorch.
    Thus the gate also runs the vessel (Mode.VESSEL), in a thread of its own
    with a lease of its own (the threads).  The vessel is not a
    configuration of CONFIGURATIONS.

    The vessel runs for each change that can change each build
    (changes_each_build()), and for each change of a path of VESSEL_PREFIX
    (reaches_vessel()).  Ninja then compiles only the vessel units that the
    change reaches.  The selection reads no include graph of a build of a
    lane, because a lane can skip a commit that changed that graph.  A
    change that reaches no vessel unit costs a build with no compile and the
    run of the vessel tests.

    The run of the vessel has these steps:
      1. link_torch() gives the warm tree the link TORCH_VENV to the
         directory TORCH_VENV of the main work tree of the repository.
         TORCH_DIR of the preset names a directory in TORCH_VENV of the
         source tree, and git does not hold it.  When the main work tree has
         no such directory, the vessel gets a problem that names it.  The
         gate then refuses the commit, because it cannot build the vessel.
         The preset sets CRUCIBLE_REQUIRE_VESSEL, so a TORCH_DIR with no
         PyTorch stops the configure with the reason
      2. The configure of the build directory, when it has no graph of Ninja
      3. `cmake --build BUILD --target` of each target of VESSEL_TARGETS,
         with no -j (the jobserver).  Then the wait for the write of the
         built files, only when the vessel reads a written file
         (is_read_by_vessel()).  The vessel reads no other file of
         BUILT_FILES, so its tests on the commit give the result of its tests
         on the commit of the fold
      4. `guard check --rule RULE --build-dir BUILD --target TARGET...` of
         each plugin rule with no ledger and of each rule of
         VESSEL_COUNTED_RULES (vessel_rules()), over the objects of
         VESSEL_TARGETS.  A family kind of the plugin, such as the kind
         missing-mark of lifetime-twin, is no compile error in a mode, so no
         build of the vessel stops on it, and only this check reads it in
         crucible_fallback.cpp and in the shards of register.cpp.  A finding
         of a rule with no ledger is a problem.  A count of a rule of
         VESSEL_COUNTED_RULES above its row is a problem, as in each other
         build.  The exit 77 is a problem too: the build made the targets,
         so a rule that reads no object of them did not check the vessel.
         The plant plant-vessel of utils/plants/lifetime-twin/ holds a
         finding at a path of vessel/torch
      5. `ctest --label-regex` of VESSEL_LABEL, with the jobs of the lease.
         Each red test runs one more time apart from the suite, as in step 3
         of the run of a configuration.  A disabled test, and a run in which
         no test runs, are problems too.  The fixtures with the label
         vessel_neg need no PyTorch, and each configuration of the gate runs
         them.
    The round after the reduce runs these steps again.  The vessel runs no
    counted rule other than those of VESSEL_COUNTED_RULES, no rule
    reconfigure-noop and no `guard warnings`.  When the round built the
    vessel with no problem, the reduce of each rule of VESSEL_COUNTED_RULES
    reads the objects of VESSEL_TARGETS in its build too (reduce_words()).
    A reduce with no vessel build keeps each row of a path with a unit that
    only the vessel compiles, so no round lowers a count that it did not
    read (the unread units of assay.guard.engines.plugin.runner).  The run
    of the base runs the red tests of the vessel, or each vessel test after
    a problem of a different kind.
    A lease reserves the memory of one job of its configuration for each
    slot: the largest kind of KIND_GB_PER_JOB of the step that holds it
    (gb_per_job_of(), the memory and the budget of assay.loop.heavy).  A
    lease of `loop test` builds and tests, so it reserves the largest of
    the three kinds.  The vessel builds with the memory of a shard compile,
    and its lease then reserves the memory of a vessel test for the ctest
    step.  The lease has the weight VESSEL_WEIGHT among the leases of the
    gate (the weight of assay.loop.heavy).  The weight stays 1, because
    the shards of four runs at the same time hold much of the memory of the
    host.  The lease is capped: it never grows past the slice of the gate
    times VESSEL_WEIGHT (the pool of assay.loop.heavy).

Careful selection
    The selection reads files of the commit with the code of the gate, which
    is the code of main (the driver of assay.loop.land).  A commit can change
    the format of a file and its reader together.  An example is a new kind
    of row of the rule table and the code that reads it.  The code of the
    gate then cannot read that file.  Each read of the selection that gives
    an error then gives its widest result (careful()):
      * each parse configuration
      * the mode of the changed inputs in each other configuration of objects
      * no counted rule, so the reduce lowers no row.
    The gate writes a line with the error.  A wider selection runs each
    configuration and each test that a narrower one runs, so the invariant
    of the selection holds.  `loop syntax --changed` is the master of the
    warm tree, and an error of it also gives each parse configuration.  The
    commit then gets its verdict from its own tests.

Warm tree
    One repository in the directory TREE of the work directory holds the
    commit of each landing.  It borrows the objects of the repository of the
    landing through the file objects/info/alternates.  move_tree() gives it
    the commit with `git read-tree --reset -u` and `git update-ref
    --no-deref HEAD`.  Git writes only the files that the commit changes.
    Each other file keeps its time of change, and Ninja builds again only
    what the commit changes.  When read-tree fails and git cannot read the
    index, such as after a stop of the host, the gate makes the index again
    from HEAD (repair_index()), and the files keep their times of change.
    A HEAD with no commit gets the last commit of its reflog (repair_head()).
    After the move, the gate removes each output of a build with no bytes,
    and a reply of the CMake file API that does not read, with its graph
    (repair_builds()).  It also removes each graph build.ninja that the
    ninja of the build cannot load, such as a graph that names a rule that
    CMakeFiles/rules.ninja does not hold (unloadable_graphs()).  Ninja builds
    such an output again, and the run configures such a build again.  The build directory of each configuration is
    in the warm tree, at the binaryDir of its preset.  The build directories
    stay from one landing to the next.  A guard that reads the git history
    reads the history of the landing.  The gate refuses a warm tree in the
    work tree of the repository.

Run of a configuration
    The HEAD of the warm tree is the commit, with no branch.  The change of the
    tree is then the commit on its first parent (assay.core.git, the
    change).  The suite does not get the variable CRUCIBLE_CHANGE_BASE,
    because a self-test plants repositories of its own, and the variable
    names no commit of them.  `loop syntax --changed` gets the variable, with
    the commit that the composition started from.  Each configuration of a
    check runs in a thread of its own, with a lease of the slot pool of the
    host (the jobserver, the threads):
      1. A build directory with no graph of Ninja gets its configure:
         `cmake --preset PRESET -B BUILD`.  A build directory whose cache
         names a compiler that its toolchain file does not resolve gets
         `cmake --preset PRESET -B BUILD --fresh` (the compiler heal).
      2. A parse configuration: `loop syntax BUILD`, then `guard check
         --build-dir BUILD` of each rule of COUNTED_RULES of
         assay.guard.engines.buildsys.parse_builds, at the same time.
      3. A configuration of objects: `loop test BUILD` with the words of its
         mode, which builds the target all and runs the tests of the mode
         (assay.loop.affected, the selection).  A retry after a move of HEAD
         and each subsequent run of a batch run `loop test BUILD` in a
         configuration of the full suite.  That command runs only the tests
         with an input that changed, and each test of a rule that reads a
         build.  The file of --only is in the directory of the logs.
         Then each test that is red runs one more time apart from the
         suite.  The red tests run at the same time, in one run of ctest
         with the jobs of the lease, and each one keeps its own verdict.  A
         test that is red in that run too ("red alone too") rejects the
         commit.  A test that is green in that run ("green alone") is a
         flaky test, and no retry hides it: the
         gate writes its name, and the verdict gives it (Verdict.flaky).
         The landing writes it in the verdict of each request of the run
         (assay.loop.land, the flaky tests).
         An exit other than 0 with no red test rejects the commit too.  Then
         `guard warnings BUILD`.  The suite holds the ledger checks of the build
         kind, such as the tests guard_plugin and guard_artifact_symbols.
         A second run of these checks gives no new verdict.  A test that gives
         a skip does not reject the commit, and the gate writes its name.
         The run of the red tests writes their output to one log.
      4. A configuration of objects, after step 3 with no problem: `guard
         check --rule reconfigure-noop --build-dir BUILD` (RECONFIGURE_RULE).
         The rule configures the build of the warm tree again and builds it
         two times.  A build that compiles again or a dependency of the log
         of Ninja that does not exist rejects the commit.  No test of the
         suite can configure the build that runs it, so the gate runs the
         rule.  The step runs in each mode of the selection and in the run of
         the base.  The run again after the reduce does not run it.
    Each step runs the masters of the warm tree.  Then the tests of the
    commit judge the commit.  The step writes its two outputs to a log in
    the directory of the logs of the gate (the logs).  The first
    configuration with a problem stops the others (the stop).

Compiler heal
    CMake keeps the compiler of the first configure of a build directory,
    and the warm tree keeps its build directories from one landing to the
    next.  So a new default prefix of a toolchain file, such as the prefix
    of a new patch of GCC, never reaches a build directory of the warm tree
    by itself.  Before the build of a configuration with a graph of Ninja,
    the gate runs `loop heal BUILD PRESET` of the warm tree (heal_line()).
    That command compares the compilers of the cache with the compilers
    that the toolchain file of the commit resolves (the stale compiler of
    assay.loop.heal).  It runs from the warm tree, because the rules of the
    commit decide, and this copy of the gate can be older than the commit.
    When the command gives a line, the gate writes the line, which names the
    paths of the cache and of the toolchain file.  Then it configures the
    build directory again with `cmake --preset PRESET -B BUILD --fresh`
    (configure_plan()).  The commands of the compiles then name the new
    compiler, or `loop heal` removed the log of Ninja for a compiler that
    keeps its path, so Ninja builds each object again.  A cache with no
    record of its compiler heals one time and builds nothing again.  A build
    directory with no graph of Ninja takes step 1 of the run of a
    configuration as it is.
    `loop test` of each configuration of objects also parses each
    configured parse build of the warm tree (the parse builds of
    assay.loop.affected), also when the plan runs no parse configuration.
    Thus, before the runs start, the gate heals each configured parse build
    that no run of the plan configures, in the same way
    (Step.heal_parse_builds()).  On 2026-11-19, after the switch to a new
    patch of GCC, the parse builds of three lanes kept the old compiler, and
    each landing with no parse configuration in those lanes was refused as
    a red main.
    Outside the gate, cmake/StaleCompiler.cmake refuses such a cache in each
    configure, with the command that heals it.

Use of the host
    The verdict of each check gives the busy share of all CPUs of the host
    over the run, from two reads of STAT_FILE (host_busy_pct), the CPU
    time of each process that the run started and waited for (cpu_seconds),
    and the count of the CPUs of the host (host_cpus).
    The record of each run then holds them (assay.loop.land, the record).
    The busy share counts each process of the host, so a run with idle CPUs
    shows a slot or a step that waits.

Jobserver
    Each configuration of a check holds a lease of the slot pool of the host
    while it runs (the pool of assay.loop.heavy).  The writes of the built
    files and of the reduce hold a lease too.  The cap of a lease is the
    slice of the gate: --jobs with SLICE of assay.loop.heavy as the most, or
    less when the available memory is small (the memory of
    assay.loop.heavy).  Each lease has a jobserver of its own
    (assay.core.jobserver), and each command of the configuration gets the
    variable MAKEFLAGS of that jobserver.  The pump of the lease adds slots
    while the configuration uses each one, and it gives idle slots back to
    the pool.  It grows past the slice only while no different lease waits.
    A configuration in its test phase then cannot take the jobs of a
    different configuration that waits, and the slots of the host go to each
    heavy job of the host.  The leases of the gate go before the other
    leases of the host (the pool of assay.loop.heavy).  The wait for the
    first slot watches the stop event.
    The gate holds one slot of the jobserver for each command that it runs:
    its implicit slot, or one token.  The command then runs its first job
    on that slot.  ninja, ctest and `loop syntax` take one token for each
    other job.  `loop test` gives its build no -j with the jobserver,
    because ninja obeys the jobserver only with no -j (assay.loop.affected).
    ctest and the pool of `loop syntax` get -j with the most slots of the
    lease, and the tokens of the jobserver then decide.
    The verdict gives the wait of each configuration for its first slot, and
    the sum of its slots over its time (Verdict.wait_seconds and
    Verdict.slot_seconds).  Its time in step_seconds starts after that wait.

Threads
    Each configuration of the plan runs in a thread of its own, at the same
    time as the others.  The other configurations of objects (the last two
    modes of the selection) run in threads of their own too.  They start
    when their plan is known.  A change that can change each build gives
    their plan immediately.  Else, their plan reads the touched tests of the
    build of ALWAYS.  The run of ALWAYS then first builds the target
    build.ninja of its build, which configures the build again with the
    change.  Then the other configurations start.  The checks of a parse
    configuration run in threads of their own too.  The writes of the reduce
    also run in threads of their own.

    The verdict gives the configurations, their times and their problems in
    the sequence of the plan and then of the other configurations.  It does
    not give them in the sequence of their ends.

Stop
    In the run of a commit, the first configuration with a problem stops
    the others.  A run of the base has no such stop, and a crossed row of a
    budget in the first round of a candidate with a budget reason does not
    stop the others (the raise of a budget).  Each command
    of the step looks at the stop event each STOP_POLL_S seconds.  At the
    stop, the process group of the command gets the stop of
    Running.stop() of assay.core.proc.  Its configuration then ends with no
    problem of its own.  A command that did not start does not start.  The
    verdict gives each problem of each configuration that ended, and it
    does not give a configuration that the stop ended.  An error of a step
    of the gate stops the others too, and the check raises it after each
    thread ended.  A thread that the error stopped does not hide the error.
    A check that the stop ended with no problem of a configuration is
    an error of the gate (require_problem()).  The landing then refuses the
    commit with that error, and it does not accept it.

    Each command of the gate has the stop word interrupt of
    assay.core.proc.  Its group gets SIGINT, and SIGKILL after STOP_GRACE_S
    seconds.  A master of the package then stops in its own way.  Its wait
    sends SIGINT to the group of each child (an interrupt of
    assay.core.proc), and ninja and ctest stop their jobs.  SIGTERM ends a
    master with no such step.  Its children run in process groups of their
    own, and they then go on in the warm tree.

Hang guard
    ctest gives each test of the gate the wall guard TEST_TIMEOUT_S with
    --timeout: in `loop test` (after --), in the run of the red tests alone,
    and in the run of the vessel tests.  With no such option, ctest gives a
    test a limit of 10,000,000 s, and the 300 s of the test presets apply
    only to `ctest --preset`.  The guard judges no test.  It only bounds a
    hang, so a hang holds a lane for at most that time.  A test that gets to
    it is red, and it is a defect to correct, because a test judges its
    work and not the wall time.  A test with the property TIMEOUT, such as
    a test of the vessel, keeps its own limit.  On 2026-11-19 the slowest
    test of a loaded run took about 75 s.

Kill for memory
    The kernel can end a process when the host or its control group runs
    out of memory, and the process then exits 137.  Such a kill tells that
    the host was short of memory, not that the change has a defect.  Each
    configuration reads the count of such kills before its run and after it
    (Gate.oom_kills, heavy.oom_kills()).  A run with a problem and a rise of
    the count runs one time again, with the same lease, before its problem
    stops the others, and a line tells of the kill.  The second run gives
    the outcome.  A kill in a different process at that time also gives the
    second run, and a correct red run then stays red.  Each such kill adds
    one JSON line to KILL_LOG of the work directory, with the configuration,
    the memory of a job of its lease and the first problem.  A count of the
    lines of one kind tells if the reservation of that kind is too small
    (KIND_GB_PER_JOB).

Reduce
    A ledger fails only on a growth (the policies of assay.core.ledger).  A
    count below its row, and a row of the policy admit that admits no
    finding, then make no check fail.  Only the gate lowers the rows of a
    counted ledger, because one build cannot see the findings of a different
    build (the write of assay.core.ledger).  The legs of the gate write no
    ledger.

    The gate lowers the rows only when the first round built each
    configuration of CONFIGURATIONS with no problem (is_each_built()).  The
    configurations of ON_REQUEST are in that set, so a round of
    --all-configurations with no --v4 lowers no row either.  A
    landing that builds only some configurations then lowers no row, and
    its checks still judge each of its builds.  After each leg of the first
    round ended, the gate runs one `guard write --rule RULE --build-dir
    BUILD...` of each counted rule.  The write gets the build of each
    configuration that checks the rule (is_checked()).  It takes the largest
    count of each key over these builds, and it lowers each row above that
    count (the reduce of assay.core.ledger).  Each mode of the selection
    builds the target all first, and a write then reads the full build.  The
    writes of the rules run at the same time, and each ledger has one writer.

    A write refuses each count above its row and each new row.  The write
    reads no git history.  The gate does not write an increase, because the
    check of each leg refuses it first.  The change writes an increase with
    `guard write --rule RULE --build-dir BUILD` before the landing, in the
    mode --raise, or --admit with a reason for a ledger of the policy admit.

    After the writes, the gate folds each ledger that a write changed into a
    new commit (replaced_commit() of assay.core.git).  The new commit has
    the parents and the message of the commit, and its tree holds these
    ledgers.  After a write of the built files, the commit is the commit of
    that write.  A landing of more than one commit gets the ledgers in its
    last commit.

    The warm tree moves to the new commit.  Each configuration of the run
    then runs again, at the same time, each with a lease.  A parse
    configuration runs `guard check --build-dir BUILD` of each rule of
    COUNTED_RULES again, on the reports that it keeps, with no write.  A
    ledger that the gate folded can make such a check red, and the gate then
    rejects the commit.  A configuration of the full suite runs `loop test
    BUILD`, which runs each test with an input that changed, as the run
    after a move of HEAD does.  The other configurations get the mode of the
    selection again, with the ledgers in the change.

    The verdict gives the new commit and the ledgers, and the landing moves
    the branch to that commit.  A change that lowers counts and lands with
    --all-configurations then lands with no manual write of a ledger.

Built files
    BUILT_FILES gives each generated file that the gate writes from the
    build of ALWAYS, with its target of `loop derive`.  The mint inventory
    is such a file.  It needs the mint records of a full build, and a full
    build of a work tree takes hours on a busy host.  Thus the change of a
    landing does not hold the file, and the gate writes it.

    The gate writes each file of BUILT_FILES for each commit whose change
    can change it (built_selection()).  The module of the target gives the
    function is_input() of the paths that can change the file.  The modules
    of its import closure can change the file too.  A read of the selection
    that gives an error selects each file (the careful selection).  The run
    of the base writes no file, because its commit is the commit that the
    change starts from.  When the two sides of the merge of
    assay.loop.land change such a file, the composition keeps its own copy,
    and the candidate names the file (Candidate.built).  The gate writes
    each such file too.  The changed paths of the candidate hold each file
    that the gate writes, as they hold a merged file.  The tests that read
    such a file are then touched tests, and each configuration runs them on
    the written file (the selection).

    The test of the rule derived-files reads the file in each configuration
    of objects.  Thus no step of a configuration other than its compile runs
    before the write ends.  The run of ALWAYS writes each file (Step.
    settle_built()):
      1. Its configure, and the target build.ninja when the others wait for
         it (the threads)
      2. `cmake --build BUILD` of the target all, with no -j (the jobserver)
      3. `loop derive TARGET --write --build-dir BUILD` of the warm tree
      4. The fold of each written file that changed into a new commit
         (built_commit()).  The new commit has the parents of the commit,
         and its message adds the sentence of BUILT_NOTE.  The warm tree
         moves to that commit.
    Each other configuration compiles at the same time: the configure and
    `cmake --build BUILD` of a configuration of objects, `loop syntax BUILD`
    of a parse configuration, and the build of the vessel targets.  Then it
    waits for the end of the write, and it runs its tests and its checks on
    the new commit.  The vessel waits only for a file that it reads (the
    vessel).  A compile reads no git state and no file of
    BUILT_FILES, so the move of the warm tree does not change it.  A build
    or a write that gives an error is a problem of ALWAYS, and the stop ends
    the other configurations.  The wait watches the stop event.

    The build of a configuration of objects writes its output to the log
    test.log, as `loop test` does.  A build error then gives the problem of
    a build error of `loop test` (BUILD_PROBLEM).  The run of the base writes
    no built file, and `loop test` builds there.  The two runs then give the
    same problem for one build error, and the landing compares them (the run
    of the base).

    A plan with no run of ALWAYS, such as the focus of a request that runs
    alone (the confirmation of assay.loop.land), writes each file before
    each configuration runs (Step.write_built()).  The round after the
    reduce writes no file.  The rule derived-files of the suite compares the
    written file with each build.  The verdict gives each written file that
    changed (Verdict.built).

Ratchet rows
    A row of a counted ledger that must rise, or a new row, is a decision of
    the author of the change.  Examples are a row of the rule escape-doors
    and a count of the rule owners.  The gate thus writes no such row.  But
    the write of such a row needs the records of a full build, and the gate
    has that build.  When the first round of a commit gives a problem, the
    gate gives the rows that the build of each red configuration of objects
    gives (Step.ratchet_rows()):
      * The test PLUGIN_TEST of the suite checks each rule of the engine
        plugin, and the test guard_ID checks the rule ID of the engine elf.
        Each counted rule whose test is red alone too gets one write on the
        build of that configuration.
      * The write is `guard write --rule ID --build-dir BUILD --admit
        --reason ROW_REASON` of the warm tree.  The gate keeps the bytes of
        the ledger, and it puts them back after the write.
      * Each line of the written ledger that the ledger of the commit does
        not hold is a row of the verdict (Verdict.rows).  No row admits a
        finding of a kind of REFUSED_KINDS of the module of the rule.  The
        verdict then tells the author to correct the code.
    The writes start after each configuration ended, with a stop event of
    their own, so the stop of the first round does not end them.  An error
    of the writes gives one line and no row, and it does not change the
    verdict (Gate.ratchet()).  The rows
    of one build are partial, and a different configuration can give a
    higher count (the write of assay.core.ledger).  The refusal of the
    landing gives the rows after the problems (assay.loop.land).

Raise of a budget
    A row of a rule of BUDGET_RULES keeps a size or a cost: object-size,
    step-cost and instruction-total.  The owner decision of
    2026-11-15 tells that a correct change that crosses such a row raises
    the row with its measured value and a reason.  A candidate with a budget
    reason (Candidate.budget_reason, `loop land --budget-reason TEXT`) lets
    the gate make that raise in its own run, so no second queue is
    necessary.  The rules of correctness are never in BUDGET_RULES, so no
    count of the quarantine, of must-consume or of a different such rule
    rises this way, and no red test or fixture passes this way.
      * An outcome of a budget has one problem, and each test that is red
        alone too is the test of a rule of BUDGET_RULES (BUDGET_TESTS).  Each
        failing item is a finding line of such a rule (is_budget_outcome()).
        In the first round of such a candidate, an outcome of a budget does
        not set the stop (the stop).  Thus each configuration ends, and the
        round shows each crossed row.
      * When each outcome with a problem is an outcome of a budget, the gate
        runs `guard write --rule RULE --build-dir BUILD --reason TEXT` of
        the warm tree for each such rule and configuration (Step.
        raise_budgets()).  The write of a measured rule writes each crossed
        row with the measured value (the write of assay.core.budget).
      * A candidate can give a row its own text (Candidate.budget_rows,
        `loop land --budget-reason ITEM=TEXT`).  The write then uses the
        plain text, or the first own text when the candidate gives no plain
        text.  After it, `guard write --rule RULE --row ITEM [--kind KIND]
        --reason TEXT` gives each crossed row that an item names its own
        text (Step.written_row_texts()).  An item names a row by its path,
        its path with no @build/ mark, or its entity (is_named_row()).  A
        crossed row with no own text and no plain text, and an item that
        names no crossed row, each give a line that names the row or the
        item, and the raise writes nothing.  The verdict gives each such
        line as a problem, so each requester of the run reads it.
      * A write that fails, or writes that change no row, give the ledgers
        their bytes back, and the problems of the round stay.  The verdict
        also gives a problem with the cause: the status and the log of a write
        that fails, or that the writes changed no row.
      * Else the round has no problem.  The reduce runs when the round built
        each configuration, and the fold puts the raised ledgers and the
        falls into the commit.  Each configuration then runs again, as after
        the reduce, and the round after the fold also runs `guard warnings`
        and the rule reconfigure-noop for each configuration whose first
        round was red (Step.unchecked).  A crossed row in that round is a
        problem, so the candidate gets no second raise.
    The verdict gives a line for each raised row, with its old and its new
    value (Verdict.raised), and the record of the run holds the lines.

Lower of a budget
    A row of a budget goes down only by an explicit write that lowers one
    named row to the largest measure of the configurations of the gate, with
    the reason of the cut (the lower of
    assay.guard.engines.buildsys.cost_budgets).  An agent has the build of
    one configuration, and the gate has a build of each.  Thus a candidate
    with a lower (Candidate.lowers, `loop land --lower RULE ITEM
    --lower-reason TEXT`) lets the gate make that write in its own run.  The
    rules are those of LOWER_RULES.
      * The candidate selects each configuration, as --all-configurations
        and --v4 together do (the selection).  A lowered row holds the
        largest measure of each build, release too.
      * When the first round built each configuration with no problem, the
        gate runs `guard write --rule RULE --lower --row ITEM... --reason
        TEXT --build-dir BUILD...` of the warm tree for each rule and each
        reason, with the build of each configuration of objects
        (Step.lower_budgets()).  The writes run after the reduce, and the
        fold puts the lowered ledgers into the commit with the falls.  Each
        configuration then runs again, as after the reduce.  A lowered row
        holds the largest measure of the builds of the round, so the round
        after the fold measures no item above it.
      * A write that fails gives its ledger its bytes back, and the verdict
        gives the line of the failure with its log.  A lower is no check of
        correctness, so the failure does not refuse the candidate.  A round
        that did not build each configuration lowers no row, and the verdict
        tells it.
    The verdict gives a line for each lowered or removed row, with its old
    and its new value (Verdict.lowered), and the record of the run holds the
    lines.  A preset of `loop nightly` or `loop release` that then measures
    more raises its row with its own reason, because the nightly is no gate.

Run of the base
    A candidate with a focus is the commit of a run of the base (the run of
    the base of assay.loop.land), or of the run alone of one request (the
    confirmation of assay.loop.land).  The rules of this section apply to
    the two.  The title of the candidate tells the kind of the run in the
    first line of the check (Candidate.title).  The focus gives each configuration with
    a problem in the run of the batch, and the tests that were red alone
    too in it (Focus).  The gate runs only that focus, each configuration
    in a thread of its own (focused_runs()).  It runs the red tests with
    `loop test BUILD --only FILE`, a parse configuration in its mode, and
    each other configuration in the mode of the changed inputs.  The vessel
    runs in its mode (the vessel).  The run of the base writes no ledger and
    folds no ledger.  Its problems then have the
    words of the problems of the batch.  A problem of a run of the base does
    not stop the other configurations of the focus (the stop).  The run of
    the base gives the result of each of them, because the landing compares
    each problem of the two runs.

Verdict
    The gate has no base side: it does not compare the commit with HEAD.  A
    test that is red alone on HEAD then rejects each landing, until a commit
    corrects it.  Main must stay green.  The verdict gives the focus of each
    configuration with a problem, for the run of the base.

Failing items
    The verdict also gives the failing items of its problems (Verdict.items),
    and the landing compares the items of two runs, not their problems (the
    run of the base of assay.loop.land).  Two runs can fail one test of a
    rule on different files.  Their problems then have the same words, but
    the problem of the batch is not the problem of the base.
      * A test that is red alone too gives one item for each finding line of
        its output in the report of the run alone: the configuration, the
        test, the path, the line and the rule (failing_items()).  A finding
        line has the format of assay.core.report, PATH:LINE: error: [CHECK]
        MESSAGE.  The item does not hold the message, because the message
        can hold a measure that changes from run to run.  The path loses the
        root of the warm tree.  The path is the PATH of the line.  A rule of
        a ledger can give a directory there, such as test/fixy/, and two
        findings on one line number of that directory then give one item.
      * A test with no finding line gives one item: the configuration and
        the test.
      * A test with no output in the report, or with an output that ctest
        cut, gives an item with the commit of the run.  The items of two
        runs of different commits then do not compare equal.  The run alone
        lets ctest keep ALONE_OUTPUT_BYTES of the output of each red test.
      * Each other problem is its own item.

Logs
    Each step writes its log into the directory of the logs of its gate.
    That is the directory LOGS of the work directory, or a directory of the
    run in LOGS.  hold_run() makes a new directory of a run, one above the
    highest number of a run.  The run holds an exclusive lock of flock(2) on
    the file LIVE of its directory from its start to its end
    (Gate.check_in_run()).  The problems of a refused landing then name logs
    that the next run does not change.

    hold_run() also removes each ended run, other than the last RUNS_KEPT
    ended runs.  An ended run is a directory whose file LIVE no process
    holds, or a directory with no file LIVE.  A burst of short runs then
    does not remove the logs of a long run that is live.
    A verdict names the logs of its run, and its process can read it hours
    later, such as after its own work as the holder of a different lane.
    Thus an ended run that a verdict in the queue cites stays past RUNS_KEPT
    until the process reads the verdict and leaves the queue (Gate.cited,
    cited_runs() of assay.loop.land).  Two bounds hold such runs: an age of
    CITED_RUN_AGE_S, and CITED_RUNS_BYTES for all of them together, with the
    oldest run the first to go (removed_runs()).  The lock file
    RUNS_LOCK of the work directory lets one landing at a time make a run
    and remove the ended runs.  A different landing then never sees a new
    run before its lock.

    The name of a log holds its configuration and its step.  Two threads
    then do not write one log.  The round after the reduce puts AGAIN_MARK
    after the name of the configuration (log_name()).  The logs of the first
    round then stay, such as the output of a test that was red in the suite
    and green alone.
    When a run has a red test, the gate copies the report of the suite into
    the log SUITE_REPORT.  The next landing in the lane replaces the report in
    the build, so only that copy keeps the output of a flaky test.

Lock of the tree
    One landing at a time uses a warm tree.  tree_lock() holds an exclusive
    lock of flock(2) on the lock file of the warm tree of a lane in the work
    directory.  It waits for the lock, or with no wait it tells that a
    different landing holds it.  session() holds the lock of the first lane
    and gives the gate.

Lanes
    Up to LANES landings run at the same time, each in a warm tree of its
    own: TREE, TREE-2, TREE-3 and so on, each with its own build
    directories.  LANES is the size of the slot pool of the host divided by
    SLICE, with one as the minimum and MOST_LANES as the maximum.  MOST_LANES
    is 1: one holder at a time holds the branch, so no batch stacks on a
    different batch and no run loses the compare-and-swap.  The pool caps the
    slots of all the lanes together, so a lane past the slots of the busy
    lanes only fills idle slots.
    free_lane() holds the lock of the first free lane.  A holder of the
    first lane also holds the lock of PRIMARY.  A lane after the first one is
    free only while that lock is held, so only while a holder of this copy
    of the module holds the first lane.  An earlier copy of the module takes
    only the first lane, and it reads the queue with no claim
    (assay.loop.land, the lanes).  It then never runs at the same time as a
    different lane.

`guard selftest tests.loop.test_gate` does a test of the module on a planted repository.
"""

import contextlib
import enum
import fcntl
import functools
import json
import math
import os
import re
import resource
import shutil
import sys
import threading
import time
from collections.abc import Callable, Collection, Iterator, Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path, PurePosixPath

from assay.core import git, jobserver, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
from assay.loop import affected, heavy, tiers
from assay.loop import inputs as guard_inputs
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

# The directory of the warm tree, the lock of the tree, the directory of the logs and the lock of its runs, in the work
# directory (the logs).
TREE = "tree"
LOCK = "tree.lock"
LOGS = "logs"
RUNS_LOCK = "logs.lock"
# The lock that a holder of the first lane holds, and the count of the lanes (the lanes).  The pool caps the slots of
# all the lanes together, so each lane past the slots of the busy lanes fills only idle slots.  The count comes from the
# CPUs and not from the slots of the pool, because each lane has a warm tree of its own.  MOST_LANES caps the count.  On
# 2026-11-23, the owner set one lane: one request at a time holds main, and its run takes each slot of the host.  With
# five lanes, 39 of 143 runs passed and then ran again, because a different lane moved main first.
PRIMARY = "primary.lock"
MOST_LANES = 1
LANES = min(MOST_LANES, max(1, proc.DEFAULT_JOBS // heavy.SLICE))
# The name of the directory of the logs of one run, before its number, the count of the ended runs that stay, and the
# file of the directory that a live run holds a lock on (the logs).
RUN_MARK = "run-"
RUN_WIDTH = 6
RUNS_KEPT = 24
LIVE = ".live"
# An ended run that an unread verdict cites stays past RUNS_KEPT while it is younger than CITED_RUN_AGE_S, and while
# the cited runs that stay hold CITED_RUNS_BYTES or less together (the logs).  On 2026-11-18, the lanes made about 16
# runs in an hour, of a median of 7.8 MB.
CITED_RUN_AGE_S = 24 * 3600.0
CITED_RUNS_BYTES = 2 << 30
# The mark after the name of the configuration in the name of a log of the round after the reduce (the logs).
AGAIN_MARK = ".again"
# The step name of the copy of the report of the suite, which holds the output of each red test (the logs).
SUITE_REPORT = "suite.junit.xml"
# The build files of a change that selects the release builds and ThreadSanitizer, as a C or C++ file does (the
# selection, COMPILED_SUFFIXES of assay.loop.inputs).
BUILD_FILE_NAMES = ("CMakeLists.txt", "CMakePresets.json")
BUILD_FILE_SUFFIX = ".cmake"
SANITIZED_PREFIXES = ("include/", "src/", "cmake/")
BENCH_PREFIXES = ("bench/", "test/fuzz/")
# The inputs of the test fuzz_afl_probe of test/fuzz/CMakeLists.txt that are no file of BENCH_PREFIXES: the probe of
# assay.loop.fuzz, the install of the pin aflplusplus, and the pin table.  Only the build of bench-fuzz holds the test,
# and the touched tests of a change come from the build of default, so such a change selects bench-fuzz (the
# selection).
AFL_PROBE_INPUTS = ("utils/assay/loop/fuzz.py", "utils/assay/setup/install.py", "utils/pins.txt")
EVERY_PREFIXES = ("utils/tools/quarantine/", "utils/rule-table.txt")
# The ctest labels of the tests that only the build of a configuration holds (the selection).  bench-fuzz is the
# preset default with the benches and the fuzz harnesses on, so each other test of its build runs with the flags of
# default, which runs it.  On 2026-11-23 the labels gave the 53 tests that only bench-fuzz holds, of its 4673.  The
# option --labels also keeps each check that reads the build, because the units of the benches are only in bench-fuzz.
OWN_TEST_LABELS = {"bench-fuzz": "bench|fuzz_property|fuzz_boundary"}
LEDGER_PREFIX = "utils/ledgers/"
# The sentence that the gate adds to the message of a commit with a written file (the built files).
BUILT_NOTE = "The gate wrote {paths} from the build of the composed tree."
# The words of the problem of a build error of a configuration of objects, in `loop test` and before it (the built
# files).  The two give the same words, so the run of the base compares them (the run of the base).
BUILD_PROBLEM = "the build or the test run gives an error"
# The test of the suite that checks each rule of the engine plugin, the prefix of the test of a rule of the engine elf,
# and the reason cell of a new row that the gate gives (the ratchet rows).
PLUGIN_TEST = "guard_plugin"
RULE_TEST_PREFIX = "guard_"
ROW_REASON = "REASON"
# The rules of a budget, whose rows the gate raises for a candidate with a budget reason, and the test of the suite of
# each rule (the raise of a budget).  No rule of correctness is in the list.
BUDGET_RULES = ("instruction-total", "object-size", "step-cost")
BUDGET_TESTS = {f"{RULE_TEST_PREFIX}{rule.replace('-', '_')}": rule for rule in BUDGET_RULES}
# The rules of a budget whose named rows the gate lowers for a candidate with a lower (the lower of a budget).  The rule
# instruction-total has one total and no named row, and its write with no reason lowers it.
LOWER_RULES = ("object-size", "step-cost")
# The rule of a failing item of a finding line (the failing items).
ITEM_RULE = re.compile(r" \[(?P<rule>[^\]\s]+)\]$")
# The configuration that runs the full suite for each change.  Its build gives the tests that read a build (the
# selection).
ALWAYS = "default"
# The configurations of objects that run only with --v4, a lower or a change of EVERY_PREFIXES, and not with
# --all-configurations: release compiles for the level of the host, x86-64-v4 on luks-a (the selection).
ON_REQUEST = frozenset({"release"})
# The preset of the vessel, the targets that the gate builds in its build, the label of the vessel tests, and the
# prefix of the paths of the vessel (the vessel).  TORCH_DIR of the preset names a directory in TORCH_VENV of the
# source tree, which holds the patched PyTorch.
VESSEL = "vessel"
VESSEL_TARGETS = ("crucible_vessel", "crucible_dispatch")
# The counted plugin rules that the vessel step checks over VESSEL_TARGETS, and whose reduce reads the vessel build
# (the vessel).  Each one counts the findings of each unit, so the objects of two targets give a correct part of its
# counts.  The counted rules definition-home, home, must-consume and syscalls are not here: each one judges the whole
# target all (a header that no unit read, a row of the rule table with no site, a grant set that no unit compared),
# so a build of two targets gives errors that are no findings of the vessel.
VESSEL_COUNTED_RULES = ("process-wide-state", "proof-routes", "quarantine")
VESSEL_LABEL = "vessel"
VESSEL_PREFIX = "vessel/"
TORCH_VENV = ".venv-torch"
# The memory of one job of each kind of each configuration, in GB (the budget of assay.loop.heavy): the 99th
# percentile of the peaks of the compiles and of the tests, and the largest peak of the links.  The records of the build
# launcher of the seven lanes gave them on 2026-11-15, from 926 to 2461 compiles, 3081 to 3817 links and about 23000
# tests for each configuration.  A link is about as heavy as each other link, so its peak counts.  The vessel compiles
# 96 shards of the same size, so its compile counts at a peak too: 1.65 GB, the peak of a shard on 2026-11-14 with the
# full debug information, above the 1.56 GB with -g1.  release-v3 takes the row of release.  On 2026-11-16, the same
# method gave 0.578, 0.316 and 0.372 GB for one build of release-v3, and 0.572, 0.318 and 0.368 GB for the release
# build of the first lane.
KIND_GB_PER_JOB = {
    "default": {"compile": 0.56, "link": 0.50, "test": 0.35},
    "release": {"compile": 0.52, "link": 0.32, "test": 0.31},
    "release-v3": {"compile": 0.52, "link": 0.32, "test": 0.31},
    "tsan": {"compile": 0.49, "link": 0.36, "test": 0.40},
    "ubsan-strict": {"compile": 0.51, "link": 0.38, "test": 0.31},
    "bench-fuzz": {"compile": 0.81, "link": 0.50, "test": 0.30},
    "vessel": {"compile": 1.65, "link": 0.96, "test": 1.13},
}
# The kinds of jobs of each step of a lease.  A token of the jobserver names no kind, so a lease reserves the largest
# kind of the step that holds it.  `loop test` builds and tests in one command, and the vessel builds and tests in two.
BUILD_KINDS = ("compile", "link")
EACH_KIND = ("compile", "link", "test")
TEST_KINDS = ("test",)
# The share of a GB that the memory of a job of KIND_GB_PER_JOB rounds up to.
GB_STEP = 0.1
# The file of the work directory with one JSON line for each kill for lack of memory (the kill for memory).
KILL_LOG = "oom-kills.jsonl"
# The file whose first line gives the CPU times of all CPUs of the host, in jiffies, the count of its fields that hold
# the total (user through steal, because the times of a guest are in user and nice too), and the fields of the idle
# time in that line: idle and iowait (the use of the host).
STAT_FILE = Path("/proc/stat")
TOTAL_FIELDS = 8
IDLE_FIELDS = (3, 4)
# The forms of a file HEAD that names a commit: a hash of SHA-1 or of SHA-256 in lowercase hexadecimal digits, and the
# branch with no commit that repair_head() writes when the reflog names no commit (the warm tree).
HASH_LENGTHS = (40, 64)
HEX_DIGITS = "0123456789abcdef"
UNBORN_REF = "refs/heads/main"
# The outputs that a build never leaves with no bytes, the reply directory of the CMake file API in a build
# directory, and the number of empty outputs that the line of repair_builds() names (the warm tree).
OUTPUT_SUFFIXES = (".o", ".a", ".so")
REPLY_DIRECTORY = (".cmake", "api", "v1", "reply")
# The graph of Ninja in a build directory, and the entry of the CMake cache that names the ninja of the build (the
# warm tree).
GRAPH_FILE = "build.ninja"
NINJA_ENTRY = "CMAKE_MAKE_PROGRAM"
EMPTY_SHOWN = 5
# The word of a configure that drops the cache of the build directory first (the compiler heal).
FRESH_WORD = "--fresh"
# The weight of the lease of the vessel in the slot pool (the weight of assay.loop.heavy).  With the weight 2, four runs
# of the gate built the vessel at the same time with 64 jobs each, and the host ran out of memory two times on
# 2026-11-15.  Each lease reads the available memory one time at its start, so leases that start together each count
# all of it.  The weight is 1 until the pool holds one memory budget for all leases.
VESSEL_WEIGHT = 1
# The rule that configures the build of a configuration of objects again after its first build (the run of a
# configuration).
RECONFIGURE_RULE = "reconfigure-noop"
# The engines and the policies of a counted rule (the counted rules).
COUNTED_ENGINES = frozenset({rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF})
COUNTED_POLICIES = frozenset({rule_registry.Policy.COUNT, rule_registry.Policy.ADMIT})
# The statuses of a test that count as red, the status of a skip, of a test that ran and passed, and of a disabled test.
RED = frozenset({ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.NOTRUN})
SKIPPED = ctest_reader.TestStatus.SKIPPED
RAN = ctest_reader.TestStatus.RUN
DISABLED = ctest_reader.TestStatus.DISABLED
# The limit of one call of `loop test` and of one configure, in seconds.
TEST_RUN_S = 2 * proc.BUILD_S
CONFIGURE_S = proc.TOOL_S
# The one wall guard of a test in the gate, in seconds (the hang guard).  It is no judge of a test.  It only bounds a
# hang, and a test that gets to it is red and is a defect to correct.
TEST_TIMEOUT_S = 1800
# The room of the run of the red tests alone above the guards of its tests, in seconds: ctest starts, stops a test at
# its guard and writes its report in that time, also on a loaded host.
ALONE_ROOM_S = proc.TOOL_S
# The bytes of the output of one red test that the report of the run alone keeps.  The default of ctest is 300 KB
# (the failing items).
ALONE_OUTPUT_BYTES = 64 * 1024 * 1024
# A finding line of a check (the format of assay.core.report), and the words with which ctest replaces the part of an
# output past its limit.  The pattern has no nested repeat, so a match is linear in the length of the line.
FINDING_LINE = re.compile(r"^(?P<path>[^\s:][^:\n]*):(?P<line>\d+): error: \[(?P<rule>[^\]\s]+)\] ", re.MULTILINE)
CUT_MARK = "test output was removed since it exceeds the threshold"
# The interval of the look of a command at the stop event, in seconds (the stop).  The other configurations wait for
# the build of ALWAYS with the same interval (the threads).  The wait between SIGINT and SIGKILL to the group of a
# command at a stop or at its limit, in seconds.
STOP_POLL_S = 0.2
STOP_GRACE_S = 30.0


@dataclass(frozen=True, slots=True)
class Configuration:
    """One configuration of the gate: a configure preset with the tier gate (the configurations).

    `build` is the build directory of the preset, relative to the tree.  `configuration_name` is the name of the
    configuration of its build (assay.core.configuration).
    """

    name: str
    build: str
    configuration_name: str
    is_parse: bool


def configurations(root: Path) -> tuple[Configuration, ...]:
    """Return each configure preset of a tree with the tier gate, the parse presets first (the configurations).

    Complexity: one read of the presets file, and linear in the presets.

    Raises:
        GateError: If a preset has a problem, a build directory is not in the tree, or no preset has the tier gate
    """
    read = tiers.read(root)
    if read.each_problem:
        raise GateError(f"the gate refuses the presets of {root}: {'; '.join(read.each_problem)}")
    found: list[Configuration] = []
    for preset in read.configure:
        if preset.tier is not tiers.Tier.GATE or preset.build is None:
            continue
        if not preset.build.is_relative_to(root):
            raise GateError(f"the build directory {preset.build} of the preset {preset.name} is not in the tree {root}")
        build = preset.build.relative_to(root).as_posix()
        found.append(Configuration(preset.name, build, preset.configuration, preset.is_parse))
    if not found:
        raise GateError(f"no configure preset of {root / tiers.PRESETS_FILE} has the tier gate")
    return tuple(sorted(found, key=lambda item: not item.is_parse))


def vessel_configuration(root: Path) -> Configuration | None:
    """Return the configuration of the preset VESSEL of a tree, or None when the presets file gives no such preset.

    the vessel gives the rules.  Complexity: one read of the presets file, and linear in the presets.

    Raises:
        GateError: If the preset has a problem, or its build directory is not in the tree
    """
    preset = next((item for item in tiers.read(root).configure if item.name == VESSEL), None)
    if preset is None:
        return None
    if preset.problem or preset.build is None:
        reason = preset.problem or "it has no build directory"
        raise GateError(f"the gate refuses the preset {VESSEL} of {root}: {reason}")
    if not preset.build.is_relative_to(root):
        raise GateError(f"the build directory {preset.build} of the preset {VESSEL} is not in the tree {root}")
    return Configuration(VESSEL, preset.build.relative_to(root).as_posix(), preset.configuration, False)


class GateError(Exception):
    """The warm tree cannot hold the commit, or a step of the gate cannot start."""


class Stopped(Exception):
    """A step of a run ends, because a different run of the check gave a problem (the stop)."""


class Mode(enum.Enum):
    """How one configuration runs (the selection).  The value is the words that the gate writes before its step."""

    PARSE = "parse and check"
    FULL = "build and test"
    CHANGED = "build and test the changed inputs"
    TOUCHED = "build and run the touched tests"
    VESSEL = "build the vessel and run the vessel tests"


@dataclass(frozen=True, slots=True)
class Run:
    """One configuration of a run of the gate: its mode, the names of the tests of the mode TOUCHED, and --build-only.

    With `is_build_only`, the run skips each test of the master guard that reads no build, and default runs it (the
    selection).
    """

    configuration: Configuration
    mode: Mode
    names: tuple[str, ...] = ()
    is_build_only: bool = False


@dataclass(frozen=True, slots=True)
class Focus:
    """One configuration of a run of the base, and the tests that were red alone too in it (the run of the base).

    With no names, the problem of the configuration was not a red test, and the run of the base runs the configuration.
    """

    configuration: str
    names: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Candidate:
    """The commit of a landing: the commit that the composition started from, the commit, the paths and the focus.

    `focus` is empty for the commit of a batch, and it gives the configurations of a run of the base (the run of the
    base).  `is_every` selects each configuration other than those of ON_REQUEST, as --all-configurations does (the
    selection).  A candidate with `lowers` selects those of ON_REQUEST too.  `built` names each
    file of BUILT_FILES that the merge of assay.loop.land leaves to the gate.  The gate adds each file that the change
    can change, and it writes each one before the tests of each configuration (the built files).  `title` names the
    run in the first line of the check, such as "the run of the base at COMMIT".  The landing gives it, because only
    the landing knows the kind of the run.  With no title, the line names the commit.  `on_fold` gets the new commit
    when the gate folds the built files into the commit, before the tests of each configuration (the built files).
    The landing then writes that commit into its entry of the stack (the stack of assay.loop.land).  `budget_reason`
    is the reason of each row of a budget that the gate raises with no text of its own, or "".  `budget_rows` holds
    the item and the text of each row with a text of its own (the raise of a budget).  `lowers`
    holds the rule, the item and the reason of the cut of each row that the gate lowers, and a candidate with a lower
    selects each configuration (the lower of a budget).
    """

    head: str
    commit: str
    changed: tuple[str, ...]
    focus: tuple[Focus, ...] = ()
    is_every: bool = False
    built: tuple[str, ...] = ()
    title: str = ""
    on_fold: Callable[[str], None] | None = field(default=None, compare=False)
    budget_reason: str = ""
    lowers: tuple[tuple[str, str, str], ...] = ()
    budget_rows: tuple[tuple[str, str], ...] = ()

    @property
    def has_budget_reason(self) -> bool:
        """Tell if the candidate gives a text for a raise of a budget: a plain text or the text of a named row."""
        return bool(self.budget_reason or self.budget_rows)


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of the gate: the configurations that ran, each problem, and the wall times in seconds.

    `seconds` holds each wait.  `step_seconds` gives the time of each configuration after its wait for its first slot,
    in the sequence of `configurations`.  `wait_seconds` gives that wait, and `slot_seconds` the sum of the slots of
    the configuration over its time (the jobserver).  `final` is the commit that the gate judged
    last, and `ledgers` holds each ledger that the reduce folded into it.  `final` is "" for a verdict of no commit.
    `logs` is the directory of the logs of the run (the logs).  `focus` gives each configuration with a problem (the
    verdict).  `flaky` gives each test that was red in the suite and green alone, as CONFIGURATION: TEST.  `built`
    holds each file that the gate wrote from the build of ALWAYS and folded into `final` (the built files).  `rows`
    holds the lines of the ratchet rows of a refused commit.  `items` holds the failing items of the problems (the
    failing items).  A verdict with problems and no items gets each problem as its own item.  `raised` holds a line for
    each row of a budget that the gate raised, with the old and the new value (the raise of a budget).  `lowered`
    holds a line for each row that the gate lowered, or the line of a lower that wrote nothing (the lower of a budget).
    `host_busy_pct` is the busy share of all CPUs of the host over the run, for each process of the host, or 0.0 when
    STAT_FILE does not read.  `cpu_seconds` is the CPU time of the processes that the run started and waited for, and
    `host_cpus` the count of the CPUs of the host, so cpu_seconds / (seconds * host_cpus) is the share of the run (the
    use of the host).
    """

    configurations: tuple[str, ...]
    problems: tuple[str, ...]
    seconds: float
    step_seconds: tuple[float, ...] = ()
    final: str = ""
    ledgers: tuple[str, ...] = ()
    logs: str = ""
    focus: tuple[Focus, ...] = ()
    flaky: tuple[str, ...] = ()
    built: tuple[str, ...] = ()
    wait_seconds: tuple[float, ...] = ()
    slot_seconds: tuple[float, ...] = ()
    rows: tuple[str, ...] = ()
    items: tuple[str, ...] = ()
    raised: tuple[str, ...] = ()
    lowered: tuple[str, ...] = ()
    host_busy_pct: float = 0.0
    cpu_seconds: float = 0.0
    host_cpus: int = 0

    def __post_init__(self) -> None:
        """Give each problem as its own item when the verdict has no items, so a problem always has an item."""
        if not self.items:
            object.__setattr__(self, "items", self.problems)

    @property
    def is_accepted(self) -> bool:
        """Tell if the gate accepts the commit: no configuration has a problem."""
        return not self.problems


@dataclass(frozen=True, slots=True)
class Outcome:
    """The end of the run of one configuration in a check (the threads, the stop).

    `red` holds the tests that were red alone too, and `flaky` the tests that were red in the suite and green alone.
    `is_stopped` tells that the stop ended the run, which then has no problem of its own.  `seconds` starts after the
    wait for the first slot of the lease, and `wait_s` is that wait.  `slot_seconds` is the sum of the slots of the
    lease over its time (the jobserver).  `items` holds the failing items of the problems (the failing items).  An
    outcome with no items gives each problem as its own item.
    """

    name: str
    problems: tuple[str, ...]
    red: tuple[str, ...]
    seconds: float
    is_stopped: bool
    flaky: tuple[str, ...] = ()
    wait_s: float = 0.0
    slot_seconds: float = 0.0
    items: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class BuiltFile:
    """One generated file that the gate writes from the build of ALWAYS (the built files).

    `target` is the target of `loop derive` that writes the file.  `module` is the module of the target.  Its function
    is_input(path) tells if a change of a path can change the file, and the modules of its import closure can change
    the file too.  `is_vessel_input` tells if the build of the vessel targets or a vessel test reads the file.  The
    tests of the vessel then wait for the write (the vessel).
    """

    target: str
    module: str
    is_vessel_input: bool


# ── The selection ──────────────────────────────────────────────────────────

# The generated files that the gate writes from the build of ALWAYS (the built files).
BUILT_FILES = {
    "misc/mint-inventory.md": BuiltFile("mint-inventory", "assay.loop.derive.mint_inventory", is_vessel_input=False)
}
CONFIGURATIONS = configurations(paths.REPO_ROOT)
BY_NAME = {configuration.name: configuration for configuration in CONFIGURATIONS}
VESSEL_CONFIGURATION = vessel_configuration(paths.REPO_ROOT)
# The errors of a read of the selection that give its widest result (the careful selection).
READ_ERRORS = (GateError, git.GitError, proc.ToolFailed, OSError, ValueError)


def careful[Result](reading: Callable[[], Result], widest: Result, what: str, result_text: str) -> Result:
    """Return the result of a read of the selection, or its widest result when the read gives an error.

    the careful selection gives the rule.  The function writes one line with the error.

    Args:
        reading: The read
        widest: The widest result of the read
        what: The words that tell what the read reads
        result_text: The words that tell the widest result
    """
    try:
        return reading()
    except READ_ERRORS as problem:
        check_report.write_line(
            f"loop land: the code of the gate cannot read {what}: {problem}.  The gate selects {result_text}"
        )
        return widest


def is_build_file(path: str) -> bool:
    """Tell if a path is a build file (the selection)."""
    return PurePosixPath(path).name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIX)


def is_compiled_change(path: str) -> bool:
    """Tell if a path of a change is a C or C++ file or a build file, which a build of a configuration can read."""
    return guard_inputs.is_compiled_file(path) or is_build_file(path)


def select(
    changed: Sequence[str], parse_presets: Sequence[str], is_every: bool, is_v4: bool = False
) -> tuple[Configuration, ...]:
    """Return the configurations of the full suite and the parse configurations of a change (the selection).

    The configurations are in the sequence of CONFIGURATIONS.

    Args:
        changed: The paths that the change touches
        parse_presets: The parse presets that run: those that `loop syntax --changed` names, and each parse preset
            when the change touches the inputs of a rule of COUNTED_RULES
        is_every: Select each configuration other than those of ON_REQUEST.  A change of EVERY_PREFIXES selects
            each configuration, those of ON_REQUEST too
        is_v4: Select each configuration of ON_REQUEST too

    Complexity: linear in the paths and the names.
    """
    names = {ALWAYS}
    for path in changed:
        if is_compiled_change(path):
            names.update(("release-v3", "tsan"))
        is_top_build_file = is_build_file(path) and "/" not in path
        if path.startswith(SANITIZED_PREFIXES) or is_top_build_file:
            names.update(("tsan", "ubsan-strict"))
        if path.startswith(BENCH_PREFIXES) or path in AFL_PROBE_INPUTS:
            names.add("bench-fuzz")
        if path.startswith(EVERY_PREFIXES):
            is_every = True
            names.update(ON_REQUEST)
    names.update(preset for preset in parse_presets if preset in BY_NAME)
    if is_v4:
        names.update(ON_REQUEST)
    return tuple(item for item in CONFIGURATIONS if (is_every and item.name not in ON_REQUEST) or item.name in names)


def changes_each_build(tree: Path, change: guard_inputs.Change, changed: Sequence[str]) -> bool:
    """Tell if a change can change each build: a C or C++ file, a build file, or an input of a meter of a test.

    The meters are the test launcher and the fixture driver (meter_inputs() of assay.loop.affected, the selection).
    Complexity: linear in the paths, and one parse of each module of the closures of the meters.
    """
    if any(is_compiled_change(path) for path in changed):
        return True
    return any(meter.is_touched(change) for meter in affected.meter_inputs(tree))


def reaches_vessel(changed: Sequence[str], is_each: bool) -> bool:
    """Tell if the vessel runs for a change: it can change each build, or it changes a path of VESSEL_PREFIX.

    the vessel gives the rule.  `is_each` tells that the change can change each build (changes_each_build()).
    Complexity: linear in the paths.
    """
    return is_each or any(path.startswith(VESSEL_PREFIX) for path in changed)


def is_read_by_vessel(built: Sequence[str], rows: Mapping[str, BuiltFile] = BUILT_FILES) -> bool:
    """Tell if the vessel reads a file that the gate writes for a candidate, so its tests wait for the write.

    the vessel gives the rule.  `built` holds the paths of the files that the gate writes, and each one is a key of
    `rows`.  Complexity: linear in the paths.
    """
    return any(rows[path].is_vessel_input for path in built)


def built_selection(tree: Path, changed: Sequence[str]) -> tuple[str, ...]:
    """Return each file of BUILT_FILES that a change can change, sorted (the built files).

    The function is_input() of the module of a row judges each path.  A path of a module of the import closure of that
    module selects the file too.  The code of the gate imports each module of a row here, and only here.

    Raises:
        GateError: If the module of a row cannot be imported, or it gives no function is_input()
        git.GitError: If the tree is not a git work tree

    Complexity: linear in the paths for each row, and one parse of each module of the closure of a row.
    """
    import importlib

    deriver = guard_inputs.Deriver(tree)
    found: list[str] = []
    for path, row in sorted(BUILT_FILES.items()):
        try:
            is_input: Callable[[str], bool] = importlib.import_module(row.module).is_input
        except (ImportError, AttributeError) as problem:
            raise GateError(f"the module {row.module} of {path} gives no function is_input(): {problem}") from None
        code = {file for name in deriver.closure([row.module]) if (file := deriver.module_file(name)) is not None}
        if any(item in code or is_input(item) for item in changed):
            found.append(path)
    return tuple(found)


def link_torch(root: Path, tree: Path) -> str | None:
    """Give the warm tree the link TORCH_VENV to the patched PyTorch of the main work tree, or give a problem (the vessel).

    The main work tree is the parent of the git directory that the work trees of the repository of root share.  A
    directory TORCH_VENV of the warm tree that is no link stays as it is.  A link to a different directory gets the
    directory of the main work tree.  A link that cannot be made is a problem too.

    Raises:
        git.GitError: If root is not in a git work tree
    """
    source = git.common_dir(root).parent / TORCH_VENV
    if not source.is_dir():
        return (
            f"{VESSEL}: the main work tree {source.parent} has no directory {TORCH_VENV}, which holds the patched "
            f"PyTorch that TORCH_DIR of the preset {VESSEL} names.  The gate cannot build the vessel, so it refuses "
            f"the commit.  Make the virtual environment {source} with the wheel of the patched PyTorch"
        )
    link = tree / TORCH_VENV
    try:
        if link.is_symlink() and link.readlink() != source:
            link.unlink()
        if not link.is_symlink() and not link.exists():
            link.symlink_to(source, target_is_directory=True)
    except OSError as problem:
        return f"{VESSEL}: the gate cannot make the link {link} to {source}: {problem}"
    return None


def touches_parse_rules(tree: Path, change: guard_inputs.Change) -> bool:
    """Tell if a change touches the inputs of a rule that a parse configuration checks (COUNTED_RULES, the selection).

    The caller gives the change with no C or C++ file and no build file.  A change of such a file changes the result of
    a parse build only through an arm, and `loop syntax --changed` names the parse configurations of an arm.

    Raises:
        git.GitError: If the tree is not a git work tree

    Complexity: the cost of assay.loop.inputs for each rule of COUNTED_RULES.
    """
    deriver = guard_inputs.Deriver(tree)
    return any(deriver.rule_inputs(rule_id, "check").is_touched(change) for rule_id in COUNTED_RULES)


def other_runs(others: Sequence[Configuration], is_each: bool, names: Sequence[str]) -> tuple[Run, ...]:
    """Return the runs of the configurations of objects that do not run the full suite (the selection).

    Args:
        others: The configurations of objects that select() does not give
        is_each: The change can change each build (changes_each_build())
        names: The touched tests: each test that reads the build and that has an input that the change touches
    """
    if is_each:
        return tuple(Run(item, Mode.CHANGED, is_build_only=True) for item in others)
    return tuple(Run(item, Mode.TOUCHED, tuple(names), is_build_only=True) for item in others) if names else ()


def focused_runs(focus: Sequence[Focus], vessel: Configuration | None = VESSEL_CONFIGURATION) -> tuple[Run, ...]:
    """Return the runs of a run of the base, one for each configuration of its focus (the run of the base).

    A parse configuration runs in its mode.  A configuration with red tests runs them in the mode TOUCHED, and each
    other configuration runs in the mode CHANGED.  The vessel runs in its mode, with its red tests only when the focus
    names them (the vessel).

    Raises:
        GateError: If the focus names a configuration that the gate does not have
    """
    runs: list[Run] = []
    for item in focus:
        if vessel is not None and item.configuration == vessel.name:
            runs.append(Run(vessel, Mode.VESSEL, item.names))
            continue
        configuration = BY_NAME.get(item.configuration)
        if configuration is None:
            raise GateError(
                f"the run of the base names the configuration {item.configuration}, which the gate does not have"
            )
        if configuration.is_parse:
            runs.append(Run(configuration, Mode.PARSE))
        elif item.names:
            runs.append(Run(configuration, Mode.TOUCHED, item.names))
        else:
            runs.append(Run(configuration, Mode.CHANGED))
    return tuple(runs)


def vessel_rules(tree: Path) -> tuple[str, ...]:
    """Return the rules that the vessel step checks, sorted: each plugin rule of a tree with no ledger, and each rule
    of VESSEL_COUNTED_RULES that the registry holds (the vessel).

    Each finding of a rule with no ledger is an error, so the vessel build needs no row for it.  A rule of
    VESSEL_COUNTED_RULES judges the counts of the vessel build against its rows, and its reduce reads the vessel build.

    Raises:
        GateError: If the registry does not read

    Complexity: one read of the registry.
    """
    try:
        registry = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of {tree} does not read: {problem}") from None
    return tuple(
        rule_id
        for rule_id, rule in sorted(registry.rules.items())
        if rule.engine == rule_registry.Engine.PLUGIN
        and (rule.policy == rule_registry.Policy.NONE or rule_id in VESSEL_COUNTED_RULES)
    )


def counted_rules(tree: Path) -> tuple[str, ...]:
    """Return each counted rule of a tree, sorted (the counted rules).

    A ledger with no row gives a write no fall and no move, and its rule is not counted.

    Raises:
        GateError: If the registry or the ledger of a counted rule does not read

    Complexity: one read of the registry, and one read of the ledger of each rule of COUNTED_ENGINES and
    COUNTED_POLICIES.
    """
    try:
        registry = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of {tree} does not read: {problem}") from None
    counted: list[str] = []
    for rule_id, rule in sorted(registry.rules.items()):
        if rule.engine not in COUNTED_ENGINES or rule.policy not in COUNTED_POLICIES:
            continue
        try:
            ledger = rule_ledger.read(rule, tree)
        except (OSError, ValueError) as problem:
            raise GateError(f"the ledger of {rule_id} in {tree} does not read: {problem}") from None
        if ledger.base:
            counted.append(rule_id)
    return tuple(counted)


def ratchet_tests(tree: Path, counted: Sequence[str]) -> dict[str, tuple[rule_registry.Rule, ...]]:
    """Return the counted rules of each test of the suite that checks them (the ratchet rows).

    PLUGIN_TEST checks each rule of the engine plugin, and RULE_TEST_PREFIX with the identifier of a rule of the engine
    elf, with "_" in place of "-", names the test of that rule.  Complexity: one read of the registry, and linear in the
    counted rules.

    Raises:
        GateError: If the registry of the tree does not read
    """
    try:
        registry = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of {tree} does not read: {problem}") from None
    found: dict[str, list[rule_registry.Rule]] = {}
    for rule_id in counted:
        rule = registry.rules.get(rule_id)
        if rule is None:
            continue
        is_plugin = rule.engine is rule_registry.Engine.PLUGIN
        test = PLUGIN_TEST if is_plugin else f"{RULE_TEST_PREFIX}{rule_id.replace('-', '_')}"
        found.setdefault(test, []).append(rule)
    return {test: tuple(rules) for test, rules in found.items()}


def row_kind(line: str) -> str:
    """Return the KIND cell of a row of a ledger, PATH | KIND | VALUE | ENTITY | REASON, or "" for a different line."""
    cells = line.split(" | ", 2)
    return cells[1] if len(cells) > 1 else ""


def is_budget_outcome(outcome: Outcome) -> bool:
    """Tell if each problem of an outcome is a crossed row of a budget (the raise of a budget).

    Such an outcome has one problem: the tests that are red alone too.  Each such test is a test of BUDGET_TESTS, and
    each failing item is a finding line of a rule of BUDGET_RULES.  A test with no finding line, or with an output that
    ctest cut, gives an item with no rule, and such an outcome is no outcome of a budget.  Complexity: linear in the
    items.
    """
    if len(outcome.problems) != 1 or not outcome.red or not outcome.items:
        return False
    if not all(name in BUDGET_TESTS for name in outcome.red):
        return False
    found = [ITEM_RULE.search(item) for item in outcome.items]
    return all(match is not None and match["rule"] in BUDGET_RULES for match in found)


def is_budget_round(outcomes: Sequence[Outcome]) -> bool:
    """Tell if a round has a problem, and each outcome with a problem is an outcome of a budget (the raise of a budget).

    A stopped outcome tells that a different problem set the stop, so a round with one is no round of a budget.
    """
    if any(item.is_stopped for item in outcomes):
        return False
    red = [item for item in outcomes if item.problems]
    return bool(red) and all(is_budget_outcome(item) for item in red)


def with_raise_problems(outcomes: Sequence[Outcome], lines: Sequence[str]) -> list[Outcome]:
    """Return the outcomes with the lines of a raise that wrote nothing as problems of the first outcome with a problem.

    The verdict gives each problem to each requester of the run, and a line of the holder reaches no requester of a
    different process (the raise of a budget).  The failing items stay, so the compare with the run of the base does
    not change.  Complexity: linear in the outcomes.
    """
    found: list[Outcome] = []
    is_added = False
    for item in outcomes:
        is_first = bool(item.problems) and not is_added
        found.append(replace(item, problems=(*item.problems, *lines)) if is_first else item)
        is_added = is_added or is_first
    return found


def ledger_values(text: str) -> dict[tuple[str, str, str], str]:
    """Return the value of each row of a ledger text by its key: PATH, KIND and ENTITY.

    A row is PATH | KIND | VALUE | ENTITY | REASON.  Complexity: linear in the lines of the text.
    """
    rows: dict[tuple[str, str, str], str] = {}
    for line in text.splitlines():
        cells = line.split(" | ", 4)
        if len(cells) == 5 and not line.startswith("#"):
            rows[(cells[0], cells[1], cells[3])] = cells[2]
    return rows


def shown_key(key: tuple[str, str, str]) -> str:
    """Return the cells of the key of a row with no empty cell '-', as one text."""
    return " ".join(cell for cell in key if cell != "-")


def raised_lines(path: str, before: str, after: str) -> list[str]:
    """Return a line for each row of a ledger that a write changed: its key, its old value or no row, and its new value.

    A row that the write did not change gives no line, and a row that the write removed has the new value "no row".
    Complexity: linear in the lines of the two texts.
    """
    old, new = ledger_values(before), ledger_values(after)
    lines: list[str] = []
    for key in sorted(old.keys() | new.keys()):
        if old.get(key) == new.get(key):
            continue
        lines.append(f"{path}: {shown_key(key)} from {old.get(key, 'no row')} to {new.get(key, 'no row')}")
    return lines


def is_named_row(item: str, key: tuple[str, str, str]) -> bool:
    """Tell if an item of `--budget-reason ITEM=TEXT` names a row: its path, its path with no @build/ mark, its entity.

    The cell '-' is an empty cell, and no item names it (the raise of a budget).
    """
    path, _kind, entity = key
    return (path != "-" and item in (path, path.removeprefix(cost_meter.BUILD_PATH_PREFIX))) or (
        entity != "-" and item == entity
    )


def is_checked(configuration: Configuration, rule_id: str) -> bool:
    """Tell if the run of a configuration checks a rule: each run of objects, and a parse run for COUNTED_RULES."""
    return not configuration.is_parse or rule_id in COUNTED_RULES


def is_each_built(outcomes: Sequence[Outcome]) -> bool:
    """Tell if the outcomes of a round build each configuration of CONFIGURATIONS with no problem (the reduce).

    Complexity: linear in the outcomes.
    """
    built = {item.name for item in outcomes if not item.is_stopped and not item.problems}
    return all(item.name in built for item in CONFIGURATIONS)


def built_vessel(vessel: Configuration | None, outcomes: Sequence[Outcome]) -> Configuration | None:
    """Return the vessel configuration when the outcomes hold a run of it with no problem and no stop, or None (the
    reduce).

    Complexity: linear in the outcomes.
    """
    if vessel is None:
        return None
    is_built = any(item.name == vessel.name and not item.problems and not item.is_stopped for item in outcomes)
    return vessel if is_built else None


def reduce_words(tree: Path, rule_id: str, vessel: Configuration | None = None) -> list[str]:
    """Return the words after `guard write` of the reduce of one counted rule in a tree (the reduce).

    The words give the build of each configuration that checks the rule (is_checked()).  `vessel` is the vessel
    configuration when the round built the vessel with no problem.  A rule of VESSEL_COUNTED_RULES then reads the
    objects of VESSEL_TARGETS in its build too.  With no vessel build, the write keeps each row of a unit that only
    the vessel compiles (the unread units of assay.guard.engines.plugin.runner).
    """
    words = ["write", "--rule", rule_id]
    for configuration in CONFIGURATIONS:
        if is_checked(configuration, rule_id):
            words += ["--build-dir", str(build_directory(tree, configuration))]
    if vessel is not None and rule_id in VESSEL_COUNTED_RULES:
        words += ["--build-dir", str(build_directory(tree, vessel))]
        words += [word for target in VESSEL_TARGETS for word in ("--target", target)]
    return words


# ── The warm tree ──────────────────────────────────────────────────────────


def move_tree(root: Path, tree: Path, commit: str) -> None:
    """Give the warm tree the commit, and make the tree first when it is not there (the warm tree).

    Raises:
        GateError: If the tree is in the work tree of the repository
        git.GitError: If a git command fails
    """
    resolved_root, resolved_tree = root.resolve(), tree.resolve()
    if resolved_tree == resolved_root or resolved_root in resolved_tree.parents:
        raise GateError(f"the warm tree {tree} is in the work tree {root} of the repository")
    if not (tree / ".git").is_dir():
        tree.mkdir(parents=True, exist_ok=True)
        git.init(tree)
        git.run(tree, "config", "gc.auto", "0")
    info = tree / ".git" / "objects" / "info"
    info.mkdir(parents=True, exist_ok=True)
    (info / "alternates").write_text(f"{git.common_dir(root) / 'objects'}\n", encoding="utf-8")
    repair_head(tree)
    # A file with a new time of change and the same bytes gets a new entry of the index, and read-tree then keeps it.
    git.run_any_exit(tree, "update-index", "-q", "--refresh", timeout_s=proc.TOOL_S)
    try:
        git.run(tree, "read-tree", "--reset", "-u", commit, timeout_s=proc.TOOL_S)
    except git.GitError:
        if git.run_any_exit(tree, "ls-files", "--stage", timeout_s=proc.TOOL_S).exit == 0:
            raise
        repair_index(tree)
        git.run(tree, "read-tree", "--reset", "-u", commit, timeout_s=proc.TOOL_S)
    git.run(tree, "update-ref", "--no-deref", "HEAD", commit)


def gb_per_job_of(name: str, kinds: Sequence[str] = EACH_KIND) -> float:
    """Return the memory of one job of a lease of a configuration that runs jobs of `kinds`, in GB (the budget of
    assay.loop.heavy).

    The memory is the largest of the kinds in KIND_GB_PER_JOB, rounded up to GB_STEP.  A configuration with no row,
    such as a parse configuration, has GB_PER_JOB of assay.loop.heavy.
    """
    row = KIND_GB_PER_JOB.get(name)
    if row is None:
        return heavy.GB_PER_JOB
    largest = max(row[kind] for kind in kinds)
    return round(math.ceil(round(largest / GB_STEP, 6)) * GB_STEP, 6)


def repair_head(tree: Path) -> None:
    """Give a warm tree a HEAD again, when its file HEAD holds no reference and no commit (the warm tree).

    A stop of the host during a write of git can leave HEAD with no bytes, and git then sees no repository in the
    tree.  The function writes the last commit of the reflog of HEAD into HEAD, so the repair of the index keeps the
    times of change.  When the reflog names no commit that the tree holds, HEAD names a branch with no commit, and the
    next read-tree writes each file of the commit.

    Raises:
        git.GitError: If a git command fails
    """
    head = tree / ".git" / "HEAD"
    held = head.read_bytes().strip() if head.is_file() else b""
    if held.startswith(b"ref: ") or (len(held) in HASH_LENGTHS and all(chr(byte) in HEX_DIGITS for byte in held)):
        return
    check_report.write_line(f"loop land: the file HEAD of the warm tree {tree} names no commit, so the gate writes it")
    reflog = tree / ".git" / "logs" / "HEAD"
    lines = reflog.read_bytes().splitlines() if reflog.is_file() else []
    last = lines[-1].split()[1].decode("ascii", errors="replace") if lines and len(lines[-1].split()) > 1 else ""
    head.write_text(f"{last}\n" if last else f"ref: {UNBORN_REF}\n", encoding="ascii")
    if last and git.resolve(tree, last) is None:
        head.write_text(f"ref: {UNBORN_REF}\n", encoding="ascii")


def host_jiffies(path: Path = STAT_FILE) -> tuple[int, int] | None:
    """Return the total and the idle CPU time of all CPUs of the host in jiffies, or None for a file that does not read.

    Complexity: one read of the first line of the file.
    """
    try:
        with open(path, encoding="ascii") as handle:
            words = handle.readline().split()
    except OSError, UnicodeDecodeError:
        return None
    if len(words) <= TOTAL_FIELDS or words[0] != "cpu" or not all(word.isdigit() for word in words[1:]):
        return None
    values = [int(word) for word in words[1 : TOTAL_FIELDS + 1]]
    return sum(values), sum(values[index] for index in IDLE_FIELDS)


def busy_percent(before: tuple[int, int] | None, after: tuple[int, int] | None) -> float:
    """Return the busy share of all CPUs of the host between two reads of host_jiffies(), or 0.0 with no reads."""
    if before is None or after is None or after[0] <= before[0]:
        return 0.0
    idle = max(0, after[1] - before[1])
    return round(100.0 * (1.0 - idle / (after[0] - before[0])), 1)


def children_cpu_seconds() -> float:
    """Return the user and system CPU time of each child that this process started and waited for, in seconds."""
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def remove_untracked(tree: Path) -> None:
    """Remove each file of a warm tree that the commit does not hold and that no ignore rule names (the warm tree).

    read-tree keeps such a file, for example a file of a batch that a stop ended, or a file that a repair of the
    index by hand left out of the index.  A census of the tree, such as the census of the fixtures, then reads it.
    The ignored files stay, such as the build directories and the link TORCH_VENV.  The holder of the lane is the only
    writer of the warm tree.  Complexity: one listing of the files that are not ignored.

    Raises:
        git.GitError: If git cannot list the files
    """
    listed = git.run(tree, "ls-files", "--others", "--exclude-standard", "-z", timeout_s=proc.TOOL_S)
    names = [name for name in listed.decode("utf-8", errors="surrogateescape").split("\0") if name]
    for name in names:
        (tree / name).unlink(missing_ok=True)
    if names:
        shown = " ".join(names[:EMPTY_SHOWN])
        check_report.write_line(
            f"loop land: the warm tree {tree} held {len(names)} files that its commit does not hold, and the gate "
            f"removes them: {shown}"
        )


def repair_builds(tree: Path, configurations: Sequence[Configuration]) -> None:
    """Remove the damage that a stop of the host leaves in the build directories of a warm tree (the warm tree).

    A stop during a build can leave an output with no bytes, and Ninja then sees the output as built.  The function
    removes each object, archive and shared library of OUTPUT_SUFFIXES with no bytes, and Ninja builds it again.  A
    stop during a configure can leave a reply of the CMake file API with no bytes, or an index of the reply that does
    not read as JSON.  The function then removes the reply and the graph build.ninja, so the run configures the
    build again (the run of a configuration, step 1).  A graph that ninja cannot load goes too (unloadable_graphs()).
    The holder of the lane is the only writer of the warm tree.  Complexity: linear in the files of the build
    directories, and one load of each graph.
    """
    for configuration in configurations:
        build = build_directory(tree, configuration)
        if not build.is_dir():
            continue
        empty = [path for path in output_files(build) if path.lstat().st_size == 0]
        for path in empty:
            path.unlink(missing_ok=True)
        if empty:
            shown = " ".join(str(path.relative_to(build)) for path in empty[:EMPTY_SHOWN])
            check_report.write_line(
                f"loop land: {configuration.name}: the gate removes {len(empty)} outputs with no bytes, the damage of a "
                f"stop of the host, and the build makes them again: {shown}"
            )
        reply = build.joinpath(*REPLY_DIRECTORY)
        if reply.is_dir() and not is_reply_whole(reply):
            shutil.rmtree(reply, ignore_errors=True)
            (build / GRAPH_FILE).unlink(missing_ok=True)
            check_report.write_line(
                f"loop land: {configuration.name}: the reply of the CMake file API does not read, so the gate removes "
                f"it and the run configures {build} again"
            )
    builds = {build_directory(tree, item): item.name for item in configurations}
    for build, problem in unloadable_graphs([build for build in builds if (build / GRAPH_FILE).is_file()]).items():
        (build / GRAPH_FILE).unlink(missing_ok=True)
        check_report.write_line(
            f"loop land: {builds[build]}: ninja cannot load {GRAPH_FILE}, so the gate removes it and the run "
            f"configures {build} again: {problem}"
        )


def unloadable_graphs(builds: Sequence[Path]) -> dict[Path, str]:
    """Return the first line of the error of ninja for each build whose graph build.ninja ninja cannot load.

    A configure that a stop ends after CMake writes CMakeFiles/rules.ninja and before it writes build.ninja leaves a
    graph that names a rule that the rules do not hold.  Ninja then cannot load the graph, so it never runs the step of
    the graph that configures the build again, and each run of the lane fails (2026-11-21, lane 5).  The ninja of the
    CMake cache of each build loads its graph with `ninja -t query`, and the loads run at the same time: a load of a
    graph of 42 MB took 0.35 s.  A build whose cache names no ninja is not judged.  Complexity: one load of each graph.
    """
    running: list[tuple[Path, proc.Running]] = []
    for build in builds:
        program = ninja_of(build)
        if program:
            command = proc.Command([program, "-C", str(build), "-t", "query", GRAPH_FILE], timeout_s=proc.TOOL_S)
            running.append((build, proc.start(command)))
    found: dict[Path, str] = {}
    for build, started in running:
        completed = started.wait()
        if completed.exit != 0:
            found[build] = next(iter(completed.err.strip().splitlines()), f"ninja exits {completed.exit}")
    return found


def ninja_of(build: Path) -> str:
    """Return the ninja that the CMake cache of a build names, or "" when the cache names no ninja that exists."""
    try:
        entry = cmake_reader.cache(build).get(NINJA_ENTRY)
    except cmake_reader.CmakeError:
        return ""
    program = entry.value.strip() if entry is not None else ""
    return program if program and Path(program).is_file() else ""


def output_files(build: Path) -> Iterator[Path]:
    """Give each file of a build directory whose name ends in one of OUTPUT_SUFFIXES or holds `.so.`."""
    for directory, _, names in os.walk(build):
        for name in names:
            if name.endswith(OUTPUT_SUFFIXES) or ".so." in name:
                yield Path(directory) / name


def is_reply_whole(reply: Path) -> bool:
    """Tell if each file of a reply of the CMake file API has bytes, and its newest index reads as JSON."""
    files = sorted(path for path in reply.iterdir() if path.is_file())
    if any(path.stat().st_size == 0 for path in files):
        return False
    indexes = [path for path in files if path.name.startswith("index-") and path.suffix == ".json"]
    if not indexes:
        return True
    try:
        json.loads(indexes[-1].read_text(encoding="utf-8"))
    except OSError, ValueError:
        return False
    return True


def repair_index(tree: Path) -> None:
    """Make the index of a warm tree again from its HEAD, when no git command can read it (the warm tree).

    A stop of the host during a write of git can leave an index that git cannot read, such as an index that is
    shorter than its header says.  Each read-tree then fails.  The holder of the lane is the only writer of the warm
    tree, so the function removes the index and reads the tree of HEAD into a new one.  A refresh then gives each file
    that has the bytes of HEAD its time of change, so the next read-tree writes only the files that the commit changes,
    and Ninja builds again only what the commit changes.  A tree with no HEAD gets no new index.

    Raises:
        git.GitError: If a git command fails
    """
    check_report.write_line(f"loop land: git cannot read the index of the warm tree {tree}, so the gate makes it again")
    (tree / ".git" / "index").unlink(missing_ok=True)
    head = git.resolve(tree, "HEAD")
    if head is None:
        return
    git.run(tree, "read-tree", head, timeout_s=proc.TOOL_S)
    git.run_any_exit(tree, "update-index", "-q", "--refresh", timeout_s=proc.TOOL_S)


def build_directory(tree: Path, configuration: Configuration) -> Path:
    """Return the build directory of a configuration in a tree, such as the warm tree."""
    return tree / configuration.build


def log_name(configuration: Configuration, step: str, is_again: bool = False) -> str:
    """Return the name of the log of one step of a configuration, with AGAIN_MARK in the round after the reduce.

    the logs gives the rule.  `step` is the rest of the name, such as test.log.
    """
    return f"{configuration.name}{AGAIN_MARK if is_again else ''}.{step}"


def failing_items(
    configuration: str, names: Sequence[str], outputs: Mapping[str, str], tree: Path, commit: str
) -> tuple[str, ...]:
    """Return the failing items of the tests of one configuration that are red alone too (the failing items).

    `outputs` gives the output of each test in the report of the run alone, and `commit` is the commit of the run.
    Each item is in the sequence of the tests and of their lines, with no duplicate.  Complexity: linear in the length
    of the outputs of the tests.
    """
    root = f"{tree}/"
    items: dict[str, None] = {}
    for name in names:
        text = outputs.get(name)
        if text is None or CUT_MARK in text:
            items[f"{configuration}: {name}: the items are not known in the run of the commit {commit}"] = None
            continue
        found = [
            f"{configuration}: {name}: {match['path'].removeprefix(root)}:{match['line']} [{match['rule']}]"
            for match in FINDING_LINE.finditer(text)
        ]
        items.update(dict.fromkeys(found or [f"{configuration}: {name}"]))
    return tuple(items)


def configure_words(tree: Path, configuration: Configuration) -> list[str]:
    """Return the words of the configure of the build directory of a configuration in a tree."""
    return ["cmake", "--preset", configuration.name, "-B", str(build_directory(tree, configuration))]


def configure_plan(
    tree: Path, configuration: Configuration, heal: Callable[[Path], str]
) -> tuple[list[str], str] | None:
    """Return the words of the configure of a build directory and the line of its heal, or None for no configure.

    The run of a configuration, step 1, and the compiler heal give the rules.  A build directory with no graph of Ninja
    gets the words of configure_words() and an empty line.  `heal` gives the line of `loop heal` for a build directory
    with a graph, an empty line when its cache agrees with its toolchain file.  A line gets the words with FRESH_WORD.
    """
    build = build_directory(tree, configuration)
    words = configure_words(tree, configuration)
    if not ninja_reader.graph_file(build).is_file():
        return words, ""
    line = heal(build)
    if not line:
        return None
    return [*words, FRESH_WORD], line


def configured_program(build: Path, name: str) -> str:
    """Return the cmake or the ctest that the CMake cache of a build names.

    Raises:
        GateError: If the CMake cache of the build names no such program
    """
    try:
        return cmake_pin.configured_program(build, name)
    except cmake_pin.PinError as problem:
        raise GateError(str(problem)) from None


def built_commit(root: Path, commit: str, files: Mapping[str, bytes | None]) -> str:
    """Write a commit with the parents of a commit, its tree with some files replaced, and the sentence of BUILT_NOTE.

    The message is the message of the commit, and then the sentence that names the files (the built files).
    replaced_commit() of assay.core.git gives the tree.  Complexity: the cost of replaced_commit() and three git
    commands.

    Raises:
        git.GitError: If git fails
    """
    replaced = git.replaced_commit(root, commit, files)
    parents = git.run(root, "rev-list", "--parents", "-n", "1", replaced).decode().split()[1:]
    tree = git.run(root, "rev-parse", f"{replaced}^{{tree}}").decode().strip()
    note = BUILT_NOTE.format(paths=" and ".join(sorted(files)))
    return git.commit_tree(root, tree, parents, f"{git.message_of(root, commit).rstrip()}\n\n{note}\n")


# ── The gate ───────────────────────────────────────────────────────────────


def no_cited_runs() -> frozenset[str]:
    """Return no name of a run: a gate with no queue keeps no ended run for a verdict (the logs)."""
    return frozenset()


@dataclass(frozen=True, slots=True)
class Gate:
    """The gate of one session: the repository, the work directory, the jobs, --all-configurations, and the memory.

    `jobs` is the most jobs of the lease of one configuration, with SLICE of assay.loop.heavy as the most (slice).
    `read` is the reader of the available memory, which gives the cap of a lease (the memory of assay.loop.heavy).
    `run` names the directory of the logs of one run in LOGS, or it is "" for LOGS itself (the logs).  `pool` is the
    slot pool of the leases, or None for the pool of the host (the jobserver).  `lane` names the warm tree (the
    lanes).  `vessel` is the configuration of the vessel, or None for a gate with no vessel (the vessel).  `oom_kills`
    gives the count of the kills of the kernel for lack of memory (the kill for memory).  `cited` gives the name of each
    ended run that an unread verdict cites, which the sweep of the runs keeps (the logs).  `test_timeout_s` is the wall
    guard of one test (the hang guard).  `is_v4` adds each configuration of ON_REQUEST, as --v4 does (the selection).
    """

    root: Path
    work: Path
    jobs: int
    is_every: bool
    read: heavy.Reader = heavy.available_gb
    run: str = ""
    pool: heavy.Pool | None = None
    lane: int = 0
    vessel: Configuration | None = VESSEL_CONFIGURATION
    oom_kills: Callable[[], int] = heavy.oom_kills
    cited: Callable[[], frozenset[str]] = no_cited_runs
    test_timeout_s: int = TEST_TIMEOUT_S
    is_v4: bool = False

    @property
    def guard_words(self) -> list[str]:
        """The words that give ctest the wall guard of one test (the hang guard)."""
        return ["--timeout", str(self.test_timeout_s)]

    @property
    def slice(self) -> int:
        """The most jobs of the lease of one configuration: `jobs`, with SLICE of assay.loop.heavy as the most."""
        return max(1, min(self.jobs, heavy.SLICE))

    @property
    def tree(self) -> Path:
        """The warm tree of the lane of the gate (the lanes)."""
        return self.work / tree_name(self.lane)

    @property
    def logs(self) -> Path:
        """The directory of the logs of the run."""
        return self.work / LOGS / self.run if self.run else self.work / LOGS

    def master(self, name: str) -> list[str]:
        """Return the words that run a master of the warm tree."""
        return [sys.executable, str(self.tree / "utils" / "bin" / name)]

    def _parse_presets(self, environment: Mapping[str, str]) -> list[str]:
        """Return each parse preset that `loop syntax --changed` of the warm tree names.

        Raises:
            GateError: If the command cannot run or gives an error
        """
        command = [*self.master("loop"), "syntax", "--changed", "--root", str(self.tree)]
        try:
            completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.tree, env=dict(environment)))
        except proc.ToolFailed as problem:
            raise GateError(f"`loop syntax --changed` cannot run: {problem}") from None
        if completed.exit != ExitCode.OK:
            raise GateError(f"`loop syntax --changed` gives an error: {completed.err.strip()}")
        return completed.out.split()

    def loop_words(self, run: Run, is_plain: bool, is_again: bool = False) -> list[str]:
        """Return the words of the mode of a run after `loop test BUILD` (the selection).

        A run of the full suite with `is_plain` runs the tests of the changed inputs.  The run of the touched tests
        writes the file of --only into the directory of the logs, with the name of its round (log_name()).  A run with
        `is_build_only` gets --build-only.  A configuration of OWN_TEST_LABELS gets --labels with its labels in each
        mode.
        """
        labels = OWN_TEST_LABELS.get(run.configuration.name)
        own = ["--labels", labels] if labels is not None else []
        if run.mode is Mode.TOUCHED:
            self.logs.mkdir(parents=True, exist_ok=True)
            only = self.logs / log_name(run.configuration, "only.txt", is_again)
            only.write_text("".join(f"{name}\n" for name in run.names), encoding="utf-8")
            words = ["--only", str(only), "--build-only"] if run.is_build_only else ["--only", str(only)]
            return [*words, *own]
        words = ["--all"] if run.mode is Mode.FULL and not is_plain else []
        return [*words, "--build-only", *own] if run.is_build_only else [*words, *own]

    def touched_names(self, change: guard_inputs.Change) -> tuple[str, ...]:
        """Return each touched test of the build of ALWAYS, sorted: it reads the build, and the change touches its inputs.

        The run of ALWAYS configures its build with the change first (the threads).

        Raises:
            GateError: If the build of ALWAYS or its tests cannot be read
        """
        build = build_directory(self.tree, BY_NAME[ALWAYS])
        try:
            tests = affected.build_tests(build)
        except affected.RunnerError as problem:
            raise GateError(f"the tests of {build} that read the build do not read: {problem}") from None
        return tuple(sorted(name for name, found in tests.items() if found.is_touched(change)))

    def changes_each(self, candidate: Candidate, ledgers: Sequence[str] = ()) -> bool:
        """Tell if the change of a candidate and `ledgers` can change each build (changes_each_build()).

        Raises:
            git.GitError: If git cannot read the registry at the commit that the change starts from
        """
        changed = (*candidate.changed, *ledgers)
        return changes_each_build(self.tree, guard_inputs.read_change(self.tree, candidate.head, changed), changed)

    def other_plan(
        self, candidate: Candidate, others: Sequence[Configuration], ledgers: Sequence[str] = (), is_each: bool = False
    ) -> tuple[Run, ...]:
        """Return the runs of the configurations of objects with no full suite, for the change and `ledgers`.

        `is_each` tells that the change can change each build (changes_each()).  Else, the plan reads the touched tests
        of the build of ALWAYS.  A read that gives an error gives the plan of a change that can change each build (the
        careful selection).
        """
        if not others:
            return ()
        each = other_runs(others, True, ())
        if is_each:
            return each

        def touched() -> tuple[Run, ...]:
            """Return the runs of the touched tests of the build of ALWAYS."""
            change = guard_inputs.read_change(self.tree, candidate.head, (*candidate.changed, *ledgers))
            names = self.touched_names(change)
            shown = " ".join(names) if names else "none"
            check_report.write_line(f"loop land: the touched tests of the other configurations of objects: {shown}")
            return other_runs(others, False, names)

        return careful(
            touched, each, "the touched tests", "the tests of the changed inputs in each other configuration"
        )

    def check(self, candidate: Candidate, is_retry: bool) -> Verdict:
        """Give the warm tree the commit, select the configurations, and run them at the same time (the threads).

        A candidate with a focus runs only its focus (the run of the base).  The gate writes each built file that the
        change can change, or that the candidate names, before the tests of each configuration (the built files).

        Raises:
            GateError: If the warm tree cannot hold the commit, a step cannot start, or the candidate names a built file
                that no row of BUILT_FILES gives
            git.GitError: If a git command fails
        """
        started = time.monotonic()
        host_before, cpu_before = host_jiffies(), children_cpu_seconds()
        unknown = [path for path in candidate.built if path not in BUILT_FILES]
        if unknown:
            raise GateError(
                f"the candidate names {' '.join(unknown)} as a file that the gate writes, and no row of BUILT_FILES "
                f"gives it"
            )
        move_tree(self.root, self.tree, candidate.commit)
        remove_untracked(self.tree)
        builds = (*CONFIGURATIONS, *(() if self.vessel is None else (self.vessel,)))
        repair_builds(self.tree, builds)
        if candidate.commit != candidate.head:
            built = careful(
                lambda: built_selection(self.tree, candidate.changed),
                tuple(BUILT_FILES),
                "the inputs of the built files",
                "each built file",
            )
            files = tuple(sorted({*candidate.built, *built}))
            candidate = replace(candidate, changed=tuple(dict.fromkeys((*candidate.changed, *files))), built=files)
        based = proc.environment({git.CHANGE_BASE_VARIABLE: candidate.head})
        environment = {name: value for name, value in based.items() if name != git.CHANGE_BASE_VARIABLE}
        others: tuple[Configuration, ...] = ()
        counted: tuple[str, ...] = ()
        is_each = True
        title = candidate.title or f"the commit {candidate.commit[:9]}"
        if candidate.focus:
            plan = focused_runs(candidate.focus, self.vessel)
            shown = " ".join(run.configuration.name + "".join(f" {name}" for name in run.names) for run in plan)
            check_report.write_line(f"loop land: {title} runs only {shown}, logs in {self.logs}")
        else:
            each_parse = [item.name for item in CONFIGURATIONS if item.is_parse]
            presets = careful(
                lambda: self._parse_presets(based),
                list(each_parse),
                "the arms of the change",
                "each parse configuration",
            )
            uncompiled = [path for path in candidate.changed if not is_compiled_change(path)]

            def touches() -> bool:
                """Tell if the change touches the inputs of a rule that a parse configuration checks."""
                return touches_parse_rules(self.tree, guard_inputs.read_change(self.tree, candidate.head, uncompiled))

            if careful(touches, True, "the inputs of the rules of the parse builds", "each parse configuration"):
                presets += each_parse
            counted = careful(lambda: counted_rules(self.tree), (), "the counted ledgers", "no reduce")
            selected = select(
                candidate.changed,
                presets,
                self.is_every or candidate.is_every or bool(candidate.lowers),
                self.is_v4 or bool(candidate.lowers),
            )
            plan = tuple(
                Run(item, Mode.PARSE) if item.is_parse else Run(item, Mode.FULL, is_build_only=item.name != ALWAYS)
                for item in selected
            )
            others = tuple(
                item
                for item in CONFIGURATIONS
                if not item.is_parse and item not in selected and item.name not in ON_REQUEST
            )
            is_each = (
                careful(
                    lambda: self.changes_each(candidate),
                    True,
                    "the inputs of the meters of the tests",
                    "the tests of the changed inputs in each other configuration of objects",
                )
                if others
                else True
            )
            if self.vessel is not None and reaches_vessel(candidate.changed, is_each):
                plan = (*plan, Run(self.vessel, Mode.VESSEL))
            shown = " ".join(run.configuration.name for run in plan)
            rest = f", and the tests of the change in {' '.join(item.name for item in others)}" if others else ""
            check_report.write_line(f"loop land: {title} runs in {shown}{rest} at the same time, logs in {self.logs}")
        if candidate.built:
            check_report.write_line(
                f"loop land: the change can change {' '.join(candidate.built)}, so the gate writes it from the build "
                f"of {ALWAYS} before the tests of each configuration"
            )
        step = Step(self, candidate, environment)
        try:
            step.heal_parse_builds(plan)
            if is_each:
                step.always_ready.set()

            def first_plan() -> tuple[Run, ...]:
                """Return the runs of the other configurations of the first round."""
                return self.other_plan(candidate, others, (), is_each)

            has_always = any(run.configuration.name == ALWAYS for run in plan)
            outcomes = step.write_built() if candidate.built and not has_always else []
            if not outcomes:
                outcomes = step.run_all(plan, first_plan if others else None, False, is_retry)
                step.require_problem(outcomes)
            raised: tuple[str, ...] = ()
            if not candidate.focus and candidate.has_budget_reason and is_budget_round(outcomes):
                raised = step.raise_budgets(outcomes)
            if step.raise_problems:
                outcomes = with_raise_problems(outcomes, step.raise_problems)
            if raised:
                step.unchecked = frozenset(item.name for item in outcomes if item.problems or item.is_stopped)
                outcomes = [replace(item, problems=(), red=(), items=()) for item in outcomes]
                for line in raised:
                    check_report.write_line(f"loop land: the gate raised the row {line}")
            rows: tuple[str, ...] = ()
            if not candidate.focus and counted and any(item.red for item in outcomes):
                rows = self.ratchet(step.candidate, environment, outcomes, counted)
            final: str = step.candidate.commit
            ledgers: tuple[str, ...] = ()
            lowered: tuple[str, ...] = ()
            if not candidate.focus and not any(item.problems for item in outcomes):
                if is_each_built(outcomes):
                    step.reduce(counted, built_vessel(self.vessel, outcomes))
                    lowered = step.lower_budgets() if candidate.lowers else ()
                else:
                    check_report.write_line(
                        "loop land: the landing builds only some configurations of the gate, so the gate lowers no "
                        "row of a counted ledger"
                    )
                    if candidate.lowers:
                        lowered = ("the gate lowers no named row, because the round did not build each configuration",)
                for line in lowered:
                    check_report.write_line(f"loop land: the lower of a budget: {line}")
                final, ledgers = step.fold()
            if ledgers:
                check_report.write_line(
                    f"loop land: the writes of the gate changed {' '.join(ledgers)}, folded into the commit "
                    f"{final[:9]}, and each configuration of the run runs again"
                )
                is_each_again = (
                    careful(
                        lambda: self.changes_each(candidate, ledgers),
                        True,
                        "the inputs of the meters of the tests",
                        "the tests of the changed inputs in each other configuration",
                    )
                    if others
                    else True
                )

                def again_plan() -> tuple[Run, ...]:
                    """Return the runs of the other configurations of the round after the fold."""
                    return self.other_plan(candidate, others, ledgers, is_each_again)

                outcomes += step.run_all(plan, again_plan if others else None, True, is_retry)
                step.require_problem(outcomes)
        except (jobserver.JobserverError, OSError) as problem:
            raise GateError(f"the slot pool or a jobserver of the gate fails: {problem}") from None
        ended = [item for item in outcomes if not item.is_stopped]
        return Verdict(
            tuple(item.name for item in ended),
            tuple(problem for item in outcomes for problem in item.problems),
            time.monotonic() - started,
            tuple(item.seconds for item in ended),
            final,
            ledgers,
            str(self.logs),
            tuple(Focus(item.name, item.red) for item in outcomes if item.problems),
            tuple(dict.fromkeys(f"{item.name}: {name}" for item in outcomes for name in item.flaky)),
            step.built,
            tuple(item.wait_s for item in ended),
            tuple(round(item.slot_seconds, 1) for item in ended),
            rows,
            tuple(dict.fromkeys(found for item in outcomes for found in item.items or item.problems)),
            raised,
            lowered,
            busy_percent(host_before, host_jiffies()),
            round(children_cpu_seconds() - cpu_before, 1),
            os.cpu_count() or 0,
        )

    def ratchet(
        self,
        candidate: Candidate,
        environment: Mapping[str, str],
        outcomes: Sequence[Outcome],
        counted: Sequence[str],
    ) -> tuple[str, ...]:
        """Return the ratchet rows of a red round, from a step with a stop event of its own.

        The rows only aid the author.  An error of their step gives one line and no row, and the problems of the round
        stay the verdict.
        """
        try:
            return Step(self, candidate, environment).ratchet_rows(outcomes, counted)
        except Exception as problem:  # noqa: BLE001
            check_report.write_line(f"loop land: the gate cannot give the rows of the ratchet ledgers: {problem}")
            return ()

    def check_in_run(self, candidate: Candidate, is_retry: bool) -> Verdict:
        """Check a candidate with its logs in a new run, which stays live until the check ends (the logs).

        Raises:
            GateError: If check() gives the error
            git.GitError: If a git command fails
            OSError: If the directory of the run or its lock cannot be made
        """
        with hold_run(self.work, self.cited()) as run:
            return replace(self, run=run).check(candidate, is_retry)


# ── The step of a check ────────────────────────────────────────────────────


class Step:
    """The work of one check: its commands, its threads, its leases and its stop event.

    `environment` is the environment of each command, without MAKEFLAGS.  Each command gets MAKEFLAGS of the lease of
    its configuration (the jobserver).  `always_ready` tells that the build of ALWAYS holds the configure of the change
    (the threads).  `built_ready` tells that the write of the built files ended, and it is set from the start for a
    candidate with no built file.  `built` holds each written file that changed, and the run of ALWAYS or
    write_built() is its one writer.  `unchecked` names each configuration whose first round ended before its checks
    after the tests, and the round after a raise runs those checks (the raise of a budget).  The thread of the check
    writes it before that round.  `raise_problems` holds each line of a crossed row with no text, of an item that
    names no crossed row and of a raise that wrote nothing, and the verdict gives each one as a problem
    (raise_budgets(), written_row_texts()).
    """

    __slots__ = (
        "_said",
        "always_ready",
        "built",
        "built_ready",
        "candidate",
        "environment",
        "gate",
        "raise_problems",
        "stop",
        "unchecked",
    )

    def __init__(self, gate: Gate, candidate: Candidate, environment: Mapping[str, str]) -> None:
        """Keep the gate, the candidate and the environment."""
        self.gate = gate
        self.candidate = candidate
        self.environment = {name: value for name, value in environment.items() if name != jobserver.VARIABLE}
        self.stop = threading.Event()
        self.always_ready = threading.Event()
        self.built_ready = threading.Event()
        self.built: tuple[str, ...] = ()
        self.unchecked: frozenset[str] = frozenset()
        self.raise_problems: tuple[str, ...] = ()
        if not candidate.built:
            self.built_ready.set()
        self._said = threading.Lock()

    def say(self, text: str) -> None:
        """Write one line of the gate, with no line of a different thread in it."""
        with self._said:
            check_report.write_line(text)

    @contextlib.contextmanager
    def leased(
        self, gb_per_job: float = heavy.GB_PER_JOB, is_capped: bool = False, weight: int = 1
    ) -> Iterator[heavy.Share]:
        """Hold a lease of the slot pool with the slice of the gate times `weight` for the block (the jobserver).

        `gb_per_job` is the memory of one job of the lease, and a lease with `is_capped` never grows past its slice.
        `weight` multiplies the fair share of the lease among the leases of the gate (the pool, the memory and the
        weight of assay.loop.heavy).

        Raises:
            Stopped: If the stop event is set before the lease holds a slot
        """
        with heavy.leased(
            self.gate.root,
            self.gate.slice * weight,
            self.gate.pool,
            self.gate.read,
            self.stop,
            is_gate=True,
            gb_per_job=gb_per_job,
            is_capped=is_capped,
            weight=weight,
        ) as share:
            if share is None:
                raise Stopped
            yield share

    def concurrently[Item, Result](self, items: Sequence[Item], work: Callable[[Item], Result]) -> list[Result]:
        """Run the work of each item in a thread of its own, and return the results in the sequence of the items.

        An exception of a thread sets the stop event immediately.  After each thread ended, the first exception that is
        not Stopped goes to the caller, and Stopped goes only when each exception is Stopped.  A Stopped of a thread that
        a different error stopped then does not hide that error (the stop).  The pool has at most proc.DEFAULT_JOBS
        threads.  Each caller gives fewer items.  Then each item gets a thread of its own, and no item waits for a
        different item.  Complexity: one thread for each item.
        """
        from concurrent.futures import ThreadPoolExecutor, wait

        def guarded(item: Item) -> Result:
            """Do the work of one item, and set the stop event on an exception."""
            try:
                return work(item)
            except BaseException:
                self.stop.set()
                raise

        if not items:
            return []
        with ThreadPoolExecutor(max_workers=min(proc.DEFAULT_JOBS, len(items))) as pool:
            futures = [pool.submit(guarded, item) for item in items]
            try:
                wait(futures)
            except BaseException:
                self.stop.set()
                raise
        errors = [error for future in futures if (error := future.exception()) is not None]
        for error in errors:
            if not isinstance(error, Stopped):
                raise error
        if errors:
            raise errors[0]
        return [future.result() for future in futures]

    def run_logged(self, command: list[str], log: str, share: heavy.Share, timeout_s: float) -> int:
        """Run one command in the warm tree with its two outputs in a log, in one slot of a lease, and give its exit.

        The command gets MAKEFLAGS of the jobserver of the lease (the jobserver).  A command that runs longer than its
        limit gives the exit of its end.  The command looks at the stop event each STOP_POLL_S seconds (the stop).

        Raises:
            Stopped: If the stop event is set before the command starts, or while it runs
            GateError: If the program cannot start
        """
        logs = self.gate.logs
        logs.mkdir(parents=True, exist_ok=True)
        command_line = proc.Command(
            command,
            timeout_s=timeout_s,
            cwd=self.gate.tree,
            env={**self.environment, **share.environment},
            stdout=logs / log,
            stderr=proc.Output.MERGE,
            grace_s=STOP_GRACE_S,
            stop=proc.Stop.INTERRUPT,
        )
        with share.client.held(self.stop) as is_held:
            if not is_held:
                raise Stopped
            try:
                running = proc.start(command_line)
            except proc.ToolFailed as problem:
                raise GateError(f"{command[0]} cannot run: {problem}") from None
            try:
                completed = running.poll(STOP_POLL_S)
                while completed is None:
                    if self.stop.is_set():
                        running.stop()
                        raise Stopped
                    completed = running.poll(STOP_POLL_S)
            except proc.TimedOut as problem:
                return problem.completed.exit if problem.completed is not None else ExitCode.TOOL_FAILED
            return completed.exit

    def configure(self, configuration: Configuration, share: heavy.Share, is_again: bool = False) -> str | None:
        """Configure the build directory of a configuration, and give a problem (the run of a configuration, step 1).

        A build directory with no graph of Ninja gets its configure.  A build directory whose cache names a compiler
        that its toolchain file does not resolve gets its configure with FRESH_WORD (the compiler heal).

        Raises:
            GateError: If `loop heal` of the warm tree cannot run or gives an error
        """
        plan = configure_plan(self.gate.tree, configuration, lambda build: self.heal_line(configuration, build))
        if plan is None:
            return None
        words, line = plan
        if line:
            self.say(line)
        log = log_name(configuration, "configure.log", is_again)
        if self.run_logged(words, log, share, CONFIGURE_S) != 0:
            return f"{configuration.name}: the configure gives an error, see {self.gate.logs / log}"
        return None

    def heal_parse_builds(self, plan: Sequence[Run]) -> None:
        """Configure again each configured parse build whose compiler is stale and that no run of the plan configures.

        `loop test` of each configuration of objects parses each configured parse build of the warm tree (the parse
        builds of assay.loop.affected), also when the plan runs no parse configuration.  A parse build that keeps the
        compiler of its first configure then makes cmake/StaleCompiler.cmake refuse that parse (the compiler heal).  A
        parse build with no graph of Ninja stays as it is, because `loop test` does not parse it.

        Raises:
            GateError: If `loop heal` of the warm tree gives an error, or the configure of a parse build fails
            Stopped: If the stop event is set before the lease of a configure holds a slot
        """
        planned = {run.configuration.name for run in plan}
        for configuration in CONFIGURATIONS:
            if not configuration.is_parse or configuration.name in planned:
                continue
            build = build_directory(self.gate.tree, configuration)
            if not ninja_reader.graph_file(build).is_file():
                continue
            line = self.heal_line(configuration, build)
            if not line:
                continue
            self.say(line)
            log = log_name(configuration, "configure.log", False)
            words = [*configure_words(self.gate.tree, configuration), FRESH_WORD]
            with self.leased() as share:
                if self.run_logged(words, log, share, CONFIGURE_S) != 0:
                    raise GateError(
                        f"{configuration.name}: the configure of the heal fails, see {self.gate.logs / log}"
                    )

    def heal_line(self, configuration: Configuration, build: Path) -> str:
        """Return the line of `loop heal` of the warm tree for a build directory, or an empty line (the compiler heal).

        The command of the warm tree resolves the compilers with the rules of the commit, not of this copy of the gate.

        Raises:
            GateError: If the command cannot run or gives an error
        """
        command = [*self.gate.master("loop"), "heal", str(build), configuration.name]
        try:
            completed = proc.run(
                proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.gate.tree, env=dict(self.environment))
            )
        except proc.ToolFailed as problem:
            raise GateError(f"`loop heal` cannot run: {problem}") from None
        if completed.exit != ExitCode.OK:
            raise GateError(f"`loop heal` gives an error: {completed.err.strip()}")
        return completed.out.strip()

    def regenerate(self, configuration: Configuration, share: heavy.Share) -> str | None:
        """Build the target build.ninja of the build of a configuration, and give a problem (the threads).

        The build configures the build directory again when the change touches its configure.  It gets no -j, because
        ninja obeys the jobserver only with no -j (the jobserver).

        Raises:
            GateError: If the CMake cache of the build names no cmake
        """
        build = build_directory(self.gate.tree, configuration)
        log = log_name(configuration, "regenerate.log")
        command = [configured_program(build, "cmake"), "--build", str(build), "--target", ninja_reader.GRAPH_NAME]
        if self.run_logged(command, log, share, CONFIGURE_S) != 0:
            return f"{configuration.name}: the configure gives an error, see {self.gate.logs / log}"
        return None

    def write_built(self) -> list[Outcome]:
        """Write each built file of the candidate from the build of ALWAYS before each configuration runs.

        The built files give the rules: a plan with no run of ALWAYS writes the files here.  The result is an outcome
        of ALWAYS with each problem, or no outcome.

        Raises:
            GateError: If the CMake cache of the build names no cmake
            git.GitError: If a git command of the fold fails
        """
        self.say(f"loop land: {ALWAYS}: build and write {' '.join(self.candidate.built)}")
        began = time.monotonic()
        try:
            with self.leased(gb_per_job_of(ALWAYS)) as share:
                problems = self.written_built(BY_NAME[ALWAYS], share)
        finally:
            self.built_ready.set()
        if problems:
            return [Outcome(ALWAYS, tuple(problems), (), round(time.monotonic() - began, 1), False)]
        return []

    def settle_built(self, configuration: Configuration, share: heavy.Share) -> list[str]:
        """Settle the built files before the tests of a configuration of objects, and give each problem.

        The run of ALWAYS writes each built file and folds each one that changed (written_built()).  Each other run
        builds the target all, and then waits for the end of that write (wait_built()).

        Raises:
            Stopped: If the stop event is set while the run waits
            GateError: If the CMake cache of the build names no cmake
            git.GitError: If a git command of the fold fails
        """
        if configuration.name == ALWAYS:
            try:
                return self.written_built(configuration, share)
            finally:
                self.built_ready.set()
        problem = self.build_all(configuration, share)
        if problem is not None:
            return [problem]
        self.wait_built(configuration)
        return []

    def wait_built(self, configuration: Configuration) -> None:
        """Wait for the end of the write of the built files, watch the stop event, and write the time of a wait.

        Raises:
            Stopped: If the stop event is set before or after the end of the write
        """
        began = time.monotonic()
        while not self.built_ready.wait(STOP_POLL_S):
            if self.stop.is_set():
                raise Stopped
        if self.stop.is_set():
            raise Stopped
        waited = time.monotonic() - began
        if waited >= STOP_POLL_S:
            self.say(f"loop land: {configuration.name}: the run waited {waited:.0f} s for the write of the built files")

    def build_all(self, configuration: Configuration, share: heavy.Share) -> str | None:
        """Build the target all of a configuration of objects, and give a problem (the built files).

        The build gets no -j, because ninja obeys the jobserver only with no -j (the jobserver).  It writes its output
        to the log of `loop test`, and a build error gives the problem of a build error of `loop test`.  A run of the
        base writes no built file, so its build error then gives the same problem (the run of the base).

        Raises:
            GateError: If the CMake cache of the build names no cmake
        """
        build = build_directory(self.gate.tree, configuration)
        log = log_name(configuration, "test.log")
        command = [configured_program(build, "cmake"), "--build", str(build)]
        if self.run_logged(command, log, share, proc.BUILD_S) != 0:
            return f"{configuration.name}: {BUILD_PROBLEM}, see {self.gate.logs / log}"
        return None

    def written_built(self, configuration: Configuration, share: heavy.Share) -> list[str]:
        """Build the target all of ALWAYS, write each built file of the candidate, and fold each one that changed.

        The built files give the steps.  The result holds a problem for a configure or a build that gives an error, or
        for each write that gives an error.  After a fold, `candidate` holds the new commit, and `built` holds each
        written file that changed.

        Raises:
            GateError: If the CMake cache of the build names no cmake
            git.GitError: If a git command of the fold fails
        """
        began = time.monotonic()
        problem = self.configure(configuration, share) or self.build_all(configuration, share)
        if problem is not None:
            return [problem]
        built_s = time.monotonic() - began
        build = build_directory(self.gate.tree, configuration)
        problems: list[str] = []
        for path in self.candidate.built:
            target = BUILT_FILES[path].target
            log = log_name(configuration, f"write-{target}.log")
            command = [*self.gate.master("loop"), "derive", target, "--write", "--build-dir", str(build)]
            if self.run_logged(command, log, share, proc.TOOL_S) != 0:
                problems.append(
                    f"{configuration.name}: the gate cannot write {path} from the build, see {self.gate.logs / log}"
                )
        if not problems:
            self.built = self.fold_built(built_s, time.monotonic() - began - built_s)
        return problems

    def fold_built(self, built_s: float, write_s: float) -> tuple[str, ...]:
        """Fold each built file that the write changed in the warm tree into a new commit, and give these files.

        The warm tree and `candidate` move to the new commit (built_commit()).  With no changed file, the commit stays.
        `built_s` is the time of the configure and the build, and `write_s` the time of the writes and the fold, for the
        line of the gate.

        Raises:
            GateError: If the warm tree is in the work tree of the repository
            git.GitError: If a git command fails
        """
        began = time.monotonic()

        def times() -> str:
            """Return the words that give the time of the build, and of the writes and the fold until now."""
            return f"the build took {built_s:.0f} s, and the write took {write_s + time.monotonic() - began:.0f} s"

        tree = self.gate.tree
        written = tuple(sorted(git.work_changes(tree, "HEAD", list(self.candidate.built))))
        if not written:
            self.say(
                f"loop land: the build of {ALWAYS} gives the bytes of the commit for {' '.join(self.candidate.built)}, "
                f"so the gate folds nothing: {times()}"
            )
            return ()
        files = {name: (tree / name).read_bytes() if (tree / name).is_file() else None for name in written}
        commit = built_commit(self.gate.root, self.candidate.commit, files)
        move_tree(self.gate.root, tree, commit)
        self.candidate = replace(self.candidate, commit=commit)
        self.say(
            f"loop land: the gate wrote {' '.join(written)} from the build of {ALWAYS}, folded into the commit "
            f"{commit[:9]}: {times()}"
        )
        if self.candidate.on_fold is not None:
            self.candidate.on_fold(commit)
        return written

    def checked(
        self,
        configuration: Configuration,
        rules: Sequence[str],
        share: heavy.Share,
        is_again: bool = False,
        targets: Sequence[str] = (),
    ) -> list[str]:
        """Run `guard check --build-dir` of each rule on a build at the same time, and give a problem for each exit other
        than 0 or 77.

        With targets, each rule reads the objects of the targets in place of all (`guard check --target`), and the
        exit 77 is a problem too: the build made the targets, so a rule that does not apply reads no object of them.
        """
        build = build_directory(self.gate.tree, configuration)
        target_words = [word for target in targets for word in ("--target", target)]
        passed = (ExitCode.OK,) if targets else (ExitCode.OK, ExitCode.SKIPPED)

        def check_one(rule: str) -> str | None:
            """Run the check of one rule, and give its problem or None."""
            log = log_name(configuration, f"check-{rule}.log", is_again)
            command = [*self.gate.master("guard"), "check", "--rule", rule, "--build-dir", str(build), *target_words]
            status = self.run_logged(command, log, share, proc.TOOL_S)
            if status in passed:
                return None
            if status == ExitCode.SKIPPED:
                return (
                    f"{configuration.name}: the rule {rule} reads no object of {' and '.join(targets)}, so the gate "
                    f"cannot check the build, see {self.gate.logs / log}"
                )
            return f"{configuration.name}: the rule {rule} gives findings, see {self.gate.logs / log}"

        return [problem for problem in self.concurrently(list(rules), check_one) if problem is not None]

    def reduce(self, counted: Sequence[str], vessel: Configuration | None = None) -> None:
        """Run the reduce of each counted rule over the builds of the configurations, at the same time (the reduce).

        Each rule has one write.  A write that refuses a count exits 1, and it writes no ledger.  The writes share one
        lease (the jobserver).  `vessel` is the vessel configuration when the round built the vessel with no problem
        (reduce_words()).  Complexity: one write of each counted rule.
        """
        if not counted:
            return
        with self.leased() as share:

            def write_one(rule: str) -> None:
                """Write the ledger of one rule over the build of each configuration that checks it."""
                log = f"reduce-{rule}.log"
                command = [*self.gate.master("guard"), *reduce_words(self.gate.tree, rule, vessel)]
                status = self.run_logged(command, log, share, proc.TOOL_S)
                self.say(f"loop land: the reduce of {rule} exits {status}")

            self.concurrently(list(counted), write_one)

    def fold(self) -> tuple[str, tuple[str, ...]]:
        """Fold each ledger that the reduce changed in the warm tree into the commit, and give the commit and the ledgers.

        The warm tree moves to the new commit.  With no changed ledger, the result is the commit and no ledger.

        Raises:
            GateError: If the warm tree is in the work tree of the repository
            git.GitError: If a git command fails
        """
        tree = self.gate.tree
        ledgers = tuple(sorted(git.work_changes(tree, "HEAD", [LEDGER_PREFIX])))
        if not ledgers:
            return self.candidate.commit, ()
        files = {name: (tree / name).read_bytes() if (tree / name).is_file() else None for name in ledgers}
        final = git.replaced_commit(self.gate.root, self.candidate.commit, files)
        move_tree(self.gate.root, tree, final)
        return final, ledgers

    def raise_budgets(self, outcomes: Sequence[Outcome]) -> tuple[str, ...]:
        """Write the crossed rows of each budget of a round with the budget reason, and give a line for each new row.

        the raise of a budget gives the rules.  Each rule gets one write on the build of each configuration whose test
        of the rule is red alone too.  The writes of one rule run one after the other, and the rules run at the same
        time.  A write that fails, or writes that change no row, give each ledger its bytes back and no line, so the
        problems of the round stay, and the verdict also gives the cause (Step.raise_problems).  Complexity: one write
        for each rule and each such configuration.
        """
        plan: dict[str, list[Configuration]] = {}
        for item in outcomes:
            configuration = BY_NAME.get(item.name)
            if not item.problems or configuration is None:
                continue
            for name in item.red:
                plan.setdefault(BUDGET_TESTS[name], []).append(configuration)
        if not plan:
            return ()
        tree = self.gate.tree
        paths = {rule: rule_ledger.shown_path(rule) for rule in plan}
        before = {rule: (tree / path).read_bytes() if (tree / path).is_file() else None for rule, path in paths.items()}
        self.say(f"loop land: the gate raises the crossed rows of {' '.join(sorted(plan))} with the budget reason")
        reason = self.candidate.budget_reason or self.candidate.budget_rows[0][1]
        with self.leased() as share:

            def write_rule(rule: str) -> str:
                """Write the rows of one rule from the build of each of its configurations, and give a failure, or ""."""
                for configuration in plan[rule]:
                    build = build_directory(tree, configuration)
                    log = log_name(configuration, f"raise-{rule}.log")
                    command = [*self.gate.master("guard"), "write", "--rule", rule, "--build-dir", str(build)]
                    command += ["--reason", reason]
                    status = self.run_logged(command, log, share, proc.TOOL_S)
                    if status != ExitCode.OK:
                        shown = self.gate.logs / log
                        return f"{configuration.name}: the raise of {rule} exits {status}, see {shown}"
                return ""

            failures = tuple(line for line in self.concurrently(sorted(plan), write_rule) if line)
            for line in failures:
                self.say(f"loop land: {line}")
            self.raise_problems = failures
            is_written = not failures and self.written_row_texts(paths, before, share)
        lines: list[str] = []
        for rule, path in sorted(paths.items()):
            after = (tree / path).read_bytes() if (tree / path).is_file() else None
            lines += raised_lines(path, (before[rule] or b"").decode("utf-8"), (after or b"").decode("utf-8"))
        if is_written and lines:
            return tuple(lines)
        for rule, path in paths.items():
            kept = before[rule]
            if kept is None:
                (tree / path).unlink(missing_ok=True)
            else:
                (tree / path).write_bytes(kept)
        nothing = (
            f"the raise of {' '.join(sorted(plan))} writes no row, so the crossed rows of the budgets stay problems"
        )
        self.say(f"loop land: {nothing}")
        if is_written:
            self.raise_problems = (nothing,)
        return ()

    def written_row_texts(
        self, paths: Mapping[str, str], before: Mapping[str, bytes | None], share: heavy.Share
    ) -> bool:
        """Give each crossed row that an item of the candidate names its own text, and tell if each crossed row has one.

        the raise of a budget gives the rules.  The write of a rule gives each crossed row the plain text, or the first
        own text when the candidate gives no plain text.  Then `guard write --rule RULE --row ITEM [--kind KIND]
        --reason TEXT` gives each crossed row of an item its own text.  A crossed row with no text, an item that names
        no crossed row, and a write that fails each give a line, and the function then gives False.  Complexity:
        linear in the rows of the ledgers times the items.
        """
        named = dict(self.candidate.budget_rows)
        if not named:
            return True
        tree = self.gate.tree
        problems: list[str] = []
        used: set[str] = set()
        writes: dict[tuple[str, str, str], list[str]] = {}
        for rule, path in sorted(paths.items()):
            after = (tree / path).read_text(encoding="utf-8") if (tree / path).is_file() else ""
            old, new = ledger_values((before[rule] or b"").decode("utf-8")), ledger_values(after)
            for key in sorted(key for key, value in new.items() if old.get(key) != value):
                item = next((name for name in named if is_named_row(name, key)), "")
                if not item and not self.candidate.budget_reason:
                    problems.append(
                        f"the crossed row {shown_key(key)} of {path} gets no text: give --budget-reason "
                        f"{key[0] if key[0] != '-' else key[2]}=TEXT, or a plain --budget-reason TEXT for each such row"
                    )
                if item:
                    used.add(item)
                    target = key[0] if key[0] != "-" else key[2]
                    writes.setdefault((rule, key[1], named[item]), []).append(target)
        problems += [
            f"--budget-reason {item}=TEXT names no crossed row of a budget, so its text goes into no row: give the "
            f"path or the entity of a crossed row"
            for item in named
            if item not in used
        ]
        for line in problems:
            self.say(f"loop land: {line}")
        if problems:
            self.raise_problems = tuple(problems)
            return False
        for index, ((rule, kind, text), targets) in enumerate(sorted(writes.items())):
            command = [*self.gate.master("guard"), "write", "--rule", rule, "--reason", text]
            command += [word for target in targets for word in ("--row", target)]
            command += ["--kind", kind] if kind != "-" else []
            log = f"raise-reason-{rule}-{index}.log"
            status = self.run_logged(command, log, share, proc.TOOL_S)
            if status != ExitCode.OK:
                line = f"the text of the named rows of {rule} exits {status}, see {self.gate.logs / log}"
                self.say(f"loop land: {line}")
                self.raise_problems = (line,)
                return False
        return True

    def lower_budgets(self) -> tuple[str, ...]:
        """Lower each row that the candidate names to the largest measure of the builds of the round, and give the lines.

        The lower of a budget gives the rules.  Each rule gets one write for each reason, on the build of each
        configuration of objects.  The writes of one rule run one after the other, and the rules run at the same time.
        A rule whose write fails gets its ledger bytes back and one line that names the log.  Complexity: one write for
        each rule and each reason.
        """
        groups: dict[tuple[str, str], list[str]] = {}
        for rule, item, reason in self.candidate.lowers:
            groups.setdefault((rule, reason), []).append(item)
        tree = self.gate.tree
        builds = [str(build_directory(tree, item)) for item in CONFIGURATIONS if not item.is_parse]
        rules = sorted({rule for rule, _ in groups})
        paths = {rule: rule_ledger.shown_path(rule) for rule in rules}
        before = {rule: (tree / path).read_bytes() if (tree / path).is_file() else None for rule, path in paths.items()}
        self.say(f"loop land: the gate lowers the named rows of {' '.join(rules)} to the largest measure of its builds")
        with self.leased() as share:

            def write_rule(rule: str) -> str:
                """Write the lowers of one rule, and give the line of a write that fails, or ""."""
                writes = [(reason, items) for (name, reason), items in sorted(groups.items()) if name == rule]
                for index, (reason, items) in enumerate(writes):
                    log = f"lower-{rule}-{index}.log"
                    command = [*self.gate.master("guard"), "write", "--rule", rule, "--lower", "--reason", reason]
                    command += [word for item in items for word in ("--row", item)]
                    command += [word for build in builds for word in ("--build-dir", build)]
                    status = self.run_logged(command, log, share, proc.TOOL_S)
                    if status != ExitCode.OK:
                        return f"the lower of {rule} exits {status} and writes no row, see {self.gate.logs / log}"
                return ""

            failures = dict(zip(rules, self.concurrently(rules, write_rule), strict=True))
        lines: list[str] = []
        for rule, path in paths.items():
            if failures[rule]:
                kept = before[rule]
                if kept is None:
                    (tree / path).unlink(missing_ok=True)
                else:
                    (tree / path).write_bytes(kept)
                lines.append(failures[rule])
                continue
            after = (tree / path).read_bytes() if (tree / path).is_file() else None
            lines += raised_lines(path, (before[rule] or b"").decode("utf-8"), (after or b"").decode("utf-8"))
        return tuple(lines)

    def ratchet_rows(self, outcomes: Sequence[Outcome], counted: Sequence[str]) -> tuple[str, ...]:
        """Return the lines of the ratchet rows of the red configurations of objects of a round.

        The writes of the rules run at the same time in one lease, and the writes of one rule run one after the other,
        because each one writes the ledger of the rule.  The caller gives the step a stop event of its own.
        Complexity: one write for each counted rule and each configuration whose test of the rule is red alone too.

        Raises:
            GateError: If the registry of the tree does not read, or the CMake cache of a build names no cmake
        """
        tests = ratchet_tests(self.gate.tree, counted)
        plan: dict[str, tuple[rule_registry.Rule, list[Configuration]]] = {}
        for item in outcomes:
            configuration = BY_NAME.get(item.name)
            if item.is_stopped or configuration is None or configuration.is_parse:
                continue
            for name in item.red:
                for rule in tests.get(name, ()):
                    plan.setdefault(rule.id, (rule, []))[1].append(configuration)
        if not plan:
            return ()
        self.say(f"loop land: the gate gives the rows of {' '.join(sorted(plan))} from the build of each red run")
        with self.leased() as share:

            def rows_of(rule_id: str) -> list[str]:
                """Return the lines of the rows of one rule from the build of each of its configurations."""
                rule, configurations = plan[rule_id]
                return [line for item in configurations for line in self.rule_rows(rule, item, share)]

            found = self.concurrently(sorted(plan), rows_of)
        return tuple(line for lines in found for line in lines)

    def rule_rows(self, rule: rule_registry.Rule, configuration: Configuration, share: heavy.Share) -> list[str]:
        """Write the ledger of a rule from the build of a configuration in the mode admit, and give its new rows.

        the ratchet rows gives the rules.  The ledger of the warm tree gets its bytes back after the write.
        Complexity: one write, and linear in the lines of the ledger.
        """
        from assay.guard.engines.meta import runner as meta_runner

        path = rule_ledger.shown_path(rule.id)
        ledger = self.gate.tree / path
        before = ledger.read_bytes() if ledger.is_file() else None
        build = build_directory(self.gate.tree, configuration)
        log = log_name(configuration, f"rows-{rule.id}.log")
        command = [*self.gate.master("guard"), "write", "--rule", rule.id, "--build-dir", str(build)]
        command += ["--admit", "--reason", ROW_REASON]
        try:
            status = self.run_logged(command, log, share, proc.TOOL_S)
            after = ledger.read_bytes() if ledger.is_file() else None
        finally:
            if before is None:
                ledger.unlink(missing_ok=True)
            else:
                ledger.write_bytes(before)
        name = configuration.name
        if status == ExitCode.SKIPPED:
            return []
        if status != ExitCode.OK:
            return [
                f"loop land: {name}: the gate cannot give the rows of {path} from the build, see {self.gate.logs / log}"
            ]
        held = set((before or b"").decode("utf-8").splitlines())
        added = [
            line
            for line in (after or b"").decode("utf-8").splitlines()
            if line not in held and not line.startswith("#")
        ]
        try:
            refused = meta_runner.refused_kinds(rule)
        except ImportError:
            # The code of the gate has no module of a rule that the commit adds, and the check of that rule judges it.
            refused = frozenset()
        kept = [line for line in added if row_kind(line) not in refused]
        lines = [
            f"loop land: {name}: no row of {path} admits a finding of the kind {row_kind(line)}, so correct the code: "
            f"{line}"
            for line in added
            if row_kind(line) in refused
        ]
        if not kept:
            return lines
        head = (
            f"loop land: {name}: the build gives these rows of {path}.  Put each row in the ledger of the change, and "
            f"write the reason of a new row in place of {ROW_REASON}.  The rows come from one build, and a different "
            f"configuration can give a higher count:"
        )
        return [head, *(f"  {line}" for line in kept), *lines]

    def red_alone(
        self, configuration: Configuration, names: Sequence[str], share: heavy.Share, is_again: bool = False
    ) -> list[str]:
        """Run each red test one more time apart from the suite, and return the names of the tests that are red again.

        The tests run at the same time, in one run of ctest with the jobs of the lease (the run of a configuration).
        The report of that run gives the status of each test.  A test with no status in the report is red.  Each test
        has the wall guard of the hang guard.  The limit of the run is that guard for each test, the time of the tests
        one after the other, and ALONE_ROOM_S for ctest to start, stop a test at its guard and write the report.  The
        files of the run have the name of its round (log_name()).

        Raises:
            GateError: If the CMake cache of the build names no ctest
        """
        if not names:
            return []
        build = build_directory(self.gate.tree, configuration)
        logs = self.gate.logs
        selection = logs / log_name(configuration, "alone.txt", is_again)
        report = logs / log_name(configuration, "alone.junit.xml", is_again)
        ctest = configured_program(build, "ctest")
        logs.mkdir(parents=True, exist_ok=True)
        selection.write_text("".join(f"{name}\n" for name in names), encoding="utf-8")
        report.unlink(missing_ok=True)
        command = [ctest, "--test-dir", str(build), "--tests-from-file", str(selection), "-j", str(share.jobs)]
        command += [*self.gate.guard_words, "--output-on-failure", "--output-junit", str(report)]
        command += ["--test-output-size-failed", str(ALONE_OUTPUT_BYTES)]
        limit_s = self.gate.test_timeout_s * len(names) + ALONE_ROOM_S
        self.run_logged(command, log_name(configuration, "alone.log", is_again), share, limit_s)
        try:
            statuses = ctest_reader.results(report)
        except ctest_reader.CtestError:
            statuses = {}
        return [name for name in names if statuses.get(name, ctest_reader.TestStatus.NOTRUN) in RED]

    def run_parse(self, configuration: Configuration, share: heavy.Share) -> list[str]:
        """Do steps 1 and 2 of the run of a configuration for a parse configuration, and give each problem."""
        problem = self.configure(configuration, share)
        if problem is not None:
            return [problem]
        build = build_directory(self.gate.tree, configuration)
        log = log_name(configuration, "syntax.log")
        command = [*self.gate.master("loop"), "syntax", str(build), "-j", str(share.jobs)]
        if self.run_logged(command, log, share, TEST_RUN_S) != 0:
            return [f"{configuration.name}: a unit does not parse, see {self.gate.logs / log}"]
        self.wait_built(configuration)
        return self.checked(configuration, COUNTED_RULES, share)

    def run_objects(
        self, configuration: Configuration, words: Sequence[str], share: heavy.Share, is_again: bool
    ) -> tuple[list[str], tuple[str, ...], tuple[str, ...]]:
        """Do steps 1 and 3 of the run of a configuration for a configuration of objects, and give each problem.

        `words` are the words of the mode after `loop test BUILD` (Gate.loop_words()).  A run again after the fold does
        not run RECONFIGURE_RULE.  The run of ALWAYS first builds the target build.ninja when the other configurations
        wait for it (the threads).  Then each run settles the built files (settle_built()).  The second result holds the
        tests that are red alone too, and the third result the flaky tests: red in the suite and green alone.
        """
        problem = self.configure(configuration, share, is_again)
        if problem is not None:
            return [problem], (), ()
        if configuration.name == ALWAYS and not self.always_ready.is_set():
            problem = self.regenerate(configuration, share)
            self.always_ready.set()
            if problem is not None:
                return [problem], (), ()
        if not self.built_ready.is_set():
            problems = self.settle_built(configuration, share)
            if problems:
                return problems, (), ()
        build = build_directory(self.gate.tree, configuration)
        report = build / affected.STATE_DIRECTORY / "junit.xml"
        report.unlink(missing_ok=True)
        logs = self.gate.logs
        log = log_name(configuration, "test.log", is_again)
        command = [*self.gate.master("loop"), "test", str(build), "-j", str(share.jobs), *words]
        command += ["--", *self.gate.guard_words]
        status = self.run_logged(command, log, share, TEST_RUN_S)
        statuses: dict[str, ctest_reader.TestStatus] = {}
        if report.is_file():
            try:
                statuses = ctest_reader.results(report)
            except ctest_reader.CtestError as problem_text:
                return [f"{configuration.name}: the test report does not read: {problem_text}"], (), ()
        elif status != 0:
            return [f"{configuration.name}: {BUILD_PROBLEM}, see {logs / log}"], (), ()
        problems, still, flaky = self.judge(
            configuration, report, statuses, status, log, "`loop test`", share, is_again
        )
        if problems:
            return problems, still, flaky
        warnings_log = log_name(configuration, "warnings.log", is_again)
        warnings = [*self.gate.master("guard"), "warnings", str(build)]
        if self.run_logged(warnings, warnings_log, share, proc.TOOL_S):
            return [f"{configuration.name}: `guard warnings` gives an error, see {logs / warnings_log}"], (), flaky
        if is_again and configuration.name not in self.unchecked:
            return [], (), flaky
        return self.checked(configuration, (RECONFIGURE_RULE,), share), (), flaky

    def judge(
        self,
        configuration: Configuration,
        report: Path,
        statuses: Mapping[str, ctest_reader.TestStatus],
        status: int,
        log: str,
        program: str,
        share: heavy.Share,
        is_again: bool,
    ) -> tuple[list[str], tuple[str, ...], tuple[str, ...]]:
        """Judge the statuses of a run of tests, and give each problem, the tests red alone too, and the flaky tests.

        Each red test runs one more time apart from the suite (the run of a configuration).  `report` is the JUnit
        report of the run, `status` its exit, `log` the name of its log, and `program` the words of its command in a
        problem.  Complexity: linear in the statuses, and one run of ctest for the red tests.

        Raises:
            GateError: If the CMake cache of the build names no ctest
        """
        logs = self.gate.logs
        red = sorted(name for name, test_status in statuses.items() if test_status in RED)
        if red:
            shutil.copyfile(report, logs / log_name(configuration, SUITE_REPORT, is_again))
        if status != 0 and not red:
            return [f"{configuration.name}: {program} exits {status} with no red test, see {logs / log}"], (), ()
        skipped = sorted(name for name, test_status in statuses.items() if test_status is SKIPPED)
        if skipped:
            self.say(f"loop land: {configuration.name}: {len(skipped)} skipped: {' '.join(skipped)}")
        if red:
            self.say(f"loop land: {configuration.name}: {len(red)} red, they run again together apart from the suite")
        still = self.red_alone(configuration, red, share, is_again)
        flaky = tuple(name for name in red if name not in still)
        if flaky:
            self.say(
                f"loop land: {configuration.name}: FLAKY, red in the suite and green alone: {' '.join(flaky)}, "
                f"output in {logs / log_name(configuration, SUITE_REPORT, is_again)}"
            )
        if still:
            return [f"{configuration.name}: red alone too: {' '.join(still)}, see {logs / log}"], tuple(still), flaky
        return [], (), flaky

    def run_vessel(
        self, run: Run, share: heavy.Share, is_again: bool
    ) -> tuple[list[str], tuple[str, ...], tuple[str, ...]]:
        """Build the vessel targets and run the vessel tests, and give each problem, the tests red alone too, and the
        flaky tests (the vessel).

        A run with names runs only the tests that it names, as a run of the base does.  A run in which no test runs,
        and a disabled test, give a problem, so the gate does not pass a vessel that it did not test.

        Raises:
            GateError: If the CMake cache of the build names no cmake or no ctest
            git.GitError: If the main work tree of the repository cannot be found
        """
        configuration = run.configuration
        problem = link_torch(self.gate.root, self.gate.tree)
        if problem is None:
            problem = self.configure(configuration, share, is_again)
        if problem is not None:
            return [problem], (), ()
        build = build_directory(self.gate.tree, configuration)
        logs = self.gate.logs
        log = log_name(configuration, "build.log", is_again)
        command = [configured_program(build, "cmake"), "--build", str(build), "--target", *VESSEL_TARGETS]
        if self.run_logged(command, log, share, TEST_RUN_S) != 0:
            targets = " and ".join(VESSEL_TARGETS)
            return [f"{configuration.name}: the build of {targets} gives an error, see {logs / log}"], (), ()
        checked = self.checked(configuration, vessel_rules(self.gate.tree), share, is_again, VESSEL_TARGETS)
        if checked:
            return checked, (), ()
        if is_read_by_vessel(self.candidate.built):
            self.wait_built(configuration)
        share.lease.set_gb_per_job(gb_per_job_of(VESSEL, TEST_KINDS))
        report = logs / log_name(configuration, "test.junit.xml", is_again)
        report.unlink(missing_ok=True)
        words = ["--label-regex", f"^{VESSEL_LABEL}$"]
        if run.names:
            selection = logs / log_name(configuration, "only.txt", is_again)
            selection.write_text("".join(f"{name}\n" for name in run.names), encoding="utf-8")
            words = ["--tests-from-file", str(selection)]
        log = log_name(configuration, "test.log", is_again)
        command = [configured_program(build, "ctest"), "--test-dir", str(build), *words, "-j", str(share.jobs)]
        command += [*self.gate.guard_words, "--output-on-failure", "--output-junit", str(report)]
        status = self.run_logged(command, log, share, TEST_RUN_S)
        if not report.is_file():
            return [f"{configuration.name}: ctest exits {status} and writes no report, see {logs / log}"], (), ()
        try:
            statuses = ctest_reader.results(report)
        except ctest_reader.CtestError as problem_text:
            return [f"{configuration.name}: the test report does not read: {problem_text}"], (), ()
        disabled = sorted(name for name, test_status in statuses.items() if test_status is DISABLED)
        if disabled:
            shown = " ".join(disabled)
            return [f"{configuration.name}: the build disables the tests {shown}, see {logs / log}"], (), ()
        if not any(test_status is RAN or test_status in RED for test_status in statuses.values()):
            return [f"{configuration.name}: no test with the label {VESSEL_LABEL} runs, see {logs / log}"], (), ()
        return self.judge(configuration, report, statuses, status, log, "ctest", share, is_again)

    def run_of(
        self, run: Run, share: heavy.Share, is_again: bool, is_retry: bool
    ) -> tuple[list[str], tuple[str, ...], tuple[str, ...]]:
        """Run one configuration, or its tests again after the reduce, and give each problem, the tests red alone too,
        and the flaky tests."""
        configuration = run.configuration
        if run.mode is Mode.VESSEL:
            return self.run_vessel(run, share, is_again)
        if configuration.is_parse and is_again:
            return self.checked(configuration, COUNTED_RULES, share, is_again), (), ()
        if configuration.is_parse:
            return self.run_parse(configuration, share), (), ()
        words = self.gate.loop_words(run, is_retry or is_again, is_again)
        return self.run_objects(configuration, words, share, is_again)

    def log_kill(self, name: str, problem: str, gb_per_job: float) -> None:
        """Append one line of a kill for lack of memory to KILL_LOG of the work directory (the kill for memory).

        The line holds the time, the logs of the run, the configuration, the memory of a job of its lease, and the first
        problem, which names the red test or the log of the build.  One write of a short line with the mode a keeps the
        lines of the threads apart.
        """
        line = {
            "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "logs": str(self.gate.logs),
            "configuration": name,
            "gb_per_job": gb_per_job,
            "problem": problem,
        }
        with open(self.gate.work / KILL_LOG, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(line) + "\n")

    def outcome_of(self, run: Run, is_again: bool, is_retry: bool) -> Outcome:
        """Run one configuration in the thread of the caller, with a lease of its own, and give its outcome.

        the threads, the stop and the jobserver give the rules.  The time of the outcome starts after the wait for the
        first slot of the lease.  A problem sets the stop event, other than in a run of the base (the run of the
        base), and other than a crossed row of a budget in the first round of a candidate with a budget reason (the
        raise of a budget).  An error of the run sets it too, and goes to the caller.
        """
        name = run.configuration.name
        self.say(f"loop land: {name}: {'test again' if is_again else run.mode.value}")
        began = time.monotonic()
        wait_s = 0.0
        lease: heavy.Lease | None = None
        is_vessel = run.mode is Mode.VESSEL
        gb_per_job = gb_per_job_of(VESSEL, BUILD_KINDS) if is_vessel else gb_per_job_of(name)
        try:
            weight = VESSEL_WEIGHT if is_vessel else 1
            with self.leased(gb_per_job, is_capped=is_vessel, weight=weight) as share:
                wait_s, lease = round(share.wait_s, 1), share.lease
                began += share.wait_s
                kills = self.gate.oom_kills()
                problems, red, flaky = self.run_of(run, share, is_again, is_retry)
                if problems and not self.stop.is_set() and self.gate.oom_kills() > kills:
                    self.say(
                        f"loop land: {name}: the kernel ended a process for lack of memory during the run, so the run "
                        f"runs one time again: {problems[0]}"
                    )
                    self.log_kill(name, problems[0], share.lease.gb_per_job)
                    problems, red, flaky = self.run_of(run, share, is_again, True)
        except Stopped:
            seconds = round(time.monotonic() - began, 1)
            self.say(f"loop land: {name}: the stop ends the run after {seconds:.0f} s")
            return Outcome(name, (), (), seconds, True, wait_s=wait_s)
        except BaseException:
            self.stop.set()
            raise
        finally:
            if name == ALWAYS:
                self.always_ready.set()
                self.built_ready.set()
        seconds = round(time.monotonic() - began, 1)
        slot_seconds = lease.slot_seconds if lease is not None else 0.0
        self.say(f"loop land: {name}: {seconds:.0f} s after a wait of {wait_s:.0f} s for its first slot")
        items = self.red_items(run, red, is_again) if red else ()
        outcome = Outcome(name, tuple(problems), red, seconds, False, flaky, wait_s, slot_seconds, items)
        is_raisable = self.candidate.has_budget_reason and not is_again and is_budget_outcome(outcome)
        if problems and not self.candidate.focus and not is_raisable:
            self.stop.set()
        return outcome

    def red_items(self, run: Run, red: Sequence[str], is_again: bool) -> tuple[str, ...]:
        """Return the failing items of the tests that are red alone too, from the report of the run alone.

        A report that does not read gives no output, so each test gets the item of an unknown output (the failing
        items).
        """
        report = self.gate.logs / log_name(run.configuration, "alone.junit.xml", is_again)
        try:
            outputs = ctest_reader.outputs(report)
        except ctest_reader.CtestError:
            outputs = {}
        return failing_items(run.configuration.name, red, outputs, self.gate.tree, self.candidate.commit)

    def require_problem(self, outcomes: Sequence[Outcome]) -> None:
        """Refuse a check that the stop ended with no problem of a configuration (the stop).

        Only a problem or an error sets the stop event, and an error goes to the caller.  A set event with no problem
        then tells of an error that did not get to the caller.  The landing then refuses the commit, and it does not
        accept it.

        Raises:
            GateError: If the stop event is set and no outcome has a problem
        """
        if self.stop.is_set() and not any(item.problems for item in outcomes):
            raise GateError("the check of the gate stopped with no problem of a configuration")

    def run_all(
        self, runs: Sequence[Run], others: Callable[[], Sequence[Run]] | None, is_again: bool, is_retry: bool
    ) -> list[Outcome]:
        """Run each configuration at the same time, and the runs that `others` gives, and give each outcome (the threads).

        `others` gives the runs of the other configurations of objects.  The function calls it when the build of ALWAYS
        holds the configure of the change.  The outcomes are in the sequence of `runs`, and then of the runs of
        `others`.
        """
        later: list[Outcome] = []

        def run_others() -> None:
            """Wait for the build of ALWAYS, then run the other configurations at the same time."""
            while not self.always_ready.wait(STOP_POLL_S):
                if self.stop.is_set():
                    return
            if self.stop.is_set() or others is None:
                return
            later.extend(self.concurrently(others(), lambda run: self.outcome_of(run, is_again, is_retry)))

        tasks: list[Callable[[], Outcome | None]] = [
            functools.partial(self.outcome_of, run, is_again, is_retry) for run in runs
        ]
        if others is not None:
            tasks.append(run_others)
        results = self.concurrently(tasks, lambda task: task())
        return [*(item for item in results if item is not None), *later]


def make_run(logs: Path, runs: Sequence[Path]) -> str:
    """Make the directory of a new run in `logs`, one above the highest number of `runs`, and return its name (the logs).

    A directory that a landing of an earlier copy of the module made first moves the number up by one.
    """
    number = max((int(path.name.removeprefix(RUN_MARK)) for path in runs), default=0) + 1
    while True:
        name = f"{RUN_MARK}{number:0{RUN_WIDTH}d}"
        try:
            (logs / name).mkdir()
            return name
        except FileExistsError:
            number += 1


def run_bytes(run: Path) -> int:
    """Return the bytes of the files of the directory of a run.  A file that goes during the walk counts as 0 bytes.

    Complexity: one stat of each file of the run.
    """
    total = 0
    for directory, _, files in os.walk(run):
        for name in files:
            with contextlib.suppress(OSError):
                total += os.lstat(os.path.join(directory, name)).st_size
    return total


def removed_runs(ended: Sequence[Path], cited: Collection[str], max_age_s: float, max_bytes: int) -> list[Path]:
    """Return the ended runs that the sweep removes, from the oldest (the logs).

    `ended` gives the ended runs from the oldest.  The last RUNS_KEPT of them stay.  Of the older runs, a run that a
    verdict cites stays while it is younger than `max_age_s`, and while the cited runs that stay hold `max_bytes` or
    less together.  Over the bytes, the oldest cited runs go first.  Complexity: linear in the runs, with one walk of
    each cited run that is younger than the bound.
    """
    older = ended[: max(0, len(ended) - RUNS_KEPT)]
    now = time.time()
    removed: list[Path] = []
    kept: list[tuple[Path, int]] = []
    for path in older:
        if path.name not in cited:
            removed.append(path)
            continue
        try:
            age = now - path.stat().st_mtime
        except OSError:
            continue
        if age > max_age_s:
            removed.append(path)
        else:
            kept.append((path, run_bytes(path)))
    total = sum(size for _, size in kept)
    for path, size in kept:
        if total <= max_bytes:
            break
        removed.append(path)
        total -= size
    return removed


@contextlib.contextmanager
def hold_run(
    work: Path,
    cited: Collection[str] = frozenset(),
    max_age_s: float = CITED_RUN_AGE_S,
    max_bytes: int = CITED_RUNS_BYTES,
) -> Iterator[str]:
    """Make the directory of the logs of a new run, hold it live for the block, and give its name (the logs).

    The block holds an exclusive lock of flock(2) on the file LIVE of the directory.  The function also removes each
    ended run, other than the last RUNS_KEPT ended runs and the runs of `cited` in the bounds of removed_runs().  It
    holds the lock of RUNS_LOCK while it makes the run and removes the ended runs.  Thus no landing sees a run before
    its lock.  Complexity: linear in the directories of the runs.
    """
    logs = work / LOGS
    logs.mkdir(parents=True, exist_ok=True)
    with contextlib.ExitStack() as held:
        with open(work / RUNS_LOCK, "a", encoding="ascii") as runs_lock:
            fcntl.flock(runs_lock, fcntl.LOCK_EX)
            runs = [path for path in paths.listing(logs, f"{RUN_MARK}*") if path.name.removeprefix(RUN_MARK).isdigit()]
            ended = [path for path in runs if not heavy.is_held(path / LIVE)]
            name = make_run(logs, runs)
            fcntl.flock(held.enter_context(open(logs / name / LIVE, "a", encoding="ascii")), fcntl.LOCK_EX)
            for old in removed_runs(ended, cited, max_age_s, max_bytes):
                shutil.rmtree(old, ignore_errors=True)
        yield name


def tree_name(lane: int) -> str:
    """Return the name of the warm tree of a lane in the work directory: TREE, then TREE-2 and TREE-3 (the lanes)."""
    return TREE if lane == 0 else f"{TREE}-{lane + 1}"


def lock_name(lane: int) -> str:
    """Return the name of the lock file of the warm tree of a lane in the work directory (the lanes)."""
    return LOCK if lane == 0 else f"{tree_name(lane)}.lock"


@contextlib.contextmanager
def tree_lock(work: Path, wait: bool = True, lane: int = 0) -> Iterator[bool]:
    """Hold the lock of the warm tree of a lane for the block, and give True (the lock of the tree).

    With a wait, the block waits while a different landing holds the lock.  With no wait, the block gets False
    immediately when a different landing holds it, and holds nothing.  A holder of the first lane also holds the lock
    of PRIMARY (the lanes).
    """
    work.mkdir(parents=True, exist_ok=True)
    lock = work / lock_name(lane)
    with open(lock, "a", encoding="ascii") as handle, open(work / PRIMARY, "a", encoding="ascii") as primary:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            is_held = True
        except BlockingIOError:
            is_held = False
        if not is_held and wait:
            check_report.write_line(f"loop land: a different landing holds {lock}, and this one waits")
            fcntl.flock(handle, fcntl.LOCK_EX)
            is_held = True
        if is_held and lane == 0:
            fcntl.flock(primary, fcntl.LOCK_EX)
        yield is_held


@contextlib.contextmanager
def free_lane(work: Path, lanes: int = LANES) -> Iterator[int | None]:
    """Hold the lock of the first free lane of `lanes` for the block, and give its number, or None when none is free.

    A lane after the first one is free only while a holder of this copy of the module holds the first lane (the
    lanes).  Complexity: one try of the lock of each lane.
    """
    for lane in range(lanes):
        if lane > 0 and not heavy.is_held(work / PRIMARY):
            break
        with tree_lock(work, wait=False, lane=lane) as is_held:
            if is_held:
                yield lane
                return
    yield None


@contextlib.contextmanager
def session(root: Path, work: Path, jobs: int, is_every: bool, is_v4: bool = False) -> Iterator[Gate]:
    """Hold the lock of the warm tree for the block, and give the gate (the lock of the tree).

    The block waits while a different landing holds the lock.
    """
    with tree_lock(work):
        yield Gate(root, work, jobs, is_every, is_v4=is_v4)
