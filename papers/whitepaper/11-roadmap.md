# 11. Research Roadmap

Phase 1 (foundation) is complete. Subsequent phases produce independently evaluable artifacts.

**Phase 2: FX Core.** Z3 proof suites for all core invariants, consteval infrastructure, reflection checks, proof-carrying build. Eval: proof coverage, solver time scaling, structural check false positive rate.

**Phase 3: Longitude.** GPU profiling, network probing, kernel calibration, Z3 topology solver. Eval: calibration accuracy (predicted vs measured per shape/device), solver time at various cluster sizes, configuration quality vs expert tuning.

**Phase 4: Augur.** Digital twin, drift detection, bottleneck diagnosis, model intelligence. Eval: prediction accuracy, regression detection latency, diagnostic accuracy.

**Phase 5: Compiled Tiers 2--3.** ConductorTensorImpl, async kernel execution, CUDA Graph replay. Eval: per-op dispatch overhead (Tier 2 vs 1 vs eager), per-iteration overhead (Tier 3 vs 2), end-to-end throughput on standard benchmarks.

**Phase 6: Distribution.** Canopy (gossip, Raft, CRDTs), Keepers, full Cipher (three-tier persistence, configurable alpha redundancy). Eval: recovery time, redundancy overhead, heterogeneous scaling efficiency.

**Phase 7: Model-Aware Optimization.** Token adaptation, layer analysis, architecture mutation, training optimization. Integrated pipeline: Augur diagnoses, FX verifies, Keeper activates. Eval: time-to-accuracy improvement, quality preservation under automated mutation, comparison with expert manual optimization.

Each phase produces a focused evaluation against established baselines. We plan to make prototype, formalization, and evaluation artifacts publicly available.
