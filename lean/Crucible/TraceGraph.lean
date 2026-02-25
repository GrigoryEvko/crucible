import Crucible.Basic
import Mathlib.Data.Finset.Basic
import Mathlib.Data.Fin.Basic
import Mathlib.Tactic

/-!
# Crucible.TraceGraph — Bidirectional CSR Property Graph

Models TraceGraph.h: the per-iteration dataflow graph built by the
background thread from recorded ops.

C++ data structures:
  EdgeKind   — enum: DATA_FLOW, ALIAS, CONTROL_FLOW, SCALAR_FLOW
  Edge       — 12 bytes: src(4), dst(4), src_port(1), dst_port(1), kind(1), pad(1)
  TraceGraph — CSR adjacency: fwd_edges/offsets (sorted by src),
               rev_edges/offsets (sorted by dst), ops[], slots[]

Key structural properties formalized:
1. Edge validity: src and dst are within [0, num_ops)
2. No self-loops: src != dst for DATA_FLOW edges
3. DATA_FLOW acyclicity: producer index < consumer index (trace-order)
4. Bidirectional consistency: same edge set in forward and reverse CSR
5. CSR offset monotonicity: offsets[i] <= offsets[i+1]
6. Degree queries: out_degree(i) = fwd_offsets[i+1] - fwd_offsets[i]

C++ build_csr algorithm (O(V+E)):
  Phase 1: count degrees (two passes over edges)
  Phase 2: prefix sum -> offsets
  Phase 3: scatter edges into sorted positions via cursor array

The graph is built once per iteration on the background thread.
Forward CSR answers "who consumes op i's outputs?"
Reverse CSR answers "who produces op i's inputs?"
-/

namespace Crucible

/-! ## Edge Types -/

/-- Edge kinds in the property graph. C++: `enum class EdgeKind : uint8_t`.
    DATA_FLOW = tensor produced by src consumed by dst (from PtrMap tracking).
    ALIAS = same data_ptr from two different ops (views, in-place).
    CONTROL_FLOW = explicit execution ordering (branch targets).
    SCALAR_FLOW = scalar value dependency (e.g. loss.item() -> consumer). -/
inductive EdgeKind where
  | DATA_FLOW
  | ALIAS
  | CONTROL_FLOW
  | SCALAR_FLOW
  deriving DecidableEq, Repr

/-- One edge in the property graph. C++: `struct Edge` (12 bytes).
    Port-level granularity: src_port = which output of src,
    dst_port = which input of dst.

    C++ layout: src(OpIndex 4B), dst(OpIndex 4B), src_port(1B),
    dst_port(1B), kind(EdgeKind 1B), pad(1B). static_assert == 12. -/
structure Edge where
  src      : Nat        -- source op index. C++: OpIndex (uint32_t)
  dst      : Nat        -- destination op index. C++: OpIndex (uint32_t)
  src_port : Nat        -- which output of src (uint8_t)
  dst_port : Nat        -- which input of dst (uint8_t)
  kind     : EdgeKind   -- edge type
  deriving DecidableEq, Repr

/-! ## CSR Representation -/

/-- CSR (Compressed Sparse Row) adjacency structure.
    C++: parallel arrays `edges[]` and `offsets[]` where
    `offsets[i]..offsets[i+1]` indexes into `edges[]` for node i.

    The offset array has `num_nodes + 1` entries.
    `offsets[0] = 0` and `offsets[num_nodes] = edges.length`. -/
structure CSR where
  edges    : List Edge
  offsets  : List Nat     -- length = num_nodes + 1
  numNodes : Nat
  hOffLen  : offsets.length = numNodes + 1
  hFirst   : offsets[0]'(by omega) = 0
  hLast    : offsets[numNodes]'(by omega) = edges.length
  hMono    : ∀ i : Fin numNodes,
               offsets[i.val]'(by have := hOffLen; omega) ≤
               offsets[i.val + 1]'(by have := hOffLen; omega)

/-- Degree of node i in a CSR structure.
    C++: `fwd_offsets[i+1] - fwd_offsets[i]` (out_degree)
    or `rev_offsets[i+1] - rev_offsets[i]` (in_degree). -/
def CSR.degree (csr : CSR) (i : Fin csr.numNodes) : Nat :=
  csr.offsets[i.val + 1]'(by have := csr.hOffLen; omega) -
  csr.offsets[i.val]'(by have := csr.hOffLen; omega)

/-- Edges adjacent to node i in a CSR: the slice
    `edges[offsets[i] .. offsets[i+1])`. -/
def CSR.edgesOf (csr : CSR) (i : Fin csr.numNodes) : List Edge :=
  csr.edges.drop (csr.offsets[i.val]'(by have := csr.hOffLen; omega))
    |>.take (csr.degree i)

/-! ## TraceGraph -/

/-- Bidirectional CSR property graph. C++: `struct TraceGraph`.

    Forward CSR: edges sorted by src ("who consumes op i's outputs?").
    Reverse CSR: edges sorted by dst ("who produces op i's inputs?").

    Both CSR structures contain the same edge set, just sorted differently.
    This enables O(1) degree queries and O(degree) neighbor iteration
    in both directions. -/
structure TraceGraph where
  numOps    : Nat           -- number of ops (nodes)
  numEdges  : Nat           -- total edges
  fwd       : CSR           -- forward: sorted by src
  rev       : CSR           -- reverse: sorted by dst
  hFwdNodes : fwd.numNodes = numOps
  hRevNodes : rev.numNodes = numOps
  hFwdEdges : fwd.edges.length = numEdges
  hRevEdges : rev.edges.length = numEdges

/-- Out-degree of node i: number of consumers of op i's outputs.
    C++: `TraceGraph::out_degree(i)` = `fwd_offsets[i+1] - fwd_offsets[i]`. -/
def TraceGraph.outDegree (g : TraceGraph) (i : Fin g.numOps) : Nat :=
  g.fwd.degree (i.cast g.hFwdNodes.symm)

/-- In-degree of node i: number of producers of op i's inputs.
    C++: `TraceGraph::in_degree(i)` = `rev_offsets[i+1] - rev_offsets[i]`. -/
def TraceGraph.inDegree (g : TraceGraph) (i : Fin g.numOps) : Nat :=
  g.rev.degree (i.cast g.hRevNodes.symm)

/-! ## Well-Formedness Predicates -/

/-- An edge is valid within a graph of `n` ops: both endpoints in bounds. -/
def Edge.valid (e : Edge) (n : Nat) : Prop :=
  e.src < n ∧ e.dst < n

/-- DATA_FLOW edges must not be self-loops. A tensor cannot be
    produced and consumed by the same op in the same port relationship.
    C++: PtrMap tracking guarantees src != dst because the producer
    op must complete before the consumer op is recorded. -/
def Edge.noSelfLoop (e : Edge) : Prop :=
  e.kind = .DATA_FLOW → e.src ≠ e.dst

/-- DATA_FLOW edges respect trace order: producer comes before consumer.
    C++: ops are recorded in execution order. PtrMap records the producing
    op index; consumers always come later in the trace.
    This is the fundamental acyclicity invariant. -/
def Edge.traceOrdered (e : Edge) : Prop :=
  e.kind = .DATA_FLOW → e.src < e.dst

/-- All edges in a graph are valid (endpoints in bounds). -/
def TraceGraph.allValid (g : TraceGraph) : Prop :=
  ∀ e ∈ g.fwd.edges, e.valid g.numOps

/-- No self-loops among DATA_FLOW edges. -/
def TraceGraph.noSelfLoops (g : TraceGraph) : Prop :=
  ∀ e ∈ g.fwd.edges, e.noSelfLoop

/-- DATA_FLOW edges are acyclic (trace-ordered). -/
def TraceGraph.dataFlowAcyclic (g : TraceGraph) : Prop :=
  ∀ e ∈ g.fwd.edges, e.traceOrdered

/-- Forward and reverse CSR contain the same edge multiset. -/
def TraceGraph.bidirectional (g : TraceGraph) : Prop :=
  g.fwd.edges.Perm g.rev.edges

/-- Complete well-formedness: valid + no self-loops + acyclic + bidirectional. -/
def TraceGraph.WellFormed (g : TraceGraph) : Prop :=
  g.allValid ∧ g.noSelfLoops ∧ g.dataFlowAcyclic ∧ g.bidirectional

/-! ## CSR Construction (build_csr model)

C++ `build_csr` in TraceGraph.h:
  1. Count degrees: scan edges, increment fwd_offsets[src+1] and rev_offsets[dst+1]
  2. Prefix sum: fwd_offsets[i] += fwd_offsets[i-1] (and rev)
  3. Scatter: place each edge at cursor position, increment cursor

We model the counting sort as a pure function from a flat edge list
to a CSR structure. The key property: the output CSR contains exactly
the same edges as the input, just sorted by the key function. -/

/-- Count occurrences of each key value in [0, n) across the edge list.
    Models C++ Phase 1 of build_csr: degree counting. -/
def countBy (keyFn : Edge → Nat) (edges : List Edge) (n : Nat) : List Nat :=
  List.ofFn fun (i : Fin n) => edges.countP (fun e => keyFn e = i.val)

/-- Prefix sum. Models C++ Phase 2 of build_csr.
    Input: counts[0..n-1]. Output: offsets[0..n] where
    offsets[0] = 0 and offsets[i+1] = offsets[i] + counts[i]. -/
def prefixSum : List Nat → List Nat
  | [] => [0]
  | c :: cs => 0 :: (prefixSum cs).map (· + c)

theorem prefixSum_length (cs : List Nat) :
    (prefixSum cs).length = cs.length + 1 := by
  induction cs with
  | nil => simp [prefixSum]
  | cons c cs ih => simp [prefixSum, ih]

theorem prefixSum_head (cs : List Nat) (h : 0 < (prefixSum cs).length) :
    (prefixSum cs)[0]'h = 0 := by
  cases cs with
  | nil => simp [prefixSum]
  | cons c cs => simp [prefixSum]

/-! ## Structural Theorems -/

/-- Trace ordering implies no self-loops for DATA_FLOW edges.
    If src < dst, then src != dst. Strict ordering is stronger. -/
theorem traceOrdered_implies_noSelfLoop (e : Edge)
    (h : e.traceOrdered) : e.noSelfLoop := by
  intro hkind hsrc_eq_dst
  have hlt := h hkind
  omega

/-- If all DATA_FLOW edges are trace-ordered, there are no self-loops. -/
theorem acyclic_implies_no_self_loops (g : TraceGraph)
    (h : g.dataFlowAcyclic) : g.noSelfLoops := by
  intro e he
  exact traceOrdered_implies_noSelfLoop e (h e he)

/-- An edge is valid iff swapping src and dst is valid.
    (Validity only checks bounds, not order.) -/
theorem edge_valid_swap (e : Edge) (n : Nat) :
    e.valid n ↔ (Edge.mk e.dst e.src e.dst_port e.src_port e.kind).valid n := by
  simp [Edge.valid]
  exact ⟨fun ⟨a, b⟩ => ⟨b, a⟩, fun ⟨a, b⟩ => ⟨b, a⟩⟩

/-- CSR degree is non-negative (trivially true for Nat). -/
theorem CSR.degree_nonneg (csr : CSR) (i : Fin csr.numNodes) :
    0 ≤ csr.degree i := Nat.zero_le _

/-- Offset array is globally monotone: offsets[0] <= offsets[k] for all k <= n. -/
private theorem offsets_mono_from_zero (offsets : List Nat) (n : Nat)
    (hlen : n + 1 ≤ offsets.length)
    (hmono : ∀ i : Fin n,
      offsets[i.val]'(by omega) ≤ offsets[i.val + 1]'(by omega))
    (k : Nat) (hk : k ≤ n) :
    offsets[0]'(by omega) ≤ offsets[k]'(by omega) := by
  induction k with
  | zero => omega
  | succ j ihj =>
    have hj_lt : j < n := by omega
    have step : offsets[j]'(by omega) ≤ offsets[j + 1]'(by omega) :=
      hmono ⟨j, hj_lt⟩
    have prev := ihj (by omega)
    omega

/-- Telescoping sum over a monotone offset array.
    sum_{i=0}^{n-1} (offsets[i+1] - offsets[i]) = offsets[n] - offsets[0].
    Parameterized so that `n ≤ offsets.length - 1` suffices (no exact length match). -/
private theorem telescoping_sum (offsets : List Nat) (n : Nat)
    (hlen : n + 1 ≤ offsets.length)
    (hmono : ∀ i : Fin n,
      offsets[i.val]'(by omega) ≤ offsets[i.val + 1]'(by omega)) :
    (Finset.univ.sum fun i : Fin n =>
      offsets[i.val + 1]'(by omega) - offsets[i.val]'(by omega)) =
    offsets[n]'(by omega) - offsets[0]'(by omega) := by
  induction n with
  | zero => simp
  | succ k ih =>
    rw [Fin.sum_univ_castSucc]
    simp only [Fin.val_last, Fin.val_castSucc]
    have hmono' : ∀ i : Fin k,
        offsets[i.val]'(by omega) ≤ offsets[i.val + 1]'(by omega) :=
      fun i => hmono ⟨i.val, by omega⟩
    rw [ih (by omega) hmono']
    have hk : offsets[k]'(by omega) ≤ offsets[k + 1]'(by omega) :=
      hmono ⟨k, by omega⟩
    have h0k := offsets_mono_from_zero offsets (k + 1) (by omega) hmono k (by omega)
    omega

/-- Sum of all degrees equals the number of edges.
    C++ invariant: offsets[numNodes] - offsets[0] = num_edges.
    Since offsets[0] = 0 and offsets[numNodes] = edges.length,
    this follows directly from the CSR invariants. -/
theorem CSR.sum_degrees_eq_edges (csr : CSR) :
    (Finset.univ.sum fun i : Fin csr.numNodes => csr.degree i) = csr.edges.length := by
  -- degree i = offsets[i+1] - offsets[i], so this is a telescoping sum
  show (Finset.univ.sum fun i : Fin csr.numNodes =>
    csr.offsets[i.val + 1]'(by have := csr.hOffLen; omega) -
    csr.offsets[i.val]'(by have := csr.hOffLen; omega)) = csr.edges.length
  rw [telescoping_sum csr.offsets csr.numNodes (by have := csr.hOffLen; omega) csr.hMono]
  rw [csr.hFirst, csr.hLast]
  omega

/-! ## Empty Graph -/

/-- The empty CSR (0 nodes, 0 edges). -/
def CSR.empty : CSR where
  edges := []
  offsets := [0]
  numNodes := 0
  hOffLen := by simp
  hFirst := by simp
  hLast := by simp
  hMono := by intro ⟨i, hi⟩; omega

/-- The empty TraceGraph (0 ops, 0 edges). -/
def TraceGraph.empty : TraceGraph where
  numOps := 0
  numEdges := 0
  fwd := CSR.empty
  rev := CSR.empty
  hFwdNodes := rfl
  hRevNodes := rfl
  hFwdEdges := by simp [CSR.empty]
  hRevEdges := by simp [CSR.empty]

/-- Empty graph is well-formed. -/
theorem TraceGraph.empty_wellFormed : TraceGraph.empty.WellFormed := by
  refine ⟨?_, ?_, ?_, ?_⟩
  · intro e he; simp [TraceGraph.empty, CSR.empty] at he
  · intro e he; simp [TraceGraph.empty, CSR.empty] at he
  · intro e he; simp [TraceGraph.empty, CSR.empty] at he
  · exact List.Perm.refl _

/-- Empty graph has zero edges. -/
theorem TraceGraph.empty_numEdges : TraceGraph.empty.numEdges = 0 := rfl

/-! ## Single-Edge Graph -/

/-- Construct a CSR with one node and no edges. -/
def CSR.singleton : CSR where
  edges := []
  offsets := [0, 0]
  numNodes := 1
  hOffLen := by simp
  hFirst := by simp
  hLast := by simp
  hMono := by intro ⟨i, hi⟩; simp at hi; subst hi; simp

/-- DATA_FLOW edge: trace-ordered implies valid no-self-loop edge. -/
theorem dataflow_edge_properties (src dst : Nat) (n : Nat)
    (hsrc : src < n) (hdst : dst < n) (hord : src < dst) :
    let e := Edge.mk src dst 0 0 .DATA_FLOW
    e.valid n ∧ e.noSelfLoop ∧ e.traceOrdered := by
  refine ⟨⟨hsrc, hdst⟩, ?_, ?_⟩
  · intro _ heq; simp at heq; omega
  · intro; exact hord

/-! ## Reachability and Path Properties -/

/-- A path in the graph: sequence of node indices connected by edges. -/
inductive GraphPath (edges : List Edge) : Nat → Nat → Prop where
  | single (u : Nat) : GraphPath edges u u
  | step (u v w : Nat) :
      (∃ e ∈ edges, e.src = u ∧ e.dst = v) →
      GraphPath edges v w →
      GraphPath edges u w

/-- Reachability via DATA_FLOW edges only.
    Used for dependency analysis, topological sort, and fusion decisions. -/
inductive DataFlowPath (edges : List Edge) : Nat → Nat → Prop where
  | single (u : Nat) : DataFlowPath edges u u
  | step (u v w : Nat) :
      (∃ e ∈ edges, e.kind = .DATA_FLOW ∧ e.src = u ∧ e.dst = v) →
      DataFlowPath edges v w →
      DataFlowPath edges u w

/-- DATA_FLOW paths in a trace-ordered graph go strictly forward.
    If there is a DATA_FLOW edge from u to v and a DATA_FLOW path
    from v to w, then u < w. -/
theorem dataflow_path_forward (edges : List Edge)
    (hord : ∀ e ∈ edges, e.traceOrdered)
    {u v : Nat}
    (huv : u < v)
    {w : Nat}
    (hpath : DataFlowPath edges v w) :
    u < w := by
  induction hpath with
  | single => exact huv
  | step v' v'' w' hedge' _ ih =>
    obtain ⟨e', he'_mem, he'_kind, he'_src, he'_dst⟩ := hedge'
    have hv'v'' : v' < v'' := by
      have := hord e' he'_mem he'_kind
      rw [he'_src, he'_dst] at this; exact this
    exact ih (by omega)

/-- DATA_FLOW paths in a trace-ordered graph: if edge u->v exists
    and path v->w exists, then u < w. -/
theorem dataflow_path_ordered (edges : List Edge)
    (hord : ∀ e ∈ edges, e.traceOrdered)
    (u v w : Nat)
    (hedge : ∃ e ∈ edges, e.kind = .DATA_FLOW ∧ e.src = u ∧ e.dst = v)
    (hpath : DataFlowPath edges v w) :
    u < w := by
  obtain ⟨e, he_mem, he_kind, he_src, he_dst⟩ := hedge
  have huv : u < v := by
    have := hord e he_mem he_kind
    rw [he_src, he_dst] at this; exact this
  exact dataflow_path_forward edges hord huv hpath

/-- No non-trivial DATA_FLOW cycles in a trace-ordered graph.
    If edges are trace-ordered, then u cannot reach itself via
    a non-trivial DATA_FLOW path. -/
theorem no_dataflow_cycle (edges : List Edge)
    (hord : ∀ e ∈ edges, e.traceOrdered)
    (u v : Nat)
    (hedge : ∃ e ∈ edges, e.kind = .DATA_FLOW ∧ e.src = u ∧ e.dst = v)
    (hpath : DataFlowPath edges v u) :
    False := by
  have := dataflow_path_ordered edges hord u v u hedge hpath
  omega

/-! ## Source and Sink Nodes -/

/-- A source node has in-degree 0: no incoming edges.
    C++: these are the graph's root nodes (typically parameter loads,
    input tensors from data loader). -/
def TraceGraph.isSource (g : TraceGraph) (i : Fin g.numOps) : Prop :=
  g.inDegree i = 0

/-- A sink node has out-degree 0: no outgoing edges.
    C++: these are the graph's terminal nodes (typically loss outputs,
    metric computations, gradient sinks). -/
def TraceGraph.isSink (g : TraceGraph) (i : Fin g.numOps) : Prop :=
  g.outDegree i = 0

/-! ## Topological Order

The trace itself provides a topological ordering of DATA_FLOW edges:
op 0, op 1, ..., op (n-1). Since DATA_FLOW edges satisfy src < dst
(traceOrdered), the identity permutation is a valid topological sort.

C++ uses this in build_trace Phase 2 to compute DFG edges in a single
forward pass, and in the compiled kernel scheduler for stream assignment. -/

/-- The trace order (identity) is a valid topological ordering.
    For every DATA_FLOW edge, src < dst -- the natural node index order
    respects all dependency edges. -/
theorem trace_is_toposort (g : TraceGraph)
    (h : g.dataFlowAcyclic) :
    ∀ e ∈ g.fwd.edges, e.kind = .DATA_FLOW → e.src < e.dst :=
  fun e he hk => h e he hk

/-! ## Edge Kind Partitioning -/

/-- Count of DATA_FLOW edges. -/
def TraceGraph.numDataFlowEdges (g : TraceGraph) : Nat :=
  g.fwd.edges.countP (·.kind = .DATA_FLOW)

/-- Count of ALIAS edges. -/
def TraceGraph.numAliasEdges (g : TraceGraph) : Nat :=
  g.fwd.edges.countP (·.kind = .ALIAS)

/-- Edge kinds partition the edge set:
    dataflow + alias + control + scalar = total edges. -/
theorem edge_kind_partition (edges : List Edge) :
    edges.countP (·.kind = .DATA_FLOW) +
    edges.countP (·.kind = .ALIAS) +
    edges.countP (·.kind = .CONTROL_FLOW) +
    edges.countP (·.kind = .SCALAR_FLOW) =
    edges.length := by
  induction edges with
  | nil => simp
  | cons e es ih =>
    simp only [List.countP_cons, List.length_cons]
    cases e.kind <;> simp_all <;> omega

/-! ## Bidirectional Navigation -/

/-- Forward query: edges from node i. Models C++:
    `fwd_begin(i)..fwd_end(i)` which yields the slice
    `fwd_edges[fwd_offsets[i]..fwd_offsets[i+1]]`. -/
def TraceGraph.fwdEdges (g : TraceGraph) (i : Fin g.numOps) : List Edge :=
  g.fwd.edgesOf (i.cast g.hFwdNodes.symm)

/-- Reverse query: edges into node i. Models C++:
    `rev_begin(i)..rev_end(i)` which yields the slice
    `rev_edges[rev_offsets[i]..rev_offsets[i+1]]`. -/
def TraceGraph.revEdges (g : TraceGraph) (i : Fin g.numOps) : List Edge :=
  g.rev.edgesOf (i.cast g.hRevNodes.symm)

/-- In a well-formed graph, every forward edge from node i has src = i.
    This is the CSR contract: edges in fwd_offsets[i]..fwd_offsets[i+1]
    all have the same source. -/
def TraceGraph.fwdSourceConsistent (g : TraceGraph) : Prop :=
  ∀ (i : Fin g.numOps) (e : Edge), e ∈ g.fwdEdges i → e.src = i.val

/-- In a well-formed graph, every reverse edge into node i has dst = i. -/
def TraceGraph.revDestConsistent (g : TraceGraph) : Prop :=
  ∀ (i : Fin g.numOps) (e : Edge), e ∈ g.revEdges i → e.dst = i.val

/-! ## Fusion Safety

Two adjacent ops can be fused into a single kernel iff they have a
single producer-consumer DATA_FLOW edge and no other consumers between them.
This section formalizes the safety conditions for kernel fusion. -/

/-- Two ops are fusable: exactly one DATA_FLOW edge from src to dst,
    and dst has exactly one DATA_FLOW input (no other producer).
    C++: fusion candidates in BackgroundThread fusion analysis. -/
def TraceGraph.fusable (g : TraceGraph) (src dst : Fin g.numOps) : Prop :=
  (∃ e ∈ g.fwdEdges src,
    e.kind = .DATA_FLOW ∧ e.dst = dst.val) ∧
  ((g.revEdges dst).countP (·.kind = .DATA_FLOW) = 1)

/-- Fusion preserves trace order: if fusable, src < dst (from acyclicity). -/
theorem fusion_preserves_order (g : TraceGraph)
    (hacyclic : g.dataFlowAcyclic)
    (hfwd : g.fwdSourceConsistent)
    (src dst : Fin g.numOps)
    (hfuse : g.fusable src dst) :
    src.val < dst.val := by
  obtain ⟨⟨e, he_mem, he_kind, he_dst⟩, _⟩ := hfuse
  have he_src : e.src = src.val := hfwd src e he_mem
  have he_in_fwd : e ∈ g.fwd.edges := by
    simp only [TraceGraph.fwdEdges, CSR.edgesOf] at he_mem
    exact List.mem_of_mem_drop (List.mem_of_mem_take he_mem)
  have := hacyclic e he_in_fwd he_kind
  rw [he_src, he_dst] at this
  exact this

end Crucible
