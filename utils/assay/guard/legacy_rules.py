"""The temporary table of the rules: the module that holds each rule, and the words that select its mode.

`guard check --rule ID [ARG]...` imports the module of ID and calls its
function main with the words of the row and then each ARG, unchanged.  Most
rows name a guard script that moved into assay/guard/legacy/ with its behavior
unchanged.  The other rows name a module that holds a rule job of the tree
today: the fill of the tree cache, the fill of the preprocessed store, the
session oracle and the gate mutants.  The registry of the rules replaces this
table.

The ID of a guard script is the check name that the script reports, or the
name of the script without its prefix "check-" or "audit-".
"""

# Each rule: the module, and the words that main gets before the words of the command line.
RULES: dict[str, tuple[str, tuple[str, ...]]] = {
    "admitted-audit": ("assay.guard.legacy.check_admitted_audit", ()),
    "banned-calls": ("assay.guard.legacy.check_banned_calls", ()),
    "brand-drain": ("assay.guard.legacy.check_brand_drain", ()),
    "budget-history": ("assay.guard.legacy.check_budget_history", ()),
    "build-order": ("assay.guard.legacy.check_build_order", ()),
    "cache-size": ("assay.guard.legacy.check_cache_size", ()),
    "ci-guard-labels": ("assay.guard.legacy.check_ci_guard_labels", ()),
    "compile-cost": ("assay.guard.legacy.check_compile_cost", ()),
    "contract-form": ("assay.guard.legacy.check_contract_form", ()),
    "contract-header-unit": ("assay.guard.legacy.contract_header_unit_test", ()),
    "contract-semantic": ("assay.guard.legacy.check_contract_semantic", ()),
    "core-shapes": ("assay.guard.legacy.check_core_shapes", ()),
    "ct-disassembly": ("assay.guard.legacy.check_ct_disassembly", ()),
    "ctx-init-door": ("assay.guard.legacy.check_ctx_init_door", ()),
    "ctx-testing-boundary": ("assay.guard.legacy.check_ctx_testing_boundary", ()),
    "decide-cite-ratio": ("assay.guard.legacy.audit_decide_cite_ratio", ()),
    "derived-pins": ("assay.guard.legacy.check_derived_pins", ()),
    "detail-namespace": ("assay.guard.legacy.check_detail_namespace", ()),
    "detsafe-ledger": ("assay.guard.legacy.check_detsafe_ledger", ()),
    "escape-doors": ("assay.guard.legacy.check_escape_doors", ()),
    "federation-admission": ("assay.guard.legacy.check_federation_admission", ()),
    "fixy-dim-prose": ("assay.guard.legacy.check_fixy_dim_prose", ()),
    "fixy-hw-discipline": ("assay.guard.legacy.check_fixy_hw_discipline", ()),
    "fullness-guard": ("assay.guard.legacy.check_fullness_guard", ()),
    "gate-mutants": ("assay.mutation.run", ()),
    "git-isolation": ("assay.guard.legacy.check_git_isolation", ()),
    "guard-engines": ("assay.guard.legacy.check_guard_engines", ()),
    "header-checks": ("assay.guard.legacy.check_header_checks", ()),
    "header-constexpr-ops": ("assay.guard.legacy.check_header_constexpr_ops", ()),
    "heavy-include": ("assay.guard.legacy.check_heavy_includes", ()),
    "host-owners": ("assay.guard.legacy.check_host_owners", ()),
    "layer-boundary": ("assay.guard.legacy.check_layer_boundary", ()),
    "libstdcxx-symbols": ("assay.guard.legacy.check_libstdcxx_symbols", ()),
    "lifetime-twin": ("assay.guard.legacy.check_lifetime_twin", ()),
    "lock-free-asserts": ("assay.guard.legacy.check_lock_free_asserts", ()),
    "mint-pattern": ("assay.guard.legacy.check_mint_pattern", ()),
    "no-combine-ids-duplicate": ("assay.guard.legacy.check_no_combine_ids_duplicate", ()),
    "no-coordination-refs": ("assay.guard.legacy.check_no_coordination_refs", ()),
    "no-ffast-math": ("assay.guard.legacy.check_no_ffast_math", ()),
    "no-raw-array-member": ("assay.guard.legacy.check_no_raw_array_member", ()),
    "no-unchecked-access": ("assay.guard.legacy.check_no_unchecked_access", ()),
    "no-unity-pch": ("assay.guard.legacy.check_no_unity_pch", ()),
    "padded-list": ("assay.guard.legacy.check_padded_lists", ()),
    "parse-clean": ("assay.guard.legacy.check_parse_clean", ()),
    "parse-cost": ("assay.guard.legacy.check_parse_cost", ()),
    "permission-storage": ("assay.guard.legacy.check_permission_storage", ()),
    "pre-callsite-count": ("assay.guard.legacy.audit_pre_callsite_count", ()),
    "preprocessed-fill": ("assay.guard.preprocessed", ("--fill",)),
    "process-wide-state": ("assay.guard.legacy.check_process_wide_state", ()),
    "proof-routes": ("assay.guard.legacy.check_proof_routes", ()),
    "quarantine-plugin": ("assay.guard.legacy.check_plugin", ()),
    "quarantine-ratchet": ("assay.guard.legacy.check_quarantine_ratchet", ()),
    "quarantine-regions": ("assay.guard.legacy.check_quarantine_regions", ()),
    "reconfigure-noop": ("assay.guard.legacy.check_reconfigure_noop", ()),
    "refined-pre-subsumption": ("assay.guard.legacy.check_refined_pre_subsumption", ()),
    "row-contains-discipline": ("assay.guard.legacy.check_row_contains_discipline", ()),
    "session-oracle": ("assay.oracle.runner", ()),
    "splits-orphan": ("assay.guard.legacy.check_splits_orphan", ()),
    "start-lifetime": ("assay.guard.legacy.check_start_lifetime", ()),
    "stub-discipline": ("assay.guard.legacy.check_stub_discipline", ()),
    "syscall-capability": ("assay.guard.legacy.check_syscall_capability", ()),
    "syscall-grant-coverage": ("assay.guard.legacy.check_syscall_grant_coverage", ()),
    "test-time": ("assay.guard.legacy.check_test_time", ()),
    "trait-guard": ("assay.guard.legacy.check_trait_injection", ()),
    "tree-fill": ("assay.readers.tsast", ("--fill",)),
    "use-after-move": ("assay.guard.legacy.check_use_after_move", ()),
    "use-after-move-attacks": ("assay.guard.legacy.test_use_after_move_attacks", ()),
    "vessel-acceptance": ("assay.guard.legacy.check_vessel_acceptance", ()),
    "walk-units": ("assay.guard.legacy.check_walk_units", ()),
    "witness-roster": ("assay.guard.legacy.check_witness_roster", ()),
}
