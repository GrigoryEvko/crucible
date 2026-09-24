# Sprout(A): the `opt` mode accepts bag protocols that the `naive` mode rejects

## Summary

On the `bag` network, the `opt` mode of Sprout(A) gives "implementable" for three small protocols, and the `naive` mode gives "non-implementable".
In `opt` mode, two transition pairs with the same four state numbers get the same query file name, and the second query replaces the first.

## Environment

Our build:

| Item | Value |
|---|---|
| Front end | `query-generator/` from `source.zip` of doi 10.5281/zenodo.19600644, sha256 `6e24c2b69ffba4872497d064e0d7e97f759b1e4e83ac07efb8fd375daf2d1520` |
| MuVal | CoAR, github.com/hiroshi-unno/coar, commit `1d49999975b00f1430b3c9d10b90ab00b561e836` |
| OCaml | 5.2.1, opam switch `sprout` |
| dune | 3.18.2 |
| Z3 | the OCaml package `z3` 4.14.1 |
| Host | x86_64, Fedora 44, Linux 7.1.4, 376 hardware threads |

This build is an x86_64 build from source. It is not the arm64 image of the artifact.
The build has four steps:

1. Download `source.zip` and do a check of its sha256.
2. Copy `query-generator/` out of the archive. Set `coar_location` in `config.ml` to the CoAR tree.
3. Download CoAR at the commit, and build `main.exe` with dune.
4. Build `./main.exe` in `query-generator/` with dune.

"How to get the same results" gives the commands.

We also used the image of the artifact:

- The local image is `docker.io/library/sprout-a:latest`, ID `15f4c282c660`, arm64, created 2026-04-15T23:17:42Z. We did not compare it with `sprout-a.tar` of the record.
- `query-generator/` in the image has the same files as `source.zip`, but two files are different. `config.ml` has a different MuVal path, and in `verify_all.sh` the line `"sh"` is a comment.
- Outside `_build`, the MuVal tree of the image has 1,097 files. Each file is the same, byte for byte, as the file in CoAR at the commit. Only two `.DS_Store` files are not in CoAR.
- We started the image under qemu-user-static 10.2.2 with podman 5.8.4 and `--arch arm64`. On each case that we gave it, the decisive verdicts of the image are the same as the verdicts of our build.
- Under qemu, with nine containers at the same time, some GCLTS queries did not complete in 60 s. For the second group of image runs, we set the query budget to 120 s.

## Input format and command

Our test corpus holds global types in a compact syntax, and a translator gives the Sprout(A) input.
Each state of the global type becomes a control state.
Each message becomes a transition `p->q:v{v=n}`.
The value n identifies the message: 0 for a `bool` payload, 1 for a `nat` payload, and k + 2 for branch k of a choice.
The roles are `r0`, `r1`, `r2` and `r3`.

Each result of our build comes from this command, in a new directory that holds only the file `protocol`:

```
query-generator/_build/default/main.exe protocol NETWORK 60 MODE parallel
```

NETWORK is `bag`, `p2pbox`, `senderbox`, `mailbox` or `monobox`, and MODE is `opt` or `naive`.
The query budget is 60 s when a table does not give a different value.

## Verdicts

| Protocol | Network | Our build, `opt` | Our build, `naive` | Image, `opt` | Image, `naive` |
|---|---|---|---|---|---|
| a36 | bag | implementable | non-implementable | implementable (120 s) | non-implementable (60 s) |
| a36 | p2pbox | implementable | implementable | not used | not used |
| a36 | mailbox | non-implementable | non-implementable | not used | not used |
| h2 | bag | implementable | non-implementable | implementable (120 s) | non-implementable (60 s) |
| h2 | p2pbox | implementable | implementable | not used | not used |
| h2 | mailbox | implementable | implementable | not used | not used |
| p_tirore23_eq2 | bag | implementable | non-implementable | implementable (120 s) | non-implementable (120 s) |
| p_tirore23_eq2 | p2pbox | implementable | implementable | not used | not used |
| p_tirore23_eq2 | mailbox | implementable | implementable | not used | not used |
| higher-lower-winning | bag | non-implementable | non-implementable | non-implementable (120 s) | non-implementable (120 s) |

For the three corpus protocols, our build gave the same bag verdicts in four runs of each mode.
On `p2pbox` and on `mailbox`, the two modes agree for these three protocols.
They give different verdicts only on `bag`.

## Case 1: a36

The global type, in our compact syntax:

```
branch(2,1,[msg(0,1,nat,msg(2,0,bool,msg(0,2,nat,branch(0,2,[msg(2,1,nat,rec(branch(2,1,[end]))),rec(branch(2,1,[var,var]))]))))])
```

The same type in the usual notation:

```
r2 → r1 : { l0 :
  r0 → r1 : nat.
  r2 → r0 : bool.
  r0 → r2 : nat.
  r0 → r2 : { l0 : r2 → r1 : nat. r2 → r1 : { l0 : end },
              l1 : μX. r2 → r1 : { l0 : X, l1 : X } } }
```

The Sprout(A) input:

```
Initial state: (0)
Initial register assignments: 
(0) r2->r1:v{v=2} (1)
(1) r0->r1:v{v=1} (2)
(2) r2->r0:v{v=0} (3)
(3) r0->r2:v{v=1} (4)
(4) r0->r2:v{v=2} (5)
(4) r0->r2:v{v=3} (6)
(5) r2->r1:v{v=1} (7)
(6) r2->r1:v{v=2} (6)
(6) r2->r1:v{v=3} (6)
(7) r2->r1:v{v=2} (8)
Final states: (8)
```

The verdicts on `bag`, as printed:

- `opt`: `Implementable`, then `Total verification time: 10.634560s, implementable`. MuVal finds each query invalid.
- `naive`: `Non-implementable`, then `Total verification time: 6.544172s, non-implementable`. MuVal finds `r1_bag_rcc2.hes` valid.

On `mailbox`, the two modes give non-implementable, and MuVal finds `01_mb_pe.hes` valid.
On `p2pbox`, the two modes give implementable.

### A run that stops in a deadlock on bag

We use the definitions of arXiv v1 (2602.10320v1).
A bag buffer is a multiset (Example 4.2).
Insert adds a message, and remove deletes a message that is in the multiset.
The configurations, the transitions and the deadlock are from Definition 4.3.
The local machines are the canonical implementation of Definition 5.1.
Example 4.2 tells that all bag topologies are equivalent, and we show one bag for each receiver.

The notation is from the paper.
`p ⊲ q !m` means that p sends m to q, and `q ⊳ p ?m` means that q receives m from p.

The canonical machine of r1 comes from the projection of the protocol onto r1:

| State of r1 | Transitions | Final |
|---|---|---|
| q0 | `r1 ⊳ r2 ?2` to q1 | no |
| q1 | `r1 ⊳ r0 ?1` to q2 | no |
| q2 | `r1 ⊳ r2 ?1` to q3, `r1 ⊳ r2 ?2` to q5, `r1 ⊳ r2 ?3` to q5 | no |
| q3 | `r1 ⊳ r2 ?2` to q4 | no |
| q4 | none | yes |
| q5 | `r1 ⊳ r2 ?2` to q5, `r1 ⊳ r2 ?3` to q5 | no |

The machines of r0 and r2 come from their projections in the same way.
In branch l0, r0 stops in a final state after it sends 2 to r2.
r2 stops in a final state after it sends 1 and then 2 to r1.

| Step | Event | Bag of r0 | Bag of r1 | Bag of r2 | State of r1 |
|---|---|---|---|---|---|
| 1 | `r2 ⊲ r1 !2` | {} | {2 from r2} | {} | q0 |
| 2 | `r1 ⊳ r2 ?2` | {} | {} | {} | q1 |
| 3 | `r0 ⊲ r1 !1` | {} | {1 from r0} | {} | q1 |
| 4 | `r1 ⊳ r0 ?1` | {} | {} | {} | q2 |
| 5 | `r2 ⊲ r0 !0` | {0 from r2} | {} | {} | q2 |
| 6 | `r0 ⊳ r2 ?0` | {} | {} | {} | q2 |
| 7 | `r0 ⊲ r2 !1` | {} | {} | {1 from r0} | q2 |
| 8 | `r2 ⊳ r0 ?1` | {} | {} | {} | q2 |
| 9 | `r0 ⊲ r2 !2` (branch l0, r0 is final) | {} | {} | {2 from r0} | q2 |
| 10 | `r2 ⊳ r0 ?2` | {} | {} | {} | q2 |
| 11 | `r2 ⊲ r1 !1` | {} | {1 from r2} | {} | q2 |
| 12 | `r2 ⊲ r1 !2` (r2 is final) | {} | {1 from r2, 2 from r2} | {} | q2 |
| 13 | `r1 ⊳ r2 ?2` | {} | {1 from r2} | {} | q5 |

After step 13:

- r0 and r2 are in final states, and they have no transitions.
- r1 is in q5. It can only receive 2 or 3 from r2, and its bag holds only the value 1 from r2.
- No transition is enabled. The configuration is not final, because r1 is not in a final state and a bag is not empty.

By Definition 4.3, this configuration is a deadlock.
The message 1 from r2 to r1 stays in the bag, and no role can move.

Step 13 is not a property of the canonical machine only.
Each implementation of r1 must let step 13 occur.
After steps 2 and 4, r1 has the same local events in branch l0 and in branch l1.
In branch l1, the next event of r1 is `r1 ⊳ r2 ?2`.
Steps 1 to 12 are a prefix of a trace of branch l0, and the bag lets r1 remove the value 2 before the value 1.
After step 13, no protocol run agrees with the local events of r1 and r2.

As a result, the protocol is not implementable on bag (Definition 4.5).
§10 of the paper also tells that a protocol is implementable only if its canonical implementation implements it.

The condition that fails is the second part of Generalized Receive Coherence (Definition 5.2 with r = p and m′ ≠ m).
In §7.1, this part is GRC(b), and the paper applies it only to bag.
The states 5 and 6 are "simultaneously reachable" for r1.
State 5 has `r2->r1:v{v=1}`, and state 6 has `r2->r1:v{v=2}`.
After r2 sends 1 from state 5, the value 2 from r2 can be available to r1.
The `naive` query `r1_bag_rcc2.hes` contains this pair.

## Cases 2 and 3: h2 and p_tirore23_eq2

### h2

```
rec(branch(0,1,[msg(1,2,bool,var),msg(1,2,bool,var)]))
```

In the usual notation: `μX. r0 → r1 : { l0 : r1 → r2 : bool. X, l1 : r1 → r2 : bool. X }`.

```
Initial state: (0)
Initial register assignments: 
(0) r0->r1:v{v=2} (1)
(0) r0->r1:v{v=3} (1)
(1) r1->r2:v{v=0} (0)
Final states: 
```

- `opt` on `bag`: `Total verification time: 1.112558s, implementable`.
- `naive` on `bag`: `Total verification time: 1.330219s, non-implementable`. MuVal finds `r1_bag_rcc2.hes` valid.

### p_tirore23_eq2

```
rec(msg(0,1,nat,branch(2,3,[var,var])))
```

In the usual notation: `μX. r0 → r1 : nat. r2 → r3 : { l0 : X, l1 : X }`.

```
Initial state: (0)
Initial register assignments: 
(0) r0->r1:v{v=1} (1)
(1) r2->r3:v{v=2} (0)
(1) r2->r3:v{v=3} (0)
Final states: 
```

- `opt` on `bag`: `Total verification time: 1.177812s, implementable`.
- `naive` on `bag`: `Total verification time: 1.327956s, non-implementable`. MuVal finds `r3_bag_rcc2.hes` valid.

### Status

The two modes give different verdicts, and we found no run that stops in a deadlock.
A deadlock cannot occur in these two protocols.
A role that only sends (r0 in h2, r0 and r2 in p_tirore23_eq2) always has an enabled send, because insert is always defined (axiom B1, Definition 6.1).

By hand, we found a trace that breaks protocol fidelity (Definition 4.5 (i)).
No tool examined this trace.
For h2, the trace starts with these three events:

1. `r0 ⊲ r1 !2`
2. `r0 ⊲ r1 !3`
3. `r1 ⊳ r0 ?3`

After step 3, the events of r0 are `!2 !3`, and the event of r1 is `?3`.
No protocol run has a first message that is 2 for r0 and 3 for r1.
The canonical machines can extend this prefix to an infinite run, for example with `r1 ⊲ r2 !0`, `r2 ⊳ r1 ?0`, `r1 ⊳ r0 ?2`, and then the loop again.
That infinite trace is in the language of the machines, but it is not in the protocol semantics L_A(S) of §4.
p_tirore23_eq2 has the same trace, with r2 as the sender and r3 as the receiver: `r2 ⊲ r3 !2`, `r2 ⊲ r3 !3`, `r3 ⊳ r2 ?3`.

## Why the `opt` mode does not find the valid query

The code in `source.zip`:

- `main.ml` line 205: on `bag`, `opt` calls `generate_bag_rcc_queries1_opt` and `generate_bag_rcc_queries2_opt`. Line 215: `naive` calls the `_naive` functions.
- `bag_rcc2.ml` lines 294 to 298: `naive` is version v1a, and `opt` is version v2b.
- v1a (lines 192 to 203) writes one file for each receiver. The file holds a disjunction of all transition pairs.
- v2b (lines 261 to 270) writes one file for each transition pair. Line 266 makes the file name from four state numbers only:
  `p ^ "_bag_rcc2_" ^ string_of_int tr1.pre ^ string_of_int tr1.post ^ "_" ^ string_of_int tr2.pre ^ string_of_int tr2.post ^ ".hes"`.
- `common.ml` line 266: `all_transition_pairs` gives each ordered pair, and also each transition with itself.
- `common.ml` line 27: `write_to_file` opens the file with `open_out`, which deletes the old contents of the file.

Two pairs with the same four state numbers get the same file name.
The last of these pairs in the list replaces the files of the pairs before it.

The generated directories of our `opt` runs show this:

| Protocol | File | Pairs (x1, x2) with this name | Pair in the file | MuVal |
|---|---|---|---|---|
| h2 | `r1_bag_rcc2_01_01.hes` | (2, 2), (2, 3), (3, 2), (3, 3) | (3, 3) | invalid |
| p_tirore23_eq2 | `r3_bag_rcc2_10_10.hes` | (2, 2), (2, 3), (3, 2), (3, 3) | (3, 3) | invalid |
| a36 | `r1_bag_rcc2_66_57.hes` | (2, 1), (3, 1) | (3, 1) | invalid |
| a36 | `r1_bag_rcc2_66_66.hes` | (2, 2), (2, 3), (3, 2), (3, 3) | (3, 3) | invalid |

The pair (3, 3) is unsatisfiable, because the query also contains `x1 != x2`.
This is the head of `r1_bag_rcc2_01_01.hes` for h2:

```
exists (x1: int) (x2: int). 
(x1 != x2 /\ 
(prodreach_r1_0_0 )
 /\ 
((x1 = 3))
 /\ 
((x2 = 3))
 /\ 
(bagavail_r0r1_r1 x1 1 )
)
s.t.
```

For a36, the pair (2, 1) of the name `r1_bag_rcc2_66_57.hes` is the pair of the deadlock in Case 1.
Its tr1 is `(6) r2->r1:v{v=2} (6)`, and its tr2 is `(5) r2->r1:v{v=1} (7)`.
After tr2, the protocol is in state 7, where r2 sends 2 to r1.
The pair (3, 1) replaces it, and the value 3 is not available from state 7.

To make sure that this is the cause, we made a copy of each protocol with one more state.
The added state has the same transitions to other states as an old state, and the two protocols have the same runs when you ignore state names.
In the copies, the pairs get different file names, and the two modes agree:

| Protocol | `opt` | `naive` | `opt` files that MuVal finds valid |
|---|---|---|---|
| a36-split | non-implementable | non-implementable | `r1_bag_rcc2_66_57.hes`, `_66_69`, `_69_66`, `_96_99`, `_99_96` |
| h2-split | non-implementable | non-implementable | `r1_bag_rcc2_01_02.hes`, `_02_01` |
| p_tirore23_eq2-split | non-implementable | non-implementable | `r3_bag_rcc2_10_12.hes`, `_12_10` |

In a36-split, the valid file `r1_bag_rcc2_66_57.hes` holds `x1 = 2` and `x2 = 1`.
The image under qemu also gives non-implementable for h2-split in `opt` mode, with the same two valid files.
These are the three copies:

```
a36-split
Initial state: (0)
Initial register assignments: 
(0) r2->r1:v{v=2} (1)
(1) r0->r1:v{v=1} (2)
(2) r2->r0:v{v=0} (3)
(3) r0->r2:v{v=1} (4)
(4) r0->r2:v{v=2} (5)
(4) r0->r2:v{v=3} (6)
(5) r2->r1:v{v=1} (7)
(6) r2->r1:v{v=2} (6)
(6) r2->r1:v{v=3} (9)
(9) r2->r1:v{v=2} (6)
(9) r2->r1:v{v=3} (9)
(7) r2->r1:v{v=2} (8)
Final states: (8)

h2-split
Initial state: (0)
Initial register assignments: 
(0) r0->r1:v{v=2} (1)
(0) r0->r1:v{v=3} (2)
(1) r1->r2:v{v=0} (0)
(2) r1->r2:v{v=0} (0)
Final states: 

p_tirore23_eq2-split
Initial state: (0)
Initial register assignments: 
(0) r0->r1:v{v=1} (1)
(1) r2->r3:v{v=2} (0)
(1) r2->r3:v{v=3} (2)
(2) r0->r1:v{v=1} (1)
Final states: 
```

Two more facts about the code:

- A pair of a transition with itself is a necessary query for a symbolic transition. The valid query of Case 4 has the pair of `(0) a->b:n{...} (1)` with itself. A repair that removes these pairs is not correct. A file name that holds the positions of the two transitions in the list keeps each pair.
- The same name scheme is in `rcc.ml`, `sb_rcc.ml`, `mb_rcc.ml`, `monob_rcc.ml` and `bag_rcc1.ml` (four state numbers), `scc.ml` (`tr.pre`, `tr.post` and, in some versions, `s2`), `nmc.ml` (`tr1.pre` and `tr2.pre`) and `gclts.ml` (`s`, `tr1.post` and `tr2.post`). The numbers have no separator between them. The state pairs (1, 12) and (11, 2) both give `112`. We did not look for a verdict that these files change.

## Case 4: higher-lower-winning on bag

`source.zip` has no examples.
We copied this file from `/home/opam/sprout/examples/sprout/higher-lower-winning` in the image:

```
Initial state: (0)
Initial register assignments: rn=0, rt=0, rx=0
(0) a->b:n{0<=n/\n<100/\rn'=n} (1)
(1) a->b:t{t>rn/\rt'=t} (2)
(2) c->b:x{0<=x/\x<100/\rx'=x} (3)
(3) b->c:higher{higher=3/\rn>rx/\rt>1} (4)
(4) b->a:higher{higher=3/\rt'=rt-1} (2)
(3) b->c:won{won=1/\rn=rx} (5)
(5) b->a:lost{lost=0} (7)
(3) b->c:lower{lower=2/\rn<rx/\rt>1} (8)
(8) b->a:lower{lower=2/\rt'=rt-1} (2)
(3) b->c:lost{lost=0/\rn!=rx/\rt=1} (6)
(6) b->a:won{won=1} (7)
Final states: (7)
```

The row of Table 1 in arXiv v1, with the columns p2p, sb, mb, monob and bag:

```
higher-lower-winning  3  ?  T/O  ✓  36.3s  ×  37.5s  ×  45.1s  ✓  62.2s
```

Our results:

| Network | Query budget | `opt` | `naive` |
|---|---|---|---|
| bag | 60 s | non-implementable | non-implementable |
| bag | 30 s (the value in `verify_all.sh`) | non-implementable | not used |
| bag | 15 s | non-implementable | non-implementable |
| bag, image under qemu | 120 s | non-implementable | non-implementable |
| p2pbox | 60 s | inconclusive | inconclusive |
| senderbox | 60 s | inconclusive | inconclusive |
| mailbox | 60 s | non-implementable | non-implementable |
| monobox | 60 s | non-implementable | non-implementable |

On p2pbox and senderbox, only send-coherence queries (`c_scc`, and also `b_scc` in `naive` mode) stop at the timeout.
Table 1 gives ✓ on senderbox, but a timeout causes our inconclusive result, and we do not report it as a difference.

On bag, MuVal finds `b_bag_rcc2_01_01.hes` valid in `opt` mode and `b_bag_rcc2.hes` valid in `naive` mode.
The MuVal of the image, under qemu, gives `valid,1` for our file `b_bag_rcc2_01_01.hes`.
This is the head of that query:

```
exists (rn1: int) (rt1: int) (rx1: int) (rn'1: int) (rt'1: int) (rx'1: int) (rn2: int) (rt2: int) (rx2: int) (rn'2: int) (rt'2: int) (rx'2: int) (x1: int) (x2: int). 
(x1 != x2 /\ 
(prodreach_b_0_0 rn1 rt1 rx1 rn2 rt2 rx2 )
 /\ 
((((((0 <= x1) /\ (x1 < 100)) /\ (rn'1 = x1)) /\ (rt'1 = rt1)) /\ (rx'1 = rx1)))
 /\ 
((((((0 <= x2) /\ (x2 < 100)) /\ (rn'2 = x2)) /\ (rt'2 = rt2)) /\ (rx'2 = rx2)))
 /\ 
(bagavail_ab_b x1 1 rn'2 rt'2 rx'2 )
)
s.t.
```

The last disjunct of `bagavail_ab_b` is the guard of the message t in state 1:

```
(s = 1 /\ (exists(rn': int)(rt': int)(rx': int).  ((((x1 > rn) /\ (rt' = x1)) /\ (rn' = rn)) /\ (rx' = rx))))
```

We read the query as follows.
The query has the pair of `(0) a->b:n` with itself, with two different values.
After a sends n = x2, the value x1 can be available to b as t, with x1 > x2.
b stays in state 0, and it can receive x1 as n, because 0 <= x1 < 100.

This run on bag stops in a deadlock (Definitions 4.3 and 5.1, Example 4.2):

1. `a ⊲ b !5`. a sends n = 5.
2. `a ⊲ b !7`. a sends t = 7, and the guard 7 > 5 is true.
3. `b ⊳ a ?7`. b receives 7 as n, and the guard 0 <= 7 < 100 is true.
4. `c ⊲ b !50`. c sends x = 50.

After step 4, b must receive t from a with t > 7.
The bag of b holds 5 from a and 50 from c.
The guard t > 7 is false for 5, and b cannot receive from c before it receives t.
a and c wait for a message from b.
No transition is enabled, and the configuration is not final.
By Definition 4.3, this is a deadlock.

The image also holds output files from an earlier run, in `/home/opam/sprout/examples`, with the date 2026-04-13.
`aggregated_output_mailbox.txt` has the line `higher-lower-winning iteration 1:  38.128002s, non-implementable`.
`aggregated_output_monobox.txt` has the line `higher-lower-winning iteration 1:  37.335804s, non-implementable`.
The image has no output file for bag.

To the authors: we want to know how Table 1 got ✓ for this protocol on bag.
We want to know if that run used a different version of the protocol file, of the front end or of MuVal.
If we read the bag semantics incorrectly, we want to know where.
Lemma 7.9 tells that a bag-implementable protocol is p2p-implementable.
Because the p2p cell of this row is ?, the row does not break Lemma 7.9.

## How to get the same results

Make the opam switch (these are the commands of our build script):

```
opam switch create sprout ocaml-base-compiler.5.2.1 --no-switch
opam pin add -n --switch=sprout libsvm.0.10.0~modified \
    git+https://github.com/hiroshi-unno/libsvm-ocaml.git#5d3b175220f865e1ce60909bef11b7e2773451d8
opam install --switch=sprout dune.3.18.2 menhir.20240715 \
    base.v0.17.1 core.v0.17.1 core_kernel.v0.17.0 core_unix.v0.17.0 \
    ppx_deriving.6.0.3 ppx_deriving_yojson.3.9.1 yojson.2.2.2 ppx_expect.v0.17.2 \
    ocaml-compiler-libs.v0.17.0 ocamlgraph.2.1.0 zarith.1.14 z3.4.14.1 \
    minisat.0.6 camlzip.1.13 lacaml.11.1.1 libsvm.0.10.0~modified \
    domainslib.0.5.0 num.1.5-1 logs.0.8.0 fmt.0.10.0 stdio.v0.17.0 \
    sexplib.v0.17.0 ppx_custom_printf.v0.17.0 ppx_compare.v0.17.0 \
    ppx_hash.v0.17.0 ppx_sexp_conv.v0.17.0
```

On Fedora, also install `gmp-devel`, `mpfr-devel`, `blas-devel`, `lapack-devel`, `libzstd-devel` and `zlib-ng-compat-static`.

Build MuVal and the front end:

```
C=coar-1d49999975b00f1430b3c9d10b90ab00b561e836
curl -L -o coar.tar.gz https://github.com/hiroshi-unno/coar/archive/1d49999975b00f1430b3c9d10b90ab00b561e836.tar.gz
tar xzf coar.tar.gz
(cd "$C" && opam exec --switch=sprout -- dune build main.exe)
curl -L -o source.zip https://zenodo.org/api/records/19600644/files/source.zip/content
sha256sum source.zip    # 6e24c2b69ffba4872497d064e0d7e97f759b1e4e83ac07efb8fd375daf2d1520
unzip source.zip 'query-generator/*'
printf 'let coar_location = "%s"\n' "$PWD/$C" > query-generator/config.ml
(cd query-generator && opam exec --switch=sprout -- dune build ./main.exe)
```

Give one protocol to Sprout(A).
Put the text of the protocol into a file `protocol` in a new directory.
In that directory, type these commands:

```
$FRONT/_build/default/main.exe protocol bag 60 opt parallel
$FRONT/_build/default/main.exe protocol bag 60 naive parallel
```

`$FRONT` is the absolute path of `query-generator`.
Use a new directory for each command, or delete `protocol-generated/` and `protocol-generated-gclts/` first.
The front end does not delete the query files of an earlier run, and it gives them to MuVal again.
The generated queries of the command stay in `protocol-generated/`.

The same check in the image, under qemu (the name `a36` is an example):

```
podman run --rm --arch arm64 -v "$PWD/protocol:/data/protocol:ro,z" docker.io/library/sprout-a:latest \
    bash -c 'cp /data/protocol ../examples/sprout/a36 && ./_build/default/main.exe ../examples/sprout/a36 bag 120 opt parallel'
```

MuVal of the image on one query file:

```
podman run --rm --arch arm64 -v "$PWD/b_bag_rcc2_01_01.hes:/data/q.hes:ro,z" docker.io/library/sprout-a:latest \
    bash -c 'cd /home/opam/sprout/muval && ./_build/default/main.exe -c ./config/solver/muval_parallel_tbq_ar.json -p muclp /data/q.hes'
```

## What this report does not show

- We examined the deadlock runs of Cases 1 and 4 and the fidelity trace of Cases 2 and 3 by hand, against the definitions of arXiv v1. No tool examined them.
- We compared our results with Table 1 of arXiv v1 (2602.10320v1). We could not get the ACM version (doi 10.1145/3808319), because the server gave the status HTTP 403.
- We did not compare the local image with `sprout-a.tar` of the record.
- We did not look for a verdict that the same file names change in the other generators. The section about the cause gives the list of these generators.
- Table 1 gives ✓ on bag for 14 more benchmarks of the image. These are two-buyer, double-buffering, plus-minus, ring-max, travel-agency2, send-validity-yes, receive-validity-yes, symbolic-two-bidder-yes, figure12-yes, symbolic-send-validity-yes, symbolic-receive-validity-yes, higher-lower-encrypt-yes, mb-no-p2p-yes and monob-no-mb-no. On bag, our `opt` mode gives implementable for all 14. The `naive` mode gives implementable for 8, and inconclusive for 6 with only send-coherence timeouts. MuVal found no valid query in these `naive` runs. We found no other cell of Table 1 that the file names change.
- An earlier run of our test suite gave non-implementable in `opt` mode on `p2pbox` for two other protocols, where `naive` mode gave implementable. MuVal found `r0_scc_25_1.hes` and `r3_scc_34_2.hes` valid in these runs. In eight more `opt` runs of each protocol, `opt` gave implementable, and we do not know the cause of the earlier result. The file names cannot cause it, because a replaced file removes a query and does not add one. The next code block holds the two protocols.

```
r36
Initial state: (0)
Initial register assignments: 
(0) r0->r2:v{v=0} (1)
(1) r1->r2:v{v=1} (2)
(2) r0->r1:v{v=2} (3)
(2) r0->r1:v{v=3} (4)
(2) r0->r1:v{v=4} (5)
(3) r0->r1:v{v=1} (6)
(4) r0->r1:v{v=0} (7)
(5) r1->r2:v{v=0} (8)
(6) r0->r1:v{v=1} (7)
(8) r0->r2:v{v=0} (7)
Final states: (7)

p_pmy25_ex12_g2_prefixed
Initial state: (0)
Initial register assignments: 
(0) r0->r1:v{v=1} (1)
(1) r3->r2:v{v=1} (2)
(2) r0->r1:v{v=2} (2)
(2) r0->r1:v{v=3} (3)
(3) r3->r2:v{v=1} (4)
Final states: (4)
```
