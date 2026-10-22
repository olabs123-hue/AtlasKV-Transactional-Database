# Benchmark of this GitHub repository's current implementation

Run from repository root: `bash benchmarks/run_current_repository.sh`.

The script compiles the **current repository source** in Release/optimized mode and runs its own benchmark five times. Record CPU, compiler, OS, and raw output. Do not equate these numbers with `benchmarks/HISTORICAL_PORTFOLIO_RESULT.md`: that document describes a different, independently packaged implementation.

**Workload:** Current repository's in-memory single-key transaction commit benchmark; no durable disk commit.

This runner does not measure production end-to-end performance. Review the benchmark code and confirm tests pass before quoting results on a CV.
