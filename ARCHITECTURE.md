# Architecture and design notes

## Data structures
- `records_`: ordered map of current key -> `{category,value}`; enables ordered full scans.
- `history_`: per-key append-only vector of `{commit_version, optional<Record>}`; tombstones represent deletion.
- `category_index_`: ordered map of category -> ordered set of current keys.
- `version_`: monotonically increasing database commit version for write transactions.
- `epoch_`: increments on snapshot load to invalidate pre-load transactions.

## Snapshot isolation and conflict semantics
A transaction captures `version_` on `begin`. `get(key)` first checks its own write set; otherwise it uses binary search in `history_[key]` to return the last revision whose version is <= its begin version. At commit, any read/write key changed since begin causes a conflict. This is optimistic concurrency control with historical point reads, not full SQL snapshot isolation/serializability. A write-only transaction touching disjoint keys can commit even if other transactions commit concurrently.

## Atomicity
All validation and all in-memory modifications of a transaction occur under the same database mutex. There is no external durable commit; out-of-memory exceptions are not handled with a strong transactional rollback guarantee.

## Query execution
A category predicate selects the secondary index. Otherwise the executor scans ordered primary keys. The optional substring predicate is evaluated after candidate retrieval. Limit applies to matched records. `QueryStats.examined` counts candidates considered; `matched` counts returned records.

## Persistence
A snapshot has 8-byte ASCII magic `ATKV0002`, little-endian uint64 commit version, uint64 record count, then length-prefixed key/category/value UTF-8 byte strings. The loader rejects oversized individual fields (>16 MiB), excessive record count (>10M), duplicate keys, truncated input, wrong magic, and trailing bytes. Loading replaces state only after successful parsing. Snapshots omit historical revisions and are not a WAL.

## Interview questions to practice
1. Why do point reads need history when commits are optimistic?
2. What does a read-write conflict prevent? What does it not prevent?
3. How would you implement range-read phantom detection?
4. How does category indexing change query complexity?
5. Why is atomic rename insufficient for durable transactions?
6. What would a cost-based planner need (cardinality, selectivity, costs)?
7. How would you safely garbage-collect old revisions while transactions are active?
8. What failure modes would a WAL recovery test inject?
