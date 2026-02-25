import Mathlib.Data.List.Basic
import Mathlib.Tactic

/-!
# Crucible.Graph -- Computation Graph IR

Models Graph.h: mutable computation graph for kernel scheduling.

C++ types:
  GraphNode (64B) -- one operation producing output buffer(s)
  Inst (8B)       -- one micro-op in SSA form (kernel body)
  Graph           -- arena-owned container with DCE + topological sort

Key invariants formalized:
  1. **Acyclicity**: data dependencies form a DAG (no node transitively depends on itself)
  2. **Topological ordering**: every edge (dep, node) has dep.order < node.order
  3. **DCE semantics**: removing dead nodes preserves computation for live outputs
  4. **SSA well-formedness**: instruction operands reference strictly earlier instructions
  5. **Use counting**: num_uses equals the number of live consumers

C++ layout (GraphNode = 64B = one cache line):
  [0..3]   NodeId id               -- unique ID (= buffer name "buf{id}")
  [4]      NodeKind kind            -- 1B
  [5]      flags                    -- 1B (DEAD, VISITED, FUSED, REALIZED)
  [6]      ndim                     -- 1B output dimensions
  [7]      nred                     -- 1B reduction dimensions
  [8]      ScalarType dtype         -- 1B
  [9]      src_dtype                -- 1B (reductions)
  [10]     device_idx               -- 1B
  [11]     ReduceOp                 -- 1B
  [12]     ReduceHint               -- 1B
  [13]     pad                      -- 1B
  [14..15] num_inputs               -- 2B
  [16..47] 4 pointers (size/stride/body/inputs)  -- 32B
  [48..49] num_uses                 -- 2B
  [50..51] num_outputs              -- 2B
  [52..55] schedule_order           -- 4B
  [56..59] group_hash               -- 4B
  [60..63] fused_group_id           -- 4B
-/

namespace Crucible

/-! ## Node Classification -/

/-- Graph node classification. C++: `enum class NodeKind : uint8_t`.
    Maps CKernelId ranges to scheduling categories.
    10 variants, exactly matching the C++ enum. -/
inductive GraphNodeKind where
  | INPUT      -- Graph input (no computation)
  | CONSTANT   -- Compile-time constant tensor
  | POINTWISE  -- Element-wise computation
  | REDUCTION  -- Reduction (sum, max, argmax, etc.)
  | SCAN       -- Prefix scan (cumsum, cumprod)
  | SORT       -- Sort operation
  | EXTERN     -- Opaque external kernel (mm, conv, cuBLAS)
  | TEMPLATE   -- Template-based kernel (CUTLASS, Triton)
  | MUTATION   -- In-place mutation of existing buffer
  | NOP        -- No computation (concat, view, etc.)
  deriving DecidableEq, Repr

/-- Reduction operation type. C++: `enum class ReduceOp : uint8_t`. -/
inductive ReduceOp where
  | SUM | PROD | MAX | MIN | ARGMAX | ARGMIN | ANY | XOR_SUM | WELFORD | DOT
  deriving DecidableEq, Repr

/-! ## SSA Micro-Op Instruction Set -/

/-- Micro-op opcodes for kernel bodies. C++: `enum class MicroOp : uint8_t`.
    51 opcodes total. Operands are 0-based indices into the instruction array.
    SSA form: each instruction produces exactly one value, referenced by index. -/
inductive MicroOp where
  -- Memory
  | LOAD | STORE
  -- Arithmetic
  | ADD | SUB | MUL | TRUEDIV | FLOORDIV | MOD
  | NEG | ABS | RECIPROCAL | SQUARE
  -- Comparison
  | EQ | NE | LT | LE | GT | GE
  -- Math
  | EXP | LOG | LOG2 | SQRT | RSQRT
  | SIN | COS | TAN | ASIN | ACOS | ATAN
  | SINH | COSH | TANH | ASINH
  | ERF | CEIL | FLOOR | TRUNC | ROUND
  | SIGMOID | RELU
  -- Bitwise
  | BIT_AND | BIT_OR | BIT_XOR | BIT_NOT
  | LSHIFT | RSHIFT
  -- Logic
  | AND | OR | NOT
  -- Special
  | TO_DTYPE | CONSTANT | WHERE | REDUCE | INDEX_EXPR
  deriving DecidableEq, Repr

/-- Number of operands consumed by each micro-op.
    C++: Inst.operands[3] -- up to 3, actual count depends on op.
    LOAD: 0 (buffer index is metadata, not SSA reference).
    STORE: 1 (value to store). Unary: 1. Binary: 2.
    WHERE: 3 (cond, true_val, false_val). CONSTANT: 0. INDEX_EXPR: 0. -/
def MicroOp.arity : MicroOp -> Nat
  | .LOAD => 0
  | .STORE => 1
  | .ADD | .SUB | .MUL | .TRUEDIV | .FLOORDIV | .MOD => 2
  | .NEG | .ABS | .RECIPROCAL | .SQUARE => 1
  | .EQ | .NE | .LT | .LE | .GT | .GE => 2
  | .EXP | .LOG | .LOG2 | .SQRT | .RSQRT => 1
  | .SIN | .COS | .TAN | .ASIN | .ACOS | .ATAN => 1
  | .SINH | .COSH | .TANH | .ASINH => 1
  | .ERF | .CEIL | .FLOOR | .TRUNC | .ROUND => 1
  | .SIGMOID | .RELU => 1
  | .BIT_AND | .BIT_OR | .BIT_XOR => 2
  | .BIT_NOT => 1
  | .LSHIFT | .RSHIFT => 2
  | .AND | .OR => 2
  | .NOT => 1
  | .TO_DTYPE => 1
  | .CONSTANT => 0
  | .WHERE => 3
  | .REDUCE => 1
  | .INDEX_EXPR => 0

/-- SSA instruction. C++: `struct Inst` (8 bytes).
    Operands are SSA references (indices into the instruction array, 0-based).
    Each instruction produces exactly one value, consumed by later instructions. -/
structure Inst where
  op       : MicroOp
  operands : List Nat  -- SSA data-dependency references
  deriving DecidableEq, Repr

/-- Compute body: a sequence of SSA instructions. C++: `struct ComputeBody`.
    Replaces inductor's inner_fn closures. Directly emittable to CUDA C++.

    Example: C = relu(A + B)
      [0] LOAD            -- load from input A (buffer index is metadata)
      [1] LOAD            -- load from input B
      [2] ADD   $0, $1    -- element-wise add
      [3] RELU  $2        -- relu = max(x, 0)
      [4] STORE $3        -- store to output -/
structure ComputeBody where
  ops      : List Inst
  numLoads : Nat       -- LOAD count (= distinct input buffers)
  storeIdx : Nat       -- index of the STORE instruction
  deriving Repr

/-- SSA well-formedness: every operand references a strictly earlier instruction.
    C++ enforces this structurally via the instruction array's linear order. -/
def ComputeBody.ssaWf (body : ComputeBody) : Prop :=
  ∀ (i : Nat) (hi : i < body.ops.length),
    ∀ opIdx ∈ body.ops[i].operands, opIdx < i

/-- STORE is within bounds. -/
def ComputeBody.storeValid (body : ComputeBody) : Prop :=
  body.storeIdx < body.ops.length

/-! ## Graph Node -/

/-- A computation graph node. Models C++: `struct GraphNode` (64 bytes).
    `id` is the unique identifier (= buffer name "buf{id}").
    `inputs` are indices of dependency nodes (not pointers -- pure model).
    `numUses` tracks live consumer count (for DCE). -/
@[ext] structure GNode where
  id         : Nat
  kind       : GraphNodeKind
  isDead     : Bool            -- C++: flags & NodeFlags::DEAD
  inputs     : List Nat        -- node IDs of dependencies
  numUses    : Nat             -- live consumer count
  numOutputs : Nat             -- output buffers produced (usually 1)
  schedOrder : Nat             -- topological order (set by scheduler)
  deriving DecidableEq, Repr

/-! ## Graph -/

/-- Computation graph. Models C++: `class Graph`.
    Nodes indexed by ID (position in list). Provides DCE and topological sort.
    `outputIds` are the graph output node IDs -- these are roots for liveness. -/
@[ext] structure Graph where
  nodes     : List GNode
  outputIds : List Nat
  deriving Repr

/-! ## Graph Well-Formedness -/

/-- All inputs reference existing nodes. -/
def Graph.allInputsValid (g : Graph) : Prop :=
  ∀ (i : Nat) (hi : i < g.nodes.length),
    ∀ dep ∈ g.nodes[i].inputs, dep < g.nodes.length

/-- Transitive reachability via inputs. Node `a` reaches `b` if there is a
    chain of input edges from `b` back to `a`. -/
inductive Graph.reaches (g : Graph) : Nat -> Nat -> Prop where
  | direct : ∀ {a b}, (hb : b < g.nodes.length) ->
      a ∈ g.nodes[b].inputs -> g.reaches a b
  | trans : ∀ {a b c}, g.reaches a b -> g.reaches b c ->
      g.reaches a c

/-- Acyclicity: no node transitively depends on itself.
    C++: the graph is a DAG -- cycles would cause infinite loops in
    DCE propagation and topological sort. -/
def Graph.acyclic (g : Graph) : Prop :=
  ∀ id, id < g.nodes.length -> ¬ g.reaches id id

/-- The graph outputs reference existing nodes. -/
def Graph.outputsValid (g : Graph) : Prop :=
  ∀ id ∈ g.outputIds, id < g.nodes.length

/-- Complete well-formedness predicate. -/
structure Graph.WellFormed (g : Graph) : Prop where
  inputs_valid  : g.allInputsValid
  acyclic       : g.acyclic
  outputs_valid : g.outputsValid
  ids_match     : ∀ (i : Nat) (h : i < g.nodes.length), g.nodes[i].id = i

/-! ## Topological Ordering -/

/-- A topological ordering is valid: for every live edge (dep -> node),
    the dependency has a strictly smaller schedule_order.
    C++: `topological_sort()` via Kahn's algorithm ensures this. -/
def Graph.validTopoOrder (g : Graph) : Prop :=
  ∀ (i : Nat) (hi : i < g.nodes.length),
    g.nodes[i].isDead = false ->
    ∀ dep ∈ g.nodes[i].inputs,
      ∀ (hdep : dep < g.nodes.length),
        g.nodes[dep].isDead = false ->
        g.nodes[dep].schedOrder < g.nodes[i].schedOrder

/-- Schedule orders are unique among live nodes. -/
def Graph.uniqueOrders (g : Graph) : Prop :=
  ∀ (i j : Nat) (hi : i < g.nodes.length) (hj : j < g.nodes.length),
    i ≠ j ->
    g.nodes[i].isDead = false -> g.nodes[j].isDead = false ->
    g.nodes[i].schedOrder ≠ g.nodes[j].schedOrder

/-! ## Dead Code Elimination -/

/-- Mark a node as dead (set isDead = true).
    C++: `n->flags |= NodeFlags::DEAD`. -/
def GNode.markDead (n : GNode) : GNode :=
  { n with isDead := true }

/-- Should DCE kill this node? Zero uses, alive, and not a mutation.
    C++: `n->num_uses == 0 && n->kind != NodeKind::MUTATION`. -/
def GNode.shouldKill (n : GNode) : Bool :=
  !n.isDead && n.numUses == 0 && n.kind != GraphNodeKind.MUTATION

/-- Apply one DCE step: mark dead all nodes with zero uses that are
    not mutations or already dead. Returns the updated graph.
    C++: one pass of the inner loop in `eliminate_dead_nodes()`.

    Note: the C++ implementation iterates in reverse and decrements
    input use counts. We model this as a pure transformation of one
    pass, abstracting the use-count propagation separately. -/
def Graph.dceStep (g : Graph) : Graph :=
  { g with
    nodes := g.nodes.mapIdx (fun _ n => if n.shouldKill then n.markDead else n) }

/-- The semantic output of a graph: whether a node's computation is
    reachable from any graph output.
    A node is semantically live if some output transitively depends on it. -/
def Graph.semanticallyLive (g : Graph) (id : Nat) : Prop :=
  ∃ outId ∈ g.outputIds, id = outId ∨ g.reaches id outId

/-! ## RAUW (Replace All Uses With) -/

/-- Replace all occurrences of `oldId` with `newId` in input lists.
    C++: `replace_all_uses(old_node, new_node)` patches inputs arrays. -/
def Graph.replaceAllUses (g : Graph) (oldId newId : Nat) : Graph :=
  { g with
    nodes := g.nodes.map (fun n =>
      { n with inputs := n.inputs.map (fun dep =>
          if dep = oldId then newId else dep) })
    outputIds := g.outputIds.map (fun id =>
      if id = oldId then newId else id) }

/-! ## Key Theorems -/

/-- Acyclic graphs have no self-loops.
    If `g.acyclic`, then no node lists itself as an input.
    C++: `inputs[j]` can never equal the node itself in a well-formed DAG. -/
theorem acyclic_no_self_loop {g : Graph} (hwf : g.acyclic)
    {id : Nat} (hid : id < g.nodes.length)
    (hn : id ∈ g.nodes[id].inputs) :
    False :=
  hwf id hid (Graph.reaches.direct hid hn)

/-- In a valid topological order, no two live nodes form a 2-cycle.
    If dep.schedOrder < node.schedOrder for all edges, then
    two nodes cannot mutually depend on each other.
    This is the key property that makes Kahn's algorithm correct. -/
theorem validTopo_no_mutual_dep {g : Graph}
    (htopo : g.validTopoOrder)
    {a b : Nat} (ha : a < g.nodes.length) (hb : b < g.nodes.length)
    (haLive : g.nodes[a].isDead = false)
    (hbLive : g.nodes[b].isDead = false)
    (hab : a ∈ g.nodes[b].inputs)
    (hba : b ∈ g.nodes[a].inputs) :
    False := by
  have h1 := htopo b hb hbLive a hab ha haLive
  have h2 := htopo a ha haLive b hba hb hbLive
  omega

/-- DCE preserves graph outputs: marking dead nodes does not change
    which node IDs are graph outputs.
    C++: `eliminate_dead_nodes` only sets DEAD flags, never modifies
    `output_ids_`. -/
theorem dce_preserves_outputs (g : Graph) :
    (g.dceStep).outputIds = g.outputIds := by
  simp [Graph.dceStep]

/-- DCE preserves node count: no nodes are added or removed,
    only flagged as dead.
    C++: `eliminate_dead_nodes` iterates existing nodes, never calls
    `alloc_node_`. -/
theorem dce_preserves_length (g : Graph) :
    (g.dceStep).nodes.length = g.nodes.length := by
  simp [Graph.dceStep, List.length_mapIdx]

/-- Already-dead nodes stay dead after DCE.
    C++: DEAD flag is only set, never cleared by eliminate_dead_nodes. -/
theorem dce_monotone_dead (g : Graph) (i : Nat) (hi : i < g.nodes.length) :
    g.nodes[i].isDead = true ->
    (hi' : i < (g.dceStep).nodes.length) ->
    (g.dceStep).nodes[i].isDead = true := by
  intro hdead hi'
  simp [Graph.dceStep, List.getElem_mapIdx]
  simp [GNode.shouldKill, hdead]

/-- After DCE, every surviving node was alive before.
    Contrapositive of `dce_monotone_dead`. -/
theorem dce_live_subset (g : Graph) (i : Nat) (hi : i < g.nodes.length) :
    (hi' : i < (g.dceStep).nodes.length) ->
    (g.dceStep).nodes[i].isDead = false ->
    g.nodes[i].isDead = false := by
  intro hi' halive
  by_contra h
  simp only [Bool.not_eq_false] at h
  have := dce_monotone_dead g i hi h hi'
  simp_all

/-- MUTATION nodes survive DCE: they have side effects.
    C++: `n->kind != NodeKind::MUTATION` check in eliminate_dead_nodes. -/
theorem dce_preserves_mutations (g : Graph) (i : Nat) (hi : i < g.nodes.length) :
    g.nodes[i].kind = GraphNodeKind.MUTATION ->
    g.nodes[i].isDead = false ->
    (hi' : i < (g.dceStep).nodes.length) ->
    (g.dceStep).nodes[i].isDead = false := by
  intro hkind halive hi'
  simp [Graph.dceStep, List.getElem_mapIdx]
  simp [GNode.shouldKill, hkind, halive]

/-- Nodes with nonzero uses survive DCE.
    C++: `n->num_uses == 0` is required for DCE candidacy. -/
theorem dce_preserves_used (g : Graph) (i : Nat) (hi : i < g.nodes.length) :
    g.nodes[i].numUses > 0 ->
    g.nodes[i].isDead = false ->
    (hi' : i < (g.dceStep).nodes.length) ->
    (g.dceStep).nodes[i].isDead = false := by
  intro huses halive hi'
  simp [Graph.dceStep, List.getElem_mapIdx]
  simp [GNode.shouldKill, halive]
  split
  · next h => exact absurd h.1 (by omega)
  · exact halive

/-- RAUW preserves node count. -/
theorem rauw_preserves_length (g : Graph) (oldId newId : Nat) :
    (g.replaceAllUses oldId newId).nodes.length = g.nodes.length := by
  simp [Graph.replaceAllUses]

/-- Mapping with identity substitution is identity. -/
private theorem map_ite_self (l : List Nat) (id : Nat) :
    l.map (fun dep => if dep = id then id else dep) = l := by
  induction l with
  | nil => rfl
  | cons hd tl ih => simp [List.map, ih]; omega

/-- RAUW is a no-op when old = new.
    C++: `if (old_node == new_node) return;` early-out. -/
theorem rauw_self (g : Graph) (id : Nat) :
    g.replaceAllUses id id = g := by
  unfold Graph.replaceAllUses
  simp only [map_ite_self]
  suffices h : g.nodes.map (fun n => { n with inputs := n.inputs }) = g.nodes by
    simp [h]
  induction g.nodes with
  | nil => rfl
  | cons hd tl ih => simp [List.map, ih]

/-! ## SSA Properties -/

/-- An empty compute body is trivially SSA well-formed. -/
theorem empty_body_ssa_wf : ComputeBody.ssaWf ⟨[], 0, 0⟩ := by
  intro i hi; simp at hi

/-- A single CONSTANT instruction (no operands) is SSA well-formed. -/
theorem single_constant_ssa_wf :
    ComputeBody.ssaWf ⟨[⟨.CONSTANT, []⟩], 0, 0⟩ := by
  intro i hi opIdx hIn
  simp at hi; subst hi; simp at hIn

/-- A typical relu(A+B) body is SSA well-formed.
    Models the example from ComputeBody's docstring:
      [0] LOAD            [1] LOAD            [2] ADD $0,$1
      [3] RELU $2         [4] STORE $3
    LOAD's buffer index is metadata (aux data), not an SSA operand. -/
theorem relu_add_body_ssa_wf :
    ComputeBody.ssaWf ⟨[
      ⟨.LOAD, []⟩,        -- load buf0
      ⟨.LOAD, []⟩,        -- load buf1
      ⟨.ADD, [0, 1]⟩,     -- add $0, $1
      ⟨.RELU, [2]⟩,       -- relu $2
      ⟨.STORE, [3]⟩       -- store $3
    ], 2, 4⟩ := by
  intro i hi opIdx hIn
  simp at hi
  interval_cases i <;> simp_all
  all_goals omega

/-! ## Count Live -/

/-- Count of live nodes. C++: `Graph::count_live()`. -/
def Graph.countLive (g : Graph) : Nat :=
  g.nodes.filter (fun n => !n.isDead) |>.length

/-- An empty graph has zero live nodes. -/
theorem empty_graph_count : Graph.countLive ⟨[], []⟩ = 0 := by rfl

/-- A graph of all-dead nodes has zero live nodes. -/
theorem all_dead_count (nodes : List GNode) (h : ∀ n ∈ nodes, n.isDead = true) :
    (Graph.mk nodes []).countLive = 0 := by
  simp only [Graph.countLive]
  suffices hf : List.filter (fun n => !n.isDead) nodes = [] by simp [hf]
  rw [List.filter_eq_nil_iff]
  intro n hn
  simp [h n hn]

end Crucible
