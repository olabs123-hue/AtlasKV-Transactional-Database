# AtlasKV — C++17 Transactional Key-Value Store and Query Engine

A self-contained educational storage-engine project demonstrating concepts relevant to database engineering: **multi-version reads (MVCC), optimistic transaction validation, secondary indexing, query-plan selection, snapshots, concurrency, and reproducible benchmarks**.

> This is **not** FoundationDB, a distributed database, a production SQL engine, or a crash-durable transaction system. See [Limitations](#limitations).

## Quick start

Requirements: CMake >= 3.16, C++17 compiler (GCC, Clang or MSVC), and a terminal.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Linux/macOS, launch `./build/atlaskv_cli` or `./build/atlaskv_bench`. On Windows, use the matching executable under `build/Debug/` or `build/Release/` depending on generator.

## Interactive demo

```text
$ ./build/atlaskv_cli
> PUT order-1 finance 100 shares
OK
> PUT order-2 tech backend service
OK
> GET order-1
finance | 100 shares
> QUERY finance
order-1 | finance | 100 shares
Plan: CATEGORY_INDEX, examined=1, matched=1
> ALL
...
Plan: FULL_SCAN, examined=2, matched=2
> SAVE demo.atkv
SAVED
> EXIT
```

Restore with `./build/atlaskv_cli demo.atkv`. Commands: `PUT key category value`, `GET key`, `DEL key`, `QUERY category`, `ALL`, `SAVE filename`, `EXIT`. Values may contain spaces. CLI limits query results to 1,000.

## API example

```cpp
#include "atlaskv/database.hpp"
using namespace atlaskv;
Database db;
auto tx = db.begin();
tx.put("user:1", {"users", "Alice"});
if (!tx.commit()) { /* retry on optimistic conflict */ }
QueryStats stats;
auto matches = db.query(Query{std::string("users"), std::nullopt, 100}, &stats);
db.save_snapshot("database.atkv");
```

## Architecture

```text
Application / CLI
       |
Transaction API ----------- Read snapshot at begin version
       |                         |
Optimistic commit ---------- Version history (MVCC)
       |
In-memory ordered key map -- Category -> ordered key set
       |                         |
       +---------- Query executor + explain() (index or scan)
       |
Explicit binary snapshot save/load
```

- **MVCC reads:** A transaction reads the latest key revision at or before its begin version, even if another transaction subsequently commits a new value. Versions are retained in memory.
- **Conflict detection:** At commit, the transaction checks read keys and written keys for newer revisions. On conflict, commit returns `false` without applying writes. Multi-key commits are applied together under a mutex.
- **Indexes:** An ordered primary key map and a category-to-key secondary index. Index membership is updated on commit.
- **Query planner:** `explain()` chooses `CategoryIndex` when a category predicate is present, otherwise `FullScan`. `QueryStats` reports records examined and matched. This is rule-based, not cost-based.
- **Snapshots:** `save_snapshot` writes the current records to a versioned binary format through a temporary file and rename. `load_snapshot` validates and replaces database state; it rejects corrupt/truncated input. The snapshot stores **current records only**, not historical MVCC revisions.
- **Concurrency:** Mutex-protected state and tests with four writer threads.

## Testing and benchmarks

```bash
ctest --test-dir build --output-on-failure
./build/atlaskv_bench
```

Tests cover basic CRUD, read-your-writes, historical reads, conflicts, delete and rollback, indexed filtering, scan filtering, snapshot round-trip, corrupted snapshots, load invalidation, and concurrent writers. CI runs the build and test on Ubuntu.

**Local reference measurement (21 Oct 2026, this execution environment):** 100,000 single-key in-memory commits in 0.228591 s, approximately **437,462 transactions/sec**. This is a single local run, not a guaranteed result or an end-to-end database benchmark. Run the benchmark on your own machine and report your own measured result.

## Limitations

1. **Not crash-durable per commit:** A successful transaction is only in memory until an explicit snapshot. No write-ahead log, fsync or crash-recovery guarantee. The temporary-file rename is not equivalent to durable commit.
2. **Not serializable for range queries:** Transactional point reads and writes are validated, but `Database::query()` is not a transaction-scoped snapshot query and does not protect against range phantoms.
3. **Unbounded MVCC history:** Old revisions are never garbage-collected during a running process; snapshot reload compacts to current state.
4. **No distributed replication, sharding, SQL parser, cost-based planner, B+ tree, or storage pages.**
5. **No cross-process concurrency or file locking.** Snapshot load is designed for controlled use; in-flight transactions are invalidated, but callers must coordinate concurrent API operations and save paths.
6. **Simplified persistence format:** No checksum, encryption, or authentication; do not load untrusted snapshots.

## Suggested roadmap

- **V3:** WAL with checksums, flush ordering, recovery and fault-injection tests.
- **V4:** Transaction-scoped range reads with phantom detection and safe MVCC garbage collection.
- **V5:** Index statistics and cost-based access-path selection, explain plans, and benchmark comparisons.
- **V6:** Optional disk pages / B+ tree and concurrency profiling.

## Repository layout

```text
include/atlaskv/database.hpp  Public API
src/database.cpp             Storage, transactions, queries, snapshots
src/main.cpp                 Interactive CLI
benchmarks/bench.cpp         Reproducible local benchmark
 tests/test_database.cpp     Automated regression tests
.github/workflows/ci.yml    CI pipeline
CMakeLists.txt               Cross-platform build
ARCHITECTURE.md              Design decisions
LICENSE                      MIT license
```

## CV bullet (after you have personally built/tested and can explain it)

**AtlasKV — Transactional Key-Value Store & Query Engine | C++17, CMake, GitHub Actions:** Implemented historical MVCC reads, optimistic multi-key transactions with conflict detection, secondary category indexes, query-plan selection, binary snapshot recovery, and automated concurrency tests; benchmarked 100,000 in-memory commits locally (machine-specific throughput).
