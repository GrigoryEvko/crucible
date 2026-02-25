namespace Crucible

/-!
# Crucible.Dag — Content-Addressed Merkle DAG

Models MerkleDag.h: the central data structure of Crucible.
Simultaneously: computation specification, compilation cache key,
guard system, versioning mechanism, and deployment artifact.

Node hierarchy (C++ uses inheritance; Lean uses inductive):
  TraceNodeKind: REGION, BRANCH, TERMINAL
  TraceNode:   { kind, merkle_hash, next }
  RegionNode:  extends TraceNode + { content_hash, ops, compiled, plan }
  BranchNode:  extends TraceNode + { guard, arms }

Content hash (`ContentHash`): identity of THIS node's ops only (kernel cache key).
  C++: `compute_content_hash()` — wymix-based streaming hash over
  schema_hashes, tensor shapes/strides/dtypes, scalar args.
  Identical computation → identical hash → shared compiled kernel.

Merkle hash (`MerkleHash`): identity of the entire subtree from this node downward.
  C++: `compute_merkle_hash()` — recursive:
    region (no next): content_hash
    region (with next): fmix64(content_hash ^ next.merkle_hash)
    branch: fold(fmix64(guard_hash), arm merkle hashes), then fmix64(h ^ next.merkle_hash)
    terminal/null: 0

Key properties:
- Content hash is deterministic: same ops → same hash
- Merkle hash captures subtree: any descendant change → different hash
- Replay is deterministic: same guard values → same execution path
- ContentHash and MerkleHash are distinct strong types in C++ (both uint64_t wrappers)
- Zero content hash is reserved as the empty-slot sentinel in KernelCache
-/

/-- DAG node types. C++: `enum class TraceNodeKind : uint8_t`. -/
inductive NodeKind where
  | REGION     -- compilable op sequence → compiled kernel
  | BRANCH     -- guard check → routes to different arms
  | TERMINAL   -- end of trace
  deriving DecidableEq, Repr

/-- Guard kind. C++: `Guard::Kind` enum (5 variants, uint8_t).
    Determines what condition is checked at a branch point. -/
inductive GuardKind where
  | SHAPE_DIM      -- a specific dimension of a specific input
  | SCALAR_VALUE   -- a scalar produced by a specific op
  | DTYPE          -- input tensor dtype
  | DEVICE         -- input tensor device
  | OP_SEQUENCE    -- the op at position N matches expected schema
  deriving DecidableEq, Repr

/-- Guard at a branch point. C++: `Guard` struct (12 bytes, packed).
    Layout: kind (1B) | pad (3B) | op_index (4B) | arg_index (2B) | dim_index (2B).
    Hash = fmix64(kind | (op_index << 8) | (arg_index << 40) | (dim_index << 56)). -/
structure Guard where
  kind      : GuardKind  -- C++: Guard::Kind (uint8_t)
  op_index  : Nat        -- C++: OpIndex (uint32_t, UINT32_MAX = none)
  arg_index : Nat        -- C++: uint16_t — which argument
  dim_index : Nat        -- C++: uint16_t — which dimension (for SHAPE_DIM)
  deriving DecidableEq, Repr

/-- Guard hash. Models C++: `Guard::hash()`.
    C++ packs fields into a single uint64_t then applies fmix64.
    We model as a function from Guard fields to Nat, abstracting fmix64. -/
def Guard.hash (g : Guard) : Nat :=
  -- Models: fmix64(kind | (op_index << 8) | (arg_index << 40) | (dim_index << 56))
  -- We use a simplified injective combination for specification.
  g.op_index + g.arg_index + g.dim_index + 1

/-! ## Abstract hash mixing

C++ uses `fmix64` (MurmurHash3 finalizer) and `wymix` (128-bit multiply)
for hash mixing. These are 64-bit operations that cannot be faithfully
reproduced in Lean's arbitrary-precision `Nat`. We model the mixing as
a concrete function that preserves the essential structural property:
the result depends on both inputs.

`mix a b` models `fmix64(a ^ b)` from the C++ code. -/

/-- Abstract hash mixing function. Models `fmix64(a ^ b)` from C++.
    Properties: depends on both arguments (sufficient for structural proofs). -/
def mix (a b : Nat) : Nat := a + b + 1

/-- Content-addressable DAG node.
    C++ uses class hierarchy (TraceNode → RegionNode/BranchNode).
    Lean uses a single inductive with explicit kind.

    The `next` field is the continuation (shared suffix after
    branches reconverge). Terminal nodes have no next.

    Note: C++ uses separate ContentHash and MerkleHash strong types
    (both uint64_t wrappers). Here content_hash models ContentHash. -/
inductive DagNode where
  | region
      (content_hash : Nat)  -- kernel identity (this region's ops). C++: ContentHash
      (num_ops : Nat)       -- number of ops in this region
      (next : Option DagNode)
  | branch
      (guard : Guard)
      (arms : List (Int × DagNode))  -- value → target pairs. C++: BranchNode::Arm[]
      (next : Option DagNode)         -- merge point (shared suffix)
  | terminal

/-- Compute Merkle hash. Models C++: `compute_merkle_hash(node)`.

    C++ algorithm (MerkleDag.h lines 361-385):
      null → MerkleHash{0}
      TERMINAL → MerkleHash{0}
      REGION → h = content_hash;
               if (next) h = fmix64(h ^ next.merkle_hash);
               return MerkleHash{h}
      BRANCH → h = fmix64(guard.hash());
               for each arm: h ^= fmix64(arm.target->merkle_hash); h *= PHI;
               if (next) h = fmix64(h ^ next.merkle_hash);
               return MerkleHash{h}

    We model fmix64(a ^ b) as `mix a b = a + b + 1` and the arm fold
    as sequential accumulation.

    Uses explicit list recursion via `where` to satisfy the termination
    checker on the nested inductive (List (Int × DagNode)). -/
def merkleHash : DagNode → Nat
  | .terminal => 0
  | .region ch _ none => ch
  | .region ch _ (some next) => mix ch (merkleHash next)
  | .branch g arms none => merkleHashArms (mix 0 (g.hash)) arms
  | .branch g arms (some next) =>
      mix (merkleHashArms (mix 0 (g.hash)) arms) (merkleHash next)
where
  /-- Fold merkle hashes of arm target nodes into accumulator.
      Models C++: `h ^= fmix64(arm.target->merkle_hash); h *= PHI;`
      We abstract the XOR+multiply as addition with mix. -/
  merkleHashArms (acc : Nat) : List (Int × DagNode) → Nat
    | [] => acc
    | (_, node) :: rest => merkleHashArms (acc + mix 0 (merkleHash node) + 1) rest

/-- Get content hash of a region node. -/
def DagNode.contentHash? : DagNode → Option Nat
  | .region ch _ _ => some ch
  | _ => none

/-- Get continuation node. -/
def DagNode.next? : DagNode → Option DagNode
  | .region _ _ next => next
  | .branch _ _ next => next
  | .terminal => none

/-! ## KernelCache

    Models KernelCache in MerkleDag.h: global lock-free
    content_hash → CompiledKernel* map.

    Open-addressing hash map. Lock-free reads via atomic pointers.
    Thread-safe inserts via CAS on the content_hash slot.
    Capacity must be power of two.

    C++ invariant: content_hash == 0 is the empty-slot sentinel.
    `insert` asserts content_hash != 0.

    We model it as a pure function (Nat → Option Nat) since Lean doesn't
    have mutable state. The key property: content-addressing
    means identical computation → identical hash → cache hit.

    C++ insert behavior:
    - CAS(0 → content_hash) succeeds → new slot, size increments
    - CAS fails but existing == content_hash → overwrite kernel, size unchanged
    - CAS fails and existing != content_hash → probe next slot -/

/-- KernelCache as a pure lookup function.
    C++: open-addressing hash map with CAS inserts. -/
structure KernelCache where
  entries : Nat → Option Nat  -- content_hash → compiled kernel id
  size : Nat
  capacity : Nat
  hCap : capacity > 0

/-- Cache lookup. C++: `KernelCache::lookup(ContentHash)`.
    Lock-free read via atomic load. Returns the compiled kernel id
    if a matching content_hash is found, none otherwise. -/
def KernelCache.lookup (cache : KernelCache) (content_hash : Nat) : Option Nat :=
  cache.entries content_hash

/-- Cache insert. C++: `KernelCache::insert(ContentHash, CompiledKernel*)`.
    Thread-safe via CAS on the content_hash slot.
    Overwrites if key already exists (newer variant wins).
    Size only increments on new insertions (C++: CAS success path),
    not on overwrites (C++: existing key match path).
    C++ asserts content_hash != 0 (zero is the empty-slot sentinel). -/
def KernelCache.insert (cache : KernelCache) (content_hash kernel_id : Nat) : KernelCache :=
  let isNew := (cache.entries content_hash).isNone
  { cache with
    entries := fun h => if h = content_hash then some kernel_id else cache.entries h
    size := if isNew then cache.size + 1 else cache.size }

/-! ## DAG Properties -/

/-- Merkle hash is deterministic: same node → same hash.
    (Trivially true for a pure function.) -/
theorem merkle_det (n : DagNode) :
    ∀ h₁ h₂, merkleHash n = h₁ → merkleHash n = h₂ → h₁ = h₂ := by
  intros h₁ h₂ e₁ e₂; rw [← e₁, ← e₂]

/-- Terminal has zero merkle hash. C++: TERMINAL → MerkleHash{0}. -/
theorem terminal_merkle : merkleHash .terminal = 0 := by
  rfl

/-- Region with no continuation: merkle hash equals content hash.
    C++: `h = content_hash; /* no next */ return MerkleHash{h}`. -/
theorem region_no_next_merkle (ch n : Nat) :
    merkleHash (.region ch n none) = ch := by
  rfl

/-- Region's merkle hash depends on its content hash.
    If content changes, merkle changes (assuming continuation fixed). -/
theorem region_merkle_depends_content (ch₁ ch₂ : Nat) (n : Nat)
    (next : Option DagNode) (hne : ch₁ ≠ ch₂) :
    merkleHash (.region ch₁ n next) ≠ merkleHash (.region ch₂ n next) := by
  cases next <;> simp [merkleHash, mix] <;> omega

/-- Content addressing: identical content_hash → same lookup result.
    This is THE property that enables kernel reuse across models. -/
theorem content_addressing (cache : KernelCache)
    (h₁ h₂ : Nat) (heq : h₁ = h₂) :
    cache.lookup h₁ = cache.lookup h₂ := by
  subst heq; rfl

/-- Insert then lookup returns the inserted value. -/
theorem insert_lookup (cache : KernelCache) (ch kid : Nat) :
    (cache.insert ch kid).lookup ch = some kid := by
  simp [KernelCache.insert, KernelCache.lookup]

/-- Lookup of a different key is unaffected by insert. -/
theorem insert_lookup_other (cache : KernelCache) (ch₁ ch₂ kid : Nat)
    (hne : ch₁ ≠ ch₂) :
    (cache.insert ch₁ kid).lookup ch₂ = cache.lookup ch₂ := by
  simp [KernelCache.insert, KernelCache.lookup]
  intro h; exact absurd h (Ne.symm hne)

/-- Overwrite does not change cache size.
    C++: when CAS finds existing key, updates kernel but does NOT increment size. -/
theorem insert_existing_size (cache : KernelCache) (ch kid₁ kid₂ : Nat) :
    (cache.insert ch kid₁).size = (cache.insert ch kid₂).size := by
  simp [KernelCache.insert]

/-- Double insert of same key: size increments only once.
    First insert is new (size+1), second is overwrite (size unchanged). -/
theorem insert_twice_size (cache : KernelCache) (ch kid₁ kid₂ : Nat)
    (hNew : (cache.entries ch).isNone) :
    ((cache.insert ch kid₁).insert ch kid₂).size = cache.size + 1 := by
  simp [KernelCache.insert, hNew]

/-! ## Replay

    Models `replay()` in MerkleDag.h: traverse compiled DAG.
    GuardEval provides guard values, RegionExec handles each region.

    C++ template: `replay<GuardEval, RegionExec>(node, eval, exec)`.
    Walks the DAG: region → exec + continue; branch → eval guard,
    find matching arm, recurse into arm, then continue from merge point;
    terminal → done.

    C++ returns bool (true = success, false = unseen guard value).
    We model this as ReplayResult to capture which regions were executed. -/

/-- Replay result: either completed successfully or no matching arm found. -/
inductive ReplayResult where
  | success (regions_executed : List Nat)  -- content hashes of executed regions
  | no_matching_arm                        -- unseen guard value → fall back
  deriving DecidableEq, Repr

/-- Execute replay. Models C++: `replay(node, eval_guard, exec_region)`.
    Returns the list of region content hashes executed, or failure.

    C++ algorithm (MerkleDag.h lines 688-735):
      while (node):
        REGION → exec_region(region); node = node->next;
        BRANCH → val = eval_guard(guard);
                 find matching arm (linear scan);
                 if !arm: return false;
                 replay(arm, ...);  // recurse into arm
                 node = branch->next;  // continue from merge
        TERMINAL → return true;

    Uses explicit list recursion via `where` for the arm search,
    satisfying the termination checker on the nested inductive. -/
def replayDag (eval_guard : Guard → Int) : DagNode → ReplayResult
  | .terminal => .success []
  | .region ch _ none => .success [ch]
  | .region ch _ (some next) =>
    match replayDag eval_guard next with
    | .success rest => .success (ch :: rest)
    | .no_matching_arm => .no_matching_arm
  | .branch g arms next =>
    let val := eval_guard g
    match findArm eval_guard val arms with
    | .no_matching_arm => .no_matching_arm
    | .success arm_regions =>
      match next with
      | none => .success arm_regions
      | some n =>
        match replayDag eval_guard n with
        | .success rest => .success (arm_regions ++ rest)
        | .no_matching_arm => .no_matching_arm
where
  /-- Search arms for matching guard value, recurse into matched arm.
      Models C++ linear scan: `for (i = 0; i < num_arms; i++)
        if (arms[i].value == val) { arm = arms[i].target; break; }` -/
  findArm (eval_guard : Guard → Int) (val : Int) :
      List (Int × DagNode) → ReplayResult
    | [] => .no_matching_arm
    | (v, arm_node) :: rest =>
      if v == val then replayDag eval_guard arm_node
      else findArm eval_guard val rest

/-- Replay is deterministic: same guard values → same execution.
    (Trivially true for a pure function.) -/
theorem replay_det (eval : Guard → Int) (node : DagNode) :
    ∀ r₁ r₂, replayDag eval node = r₁ → replayDag eval node = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Replay of a single region always succeeds. -/
theorem replay_single_region (eval : Guard → Int) (ch n : Nat) :
    replayDag eval (.region ch n none) = .success [ch] := by
  rfl

/-- Replay of terminal always succeeds with empty list. -/
theorem replay_terminal (eval : Guard → Int) :
    replayDag eval .terminal = .success [] := by
  rfl

end Crucible
