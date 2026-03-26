# 11. Research Roadmap

Crucible's development is organized into seven phases. Phase 1 (foundation) is complete. Subsequent phases build on each other but are designed to produce independently evaluable artifacts.

**Phase 2: FX Core.** Complete the four-layer verification architecture. Deliver: Z3 proof suites for all core invariants (arena, hashing, SPSC, memory plan, kernel access), consteval bounded verification infrastructure, reflection-based structural checks, and a proof-carrying build where compilation failure implies an invariant was violated. Evaluation: proof coverage (fraction of documented invariants with machine-checked proofs), solver time scaling with problem size, false positive rate of structural checks.

**Phase 3: Longitude.** Implement hardware calibration and configuration optimization. Deliver: GPU profiling, network probing, kernel calibration protocol, and Z3-based topology solver. Evaluation: calibration accuracy (predicted vs measured kernel time across shapes and devices), solver time for configuration optimization at various cluster sizes, configuration quality compared to expert manual tuning.

**Phase 4: Augur.** Implement the digital twin and continuous monitoring. Deliver: per-iteration performance prediction, drift detection, bottleneck diagnosis, model intelligence diagnostics. Evaluation: prediction accuracy across model architectures and hardware configurations, detection latency for performance regressions, diagnostic accuracy (correct identification of bottleneck type).

**Phase 5: Compiled Tiers 2-3.** Implement shadow handle execution and CUDA Graph replay. Deliver: ConductorTensorImpl, asynchronous kernel execution decoupled from Python, CUDA Graph capture and replay. Evaluation: per-operation dispatch overhead (Tier 2 vs Tier 1 vs eager), per-iteration overhead (Tier 3 vs Tier 2), end-to-end training throughput on standard benchmarks.

**Phase 6: Distribution.** Implement the Canopy, Keepers, and full Cipher. Deliver: gossip-based peer discovery, Raft consensus, configurable redundancy, three-tier persistence, heterogeneous compute support. Evaluation: recovery time from single and multi-Relay failure, redundancy overhead at various α values, scaling efficiency on heterogeneous clusters.

**Phase 7: Model-Aware Optimization.** Implement token-level adaptation, layer-level analysis, architecture mutation, and training optimization. Deliver: integrated pipeline where Augur diagnoses, FX verifies, and the Keeper activates optimizations as DAG branches. Evaluation: training efficiency improvement (time-to-accuracy) on standard benchmarks, quality preservation under automated architecture mutation, comparison with manual optimization by expert practitioners.

Each phase will produce a focused evaluation comparing Crucible's approach against established baselines on standard benchmarks. We plan to make the prototype, formalization, and evaluation artifacts publicly available.
