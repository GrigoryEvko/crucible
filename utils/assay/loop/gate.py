"""gate — build and test the commit of a landing in a warm tree, in each configuration that the change selects.

THE CONFIGURATIONS
    CONFIGURATIONS gives each configuration of the gate: each configure
    preset with the tier gate (assay.loop.tiers), the parse presets first.  A
    configuration has the name of its preset and its build directory in the
    warm tree.  The block of the preset gives the name of the configuration
    of its build (assay.core.configuration).  A parse configuration is a
    preset that sets CRUCIBLE_SYNTAX_ONLY.  These configurations are the one
    list of the configurations that a counted ledger holds (THE REDUCE).
    The gate reads the presets of the tree of the package, and it refuses
    presets with a problem.

THE COUNTED RULES
    A counted rule is a rule of the engine plugin or elf with the policy
    count or admit and a ledger with a row (counted_rules()).  Each one reads
    the objects of the target all of a build, or the reports of a parse
    build.  Its ledger holds one set of rows, and these rows judge each build
    of the rule.  The run of a configuration of objects checks each rule.
    The run of a parse configuration checks only the rules of COUNTED_RULES
    of assay.guard.engines.buildsys.parse_builds (is_checked()).

THE SELECTION
    The gate holds one invariant: a landing cannot make a test red in a
    configuration of the gate when the test can see the change.  Each
    configuration of objects then runs for each change, in one of three
    modes (Mode):
      * The full suite, `loop test BUILD --all`.  select() gives these
        configurations:
          - default for each change
          - release, release-v3 and tsan for a change of a C++ file or of a
            build file.  ThreadSanitizer gives incorrect errors of
            -Wmaybe-uninitialized that the other builds do not give, also
            for a change of a test.  release-v3 compiles for x86-64-v3, so
            each test runs the AVX2 arms at the width of 256 bits.  A
            header of the Simd family and a body in the inline namespace
            CRUCIBLE_SIMD_TARGET are C++ files, so each change of them
            selects release-v3
          - tsan and ubsan-strict for a change of include/, src/ or cmake/,
            or of a build file at the top of the tree
          - bench-fuzz for a change of bench/ or test/fuzz/
          - each configuration for a change of the quarantine plugin or of
            the rule table, and for --all-configurations.  Also for a
            candidate whose composition summed two changes of one count of a
            ledger (assay.loop.land, THE MERGE), so THE REDUCE writes the
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
        again with the change (THE THREADS).  Inputs.is_touched() of
        assay.loop.inputs judges each one (THE TOUCH).  A test that reads no
        build gives the same result in each configuration, and default runs
        it.  When the change touches no such test, these configurations do
        not run.
    A build file is a file CMakeLists.txt, a file *.cmake or
    CMakePresets.json.

    A test of the master guard that reads no build gives the same result in
    each configuration (THE TESTS THAT READ THE BUILD of assay.loop.affected).
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
    configurations of the full suite (THE THREADS).  The model holds no
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
    each configuration writes each count below its row (THE REDUCE).

    A change that no unit of a configuration reads makes its build do
    nothing.  A parse build then keeps the report of each unit (THE KEPT
    REPORTS of assay.loop.syntax).

    The vessel runs too, for a change that can change it (THE VESSEL).

THE VESSEL
    The preset VESSEL builds the vessel against the patched PyTorch.  Its
    tier is nightly, and `loop nightly` builds and tests each target of its
    build.  No configuration of the gate compiles crucible_fallback.cpp and
    the shards of register.cpp of vessel/torch, because they need PyTorch.
    Thus the gate also runs the vessel (Mode.VESSEL), in a thread of its own
    with a lease of its own (THE THREADS).  The vessel is not a
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
         with no -j (THE JOBSERVER).  Then the wait for the write of THE
         BUILT FILES, only when the vessel reads a written file
         (is_read_by_vessel()).  The vessel reads no other file of
         BUILT_FILES, so its tests on the commit give the result of its tests
         on the commit of the fold
      4. `ctest --label-regex` of VESSEL_LABEL, with the jobs of the lease.
         Each red test runs one more time apart from the suite, as in step 3
         of THE RUN OF A CONFIGURATION.  A disabled test, and a run in which
         no test runs, are problems too.  The fixtures with the label
         vessel_neg need no PyTorch, and each configuration of the gate runs
         them.
    The round after THE REDUCE runs these steps again.  The vessel runs no
    counted rule, no rule reconfigure-noop and no `guard warnings`, and THE
    REDUCE reads no build of it.  The ledgers then hold the counts of the
    configurations of the gate only.  The run of the base runs the red tests
    of the vessel, or each vessel test after a problem of a different kind.
    A lease reserves the memory of one job of its configuration for each
    slot: the largest kind of KIND_GB_PER_JOB of the step that holds it
    (gb_per_job_of(), THE MEMORY and THE BUDGET of assay.loop.heavy).  A
    lease of `loop test` builds and tests, so it reserves the largest of
    the three kinds.  The vessel builds with the memory of a shard compile,
    and its lease then reserves the memory of a vessel test for the ctest
    step.  The lease has the weight VESSEL_WEIGHT among the leases of the
    gate (THE WEIGHT of assay.loop.heavy).  The weight stays 1, because
    the shards of four runs at the same time hold much of the memory of the
    host.  The lease is capped: it never grows past the slice of the gate
    times VESSEL_WEIGHT (THE POOL of assay.loop.heavy).

THE CAREFUL SELECTION
    The selection reads files of the commit with the code of the gate, which
    is the code of main (THE DRIVER of assay.loop.land).  A commit can change
    the format of a file and its reader together.  An example is a new kind
    of row of the rule table and the code that reads it.  The code of the
    gate then cannot read that file.  Each read of the selection that gives
    an error then gives its widest result (careful()):
      * each parse configuration
      * the mode of the changed inputs in each other configuration of objects
      * no counted rule, so THE REDUCE lowers no row.
    The gate writes a line with the error.  A wider selection runs each
    configuration and each test that a narrower one runs, so the invariant
    of THE SELECTION holds.  `loop syntax --changed` is the master of the
    warm tree, and an error of it also gives each parse configuration.  The
    commit then gets its verdict from its own tests.

THE WARM TREE
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
    (repair_builds()).  Ninja builds such an output again, and the run
    configures such a build again.  The build directory of each configuration is
    in the warm tree, at the binaryDir of its preset.  The build directories
    stay from one landing to the next.  A guard that reads the git history
    reads the history of the landing.  The gate refuses a warm tree in the
    work tree of the repository.

THE RUN OF A CONFIGURATION
    The HEAD of the warm tree is the commit, with no branch.  The change of the
    tree is then the commit on its first parent (assay.core.git, THE
    CHANGE).  The suite does not get the variable CRUCIBLE_CHANGE_BASE,
    because a self-test plants repositories of its own, and the variable
    names no commit of them.  `loop syntax --changed` gets the variable, with
    the commit that the composition started from.  Each configuration of a
    check runs in a thread of its own, with a lease of the slot pool of the
    host (THE JOBSERVER, THE THREADS):
      1. A build directory with no graph of Ninja gets its configure:
         `cmake --preset PRESET -B BUILD`.  A build directory whose cache
         names a compiler that its toolchain file does not resolve gets
         `cmake --preset PRESET -B BUILD --fresh` (THE COMPILER HEAL).
      2. A parse configuration: `loop syntax BUILD`, then `guard check
         --build-dir BUILD` of each rule of COUNTED_RULES of
         assay.guard.engines.buildsys.parse_builds, at the same time.
      3. A configuration of objects: `loop test BUILD` with the words of its
         mode, which builds the target all and runs the tests of the mode
         (assay.loop.affected, THE SELECTION).  A retry after a move of HEAD
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
         (assay.loop.land, THE FLAKY TESTS).
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
         rule.  The step runs in each mode of THE SELECTION and in THE RUN OF
         THE BASE.  The run again after THE REDUCE does not run it.
    Each step runs the masters of the warm tree.  Then the tests of the
    commit judge the commit.  The step writes its two outputs to a log in
    the directory of the logs of the gate (THE LOGS).  The first
    configuration with a problem stops the others (THE STOP).

THE COMPILER HEAL
    CMake keeps the compiler of the first configure of a build directory,
    and the warm tree keeps its build directories from one landing to the
    next.  So a new default prefix of a toolchain file, such as the prefix
    of a new patch of GCC, never reaches a build directory of the warm tree
    by itself.  Before the build of a configuration with a graph of Ninja,
    the gate runs `loop heal BUILD PRESET` of the warm tree (heal_line()).
    That command compares the compilers of the cache with the compilers
    that the toolchain file of the commit resolves (THE STALE COMPILER of
    assay.loop.heal).  It runs from the warm tree, because the rules of the
    commit decide, and this copy of the gate can be older than the commit.
    When the command gives a line, the gate writes the line, which names the
    paths of the cache and of the toolchain file.  Then it configures the
    build directory again with `cmake --preset PRESET -B BUILD --fresh`
    (configure_plan()).  The commands of the compiles then name the new
    compiler, or `loop heal` removed the log of Ninja for a compiler that
    keeps its path, so Ninja builds each object again.  A cache with no
    record of its compiler heals one time and builds nothing again.  A build
    directory with no graph of Ninja takes step 1 of THE RUN OF A
    CONFIGURATION as it is.
    Outside the gate, cmake/StaleCompiler.cmake refuses such a cache in each
    configure, with the command that heals it.

THE JOBSERVER
    Each configuration of a check holds a lease of the slot pool of the host
    while it runs (THE POOL of assay.loop.heavy).  The writes of THE BUILT
    FILES and of THE REDUCE hold a lease too.  The cap of a lease is the
    slice of the gate: --jobs with SLICE of assay.loop.heavy as the most, or
    less when the available memory is small (THE MEMORY of
    assay.loop.heavy).  Each lease has a jobserver of its own
    (assay.core.jobserver), and each command of the configuration gets the
    variable MAKEFLAGS of that jobserver.  The pump of the lease adds slots
    while the configuration uses each one, and it gives idle slots back to
    the pool.  It grows past the slice only while no different lease waits.
    A configuration in its test phase then cannot take the jobs of a
    different configuration that waits, and the slots of the host go to each
    heavy job of the host.  The leases of the gate go before the other
    leases of the host (THE POOL of assay.loop.heavy).  The wait for the
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

THE THREADS
    Each configuration of the plan runs in a thread of its own, at the same
    time as the others.  The other configurations of objects (the last two
    modes of THE SELECTION) run in threads of their own too.  They start
    when their plan is known.  A change that can change each build gives
    their plan immediately.  Else, their plan reads the touched tests of the
    build of ALWAYS.  The run of ALWAYS then first builds the target
    build.ninja of its build, which configures the build again with the
    change.  Then the other configurations start.  The checks of a parse
    configuration run in threads of their own too.  The writes of THE REDUCE
    also run in threads of their own.

    The verdict gives the configurations, their times and their problems in
    the sequence of the plan and then of the other configurations.  It does
    not give them in the sequence of their ends.

THE STOP
    In the run of a commit, the first configuration with a problem stops
    the others.  A run of the base has no such stop.  Each command
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
    sends SIGINT to the group of each child (AN INTERRUPT of
    assay.core.proc), and ninja and ctest stop their jobs.  SIGTERM ends a
    master with no such step.  Its children run in process groups of their
    own, and they then go on in the warm tree.

THE KILL FOR MEMORY
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

THE REDUCE
    A ledger fails only on a growth (THE POLICIES of assay.core.ledger).  A
    count below its row, and a row of the policy admit that admits no
    finding, then make no check fail.  Only the gate lowers the rows of a
    counted ledger, because one build cannot see the findings of a different
    build (THE WRITE of assay.core.ledger).  The legs of the gate write no
    ledger.

    The gate lowers the rows only when the first round built each
    configuration of CONFIGURATIONS with no problem (is_each_built()).  A
    landing that builds only some configurations then lowers no row, and
    its checks still judge each of its builds.  After each leg of the first
    round ended, the gate runs one `guard write --rule RULE --build-dir
    BUILD...` of each counted rule.  The write gets the build of each
    configuration that checks the rule (is_checked()).  It takes the largest
    count of each key over these builds, and it lowers each row above that
    count (THE REDUCE of assay.core.ledger).  Each mode of THE SELECTION
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
    ledgers.  After a write of THE BUILT FILES, the commit is the commit of
    that write.  A landing of more than one commit gets the ledgers in its
    last commit.

    The warm tree moves to the new commit.  Each configuration of the run
    then runs again, at the same time, each with a lease.  A parse
    configuration runs `guard check --build-dir BUILD` of each rule of
    COUNTED_RULES again, on the reports that it keeps, with no write.  A
    ledger that the gate folded can make such a check red, and the gate then
    rejects the commit.  A configuration of the full suite runs `loop test
    BUILD`, which runs each test with an input that changed, as the run
    after a move of HEAD does.  The other configurations get the mode of THE
    SELECTION again, with the ledgers in the change.

    The verdict gives the new commit and the ledgers, and the landing moves
    the branch to that commit.  A change that lowers counts and lands with
    --all-configurations then lands with no manual write of a ledger.

THE BUILT FILES
    BUILT_FILES gives each generated file that the gate writes from the
    build of ALWAYS, with its target of `loop derive`.  The mint inventory
    is such a file.  It needs the mint records of a full build, and a full
    build of a work tree takes hours on a busy host.  Thus the change of a
    landing does not hold the file, and the gate writes it.

    The gate writes each file of BUILT_FILES for each commit whose change
    can change it (built_selection()).  The module of the target gives the
    function is_input() of the paths that can change the file.  The modules
    of its import closure can change the file too.  A read of the selection
    that gives an error selects each file (THE CAREFUL SELECTION).  The run
    of the base writes no file, because its commit is the commit that the
    change starts from.  When the two sides of THE MERGE of
    assay.loop.land change such a file, the composition keeps its own copy,
    and the candidate names the file (Candidate.built).  The gate writes
    each such file too.  The changed paths of the candidate hold each file
    that the gate writes, as they hold a merged file.  The tests that read
    such a file are then touched tests, and each configuration runs them on
    the written file (THE SELECTION).

    The test of the rule derived-files reads the file in each configuration
    of objects.  Thus no step of a configuration other than its compile runs
    before the write ends.  The run of ALWAYS writes each file (Step.
    settle_built()):
      1. Its configure, and the target build.ninja when the others wait for
         it (THE THREADS)
      2. `cmake --build BUILD` of the target all, with no -j (THE JOBSERVER)
      3. `loop derive TARGET --write --build-dir BUILD` of the warm tree
      4. The fold of each written file that changed into a new commit
         (built_commit()).  The new commit has the parents of the commit,
         and its message adds the sentence of BUILT_NOTE.  The warm tree
         moves to that commit.
    Each other configuration compiles at the same time: the configure and
    `cmake --build BUILD` of a configuration of objects, `loop syntax BUILD`
    of a parse configuration, and the build of the vessel targets.  Then it
    waits for the end of the write, and it runs its tests and its checks on
    the new commit.  The vessel waits only for a file that it reads (THE
    VESSEL).  A compile reads no git state and no file of
    BUILT_FILES, so the move of the warm tree does not change it.  A build
    or a write that gives an error is a problem of ALWAYS, and the stop ends
    the other configurations.  The wait watches the stop event.

    The build of a configuration of objects writes its output to the log
    test.log, as `loop test` does.  A build error then gives the problem of
    a build error of `loop test` (BUILD_PROBLEM).  The run of the base writes
    no built file, and `loop test` builds there.  The two runs then give the
    same problem for one build error, and the landing compares them (THE RUN
    OF THE BASE).

    A plan with no run of ALWAYS, such as the focus of a request that runs
    alone (THE CONFIRMATION of assay.loop.land), writes each file before
    each configuration runs (Step.write_built()).  The round after THE
    REDUCE writes no file.  The rule derived-files of the suite compares the
    written file with each build.  The verdict gives each written file that
    changed (Verdict.built).

THE RATCHET ROWS
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
    higher count (THE WRITE of assay.core.ledger).  The refusal of the
    landing gives the rows after the problems (assay.loop.land).

THE RUN OF THE BASE
    A candidate with a focus is the commit of a run of the base (THE RUN OF
    THE BASE of assay.loop.land), or of the run alone of one request (THE
    CONFIRMATION of assay.loop.land).  The rules of this section apply to
    the two.  The title of the candidate tells the kind of the run in the
    first line of the check (Candidate.title).  The focus gives each configuration with
    a problem in the run of the batch, and the tests that were red alone
    too in it (Focus).  The gate runs only that focus, each configuration
    in a thread of its own (focused_runs()).  It runs the red tests with
    `loop test BUILD --only FILE`, a parse configuration in its mode, and
    each other configuration in the mode of the changed inputs.  The vessel
    runs in its mode (THE VESSEL).  The run of the base writes no ledger and
    folds no ledger.  Its problems then have the
    words of the problems of the batch.  A problem of a run of the base does
    not stop the other configurations of the focus (THE STOP).  The run of
    the base gives the result of each of them, because the landing compares
    each problem of the two runs.

THE VERDICT
    The gate has no base side: it does not compare the commit with HEAD.  A
    test that is red alone on HEAD then rejects each landing, until a commit
    corrects it.  Main must stay green.  The verdict gives the focus of each
    configuration with a problem, for THE RUN OF THE BASE.

THE FAILING ITEMS
    The verdict also gives the failing items of its problems (Verdict.items),
    and the landing compares the items of two runs, not their problems (THE
    RUN OF THE BASE of assay.loop.land).  Two runs can fail one test of a
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

THE LOGS
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
    then do not write one log.  The round after THE REDUCE puts AGAIN_MARK
    after the name of the configuration (log_name()).  The logs of the first
    round then stay, such as the output of a test that was red in the suite
    and green alone.
    When a run has a red test, the gate copies the report of the suite into
    the log SUITE_REPORT.  The next landing in the lane replaces the report in
    the build, so only that copy keeps the output of a flaky test.

THE LOCK OF THE TREE
    One landing at a time uses a warm tree.  tree_lock() holds an exclusive
    lock of flock(2) on the lock file of the warm tree of a lane in the work
    directory.  It waits for the lock, or with no wait it tells that a
    different landing holds it.  session() holds the lock of the first lane
    and gives the gate.

THE LANES
    Up to LANES landings run at the same time, each in a warm tree of its
    own: TREE, TREE-2, TREE-3 and so on, each with its own build
    directories.  LANES is the size of the slot pool of the host divided by
    SLICE, with one as the minimum and MOST_LANES as the maximum (5 for 224
    slots, where the division gives 7).  The pool caps the
    slots of all the lanes together, so a lane past the slots of the busy
    lanes only fills idle slots.
    free_lane() holds the lock of the first free lane.  A holder of the
    first lane also holds the lock of PRIMARY.  A lane after the first one is
    free only while that lock is held, so only while a holder of this copy
    of the module holds the first lane.  An earlier copy of the module takes
    only the first lane, and it reads the queue with no claim
    (assay.loop.land, THE LANES).  It then never runs at the same time as a
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
import shutil
import sys
import threading
import time
from collections.abc import Callable, Collection, Iterator, Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path, PurePosixPath

from assay.core import git, jobserver, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
from assay.loop import affected, heavy, tiers
from assay.loop import inputs as guard_inputs
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

# The directory of the warm tree, the lock of the tree, the directory of the logs and the lock of its runs, in the work
# directory (THE LOGS).
TREE = "tree"
LOCK = "tree.lock"
LOGS = "logs"
RUNS_LOCK = "logs.lock"
# The lock that a holder of the first lane holds, and the count of the lanes (THE LANES).  The pool caps the slots of
# all the lanes together, so each lane past the slots of the busy lanes fills only idle slots.  The count comes from the
# CPUs and not from the slots of the pool, because each lane has a warm tree of its own.  MOST_LANES caps the count: the
# owner set 5 lanes on 2026-11-16, for 5 working agents.  A host with fewer CPUs still gets fewer lanes.
PRIMARY = "primary.lock"
MOST_LANES = 5
LANES = min(MOST_LANES, max(1, proc.DEFAULT_JOBS // heavy.SLICE))
# The name of the directory of the logs of one run, before its number, the count of the ended runs that stay, and the
# file of the directory that a live run holds a lock on (THE LOGS).
RUN_MARK = "run-"
RUN_WIDTH = 6
RUNS_KEPT = 24
LIVE = ".live"
# An ended run that an unread verdict cites stays past RUNS_KEPT while it is younger than CITED_RUN_AGE_S, and while
# the cited runs that stay hold CITED_RUNS_BYTES or less together (THE LOGS).  On 2026-11-18, the lanes made about 16
# runs in an hour, of a median of 7.8 MB.
CITED_RUN_AGE_S = 24 * 3600.0
CITED_RUNS_BYTES = 2 << 30
# The mark after the name of the configuration in the name of a log of the round after THE REDUCE (THE LOGS).
AGAIN_MARK = ".again"
# The step name of the copy of the report of the suite, which holds the output of each red test (THE LOGS).
SUITE_REPORT = "suite.junit.xml"
# The build files of a change that selects the release builds and ThreadSanitizer, as a C or C++ file does (THE
# SELECTION, COMPILED_SUFFIXES of assay.loop.inputs).
BUILD_FILE_NAMES = ("CMakeLists.txt", "CMakePresets.json")
BUILD_FILE_SUFFIX = ".cmake"
SANITIZED_PREFIXES = ("include/", "src/", "cmake/")
BENCH_PREFIXES = ("bench/", "test/fuzz/")
EVERY_PREFIXES = ("utils/tools/quarantine/", "utils/rule-table.txt")
LEDGER_PREFIX = "utils/ledgers/"
# The sentence that the gate adds to the message of a commit with a written file (THE BUILT FILES).
BUILT_NOTE = "The gate wrote {paths} from the build of the composed tree."
# The words of the problem of a build error of a configuration of objects, in `loop test` and before it (THE BUILT
# FILES).  The two give the same words, so the run of the base compares them (THE RUN OF THE BASE).
BUILD_PROBLEM = "the build or the test run gives an error"
# The test of the suite that checks each rule of the engine plugin, the prefix of the test of a rule of the engine elf,
# and the reason cell of a new row that the gate gives (THE RATCHET ROWS).
PLUGIN_TEST = "guard_plugin"
RULE_TEST_PREFIX = "guard_"
ROW_REASON = "REASON"
# The configuration that runs the full suite for each change.  Its build gives the tests that read a build (THE
# SELECTION).
ALWAYS = "default"
# The preset of the vessel, the targets that the gate builds in its build, the label of the vessel tests, and the
# prefix of the paths of the vessel (THE VESSEL).  TORCH_DIR of the preset names a directory in TORCH_VENV of the
# source tree, which holds the patched PyTorch.
VESSEL = "vessel"
VESSEL_TARGETS = ("crucible_vessel", "crucible_dispatch")
VESSEL_LABEL = "vessel"
VESSEL_PREFIX = "vessel/"
TORCH_VENV = ".venv-torch"
# The memory of one job of each kind of each configuration, in GB (THE BUDGET of assay.loop.heavy): the 99th
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
# The file of the work directory with one JSON line for each kill for lack of memory (THE KILL FOR MEMORY).
KILL_LOG = "oom-kills.jsonl"
# The forms of a file HEAD that names a commit: a hash of SHA-1 or of SHA-256 in lowercase hexadecimal digits, and the
# branch with no commit that repair_head() writes when the reflog names no commit (THE WARM TREE).
HASH_LENGTHS = (40, 64)
HEX_DIGITS = "0123456789abcdef"
UNBORN_REF = "refs/heads/main"
# The outputs that a build never leaves with no bytes, the reply directory of the CMake file API in a build
# directory, and the number of empty outputs that the line of repair_builds() names (THE WARM TREE).
OUTPUT_SUFFIXES = (".o", ".a", ".so")
REPLY_DIRECTORY = (".cmake", "api", "v1", "reply")
EMPTY_SHOWN = 5
# The word of a configure that drops the cache of the build directory first (THE COMPILER HEAL).
FRESH_WORD = "--fresh"
# The weight of the lease of the vessel in the slot pool (THE WEIGHT of assay.loop.heavy).  With the weight 2, four runs
# of the gate built the vessel at the same time with 64 jobs each, and the host ran out of memory two times on
# 2026-11-15.  Each lease reads the available memory one time at its start, so leases that start together each count
# all of it.  The weight is 1 until the pool holds one memory budget for all leases.
VESSEL_WEIGHT = 1
# The rule that configures the build of a configuration of objects again after its first build (THE RUN OF A
# CONFIGURATION).
RECONFIGURE_RULE = "reconfigure-noop"
# The engines and the policies of a counted rule (THE COUNTED RULES).
COUNTED_ENGINES = frozenset({rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF})
COUNTED_POLICIES = frozenset({rule_registry.Policy.COUNT, rule_registry.Policy.ADMIT})
# The statuses of a test that count as red, the status of a skip, of a test that ran and passed, and of a disabled test.
RED = frozenset({ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.NOTRUN})
SKIPPED = ctest_reader.TestStatus.SKIPPED
RAN = ctest_reader.TestStatus.RUN
DISABLED = ctest_reader.TestStatus.DISABLED
# The limit of one call of `loop test`, of one configure, and of one test in the run of the red tests, in seconds.
TEST_RUN_S = 2 * proc.BUILD_S
CONFIGURE_S = proc.TOOL_S
ALONE_S = 1500.0
# The bytes of the output of one red test that the report of the run alone keeps.  The default of ctest is 300 KB
# (THE FAILING ITEMS).
ALONE_OUTPUT_BYTES = 64 * 1024 * 1024
# A finding line of a check (THE FORMAT of assay.core.report), and the words with which ctest replaces the part of an
# output past its limit.  The pattern has no nested repeat, so a match is linear in the length of the line.
FINDING_LINE = re.compile(r"^(?P<path>[^\s:][^:\n]*):(?P<line>\d+): error: \[(?P<rule>[^\]\s]+)\] ", re.MULTILINE)
CUT_MARK = "test output was removed since it exceeds the threshold"
# The interval of the look of a command at the stop event, in seconds (THE STOP).  The other configurations wait for
# the build of ALWAYS with the same interval (THE THREADS).  The wait between SIGINT and SIGKILL to the group of a
# command at a stop or at its limit, in seconds.
STOP_POLL_S = 0.2
STOP_GRACE_S = 30.0


@dataclass(frozen=True, slots=True)
class Configuration:
    """One configuration of the gate: a configure preset with the tier gate (THE CONFIGURATIONS).

    `build` is the build directory of the preset, relative to the tree.  `configuration_name` is the name of the
    configuration of its build (assay.core.configuration).
    """

    name: str
    build: str
    configuration_name: str
    is_parse: bool


def configurations(root: Path) -> tuple[Configuration, ...]:
    """Return each configure preset of a tree with the tier gate, the parse presets first (THE CONFIGURATIONS).

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

    THE VESSEL gives the rules.  Complexity: one read of the presets file, and linear in the presets.

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
    """A step of a run ends, because a different run of the check gave a problem (THE STOP)."""


class Mode(enum.Enum):
    """How one configuration runs (THE SELECTION).  The value is the words that the gate writes before its step."""

    PARSE = "parse and check"
    FULL = "build and test"
    CHANGED = "build and test the changed inputs"
    TOUCHED = "build and run the touched tests"
    VESSEL = "build the vessel and run the vessel tests"


@dataclass(frozen=True, slots=True)
class Run:
    """One configuration of a run of the gate: its mode, the names of the tests of the mode TOUCHED, and --build-only.

    With `is_build_only`, the run skips each test of the master guard that reads no build, and default runs it (THE
    SELECTION).
    """

    configuration: Configuration
    mode: Mode
    names: tuple[str, ...] = ()
    is_build_only: bool = False


@dataclass(frozen=True, slots=True)
class Focus:
    """One configuration of a run of the base, and the tests that were red alone too in it (THE RUN OF THE BASE).

    With no names, the problem of the configuration was not a red test, and the run of the base runs the configuration.
    """

    configuration: str
    names: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Candidate:
    """The commit of a landing: the commit that the composition started from, the commit, the paths and the focus.

    `focus` is empty for the commit of a batch, and it gives the configurations of a run of the base (THE RUN OF THE
    BASE).  `is_every` selects each configuration, as --all-configurations does (THE SELECTION).  `built` names each
    file of BUILT_FILES that THE MERGE of assay.loop.land leaves to the gate.  The gate adds each file that the change
    can change, and it writes each one before the tests of each configuration (THE BUILT FILES).  `title` names the
    run in the first line of the check, such as "the run of the base at COMMIT".  The landing gives it, because only
    the landing knows the kind of the run.  With no title, the line names the commit.  `on_fold` gets the new commit
    when the gate folds the built files into the commit, before the tests of each configuration (THE BUILT FILES).
    The landing then writes that commit into its entry of the stack (THE STACK of assay.loop.land).
    """

    head: str
    commit: str
    changed: tuple[str, ...]
    focus: tuple[Focus, ...] = ()
    is_every: bool = False
    built: tuple[str, ...] = ()
    title: str = ""
    on_fold: Callable[[str], None] | None = field(default=None, compare=False)


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of the gate: the configurations that ran, each problem, and the wall times in seconds.

    `seconds` holds each wait.  `step_seconds` gives the time of each configuration after its wait for its first slot,
    in the sequence of `configurations`.  `wait_seconds` gives that wait, and `slot_seconds` the sum of the slots of
    the configuration over its time (THE JOBSERVER).  `final` is the commit that the gate judged
    last, and `ledgers` holds each ledger that THE REDUCE folded into it.  `final` is "" for a verdict of no commit.
    `logs` is the directory of the logs of the run (THE LOGS).  `focus` gives each configuration with a problem (THE
    VERDICT).  `flaky` gives each test that was red in the suite and green alone, as CONFIGURATION: TEST.  `built`
    holds each file that the gate wrote from the build of ALWAYS and folded into `final` (THE BUILT FILES).  `rows`
    holds the lines of THE RATCHET ROWS of a refused commit.  `items` holds the failing items of the problems (THE
    FAILING ITEMS).  A verdict with problems and no items gets each problem as its own item.
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
    """The end of the run of one configuration in a check (THE THREADS, THE STOP).

    `red` holds the tests that were red alone too, and `flaky` the tests that were red in the suite and green alone.
    `is_stopped` tells that the stop ended the run, which then has no problem of its own.  `seconds` starts after the
    wait for the first slot of the lease, and `wait_s` is that wait.  `slot_seconds` is the sum of the slots of the
    lease over its time (THE JOBSERVER).  `items` holds the failing items of the problems (THE FAILING ITEMS).  An
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
    """One generated file that the gate writes from the build of ALWAYS (THE BUILT FILES).

    `target` is the target of `loop derive` that writes the file.  `module` is the module of the target.  Its function
    is_input(path) tells if a change of a path can change the file, and the modules of its import closure can change
    the file too.  `is_vessel_input` tells if the build of the vessel targets or a vessel test reads the file.  The
    tests of the vessel then wait for the write (THE VESSEL).
    """

    target: str
    module: str
    is_vessel_input: bool


# ── The selection ──────────────────────────────────────────────────────────

# The generated files that the gate writes from the build of ALWAYS (THE BUILT FILES).
BUILT_FILES = {
    "misc/mint-inventory.md": BuiltFile("mint-inventory", "assay.loop.derive.mint_inventory", is_vessel_input=False)
}
CONFIGURATIONS = configurations(paths.REPO_ROOT)
BY_NAME = {configuration.name: configuration for configuration in CONFIGURATIONS}
VESSEL_CONFIGURATION = vessel_configuration(paths.REPO_ROOT)
# The errors of a read of the selection that give its widest result (THE CAREFUL SELECTION).
READ_ERRORS = (GateError, git.GitError, proc.ToolFailed, OSError, ValueError)


def careful[Result](reading: Callable[[], Result], widest: Result, what: str, result_text: str) -> Result:
    """Return the result of a read of the selection, or its widest result when the read gives an error.

    THE CAREFUL SELECTION gives the rule.  The function writes one line with the error.

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
    """Tell if a path is a build file (THE SELECTION)."""
    return PurePosixPath(path).name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIX)


def is_compiled_change(path: str) -> bool:
    """Tell if a path of a change is a C or C++ file or a build file, which a build of a configuration can read."""
    return guard_inputs.is_compiled_file(path) or is_build_file(path)


def select(changed: Sequence[str], parse_presets: Sequence[str], is_every: bool) -> tuple[Configuration, ...]:
    """Return the configurations of the full suite and the parse configurations of a change (THE SELECTION).

    The configurations are in the sequence of CONFIGURATIONS.

    Args:
        changed: The paths that the change touches
        parse_presets: The parse presets that run: those that `loop syntax --changed` names, and each parse preset
            when the change touches the inputs of a rule of COUNTED_RULES
        is_every: Select each configuration

    Complexity: linear in the paths and the names.
    """
    names = {ALWAYS}
    for path in changed:
        if is_compiled_change(path):
            names.update(("release", "release-v3", "tsan"))
        is_top_build_file = is_build_file(path) and "/" not in path
        if path.startswith(SANITIZED_PREFIXES) or is_top_build_file:
            names.update(("tsan", "ubsan-strict"))
        if path.startswith(BENCH_PREFIXES):
            names.add("bench-fuzz")
        if path.startswith(EVERY_PREFIXES):
            is_every = True
    names.update(preset for preset in parse_presets if preset in BY_NAME)
    return tuple(item for item in CONFIGURATIONS if is_every or item.name in names)


def changes_each_build(tree: Path, change: guard_inputs.Change, changed: Sequence[str]) -> bool:
    """Tell if a change can change each build: a C or C++ file, a build file, or an input of a meter of a test.

    The meters are the test launcher and the fixture driver (meter_inputs() of assay.loop.affected, THE SELECTION).
    Complexity: linear in the paths, and one parse of each module of the closures of the meters.
    """
    if any(is_compiled_change(path) for path in changed):
        return True
    return any(meter.is_touched(change) for meter in affected.meter_inputs(tree))


def reaches_vessel(changed: Sequence[str], is_each: bool) -> bool:
    """Tell if the vessel runs for a change: it can change each build, or it changes a path of VESSEL_PREFIX.

    THE VESSEL gives the rule.  `is_each` tells that the change can change each build (changes_each_build()).
    Complexity: linear in the paths.
    """
    return is_each or any(path.startswith(VESSEL_PREFIX) for path in changed)


def is_read_by_vessel(built: Sequence[str], rows: Mapping[str, BuiltFile] = BUILT_FILES) -> bool:
    """Tell if the vessel reads a file that the gate writes for a candidate, so its tests wait for the write.

    THE VESSEL gives the rule.  `built` holds the paths of the files that the gate writes, and each one is a key of
    `rows`.  Complexity: linear in the paths.
    """
    return any(rows[path].is_vessel_input for path in built)


def built_selection(tree: Path, changed: Sequence[str]) -> tuple[str, ...]:
    """Return each file of BUILT_FILES that a change can change, sorted (THE BUILT FILES).

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
    """Give the warm tree the link TORCH_VENV to the patched PyTorch of the main work tree, or give a problem (THE VESSEL).

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
    """Tell if a change touches the inputs of a rule that a parse configuration checks (COUNTED_RULES, THE SELECTION).

    The caller gives the change with no C or C++ file and no build file.  A change of such a file changes the result of
    a parse build only through an arm, and `loop syntax --changed` names the parse configurations of an arm.

    Raises:
        git.GitError: If the tree is not a git work tree

    Complexity: the cost of assay.loop.inputs for each rule of COUNTED_RULES.
    """
    deriver = guard_inputs.Deriver(tree)
    return any(deriver.rule_inputs(rule_id, "check").is_touched(change) for rule_id in COUNTED_RULES)


def other_runs(others: Sequence[Configuration], is_each: bool, names: Sequence[str]) -> tuple[Run, ...]:
    """Return the runs of the configurations of objects that do not run the full suite (THE SELECTION).

    Args:
        others: The configurations of objects that select() does not give
        is_each: The change can change each build (changes_each_build())
        names: The touched tests: each test that reads the build and that has an input that the change touches
    """
    if is_each:
        return tuple(Run(item, Mode.CHANGED, is_build_only=True) for item in others)
    return tuple(Run(item, Mode.TOUCHED, tuple(names)) for item in others) if names else ()


def focused_runs(focus: Sequence[Focus], vessel: Configuration | None = VESSEL_CONFIGURATION) -> tuple[Run, ...]:
    """Return the runs of a run of the base, one for each configuration of its focus (THE RUN OF THE BASE).

    A parse configuration runs in its mode.  A configuration with red tests runs them in the mode TOUCHED, and each
    other configuration runs in the mode CHANGED.  The vessel runs in its mode, with its red tests only when the focus
    names them (THE VESSEL).

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


def counted_rules(tree: Path) -> tuple[str, ...]:
    """Return each counted rule of a tree, sorted (THE COUNTED RULES).

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
    """Return the counted rules of each test of the suite that checks them (THE RATCHET ROWS).

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


def is_checked(configuration: Configuration, rule_id: str) -> bool:
    """Tell if the run of a configuration checks a rule: each run of objects, and a parse run for COUNTED_RULES."""
    return not configuration.is_parse or rule_id in COUNTED_RULES


def is_each_built(outcomes: Sequence[Outcome]) -> bool:
    """Tell if the outcomes of a round build each configuration of CONFIGURATIONS with no problem (THE REDUCE).

    Complexity: linear in the outcomes.
    """
    built = {item.name for item in outcomes if not item.is_stopped and not item.problems}
    return all(item.name in built for item in CONFIGURATIONS)


def reduce_words(tree: Path, rule_id: str) -> list[str]:
    """Return the words after `guard write` of the reduce of one counted rule in a tree (THE REDUCE).

    The words give the build of each configuration that checks the rule (is_checked()).
    """
    words = ["write", "--rule", rule_id]
    for configuration in CONFIGURATIONS:
        if is_checked(configuration, rule_id):
            words += ["--build-dir", str(build_directory(tree, configuration))]
    return words


# ── The warm tree ──────────────────────────────────────────────────────────


def move_tree(root: Path, tree: Path, commit: str) -> None:
    """Give the warm tree the commit, and make the tree first when it is not there (THE WARM TREE).

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
    """Return the memory of one job of a lease of a configuration that runs jobs of `kinds`, in GB (THE BUDGET of
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
    """Give a warm tree a HEAD again, when its file HEAD holds no reference and no commit (THE WARM TREE).

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


def remove_untracked(tree: Path) -> None:
    """Remove each file of a warm tree that the commit does not hold and that no ignore rule names (THE WARM TREE).

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
    """Remove the damage that a stop of the host leaves in the build directories of a warm tree (THE WARM TREE).

    A stop during a build can leave an output with no bytes, and Ninja then sees the output as built.  The function
    removes each object, archive and shared library of OUTPUT_SUFFIXES with no bytes, and Ninja builds it again.  A
    stop during a configure can leave a reply of the CMake file API with no bytes, or an index of the reply that does
    not read as JSON.  The function then removes the reply and the graph build.ninja, so the run configures the
    build again (THE RUN OF A CONFIGURATION, step 1).  The holder of the lane is the only writer of the warm tree.
    Complexity: linear in the files of the build directories.
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
            (build / "build.ninja").unlink(missing_ok=True)
            check_report.write_line(
                f"loop land: {configuration.name}: the reply of the CMake file API does not read, so the gate removes "
                f"it and the run configures {build} again"
            )


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
    """Make the index of a warm tree again from its HEAD, when no git command can read it (THE WARM TREE).

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
    """Return the name of the log of one step of a configuration, with AGAIN_MARK in the round after THE REDUCE.

    THE LOGS gives the rule.  `step` is the rest of the name, such as test.log.
    """
    return f"{configuration.name}{AGAIN_MARK if is_again else ''}.{step}"


def failing_items(
    configuration: str, names: Sequence[str], outputs: Mapping[str, str], tree: Path, commit: str
) -> tuple[str, ...]:
    """Return the failing items of the tests of one configuration that are red alone too (THE FAILING ITEMS).

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

    THE RUN OF A CONFIGURATION, step 1, and THE COMPILER HEAL give the rules.  A build directory with no graph of Ninja
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

    The message is the message of the commit, and then the sentence that names the files (THE BUILT FILES).
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
    """Return no name of a run: a gate with no queue keeps no ended run for a verdict (THE LOGS)."""
    return frozenset()


@dataclass(frozen=True, slots=True)
class Gate:
    """The gate of one session: the repository, the work directory, the jobs, --all-configurations, and the memory.

    `jobs` is the most jobs of the lease of one configuration, with SLICE of assay.loop.heavy as the most (slice).
    `read` is the reader of the available memory, which gives the cap of a lease (THE MEMORY of assay.loop.heavy).
    `run` names the directory of the logs of one run in LOGS, or it is "" for LOGS itself (THE LOGS).  `pool` is the
    slot pool of the leases, or None for the pool of the host (THE JOBSERVER).  `lane` names the warm tree (THE
    LANES).  `vessel` is the configuration of the vessel, or None for a gate with no vessel (THE VESSEL).  `oom_kills`
    gives the count of the kills of the kernel for lack of memory (THE KILL FOR MEMORY).  `cited` gives the name of each
    ended run that an unread verdict cites, which the sweep of the runs keeps (THE LOGS).
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

    @property
    def slice(self) -> int:
        """The most jobs of the lease of one configuration: `jobs`, with SLICE of assay.loop.heavy as the most."""
        return max(1, min(self.jobs, heavy.SLICE))

    @property
    def tree(self) -> Path:
        """The warm tree of the lane of the gate (THE LANES)."""
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
        """Return the words of the mode of a run after `loop test BUILD` (THE SELECTION).

        A run of the full suite with `is_plain` runs the tests of the changed inputs.  The run of the touched tests
        writes the file of --only into the directory of the logs, with the name of its round (log_name()).  A run with
        `is_build_only` gets --build-only.
        """
        if run.mode is Mode.TOUCHED:
            self.logs.mkdir(parents=True, exist_ok=True)
            only = self.logs / log_name(run.configuration, "only.txt", is_again)
            only.write_text("".join(f"{name}\n" for name in run.names), encoding="utf-8")
            return ["--only", str(only)]
        words = ["--all"] if run.mode is Mode.FULL and not is_plain else []
        return [*words, "--build-only"] if run.is_build_only else words

    def touched_names(self, change: guard_inputs.Change) -> tuple[str, ...]:
        """Return each touched test of the build of ALWAYS, sorted: it reads the build, and the change touches its inputs.

        The run of ALWAYS configures its build with the change first (THE THREADS).

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
        of the build of ALWAYS.  A read that gives an error gives the plan of a change that can change each build (THE
        CAREFUL SELECTION).
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
        """Give the warm tree the commit, select the configurations, and run them at the same time (THE THREADS).

        A candidate with a focus runs only its focus (THE RUN OF THE BASE).  The gate writes each built file that the
        change can change, or that the candidate names, before the tests of each configuration (THE BUILT FILES).

        Raises:
            GateError: If the warm tree cannot hold the commit, a step cannot start, or the candidate names a built file
                that no row of BUILT_FILES gives
            git.GitError: If a git command fails
        """
        started = time.monotonic()
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
            selected = select(candidate.changed, presets, self.is_every or candidate.is_every)
            plan = tuple(
                Run(item, Mode.PARSE) if item.is_parse else Run(item, Mode.FULL, is_build_only=item.name != ALWAYS)
                for item in selected
            )
            others = tuple(item for item in CONFIGURATIONS if not item.is_parse and item not in selected)
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
            rows: tuple[str, ...] = ()
            if not candidate.focus and counted and any(item.red for item in outcomes):
                rows = self.ratchet(step.candidate, environment, outcomes, counted)
            final: str = step.candidate.commit
            ledgers: tuple[str, ...] = ()
            if not candidate.focus and not any(item.problems for item in outcomes):
                if is_each_built(outcomes):
                    step.reduce(counted)
                else:
                    check_report.write_line(
                        "loop land: the landing builds only some configurations of the gate, so the gate lowers no "
                        "row of a counted ledger"
                    )
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
        )

    def ratchet(
        self,
        candidate: Candidate,
        environment: Mapping[str, str],
        outcomes: Sequence[Outcome],
        counted: Sequence[str],
    ) -> tuple[str, ...]:
        """Return THE RATCHET ROWS of a red round, from a step with a stop event of its own.

        The rows only aid the author.  An error of their step gives one line and no row, and the problems of the round
        stay the verdict.
        """
        try:
            return Step(self, candidate, environment).ratchet_rows(outcomes, counted)
        except Exception as problem:  # noqa: BLE001
            check_report.write_line(f"loop land: the gate cannot give the rows of the ratchet ledgers: {problem}")
            return ()

    def check_in_run(self, candidate: Candidate, is_retry: bool) -> Verdict:
        """Check a candidate with its logs in a new run, which stays live until the check ends (THE LOGS).

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
    its configuration (THE JOBSERVER).  `always_ready` tells that the build of ALWAYS holds the configure of the change
    (THE THREADS).  `built_ready` tells that the write of THE BUILT FILES ended, and it is set from the start for a
    candidate with no built file.  `built` holds each written file that changed, and the run of ALWAYS or
    write_built() is its one writer.
    """

    __slots__ = (
        "_said",
        "always_ready",
        "built",
        "built_ready",
        "candidate",
        "environment",
        "gate",
        "stop",
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
        """Hold a lease of the slot pool with the slice of the gate times `weight` for the block (THE JOBSERVER).

        `gb_per_job` is the memory of one job of the lease, and a lease with `is_capped` never grows past its slice.
        `weight` multiplies the fair share of the lease among the leases of the gate (THE POOL, THE MEMORY and THE
        WEIGHT of assay.loop.heavy).

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
        a different error stopped then does not hide that error (THE STOP).  The pool has at most proc.DEFAULT_JOBS
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

        The command gets MAKEFLAGS of the jobserver of the lease (THE JOBSERVER).  A command that runs longer than its
        limit gives the exit of its end.  The command looks at the stop event each STOP_POLL_S seconds (THE STOP).

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
        """Configure the build directory of a configuration, and give a problem (THE RUN OF A CONFIGURATION, step 1).

        A build directory with no graph of Ninja gets its configure.  A build directory whose cache names a compiler
        that its toolchain file does not resolve gets its configure with FRESH_WORD (THE COMPILER HEAL).

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

    def heal_line(self, configuration: Configuration, build: Path) -> str:
        """Return the line of `loop heal` of the warm tree for a build directory, or an empty line (THE COMPILER HEAL).

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
        """Build the target build.ninja of the build of a configuration, and give a problem (THE THREADS).

        The build configures the build directory again when the change touches its configure.  It gets no -j, because
        ninja obeys the jobserver only with no -j (THE JOBSERVER).

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

        THE BUILT FILES gives the rules: a plan with no run of ALWAYS writes the files here.  The result is an outcome
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
        """Settle THE BUILT FILES before the tests of a configuration of objects, and give each problem.

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
        """Wait for the end of the write of THE BUILT FILES, watch the stop event, and write the time of a wait.

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
        """Build the target all of a configuration of objects, and give a problem (THE BUILT FILES).

        The build gets no -j, because ninja obeys the jobserver only with no -j (THE JOBSERVER).  It writes its output
        to the log of `loop test`, and a build error gives the problem of a build error of `loop test`.  A run of the
        base writes no built file, so its build error then gives the same problem (THE RUN OF THE BASE).

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

        THE BUILT FILES gives the steps.  The result holds a problem for a configure or a build that gives an error, or
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
        self, configuration: Configuration, rules: Sequence[str], share: heavy.Share, is_again: bool = False
    ) -> list[str]:
        """Run `guard check --build-dir` of each rule on a build at the same time, and give a problem for each exit other
        than 0 or 77."""
        build = build_directory(self.gate.tree, configuration)

        def check_one(rule: str) -> str | None:
            """Run the check of one rule, and give its problem or None."""
            log = log_name(configuration, f"check-{rule}.log", is_again)
            command = [*self.gate.master("guard"), "check", "--rule", rule, "--build-dir", str(build)]
            status = self.run_logged(command, log, share, proc.TOOL_S)
            if status in (ExitCode.OK, ExitCode.SKIPPED):
                return None
            return f"{configuration.name}: the rule {rule} gives findings, see {self.gate.logs / log}"

        return [problem for problem in self.concurrently(list(rules), check_one) if problem is not None]

    def reduce(self, counted: Sequence[str]) -> None:
        """Run the reduce of each counted rule over the builds of the configurations, at the same time (THE REDUCE).

        Each rule has one write.  A write that refuses a count exits 1, and it writes no ledger.  The writes share one
        lease (THE JOBSERVER).  Complexity: one write of each counted rule.
        """
        if not counted:
            return
        with self.leased() as share:

            def write_one(rule: str) -> None:
                """Write the ledger of one rule over the build of each configuration that checks it."""
                log = f"reduce-{rule}.log"
                command = [*self.gate.master("guard"), *reduce_words(self.gate.tree, rule)]
                status = self.run_logged(command, log, share, proc.TOOL_S)
                self.say(f"loop land: the reduce of {rule} exits {status}")

            self.concurrently(list(counted), write_one)

    def fold(self) -> tuple[str, tuple[str, ...]]:
        """Fold each ledger that THE REDUCE changed in the warm tree into the commit, and give the commit and the ledgers.

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

    def ratchet_rows(self, outcomes: Sequence[Outcome], counted: Sequence[str]) -> tuple[str, ...]:
        """Return the lines of THE RATCHET ROWS of the red configurations of objects of a round.

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

        THE RATCHET ROWS gives the rules.  The ledger of the warm tree gets its bytes back after the write.
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

        The tests run at the same time, in one run of ctest with the jobs of the lease (THE RUN OF A CONFIGURATION).
        The report of that run gives the status of each test.  A test with no status in the report is red.  The limit
        of the run is ALONE_S for each test, the time of the tests one after the other.  The files of the run have the
        name of its round (log_name()).

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
        command += ["--timeout", str(int(ALONE_S)), "--output-on-failure", "--output-junit", str(report)]
        command += ["--test-output-size-failed", str(ALONE_OUTPUT_BYTES)]
        self.run_logged(command, log_name(configuration, "alone.log", is_again), share, ALONE_S * len(names))
        try:
            statuses = ctest_reader.results(report)
        except ctest_reader.CtestError:
            statuses = {}
        return [name for name in names if statuses.get(name, ctest_reader.TestStatus.NOTRUN) in RED]

    def run_parse(self, configuration: Configuration, share: heavy.Share) -> list[str]:
        """Do steps 1 and 2 of THE RUN OF A CONFIGURATION for a parse configuration, and give each problem."""
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
        """Do steps 1 and 3 of THE RUN OF A CONFIGURATION for a configuration of objects, and give each problem.

        `words` are the words of the mode after `loop test BUILD` (Gate.loop_words()).  A run again after the fold does
        not run RECONFIGURE_RULE.  The run of ALWAYS first builds the target build.ninja when the other configurations
        wait for it (THE THREADS).  Then each run settles THE BUILT FILES (settle_built()).  The second result holds the
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
        if is_again:
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

        Each red test runs one more time apart from the suite (THE RUN OF A CONFIGURATION).  `report` is the JUnit
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
        flaky tests (THE VESSEL).

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
        command += ["--output-on-failure", "--output-junit", str(report)]
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
        """Run one configuration, or its tests again after THE REDUCE, and give each problem, the tests red alone too,
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
        """Append one line of a kill for lack of memory to KILL_LOG of the work directory (THE KILL FOR MEMORY).

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

        THE THREADS, THE STOP and THE JOBSERVER give the rules.  The time of the outcome starts after the wait for the
        first slot of the lease.  A problem sets the stop event, other than in a run of the base (THE RUN OF THE
        BASE).  An error of the run sets it too, and goes to the caller.
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
        if problems and not self.candidate.focus:
            self.stop.set()
        items = self.red_items(run, red, is_again) if red else ()
        return Outcome(name, tuple(problems), red, seconds, False, flaky, wait_s, slot_seconds, items)

    def red_items(self, run: Run, red: Sequence[str], is_again: bool) -> tuple[str, ...]:
        """Return the failing items of the tests that are red alone too, from the report of the run alone.

        A report that does not read gives no output, so each test gets the item of an unknown output (THE FAILING
        ITEMS).
        """
        report = self.gate.logs / log_name(run.configuration, "alone.junit.xml", is_again)
        try:
            outputs = ctest_reader.outputs(report)
        except ctest_reader.CtestError:
            outputs = {}
        return failing_items(run.configuration.name, red, outputs, self.gate.tree, self.candidate.commit)

    def require_problem(self, outcomes: Sequence[Outcome]) -> None:
        """Refuse a check that the stop ended with no problem of a configuration (THE STOP).

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
        """Run each configuration at the same time, and the runs that `others` gives, and give each outcome (THE THREADS).

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
    """Make the directory of a new run in `logs`, one above the highest number of `runs`, and return its name (THE LOGS).

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
    """Return the ended runs that the sweep removes, from the oldest (THE LOGS).

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
    """Make the directory of the logs of a new run, hold it live for the block, and give its name (THE LOGS).

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
    """Return the name of the warm tree of a lane in the work directory: TREE, then TREE-2 and TREE-3 (THE LANES)."""
    return TREE if lane == 0 else f"{TREE}-{lane + 1}"


def lock_name(lane: int) -> str:
    """Return the name of the lock file of the warm tree of a lane in the work directory (THE LANES)."""
    return LOCK if lane == 0 else f"{tree_name(lane)}.lock"


@contextlib.contextmanager
def tree_lock(work: Path, wait: bool = True, lane: int = 0) -> Iterator[bool]:
    """Hold the lock of the warm tree of a lane for the block, and give True (THE LOCK OF THE TREE).

    With a wait, the block waits while a different landing holds the lock.  With no wait, the block gets False
    immediately when a different landing holds it, and holds nothing.  A holder of the first lane also holds the lock
    of PRIMARY (THE LANES).
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

    A lane after the first one is free only while a holder of this copy of the module holds the first lane (THE
    LANES).  Complexity: one try of the lock of each lane.
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
def session(root: Path, work: Path, jobs: int, is_every: bool) -> Iterator[Gate]:
    """Hold the lock of the warm tree for the block, and give the gate (THE LOCK OF THE TREE).

    The block waits while a different landing holds the lock.
    """
    with tree_lock(work):
        yield Gate(root, work, jobs, is_every)
