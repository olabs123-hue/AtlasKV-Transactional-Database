# Bloomberg portfolio benchmark — historical result

These measurements were recorded on 2026-10-22 from a **separate local portfolio implementation**, not independently reproduced against this GitHub repository's current code. Do not interpret the figure as a benchmark of the code in this repository until the corresponding benchmark executable has been integrated and rerun.

- Median: 657,392 operations/second across five 100,000-operation runs
- Workload: in-memory single-key transaction commits
- Machine: AMD EPYC 9V74 shared cloud CPU
- Compiler: GCC 14.2.0; C++17 Release build
- Original command: `./build/atlaskv_bench`
- Limitations: synthetic, single-process, in-memory microbenchmark; excludes networking, disk I/O, Kafka, and end-to-end latency

**Next:** integrate the exact matching benchmark source and its dependencies, run five times against the current repository code, and commit raw per-run results. The earlier portfolio archive contains the original benchmark implementation and raw outputs.
