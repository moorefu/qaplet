# M2 native mandatory inventory

`python scripts/m2_native_matrix.py inventory --report FILE` emits all five
mandatory native targets and every case name for the public ordinary SDK,
package examples, CLI, signal sender/strong gate, controlled faults, TTY, mixed
cleanup, four OS backend probes and the separate M2 lifecycle/mixed counters.
Missing targets remain `not_run`; a selected target with missing scopes is
`blocked`. `release_ready` is always false. A guest or translated process cannot
replace a native target, and the M1 `qaplet-host.lifetime` result never supplies
the separate M2 lifecycle scope.

`evaluate --selection FILE --report NEW_FILE` is read-only. Selection schema 1
uses profile `m2-native-matrix`, explicit `producer_report`, `distribution_root`,
`loader_dir`, `path_mappings`, and a `native` array. Each native entry names one
target and a `reports` mapping keyed by the inventory's scopes. Each report is
an explicit path or path/SHA/size identity. Provider files and selected raw logs
are protected against output collisions. Reports retain the measured host,
per-scope source, cases, raw evidence index and cleanup facts.

Native SDK contracts use the existing authoritative SDK validator. Independent
fault/TTY/mixed validators replay the current public catalog, actual original
outputs, cleanup barriers, terminal mode readback and retained-negative scope.
Backend probes retain their own source/producer identities; they demonstrate
real OS behavior and do not independently certify public SDK support. M2 cycle
reports must join an audited independent fixture producer, its retained public
compiler inputs, actual current M2 C source, original packer output, raw progress
and real process exit. Every target still needs 1,000 complete M2 lifecycle
cycles and 100 complete mixed cycles.

The accepted ticket23 receipt may be selected as `accepted_ticket23` only for
exact reuse of its immutable original report scopes and raw inventory. It cannot
relabel those executions as another SDK/product recipe. For new CLI executions,
`contract_oracle_receipt` plus a report entry's `contract_oracle` may select that
same accepted public CLI contract's case names and expected bytes. This is an
expectation oracle; the new SDK/CLI execution, raw stdout/stderr, terminal/child
readback and overlapped-native protocol are independently checked. Old success
scores do not become new product executions.

Cleanup is not one blanket boolean. Expected incomplete mixed/fault/CLI cases
retain their actual false/null scope, metadata/argument cases remain inapplicable,
and the strong signal gate's observed child retirement does not imply whole-SDK
quiescence. All success, failure, skipped/not-run and supervisor-timeout facts
stay visible. Full/CI, the three unavailable native hosts, actual 64K acceptance,
the one-hour soak and final strict M2 gate remain separate requirements.

The ordinary three mixed smokes and the independent twelve mixed contracts
deliberately configure event queue 1, worker 1 and helper 6. This is a bounded
fixture combination, separate from the product defaults. The new cycle fixture
uses the default worker/helper limits (2/4); TTY quota-refusal cases additionally
declare their own small explicit limits. Standalone backend probe limits describe
those native Host probes, not Runtime defaults.
