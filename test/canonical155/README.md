# PGQ-only canonical 1.5.5 port: first adapter slice

DEALLOCATE (completed 2026-10-02): deallocate_ast.cpp covers both forms,
mixed-case/Unicode names, canonical AST fields and copy independence, live
PREPARE/EXECUTE/removal, repeated removal and name reuse. Fresh in-memory
connections only. deallocate-red-20260929.log is the test-first failure;
deallocate-final-20261002.log repeats the passing gate with
-IncludeSelectExpressions. Full PEG/extension-load proof remains separate.

PREPARE (2026-09-29): prepare_ast.cpp checks canonical AST/name/move/copy,
executes PGQ PREPARE→EXECUTE for named/positional arguments and prepared DML,
and checks two negative cases. COPY is AST-only with no filesystem execution.
prepare-red-20260929.log is test-first failure; prepare-final-20260929.log
exits 0 with -IncludeSelectExpressions. Child SQL uses canonical parsing;
full PEG parameter collection/reset remains outside this adapter proof.

EXECUTE (2026-09-29): execute_ast.cpp checks canonical argument ASTs, alias
clearing/moves, live positional/named/NULL/no-argument queries and three
rejections. PREPARE fixtures are canonical SQL in separate calls.
execute-red-20260929.log reproduces production incompatibilities;
execute-final-20260929.log exits 0 with -IncludeSelectExpressions.
This does not prove PGQ PREPARE or complete extension execution.

DROP (2026-09-29): drop_ast.cpp only removes objects it creates in fresh
in-memory databases. Index/table/schema, AST parity, IF EXISTS, RESTRICT/CASCADE
and four negative cases covered. drop-red-20260929.log is test-first failure;
drop-final-20260929.log exits 0 with -IncludeSelectExpressions. Other DROP kinds
are not comprehensively runtime-tested; see DONE for secret-validation delta.

DESCRIBE (2026-09-29): describe_ast.cpp executes qualified DESCRIBE/SUMMARIZE
and compares all result cells with canonical SQL, including NULLs. String and
query targets, schema/all table lists, moves and two ParserException cases
are covered. describe-red-20260929.log records test-first compile failure;
describe-final-20260929.log exits 0 with -IncludeSelectExpressions.
Full PEG and property-graph DESCRIBE execution remain outside this slice.

CREATE VIEW (2026-09-28): view_ast.cpp checks names/aliases, canonical AST,
query moves, actual normal/recursive execution and IF NOT EXISTS. Unsupported
VIEW options require NotImplementedException on canonical 1.5.5, including
DEFER_BINDING true/false. view-red-20260928.log is the test-first compile
failure; view-final-20260928.log exits 0 with -IncludeSelectExpressions.
This is scoped transformer coverage, not full PEG/extension-load proof.

TABLE matrix (2026-09-28): table_matrix.cpp adds interleaved constraints with
canonical AST comparison, FK runtime/type enforcement, generated recomputation,
collation positives/negatives, and partition/sort ownership plus canonical
CATALOG-error differential. Fixed logical-column indexing after intervening
table constraints. table-matrix-red-20260928.log reproduces this bug;
table-matrix-final-20260928.log exits 0 with -IncludeSelectExpressions.
The earlier four untested areas below are superseded for these concrete cases,
not for all possible SQL variants. No partition/sort host support is claimed.

CREATE TABLE (2026-09-28): complete unit is in the linked gate. table_ast.cpp
executes column/default/NOT NULL lowering and CTAS with/no data in memory,
checks query move identity and five negative cases. table-red-20260928.log
reproduces API failures; table-final-20260928.log exits 0 with
-IncludeSelectExpressions. FK/generated/partition/sort and interleaved table
constraints are not comprehensively runtime-tested. Full extension LOAD open.

SECRET semantic follow-up (2026-09-28): six name/option combinations now compare
PGQ ASTs and runtime catalog metadata with an independent canonical parser and
database. Named/unnamed missing TYPE requires ParserException on both paths.
All databases and secrets are temporary/in-memory; config provider and synthetic
values only. secret-parity-red-20260928.log reproduces the three deltas;
secret-parity-final-20260928.log exits 0 with -IncludeSelectExpressions.
This closes the three semantic gaps noted in the earlier entry below, not
exhaustive provider/persistence or full PEG/extension coverage.

CREATE SECRET (2026-09-28): secret_ast.cpp uses only synthetic values with the
HTTP/config provider, explicit memory storage and forced TEMPORARY persistence.
It checks catalog presence, IF NOT EXISTS, canonical constant expressions,
copies/names and three rejection cases. No network or environment credentials.
secret-red-20260927.log records test-first failure; secret-final-20260928.log
exits 0 with -IncludeSelectExpressions. Identifier-option normalization, named
missing TYPE, mixed-case names and persistence are not certified; see DONE.

CREATE MACRO (2026-09-27): macro_ast.cpp tests canonical scalar AST rendering,
quoted names, body/query moves, defaults, live scalar/table calls and three
parameter/kind rejection cases. Full transformer unit is mandatory.
macro-red-20260927.log is the test-first failure; macro-final-20260927.log exits
0 with -IncludeSelectExpressions. Typed/overloaded macros and complete PEG
parsing are not comprehensively covered; extension loading remains open.

CREATE INDEX (2026-09-27): index_ast.cpp covers canonical AST fields/expressions,
quoted names, independent copies/moves, constants, four negative cases, and
in-memory unique enforcement/IF NOT EXISTS. Full transformer unit is mandatory.
index-red-20260927.log records production compile failures before the fix;
index-final-20260927.log exits 0 with -IncludeSelectExpressions.
This is not exhaustive index grammar/options coverage or extension loading.

COPY follow-up (2026-09-27): complete transform_copy.cpp is mandatory in the
linked gate. copy_ast.cpp checks target/column/path/options, moves/deep copies,
duplicate and empty-option exceptions, SELECT and database pragma arguments.
Real schema/data copy executes between in-memory databases and verifies data.
Evidence: Knowledge .b/pgqa/copy-red-20260924.log (test-first failure) and
copy-final-20260927.log (exit 0 with -IncludeSelectExpressions).
This does not execute file import/export or prove full extension loading.

Common follow-up (2026-09-24): the complete transform_common.cpp is mandatory
in the linked gate. common_ast.cpp checks array bounds, qualified type names,
named aggregate children/copy ownership, live STRUCT/UNION binding and timestamp
precision. common-red-20260924.log captures test-first compile failures;
common-final-20260924.log exits 0 with -IncludeSelectExpressions.
Not exhaustive common grammar coverage; zero-sized arrays and precision 10
are unchanged and not asserted as host semantic parity. Full extension/LOAD open.

COMMENT follow-up (2026-09-24): comment_ast.cpp exercises the actual transformer,
table/column set-clear through direct AST execution and catalog metadata,
qualified names and negative targets. Canonical column-comment ToString loses
the column name; a separate field assertion protects identity, and execution
does not reparse that lossy SQL. comment-final-20260924.log exits 0 with
-IncludeSelectExpressions. Whole extension build remains RED in other units;
see Knowledge DONE and .b/full-build-20260924.out.log. No link/LOAD claim.

CURRENT 2026-09-24: run.ps1 -IncludeSelectExpressions exits 0; focused executable
and full SELECT/Expression object compilation PASS. Evidence in Knowledge:
.b/pgqa/comprehension-final-20260924.log. comprehension_ast.cpp adds twelve live
results plus named-argument alias/order/unique ownership checks. Earlier RED
entries below are historical. Full extension linking, full PEG execution and
dynamic LOAD remain separate open gates; eager evaluation-order parity for
erroring/volatile result expressions is not asserted.

interval_ast.cpp: eight units with positive/negative/fractional inputs (24 live
comparisons), pointer identity, string interval and invalid input. Evidence:
Knowledge .b/pgqa/interval-final-20260923.log (focused PASS, full Expression
compile still RED at a FunctionArgument-vector constructor). Not exhaustive
interval/overflow coverage or full PEG/extension execution.

replace_entry_ast.cpp now also tests three qualified-column constructors and
trim/ltrim/rtrim AST/ownership/fixed live results. REPLACE spelling, ownership,
invalid target and duplicates are covered. See Knowledge
.b/pgqa/constructors-final-20260923.log: focused PASS; remaining Expression
compile RED at the interval-literal function constructor. See DONE for TRIM
qualification/oracle limits; full PEG parsing/loading remains unproven.

subqueries_ast.cpp covers sixteen scalar/EXISTS/NOT AST/live cases, empty/NULL,
scalar cardinality rejection, statement and TableRef move identity, defaults
and keyword mappings. Evidence: Knowledge .b/pgqa/subqueries-final-20260923.log
(focused PASS; remaining Expression compile fails at TransformReplaceEntry).
Correlated queries, full PEG parsing and dynamic LOAD are not claimed here.

child_operators_ast.cpp: four COALESCE and four TRY cases with canonical AST,
ownership and live SQL checks; UNPACK via greatest(*COLUMNS(*)); rejection of
TRY binder errors and invalid scalar UNPACK. Evidence in Knowledge:
.b/pgqa/child-operators-final-20260923.log (focused PASS, later full Expression
compile RED at subquery type/query accessors). Not full PEG/dynamic loading.

aggregate_star_ast.cpp covers DISTINCT/ORDER BY, count-star lowering, forbidden
NULL modifiers, five star variants and modifier conflicts, COLUMNS selectors
and unpack execution. Canonical list_sort rewriting is covered through live
ordered-list results; exact aggregate AST comparison uses sum. Final evidence:
Knowledge .b/pgqa/aggregate-columns-final-20260922.log (focused PASS; full
Expression still RED at COALESCE/UNPACK/TRY). Nested COLUMNS/lambda and full PEG
parsing/dynamic extension execution are not claimed by this slice.

Unary/numbers: unary_numbers_ast.cpp exercises real prefix/numeric code with
test-local leaf dispatch, number/type boundaries, negation widening/fallback,
prefix order/ownership, atomic negative minima, BIGNUM precision and rejection.
Evidence: Knowledge .b/pgqa/unary-final-20260922.log (focused executable PASS;
remaining Expression compile RED at aggregate modifiers/star APIs). This is
not full PEG parsing or loaded-extension execution. See DONE for oracle details.

2026-09-22: collate_parameters_ast.cpp covers three actual collation transforms
(AST, ownership, live results), invalid collation class, anonymous/numbered/named
parameter registration and reuse, mixing rejection and positional references.
Parameters are not yet a prepared-execution test. Evidence in Knowledge:
.b/pgqa/collate-parameters-final-20260922.log (focused executable PASS; later
full Expression compile RED at unary flags/aggregate modifiers). Numeric prefix
helpers and end-to-end PEG/extension tests remain open.

Arithmetic: arithmetic_ast.cpp tests actual &, -, / and ^ chains against
canonical AST and live results, operator flags, ownership, passthrough and
integer division. Knowledge .b/pgqa/arithmetic-final-20260914.log: focused
executable PASS; remaining Expression gate RED at constant/column APIs and
later unary/parameter APIs. No complete parser/build/LOAD claim.

Regex/CASE/BETWEEN: regex_between_ast.cpp checks ten range AST/ownership/live
cases, full-match default, eight case/negation combinations, 48 ANY/ALL results
against an independent SQL subquery oracle, and ESCAPE rejection. SQL-rendered
transformer execution is not a loaded-extension/PEG end-to-end test. Evidence:
Knowledge .b/pgqa/regex-between-final-20260914.log (focused executable PASS,
remaining Expression compile RED at arithmetic flags and later APIs).

Struct/IS/comparison, 2026-09-14: struct_comparison_ast.cpp calls actual
transformers, checks aliases and child ownership, executes 18 IS combinations,
compares six comparison ASTs and DISTINCT FROM/nested tails, and checks empty,
absent and invalid cases. Struct AST oracle uses an explicit unqualified call
matching existing PGQ resolution; its live result oracle uses brace syntax.
Canonical braces add main qualification, so search-path/shadowing parity is
not claimed. Final executable PASS in Knowledge
.b/pgqa/struct-comparison-final-20260914.log; the subsequent full Expression
compile gate is still RED at regex options and generated CASE/BETWEEN accessors.

ARRAY subqueries, 2026-09-14: array_subquery_ast.cpp calls the actual PGQ
transformer and executes its rendered SQL in canonical DuckDB. Seven results
are compared against independent canonical ARRAY(source) queries (including
empty/NULL, ordering, UNION and LIMIT). Tests also check inner SELECT pointer
identity, scalar subquery type and multi-column rejection. Focused executable
PASS: Knowledge .b/pgqa/array-subquery-final-20260914.log; subsequent full
Expression compilation remains RED at struct aliases and other AST APIs.
This does not claim full PEG parsing, complete extension build or dynamic LOAD.

Expression basics/traversal, 2026-09-14: actual qualified-name/table-scan
transformers and nested ORDER qualification removal are tested (including
root identity and independently parsed AST comparison). Focused executable
PASS in Knowledge .b/pgqa/expression-basics-final-20260914.log. Subsequent
full Expression compile remains RED at ARRAY subquery APIs; no full-build claim.

2026-09-14: complete SELECT and central function-transformer units are mandatory
compile/link inputs. Tests cover additional aliases, sampling, IF/IFNULL and
ordinary function ASTs; 15 window cases now enter TransformFunctionExpression.
Remaining full Expression compilation is still RED. Evidence in Knowledge:
.b/pgqa/select-function-final-20260914.log (executable PASS, later compile FAIL).

Window-function assembly, 2026-09-13: window_function_ast.cpp runs the actual
BuildWindowFunction helper used by canonical OVER lowering. Fifteen canonical
AST comparisons, seven semantic negatives and named-window copy/override
checks PASS. Full Expression/SELECT source compilation is still red; direct
helper evidence is not full PEG parse/live query evidence. Logs in Knowledge:
.b/pgqa/window-function-check.log and window-function-final.log.

CASE/window-frame slice, 2026-09-13: window_case_ast.cpp executes real methods
from transform_window_case.cpp. Simple/searched CASE canonical ASTs, implicit
NULL ELSE, default/explicit frame boundaries, moves, EXCLUDE TIES and ORDER BY
ALL rejection PASS. Complete window-function lowering and named-window
inheritance remain outside this slice. Full SELECT/Expression gate stays red.
Evidence: Knowledge .b/pgqa/window-case-check.log and window-case-final.log.

GROUPING/CTE, 2026-09-13: grouping_cte_ast.cpp executes real GROUPING/CTE
transformer functions. CUBE/ROLLUP AST parity, duplicate row grouping, CUBE
bounds, materialization/names/query ownership, recursive UNION child/key
ownership and DML-host diagnosis pass. Recursive percentage LIMIT silently
dropped its modifier before the new failing regression; it now diagnoses it.
Evidence: Knowledge .b/pgqa/grouping-cte-recursive-red.log and
.b/pgqa/grouping-cte-final.log. Full SELECT/Expression remains red; no full
WITH-list parser dispatch, exhaustive GROUPING SETS or live query claim.

JOIN/SELECT modifiers, 2026-09-13: join_ast.cpp calls the actual extracted
transform_join.cpp/transform_select_modifiers.cpp functions. Six canonical JOIN
ASTs, LIMIT/percentage LIMIT/OFFSET, table-function aliases/ordinality and
negative/ownership tests PASS. Log: .b/pgqa/join-limit-final-20260913.log in
Knowledge (exit 0). Full SELECT/Expression gate still fails on GROUPING/CTE
and later APIs; no full PEG parse, live query execution or dynamic LOAD claim.

PIVOT/UNPIVOT, 2026-09-13: pivot_ast.cpp calls actual transformer functions;
transform_pivot.cpp, transform_pivot_table.cpp, transform_constant.cpp and the
PEGTransformer core compile/link. Tests compare three canonical parser ASTs
and check NULL modes, tuple/scalar IN, moves, rollback, group/name preservation,
qualified IN columns, UNPIVOT cardinality, constant/subquery rejection.
These PASS alongside earlier tests. Production source is shared, not copied
into a test oracle. Full PEG source parsing, generated registry dispatch and
live dynamic PIVOT enum execution are not proven by this focused gate.
Final log: Knowledge .b/pgqa/pivot-final-20260913.log. The combined
-IncludeSelectExpressions run still fails on unrelated remaining SELECT and
Expression port work; full build/dynamic LOAD remain open.

SELECT/Expression follow-up, 2026-09-13: expression_access.cpp checks canonical
table AST parity (quoted names, Unicode, embedded dots/quotes), invalid table
arities, alias/column order and cast/operator ownership. These adapter checks
pass. They do not execute the complete SELECT/Expression transformer units.

`./run.ps1 -IncludeSelectExpressions` additionally builds the explicit
`pgq_select_expression_compile` target and currently FAILS on remaining PIVOT,
Window/CASE and other API differences. It has the same bounded build deadline.
The incomplete target is excluded from the default build; baseline-only runs
print this limitation explicitly. No full build or dynamic LOAD is claimed.
Evidence: Knowledge `.b/pgqa/select-expression-final-20260913.log`.

ALTER follow-up, 2026-09-12: the full ALTER unit compiles/links. Real-transformer
tests compare rename/drop/nested-column/nullability ASTs with the canonical
parser and check three-stage dynamic-default materialization with separate
expression ownership. RESET value rejection is covered. Live DDL/WAL replay
and complete PEG source parsing are not established by this gate.

DML follow-up, 2026-09-12: DELETE/UPDATE/INSERT/MERGE translation units
compile/link. Executable transformer reconstruction tests preserve canonical
AST text for CTE/WHERE/USING/FROM/RETURNING forms, TRUNCATE and representative
conflict/merge forms. Pointer identity checks cover table/tuple moves; negative
tests cover tuple arity, INSERT DEFAULT VALUES conflict, duplicate MERGE action.
This is not complete PEG parsing, database execution or exhaustive DML coverage.

AST follow-up, 2026-09-12: CREATE TYPE/SCHEMA/SEQUENCE and
ANALYZE/ATTACH/CHECKPOINT plus generic-option units now compile/link.
Actual transformer tests compare supported utility and CREATE forms with
canonical parser AST text. ATTACH invalid/dynamic paths are diagnosed; enum
long-string storage and negative sequence increment are covered. This does
not execute database ATTACH/CHECKPOINT operations or establish a full PEG parse.

Discriminator follow-up, 2026-09-11: PropertyGraphTable now appends optional
string field 124. Missing values decode as empty; default empty output retains
the old bytes. The initial exact-spelling regression failed before the fix.
Independent old/new/explicit-empty wire fixtures, Unicode, vertex/edge/nested
graphs and 1,152 graph truncation boundaries now pass. This closes the
discriminator finding mentioned historically below. New-reader/old-record
compatibility is tested, not old-reader/new-record compatibility.

Path follow-up, 2026-09-09: the linked PathReference/PathElement/SubPath
roundtrips now pass. Base discriminator 100 precedes derived fields 200..202
or 200..206, following the canonical host convention. Independent wire oracle,
five match types/modes, nested/empty/WHERE paths, truncation, 756 invalid enum
bytes and old malformed-layout rejection are covered. This closes the path
format issue listed in the earlier entries below, not discriminator persistence
for PropertyGraphTable. No historical database migration is performed.

Serialization increment, 2026-09-09: the real PropertyGraphTable and
CreatePropertyGraphInfo units now compile/link and pass binary runtime tests.
The test first reproduced the Identifier compile failure, then the graph-reader
abort. PGQ-owned string-format adapters and populated vertex/edge read lists
resolve those failures. Independent wire oracles and every graph truncation
boundary are checked. PathReference/PathElement layout and the unpersisted
PropertyGraphTable.discriminator remain explicit follow-up findings.

Current increment, 2026-09-09: qualified-name/metadata helper regressions and
the actual USE transform pass against the canonical parser. Four type-name
cases, three parameter-map forms, USE quoting and invalid qualifications are
checked. The complete core transformer and transformer-factory files compile
in the default gate (pgq_metadata_compile); this does not link or execute the
complete PEG parser. Other AST files and serialization remain open.

2026-09-07. CompilerDeveloper. All source edits are inside this PGQ checkout;
the canonical DuckDB submodule and Freehold compiler were not modified.

Implemented `src/include/duckpgq/compat/function_access.hpp` and migrated the
function-name/argument-access sites in `src/core/parser/duckpgq_parser.cpp`
and `src/core/functions/table/match.cpp`. This uses real canonical `children`
and `function_name` fields; it does not add or shadow DuckDB host classes.
The fork branch uses its existing accessors. Runtime selection is unchanged.

Regression started red with the missing adapter header. The unit executable
now checks the linked library version v1.5.5, function name, const identity,
explicit copy, unique ownership transfer, value and replacement. Test ASTs
are produced by the canonical parser. Its internal Parser entry points are
not exported by the distributed DLL, so the unit links the existing canonical
static libraries using their matching /MT runtime. This is NOT a dynamic
extension-load test and NOT evidence of full PGQ compatibility.

Run `pwsh -NoProfile -File ./run.ps1` from this directory. Compilation has a
180-second process limit and execution 30 seconds. Outputs are retained under
the Knowledge `.b/pgqa` directory. No binaries are installed by this test.

Continuation 2026-09-08: named-argument lowering now passes an AST comparison
with the canonical parser; null arguments are diagnosed. CALL and table-function
construction use the new helper. Connect/Trigger translation units compile,
link into the unit executable, and four actual transform entry points reject
unsupported canonical-host features. Identifier map/conversion checks pass.

The complete MSVC extension build has finished with exit code 1. Remaining
groups include QualifiedName and expression getters, parameter metadata,
Identifier serialization, and scalar bind-callback signatures. A full build
and genuine dynamic LOAD into the unchanged canonical host remain open.

The next slice adds `duckpgq/compat/scalar_bind.hpp`: its canonical thunk has
the exact `bind_scalar_function_t` signature and borrows context, scalar
function and arguments. The runtime regression verifies reference identity,
unchanged expression ownership, return-type mutation and exception propagation.
All 15 scalar binder registrations route through `AdaptBind`; their binder
declarations/definitions use the PGQ-local input view. All FunctionData source
files are additionally compiled by `pgq_binders_compile` (PASS, 2026-09-08).
The CSR implementation also uses the adapter but its complete translation
unit is still part of the not-yet-green full extension build.
