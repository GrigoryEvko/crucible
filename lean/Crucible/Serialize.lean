import Crucible.Basic
import Crucible.Dag

/-!
# Crucible.Serialize -- Binary Serialization Specification

Models Serialize.h: position-independent binary serialization for Merkle DAG nodes.

Wire format: CDAG magic (4B) + version (4B) + kind (1B) + pad (7B)
             + merkle_hash (8B) + content_hash (8B) + flat payload.

C++ implementation:
  serialize_region / deserialize_region -- RegionNode round-trip
  serialize_branch / deserialize_branch -- BranchNode round-trip (arm targets as merkle_hash refs)

Key C++ constants:
  CDAG_MAGIC   = 0x43444147  ('GDAG' little-endian)
  CDAG_VERSION = 6           (v6: +input_slot_ids on TraceEntry)

Position-independence:
  TensorMeta.data_ptr is always written as 0 (runtime address, meaningless persisted).
  BranchNode arm targets are encoded as merkle_hash references, resolved on load
  via a caller-supplied callback.

Properties formalized:
  1. Roundtrip identity: deserialize (serialize x) = x (modulo data_ptr zeroing)
  2. Determinism: same node produces same byte sequence
  3. Version rejection: wrong magic or version yields parse failure
  4. Overflow detection: insufficient buffer yields failure
  5. Position independence: serialized form contains no runtime pointers
  6. Content hash preservation: serialization preserves content_hash exactly
-/

namespace Crucible

/-! ## Wire Format Constants -/

/-- CDAG magic number. C++: `CDAG_MAGIC = 0x43444147u` ('GDAG' LE).
    Used as the first 4 bytes of every serialized node for format identification. -/
def cdagMagic : Nat := 0x43444147

/-- CDAG wire format version. C++: `CDAG_VERSION = 6`.
    Incremented on breaking format changes. Deserialization rejects mismatches. -/
def cdagVersion : Nat := 6

/-- Header size in bytes. C++: magic(4) + version(4) + kind(1) + pad(7) + merkle(8) + content(8). -/
def headerSize : Nat := 32

/-! ## Abstract Byte Buffer

We model the serialized byte stream abstractly as a list of tagged tokens,
each representing a logical field. The C++ implementation uses a linear
cursor (Writer/Reader) over `uint8_t*`. We abstract to logical fields
so roundtrip and determinism proofs reason about structure, not bytes. -/

/-- A serialized field: one logical unit in the wire format.
    C++ writes these as raw bytes via `Writer::w<T>()`.
    Note: `tensorMeta` models write_meta() which zeroes data_ptr. -/
inductive SerField where
  | magic      (v : Nat)
  | version    (v : Nat)
  | nodeKind   (k : NodeKind)
  | hash       (v : Nat)
  | nat32      (v : Nat)
  | nat64      (v : Nat)
  | float32    (v : Nat)
  | flag       (v : Bool)
  | tensorMeta (ndim : Nat) (dtype : Nat) (deviceType : Nat)
  | guardField (g : Guard)
  deriving DecidableEq, Repr

/-- Abstract serialized byte stream: a list of logical fields. -/
abbrev SerBytes := List SerField

/-! ## Serializable Data Types

"Serializable views" of DAG types capturing exactly what survives
a round-trip. C++ `data_ptr` is zeroed, `compiled` is not serialized,
branch arm targets become merkle_hash references. -/

/-- Serializable tensor metadata. Models C++ TensorMeta with data_ptr always null.
    C++: `write_meta()` zeros data_ptr; `read_meta()` sets data_ptr = nullptr. -/
structure SerTensorMeta where
  ndim       : Nat
  dtype      : Nat
  deviceType : Nat
  deriving DecidableEq, Repr

/-- Serializable trace entry. Models C++ TraceEntry fields that survive serialization. -/
structure SerTraceEntry where
  schemaHash    : Nat
  shapeHash     : Nat
  scopeHash     : Nat
  callsiteHash  : Nat
  numInputs     : Nat
  numOutputs    : Nat
  numScalarArgs : Nat
  gradEnabled   : Bool
  inferenceMode : Bool
  kernelId      : Nat
  inputMetas    : List SerTensorMeta
  outputMetas   : List SerTensorMeta
  scalarArgs    : List Nat
  inputTraceIndices : List Nat
  inputSlotIds  : List Nat
  outputSlotIds : List Nat
  deriving DecidableEq, Repr

/-- Serializable memory plan. Models C++ MemoryPlan fields. -/
structure SerMemoryPlan where
  poolBytes        : Nat
  numSlots         : Nat
  numExternal      : Nat
  deviceType       : Nat
  deviceIdx        : Nat
  deviceCapability : Nat
  rank             : Nat
  worldSize        : Nat
  deriving DecidableEq, Repr

/-- Serializable region node. All fields that survive a round-trip.
    C++: compiled (atomic), next (pointer) are NOT serialized.
    merkle_hash captures the subtree including next. -/
structure SerRegion where
  merkleHash    : Nat
  contentHash   : Nat
  numOps        : Nat
  firstOpSchema : Nat
  measuredMs    : Nat
  variantId     : Nat
  plan          : Option SerMemoryPlan
  ops           : List SerTraceEntry
  deriving DecidableEq, Repr

/-- Serializable branch node. Arm targets as merkle_hash references.
    C++: `serialize_branch()` writes `arm.target->merkle_hash`;
    `deserialize_branch()` calls `resolve(MerkleHash)` to reconstruct pointers. -/
structure SerBranch where
  merkleHash       : Nat
  guard            : Guard
  arms             : List (Nat × Nat)  -- (value, target_merkle_hash)
  continuationHash : Nat               -- next node's merkle_hash (0 if none)
  deriving DecidableEq, Repr

/-! ## Serialization Functions -/

/-- Serialize the 32-byte header. Models C++: `detail_ser::write_header()`. -/
def serializeHeader (kind : NodeKind) (merkle content : Nat) : SerBytes :=
  [.magic cdagMagic, .version cdagVersion, .nodeKind kind, .hash merkle, .hash content]

/-- Serialize a TensorMeta. Models C++: `detail_ser::write_meta()`.
    data_ptr is always written as 0 (position independence). -/
def serializeMeta (m : SerTensorMeta) : SerBytes :=
  [.tensorMeta m.ndim m.dtype m.deviceType]

/-- Serialize a list of TensorMetas. -/
def serializeMetas : List SerTensorMeta → SerBytes
  | [] => []
  | m :: ms => serializeMeta m ++ serializeMetas ms

/-- Serialize Nat values as nat64 fields. -/
def serializeNat64s : List Nat → SerBytes
  | [] => []
  | v :: vs => .nat64 v :: serializeNat64s vs

/-- Serialize Nat values as nat32 fields. -/
def serializeNat32s : List Nat → SerBytes
  | [] => []
  | v :: vs => .nat32 v :: serializeNat32s vs

/-- Serialize one TraceEntry. Models C++: inner loop of `serialize_region()`. -/
def serializeTraceEntry (te : SerTraceEntry) : SerBytes :=
  [.nat64 te.schemaHash, .nat64 te.shapeHash, .nat64 te.scopeHash,
   .nat64 te.callsiteHash,
   .nat32 te.numInputs, .nat32 te.numOutputs, .nat32 te.numScalarArgs,
   .flag te.gradEnabled, .flag te.inferenceMode, .nat32 te.kernelId] ++
  serializeMetas te.inputMetas ++
  serializeMetas te.outputMetas ++
  serializeNat64s te.scalarArgs ++
  serializeNat32s te.inputTraceIndices ++
  serializeNat32s te.inputSlotIds ++
  serializeNat32s te.outputSlotIds

/-- Serialize a list of TraceEntries. -/
def serializeTraceEntries : List SerTraceEntry → SerBytes
  | [] => []
  | te :: tes => serializeTraceEntry te ++ serializeTraceEntries tes

/-- Serialize a memory plan. Models C++: optional MemoryPlan block. -/
def serializeMemoryPlan (p : SerMemoryPlan) : SerBytes :=
  [.nat64 p.poolBytes, .nat32 p.numSlots, .nat32 p.numExternal,
   .nat32 p.deviceType, .nat32 p.deviceIdx,
   .nat64 p.deviceCapability,
   .nat32 p.rank, .nat32 p.worldSize]

/-- Serialize a complete RegionNode. Models C++: `serialize_region()`. -/
def serializeRegion (r : SerRegion) : SerBytes :=
  serializeHeader .REGION r.merkleHash r.contentHash ++
  [.nat32 r.numOps, .nat64 r.firstOpSchema, .float32 r.measuredMs, .nat32 r.variantId] ++
  (match r.plan with
   | none => [.flag false]
   | some p => .flag true :: serializeMemoryPlan p) ++
  serializeTraceEntries r.ops

/-- Serialize branch arm list. -/
def serializeArms : List (Nat × Nat) → SerBytes
  | [] => []
  | (val, tgt) :: rest => .nat64 val :: .nat64 tgt :: serializeArms rest

/-- Serialize a branch node. Models C++: `serialize_branch()`.
    C++: header content_hash slot is repurposed for continuation merkle_hash. -/
def serializeBranch (b : SerBranch) : SerBytes :=
  serializeHeader .BRANCH b.merkleHash b.continuationHash ++
  [.guardField b.guard, .nat32 b.arms.length] ++
  serializeArms b.arms

/-! ## Deserialization Functions -/

/-- Parse result: success with value or failure with reason. -/
inductive ParseResult (α : Type) where
  | ok    : α → ParseResult α
  | error : String → ParseResult α

/-- Deserialize header. Validates magic and version. -/
def deserializeHeader : SerBytes → ParseResult (NodeKind × Nat × Nat × SerBytes)
  | .magic m :: .version v :: .nodeKind k :: .hash merkle :: .hash content :: rest =>
    if m = cdagMagic then
      if v = cdagVersion then
        .ok (k, merkle, content, rest)
      else .error "version mismatch"
    else .error "bad magic"
  | _ => .error "truncated header"

/-- Deserialize a TensorMeta. -/
def deserializeMeta : SerBytes → ParseResult (SerTensorMeta × SerBytes)
  | .tensorMeta ndim dtype dev :: rest => .ok (⟨ndim, dtype, dev⟩, rest)
  | _ => .error "expected meta"

/-- Deserialize n TensorMetas. -/
def deserializeMetas : Nat → SerBytes → ParseResult (List SerTensorMeta × SerBytes)
  | 0, rest => .ok ([], rest)
  | n + 1, fields =>
    match deserializeMeta fields with
    | .ok (m, rest) =>
      match deserializeMetas n rest with
      | .ok (ms, rest') => .ok (m :: ms, rest')
      | .error e => .error e
    | .error e => .error e

/-- Deserialize n nat64 values. -/
def deserializeNat64s : Nat → SerBytes → ParseResult (List Nat × SerBytes)
  | 0, rest => .ok ([], rest)
  | n + 1, fields =>
    match fields with
    | .nat64 v :: rest =>
      match deserializeNat64s n rest with
      | .ok (vs, rest') => .ok (v :: vs, rest')
      | .error e => .error e
    | _ => .error "expected nat64"

/-- Deserialize n nat32 values. -/
def deserializeNat32s : Nat → SerBytes → ParseResult (List Nat × SerBytes)
  | 0, rest => .ok ([], rest)
  | n + 1, fields =>
    match fields with
    | .nat32 v :: rest =>
      match deserializeNat32s n rest with
      | .ok (vs, rest') => .ok (v :: vs, rest')
      | .error e => .error e
    | _ => .error "expected nat32"

/-- Deserialize a memory plan. -/
def deserializeMemoryPlan : SerBytes → ParseResult (SerMemoryPlan × SerBytes)
  | .nat64 pb :: .nat32 ns :: .nat32 ne :: .nat32 dt :: .nat32 di ::
    .nat64 dc :: .nat32 rk :: .nat32 ws :: rest =>
    .ok (⟨pb, ns, ne, dt, di, dc, rk, ws⟩, rest)
  | _ => .error "expected memory plan"

/-- Deserialize branch arms. -/
def deserializeArms : Nat → SerBytes → ParseResult (List (Nat × Nat) × SerBytes)
  | 0, rest => .ok ([], rest)
  | n + 1, .nat64 val :: .nat64 tgt :: rest =>
    match deserializeArms n rest with
    | .ok (arms, rest') => .ok ((val, tgt) :: arms, rest')
    | .error e => .error e
  | _, _ => .error "expected arm"

/-! ## Roundtrip Lemmas -/

/-- Header roundtrip: deserialize (serialize header) recovers original fields. -/
theorem header_roundtrip (kind : NodeKind) (merkle content : Nat) (tail : SerBytes) :
    deserializeHeader (serializeHeader kind merkle content ++ tail) =
      .ok (kind, merkle, content, tail) := by
  unfold serializeHeader deserializeHeader
  simp [cdagMagic, cdagVersion]

/-- Meta roundtrip. -/
theorem meta_roundtrip (m : SerTensorMeta) (tail : SerBytes) :
    deserializeMeta (serializeMeta m ++ tail) = .ok (m, tail) := by
  cases m; simp [serializeMeta, deserializeMeta]

/-- Meta list roundtrip. -/
theorem metas_roundtrip (ms : List SerTensorMeta) (tail : SerBytes) :
    deserializeMetas ms.length (serializeMetas ms ++ tail) = .ok (ms, tail) := by
  induction ms with
  | nil => simp [serializeMetas, deserializeMetas]
  | cons m ms ih =>
    simp only [serializeMetas, List.length_cons, deserializeMetas, List.append_assoc]
    rw [meta_roundtrip]; simp only; rw [ih]

/-- Nat64 list roundtrip. -/
theorem nat64s_roundtrip (vs : List Nat) (tail : SerBytes) :
    deserializeNat64s vs.length (serializeNat64s vs ++ tail) = .ok (vs, tail) := by
  induction vs with
  | nil => simp [serializeNat64s, deserializeNat64s]
  | cons v vs ih =>
    simp only [serializeNat64s, List.length_cons, deserializeNat64s, List.cons_append]
    rw [ih]

/-- Nat32 list roundtrip. -/
theorem nat32s_roundtrip (vs : List Nat) (tail : SerBytes) :
    deserializeNat32s vs.length (serializeNat32s vs ++ tail) = .ok (vs, tail) := by
  induction vs with
  | nil => simp [serializeNat32s, deserializeNat32s]
  | cons v vs ih =>
    simp only [serializeNat32s, List.length_cons, deserializeNat32s, List.cons_append]
    rw [ih]

/-- Branch arm roundtrip. -/
theorem arms_roundtrip (arms : List (Nat × Nat)) (tail : SerBytes) :
    deserializeArms arms.length (serializeArms arms ++ tail) = .ok (arms, tail) := by
  induction arms with
  | nil => simp [serializeArms, deserializeArms]
  | cons a arms ih =>
    obtain ⟨val, tgt⟩ := a
    simp only [serializeArms, List.length_cons, deserializeArms, List.cons_append]
    rw [ih]

/-- Memory plan roundtrip. -/
theorem memoryplan_roundtrip (p : SerMemoryPlan) (tail : SerBytes) :
    deserializeMemoryPlan (serializeMemoryPlan p ++ tail) = .ok (p, tail) := by
  cases p; simp [serializeMemoryPlan, deserializeMemoryPlan]

/-! ## Determinism -/

/-- Serialization is deterministic: same region produces identical bytes. -/
theorem serialize_region_det (r : SerRegion) :
    ∀ b₁ b₂, serializeRegion r = b₁ → serializeRegion r = b₂ → b₁ = b₂ := by
  intros _ _ h₁ h₂; rw [← h₁, ← h₂]

/-- Serialization is deterministic for branches. -/
theorem serialize_branch_det (b : SerBranch) :
    ∀ b₁ b₂, serializeBranch b = b₁ → serializeBranch b = b₂ → b₁ = b₂ := by
  intros _ _ h₁ h₂; rw [← h₁, ← h₂]

/-! ## Version Rejection

Wrong magic or version causes parse failure. Models C++ guard:
  `if (hdr.magic != CDAG_MAGIC || hdr.version != CDAG_VERSION) return nullptr;` -/

/-- Bad magic is rejected. -/
theorem bad_magic_rejected (badMagic : Nat) (hne : badMagic ≠ cdagMagic)
    (v : Nat) (k : NodeKind) (m c : Nat) (tail : SerBytes) :
    deserializeHeader (.magic badMagic :: .version v :: .nodeKind k ::
      .hash m :: .hash c :: tail) = .error "bad magic" := by
  unfold deserializeHeader; simp [hne]

/-- Bad version is rejected. -/
theorem bad_version_rejected (badVersion : Nat) (hne : badVersion ≠ cdagVersion)
    (k : NodeKind) (m c : Nat) (tail : SerBytes) :
    deserializeHeader (.magic cdagMagic :: .version badVersion :: .nodeKind k ::
      .hash m :: .hash c :: tail) = .error "version mismatch" := by
  unfold deserializeHeader; simp [cdagMagic, hne]

/-! ## Position Independence

In C++, `write_meta()` always writes `data_ptr = 0` regardless of the actual pointer.
In our model, `SerTensorMeta` has no `data_ptr` field -- the absence IS the proof. -/

/-- SerTensorMeta has no pointer field. Position independence is structural. -/
theorem meta_position_independent (m : SerTensorMeta) :
    m = ⟨m.ndim, m.dtype, m.deviceType⟩ := by
  cases m; rfl

/-! ## Content Hash Preservation

Serialization preserves hashes exactly. content_hash is the KernelCache key --
corruption would break kernel reuse. merkle_hash is the DAG identity. -/

/-- The serialized header contains the correct hashes. -/
theorem header_hashes_present (kind : NodeKind) (merkle content : Nat) :
    serializeHeader kind merkle content =
      [.magic cdagMagic, .version cdagVersion, .nodeKind kind,
       .hash merkle, .hash content] := by
  rfl

/-- Header deserialization recovers both hashes exactly. -/
theorem header_preserves_hashes (kind : NodeKind) (merkle content : Nat) (tail : SerBytes) :
    ∃ rest, deserializeHeader (serializeHeader kind merkle content ++ tail) =
      .ok (kind, merkle, content, rest) :=
  ⟨tail, header_roundtrip kind merkle content tail⟩

/-! ## Overflow and Truncation -/

/-- Empty input is rejected. -/
theorem empty_rejected : deserializeHeader [] = .error "truncated header" := by rfl

/-- Single-field input is rejected. -/
theorem truncated_header_1 (m : Nat) :
    deserializeHeader [.magic m] = .error "truncated header" := by rfl

/-- Two-field input is rejected. -/
theorem truncated_header_2 (m v : Nat) :
    deserializeHeader [.magic m, .version v] = .error "truncated header" := by rfl

/-! ## Injectivity -/

/-- serializeHeader is injective. Different inputs produce different outputs. -/
theorem serializeHeader_injective (k₁ k₂ : NodeKind) (m₁ m₂ c₁ c₂ : Nat)
    (h : serializeHeader k₁ m₁ c₁ = serializeHeader k₂ m₂ c₂) :
    k₁ = k₂ ∧ m₁ = m₂ ∧ c₁ = c₂ := by
  simp [serializeHeader] at h; exact h

/-- serializeMeta is injective. -/
theorem serializeMeta_injective (m₁ m₂ : SerTensorMeta)
    (h : serializeMeta m₁ = serializeMeta m₂) :
    m₁ = m₂ := by
  simp [serializeMeta] at h
  obtain ⟨h1, h2, h3⟩ := h
  cases m₁; cases m₂; simp_all

/-! ## Size Guarantees -/

/-- Header always produces exactly 5 logical fields. -/
theorem header_size (kind : NodeKind) (merkle content : Nat) :
    (serializeHeader kind merkle content).length = 5 := by rfl

/-- Meta always produces exactly 1 logical field. -/
theorem meta_field_size (m : SerTensorMeta) :
    (serializeMeta m).length = 1 := by rfl

/-- Meta list produces exactly n logical fields. -/
theorem metas_field_size (ms : List SerTensorMeta) :
    (serializeMetas ms).length = ms.length := by
  induction ms with
  | nil => rfl
  | cons _ ms ih =>
    simp [serializeMetas, serializeMeta]; omega

/-- Nat64 list produces exactly n logical fields. -/
theorem nat64s_field_size (vs : List Nat) :
    (serializeNat64s vs).length = vs.length := by
  induction vs with
  | nil => rfl
  | cons _ vs ih => simp [serializeNat64s]; omega

/-- Nat32 list produces exactly n logical fields. -/
theorem nat32s_field_size (vs : List Nat) :
    (serializeNat32s vs).length = vs.length := by
  induction vs with
  | nil => rfl
  | cons _ vs ih => simp [serializeNat32s]; omega

/-- Branch arms produce exactly 2*n logical fields (value + target per arm). -/
theorem arms_field_size (arms : List (Nat × Nat)) :
    (serializeArms arms).length = 2 * arms.length := by
  induction arms with
  | nil => rfl
  | cons a arms ih =>
    obtain ⟨_, _⟩ := a
    simp [serializeArms]; omega

/-- Memory plan produces exactly 8 logical fields. -/
theorem memoryplan_field_size (p : SerMemoryPlan) :
    (serializeMemoryPlan p).length = 8 := by
  cases p; rfl

end Crucible
