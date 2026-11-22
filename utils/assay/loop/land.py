"""land — compose the commits of the waiting landings, test them one time in the gate, and move main by compare-and-swap.

Request
    The words before the word -- give the message and the edits.  The words
    after it give the own paths, and that list can be empty:
      -m TEXT                   One paragraph of the message.  The first paragraph is the subject
      --patch FILE              A unified diff that git made against HEAD
      --replace FILE OLD NEW    In the shared FILE, the text of the file NEW replaces the text of the file OLD
      --block FILE AFTER BLOCK  In the shared FILE, the text of the file BLOCK goes after the text of the file AFTER
      --series BRANCH           Each commit of BRANCH that HEAD does not hold, as one request (the series)
      --jobs N                 The slice of the lease of one configuration of the gate, with SLICE as the most
      --all-configurations      The gate runs each configuration (assay.loop.gate, the selection)
      --budget-reason TEXT      The gate raises each crossed row of a budget with this reason (the budget reason)
      --budget-reason ITEM=TEXT The gate raises the crossed row of ITEM with this reason (the budget reason)
      --lower RULE ITEM        The gate lowers the row of ITEM of RULE to the largest measure of its builds (the lower)
      --lower-reason TEXT       The reason of the cut, which each lowered row of the request gets (the lower)
      --check                   Run the gate on the commit, and do not land it
      --here                    With --check: run this copy of the module (the driver)
      --dry-run                 Print the diff of the commit, and change nothing
      --tree                    Print the object name of the tree of the commit, and change nothing
    The value of an option can be the word --.  The command then reads the
    options in sequence to find the end of the options (split_words()).  The
    files of --patch, --replace and --block are paths from the current
    directory.  An own path and the shared FILE are paths from the top of the
    repository of the current directory.  --check with no edit, patch or own
    path runs the gate on HEAD.

Tree
    The tree of the commit is the commit before it with the working-tree copy
    of each own path.  It also holds each shared file as the commit before it
    holds it, with the edits.  When an own path is gone from the working tree,
    the tree does not hold it.  A shared file does not come from the working
    tree.  The commit then cannot hold an edit of a different writer that is
    not committed, or an earlier copy of a shared file.  The text that
    --replace replaces and the text that --block writes after must each occur
    one time in the file.  An edit that the commit before holds is not
    applied again.  Some edits can name one file, and they apply in the given
    sequence.  An own path cannot be a shared file.  Each path that the commit
    changes is an own path, a file in an own directory, or a shared file.
    Else the request does not compose.

    The request file holds the commit of HEAD of the work tree at the
    request (the queue).  A different commit can land after that commit and
    change a file of an own path.  The working-tree copy can be without that
    change, and the commit of the request can revert it.  A request does not
    compose when it changes a file of an own path that is not the same in the
    two commits.  A request with no such commit compares with the commit that
    its batch started from.

Composition
    The command composes the tree in the index of a scratch repository.  That
    repository writes into the object directory of the repository (the
    borrowed index of assay.core.git).  Each blob, tree and commit that the composition
    writes is then in the repository.  The scratch repository reads the own
    paths from the work tree of the request through --work-tree.

Merge
    A patch applies with `git apply --cached`.  A patch that does not apply
    as text composes by a three-way merge of each of its files
    (merged_patch()).  git builds the base of each file from the blobs that
    the patch names, and the result of the patch on that base.  Ours is the
    copy of the composition.  A file that ours did not change takes the
    result of the patch, and a file that the two sides changed merges:
      * A ledger of a rule of the registry of the composition merges by its
        rows (the merge of assay.core.ledger).  When two changes of one count
        merge by a sum, the merged count can be too high.  The gate then runs
        each configuration, and the reduce writes the measured count.
      * A count that ours and the patch change to one value is two changes,
        so it merges by a sum too (EqualChange.SUM of assay.core.ledger).
        Two requests that each raise a row from 1 to 2 then compose to 3, on
        the branch, on the stack and in each commit of a series.  Two
        different reasons of such a row join.  The one exception is a patch
        of a request of --patch that ours holds whole, file for file: its
        commit landed already, so it changes nothing (HeldPatch.LANDED).  A
        commit of a series is never such a commit, because the series
        refuses a commit that HEAD holds (the series).
      * A witness fixture and the witness manifest come from the writer of
        assay.loop.derive.witness_fixtures and the composed rule table, when
        the composition holds the code of the writer of this copy.
      * A file of BUILT_FILES of assay.loop.gate, such as the mint inventory,
        needs a build.  The composition keeps its own copy, and the
        candidate of the gate names the file.  The gate writes the file from
        the build of the composed tree, and it folds the file into the last
        commit (the built files of assay.loop.gate).
      * The registry merges by its lines, and on a conflict by its rows
        (merged_registry()).  The key of a row is its first two words, such
        as `rule must-consume`.  The places of the rows merge with each row
        read as its key, and each row that the two sides change merges by
        its fields.  A list of a field, such as kinds=A,B, keeps the adds and
        the removes of each side, and a text cell merges by its words.  Two
        requests that each add a kind to one rule and a sentence to its text
        then compose.  Two changes of one word or of one value conflict.
      * Each other file merges by its lines with `git merge-file`.
    Two changes of one line, one side that adds or removes a file that the
    other side changes, and a merge of a ledger that conflicts each refuse
    the request.  The refusal names each such file.  A raise or an admit of
    a patch stays a change of its row in the commit.  The verdict names each
    merged file and each file that the gate writes.

Series
    `loop land --series BRANCH --` in the main work tree lands each commit
    of BRANCH that HEAD does not hold, from the oldest, each with its own
    message, as one request (series_request()).  The author of each commit
    is the configured identity.  A series takes no -m, no patch, no edit and
    no own path.  --all-configurations, --jobs, --check, --dry-run and
    --tree apply to the whole series.
      * The request holds the patch of each commit against its first parent,
        and the message of the commit.  The process writes the request
        into the file NUMBER.series of the queue.  An earlier copy of this
        module reads only the files NUMBER.request, so it never lands a
        series as one commit.
      * A commit of the branch whose subject a commit of HEAD after the
        merge base of HEAD and the branch holds refuses the series
        (landed_subjects()).  A landing makes new commits with the messages
        of the series, and the branch keeps the commits that it landed.  A
        second queue of such a branch with no rebase then holds the change
        of a landed commit two times.  The refusal tells the agent to rebase
        the branch onto main, which drops each such commit.  The subjects of
        main are unique: 2033 commits of 2026-11 had no subject two times.
      * A merge of the branch gives no commit, because its two sides are in
        the series or in HEAD.  A merge that holds a change of its own, such
        as the resolution of a conflict, refuses the series, because no
        commit of the series holds that change.  The refusal tells the
        agent to rebase the branch onto main and to resolve each conflict
        in its own commit.
      * The holder composes each commit on the commit before it, as a patch
        does (the merge).  The first commit starts from the commit of the
        batch or from the top of the stack.  When the branch started from
        an earlier commit of main, each patch thus applies to the main of
        the run, as `git cherry-pick` does.  The first commit that does not
        compose refuses the series, with its number, its commit and its
        subject.  A commit whose change the commit before it holds gives no
        commit.
    A series is one request for the batch, the search, the confirmation, the
    right of way and the stack.  The gate tests only the last commit, the
    tip, in one run, so the slot cost does not grow with the number of
    commits.  A red series is refused whole, and the search does not divide
    it.  The swap moves the branch to the tip, so main gets each commit of
    the series, in sequence.
    the built files and the reduce fold into the last commit of the batch,
    which is the last commit of a series at the end of the batch.  The fold
    gives that commit a new tree with the same parents, the same message
    and a note.  Main then holds each commit of the series one time and no
    other commit, and the gate tests the folded commit again.  A ledger or a
    generated file can change in each commit of a series.  When it is in
    the last commit only, a conflict of a rebase is only in that commit.
    Main never holds a commit that the gate did not test as part of a
    tested tip: each commit of a series is an ancestor of the tip that the
    gate accepted, and the swap moves the branch only to that tip.  The gate
    does not build or test a commit before the tip alone.

Queue
    A landing writes its request into the queue of its repository before it
    waits for the lock of the warm tree.  The queue is the directory
    QUEUE/KEY of the work directory.  KEY is the first KEY_LENGTH digits of
    the SHA-256 of the path of the shared git directory of the repository.
    The requests of two repositories then do not mix.

    The request gets a new number.  The file NEXT of the queue holds the next
    number, and an exclusive lock of flock(2) on that file keeps two
    reservations apart.  The number is also above the number of each file of
    the queue.  Each number occurs one time only, and a verdict then cannot
    go to a different request.  The process locks a new file, and it links
    that file to the file NUMBER.lock.

    The process holds that exclusive lock of flock(2) while it waits.  Then
    it writes a copy of each patch and the file NUMBER.request.  That file
    holds the message, the edits, the own paths and the options.  It also
    holds the work tree, its branch, the commit of its HEAD, its shared git
    directory and a random token.  The verdict of the request holds the same
    token, and the process reads no verdict with a different token.

    The kernel removes the lock when the process ends, also on a signal.  A
    process that ends removes its request file first and its lock file last.
    The next holder of the tree removes each file of a request with no lock.

Wait
    The landing waits for one of two events, and it looks again each POLL_S
    seconds.  When the queue holds the file NUMBER.verdict with its token,
    the landing reads that verdict.  It gives its work tree the update of the
    verdict (the index and the working tree).  Then it writes the lines of the
    verdict and exits with its status.  When the landing gets the lock of a free lane
    (free_lane() of assay.loop.gate), it is the holder of the tree of that
    lane (the holder, the lanes).  The holder takes the requests in the
    sequence of their numbers.

    A landing of an earlier copy of this module writes no request.  It waits
    for the lock of the first lane with flock(2), and it runs alone when it
    gets the lock (the lanes of assay.loop.gate).

Lanes
    Up to LANES holders run at the same time, each in the warm tree of
    its lane (the lanes of assay.loop.gate).  A holder claims each request of
    its batch with an exclusive lock of flock(2) on the file NUMBER.claim of
    the queue, with no wait (Claims.take()).  A different holder takes no request
    with a claim, so no request lands two times.  The kernel frees the claims
    of a holder that ends.  The holder frees the claim of a request that
    waits for the next batch (the batch) at the cut, and each other claim at
    the end of its batch.  A holder of a different lane can then take a
    request that waits for the next batch while the search of the first batch
    runs.  The holder gives no verdict to such a request, so the two holders
    never give one request two verdicts.

    Two lanes race only at the compare-and-swap.  The lane that loses
    composes its requests again on the new commit of the branch, and its gate
    runs again (the compare-and-swap).  A batch that loses many times takes
    the right of way, and the other lanes wait for it (the right of way).
    the reduce and the built files of a lane fold only into the commit of
    that lane.

    The request file of this copy holds the field claims.  An earlier copy of
    this module claims no request.  The holder of a lane after the first one
    then takes no batch while a request of an earlier copy waits.

Holder
    The holder of the tree takes the requests of the queue in the sequence
    of the numbers.  It takes each request with no verdict, with no claim of
    a different holder, and with a process that holds its lock (the lanes).  A request with a lock and no request file
    can come next.  The holder waits INCOMPLETE_S seconds for its request
    file, and then it takes the requests after it.  It takes the requests with
    the git directory and the branch of the first one, and the others wait
    for the next holder.  The batch reads and moves the branch, and runs the
    gate, through the shared git directory, and not through the work tree of
    a request.

    The holder lands the batch (the batch).  It writes the verdict
    of a request only while the process of the request holds its lock.  The
    holder keeps the number of each request with a verdict, and it gives no
    request a second verdict.

    While it lands the batch, it holds the session of the gate.  That session
    goes before each other heavy step of the host (the priority of the gate
    of assay.loop.heavy).  --check holds that session too.  Then it frees the
    lock.  It does that again while its own request has no verdict.  An
    error outside a run of the gate on a commit of the batch stops the batch.
    An error of the run of the base is such an error (the error of a run).
    Each request of the batch with no verdict then gets the verdict of that
    error, other than a request that landed.  A LandError gives a usage
    error.  Each other error gives a tool error.  An error that is not of the
    types of KNOWN_ERRORS is a defect of the code of the landing, such as a
    TypeError.  Its verdict also gives its traceback.  A request with a
    verdict waits for no holder, so the next holder does not take the batch
    again.  An error of the read of the queue, before the holder knows its
    batch, gives each live request of the queue the verdict of that error
    (refused_queue()).  The request file of each request gives the token of
    its verdict.  When no verdict can be written, the holder ends on the
    error.

Batch
    The batch composes each request as its own commit on the commit of the
    request before it.  A request that does not compose on them gets a
    refusal for that reason only.  An example is an own path with a file that
    an earlier request of the batch changes (the tree).  The first
    composition also cuts the batch.  A request that changes a C++ file that
    an earlier request of the batch changes waits for the next batch, with
    each request after it.

    The gate judges the full change of the batch.  A counted ledger, such as
    the ledger of the rule quarantine, keys each count on its file.  With no
    C++ file in two requests, the judgment of the batch is the judgment of
    each request alone.  Then a fall of one request in a file cannot pay for
    a rise of a different request in that file.

    The gate builds and tests the last commit one time (the gate).  The
    candidate of the gate is that commit, with the commit that the batch
    started from and the changed paths of each request.  The gate then runs
    each configuration that one or more of the requests selects.  The batch
    runs the gate with the smallest --jobs of its requests, and with
    --all-configurations when one request gives it.  When the gate accepts
    the last commit, one compare-and-swap moves the branch to the commit of
    the gate (the compare-and-swap).

Search
    When the gate rejects the last commit, a binary search finds each request
    that the gate rejects.  The batch keeps an accepted part: the first
    requests, in a chain that the gate accepted as a unit.  It also keeps the
    length of the smallest red part of the other requests.  Each run of the
    gate tests the accepted part and the first half of that red part.  An
    accepted run adds that half to the accepted part.

    A rejected run makes that half the red part.  A red part of one request
    is red on the accepted part.  the confirmation then runs its tree again
    before a refusal.  The other requests then compose again on the
    accepted part, and the gate tests them as a unit.  When only an
    interaction of two requests is red, the refusal goes to the second of the
    two.

    At the end, the compare-and-swap moves the branch to the accepted part.
    The branch then holds no commit that the gate did not accept as a unit.
    Each run after the first run of a batch runs `loop test` with the inputs
    that changed (assay.loop.gate).

Confirmation
    One red run does not refuse a request, because a flaky test or a
    transient error of a tool can make a correct request red.  Before a
    refusal, the gate runs the red tree again: the request on the accepted
    part (confirmed()).  Only a tree that is red two times refuses its
    request.
      * A green run tells that the problems of the red run did not repeat:
        they were noise.  The request joins the accepted part with the green
        run.
      * A red run refuses the request with the problems of that run.  A test
        that is red alone too in the two runs then refuses the request.
    A run of a different tree cannot show that a problem repeats.  An
    example is a request that needs an accepted request of the batch, such
    as a test of a module of that request.

    When the accepted part holds requests, the gate then also runs the
    request alone before its refusal: its commit on the commit of the batch,
    with no other request, and with the focus of the second red run
    (assay.loop.gate, the run of the base).  That run only gives the first
    line of the refusal: red alone too, or green alone and red after the
    accepted requests, which is an interaction.  A request that does not
    compose alone on the commit of the batch, such as an edit of a text that
    an accepted request writes, gets no run alone.

    A problem that a green run of the same tree did not repeat is a flaky
    test or a transient error.  The verdict of the request names it with
    the line FLAKY_MARK (the flaky tests).  No retry hides it.

    The confirmation adds one run of the gate to each refusal, and one
    focused run when the accepted part holds requests.  Each run of the
    confirmation is a retry, so `loop test` runs only the tests with an
    input that changed or with no pass, such as the red tests.

Run of the base
    The first run of a batch can be red, also for a batch of one request.
    The gate then runs on the commit of the batch with no request, with the
    changed paths of the batch.  The verdict of the first run gives the
    focus of each configuration with a problem.  The focus of a
    configuration holds the tests that were red alone too, or no test for a
    problem of a different kind.  The run of the base runs only that focus
    (the run of the base of assay.loop.gate).  It does not run each
    configuration of the batch.  It runs only the red tests of a
    configuration, with `loop test --only`, and each configuration with a
    different problem in the mode of the changed inputs.

    A verdict with no focus, such as the verdict of a planted gate, gives a
    run of the base of each configuration of the batch.  The run of the
    base is a retry.  `loop test` then runs only the tests with an input
    that changed.  When that run is red too, the branch is red with no
    request.

    The batch compares the failing items of two runs, not their problems
    (the failing items of assay.loop.gate).  An item of a red test gives the
    test, and the rule, the path and the line of each finding.  Two runs
    can fail one test of a rule on different files, with problems of the
    same words.  An item can name the directory of the logs of its run.
    The batch compares the items with LOGS_MARK as the replacement of that
    directory.  When the first run of the batch has each item of the run of
    the base, no request repairs the branch.  Each request then gets the
    refusal with the problems of the two runs, and the search does not run.
    Else a request can repair the branch, and the search runs.  A refusal
    with only items of the run of the base tells that the branch is red.

    A search on a red branch runs the gate approximately two times for each
    request.  An example is a batch of three requests with one red request.
    With the confirmation, the batch runs the gate 5 times for the first red
    request, 7 times for the second and 6 times for the last.  With no red
    request, the batch runs the gate one time.  On a red branch, it runs the
    gate 2 times.  One red request alone runs the gate 3 times.

Error of a run
    The check of the gate can raise an error on a commit of the batch.  An
    example is a file of the commit that the code of the gate cannot read.
    The error can be of each type, also a TypeError of a file of an
    unexpected shape.  The run then gets a verdict with one problem, the
    text of the error, and with no focus (gate_run()).  The run of the base then runs each
    configuration of the batch on the commit of the batch.  When that run
    raises an error too, the error is not of a request, and each request
    gets the verdict of a tool error (the holder).  Else the search finds the
    request that gives the error, that request gets the refusal with its
    text, and the others land.

    A run of the gate with no error has no added step.  An error of a run
    after the run of the base is a problem of its commit too.  A transient
    error then counts as a red run, as a flaky test does, and the
    confirmation judges it.

Flaky tests
    The gate runs the red tests of a configuration again, together in one run
    of ctest apart from the suite (assay.loop.gate, the run of a
    CONFIGURATION).  A test that is green in that run does not reject the
    commit.  Such a test is a flaky test, and no retry hides it.  The verdict of each
    request that lands, or that --check accepts, names each flaky test of
    each accepted run of the gate that holds the request, with the line
    FLAKY_MARK.  The record of a run gives its flaky tests too.  The verdict
    also names each problem that the confirmation judged noise.

Gate
    The gate builds and tests a commit in the warm tree of each configuration
    that the change selects (assay.loop.gate).  The gate has no base side.  A
    test that is red alone on HEAD then rejects each landing.  When the gate
    builds each of its configurations, it writes the falls of the counted
    ledgers into a new commit (the reduce of assay.loop.gate).  That commit
    has the parents and the message of the last commit of the batch.  The
    gate also writes each file that the merge names for it, and each built
    file that the change can change, into that commit (the built files of
    assay.loop.gate).  The message then names each such file.  The candidate
    names such a file of each link of the batch, so the last commit holds
    the written file.

    The verdict gives it, and the compare-and-swap moves the branch to it.
    Each run of the gate writes its logs into a directory of its own
    (assay.loop.gate, the logs).  The problems of a refusal then name logs
    that the next run does not change.  After the problems, a refusal gives
    the rows of the ratchet ledgers that the build gives (the ratchet rows of
    assay.loop.gate).

Budget reason
    A correct change that crosses a row of a budget raises the row with its
    measured value and a reason (the owner decision of 2026-11-15).  With
    --budget-reason TEXT, the gate makes that raise in the run of the
    request, and it folds the raised ledgers into the last commit (the raise
    of a budget of assay.loop.gate).  The request file holds the reason
    (encoded_request()).  The candidate of a chain gets a reason only when
    each request of the chain gives one, and the reason of a chain of more
    requests holds the reason of each (chain_budget_reason()).  A request
    with no such reason then never gets a raise from the reason of a
    different request.  The verdict of each request of the run names each
    raised row with its old and its new value.  A row of a rule of
    correctness never rises this way, so the gate refuses each other
    problem as before.  The text cannot hold '|' or a new line, because it
    goes into the reason cell of a row.
    The option can occur more than one time.  A value ITEM=TEXT gives the
    text of the crossed row of ITEM, and ITEM holds no space and no '='
    (budget_reasons_of()).  ITEM is the path of the row, its path with no
    @build/ mark, or its entity.  One value with no ITEM gives the text of
    each other crossed row.  A request with only ITEM=TEXT values gives no
    text to a row that it does not name, so such a crossed row refuses the
    request.  An ITEM that names no crossed row refuses it too, with a line
    that names the item (the raise of a budget of assay.loop.gate).  The
    text of an item in a chain holds the text of each request for the
    item: its own text, else its plain text (chain_budget_rows()).  A
    request file of an earlier copy holds no such row.

Lower
    A row of a budget goes down only by an explicit write of the named row,
    with the reason of the cut (the lower of
    assay.guard.engines.buildsys.cost_budgets).  With --lower RULE ITEM and
    --lower-reason TEXT, the gate makes that write in the run of the request
    on the build of each of its configurations, and it folds the lowered
    ledgers into the last commit (the lower of a budget of assay.loop.gate).
    The option --lower can occur more than one time, and RULE is a rule of
    LOWER_RULES of assay.loop.gate.  The request file holds the rows and the
    reason.  Each row of a chain takes the reason of its own request
    (chain_lowers()), and a candidate with a lower runs each configuration.
    The verdict of each request of the run names each lowered row with its
    old and its new value, or the line of a lower that wrote nothing.  The
    text of the reason obeys the rules of the budget reason.

Compare-and-swap
    The branch moves to the commit only when the branch holds the commit that
    the batch started from (`git update-ref REF NEW OLD`).  A different
    commit can land first, such as a commit of a landing of an earlier copy
    of this module.  The batch then composes its accepted requests again on
    the new commit of the branch, after a random pause, and the gate tests
    them again.  After ATTEMPTS refusals, each of them gets the refusal.  A
    landing then cannot revert a different commit.

    Before the swap, the holder drops each accepted request with a process
    that left the queue.  An interrupt of a landing then cancels it, and the
    others compose again and go to the gate again.  The holder keeps the
    lock of the warm tree for all of the batch.

Store
    A stop of the host can damage the git store of the repository: a file
    config with no bytes, or an object with no bytes.  Before each batch,
    the holder reads the store (store_problem()).  The file config must have
    bytes, and `git fsck --connectivity-only --no-reflogs` must exit 0.  A
    damaged store refuses each request of the batch with the reason, and the
    branch does not move.  The check reads no reflog: a rebase in a
    different work tree writes the reflog of its branch, and a check that
    reads it at that moment can exit 2 for a store with no damage.  The
    check also runs STORE_TRIES times, STORE_RETRY_S seconds apart, before a
    refusal, so a ref that a different tree writes at that moment refuses
    no request.  The refusal names each ref of the error of git.

Right of way
    A long run of the gate, such as a run of --all-configurations, can lose
    each swap: a short landing of a different lane moves the branch while it
    runs.  Thus, after RIGHT_AFTER refused swaps, the batch takes the right
    of way before its next composition.  It holds a lock of flock(2) on the
    file NUMBER.right of the queue, with the number of its first request,
    until the batch ends.  The kernel frees the lock of a holder that ends.
    The file of a holder that a signal ended stays until the sweep of the
    queue removes it (removed_dead_holders()).

    A right of way goes first when a different batch holds it.  For a batch
    that holds a right of way, only a right of way with a smaller number goes
    first.  Thus the oldest batch goes first, and no two batches wait for
    each other.  A right of way does not go first when its holder is below
    the batch in the stack, or when the entry of its holder is not on the
    branch (the stack), because such a holder cannot swap first.  A batch
    waits while a right of way that goes first is held, at two points:
      * Before each round, also the first one.  A holder that goes first
        moves the branch when it lands, and the swap of a run that started
        before it then cannot succeed.  Such a run only takes slots and
        memory from the run of the holder.  Thus the batch composes after
        the wait, on the commit that the last such holder leaves.  A move of
        the branch does not end this wait.
      * Before each swap.  A batch can take its right of way while a
        different batch runs.  When the branch moves from the base of the
        waiting batch, its swap cannot succeed, so this wait ends at once.
        The batch counts a refused swap, and it composes again.
    The wait looks again each POLL_S seconds, for at most RIGHT_WAIT_S
    seconds.  Each line of the wait names the holders that no earlier line
    of that wait named.  At its end, a wait before a round writes the
    seconds that it waited.

Stack
    Two batches that run at the same time on one commit of the branch lose
    one run: the first one lands, and the other one runs again.  Thus a
    batch composes on the commit that a running batch tests, and not only on
    the branch.  This is a dependent pipeline.  Each batch writes its entry
    into the file NUMBER.stack of the queue: its branch, its base, the commit
    that it tests, and if the gate accepted that commit.  One queue holds the
    batches of each branch, and a batch reads only the entries of its own
    branch.  The batch holds a lock of
    flock(2) on the file NUMBER.stacked while it has an entry.  The kernel
    frees the lock of a holder that ends, and a file with no lock is no
    entry.  The sweep of the queue removes the files of such a holder
    (removed_dead_holders()).
      * At the start of each round, the batch removes its entry.  The stack
        starts at the commit of the branch.  It follows each entry on the
        top of the stack, for at most STACK_DEPTH entries.  Among the entries
        on one commit, it takes the entry that lands first: a holder of a
        right of way before a batch with none, then the smaller number.
      * When the stack holds STACK_DEPTH entries, the batch waits before its
        run.  A run on a different base would lose its swap.
      * The line.  The batches that wait for a place of the stack go in the
        sequence of their entry into the queue: the smaller number first.
        Before its run, a batch that can stack writes its mark into the file
        NUMBER.line of the queue, with its branch, under the lock of the
        file NUMBER.stacked (Stack.join_line()).  It waits while the live
        marks of older batches on its branch are as many as the free places
        of the stack, or more.  The mark stays until the first run of the
        round writes the entry (Stack.publish()), or the round ends with no
        entry (Stack.withdraw()).  Thus no younger batch takes the place
        that an older batch takes.  A batch that waits for the verdicts of
        the rule of one lost run, or whose wait gets to STACK_WAIT_S, has no
        mark.  On 2026-11-21 the first look after a landing took the free
        place, also when an older batch waited for it.
      * The round composes on the top of the stack.  The first run of a
        round writes the entry with the commit of that run.  A red first run
        removes the entry, because the search then lands a different commit.
        An accepted run writes the final commit, with the mark accepted.
      * A run on the stack names each built file to the gate (the built
        files of assay.loop.gate).  A batch below can change a built file,
        such as the mint inventory, and the commit that it tests holds the
        file before its fold.  The gate then writes each built file from
        the build of the run.
      * When the gate folds the built files into a new commit, before the
        tests, the batch writes that commit into its entry
        (Candidate.on_fold).  A batch that starts after that write stacks on
        the commit that lands, if no later fold of the ledgers changes it.
      * A request of the run can leave the queue while the gate runs, such
        as a request that its agent cancels.  The commit of the entry holds
        its change, so that commit can never land.  A thread of the holder
        looks at the process of each request of the run each WATCH_S
        seconds, and it frees the entry at once when one left: the files
        NUMBER.stack and NUMBER.stacked go (gone_watch()).  A batch above
        then does not wait on the entry until the run ends.  On 2026-11-21
        such an entry held the next batch 260 s.
    A batch swaps only when the branch is at its base, with the same
    compare-and-swap.  Before the swap, it waits while its base is the top
    of a path of entries from the branch.  When a batch below lands a
    different commit, or when its entry leaves, the base can never be the
    branch.  An outside push of the branch has the same effect.  The batch
    then composes again on the new top of the stack.
    A fold below gives a different final commit, because the reduce and
    the built files change only ledgers and built files.  The batch then
    composes again on that final commit.  Its next run runs only the tests
    with an input that changed, with the same selection as each run after
    the first one (assay.loop.affected).  Each other result of the batch did
    not read a changed input.  Thus each result that the batch keeps is a
    result on its exact base.
    A round on the stack gives no verdict, and it runs no run of the base
    and no search, because a red can come from a batch below.  After a red
    run, the batch waits until the branch is at its base, or until its base
    leaves the stack.  When the branch is at its base, the next round
    composes on the branch, and its search judges the batch on its exact
    base.  A request that does not compose on the stack also gives a next
    round on the branch.  A round that composes again for the stack counts
    in STACK_ATTEMPTS, and not in the refused swaps of the right of way.
    After STACK_ATTEMPTS such rounds, the batch composes only on the branch.
    After a wait for the stack that gets to STACK_WAIT_S, the next round
    composes on the branch too.
      * the rule of one lost run.  The first round of a batch can compose on
        an entry with no verdict, so its run overlaps the run below it.  A
        run is lost when it is red on the stack, or when its base leaves the
        stack (StackRule).  After the first lost run, the batch can stack at
        once one more time on an entry with no verdict, but only on an entry
        of an older batch.  The round composes on the stack up to the first
        entry with no verdict of a younger batch.  On 2026-11-21 a batch
        that lost its run landed at 17:48, and a run at once on the open
        entry of the older batch below it would have landed it at about
        17:16.  After the second lost run, the batch stacks only on
        entries that the gate accepted.  Before such a round, the batch
        waits until each entry of the stack with no verdict at the first
        look of the wait has the mark accepted or has left.  The batch has
        no entry while it waits, so an entry that comes into the stack
        during the wait composed after it, and it does not hold the wait.
        On 2026-11-21, a batch waited 2400 s for five entries in turn, and
        four of them came into the stack during its wait, because each new
        first run of a different lane went in front of it.  The round then
        composes on the accepted part of the stack (settled_part()).  An
        accepted entry changes its commit only when its swap is refused, or
        when the batch below it lands a different commit.  Thus a run that
        a recomposition below can waste is the first two runs of a batch,
        and not one run for each new commit of a batch below.  The rule only
        selects a base: the swap is the same compare-and-swap, so no landing
        reverts a different commit.
    A batch that holds a right of way composes only on the branch.  Thus it
    never waits for a batch below it, and the batches below it never wait
    for its right of way while it waits for them.  It goes first on the
    branch, as the right of way tells.
    --check composes on the branch, and it writes no entry.  The stack adds
    no lane and no job: the lanes and the slot pool stay as they are.

Journal
    Before the swap, the holder writes the file NUMBER.journal of each
    accepted request.  The file holds the token, the branch, the commit of
    the swap and the verdict of the request.  After the swap, the holder
    writes each verdict.  When the swap does not occur, it removes the
    journals.

    A holder can end between the swap and the last verdict.  The next holder
    then finds a request with a journal and no verdict.  When the branch
    holds the commit of the journal, that holder gives the work tree the
    update of the journal and writes its verdict.  It does not compose the
    request again.  Else it removes the journal, and it takes the request.
    When git cannot tell if the branch holds that commit, the request gets
    the refusal of a tool error with the reason.  The commit can then be on
    the branch.

Index and working tree
    After the swap, the holder gives the work tree of each landed request its
    update.  Then it writes each verdict.  It does that before it frees the
    lock of the warm tree.  A landing that composes after it then reads a
    working tree that holds each landed change.  A landing of an earlier copy
    of this module reads that working tree too.  An error in a work tree
    gives a line of the holder, and it cannot change a verdict.

    The process of each landed request gives its own work tree the same
    update after it reads its verdict, and it writes the warnings.  An update
    that the work tree holds changes nothing.  The update also comes after a
    holder that ended after its swap (the journal).  The verdict gives the
    update: the landed commit, the paths, and the commit that the batch
    started from.  Each update holds a lock of flock(2) on the file
    UPDATE_LOCK of the git directory of its work tree.  Two updates of one
    work tree then do not write its index at one time.

    The update reads the copies of the commits, and no patch (synced_paths()).
    The target of a path is its copy in the landed commit, or in the commit
    of the branch when a later landing holds the landed commit.  Each path
    of the update gets the entry of the target in the index.  The
    working-tree copy of the path then gets the bytes of the target, when the
    copy is the copy of the index before the update or of the commit that the
    batch started from.  A copy that holds a local edit merges with the
    target by its lines, with the entry of the index as the base.  When the
    lines conflict, the edits of the request go to a shared file of an edit.
    A copy that holds the target changes nothing.  So a patch that the merge
    composed, a fold of the reduce next to a hunk, and a later request of the
    batch give no warning.

    A local edit that does not merge gives a warning, and the copy keeps the
    edit.  The owner must then give it the change by hand before the next
    commit of that file, or that commit reverts it.  An error of git or of a
    file also gives a warning, because the commit is on the branch.  When
    HEAD of the work tree is not on the branch of the request, the work tree
    gets no update.

Lock of a heavy step
    The gate takes a lease of the slot pool for each configuration, and a
    lease holds the lock of the heavy steps in the shared mode
    (assay.loop.heavy).  A landing in an exclusive heavy step then waits for
    its own lock.  The command refuses to run when its environment has the variable
    STEP_VARIABLE of assay.loop.heavy.  It also refuses to run when it has an
    open descriptor of the lock file, as flock(1) gives its command.

Record
    Each run of the gate adds one line of JSON to the file RECORDS of the
    work directory.  The line holds the time, the commits, the configurations
    and the problems.  It holds the subject and the result of each request of
    the run: landed, refused, accepted, rejected or dropped.  A run of the
    base holds no request.  It holds the commit that the gate judged last and
    the ledgers that it wrote.

    Each request of the line also holds the field queued: the time of the
    entry of the request into the queue, as ISO 8601 text in UTC.  The
    request file holds that time from its write (entered()).  The time from
    the queue to main is then the time of the line that lands the request
    less that entry time, with no sampler outside the gate.  A request file
    of an earlier copy holds no such time, and its request gets "".  The
    request of --check gets the start of its call.  A reader of the records
    reads a request with no field queued as a request with no entry time.

    It also holds the wall time, the time of each configuration in its heavy
    step and the logs.  It holds the focus of each configuration with a
    problem too.  The work directory is the directory land of the work root
    of assay.core.cache.

Driver
    A landing runs the copy of this module in the main work tree of the
    repository, which holds main.  A different copy runs the copy of the
    main work tree with the same words.  An example is the copy of a work
    tree with a change of this module.  A change of the gate then cannot make
    the gate of its own landing weaker.  --here runs this copy, and it
    applies only to --check.

    --dry-run and --tree land nothing.  They run this copy, with no gate.
    --check does not go into the queue.  It waits for the lock of the warm
    tree and runs alone.

Usage
    loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... [--block FILE AFTER BLOCK]...
              [--jobs N] [--all-configurations] [--budget-reason TEXT | ITEM=TEXT]... [--lower RULE ITEM]...
              [--lower-reason TEXT] [--check [--here] | --dry-run | --tree] -- [PATH]...
    loop land --series BRANCH [--jobs N] [--all-configurations] [--budget-reason TEXT | ITEM=TEXT]...
              [--lower RULE ITEM]... [--lower-reason TEXT] [--check [--here] | --dry-run | --tree] --

    Exit 0 when the request landed, when the gate accepts the commit of
    --check, or when --dry-run or --tree printed.  Exit 1 when the gate
    rejects the request.  Exit 2 on a usage error, or on a request that does
    not compose.  Exit 4 when a tool fails.

`guard selftest tests.loop.test_land` does a test of the module on a planted repository.
"""

import contextlib
import enum
import fcntl
import functools
import hashlib
import json
import os
import random
import re
import secrets
import sys
import tempfile
import threading
import time
import traceback
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import asdict, dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import cache, git, paths, proc, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import gate, heavy
from assay.loop import history as loop_history
from assay.loop import inputs as guard_inputs
from assay.loop.derive import witness_fixtures

# The refusals of the compare-and-swap before the batch stops, and the range of the random pause after one (the
# compare-and-swap).  With many lanes, a short landing moves the branch in each long run of the gate.  A run after a
# refused swap tests only the inputs that changed, so it is short, and a long landing needs many tries before it wins.
ATTEMPTS = 32
PAUSE_RANGE_S = (0.1, 0.9)
# The refused swaps before a batch takes the right of way, and the longest wait of a swap for a right of way of a
# different batch (the right of way).  A run of --all-configurations takes approximately 12 minutes.
RIGHT_AFTER = 2
RIGHT_WAIT_S = 1800.0
# The two steps of a batch before which it waits for a right of way that goes first (the right of way).
RUN_STEP = "run"
SWAP_STEP = "swap"
# The most entries below a batch in the stack, the wait for the batches below a batch, in seconds, and the times that a
# batch composes again for the stack before it composes only on the branch (the stack).
STACK_DEPTH = 3
STACK_WAIT_S = 3600.0
STACK_ATTEMPTS = 8
# The two kinds of an edit of a shared file (the request).
INSERT = "insert"
REPLACE = "replace"
# The count of the values of each option, for the division of the words (split_words()).
VALUE_COUNTS = {
    "-m": 1,
    "--patch": 1,
    "--replace": 3,
    "--block": 3,
    "--jobs": 1,
    "--series": 1,
    "--budget-reason": 1,
    "--lower": 2,
    "--lower-reason": 1,
}
FLAG_DRY_RUN = "--dry-run"
FLAG_TREE = "--tree"
FLAG_CHECK = "--check"
FLAG_HERE = "--here"
FLAG_EVERY = "--all-configurations"
FLAGS = (FLAG_DRY_RUN, FLAG_TREE, FLAG_CHECK, FLAG_HERE, FLAG_EVERY)
END_OF_OPTIONS = "--"
HELP_WORDS = (["-h"], ["--help"])
USAGE = (
    "usage: loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... "
    "[--block FILE AFTER BLOCK]...\n"
    "                 [--jobs N] [--all-configurations] [--budget-reason TEXT | ITEM=TEXT]... "
    "[--lower RULE ITEM]... [--lower-reason TEXT]\n"
    "                 [--check [--here] | --dry-run | --tree] -- [PATH]...\n"
    "       loop land --series BRANCH [--jobs N] [--all-configurations] [--budget-reason TEXT | ITEM=TEXT]... "
    "[--lower RULE ITEM]...\n"
    "                 [--lower-reason TEXT] [--check [--here] | --dry-run | --tree] --"
)
# The subject of the commit of --check with no message.
CHECK_SUBJECT = "Check a change in the gate"
# The length of a commit in a line of the landing.
SHORT_HASH = 9
# The work directory of the gate, in the work root, and the file of the records in it (the record).
WORK_NAME = "land"
RECORDS = "records.jsonl"
# The directory of the queues in the work directory, and the length of the key of the queue of a repository (the
# queue).
QUEUE = "queue"
KEY_LENGTH = 16
# The file of the next number in a queue, and the new lock of a reservation (the queue).
NEXT = "next"
RESERVE = ".reserve"
# The files of one request in its queue (the queue, the journal).
NUMBER_WIDTH = 8
LOCK_SUFFIX = ".lock"
REQUEST_SUFFIX = ".request"
# The request file of a series (the series).  An earlier copy of this module reads only REQUEST_SUFFIX, so it takes no
# series, and it cannot land a series as one commit.
SERIES_SUFFIX = ".series"
VERDICT_SUFFIX = ".verdict"
JOURNAL_SUFFIX = ".journal"
CLAIM_SUFFIX = ".claim"
RIGHT_SUFFIX = ".right"
# The entry of a batch in the stack, its lock file, and the mark of a batch in the line of the full stack (the stack).
# No suffix ends with LOCK_SUFFIX, because each such file is the lock of a request.
STACK_SUFFIX = ".stack"
STACK_LOCK_SUFFIX = ".stacked"
LINE_SUFFIX = ".line"
# The age in seconds of a file of the right of way or of the stack with no lock, after which the sweep removes it
# (removed_dead_holders()).  A holder makes the file before it takes the lock, also on a stalled host, so a young file
# with no lock can be the file of a live holder.  On 2026-11-19 the queue held such files of dead holders from
# 2026-11-15 on.
DEAD_HOLDER_AGE_S = 3600.0
# The lines of the error of `git fsck` that the refusal of a damaged store names, the runs of the check before a
# refusal, the pause between two runs in seconds, and a ref in the error (the store).  On 2026-11-21, a rebase in a
# different work tree made one check exit 2 for a reflog entry of its branch, and a check some minutes later exited 0.
STORE_LINES = 3
STORE_TRIES = 3
STORE_RETRY_S = 5.0
STORE_REF = re.compile(r"\brefs/[^\s:]+")
PATCH_MARK = ".patch-"
REQUEST_FORMAT = 2
# The bytes of the random token of a request (the queue).
TOKEN_BYTES = 8
# The interval of the wait for a verdict or for the lock, in seconds (the wait).  The wait for the request file of a
# request with a lock, and its interval, in seconds (the holder).
POLL_S = 2.0
# The interval of the look of a holder at the processes of the requests of its run, in seconds (the stack).
WATCH_S = POLL_S
INCOMPLETE_S = 5.0
INCOMPLETE_POLL_S = 0.1
# The lock file of the update of a work tree, in its git directory (the index and the working tree).
UPDATE_LOCK = "loop-land.lock"
# The directory of the open descriptors of this process (the lock of a heavy step).
DESCRIPTORS = Path("/proc/self/fd")
# The copy of this module in a tree, and the master that runs it (the driver).
MODULE_PATH = Path("utils/assay/loop/land.py")
MASTER_PATH = Path("utils/bin/loop")
# The limit of a landing that the driver runs from the main work tree, in seconds.
DRIVER_S = 8 * proc.BUILD_S
# The results of a request in one run of the gate (the record).
LANDED = "landed"
REFUSED = "refused"
ACCEPTED = "accepted"
REJECTED = "rejected"
DROPPED = "dropped"
# The replacement of the directory of the logs of a run, when the batch compares the problems of two runs (the run of
# the base).
LOGS_MARK = "LOGS"
# The name of the directory of a run of the gate in the text of a verdict (the logs of assay.loop.gate).
CITED_RUN = re.compile(rf"{gate.RUN_MARK}\d{{{gate.RUN_WIDTH}}}")
# The start of a line of the verdict of a request that names a flaky test (the flaky tests).
FLAKY_MARK = "loop land: FLAKY, "
# The directory of the ledgers and their suffix (the merge).
LEDGER_PREFIX = "utils/ledgers/"
LEDGER_SUFFIX = ".txt"
# The first words of a row of the registry, whose first two words are its key (the merge).
REGISTRY_ROW_WORDS = ("rule", "path")
# The files of the code of the writer of the witness fixtures, which the merge compares with this copy (the merge).
WITNESS_WRITER = (
    "utils/assay/loop/derive/witness_fixtures.py",
    "utils/assay/core/ruletable.py",
    "utils/assay/core/rows.py",
)
# The mode of an executable file, and the modes whose bytes the update of a working tree writes (the index and the
# working tree).
EXECUTABLE_MODE = "100755"
FILE_MODES = (git.PLAIN_FILE_MODE, EXECUTABLE_MODE)


class LandError(Exception):
    """The request does not compose: a usage error, an edit that does not apply, or a path that is not listed."""


class HeldPatch(enum.Enum):
    """What a patch is when the composition holds each of its files as the patch gives it (the merge).

    LANDED is the patch of a request of --patch: its commit landed already, and it changes nothing.  SECOND is a commit
    of a series: the series refuses a commit that HEAD holds (the series), so the patch is a second change of each count
    that it changes.
    """

    LANDED = "landed"
    SECOND = "second"


# The errors of a tool or of an input that the landing expects (the holder).  An error of a different type is a defect
# of the code of the landing, and its verdict also gives its type and its traceback.
KNOWN_ERRORS = (LandError, gate.GateError, git.GitError, proc.ToolFailed, OSError, ValueError)


@dataclass(frozen=True, slots=True)
class Edit:
    """One edit of a shared file: the path from the top of the repository, the kind, and the two texts."""

    path: str
    kind: str
    first: str
    second: str


@dataclass(frozen=True, slots=True)
class Part:
    """One commit of a series: its message, the patch of the commit against its first parent, and the commit (the
    series)."""

    message: str
    patch: Path
    commit: str

    @property
    def subject(self) -> str:
        """The first line of the message."""
        return self.message.partition("\n")[0]


@dataclass(frozen=True, slots=True)
class Request:
    """The message, the patches, the edits and the own paths of one landing (the request).

    `parts` holds each commit of a series, from the oldest (the series).  A series has no message, patch, edit or own
    path of its own.
    """

    messages: tuple[str, ...]
    patches: tuple[Path, ...]
    edits: tuple[Edit, ...]
    paths: tuple[str, ...]
    parts: tuple[Part, ...] = ()

    @property
    def message(self) -> str:
        """The message of the commit: the paragraphs with one empty line between two of them, and a newline at the end."""
        return "\n\n".join(self.messages or (CHECK_SUBJECT,)) + "\n"

    @property
    def subject(self) -> str:
        """The first paragraph of the message, or the subject of the first commit of a series and the count of the others."""
        if self.parts:
            more = f", and {len(self.parts) - 1} more commit(s) of the series" if len(self.parts) > 1 else ""
            return f"{self.parts[0].subject}{more}"
        return self.messages[0] if self.messages else CHECK_SUBJECT

    @property
    def is_empty(self) -> bool:
        """Tell if the request holds no patch, no edit, no own path and no commit of a series."""
        return not self.paths and not self.edits and not self.patches and not self.parts


@dataclass(frozen=True, slots=True)
class Options:
    """The words of the command line that are not part of the request."""

    is_dry_run: bool
    is_tree_only: bool
    is_check: bool = False
    is_here: bool = False
    is_every: bool = False
    jobs: int = proc.DEFAULT_JOBS
    series: str = ""
    budget_reason: str = ""
    lowers: tuple[tuple[str, str], ...] = ()
    lower_reason: str = ""
    budget_rows: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True, slots=True)
class Composed:
    """The result of one composition: the commit that it starts from, the tree, the changed paths, and the diff.

    `merged` holds each path that the merge composed, and `is_summed` tells that it summed two changes of one count.
    `built` holds each merged file of BUILT_FILES of assay.loop.gate, which the gate writes (the merge).
    """

    head: str
    tree: str
    changed: tuple[str, ...]
    diff: str
    merged: tuple[str, ...] = ()
    is_summed: bool = False
    built: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class PatchMerge:
    """The result of the merge of one patch: the entry of each path that it gives, and the paths that it merged.

    An entry of None removes its path.  `is_summed` tells that a ledger summed two changes of one count.  `built` holds
    each merged file of BUILT_FILES of assay.loop.gate, which keeps the copy of the composition and gets no entry.
    """

    entries: tuple[tuple[git.TreeEntry | None, str], ...]
    merged: tuple[str, ...]
    is_summed: bool
    built: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Queued:
    """One request of a batch: its number in the queue, the work tree of its process, the request and its options.

    `branch` is the branch of HEAD of the work tree, and `common` is its shared git directory (the holder).  `token` is
    the random token of the request, and its verdict holds the same token (the queue).  `head` is the commit of HEAD of
    the work tree at the request, or "" (the tree).  `is_claiming` tells that the process of the request runs a copy of
    this module that claims each request that it takes (the lanes).  `entry_time` is the time of the entry of the
    request into the queue, as ISO 8601 text in UTC, or "" for a request file of an earlier copy (the record).
    """

    number: int
    root: Path
    request: Request
    options: Options
    branch: str = ""
    common: str = ""
    token: str = ""
    head: str = ""
    is_claiming: bool = False
    entry_time: str = ""

    @property
    def name(self) -> str:
        """The name of the files of the request in the queue: the number in NUMBER_WIDTH digits."""
        return f"{self.number:0{NUMBER_WIDTH}d}"


@dataclass(frozen=True, slots=True)
class Link:
    """One request composed on the commit before it: the request, that commit, its own commit, and its paths.

    `merged`, `is_summed` and `built` come from its composition (the merge).  `commits` holds each commit of a series
    with its subject, from the oldest, and `commit` is the last one of them (the series).
    """

    queued: Queued
    parent: str
    commit: str
    changed: tuple[str, ...]
    merged: tuple[str, ...] = ()
    is_summed: bool = False
    built: tuple[str, ...] = ()
    commits: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True, slots=True)
class Line:
    """One line that the process of a request writes: the content, and True for the standard error."""

    content: str
    is_error: bool = False


@dataclass(frozen=True, slots=True)
class Update:
    """The update of the work tree of a landed request (the index and the working tree).

    The index gets the entry of the commit `index_commit` for each path of `index_paths`, or of a later commit of the
    branch.  `base` is the commit that the batch started from, or "".  `falls` holds the commit that the gate tested
    and the commit that landed, or nothing, and `ledgers` holds the ledgers of the reduce.  This module reads no
    falls, and a landing of an earlier copy reads them.
    """

    index_commit: str
    index_paths: tuple[str, ...]
    falls: tuple[str, ...] = ()
    ledgers: tuple[str, ...] = ()
    base: str = ""


@dataclass(frozen=True, slots=True)
class Decision:
    """The verdict of one request: the exit, the commit of the request that landed or "", the lines, and the update.

    `update` is the update of the work tree of a landed request, else None (the index and the working tree).
    """

    status: int
    commit: str = ""
    lines: tuple[Line, ...] = ()
    update: Update | None = None


@dataclass(frozen=True, slots=True)
class JournalEntry:
    """The journal of one request before a swap: its token, the branch, the commit of the swap and its verdict."""

    token: str
    branch: str
    final: str
    decision: Decision


@dataclass(frozen=True, slots=True)
class Run:
    """One run of the gate: the candidate, the number, the subject and the entry time of each request of its chain, and
    the verdict.

    A run of the base has no request (the run of the base).  The entry time of a request is Queued.entry_time.
    """

    candidate: gate.Candidate
    numbers: tuple[int, ...]
    subjects: tuple[str, ...]
    entry_times: tuple[str, ...]
    verdict: gate.Verdict


@dataclass(frozen=True, slots=True)
class RunRecord:
    """One run of the gate with the result of each of its requests, in the sequence of its chain (the record)."""

    run: Run
    results: tuple[str, ...]


# The check of the gate: the candidate and the retry flag give the verdict (the gate).
type Check = Callable[[gate.Candidate, bool], gate.Verdict]
# The function that receives the verdict of each request (the holder).
type Decide = Callable[[Queued, Decision], None]
# The check that tells if the process of a request holds its lock (the compare-and-swap).
type IsAlive = Callable[[Queued], bool]
# The function that frees the claim of a request that waits for the next batch (the lanes).
type Release = Callable[[Queued], None]
# The holder of the tree of a lane: it takes the work directory, the queue and the lane, and it tells if a batch was
# there (the wait, the lanes).
type Holder = Callable[[Path, Path, int], bool]


# ── The request ────────────────────────────────────────────────────────────


def split_words(words: Sequence[str]) -> tuple[list[str], list[str]]:
    """Return the words before the first -- that is not the value of an option, and the words after it (the request).

    Raises:
        LandError: If no such -- is in the words, or an option has fewer values than it takes

    Complexity: linear in the words.
    """
    index = 0
    while index < len(words) and words[index] != END_OF_OPTIONS:
        count = VALUE_COUNTS.get(words[index], 0)
        if index + count >= len(words):
            raise LandError(f"the option {words[index]} takes {count} value(s), and the words end before them")
        index += count + 1
    if index >= len(words):
        raise LandError("put -- before the own paths.  The list of the paths can be empty")
    return list(words[:index]), list(words[index + 1 :])


def read_text(path: Path) -> str:
    """Return the text of a file of an edit.

    Raises:
        LandError: If the file cannot be read
    """
    try:
        return path.read_text(encoding="utf-8")
    except OSError as problem:
        raise LandError(f"the file {path} of an edit cannot be read: {problem}") from None


@dataclass(frozen=True, slots=True)
class ReasonOption:
    """An option that gives the reason cell of the ledger rows that the gate writes, and the words of its refusal."""

    name: str
    rows: str
    cause: str


# The two options of a reason of a row of a budget (the budget reason, the lower).
BUDGET_REASON = ReasonOption(
    "--budget-reason",
    "each raised row of a budget",
    "Name the owner decision of 2026-11-15 and the measured cause of the growth",
)
LOWER_REASON = ReasonOption("--lower-reason", "each lowered row of a budget", "Name the cut and its measured effect")
# A value ITEM=TEXT of --budget-reason: the item of one row, with no space and no '=', and the text of that row.
BUDGET_ROW = re.compile(r"(?P<item>[^\s=|]+)=(?P<text>.*)", re.DOTALL)


def reason_of(option: ReasonOption, reason_values: Sequence[Sequence[str]]) -> str:
    """Return the reason of an option of a row reason, or "" when the words do not give the option.

    Raises:
        LandError: If the option occurs more than one time, or its text cannot be the reason cell of a ledger row
    """
    if len(reason_values) > 1:
        raise LandError(f"give {option.name} one time")
    if not reason_values:
        return ""
    return checked_reason(option, reason_values[0][0])


def checked_reason(option: ReasonOption, text: str) -> str:
    """Return the text of an option of a row reason with no outer space.

    Raises:
        LandError: If the text cannot be the reason cell of a ledger row
    """
    reason = text.strip()
    if not reason or "|" in reason or "\n" in reason or reason.startswith("-"):
        raise LandError(
            f"{option.name} takes the reason of {option.rows}, a text with no '|' and no new line that does not start "
            f"with '-', not {text!r}.  {option.cause}"
        )
    return reason


def budget_reasons_of(reason_values: Sequence[Sequence[str]]) -> tuple[str, tuple[tuple[str, str], ...]]:
    """Return the plain text and the text of each named row of the words of --budget-reason (the budget reason).

    A value ITEM=TEXT gives the text of the crossed row of the item, and ITEM holds no space and no '='.  Each other
    value is the plain text of each crossed row with no text of its own.  Complexity: linear in the values.

    Raises:
        LandError: If the plain text occurs more than one time, an item occurs more than one time, or a text cannot be
            the reason cell of a ledger row
    """
    plain: list[str] = []
    rows: dict[str, str] = {}
    for value in reason_values:
        named = BUDGET_ROW.fullmatch(value[0].strip())
        if named is None:
            plain.append(checked_reason(BUDGET_REASON, value[0]))
            continue
        item = named["item"]
        if item in rows:
            raise LandError(f"give the text of the row {item} with --budget-reason {item}=TEXT one time")
        rows[item] = checked_reason(BUDGET_REASON, named["text"])
    if len(plain) > 1:
        raise LandError(
            "give the plain --budget-reason TEXT one time.  It is the text of each crossed row with no text of its "
            "own.  Give the text of one row with --budget-reason ITEM=TEXT"
        )
    return (plain[0] if plain else ""), tuple(rows.items())


def lowers_of(lower_values: Sequence[Sequence[str]]) -> tuple[tuple[str, str], ...]:
    """Return the rule and the item of each --lower, in the sequence of the words (the lower).

    Raises:
        LandError: If a rule is not a rule of LOWER_RULES of assay.loop.gate, or an item is empty or holds '|'
    """
    found: list[tuple[str, str]] = []
    for rule, item in lower_values:
        if rule not in gate.LOWER_RULES:
            raise LandError(f"--lower takes a rule of {', '.join(gate.LOWER_RULES)} and an item, not the rule {rule!r}")
        if not item.strip() or "|" in item or "\n" in item:
            raise LandError(f"--lower takes the item of a ledger row of {rule}, not {item!r}")
        if (rule, item.strip()) not in found:
            found.append((rule, item.strip()))
    return tuple(found)


def checked_options(flags: dict[str, bool], values: Mapping[str, list[list[str]]], request: Request) -> Options:
    """Return the options of the flags and of the options with values, and refuse a combination that does not apply.

    A series takes its messages and its changes from its commits, so it takes no -m, no patch, no edit and no own path
    (the series).  --dry-run and --tree run no gate, so they take no --budget-reason and no --lower.

    Raises:
        LandError: If the options do not apply together
    """
    jobs_values, series_values = values["--jobs"], values["--series"]
    budget_reason, budget_rows = budget_reasons_of(values["--budget-reason"])
    if (budget_reason or budget_rows) and (flags[FLAG_DRY_RUN] or flags[FLAG_TREE]):
        raise LandError("--budget-reason applies to a run of the gate, so give it no --dry-run and no --tree")
    lowers = lowers_of(values["--lower"])
    lower_reason = reason_of(LOWER_REASON, values["--lower-reason"])
    if bool(lowers) != bool(lower_reason):
        raise LandError(
            "give --lower RULE ITEM and --lower-reason TEXT together: each lowered row takes the reason of the cut"
        )
    if lowers and (flags[FLAG_DRY_RUN] or flags[FLAG_TREE]):
        raise LandError("--lower applies to a run of the gate, so give it no --dry-run and no --tree")
    if len(jobs_values) > 1:
        raise LandError("give --jobs one time")
    jobs_text = jobs_values[0][0] if jobs_values else None
    if jobs_text is not None and (not jobs_text.isdigit() or int(jobs_text) < 1):
        raise LandError(f"--jobs takes a number above 0, not {jobs_text!r}")
    if sum(flags[flag] for flag in (FLAG_CHECK, FLAG_DRY_RUN, FLAG_TREE)) > 1:
        raise LandError("give one of --check, --dry-run and --tree")
    if flags[FLAG_HERE] and not flags[FLAG_CHECK]:
        raise LandError("--here applies only to --check.  A landing runs the copy of the main work tree")
    if len(series_values) > 1:
        raise LandError("give --series one time")
    series = series_values[0][0] if series_values else ""
    if series and not request.is_empty:
        raise LandError(
            "--series takes the message and the change of each commit of the branch, so give it no -m, no --patch, "
            "no --replace, no --block and no own path"
        )
    if series and request.messages:
        raise LandError("--series takes the message of each commit of the branch, so give it no -m")
    if not series and not request.messages and not flags[FLAG_TREE] and not flags[FLAG_CHECK]:
        raise LandError("give the message of the commit with -m")
    if not series and request.is_empty and not flags[FLAG_CHECK]:
        raise LandError("the request holds nothing to commit")
    jobs = int(jobs_text) if jobs_text is not None else proc.DEFAULT_JOBS
    return Options(
        flags[FLAG_DRY_RUN],
        flags[FLAG_TREE],
        flags[FLAG_CHECK],
        flags[FLAG_HERE],
        flags[FLAG_EVERY],
        jobs,
        series,
        budget_reason,
        lowers,
        lower_reason,
        budget_rows,
    )


def parse(argv: Sequence[str], current: Path) -> tuple[Request, Options]:
    """Return the request and the other options of the command line (the request).

    The command reads the options itself, and not with argparse, because the value of an option can start with -.

    Args:
        argv: The words after `loop land`
        current: The directory that the paths of --patch, --replace and --block start from

    Raises:
        LandError: If the words are not a request

    Complexity: linear in the words.
    """
    before, own = split_words(argv)
    values: dict[str, list[list[str]]] = {option: [] for option in VALUE_COUNTS}
    flags = dict.fromkeys(FLAGS, False)
    index = 0
    while index < len(before):
        word = before[index]
        if word in flags:
            flags[word] = True
            index += 1
            continue
        if word not in VALUE_COUNTS:
            raise LandError(f"the word {word!r} is not an option of loop land.\n{USAGE}")
        count = VALUE_COUNTS[word]
        values[word].append(before[index + 1 : index + 1 + count])
        index += count + 1
    edits = [
        Edit(path, kind, read_text(current / first), read_text(current / second))
        for kind, option in ((INSERT, "--block"), (REPLACE, "--replace"))
        for path, first, second in values[option]
    ]
    request = Request(
        tuple(item[0] for item in values["-m"]),
        tuple((current / item[0]).resolve() for item in values["--patch"]),
        tuple(edits),
        tuple(own),
    )
    return request, checked_options(flags, values, request)


def series_request(root: Path, revision: str, directory: Path) -> Request:
    """Return the request of a series: each commit of the revision that HEAD does not hold, from the oldest (the series).

    The patch of each commit against its first parent goes into a file of the directory.  A merge in the series gives no
    part, because its two sides are in the series or in HEAD.  Complexity: two runs of git for each commit.

    Raises:
        LandError: If the revision names no commit, HEAD holds each of its commits, or a merge of the series holds a change
            of its own, such as the resolution of a conflict
        git.GitError: If git fails
    """
    tip = git.resolve(root, revision)
    if tip is None:
        raise LandError(f"--series {revision} names no commit")
    head = git.head(root)
    for merge in git.run(root, "rev-list", "--merges", f"{head}..{tip}").decode("utf-8").split():
        if git.run(root, "show", "--remerge-diff", "--format=", "--no-color", merge).strip():
            raise LandError(
                f"the merge {merge[:SHORT_HASH]} of the series holds a change of its own, such as the resolution of a "
                f"conflict, and no commit of the series holds that change.  Rebase {revision} onto main (git rebase "
                f"main), resolve each conflict in its own commit, and queue again"
            )
    commits = git.run(root, "rev-list", "--reverse", "--topo-order", "--no-merges", f"{head}..{tip}").decode().split()
    if not commits:
        raise LandError(f"HEAD {head[:SHORT_HASH]} holds each commit of {revision}, so the series is empty")
    landed = landed_subjects(root, head, tip)
    parts: list[Part] = []
    for index, commit in enumerate(commits):
        part = Part(git.message_of(root, commit).rstrip("\n") + "\n", directory / f"part-{index}.patch", commit)
        if part.subject in landed:
            raise LandError(
                f"the commit {commit[:SHORT_HASH]} '{part.subject}' of the series has the subject of a commit of HEAD "
                f"after the merge base, so it landed already, and the series would land its change a second time.  "
                f"Rebase {revision} onto main (git rebase main), which drops each landed commit, and queue again"
            )
        part.patch.write_bytes(
            git.run(root, "diff", "--binary", "--full-index", "-M", git.parent(root, commit), commit)
        )
        parts.append(part)
    return Request((), (), (), (), tuple(parts))


def landed_subjects(root: Path, head: str, tip: str) -> frozenset[str]:
    """Return the subject of each commit of HEAD after the merge base of HEAD and the tip of a series (the series).

    Histories with no merge base give no subject.  Complexity: two runs of git, and linear in the commits after the
    merge base.

    Raises:
        git.GitError: If git fails
    """
    found = git.run_any_exit(root, "merge-base", head, tip)
    fork = found.out.strip() if found.exit == 0 else ""
    if not fork:
        return frozenset()
    return frozenset(git.run(root, "log", "--format=%s", f"{fork}..{head}").decode("utf-8").splitlines())


# ── The edits ──────────────────────────────────────────────────────────────


def edit_state(content: str, kind: str, first: str, second: str) -> str:
    """Tell if the content holds the edit ("applied"), can take it ("apply"), or is ambiguous ("ambiguous").

    A replacement is applied when the content holds the replacement, and
    each copy of the replaced text is in a copy of the replacement.  That
    rule also holds when the replacement holds the replaced text.  An edit
    that extends its text then does not apply a second time.
    """
    if kind == INSERT:
        if second in content:
            return "applied"
        return "apply" if content.count(first) == 1 else "ambiguous"
    replaced_count = content.count(first)
    if first in second:
        # The replacement extends the replaced text.  Each applied copy of the replacement then holds the replaced text.
        replacement_count = content.count(second)
        if replacement_count >= 1 and replaced_count == replacement_count * second.count(first):
            return "applied"
        return "apply" if replacement_count == 0 and replaced_count == 1 else "ambiguous"
    # A deletion, a shorter text or a plain replacement is applied when the replaced text is gone.  The content must
    # then hold the replacement, or the replacement is empty.
    if replaced_count == 0 and (second == "" or second in content):
        return "applied"
    return "apply" if replaced_count == 1 else "ambiguous"


def apply_edit(content: str, edit: Edit, where: str) -> str:
    """Return the content with one edit.

    Raises:
        LandError: If the text of the edit does not occur one time in the content
    """
    state = edit_state(content, edit.kind, edit.first, edit.second)
    if state == "applied":
        return content
    if state == "ambiguous":
        what = "anchor" if edit.kind == INSERT else "old text"
        raise LandError(f"the {what} in {where} does not occur exactly 1 time outside the edit")
    if edit.kind == INSERT:
        at = content.index(edit.first) + len(edit.first)
        return content[:at] + edit.second + content[at:]
    return content.replace(edit.first, edit.second, 1)


def edited_paths(request: Request) -> list[str]:
    """Return each shared file of an edit, one time, in the sequence of the edits."""
    return list(dict.fromkeys(edit.path for edit in request.edits))


# ── The composition ────────────────────────────────────────────────────────


def is_under(path: str, items: set[str]) -> bool:
    """Tell if a path is one of the items, or a file in the directory of one of them."""
    return any(path == item or path.startswith(item + "/") for item in items)


# ── The merge ──────────────────────────────────────────────────────────────


def patch_sides(root: Path, patch: Path) -> tuple[dict[str, git.TreeEntry], dict[str, git.TreeEntry]]:
    """Return the base and the result of each file of a patch: the entries before and after the patch (the merge).

    git builds an index of the blobs that the patch names as its base, and then it applies the patch to that index.

    Raises:
        git.GitError: If the repository does not hold a blob of the base, or the patch does not apply to its base
    """
    with git.borrowing_repository(root) as side:
        index = git.git_dir(side) / "index"
        git.run(side, "apply", f"--build-fake-ancestor={index}", str(patch))
        base = git.index_entries(side)
        git.run(side, "apply", "--cached", str(patch))
        return base, git.index_entries(side)


def is_witness_output(path: str) -> bool:
    """Tell if a path is a file that the writer of the witness fixtures writes (the merge)."""
    pure = PurePosixPath(path)
    directory = witness_fixtures.FIXTURE_DIR.as_posix()
    if pure.parent.as_posix() != directory:
        return False
    is_fixture = pure.name.startswith(witness_fixtures.FIXTURE_PREFIX) and pure.name.endswith(
        witness_fixtures.FIXTURE_SUFFIX
    )
    return is_fixture or pure.name == witness_fixtures.MANIFEST_NAME


def blob_text(root: Path, entry: git.TreeEntry | None) -> str | None:
    """Return the text of the blob of an entry, or None for no entry.

    Raises:
        LandError: If the blob is not text of UTF-8
    """
    if entry is None:
        return None
    data = git.blobs(root, [entry.name])[0]
    try:
        return (data or b"").decode("utf-8")
    except UnicodeDecodeError:
        raise LandError(f"the blob {entry.name[:SHORT_HASH]} is not text of UTF-8") from None


def ledger_rules(root: Path, scratch: Path) -> dict[str, rule_registry.Rule]:
    """Return each rule with a ledger of the registry in the index of the composition, by its id (the merge).

    A registry that does not read gives no rule, and each ledger then merges as text.
    """
    entry = git.index_entries(scratch, [rule_registry.REGISTRY_PATH]).get(rule_registry.REGISTRY_PATH)
    try:
        text = blob_text(root, entry)
    except LandError:
        return {}
    if text is None:
        return {}
    registry, problems = rule_registry.examine(text)
    if problems:
        return {}
    return {rule_id: rule for rule_id, rule in registry.rules.items() if rule.policy != rule_registry.Policy.NONE}


def merged_ledger(
    root: Path,
    rule: rule_registry.Rule,
    path: str,
    sides: tuple[git.TreeEntry | None, ...],
    equal: rule_ledger.EqualChange,
) -> tuple[git.TreeEntry | None, bool]:
    """Return the entry of a ledger that two changes of one base merge to, and tell if a count merged by a sum.

    `equal` tells how two sides that change one count to one value merge (the merge).

    Raises:
        LandError: If the merge of the ledger conflicts (the merge of assay.core.ledger)
    """
    texts = (blob_text(root, sides[0]), blob_text(root, sides[1]), blob_text(root, sides[2]))
    try:
        merged = rule_ledger.merge(rule, texts, path, equal)
    except rule_ledger.LedgerError as problem:
        raise LandError(str(problem)) from None
    if merged.text is None:
        return None, bool(merged.summed)
    mode = sides[1] or sides[2] or sides[0]
    name = git.hash_object(root, merged.text.encode("utf-8"))
    return git.TreeEntry(mode.mode if mode is not None else git.PLAIN_FILE_MODE, "blob", name), bool(merged.summed)


def merged_text(root: Path, path: str, sides: tuple[git.TreeEntry | None, ...]) -> git.TreeEntry:
    """Return the entry of a file that two changes of one base merge to, by its lines (the merge).

    Raises:
        LandError: If one side adds, removes or changes the mode of the file differently, or the lines conflict
    """
    base, ours, theirs = sides
    if base is None or ours is None or theirs is None:
        raise LandError(f"{path}: one side adds or removes the file, and the other side changes it")
    if git.GITLINK_MODE in (base.mode, ours.mode, theirs.mode):
        raise LandError(f"{path}: the two sides change a commit of a different repository")
    try:
        mode = rule_ledger.three_way(base.mode, ours.mode, theirs.mode, "the mode")
    except rule_ledger.LedgerError:
        raise LandError(f"{path}: the two sides change the mode differently") from None
    name = git.merge_file(root, ours.name, base.name, theirs.name)
    if name is None:
        raise LandError(f"{path}: the two sides change the same lines")
    return git.TreeEntry(mode, "blob", name)


def merged_lines(root: Path, texts: tuple[str, str, str]) -> str | None:
    """Merge the base, ours and theirs texts by their lines with `git merge-file`, or give None on a conflict."""
    base, ours, theirs = (git.hash_object(root, text.encode("utf-8")) for text in texts)
    name = git.merge_file(root, ours, base, theirs)
    return None if name is None else (git.blobs(root, [name])[0] or b"").decode("utf-8")


def merged_words(root: Path, texts: tuple[str, str, str]) -> str | None:
    """Merge the base, ours and theirs copies of one text cell by their words, or give None on a conflict.

    Each word goes on a line of its own, and a space between two spaces gives an empty word, so the join restores each
    space of the text.
    """
    split = tuple("".join(f"{word}\n" for word in text.split(" ")) for text in texts)
    merged = merged_lines(root, (split[0], split[1], split[2]))
    return None if merged is None else " ".join(merged.split("\n")[:-1])


def merged_set(texts: tuple[str, str, str]) -> str:
    """Merge the base, ours and theirs copies of a list of names with commas: each side keeps its adds and removes.

    An add of theirs goes after the name before it in theirs, or at the end.  Complexity: quadratic in the names.
    """
    base, ours, theirs = (text.split(",") for text in texts)
    removed = (set(base) - set(ours)) | (set(base) - set(theirs))
    result = [item for item in ours if item not in removed]
    for index, item in enumerate(theirs):
        if item in result or item in removed:
            continue
        before = theirs[index - 1] if index else ""
        result.insert(result.index(before) + 1 if before in result else len(result), item)
    return ",".join(result)


def merged_row(root: Path, rows: tuple[str, str, str]) -> str:
    """Merge three copies of one row of the registry by its fields (the merge).

    A word KEY=VALUE merges by its value: a value of a list with commas merges by merged_set(), and a text cell after
    a bar merges by its words.  Each other change of one field by the two sides is a conflict.

    Raises:
        LandError: If the two sides change one field differently, and the field merges by no rule of this function
    """
    base, ours, theirs = rows
    if ours == base or ours == theirs:
        return theirs if ours == base else ours
    if theirs == base:
        return ours
    parts = [row.split(" | ") for row in rows]
    if len({len(part) for part in parts}) != 1:
        raise LandError(f"the two sides change the cells of the registry row {ours.split(' ', 2)[:2]} differently")
    heads = [part[0].split(" ") for part in parts]
    if len({tuple(words[:2]) for words in heads}) != 1:
        raise LandError("the two sides change the key of a registry row")
    # Each word KEY=VALUE maps KEY to "=VALUE", and a word with no sign maps to "", so a join gives the word back.
    fields = [{word.partition("=")[0]: "".join(word.partition("=")[1:]) for word in words[2:]} for words in heads]
    words = heads[1][:2]
    shown = " ".join(words)
    for key in dict.fromkeys([*fields[1], *fields[2]]):
        sides = (fields[0].get(key), fields[1].get(key), fields[2].get(key))
        if sides[1] == sides[0] or sides[1] == sides[2] or sides[2] == sides[0]:
            value = sides[2] if sides[1] == sides[0] else sides[1]
        elif all(side is not None and side.startswith("=") for side in sides) and "," in f"{sides[1]}{sides[2]}":
            value = "=" + merged_set((str(sides[0])[1:], str(sides[1])[1:], str(sides[2])[1:]))
        else:
            raise LandError(f"the two sides change the field {key} of the registry row {shown} differently")
        if value is not None:
            words.append(f"{key}{value}")
    cells: list[str] = []
    for index in range(1, len(parts[1])):
        copies = (parts[0][index], parts[1][index], parts[2][index])
        if copies[1] == copies[0] or copies[1] == copies[2] or copies[2] == copies[0]:
            cell: str | None = copies[2] if copies[1] == copies[0] else copies[1]
        else:
            cell = merged_words(root, copies)
        if cell is None:
            raise LandError(f"the two sides change the same words of a text of the registry row {shown}")
        cells.append(cell)
    return " | ".join([" ".join(words), *cells])


def row_key(line: str) -> str:
    """Return the key of a row of the registry, its first two words, or "" for a line that is no row (the merge)."""
    words = line.split(" ", 2)
    return f"{words[0]} {words[1]}" if len(words) == 3 and words[0] in REGISTRY_ROW_WORDS else ""


def merged_registry(root: Path, texts: tuple[str, str, str]) -> str:
    """Merge three copies of the registry by its rows: the place of each row by its key, and each row by its fields.

    The merge of the places reads each row as its key, so two changes of one row do not conflict there.  A row that
    one side removes and the other side changes is a conflict.  Complexity: linear in the lines, and one merge of the
    fields for each row that the two sides change.

    Raises:
        LandError: If the places of the rows conflict, or a row does not merge (merged_row())
    """
    lines = [text.splitlines() for text in texts]
    rows = [{row_key(line): line for line in side if row_key(line)} for side in lines]
    for key, line in rows[0].items():
        is_lost = key not in rows[1] and rows[2].get(key, line) != line
        if is_lost or (key not in rows[2] and rows[1].get(key, line) != line):
            raise LandError(
                f"{rule_registry.REGISTRY_PATH}: one side removes the row {key}, and the other side changes it"
            )
    skeleton = tuple("".join(f"{row_key(line) or line}\n" for line in side) for side in lines)
    merged = merged_lines(root, (skeleton[0], skeleton[1], skeleton[2]))
    if merged is None:
        raise LandError(f"{rule_registry.REGISTRY_PATH}: the two sides add, remove or move rows at the same place")
    result: list[str] = []
    for line in merged.splitlines():
        copies = (rows[0].get(line), rows[1].get(line), rows[2].get(line))
        if copies == (None, None, None):
            result.append(line)
            continue
        present = [item for item in copies if item is not None]
        if copies[0] is None and len(set(present)) > 1:
            raise LandError(f"{rule_registry.REGISTRY_PATH}: the two sides add the row {line} differently")
        result.append(merged_row(root, (copies[0] or present[0], copies[1] or present[0], copies[2] or present[0])))
    return "".join(f"{line}\n" for line in result)


def merged_registry_entry(root: Path, path: str, sides: tuple[git.TreeEntry | None, ...]) -> git.TreeEntry:
    """Return the entry of the registry that two changes merge to: by its lines, else by its rows (the merge).

    Raises:
        LandError: If the lines conflict and the rows do not merge either
    """
    try:
        return merged_text(root, path, sides)
    except LandError as problem:
        base, ours, theirs = (blob_text(root, entry) for entry in sides)
        if base is None or ours is None or theirs is None or sides[1] is None:
            raise problem from None
        text = merged_registry(root, (base, ours, theirs))
        return git.TreeEntry(sides[1].mode, "blob", git.hash_object(root, text.encode("utf-8")))


def regenerated_witnesses(root: Path, scratch: Path) -> list[tuple[git.TreeEntry | None, str]]:
    """Return the entry of each witness fixture and of the manifest that the composed rule table gives (the merge).

    The entries remove each fixture of no row.

    Raises:
        LandError: If the composition changes the code of the writer, or its rule table does not read
    """
    module_root = Path(witness_fixtures.__file__).resolve().parents[4]
    composed = git.index_entries(scratch, list(WITNESS_WRITER))
    for path in WITNESS_WRITER:
        own = git.hash_object(root, (module_root / path).read_bytes())
        if path not in composed or composed[path].name != own:
            raise LandError(f"the composition changes {path}, so the gate cannot write the witness fixtures again")
    table_path = witness_fixtures.TABLE.as_posix()
    table = blob_text(root, git.index_entries(scratch, [table_path]).get(table_path))
    if table is None:
        raise LandError(f"the composition holds no {table_path}")
    with tempfile.TemporaryDirectory(prefix="land-witness-") as work:
        planted = Path(work) / "rule-table.txt"
        planted.write_text(table, encoding="utf-8")
        try:
            witnesses = ruletable.load(planted).witnesses
        except (ruletable.TableError, OSError) as problem:
            raise LandError(f"the composed {table_path} does not read: {problem}") from None
    directory = witness_fixtures.FIXTURE_DIR.as_posix()
    wanted = {f"{directory}/{witness_fixtures.fixture_name(item)}": witness_fixtures.render(item) for item in witnesses}
    wanted[f"{directory}/{witness_fixtures.MANIFEST_NAME}"] = witness_fixtures.manifest_text(witnesses)
    present = git.index_entries(scratch, None)
    entries: list[tuple[git.TreeEntry | None, str]] = []
    for path in sorted(set(wanted) | {item for item in present if is_witness_output(item)}):
        if path not in wanted:
            entries.append((None, path))
            continue
        name = git.hash_object(root, wanted[path].encode("utf-8"))
        kept = present.get(path)
        if kept is None or kept.name != name:
            entries.append((git.TreeEntry(kept.mode if kept is not None else git.PLAIN_FILE_MODE, "blob", name), path))
    return entries


def merged_patch(root: Path, scratch: Path, patch: Path, problem: str, held: HeldPatch) -> PatchMerge:
    """Compose a patch that does not apply as text by a three-way merge of each of its files (the merge).

    A count ledger that ours and the patch change to one copy merges by its rows too, and each count that the two
    change to one value merges by a sum.  A patch of HeldPatch.LANDED that ours holds whole changes nothing.

    Args:
        root: The top of the work tree of the request
        scratch: The scratch repository of the composition, whose index holds the composition before the patch
        patch: The patch
        problem: The error of `git apply`
        held: What the patch is when ours holds each of its files

    Raises:
        LandError: If a file of the patch does not merge.  The message names each such file and the error of git

    Complexity: one read of the index for the paths of the patch, and one merge for each path that the two sides change.
    """
    try:
        base, theirs = patch_sides(root, patch)
    except git.GitError as error:
        raise LandError(
            f"the patch {patch} does not apply, and git cannot build its base for a three-way merge: {error}.  "
            f"git apply said: {problem.strip()}"
        ) from None
    changed = sorted(path for path in set(base) | set(theirs) if base.get(path) != theirs.get(path))
    ours = git.index_entries(scratch, changed)
    rules = ledger_rules(root, scratch)
    is_whole_held = all(ours.get(path) == theirs.get(path) for path in changed)
    is_landed = is_whole_held and held == HeldPatch.LANDED
    equal = rule_ledger.EqualChange.AGREE if is_landed else rule_ledger.EqualChange.SUM
    entries: list[tuple[git.TreeEntry | None, str]] = []
    merged: list[str] = []
    built: list[str] = []
    conflicts: list[str] = []
    is_summed = False
    needs_witnesses = False
    for path in changed:
        sides = (base.get(path), ours.get(path), theirs.get(path))
        if sides[1] == sides[0]:
            entries.append((sides[2], path))
            continue
        stem = PurePosixPath(path).stem
        rule = rules.get(stem) if path.startswith(LEDGER_PREFIX) and path.endswith(LEDGER_SUFFIX) else None
        is_count_sum = (
            rule is not None and rule.policy == rule_registry.Policy.COUNT and equal == rule_ledger.EqualChange.SUM
        )
        if sides[1] == sides[2] and not is_count_sum:
            continue
        try:
            if is_witness_output(path):
                needs_witnesses = True
            elif path in gate.BUILT_FILES:
                if sides[1] is None or sides[2] is None:
                    raise LandError(f"{path}: one side adds or removes the file, and the other side changes it")
                built.append(path)
            elif rule is not None:
                entry, summed = merged_ledger(root, rule, path, sides, equal)
                if entry == sides[1]:
                    continue
                entries.append((entry, path))
                is_summed = is_summed or summed
            elif path == rule_registry.REGISTRY_PATH:
                entries.append((merged_registry_entry(root, path, sides), path))
            else:
                entries.append((merged_text(root, path, sides), path))
            merged.append(path)
        except LandError as conflict:
            conflicts.append(str(conflict))
    if not conflicts and needs_witnesses:
        if not git.set_index_entries(scratch, entries):
            raise LandError("git cannot give the index of the composition the merged files")
        try:
            entries += regenerated_witnesses(root, scratch)
        except LandError as conflict:
            conflicts.append(str(conflict))
    if conflicts:
        raise LandError(
            f"the patch {patch} does not apply to the composition, and its three-way merge with it conflicts: "
            f"{'; '.join(conflicts)}.  Merge main in the work tree, and land the request again.  git apply said: "
            f"{problem.strip()}"
        )
    return PatchMerge(tuple(entries), tuple(merged), is_summed, tuple(built))


def compose(
    root: Path,
    request: Request,
    head: str,
    with_diff: bool = False,
    since: str = "",
    held: HeldPatch = HeldPatch.LANDED,
) -> Composed:
    """Compose the tree of a request on a commit (the tree, the composition).

    Args:
        root: The top of the work tree of the request
        request: The request
        head: The full name of the commit that the tree starts from
        with_diff: Give the diff of the tree against the commit, too
        since: The commit of HEAD of the work tree at the request, or "".  The own paths compare with it
        held: What a patch is when the composition holds each of its files (the merge)

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails, such as a patch that does not apply

    Complexity: one read of the own paths.  When `since` is not `head`, git also compares the trees of the two commits.
    """
    own = {path.rstrip("/") for path in request.paths}
    with git.borrowing_repository(root) as scratch:
        git.run(scratch, "read-tree", head)
        if request.paths:
            git.run(scratch, "--work-tree", str(root), "add", "-A", "--", *request.paths)
        before_patches = git.write_tree(scratch)
        merged: list[str] = []
        built: list[str] = []
        is_summed = False
        for patch in request.patches:
            applied = git.run_any_exit(scratch, "apply", "--cached", str(patch))
            if applied.exit == 0:
                continue
            result = merged_patch(root, scratch, patch, applied.err, held)
            if not git.set_index_entries(scratch, result.entries):
                raise LandError("git cannot give the index of the composition the merged files")
            merged += result.merged
            built += result.built
            is_summed = is_summed or result.is_summed
        after_patches = git.write_tree(scratch)
        patched = git.tree_changes(scratch, before_patches, after_patches)
        shared = list(dict.fromkeys([*edited_paths(request), *patched]))
        overlap = sorted(set(shared) & own)
        if overlap:
            raise LandError(f"{overlap} is both a shared file and an own path")
        for path in edited_paths(request):
            entry = git.tree_entry(root, head, path)
            if entry is None:
                raise LandError(f"the shared file {path} is not in the commit {head[:SHORT_HASH]}")
            stored = git.blobs(root, [git.index_object(scratch, path)])[0]
            if stored is None:
                raise LandError(f"the index of the composition holds no blob of the shared file {path}")
            content = stored.decode("utf-8")
            for edit in request.edits:
                if edit.path == path:
                    content = apply_edit(content, edit, f"{head[:SHORT_HASH]}:{path}")
            written = git.hash_object(root, content.encode("utf-8"))
            git.set_index_entry(scratch, replace(entry, name=written), path)
        changed = git.index_changes(scratch, head)
        allowed = own | set(shared)
        for path in changed:
            if not is_under(path, allowed):
                raise LandError(f"REFUSED, '{path}' changed but is not listed")
        if since and since != head:
            own_changed = {path for path in changed if is_under(path, own)}
            moved = sorted(own_changed.intersection(git.tree_changes(scratch, since, head)))
            if moved:
                raise LandError(
                    f"REFUSED, the own path(s) {moved} changed after the request.  HEAD of the work tree was "
                    f"{since[:SHORT_HASH]} at the request, and the commit {head[:SHORT_HASH]} holds different copies.  "
                    f"The working-tree copies can be without that change, and the commit can then revert it.  Land the "
                    f"request again after the work tree holds the commit {head[:SHORT_HASH]}"
                )
        tree = git.write_tree(scratch)
        diff = git.index_patch(scratch, head) if with_diff else ""
    return Composed(head, tree, changed, diff, tuple(dict.fromkeys(merged)), is_summed, tuple(dict.fromkeys(built)))


def commit_tree(root: Path, composed: Composed, message: str) -> str:
    """Write the commit of a composed tree on its commit, with the message, and return its full name."""
    return git.commit_tree(root, composed.tree, [composed.head], message)


def compose_link(queued: Queued, parent: str, since: str) -> Link:
    """Compose the commit of a request on a commit, and return the link (the batch).

    `since` is the commit that the own paths compare with (the tree).

    Raises:
        LandError: If the request does not compose, or it changes nothing
        git.GitError: If a git command fails, such as a patch that does not apply
    """
    request = queued.request
    if request.parts:
        return series_link(queued, parent, since)
    composed = compose(queued.root, request, parent, since=since)
    if not composed.changed:
        raise LandError("the commit would change nothing")
    commit = commit_tree(queued.root, composed, request.message)
    return Link(queued, parent, commit, composed.changed, composed.merged, composed.is_summed, composed.built)


def series_link(queued: Queued, parent: str, since: str) -> Link:
    """Compose each commit of a series on the commit before it, with its own message, and return the link (the series).

    A commit whose change the commit before it holds gives no commit.  Complexity: one composition for each commit.

    Raises:
        LandError: If a commit of the series does not compose.  The message names the first such commit.  Also if the
            series changes nothing
    """
    parts = queued.request.parts
    top = parent
    commits: list[tuple[str, str]] = []
    merged: list[str] = []
    built: list[str] = []
    is_summed = False
    for index, part in enumerate(parts, start=1):
        try:
            composed = compose(queued.root, Request((), (part.patch,), (), ()), top, since=since, held=HeldPatch.SECOND)
        except (LandError, git.GitError) as problem:
            raise LandError(
                f"the commit {index} of the {len(parts)} commits of the series, {part.commit[:SHORT_HASH]} "
                f"'{part.subject}', does not compose on {top[:SHORT_HASH]}: {problem}"
            ) from None
        if not composed.changed:
            continue
        top = git.commit_tree(queued.root, composed.tree, [top], part.message)
        commits.append((top, part.subject))
        merged += composed.merged
        built += composed.built
        is_summed = is_summed or composed.is_summed
    changed = git.tree_changes(queued.root, parent, top)
    if not commits or not changed:
        raise LandError("the series would change nothing")
    return Link(
        queued,
        parent,
        top,
        changed,
        tuple(dict.fromkeys(merged)),
        is_summed,
        tuple(dict.fromkeys(built)),
        tuple(commits),
    )


# ── The index and the working tree ─────────────────────────────────────────


def branch_of(root: Path) -> str:
    """Return the branch of HEAD of a tree, such as refs/heads/main.

    Raises:
        LandError: If HEAD is not on a branch
    """
    branch = git.symbolic_head(root)
    if branch is None:
        raise LandError(f"HEAD of {root} is not on a branch, and no branch can move")
    return branch


def swap(root: Path, branch: str, new: str, head: str, subject: str) -> bool:
    """Move the branch to the new commit when it holds the commit head, and tell if it moved (the compare-and-swap)."""
    return git.swap_ref(root, branch, new, head, f"loop land: {subject}")


def working_names(root: Path, paths: Sequence[str]) -> dict[str, str]:
    """Return the blob name of the working-tree copy of each path that is a regular file, by its path.

    Complexity: one call of git for the files.
    """
    files = [path for path in paths if (root / path).is_file() and not (root / path).is_symlink()]
    return dict(zip(files, git.hash_files(root, files), strict=True))


def write_copy(root: Path, path: str, entry: git.TreeEntry | None) -> None:
    """Give the working tree the copy of an entry, or remove the file for no entry (the index and the working tree).

    Raises:
        LandError: If the entry is not a file, such as a link or a commit of a different repository
        OSError: If the file cannot be written
    """
    file = root / path
    if entry is None:
        file.unlink(missing_ok=True)
        return
    if entry.mode not in FILE_MODES:
        raise LandError(f"{path} has the mode {entry.mode}, and the update writes only a file")
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(git.blobs(root, [entry.name])[0] or b"")
    file.chmod(0o755 if entry.mode == EXECUTABLE_MODE else 0o644)


def merged_copy(
    root: Path, path: str, before: git.TreeEntry | None, wanted: git.TreeEntry | None, request: Request
) -> bool:
    """Merge a working-tree copy that holds a local edit with the landed copy, write it, and tell if it merged.

    The merge takes the copy, the entry of the index before the update as its base, and the landed copy.  When the
    lines conflict, the edits of the request go to the copy of a shared file of an edit, as each edit can.

    Raises:
        git.GitError: If git fails
        LandError: If an edit does not apply
        OSError: If the file cannot be read or written
    """
    file = root / path
    if not file.is_file():
        return False
    if before is not None and wanted is not None:
        local = git.hash_object(root, file.read_bytes())
        name = git.merge_file(root, local, before.name, wanted.name)
        if name is not None:
            file.write_bytes(git.blobs(root, [name])[0] or b"")
            return True
    if path not in edited_paths(request):
        return False
    content = file.read_text(encoding="utf-8")
    updated = content
    for edit in request.edits:
        if edit.path != path:
            continue
        state = edit_state(updated, edit.kind, edit.first, edit.second)
        if state == "ambiguous":
            return False
        if state == "apply":
            updated = apply_edit(updated, edit, path)
    if updated != content:
        file.write_text(updated, encoding="utf-8")
    return True


def synced_paths(root: Path, branch: str, request: Request, update: Update) -> list[str]:
    """Give the index and the working tree the landed copy of each path of an update, and return the warnings.

    The index and the working tree give the rules.  Complexity: a constant number of git calls for each path.
    """
    target = update.index_commit
    tip = git.resolve(root, branch) if branch else None
    if tip is not None and tip != target and git.is_ancestor(root, target, tip):
        target = tip
    paths = list(update.index_paths)
    index = git.index_entries(root, paths)
    copies = working_names(root, paths)
    entries: list[tuple[git.TreeEntry | None, str]] = []
    warnings: list[str] = []
    for path in paths:
        wanted = git.tree_entry(root, target, path)
        before = index.get(path)
        at_base = git.tree_entry(root, update.base, path) if update.base else None
        copy = copies.get(path)
        entries.append((wanted, path))
        if copy is None and ((root / path).is_symlink() or (root / path).exists()):
            warnings.append(f"loop land: WARNING, {path} of the working tree is not a file, and it gets no update")
            continue
        clean = {None if before is None else before.name, None if at_base is None else at_base.name}
        try:
            if copy == (None if wanted is None else wanted.name):
                continue
            if copy in clean:
                write_copy(root, path, wanted)
                continue
            if merged_copy(root, path, before, wanted, request):
                continue
        except LandError as problem:
            warnings.append(f"loop land: WARNING, {path} gets no update: {problem}")
            continue
        warnings.append(
            f"loop land: WARNING, the working-tree copy of {path} holds a local edit that does not merge with the copy "
            f"of {target[:SHORT_HASH]}, and it keeps the edit.  Give it that change by hand before the next commit of "
            f"that file, or that commit reverts it"
        )
    if entries and not git.set_index_entries(root, entries):
        warnings.append(f"loop land: WARNING, the index of {root} was busy and is not updated")
    return warnings


@contextlib.contextmanager
def update_lock(root: Path) -> Iterator[None]:
    """Hold an exclusive lock of flock(2) on the file UPDATE_LOCK of the git directory of a work tree for the block.

    Raises:
        git.GitError: If root is not in a git work tree
    """
    with open(git.git_dir(root) / UPDATE_LOCK, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def updated_tree(root: Path, branch: str, request: Request, update: Update) -> list[str]:
    """Give the work tree of a landed request the update of its verdict, and return the warnings.

    An error of git or of a file only gives a warning, because the commit is on the branch.  A work tree with HEAD on
    a different branch gets no update (the index and the working tree).
    """
    try:
        current = git.symbolic_head(root)
        if branch and current != branch:
            elsewhere = (
                f"loop land: WARNING, HEAD of {root} is on {current or 'no branch'} and not on {branch}, and the work "
                f"tree gets no update of the landed commit"
            )
            return [elsewhere]
        with update_lock(root):
            warnings = synced_paths(root, branch or current or "", request, update)
    except (git.GitError, LandError, OSError, ValueError) as problem:
        stopped = (
            f"loop land: WARNING, the update of the work tree {root} stops: {problem}.  The commit is on the branch.  "
            f"Give the index and the working tree its changes by hand"
        )
        return [stopped]
    return warnings


def given_update(queued: Queued, decision: Decision) -> None:
    """Give the work tree of a landed request the update of its verdict as the holder, and write each warning as a line.

    The process of the request gives the same update again after it reads the verdict (the index and the working tree).
    """
    if decision.update is None:
        return
    for warning in updated_tree(queued.root, queued.branch, queued.request, decision.update):
        check_report.write_line(warning, is_error=True)


# ── The batch ──────────────────────────────────────────────────────────────


def chain_changed(chain: Sequence[Link]) -> tuple[str, ...]:
    """Return each path that a chain of links changes or that the gate writes for it, one time, sorted."""
    return tuple(sorted({path for link in chain for path in (*link.changed, *link.built)}))


def chain_built(chain: Sequence[Link]) -> tuple[str, ...]:
    """Return each file that the gate writes for a chain of links, one time, sorted (the merge)."""
    return tuple(sorted({path for link in chain for path in link.built}))


def repository_of(queued: Queued) -> Path:
    """Return the shared git directory of a request, or its work tree for a request with no such directory (the holder)."""
    return Path(queued.common) if queued.common else queued.root


def shown_item(verdict: gate.Verdict, item: str) -> str:
    """Return one failing item of a verdict with LOGS_MARK as the replacement of the directory of its logs."""
    return item.replace(verdict.logs, LOGS_MARK) if verdict.logs else item


def shown_items(verdict: gate.Verdict) -> frozenset[str]:
    """Return the failing items of a verdict with LOGS_MARK as the replacement of the directory of its logs.

    The items of two runs then compare equal when only their directories of logs are different (the run of the base).
    """
    return frozenset(shown_item(verdict, item) for item in verdict.items)


def cut_point(links: Sequence[Link]) -> int:
    """Return the count of the first links in which no link changes a C++ file of an earlier link (the batch).

    Complexity: linear in the paths of the links.
    """
    seen: set[str] = set()
    for index, link in enumerate(links):
        files = {path for path in link.changed if guard_inputs.is_compiled_file(path)}
        if files & seen:
            return index
        seen |= files
    return len(links)


def refused(status: int, *texts: str) -> Decision:
    """Return the verdict of a refused request, with each text as a line of the standard error."""
    return Decision(status, "", tuple(Line(text, True) for text in texts))


def store_problem(repository: Path, pause: Callable[[float], None] = time.sleep) -> str | None:
    """Return why the git store of a repository is damaged, or None for a whole store (the store).

    A stop of the host can leave the file config with no bytes, or an object with no bytes.  The file config must
    have bytes, and `git fsck --connectivity-only --no-reflogs` must exit 0 in one of STORE_TRIES runs, with a pause
    of STORE_RETRY_S between two runs.  The check of the store of the tree takes about 3 s.

    Raises:
        git.GitError: If git cannot start, or it runs past its limit
    """
    config = git.common_dir(repository) / "config"
    if not config.is_file() or config.stat().st_size == 0:
        return (
            f"the file {config} of the git store has no bytes, the damage of a stop of the host, so no landing moves "
            f"the branch.  Write the configuration of the repository again, then queue the request again"
        )
    words = ("fsck", "--connectivity-only", "--no-reflogs", "--no-progress")
    completed = git.run_any_exit(repository, *words, timeout_s=proc.TOOL_S)
    for _retry in range(STORE_TRIES - 1):
        if completed.exit == 0:
            return None
        pause(STORE_RETRY_S)
        completed = git.run_any_exit(repository, *words, timeout_s=proc.TOOL_S)
    if completed.exit == 0:
        return None
    shown = " | ".join(completed.err.strip().splitlines()[:STORE_LINES])
    refs = sorted(set(STORE_REF.findall(completed.err)))
    named = f"  The error names {' '.join(refs)}." if refs else ""
    return (
        f"`git fsck --connectivity-only --no-reflogs` exits {completed.exit} in {repository} in {STORE_TRIES} runs "
        f"{STORE_RETRY_S:g} s apart, so the git store is damaged and no landing moves the branch: {shown}.{named}  "
        f"Repair the store, then queue the request again"
    )


def merge_lines(link: Link) -> list[Line]:
    """Return the line that names each file of a request that the merge composed, and each file that the gate writes.

    With no merged file, the result is no line.
    """
    if not link.merged:
        return []
    summed = ", with a sum of two changes of a count, so the gate ran each configuration" if link.is_summed else ""
    lines = [
        Line(
            f"loop land: a patch did not apply as text, and a three-way merge composed {' '.join(link.merged)}{summed}"
        )
    ]
    if link.built:
        lines.append(
            Line(
                f"loop land: the two sides change {' '.join(link.built)}, so the composition keeps its own copy, and "
                f"the gate writes {' '.join(link.built)} from the build of the composed tree"
            )
        )
    return lines


def written_text(verdict: gate.Verdict) -> str:
    """Return the words of a landed line that name the ledgers and the built files that the gate wrote, or ""."""
    falls = f", with the falls of {' '.join(verdict.ledgers)} that the gate wrote" if verdict.ledgers else ""
    built = f", with {' '.join(verdict.built)} that the gate wrote from the build" if verdict.built else ""
    return f"{falls}{built}"


def chain_budget_reason(chain: Sequence[Link]) -> str:
    """Return the budget reason of a chain, or "" when a request of the chain gives none (the budget reason).

    The reason of a chain of more requests holds the reason of each of them, in the sequence of the chain.  Complexity:
    linear in the links.
    """
    reasons = [link.queued.options.budget_reason for link in chain]
    if not reasons or not all(reasons):
        return ""
    return functools.reduce(rule_ledger.joined_reason, dict.fromkeys(reasons))


def chain_budget_rows(chain: Sequence[Link]) -> tuple[tuple[str, str], ...]:
    """Return the item and the text of each row that a request of a chain names with --budget-reason ITEM=TEXT.

    The text of a row holds the text of each request of the chain for the item: its own text for the item, else its
    plain text.  A row gets a text only when each request gives one (the budget reason).  Complexity: linear in the
    rows of the links times the links.
    """
    named = dict.fromkeys(item for link in chain for item, _ in link.queued.options.budget_rows)
    found: list[tuple[str, str]] = []
    for item in named:
        texts = [dict(link.queued.options.budget_rows).get(item, link.queued.options.budget_reason) for link in chain]
        if all(texts):
            found.append((item, functools.reduce(rule_ledger.joined_reason, dict.fromkeys(texts))))
    return tuple(found)


def chain_lowers(chain: Sequence[Link]) -> tuple[tuple[str, str, str], ...]:
    """Return the rule, the item and the reason of the cut of each row that a request of a chain lowers (the lower).

    Each row takes the reason of its own request.  A row that two requests name keeps the first.  Complexity: linear
    in the rows of the links.
    """
    found: dict[tuple[str, str], str] = {}
    for link in chain:
        for rule, item in link.queued.options.lowers:
            found.setdefault((rule, item), link.queued.options.lower_reason)
    return tuple((rule, item, reason) for (rule, item), reason in found.items())


def raised_of(verdict: gate.Verdict) -> list[Line]:
    """Return the line of each row of a budget that the gate raised or lowered for a verdict (the budget reason)."""
    raised = [Line(f"loop land: the gate raised the row {line} with the budget reason") for line in verdict.raised]
    return [*raised, *(Line(f"loop land: the lower of a budget: {line}") for line in verdict.lowered)]


def flaky_of(verdict: gate.Verdict) -> list[Line]:
    """Return the line of each flaky test of a verdict of the gate (the flaky tests)."""
    shown = f" (logs in {verdict.logs})" if verdict.logs else ""
    return [Line(f"{FLAKY_MARK}{name} is red in the suite and green alone{shown}", True) for name in verdict.flaky]


def error_text(problem: Exception) -> str:
    """Return the text of an error, with its type and its traceback when it is not of KNOWN_ERRORS (the holder)."""
    if isinstance(problem, KNOWN_ERRORS):
        return str(problem)
    trace = "".join(traceback.format_exception(problem)).rstrip("\n")
    return f"{type(problem).__name__}: {problem}\n{trace}"


def stopped_batch(problem: Exception) -> Decision:
    """Return the verdict of each request of a batch that an error stopped: a usage error or a tool error (the holder)."""
    status = ExitCode.USAGE if isinstance(problem, LandError) else ExitCode.TOOL_FAILED
    return refused(status, f"loop land: {error_text(problem)}")


class Journal:
    """The journals of the requests of one queue (the journal)."""

    __slots__ = ("queue",)

    def __init__(self, queue: Path) -> None:
        """Keep the queue."""
        self.queue = queue

    def write(self, queued: Queued, branch: str, final: str, decision: Decision) -> None:
        """Write the journal of one request before the swap."""
        write_journal(self.queue, queued, JournalEntry(queued.token, branch, final, decision))

    def clear(self, queued: Queued) -> None:
        """Remove the journal of one request after a swap that did not occur."""
        queue_file(self.queue, queued.number, JOURNAL_SUFFIX).unlink(missing_ok=True)


class RightOfWay:
    """The right of way of one batch in its queue, with the number of the first request of the batch (the right of way).

    The object holds the descriptor of the lock while the batch holds the right of way, and -1 before.
    """

    __slots__ = ("descriptor", "number", "pause", "queue")

    def __init__(self, queue: Path, number: int, pause: heavy.Pause) -> None:
        """Keep the queue, the number of the batch and the pause of the wait."""
        self.queue = queue
        self.number = number
        self.pause = pause
        self.descriptor = -1

    @property
    def is_taken(self) -> bool:
        """Tell if the batch holds its right of way."""
        return self.descriptor >= 0

    def take(self) -> None:
        """Hold the right of way of the batch until free().  A second call changes nothing.

        Raises:
            OSError: If the file of the right of way cannot be opened or locked
        """
        if self.is_taken:
            return
        right = queue_file(self.queue, self.number, RIGHT_SUFFIX)
        descriptor = os.open(right, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX)
        except OSError:
            os.close(descriptor)
            raise
        self.descriptor = descriptor

    def free(self) -> None:
        """Remove the file of the right of way, then free the lock.  With no right of way, change nothing."""
        if not self.is_taken:
            return
        queue_file(self.queue, self.number, RIGHT_SUFFIX).unlink(missing_ok=True)
        os.close(self.descriptor)
        self.descriptor = -1

    def first(self) -> list[int]:
        """Return the number of each right of way of a different batch that goes before the swap of this batch.

        A batch with no right of way waits for each one.  A batch with a right of way waits only for a smaller number.
        Complexity: linear in the files of the right of way in the queue.
        """
        found: list[int] = []
        for path in paths.listing(self.queue, f"*{RIGHT_SUFFIX}"):
            number = number_of(path)
            if number is None or number == self.number or (self.is_taken and number > self.number):
                continue
            if heavy.is_held(path):
                found.append(number)
        return sorted(found)


@dataclass(frozen=True, slots=True)
class StackEntry:
    """The entry of one batch in the stack: its number, its base, the commit that it tests, and if the gate accepted it."""

    number: int
    base: str
    commit: str
    is_accepted: bool


class StackRule(enum.Enum):
    """The entries of the stack that a round of a batch composes on, after its lost runs (the rule of one lost run).

    ANY is the rule of a batch that lost no run: each entry, also an entry with no verdict.  OLDER is the rule after the
    first lost run: each accepted entry and each entry of an older batch, with no wait.  ACCEPTED is the rule after the
    second lost run: only the accepted entries, after a wait for the verdicts of the first look.
    """

    ANY = "any"
    OLDER = "older"
    ACCEPTED = "accepted"

    @property
    def after_loss(self) -> StackRule:
        """Return the rule of the batch after one more lost run."""
        return StackRule.OLDER if self is StackRule.ANY else StackRule.ACCEPTED


class Stack:
    """The entry of one batch in the stack of its queue, with the number of the first request of the batch (the stack).

    The object holds the descriptor of the lock of the entry from the first entry or the first mark in the line of the
    batch to free(), and -1 before.  The holder of the batch is the one writer of its entry and of its mark.
    """

    __slots__ = ("descriptor", "number", "queue")

    def __init__(self, queue: Path, number: int) -> None:
        """Keep the queue and the number of the batch."""
        self.queue = queue
        self.number = number
        self.descriptor = -1

    def hold(self) -> None:
        """Hold the lock of the entry of the batch until free().  A second call changes nothing.

        Raises:
            OSError: If the lock file cannot be opened or locked
        """
        if self.descriptor >= 0:
            return
        lock = queue_file(self.queue, self.number, STACK_LOCK_SUFFIX)
        descriptor = os.open(lock, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX)
        except OSError:
            os.close(descriptor)
            raise
        self.descriptor = descriptor

    def publish(self, branch: str, base: str, commit: str, is_accepted: bool) -> None:
        """Write the entry of the batch on a branch in place of its last entry, hold its lock, and leave the line.

        One queue holds the batches of each branch of the repository, so the entry names its branch.  The mark in the
        line goes after the write of the entry, so a batch behind the mark never sees a free place that this batch takes.

        Raises:
            OSError: If the lock file cannot be opened or locked, or the entry cannot be written
        """
        self.hold()
        record = {"branch": branch, "base": base, "commit": commit, "accepted": is_accepted}
        cache.write_atomic(queue_file(self.queue, self.number, STACK_SUFFIX), (json.dumps(record) + "\n").encode())
        self.leave_line()

    def join_line(self, branch: str) -> None:
        """Write the mark of the batch in the line of the full stack of a branch, and hold the lock of the entry.

        A mark that the batch holds already stays as it is (the stack).

        Raises:
            OSError: If the lock file cannot be opened or locked, or the mark cannot be written
        """
        self.hold()
        mark = queue_file(self.queue, self.number, LINE_SUFFIX)
        if not mark.is_file():
            cache.write_atomic(mark, (json.dumps({"branch": branch}) + "\n").encode())

    def leave_line(self) -> None:
        """Remove the mark of the batch in the line.  The lock stays until free()."""
        queue_file(self.queue, self.number, LINE_SUFFIX).unlink(missing_ok=True)

    def ahead(self, branch: str) -> list[int]:
        """Return the number of each live mark in the line of a branch of an older batch: a smaller number.

        A mark is live while a process holds the lock of its lock file.  A mark that does not read, and a mark of a
        different branch, is no mark.  Complexity: linear in the files of the marks in the queue.
        """
        found: list[int] = []
        for path in paths.listing(self.queue, f"*{LINE_SUFFIX}"):
            number = number_of(path)
            if number is None or number >= self.number:
                continue
            if not heavy.is_held(queue_file(self.queue, number, STACK_LOCK_SUFFIX)):
                continue
            try:
                if json.loads(path.read_text(encoding="utf-8"))["branch"] != branch:
                    continue
            except OSError, ValueError, KeyError, TypeError:
                continue
            found.append(number)
        return sorted(found)

    def withdraw(self) -> None:
        """Remove the entry of the batch and its mark in the line.  The lock stays until free()."""
        queue_file(self.queue, self.number, STACK_SUFFIX).unlink(missing_ok=True)
        self.leave_line()

    def free(self) -> None:
        """Remove the entry and the mark, then the lock file, and then free the lock.  With no lock, only remove them."""
        self.withdraw()
        if self.descriptor < 0:
            return
        queue_file(self.queue, self.number, STACK_LOCK_SUFFIX).unlink(missing_ok=True)
        os.close(self.descriptor)
        self.descriptor = -1

    def entries(self, branch: str) -> list[StackEntry]:
        """Return the live entry on a branch of each different batch, in the sequence of the numbers.

        An entry is live while a process holds the lock of its lock file.  An entry that does not read, and an entry of
        a different branch, is no entry.  Complexity: linear in the files of the entries in the queue.
        """
        found: list[StackEntry] = []
        for path in paths.listing(self.queue, f"*{STACK_SUFFIX}"):
            number = number_of(path)
            if number is None or number == self.number:
                continue
            if not heavy.is_held(queue_file(self.queue, number, STACK_LOCK_SUFFIX)):
                continue
            try:
                record = json.loads(path.read_text(encoding="utf-8"))
                if record["branch"] != branch:
                    continue
                found.append(StackEntry(number, str(record["base"]), str(record["commit"]), bool(record["accepted"])))
            except OSError, ValueError, KeyError, TypeError:
                continue
        return sorted(found, key=lambda item: item.number)

    def top(self, tip: str, entries: Sequence[StackEntry]) -> list[StackEntry]:
        """Return the stack from the branch: each entry on the commit of the entry before it, at most STACK_DEPTH.

        Among the entries on one commit, the stack takes the entry that lands first: a holder of a right of way
        before a batch with none, then the smaller number (the stack).  Complexity: quadratic in the entries.
        """
        chain: list[StackEntry] = []
        top = tip
        while len(chain) < STACK_DEPTH:
            on_top = [item for item in entries if item.base == top and item not in chain]
            if not on_top:
                break
            chosen = min(on_top, key=lambda item: (not self.is_right_held(item.number), item.number))
            chain.append(chosen)
            top = chosen.commit
        return chain

    def is_right_held(self, number: int) -> bool:
        """Tell if the batch with the number holds its right of way (the right of way)."""
        return heavy.is_held(queue_file(self.queue, number, RIGHT_SUFFIX))


def stack_path(tip: str, base: str, entries: Sequence[StackEntry]) -> list[StackEntry] | None:
    """Return the entries of a path from the branch to a base, each on the commit of the one before, or None.

    The path of a base at the branch is empty.  None tells that the base can never be the branch (the stack).
    Complexity: quadratic in the entries.
    """
    if tip == base:
        return []
    frontier: list[tuple[str, list[StackEntry]]] = [(tip, [])]
    seen = {tip}
    while frontier:
        top, path = frontier.pop()
        for item in entries:
            if item.base != top or item.commit in seen:
                continue
            if item.commit == base:
                return [*path, item]
            seen.add(item.commit)
            frontier.append((item.commit, [*path, item]))
    return None


class BatchLanding:
    """The landing of one batch of requests (the batch, the search, the compare-and-swap).

    The object keeps the runs of the gate, the run that landed, and the run that refused each request, for the
    record.  It also keeps the requests that wait for the next batch, the dropped requests and the verdicts of the
    requests that landed.  `noise` keeps the lines of the problems that the confirmation judged noise, for each
    request.
    """

    __slots__ = (
        "base",
        "base_run",
        "branch",
        "check",
        "decide",
        "deferred",
        "dropped",
        "is_alive",
        "is_check",
        "is_cut",
        "is_entry_freed",
        "is_publishing",
        "is_stack_stuck",
        "journal",
        "landed",
        "landed_run",
        "noise",
        "pause",
        "refused_by",
        "release",
        "repository",
        "right",
        "runs",
        "stack",
        "stack_lock",
        "stack_rule",
    )

    def __init__(
        self,
        batch: Sequence[Queued],
        check: Check,
        decide: Decide,
        pause: heavy.Pause,
        is_alive: IsAlive | None = None,
        journal: Journal | None = None,
        right: RightOfWay | None = None,
        stack: Stack | None = None,
        release: Release | None = None,
    ) -> None:
        """Keep the gate, the receiver of the verdicts, the pause, the check of the processes, the journal, the right,
        the stack and the release of a claim.

        The first request gives the shared git directory and the branch (the holder).  With no check of the processes,
        each request stays in the batch.  With no right of way, the batch neither takes one nor waits for one.  With no
        stack, the batch composes on the branch, and it writes no entry (the stack).  With no release, a request that
        waits for the next batch keeps its claim until the end of the batch (the lanes).
        """
        self.repository = repository_of(batch[0])
        self.branch = batch[0].branch or branch_of(batch[0].root)
        self.check = check
        self.decide = decide
        self.pause = pause
        self.is_alive: IsAlive = is_alive if is_alive is not None else (lambda _queued: True)
        self.journal = journal
        self.right = right
        self.is_check = batch[0].options.is_check
        self.stack = None if self.is_check else stack
        self.release: Release = release if release is not None else (lambda _queued: None)
        self.is_publishing = False
        self.is_entry_freed = False
        self.stack_lock = threading.Lock()
        self.is_stack_stuck = False
        self.stack_rule = StackRule.ANY
        self.is_cut = False
        self.base = ""
        self.base_run = -1
        self.runs: list[Run] = []
        self.landed_run = -1
        self.refused_by: dict[int, int] = {}
        self.dropped: dict[int, int] = {}
        self.deferred: list[Queued] = []
        self.landed: dict[int, Decision] = {}
        self.noise: dict[int, list[Line]] = {}

    def tip(self) -> str:
        """Return the commit of the branch of the batch.

        Raises:
            LandError: If the branch holds no commit
            git.GitError: If git cannot read the repository
        """
        commit = git.resolve(self.repository, self.branch)
        if commit is None:
            raise LandError(f"the branch {self.branch} holds no commit")
        return commit

    def is_settled(self, item: StackEntry) -> bool:
        """Tell if the rule of the batch lets a round compose on an entry of the stack with no wait for its verdict.

        An accepted entry is settled with each rule.  With StackRule.ANY each entry is settled, and with StackRule.OLDER
        also each entry of an older batch: a smaller number (the rule of one lost run).
        """
        if item.is_accepted or self.stack_rule is StackRule.ANY:
            return True
        is_older = self.stack is not None and item.number < self.stack.number
        return self.stack_rule is StackRule.OLDER and is_older

    def settled_part(self, below: Sequence[StackEntry]) -> list[StackEntry]:
        """Return the entries of a stack from the branch up to the first entry that is not settled (is_settled()).

        Complexity: linear in the entries.
        """
        kept: list[StackEntry] = []
        for item in below:
            if not self.is_settled(item):
                break
            kept.append(item)
        return kept

    def gate_run(
        self,
        chain: Sequence[Link],
        focus: Sequence[gate.Focus] = (),
        is_alone: bool = False,
        is_stacked: bool = False,
    ) -> gate.Verdict:
        """Run the gate on the last commit of a chain, keep the run, and return the verdict.

        The first run of the batch runs each test, and each subsequent run runs the tests with an input that changed.
        A focus runs only the configurations and the tests of the focus (assay.loop.gate, the run of the base).
        `is_alone` tells that the chain is the run alone of its one request (the confirmation).  The title of the
        candidate names the kind of the run: the run alone of the request, or the run of the batch.  A chain with a
        count that the merge summed runs each configuration.  The candidate names each file that the merge leaves to the
        gate.  An error of the check is a problem of the commit (the error of a run).  The first run of a round writes
        the entry of the batch with its commit, and a red first run removes it.  `is_stacked` tells that the round
        composed on the stack.  Its candidate names each built file, because a batch below can change one (the stack).
        """
        is_every = any(link.is_summed for link in chain)
        commit = chain[-1].commit
        kind = f"the run alone of {chain[-1].queued.name}" if is_alone else "the run of the batch"
        built = tuple(sorted({*chain_built(chain), *gate.BUILT_FILES})) if is_stacked else chain_built(chain)
        is_published = (self.is_publishing or is_stacked) and self.stack is not None and not focus and not is_alone
        candidate = gate.Candidate(
            self.base,
            commit,
            chain_changed(chain),
            focus=tuple(focus),
            is_every=is_every,
            built=built,
            title=f"{kind} at {commit[:SHORT_HASH]}",
            on_fold=self.publish_fold if is_published else None,
            budget_reason=chain_budget_reason(chain),
            budget_rows=chain_budget_rows(chain),
            lowers=chain_lowers(chain),
        )
        if is_published and self.stack is not None:
            self.is_publishing = False
            self.is_entry_freed = False
            self.stack.publish(self.branch, self.base, commit, False)
        started = time.monotonic()
        try:
            with self.gone_watch(chain, is_published):
                verdict = self.check(candidate, bool(self.runs))
        except Exception as problem:  # noqa: BLE001
            text = f"the gate cannot judge the commit {candidate.commit[:SHORT_HASH]}: {error_text(problem)}"
            check_report.write_line(f"loop land: {text}", is_error=True)
            verdict = gate.Verdict((), (text,), time.monotonic() - started)
        if is_published and self.stack is not None and not verdict.is_accepted:
            self.stack.withdraw()
        numbers = tuple(link.queued.number for link in chain)
        subjects = tuple(link.queued.request.subject for link in chain)
        self.runs.append(Run(candidate, numbers, subjects, tuple(link.queued.entry_time for link in chain), verdict))
        return verdict

    @contextlib.contextmanager
    def gone_watch(self, chain: Sequence[Link], is_published: bool) -> Iterator[None]:
        """Free the entry of the stack at once when a request of the chain leaves the queue while the block runs.

        A batch above stacks on the entry, and the commit of the entry holds the change of the request that left, so
        that commit can never land (the stack).  A thread looks at the process of each request each WATCH_S seconds.
        After the free, no fold writes the entry again (publish_fold()).  The batch then drops the request after the
        run, as before.  With no published entry, the block runs with no thread.
        """
        if not is_published or self.stack is None:
            yield
            return
        stack = self.stack
        ended = threading.Event()

        def watch() -> None:
            """Look at the processes of the chain until the block ends, and free the entry when one of them left."""
            while not ended.wait(WATCH_S):
                gone = [link.queued.name for link in chain if not self.is_alive(link.queued)]
                if not gone:
                    continue
                with self.stack_lock:
                    self.is_entry_freed = True
                    stack.free()
                check_report.write_line(
                    f"loop land: {' '.join(gone)} left the queue during the run, so the batch frees its entry of the "
                    f"stack at once, and no batch stacks on a commit that cannot land"
                )
                return

        thread = threading.Thread(target=watch, name="land-gone-watch", daemon=True)
        thread.start()
        try:
            yield
        finally:
            ended.set()
            thread.join()

    def publish_fold(self, commit: str) -> None:
        """Write the commit of the fold of the built files into the entry of the batch, during the run (the stack).

        A batch that starts after this write stacks on the commit that the batch lands, if no later fold changes it.
        An entry that cannot be written gives a line, and the entry keeps its last commit.
        """
        if self.stack is None:
            return
        try:
            with self.stack_lock:
                if self.is_entry_freed:
                    return
                self.stack.publish(self.branch, self.base, commit, False)
        except OSError as problem:
            check_report.write_line(
                f"loop land: the entry of the stack cannot hold the commit {commit[:SHORT_HASH]}: {problem}",
                is_error=True,
            )
            return
        check_report.write_line(f"loop land: the entry of the batch holds the commit {commit[:SHORT_HASH]} of the fold")

    def base_gate_run(self, changed: Sequence[str], focus: Sequence[gate.Focus]) -> gate.Verdict:
        """Run the gate on the commit of the batch with the changed paths of the batch, and keep the run (the run of the base).

        The run is a retry.  With a focus, it runs only the focus: the red tests and the configurations with a problem
        of the first run.  With no focus, it runs the tests of the configurations of the batch with an input that
        changed.
        """
        title = f"the run of the base at {self.base[:SHORT_HASH]}"
        candidate = gate.Candidate(self.base, self.base, tuple(changed), focus=tuple(focus), title=title)
        verdict = self.check(candidate, True)
        self.runs.append(Run(candidate, (), (), (), verdict))
        self.base_run = len(self.runs) - 1
        return verdict

    def base_items(self) -> frozenset[str]:
        """Return the failing items of the run of the base with LOGS_MARK, or none when the batch has no such run."""
        return shown_items(self.runs[self.base_run].verdict) if self.base_run >= 0 else frozenset()

    def red_base_line(self) -> str:
        """Return the line that tells that the commit of the batch is red with no request (the run of the base)."""
        return f"loop land: REJECTED, the commit {self.base[:SHORT_HASH]} of {self.branch} is red with no request"

    def compose_chain(self, accepted: Sequence[Link], items: Sequence[Queued]) -> tuple[list[Link], list[Queued]]:
        """Compose each request on the accepted links and on the requests before it, and refuse each that does not.

        A request with a process that left the queue drops out with no verdict.  The own paths of a request compare
        with the commit of its HEAD at the request, else with the commit of the batch (the tree).  Returns the links
        and the requests that do not compose or drop out.
        """
        links: list[Link] = []
        lost: list[Queued] = []
        parent = accepted[-1].commit if accepted else self.base
        for queued in items:
            if not self.is_alive(queued):
                lost.append(queued)
                self.dropped[queued.number] = -1
                continue
            try:
                link = compose_link(queued, parent, queued.head or self.base)
            except LandError as problem:
                lost.append(queued)
                self.decide(queued, refused(ExitCode.USAGE, f"loop land: {problem}"))
                continue
            except git.GitError as problem:
                lost.append(queued)
                self.decide(
                    queued, refused(ExitCode.TOOL_FAILED, f"loop land: the request does not compose: {problem}")
                )
                continue
            links.append(link)
            parent = link.commit
        return links, lost

    def reject(self, queued: Queued, run: int, lead: str = "") -> None:
        """Give a request that is red on the accepted part the refusal, with the problems of its run.

        When each failing item of the run is an item of the run of the base, the first line tells that the branch is red
        (the run of the base).  A `lead` is the first line, such as the line of an interaction (the confirmation).
        """
        self.refused_by[queued.number] = run
        verdict = self.runs[run].verdict
        lines = [f"loop land: REJECTED, {item}" for item in verdict.problems]
        items = shown_items(verdict)
        if items and items <= self.base_items():
            lines.insert(0, self.red_base_line())
        if lead:
            lines.insert(0, lead)
        self.decide(queued, refused(ExitCode.FINDINGS, *lines, *verdict.rows))

    def note_noise(self, queued: Queued, red_run: int, green_run: int) -> None:
        """Keep a line for each problem of a red run that a green run of the same tree did not repeat (the confirmation).

        The holder writes each line, and the verdict of the request gives it (the flaky tests).
        """
        red, green = self.runs[red_run].verdict, self.runs[green_run].verdict
        where = f"in {red.logs} " if red.logs else ""
        again = f" in {green.logs}" if green.logs else ""
        lines = [
            Line(
                f"{FLAKY_MARK}a problem of the run {where}did not repeat in a run of the same tree{again}: {item}", True
            )
            for item in red.problems
        ]
        for line in lines:
            check_report.write_line(line.content, is_error=True)
        self.noise[queued.number] = lines

    def confirmed(self, queued: Queued, accepted: Sequence[Link], red_run: int) -> Link | None:
        """Run the red tree of a request again before its refusal, and return its link when its red run was noise.

        The confirmation gives the rules.  The function gives the refusal of a request whose tree is red two times,
        and it returns None.  A request with a process that left the queue drops out, with None.

        Args:
            queued: The request of the red part of one request
            accepted: The accepted part
            red_run: The index of the run that made the request the red part
        """
        links, lost = self.compose_chain(accepted, [queued])
        if lost:
            return None
        verdict = self.gate_run([*accepted, *links])
        again_run = len(self.runs) - 1
        if verdict.is_accepted:
            self.note_noise(queued, red_run, again_run)
            return links[0]
        lead = self.alone_line(queued, accepted, verdict.focus) if accepted else ""
        self.reject(queued, again_run, lead)
        return None

    def alone_line(self, queued: Queued, accepted: Sequence[Link], focus: Sequence[gate.Focus]) -> str:
        """Run the failed checks of a red chain on the request alone, and return the first line of its refusal.

        The run alone is the request on the commit of the batch, with the focus of the second red run (the
        confirmation).  It only gives the words of the refusal: red alone too, or an interaction.
        """
        numbers = " ".join(link.queued.name for link in accepted)
        base = self.base[:SHORT_HASH]
        try:
            alone = compose_link(queued, self.base, queued.head or self.base)
        except LandError, git.GitError:
            return (
                f"loop land: REJECTED, the request is red two times after the accepted requests {numbers}, and it "
                f"does not compose alone on {base}"
            )
        if self.gate_run([alone], focus, is_alone=True).is_accepted:
            return (
                f"loop land: REJECTED, the request is red two times after the accepted requests {numbers}, and green "
                f"alone on {base}: an interaction"
            )
        return f"loop land: REJECTED, the request is red two times after the accepted requests {numbers}, and alone too"

    def reject_on_red_base(self, items: Sequence[Queued], batch_run: int) -> None:
        """Give each request the refusal of a branch that is red with no request that repairs it (the run of the base).

        The lines give each problem of the run of the base, and each other failing item of the first run of the batch,
        with the directory of its logs.
        """
        base_verdict = self.runs[self.base_run].verdict
        batch_verdict = self.runs[batch_run].verdict
        lines = [f"{self.red_base_line()}, and no request of the batch repairs it"]
        lines += [f"loop land: REJECTED, {item}" for item in base_verdict.problems]
        base = self.base_items()
        logs = batch_verdict.logs
        lines += [
            f"loop land: the batch also gives: {item}{f', in {logs}' if logs and logs not in item else ''}"
            for item in batch_verdict.items
            if shown_item(batch_verdict, item) not in base
        ]
        lines += batch_verdict.rows
        for queued in items:
            self.refused_by[queued.number] = batch_run
            self.decide(queued, refused(ExitCode.FINDINGS, *lines))

    def cut(self, pending: list[Queued], links: list[Link]) -> tuple[list[Queued], list[Link]]:
        """Cut the batch at the first link that changes a C++ file of an earlier link.

        The requests from that link wait for the next batch.  The function cuts only at the first composition of the
        batch (the batch).
        """
        if self.is_cut:
            return pending, links
        self.is_cut = True
        kept = cut_point(links)
        if kept == len(links):
            return pending, links
        later = [link.queued for link in links[kept:]]
        self.deferred += later
        for queued in later:
            self.release(queued)
        check_report.write_line(
            f"loop land: {len(later)} request(s) from {later[0].name} change a C++ file of an earlier request of the "
            f"batch, and wait for the next batch"
        )
        return [item for item in pending if item not in later], links[:kept]

    def search(self, items: Sequence[Queued]) -> tuple[list[Link], int]:
        """Return the accepted links of the requests and the index of the run that accepted them as a unit (the search).

        The index is -1 when the gate accepts no request.  A first run that is red gets the run of the base.  When
        that run is red, and the first run has each of its problems, each request gets the refusal of a red branch.
        Complexity: one run of the gate when the gate accepts each request.  A batch of n requests with r red
        requests runs the gate O(r log n) times.  the run of the base adds one run, and the confirmation adds one or
        two runs for each red request.
        """
        accepted: list[Link] = []
        accepted_run = -1
        pending = list(items)
        red_size: int | None = None
        red_run = -1
        while pending:
            if red_size == 1:
                red_size = None
                joined = self.confirmed(pending.pop(0), accepted, red_run)
                if joined is not None:
                    accepted.append(joined)
                    accepted_run = len(self.runs) - 1
                continue
            take = len(pending) if red_size is None else red_size // 2
            links, lost = self.compose_chain(accepted, pending[:take])
            if lost:
                pending = [item for item in pending if item not in lost]
                red_size = None
                continue
            pending, links = self.cut(pending, links)
            take = min(take, len(links))
            verdict = self.gate_run([*accepted, *links])
            if verdict.is_accepted:
                accepted += links
                accepted_run = len(self.runs) - 1
                del pending[:take]
                red_size = None if red_size is None else red_size - take
                continue
            batch_run = len(self.runs) - 1
            if not accepted and red_size is None and self.base_run < 0:
                base_verdict = self.base_gate_run(chain_changed(links), verdict.focus)
                is_unrepaired = shown_items(base_verdict) <= shown_items(verdict)
                if not base_verdict.is_accepted and is_unrepaired:
                    self.reject_on_red_base(pending, batch_run)
                    return [], -1
            red_size, red_run = take, batch_run
        return accepted, accepted_run

    def flaky_lines(self, number: int) -> list[Line]:
        """Return the lines of the noise of a request and of each flaky test of each accepted run that holds it.

        The flaky tests give the rules.  Complexity: linear in the runs and their flaky tests.
        """
        found = [
            line
            for run in self.runs
            if number in run.numbers and run.verdict.is_accepted
            for line in flaky_of(run.verdict)
        ]
        return list(dict.fromkeys([*self.noise.get(number, ()), *found]))

    def accept_check(self, accepted: Sequence[Link], verdict: gate.Verdict) -> None:
        """Give each request of --check that the gate accepts its verdict.  Nothing lands."""
        final = verdict.final or accepted[-1].commit
        folded = written_text(verdict)
        for link in accepted:
            line = Line(f"loop land: the gate accepts the commit {final[:SHORT_HASH]}{folded}")
            lines = (line, *merge_lines(link), *raised_of(verdict), *self.flaky_lines(link.queued.number))
            self.decide(link.queued, Decision(ExitCode.OK, final, lines))

    def landed_decisions(
        self, accepted: Sequence[Link], verdict: gate.Verdict, final: str
    ) -> list[tuple[Queued, Decision]]:
        """Return the verdict of each accepted request, with the update of its work tree (the index and the working tree).

        The ledgers of the reduce and the built files that the gate wrote go to the last request of each work tree.
        Complexity: linear in the paths of the links.
        """
        ledgers = verdict.ledgers
        written = (*ledgers, *verdict.built)
        last = len(accepted) - 1
        last_of_root = {link.queued.root: index for index, link in enumerate(accepted)}
        folded = written_text(verdict)
        decisions: list[tuple[Queued, Decision]] = []
        for index, link in enumerate(accepted):
            commit = final if index == last else link.commit
            changed = tuple(sorted({*link.changed, *(written if index == last else ())}))
            lines = [Line(f"loop land: committed {commit[:8]} on {link.parent[:8]}{folded if index == last else ''}:")]
            if link.commits:
                series = [*link.commits[:-1], (commit, link.commits[-1][1])]
                lines = [
                    Line(
                        f"loop land: committed the series of {len(series)} commits on {link.parent[:8]}"
                        f"{folded if index == last else ''}:"
                    )
                ]
                lines += [Line(f"  {item[:8]} {subject}") for item, subject in series]
                lines.append(Line("loop land: the series changes these files:"))
            lines += [Line(f"  {path}") for path in changed]
            lines += merge_lines(link)
            lines += raised_of(verdict)
            if len(accepted) > 1:
                lines.append(
                    Line(f"loop land: a batch of {len(accepted)} requests landed at {final[:SHORT_HASH]} with one swap")
                )
            lines += self.flaky_lines(link.queued.number)
            is_last_of_root = last_of_root[link.queued.root] == index
            has_falls = bool(ledgers) and is_last_of_root
            update = Update(
                final,
                tuple(sorted({*changed, *(written if is_last_of_root else ())})),
                (accepted[-1].commit, final) if has_falls else (),
                ledgers if has_falls else (),
                self.base,
            )
            decisions.append((link.queued, Decision(ExitCode.OK, commit, tuple(lines), update)))
        return decisions

    def land(self, items: Sequence[Queued]) -> None:
        """Land the requests: compose, run the gate, search, and move the branch by compare-and-swap.

        After the swap, each landed work tree gets its update before the first verdict (the index and the working tree).

        Raises:
            gate.GateError: If the warm tree cannot hold a commit, or a step of the gate cannot start
            git.GitError: If a git command fails, other than a git command of a composition
            LandError: If the branch holds no commit
            OSError: If a journal or a verdict cannot be written
        """
        try:
            problem = store_problem(self.repository, self.pause)
            if problem is not None:
                for queued in items:
                    self.decide(queued, refused(ExitCode.TOOL_FAILED, f"loop land: REFUSED, {problem}"))
                return
            self.landed_pending(list(items))
        finally:
            if self.right is not None:
                self.right.free()
            if self.stack is not None:
                self.stack.free()

    def landed_pending(self, pending: list[Queued]) -> None:
        """Run the rounds of land() until the swap, a refusal of each request, or ATTEMPTS refused swaps.

        After RIGHT_AFTER refused swaps, the batch takes its right of way before its next round.  Before each round and
        before each swap, it waits for the batches that go first (the right of way, the stack).  A round composes on the
        top of the stack, or on the branch.  A move of the branch from the base of a round on the branch counts as a
        refused swap.  A round on the stack whose base leaves the stack counts in STACK_ATTEMPTS.  The caller frees the
        right of way and the entry.
        """
        swaps_refused = 0
        restacks = 0
        is_on_branch = False
        while pending:
            right = self.right
            if right is not None and not self.is_check and swaps_refused >= RIGHT_AFTER and not right.is_taken:
                right.take()
                check_report.write_line(
                    f"loop land: the branch moved {swaps_refused} times, and the batch takes the right of way: the "
                    "other lanes wait before their run and their swap until this batch ends"
                )
            stack = self.stack
            if stack is not None:
                stack.withdraw()
            is_holder = right is not None and right.is_taken
            is_stacking = (
                stack is not None
                and restacks < STACK_ATTEMPTS
                and not is_on_branch
                and not self.is_stack_stuck
                and not is_holder
            )
            is_on_branch = False
            self.is_stack_stuck = False
            if not self.is_check:
                self.wait_turn(RUN_STEP, is_stacking, pending)
            if self.is_stack_stuck:
                is_stacking = False
                self.is_stack_stuck = False
            entries = stack.entries(self.branch) if is_stacking and stack is not None else []
            tip = self.tip()
            below = stack.top(tip, entries)[: STACK_DEPTH - 1] if is_stacking and stack is not None else []
            below = self.settled_part(below)
            self.base = below[-1].commit if below else tip
            self.base_run = -1
            if below:
                pending, accepted, run, is_on_branch = self.stacked_round(pending, below)
                if not accepted:
                    restacks += 1
                    self.stack_rule = self.stack_rule.after_loss
                    continue
            else:
                self.is_publishing = stack is not None
                accepted, run = self.search(pending)
                self.is_publishing = False
                if not accepted:
                    return
            gone = [link.queued for link in accepted if not self.is_alive(link.queued)]
            if gone:
                for queued in gone:
                    self.dropped[queued.number] = run
                pending = [link.queued for link in accepted if link.queued not in gone]
                check_report.write_line(
                    f"loop land: {len(gone)} request(s) left the queue before the swap, and the batch composes its "
                    f"{len(pending)} other accepted request(s) again"
                )
                continue
            verdict = self.runs[run].verdict
            if self.is_check:
                self.landed_run = run
                self.accept_check(accepted, verdict)
                return
            final = verdict.final or accepted[-1].commit
            decisions = self.landed_decisions(accepted, verdict, final)
            if stack is not None:
                stack.publish(self.branch, self.base, final, True)
            is_moved = self.wait_turn(SWAP_STEP)
            if not is_moved and self.landed_swap(accepted, decisions, final, run):
                return
            pending = [link.queued for link in accepted]
            if below:
                restacks += 1
                self.stack_rule = self.stack_rule.after_loss
                check_report.write_line(
                    f"loop land: the base of the batch left the stack, and the batch composes its {len(pending)} "
                    "accepted requests again"
                )
                continue
            swaps_refused += 1
            if swaps_refused >= ATTEMPTS:
                stop = f"loop land: the branch moved {ATTEMPTS} times, and the batch stops"
                for queued in pending:
                    self.decide(queued, refused(ExitCode.USAGE, stop))
                return
            check_report.write_line(
                f"loop land: the branch moved, and the batch composes its {len(pending)} accepted requests again",
                is_error=True,
            )
            self.pause(random.uniform(*PAUSE_RANGE_S))

    def stacked_round(
        self, pending: list[Queued], below: Sequence[StackEntry]
    ) -> tuple[list[Queued], list[Link], int, bool]:
        """Run one round on the top of the stack: compose, write the entry, and run the gate one time (the stack).

        The round gives no verdict, because its base is not on the branch.  A request that does not compose on the
        stack makes the next round compose on the branch.  After a red run, the batch waits until the branch is at its
        base or its base leaves the stack, because the red can come from a batch below.  When the branch is then at its
        base, the next round composes on the branch, and its search judges the batch on its exact base.

        Returns:
            The requests that stay, the accepted links, the index of their run, and if the next round composes on the
            branch.  The links are empty when the round accepts nothing.
        """
        stack = self.stack
        if stack is None:
            return pending, [], -1, True
        names = " ".join(item.name for item in pending)
        holders = " ".join(f"{item.number:0{NUMBER_WIDTH}d}" for item in below)
        check_report.write_line(
            f"loop land: the batch {names} composes on {self.base[:SHORT_HASH]} of the stack, above {holders}"
        )
        gone = [queued for queued in pending if not self.is_alive(queued)]
        for queued in gone:
            self.dropped[queued.number] = -1
        pending = [queued for queued in pending if queued not in gone]
        if not pending:
            return pending, [], -1, False
        links: list[Link] = []
        parent = self.base
        try:
            for queued in pending:
                links.append(compose_link(queued, parent, queued.head or self.base))
                parent = links[-1].commit
        except (LandError, git.GitError) as problem:
            check_report.write_line(
                f"loop land: a request does not compose on the stack, so the batch composes on the branch: {problem}"
            )
            return pending, [], -1, True
        pending, links = self.cut(pending, links)
        verdict = self.gate_run(links, is_stacked=True)
        if verdict.is_accepted:
            return pending, links, len(self.runs) - 1, False
        check_report.write_line(
            "loop land: the run on the stack is red, so the batch gives no verdict, and it waits until the branch is at "
            "its base or its base leaves the stack"
        )
        return pending, [], -1, self.wait_base()

    def wait_base(self) -> bool:
        """Wait until the branch is at the base of the batch, or the base leaves the stack, and tell if it is the branch.

        The wait looks again each POLL_S seconds, for at most STACK_WAIT_S seconds.  After that bound, the next round
        composes on the branch (the stack).
        """
        if self.stack is None:
            return self.tip() == self.base
        waited = 0.0
        while waited < STACK_WAIT_S:
            entries = self.stack.entries(self.branch)
            path = stack_path(self.tip(), self.base, entries)
            if path is None or not path:
                return path is not None
            self.pause(POLL_S)
            waited += POLL_S
        self.is_stack_stuck = True
        return False

    def wait_turn(self, step: str, is_stacking: bool = False, pending: Sequence[Queued] = ()) -> bool:
        """Wait before a step of a round for the batches that go first, and tell if the base can never be the branch.

        Before a run, the batch waits while a right of way goes first, or while the stack holds STACK_DEPTH entries.
        `is_stacking` tells that the round can compose on the stack.  Such a batch holds a mark in the line, and it also
        waits while the older marks in the line are as many as the free places of the stack.  A batch whose `pending`
        requests each left the queue holds no mark, so it holds no younger batch until its wait ends.  A batch with
        StackRule.ACCEPTED also waits while an entry of the stack that had no verdict at the first look of the wait has
        none, and it holds no mark in the line during that wait.  An entry that comes into the stack during the wait
        does not hold it (the stack, the rule of one lost run).  Before a swap, the batch waits while a right of way
        goes first, or while batches of the stack are below its base.  A right of way does not go first when its holder
        is below the batch in the stack, or when the entry of its holder is not on the branch, because such a holder
        cannot swap first (the right of way, the stack).  The wait looks again each POLL_S seconds.  A right of way holds
        it for at most RIGHT_WAIT_S seconds, and the stack for at most STACK_WAIT_S seconds.  Each line names the
        batches that no earlier line of the wait named.

        Returns:
            True before a swap when the base of the batch can never be the branch, else False
        """
        waited = 0.0
        rights_shown: set[int] = set()
        stack_shown: set[int] = set()
        first_open: frozenset[int] | None = None
        where = "the batches below it in the stack"
        while True:
            entries = self.stack.entries(self.branch) if self.stack is not None else []
            tip = self.tip()
            if step == RUN_STEP:
                below = self.stack.top(tip, entries) if is_stacking and self.stack is not None else []
                open_numbers = {item.number for item in below if not self.is_settled(item)}
                if first_open is None:
                    first_open = frozenset(open_numbers)
                is_top_open = self.stack_rule is StackRule.ACCEPTED and bool(open_numbers & first_open)
                ahead: list[int] = []
                is_live = not pending or any(self.is_alive(item) for item in pending)
                if self.stack is not None and is_stacking and not is_top_open and is_live:
                    self.stack.join_line(self.branch)
                    ahead = self.stack.ahead(self.branch)
                elif self.stack is not None:
                    self.stack.leave_line()
                is_full = len(below) >= STACK_DEPTH
                is_behind = bool(ahead) and len(ahead) >= STACK_DEPTH - len(below)
                is_held_by_stack = is_full or is_top_open or is_behind
                if is_full:
                    holding, where = {item.number for item in below}, "the full stack"
                elif is_top_open:
                    holding, where = set(open_numbers & first_open), "the verdicts of the stack"
                else:
                    holding, where = set(ahead), "the older batches in the line of the stack"
            else:
                path = stack_path(tip, self.base, entries)
                if path is None:
                    if rights_shown:
                        check_report.write_line(
                            "loop land: the branch moved while the batch waited for a right of way, so its swap cannot "
                            "succeed"
                        )
                    return True
                below, is_held_by_stack = path, bool(path)
                holding = {item.number for item in below}
            numbers = {item.number for item in below}
            off_branch = {item.number for item in entries if item.base != tip}
            rights = self.right.first() if self.right is not None else []
            first = [number for number in rights if number not in numbers and number not in off_branch]
            if not first and not is_held_by_stack:
                if waited and step == RUN_STEP:
                    check_report.write_line(f"loop land: the batch waited {waited:.0f} s before its run")
                return False
            bound = STACK_WAIT_S if is_held_by_stack else RIGHT_WAIT_S
            if waited >= bound:
                check_report.write_line(
                    f"loop land: the batch waited {bound:.0f} seconds, and it goes on to its {step}", is_error=True
                )
                self.is_stack_stuck = is_held_by_stack
                if is_held_by_stack and self.stack is not None:
                    self.stack.leave_line()
                return False
            new_rights = [number for number in first if number not in rights_shown]
            if new_rights:
                names = " ".join(f"{number:0{NUMBER_WIDTH}d}" for number in new_rights)
                check_report.write_line(f"loop land: the batch waits before its {step} for the right of way of {names}")
                rights_shown.update(new_rights)
            new_below = sorted(holding - stack_shown) if is_held_by_stack else []
            if new_below:
                names = " ".join(f"{number:0{NUMBER_WIDTH}d}" for number in new_below)
                check_report.write_line(f"loop land: the batch waits before its {step} for {where}: {names}")
                stack_shown.update(new_below)
            self.pause(POLL_S)
            waited += POLL_S

    def landed_swap(
        self, accepted: Sequence[Link], decisions: Sequence[tuple[Queued, Decision]], final: str, run: int
    ) -> bool:
        """Move the branch from the base of the batch to the final commit, and give each verdict (the journal).

        Returns:
            True if the swap moved the branch.  False if a different lane moved the branch first, and the journals
            of the batch then go.
        """
        if self.journal is not None:
            for queued, decision in decisions:
                self.journal.write(queued, self.branch, final, decision)
        shown = accepted[0].queued.request.subject
        subject = shown if len(accepted) == 1 else f"a batch of {len(accepted)} requests from {shown}"
        if swap(self.repository, self.branch, final, self.base, subject):
            self.landed_run = run
            self.landed = {queued.number: decision for queued, decision in decisions}
            for queued, decision in decisions:
                given_update(queued, decision)
            for queued, decision in decisions:
                self.decide(queued, decision)
            return True
        if self.journal is not None:
            for queued, _decision in decisions:
                self.journal.clear(queued)
        return False

    def records(self) -> list[RunRecord]:
        """Return each run of the gate with the result of each of its requests (the record)."""
        found: list[RunRecord] = []
        for index, run in enumerate(self.runs):
            results = []
            for number in run.numbers:
                if index == self.landed_run:
                    results.append(ACCEPTED if self.is_check else LANDED)
                elif self.refused_by.get(number) == index:
                    results.append(REFUSED)
                elif self.dropped.get(number) == index:
                    results.append(DROPPED)
                else:
                    results.append(ACCEPTED if run.verdict.is_accepted else REJECTED)
            found.append(RunRecord(run, tuple(results)))
        return found


def write_records(work: Path, mode: str, records: Sequence[RunRecord], lane: int = 0) -> None:
    """Append one line of JSON for each run of the gate to the file RECORDS of the work directory (the record).

    The line gives the lane of the run, from 1 (the lanes).
    """
    if not records:
        return
    work.mkdir(parents=True, exist_ok=True)
    with open(work / RECORDS, "a", encoding="utf-8") as handle:
        for record in records:
            run = record.run
            line = {
                "time": loop_history.iso_now(),
                "mode": mode,
                "lane": lane + 1,
                "base": not run.numbers,
                "head": run.candidate.head,
                "commit": run.candidate.commit,
                "landed": LANDED in record.results,
                "requests": [
                    {"number": number, "queued": entry_time, "subject": subject, "result": result}
                    for number, entry_time, subject, result in zip(
                        run.numbers, run.entry_times, run.subjects, record.results, strict=True
                    )
                ],
                **asdict(run.verdict),
            }
            handle.write(json.dumps(line, sort_keys=True) + "\n")


# ── The queue ──────────────────────────────────────────────────────────────


def queue_of(work: Path, root: Path) -> Path:
    """Return the queue of the repository of a work tree in the work directory (the queue).

    Raises:
        git.GitError: If root is not in a git work tree
    """
    common = str(git.common_dir(root).resolve())
    return work / QUEUE / hashlib.sha256(common.encode("utf-8")).hexdigest()[:KEY_LENGTH]


def cited_runs(work: Path) -> frozenset[str]:
    """Return the name of each run of the gate that a verdict or a journal in a queue of the work directory cites.

    A verdict stays in the queue until the process of its request reads it and leaves the queue (remove_request()).
    So the sweep of the runs keeps the logs of each such run for that read (the logs of assay.loop.gate).  A file that
    does not read cites no run.  Complexity: linear in the bytes of the verdicts and the journals.
    """
    found: set[str] = set()
    for queue in paths.listing(work / QUEUE):
        for path in (*paths.listing(queue, f"*{VERDICT_SUFFIX}"), *paths.listing(queue, f"*{JOURNAL_SUFFIX}")):
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            found.update(CITED_RUN.findall(text))
    return frozenset(found)


def queue_file(queue: Path, number: int, suffix: str) -> Path:
    """Return the path of one file of a request in the queue: the number in NUMBER_WIDTH digits, and the suffix."""
    return queue / f"{number:0{NUMBER_WIDTH}d}{suffix}"


def number_of(path: Path) -> int | None:
    """Return the number of a file of the queue, or None when its name does not start with NUMBER_WIDTH digits."""
    head = path.name[:NUMBER_WIDTH]
    return int(head) if len(head) == NUMBER_WIDTH and head.isdigit() else None


def reserve(queue: Path) -> tuple[int, int]:
    """Take a new number of the queue, and return it with the descriptor of its lock, which the process holds.

    Each number occurs one time only (the queue).  The process holds the lock of the file NEXT while it reserves.  It
    locks a new file first, and then it links that file to the file of the number.  A different process then does not see
    the file of the number with no lock.  Complexity: linear in the files of the queue.
    """
    queue.mkdir(parents=True, exist_ok=True)
    with open(queue / NEXT, "a+", encoding="ascii") as counter:
        fcntl.flock(counter, fcntl.LOCK_EX)
        counter.seek(0)
        text = counter.read().strip()
        highest = max((found for path in paths.listing(queue) if (found := number_of(path)) is not None), default=0)
        number = max(int(text) if text.isdigit() else 0, highest + 1)
        counter.seek(0)
        counter.truncate()
        counter.write(f"{number + 1}\n")
        counter.flush()
        fresh = queue / RESERVE
        fresh.unlink(missing_ok=True)
        descriptor = os.open(fresh, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX)
            os.link(fresh, queue_file(queue, number, LOCK_SUFFIX))
            fresh.unlink()
        except BaseException:
            os.close(descriptor)
            raise
    return number, descriptor


def encoded_request(queued: Queued, patch_names: Sequence[str], part_names: Sequence[str] = ()) -> bytes:
    """Return the bytes of the file NUMBER.request or NUMBER.series of a request (the queue, the series).

    `part_names` gives the name of the copy of the patch of each commit of a series in the queue.
    """
    request = queued.request
    record = {
        "format": REQUEST_FORMAT,
        "token": queued.token,
        "root": str(queued.root),
        "branch": queued.branch,
        "common": queued.common,
        "head": queued.head,
        "queued": queued.entry_time,
        "messages": list(request.messages),
        "patches": list(patch_names),
        "edits": [asdict(edit) for edit in request.edits],
        "paths": list(request.paths),
        "parts": [
            {"message": part.message, "patch": name, "commit": part.commit}
            for part, name in zip(request.parts, part_names, strict=True)
        ],
        "jobs": queued.options.jobs,
        "every": queued.options.is_every,
        "budget_reason": queued.options.budget_reason,
        "budget_rows": [{"item": item, "text": text} for item, text in queued.options.budget_rows],
        "lowers": [{"rule": rule, "item": item} for rule, item in queued.options.lowers],
        "lower_reason": queued.options.lower_reason,
        "claims": True,
    }
    return (json.dumps(record, sort_keys=True) + "\n").encode("utf-8")


def text_list(value: object, name: str) -> tuple[str, ...]:
    """Return a list of texts of a request file.

    Raises:
        TypeError: If the value is not a list of texts.  The message names the field
    """
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise TypeError(f"the field {name} is not a list of texts")
    return tuple(value)


def records_of(value: object, name: str, keys: tuple[str, ...]) -> list[tuple[str, ...]]:
    """Return the texts of some keys of each record of a list of a request file.

    Raises:
        TypeError: If the value is not a list of records with a text for each key.  The message names the field
    """
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        raise TypeError(f"the field {name} is not a list of records")
    return [text_list([item.get(key) for key in keys], name) for item in value]


def token_of(data: bytes) -> str:
    """Return the token of the bytes of a request file or of a verdict file, or "" when the bytes hold no token."""
    try:
        record = json.loads(data.decode("utf-8"))
    except ValueError:
        return ""
    token = record.get("token") if isinstance(record, dict) else None
    return token if isinstance(token, str) else ""


def decoded_request(queue: Path, number: int, data: bytes) -> Queued:
    """Return the request of the bytes of a file NUMBER.request (the queue).

    Raises:
        ValueError: If the bytes do not hold JSON of REQUEST_FORMAT
        TypeError: If a field of the request does not have its type
    """
    record = json.loads(data.decode("utf-8"))
    if not isinstance(record, dict):
        raise TypeError("the request is not a record")
    if record.get("format") != REQUEST_FORMAT:
        raise ValueError(f"the request is not of the format {REQUEST_FORMAT}")
    edits = tuple(Edit(*item) for item in records_of(record.get("edits"), "edits", ("path", "kind", "first", "second")))
    keys = ("root", "branch", "common", "token", "head")
    root, branch, common, token, head = text_list([record.get(key) for key in keys], "root")
    jobs, every = record.get("jobs"), record.get("every")
    if not isinstance(jobs, int) or not isinstance(every, bool):
        raise TypeError("the field jobs is not a number, or the field every is not a truth value")
    entry_time = record.get("queued", "")
    if not isinstance(entry_time, str):
        raise TypeError("the field queued is not a text")
    budget_reason = record.get("budget_reason", "")
    if not isinstance(budget_reason, str):
        raise TypeError("the field budget_reason is not a text")
    budget_rows = tuple(
        (item, text) for item, text in records_of(record.get("budget_rows", []), "budget_rows", ("item", "text"))
    )
    lowers = tuple((rule, item) for rule, item in records_of(record.get("lowers", []), "lowers", ("rule", "item")))
    lower_reason = record.get("lower_reason", "")
    if not isinstance(lower_reason, str):
        raise TypeError("the field lower_reason is not a text")
    if jobs < 1:
        raise ValueError(f"the field jobs is {jobs}, and a request takes a number above 0")
    parts = tuple(
        Part(message, queue / name, commit)
        for message, name, commit in records_of(record.get("parts", []), "parts", ("message", "patch", "commit"))
    )
    request = Request(
        text_list(record.get("messages"), "messages"),
        tuple(queue / name for name in text_list(record.get("patches"), "patches")),
        edits,
        text_list(record.get("paths"), "paths"),
        parts,
    )
    options = Options(
        False,
        False,
        is_every=every,
        jobs=jobs,
        budget_reason=budget_reason,
        lowers=lowers,
        lower_reason=lower_reason,
        budget_rows=budget_rows,
    )
    is_claiming = record.get("claims") is True
    return Queued(number, Path(root), request, options, branch, common, token, head, is_claiming, entry_time)


@contextlib.contextmanager
def entered(
    queue: Path, root: Path, request: Request, options: Options, clock: Callable[[], float] = time.time
) -> Iterator[Queued]:
    """Write a request into the queue, hold its lock for the block, give the request, and remove its files at the end.

    The request of the block keeps the patches of the request.  The copy of each patch is in the queue.  `clock` gives
    the time of the entry, in seconds since the epoch (the record).

    Raises:
        LandError: If HEAD of the work tree is not on a branch, or a patch cannot be read
        git.GitError: If git cannot read the work tree
    """
    branch = branch_of(root)
    common = str(git.common_dir(root).resolve())
    head = git.head(root)
    number, descriptor = reserve(queue)
    try:
        token = secrets.token_hex(TOKEN_BYTES)
        entry_time = loop_history.iso_time(clock())
        queued = Queued(number, root, request, options, branch, common, token, head, entry_time=entry_time)
        sources = [*request.patches, *(part.patch for part in request.parts)]
        names = [f"{queued.name}{PATCH_MARK}{index}" for index in range(len(sources))]
        for name, patch in zip(names, sources, strict=True):
            try:
                data = patch.read_bytes()
            except OSError as problem:
                raise LandError(f"the patch {patch} cannot be read: {problem}") from None
            cache.write_atomic(queue / name, data)
        patch_names, part_names = names[: len(request.patches)], names[len(request.patches) :]
        suffix = SERIES_SUFFIX if request.parts else REQUEST_SUFFIX
        cache.write_atomic(queue_file(queue, number, suffix), encoded_request(queued, patch_names, part_names))
        yield queued
    finally:
        remove_request(queue, number)
        os.close(descriptor)


def request_file_of(queue: Path, number: int) -> Path:
    """Return the request file of a number: the file NUMBER.series of a series, else the file NUMBER.request."""
    series = queue_file(queue, number, SERIES_SUFFIX)
    return series if series.is_file() else queue_file(queue, number, REQUEST_SUFFIX)


def remove_request(queue: Path, number: int) -> None:
    """Remove each file of a request from the queue: the request file first, and the lock file last (the queue).

    A holder then takes no request of a process that left the queue, and it finds no file of a request with no lock.
    """
    for suffix in (REQUEST_SUFFIX, SERIES_SUFFIX, VERDICT_SUFFIX, JOURNAL_SUFFIX, CLAIM_SUFFIX, RIGHT_SUFFIX):
        queue_file(queue, number, suffix).unlink(missing_ok=True)
    for path in paths.listing(queue, f"{number:0{NUMBER_WIDTH}d}{PATCH_MARK}*"):
        path.unlink(missing_ok=True)
    queue_file(queue, number, LOCK_SUFFIX).unlink(missing_ok=True)


def encoded_decision(decision: Decision, token: str) -> bytes:
    """Return the bytes of the file NUMBER.verdict of a decision, with the token of its request."""
    record = {
        "format": REQUEST_FORMAT,
        "token": token,
        "status": decision.status,
        "commit": decision.commit,
        "lines": [asdict(line) for line in decision.lines],
        "update": asdict(decision.update) if decision.update is not None else None,
    }
    return (json.dumps(record, sort_keys=True) + "\n").encode("utf-8")


def decoded_update(value: object) -> Update | None:
    """Return the update of a verdict, or None for no update.

    Raises:
        TypeError: If the value is not a record of an update
    """
    if value is None:
        return None
    if not isinstance(value, dict):
        raise TypeError("the field update is not a record")
    index_commit, base = value.get("index_commit"), value.get("base", "")
    if not isinstance(index_commit, str) or not isinstance(base, str):
        raise TypeError("the field index_commit or base of the update is not a text")
    index_paths, falls, ledgers = (text_list(value.get(key), key) for key in ("index_paths", "falls", "ledgers"))
    return Update(index_commit, index_paths, falls, ledgers, base)


def decoded_decision(data: bytes) -> Decision:
    """Return the decision of the bytes of a verdict file.

    Raises:
        ValueError: If the bytes do not hold JSON of a verdict
        TypeError: If a field of the verdict does not have its type
    """
    record = json.loads(data.decode("utf-8"))
    if not isinstance(record, dict):
        raise TypeError("the verdict is not a record")
    status, commit, lines_value = record.get("status"), record.get("commit"), record.get("lines")
    if not isinstance(status, int) or not isinstance(commit, str) or not isinstance(lines_value, list):
        raise TypeError("the verdict has no status, commit or lines of their types")
    lines = tuple(
        Line(item["content"], bool(item.get("is_error")))
        for item in lines_value
        if isinstance(item, dict) and isinstance(item.get("content"), str)
    )
    return Decision(status, commit, lines, decoded_update(record.get("update")))


def write_decision(queue: Path, queued: Queued, decision: Decision) -> None:
    """Write the verdict file of a request into the queue, with the token of the request (the holder)."""
    cache.write_atomic(queue_file(queue, queued.number, VERDICT_SUFFIX), encoded_decision(decision, queued.token))


def read_decision(queue: Path, number: int, token: str) -> Decision | None:
    """Return the verdict of a request from the queue, or None when the request has no verdict with its token.

    A verdict with no token is the verdict of a request file that does not read (the holder).
    """
    try:
        data = queue_file(queue, number, VERDICT_SUFFIX).read_bytes()
        decision = decoded_decision(data)
    except OSError, ValueError, TypeError:
        return None
    return decision if token_of(data) in (token, "") else None


def write_journal(queue: Path, queued: Queued, entry: JournalEntry) -> None:
    """Write the journal file of a request into the queue (the journal)."""
    record = {
        "format": REQUEST_FORMAT,
        "token": entry.token,
        "branch": entry.branch,
        "final": entry.final,
        "decision": json.loads(encoded_decision(entry.decision, entry.token)),
    }
    cache.write_atomic(queue_file(queue, queued.number, JOURNAL_SUFFIX), (json.dumps(record) + "\n").encode("utf-8"))


def read_journal(queue: Path, number: int) -> JournalEntry | None:
    """Return the journal of a request, or None when the request has no journal that reads (the journal)."""
    try:
        record = json.loads(queue_file(queue, number, JOURNAL_SUFFIX).read_text(encoding="utf-8"))
        token, branch, final = text_list([record.get(key) for key in ("token", "branch", "final")], "journal")
        decision = decoded_decision(json.dumps(record.get("decision")).encode("utf-8"))
    except OSError, ValueError, TypeError, AttributeError:
        return None
    return JournalEntry(token, branch, final, decision)


def is_alive(queue: Path, queued: Queued) -> bool:
    """Tell if the process of a request holds the lock of the request (the holder)."""
    return heavy.is_held(queue_file(queue, queued.number, LOCK_SUFFIX))


def is_claimed(queue: Path, number: int) -> bool:
    """Tell if the holder of a different lane holds the claim of a request (the lanes)."""
    return heavy.is_held(queue_file(queue, number, CLAIM_SUFFIX))


class Claims:
    """The claims of the requests of one batch, with the descriptor of the lock of each claim (the lanes).

    The holder of the batch is the one owner.  free_all() frees each claim that the batch still holds.
    """

    __slots__ = ("descriptors", "queue")

    def __init__(self, queue: Path) -> None:
        """Keep the queue, with no claim."""
        self.queue = queue
        self.descriptors: dict[int, int] = {}

    def take(self, batch: Sequence[Queued]) -> list[Queued]:
        """Take the claim of each request of a batch with no wait, and give the claimed ones.

        A request whose claim a different holder took between the read of the queue and the claim drops out of the
        batch.  Complexity: one open and one lock for each request.
        """
        found: list[Queued] = []
        for queued in batch:
            claim = queue_file(self.queue, queued.number, CLAIM_SUFFIX)
            descriptor = os.open(claim, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                os.close(descriptor)
                continue
            self.descriptors[queued.number] = descriptor
            found.append(queued)
        return found

    def free(self, queued: Queued) -> None:
        """Free the claim of one request, so the holder of a different lane can take it.  A free claim stays free."""
        descriptor = self.descriptors.pop(queued.number, -1)
        if descriptor >= 0:
            os.close(descriptor)

    def free_all(self) -> None:
        """Free each claim that the batch still holds."""
        for descriptor in self.descriptors.values():
            os.close(descriptor)
        self.descriptors.clear()


def has_journal_verdict(queue: Path, queued: Queued) -> bool:
    """Give a request with a journal the verdict that the journal settles, and tell if the request then has a verdict.

    A journal of a swap that did not occur goes, and the function gives False (the journal).  For a journal of a
    swap that occurred, the work tree gets the update of the journal, and then the request gets its verdict.  When
    git cannot tell if the swap occurred, the journal goes, and the request gets the refusal of a tool error.
    """
    journal = queue_file(queue, queued.number, JOURNAL_SUFFIX)
    entry = read_journal(queue, queued.number)
    if entry is None:
        journal.unlink(missing_ok=True)
        return False
    try:
        has_landed = entry.token == queued.token and git.is_ancestor(repository_of(queued), entry.final, entry.branch)
    except git.GitError as problem:
        unknown = refused(
            ExitCode.TOOL_FAILED,
            f"loop land: git cannot tell if {entry.branch} holds the commit {entry.final[:SHORT_HASH]} of the journal "
            f"of this request: {problem}",
            "loop land: the commit can be on the branch.  Look at the branch before you land the request again",
        )
        write_decision(queue, queued, unknown)
        journal.unlink(missing_ok=True)
        return True
    if not has_landed:
        journal.unlink(missing_ok=True)
        return False
    check_report.write_line(
        f"loop land: {queued.name} landed before its holder ended, and gets the verdict of its journal"
    )
    given_update(queued, entry.decision)
    write_decision(queue, queued, entry.decision)
    return True


def removed_orphans(queue: Path) -> None:
    """Remove each file of a request with a verdict or a journal and no lock (the queue)."""
    for path in (*paths.listing(queue, f"*{VERDICT_SUFFIX}"), *paths.listing(queue, f"*{JOURNAL_SUFFIX}")):
        number = number_of(path)
        if number is not None and not heavy.is_held(queue_file(queue, number, LOCK_SUFFIX)):
            remove_request(queue, number)


def removed_dead_holders(queue: Path, now: float | None = None) -> list[str]:
    """Remove the files of the right of way and of the stack of each holder that ended, and return their names.

    A holder frees its files at the end of its batch (Right.free(), Stack.free()).  A holder that a signal or a stop of
    the host ended leaves them, and each reader skips a file with no lock (the right of way, the stack).  The function
    removes a file NUMBER.right or NUMBER.stacked whose lock no process holds, and a file NUMBER.stack or NUMBER.line
    whose file NUMBER.stacked no process holds.  It keeps a file younger than DEAD_HOLDER_AGE_S, because a holder makes
    its file before it takes the lock.  Complexity: linear in the files of the queue.

    Args:
        queue: The directory of the queue
        now: The time of the clock of the file times, or None for the time of this call
    """
    moment = time.time() if now is None else now
    removed: list[str] = []
    paths_of = (
        *paths.listing(queue, f"*{RIGHT_SUFFIX}"),
        *paths.listing(queue, f"*{STACK_LOCK_SUFFIX}"),
        *paths.listing(queue, f"*{STACK_SUFFIX}"),
        *paths.listing(queue, f"*{LINE_SUFFIX}"),
    )
    for path in paths_of:
        number = number_of(path)
        if number is None:
            continue
        is_entry_file = path.name.endswith((STACK_SUFFIX, LINE_SUFFIX))
        lock = queue_file(queue, number, STACK_LOCK_SUFFIX) if is_entry_file else path
        try:
            age_s = moment - path.stat().st_mtime
        except FileNotFoundError:
            continue
        if age_s >= DEAD_HOLDER_AGE_S and not heavy.is_held(lock):
            path.unlink(missing_ok=True)
            removed.append(path.name)
    return sorted(removed)


def waiting_requests(queue: Path, pause: heavy.Pause = time.sleep) -> list[Queued]:
    """Return each request of the queue with no verdict and a live process, in the sequence of the numbers (the holder).

    The function removes the files of each request of a process that ended, and each verdict of a different token.
    It gives a request with a journal the verdict that the journal settles (the journal).  It waits INCOMPLETE_S
    seconds for a live request with no request file, and then it skips it.  A request file that does not read gets a
    verdict of a usage error.  Complexity: linear in the files of the queue, for each look.
    """
    waited = 0.0
    while True:
        removed_orphans(queue)
        removed_dead_holders(queue)
        found: list[Queued] = []
        is_incomplete = False
        for lock in paths.listing(queue, f"*{LOCK_SUFFIX}"):
            number = number_of(lock)
            if number is None:
                continue
            if not heavy.is_held(lock):
                remove_request(queue, number)
                continue
            if is_claimed(queue, number):
                continue
            request_file = request_file_of(queue, number)
            try:
                data = request_file.read_bytes()
            except FileNotFoundError:
                is_incomplete = True
                continue
            except OSError as problem:
                data = f"{problem}".encode()
            verdict_file = queue_file(queue, number, VERDICT_SUFFIX)
            if verdict_file.is_file():
                with contextlib.suppress(OSError):
                    if token_of(verdict_file.read_bytes()) in (token_of(data), ""):
                        continue
                verdict_file.unlink(missing_ok=True)
            try:
                queued = decoded_request(queue, number, data)
            except (ValueError, TypeError) as problem:
                decision = refused(ExitCode.USAGE, f"loop land: the request file does not read: {problem}")
                cache.write_atomic(verdict_file, encoded_decision(decision, token_of(data)))
                continue
            if queue_file(queue, number, JOURNAL_SUFFIX).is_file() and has_journal_verdict(queue, queued):
                continue
            found.append(queued)
        if not is_incomplete or waited >= INCOMPLETE_S:
            return found
        pause(INCOMPLETE_POLL_S)
        waited += INCOMPLETE_POLL_S


def batch_of(found: Sequence[Queued]) -> list[Queued]:
    """Return the requests of the next batch, in their sequence (the holder).

    The batch holds the requests with the git directory and the branch of the first request.
    """
    if not found:
        return []
    first = found[0]
    return [item for item in found if (item.common, item.branch) == (first.common, first.branch)]


def held_gate(
    work: Path, batch: Sequence[Queued], check: Check | None, lane: int = 0
) -> tuple[Check, contextlib.AbstractContextManager[None]]:
    """Return the check of the gate of a batch and the session of the gate that the holder holds (the holder).

    A planted check gets no session.  Else the gate runs on the shared git directory of the first request, with the
    smallest --jobs of the batch, and each run writes its logs into a directory of its own.

    Raises:
        git.GitError: If git cannot find the main work tree of the repository
    """
    if check is not None:
        return check, contextlib.nullcontext()
    jobs = min(item.options.jobs for item in batch)
    is_every = any(item.options.is_every for item in batch)
    repository = repository_of(batch[0])
    cited = functools.partial(cited_runs, work)
    planned = gate.Gate(heavy.main_work_tree(repository), work, jobs, is_every, lane=lane, cited=cited)
    return planned.check_in_run, heavy.gate_session(repository)


def refused_queue(queue: Path, problem: Exception) -> bool:
    """Give each live request of the queue the verdict of an error of the read of the queue (the holder).

    The holder then knows no batch.  The request file of each request gives the token of its verdict.  A request
    with a verdict of its token keeps that verdict.

    Returns:
        True, after one verdict or more

    Raises:
        Exception: The error itself, when the function writes no verdict.  The holder then ends on it, and it does not
            wait for a verdict that no holder writes
    """
    failure = stopped_batch(problem)
    check_report.write_line(failure.lines[0].content, is_error=True)
    count = 0
    for lock in paths.listing(queue, f"*{LOCK_SUFFIX}"):
        number = number_of(lock)
        if number is None or not heavy.is_held(lock) or is_claimed(queue, number):
            continue
        verdict_file = queue_file(queue, number, VERDICT_SUFFIX)
        try:
            token = token_of(request_file_of(queue, number).read_bytes())
            if verdict_file.is_file() and token_of(verdict_file.read_bytes()) == token:
                continue
            cache.write_atomic(verdict_file, encoded_decision(failure, token))
        except OSError:
            continue
        count += 1
    if not count:
        raise problem
    return True


def hold(work: Path, queue: Path, check: Check | None = None, pause: heavy.Pause = time.sleep, lane: int = 0) -> bool:
    """Land one batch of the queue as the holder of the warm tree of a lane, and tell if a batch was there (the holder).

    The caller holds the lock of the warm tree of the lane.  The holder claims each request of its batch (the lanes).
    With no planted check, the batch runs the gate of assay.loop.gate on the shared git directory of its first request.
    It holds the session of the gate, which goes before each other heavy step (the priority of the gate of
    assay.loop.heavy).  Each run of the gate writes its logs into a directory of its own.
    """
    claims = Claims(queue)
    try:
        try:
            found = waiting_requests(queue, pause)
            if lane > 0 and not all(item.is_claiming for item in found):
                return False
            batch = claims.take(batch_of(found))
        except Exception as problem:  # noqa: BLE001
            return refused_queue(queue, problem)
        if not batch:
            return False
        return held_batch(work, queue, batch, check, pause, lane, claims.free)
    finally:
        claims.free_all()


def held_batch(
    work: Path,
    queue: Path,
    batch: Sequence[Queued],
    check: Check | None,
    pause: heavy.Pause,
    lane: int,
    release: Release | None = None,
) -> bool:
    """Land a claimed batch as the holder of the warm tree of a lane, write the records, and give True (the holder).

    `release` frees the claim of a request that waits for the next batch (the lanes).
    """
    decided: set[int] = set()

    def decide(queued: Queued, decision: Decision) -> None:
        """Write the verdict of one request while its process holds the lock, one time."""
        if queued.number in decided:
            return
        decided.add(queued.number)
        if is_alive(queue, queued):
            write_decision(queue, queued, decision)

    landing: BatchLanding | None = None
    try:
        gated, session = held_gate(work, batch, check, lane)
        shown = " ".join(item.name for item in batch)
        check_report.write_line(f"loop land: the holder of the tree of lane {lane + 1} lands the batch {shown}")
        with session:
            right = RightOfWay(queue, batch[0].number, pause)
            stack = Stack(queue, batch[0].number)
            alive = functools.partial(is_alive, queue)
            landing = BatchLanding(batch, gated, decide, pause, alive, Journal(queue), right, stack, release)
            landing.land(batch)
    except Exception as problem:  # noqa: BLE001
        failure = stopped_batch(problem)
        check_report.write_line(failure.lines[0].content, is_error=True)
        settle(batch, decide, landing, failure)
    finally:
        try:
            write_records(work, "land", landing.records() if landing is not None else [], lane)
        except Exception as problem:  # noqa: BLE001
            check_report.write_line(
                f"loop land: the record of the runs cannot be written: {error_text(problem)}", is_error=True
            )
    return True


def hold_lane(work: Path, queue: Path, lane: int) -> bool:
    """Land one batch of the queue as the holder of the warm tree of a lane, with the gate (the holder, the lanes)."""
    return hold(work, queue, lane=lane)


def settle(batch: Sequence[Queued], decide: Decide, landing: BatchLanding | None, failure: Decision) -> None:
    """Give each request of a batch that stopped on an error its verdict (the holder).

    A request that landed gets the verdict of its landing.  A request that waits for the next batch, or that dropped
    out, gets no verdict.  Each other request gets the verdict of the error.  A verdict that cannot be written gives a
    line, and the journal then gives the verdict to the next holder.
    """
    landed: dict[int, Decision] = {}
    skipped: set[int] = set()
    if landing is not None:
        landed = landing.landed
        skipped = {item.number for item in landing.deferred} | set(landing.dropped)
    for queued in batch:
        if queued.number in skipped:
            continue
        try:
            decide(queued, landed.get(queued.number, failure))
        except OSError as problem:
            check_report.write_line(
                f"loop land: the verdict of {queued.name} cannot be written: {problem}", is_error=True
            )


def written(decision: Decision) -> int:
    """Write the lines of a verdict, and return its status."""
    for line in decision.lines:
        check_report.write_line(line.content, is_error=line.is_error)
    return decision.status


def landing(
    work: Path,
    root: Path,
    request: Request,
    options: Options,
    holder: Holder = hold_lane,
    pause: heavy.Pause = time.sleep,
) -> int:
    """Write the request into the queue, wait for its verdict or for the lock, and return its exit (the wait).

    The process then gives its work tree the update of a verdict of a landing (the index and the working tree).

    Raises:
        LandError: If HEAD of the work tree is not on a branch, or a patch cannot be read
        git.GitError: If git cannot read the work tree
    """
    queue = queue_of(work, root)
    with entered(queue, root, request, options) as own:
        check_report.write_line(f"loop land: the request waits in the queue as {own.name}")
        decision = read_decision(queue, own.number, own.token)
        while decision is None:
            with gate.free_lane(work) as lane:
                while lane is not None:
                    decision = read_decision(queue, own.number, own.token)
                    if decision is not None or not holder(work, queue, lane):
                        break
            if decision is None:
                pause(POLL_S)
                decision = read_decision(queue, own.number, own.token)
        if decision.update is not None:
            warnings = updated_tree(root, own.branch, request, decision.update)
            decision = replace(decision, lines=(*decision.lines, *(Line(text, True) for text in warnings)))
    return written(decision)


# ── The check, the preview and the driver ──────────────────────────────────


def held_heavy_lock(root: Path, environment: Mapping[str, str] = os.environ) -> str | None:
    """Tell how this process holds the lock of the heavy steps, or give None (the lock of a heavy step).

    Complexity: linear in the open descriptors of the process.
    """
    if environment.get(heavy.STEP_VARIABLE):
        return f"the variable {heavy.STEP_VARIABLE} of `loop heavy` is set"
    lock = heavy.lock_path(root).resolve()
    for link in paths.listing(DESCRIPTORS):
        try:
            target = Path(os.readlink(link))
        except OSError:
            continue
        if target == lock:
            return f"the descriptor {link.name} names {lock}"
    return None


def checked(root: Path, request: Request, options: Options, work: Path) -> int:
    """Run the gate on the commit of a request as --check does, alone with the lock of the warm tree, and return the exit.

    --check with an empty request checks HEAD itself.  The entry time of the request in its record is the start of the
    call, before the wait for the lock (the record).

    Raises:
        LandError: If HEAD of the work tree is not on a branch
        gate.GateError: If the warm tree cannot hold the commit, or a step of the gate cannot start
        git.GitError: If a git command fails
    """
    decisions: list[Decision] = []
    entry_time = loop_history.iso_now()
    with gate.session(root, work, options.jobs, options.is_every) as session, heavy.gate_session(root):
        run_check = session.check_in_run
        queued = Queued(0, root, request, options, entry_time=entry_time)
        if request.is_empty:
            head = git.head(root)
            candidate = gate.Candidate(head, head, ())
            verdict = run_check(candidate, False)
            run = Run(candidate, (0,), (request.subject,), (entry_time,), verdict)
            write_records(work, "check", [RunRecord(run, (ACCEPTED if verdict.is_accepted else REJECTED,))])
            if verdict.is_accepted:
                accepted_line = Line(f"loop land: the gate accepts HEAD {head[:SHORT_HASH]}")
                return written(Decision(ExitCode.OK, head, (accepted_line, *flaky_of(verdict))))
            problems = (f"loop land: REJECTED, {item}" for item in verdict.problems)
            return written(refused(ExitCode.FINDINGS, *problems, *verdict.rows))
        landing = BatchLanding([queued], run_check, lambda _queued, decision: decisions.append(decision), time.sleep)
        try:
            landing.land([queued])
        finally:
            write_records(work, "check", landing.records())
    return written(decisions[0]) if decisions else ExitCode.TOOL_FAILED


def preview(root: Path, request: Request, options: Options) -> int:
    """Print the diff or the tree of the commit of a request on HEAD, for --dry-run and --tree, with no gate.

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails
    """
    head = git.head(root)
    if request.parts:
        link = series_link(Queued(0, root, request, options), head, "")
        for commit, subject in link.commits:
            check_report.write_line(f"loop land: the series composes {commit[:SHORT_HASH]} {subject}")
        if options.is_dry_run:
            check_report.write_line(git.run(root, "diff", "--binary", head, link.commit).decode("utf-8", "replace"))
        else:
            check_report.write_line(git.run(root, "rev-parse", f"{link.commit}^{{tree}}").decode().strip())
        return ExitCode.OK
    composed = compose(root, request, head, with_diff=options.is_dry_run)
    if options.is_dry_run:
        if not composed.changed:
            raise LandError("the commit would change nothing")
        check_report.write_line(composed.diff.rstrip("\n"))
    else:
        check_report.write_line(composed.tree)
    if composed.built:
        check_report.write_line(
            f"loop land: the composition keeps its own copy of {' '.join(composed.built)}, and the gate writes it from "
            f"the build of the composed tree",
            is_error=True,
        )
    return ExitCode.OK


def driver_tree(root: Path) -> Path | None:
    """Return the main work tree when this module is not its copy there, else None (the driver)."""
    main_tree = heavy.main_work_tree(root)
    if Path(__file__).resolve() == (main_tree / MODULE_PATH).resolve():
        return None
    return main_tree


def run_driver(main_tree: Path, argv: Sequence[str]) -> int:
    """Run the same words with the copy of the main work tree, and return its exit (the driver)."""
    command = [sys.executable, str(main_tree / MASTER_PATH), "land", *argv]
    check_report.write_line(f"loop land: the landing runs the copy of {main_tree}")
    completed = proc.run(
        proc.Command(command, timeout_s=DRIVER_S, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT)
    )
    return heavy.exit_of(completed)


def landed_or_checked(root: Path, request: Request, options: Options, argv: Sequence[str]) -> int:
    """Preview, check or land a request of the work tree of root, and return the exit.

    Raises:
        LandError: If the request does not compose, or this process holds the lock of the heavy steps
        gate.GateError: If a step of the gate cannot start
        git.GitError: If a git command fails
    """
    if options.is_dry_run or options.is_tree_only:
        return preview(root, request, options)
    held = held_heavy_lock(root)
    if held is not None:
        raise LandError(
            f"this process holds the lock of the heavy steps: {held}.  The gate takes that lock for each of its "
            f"steps, so the landing would wait for itself.  Run loop land with no heavy lock"
        )
    main_tree = driver_tree(root)
    if main_tree is not None and not options.is_here:
        return run_driver(main_tree, argv)
    work = cache.work_root() / WORK_NAME
    if options.is_check:
        return checked(root, request, options, work)
    return landing(work, root, request, options)


def main(argv: list[str]) -> int:
    """Land a request, or run the gate on it.

    Args:
        argv: The words after `loop land`

    Returns:
        The exit status
    """
    if argv in HELP_WORDS:
        check_report.write_line(USAGE)
        return ExitCode.OK
    current = Path.cwd()
    try:
        request, options = parse(argv, current)
        root = git.toplevel(current)
        with tempfile.TemporaryDirectory(prefix="loop-land-series-") as scratch:
            if options.series:
                request = series_request(root, options.series, Path(scratch))
            return landed_or_checked(root, request, options, argv)
    except LandError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.USAGE
    except gate.GateError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
    except (git.GitError, proc.ToolFailed) as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
