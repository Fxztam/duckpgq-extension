# SQL/PGQ grammar migration

This branch targets the `GrammarExtension` API in the v2.0-cyanoptera DuckDB submodule. The first migration slice is
`CREATE PROPERTY GRAPH`. The old copied/patched PEG parser and its sync, namespace-wrapper, and regeneration scripts
have been removed. DuckDB itself is not patched.

## Activation

Loading DuckPGQ registers the `duckpgq` grammar extension. Activation is explicit and connection-local:

```sql
LOAD duckpgq;
SET active_grammar_extensions = ['duckpgq'];

CREATE TABLE vertices(id BIGINT, name VARCHAR);
CREATE PROPERTY GRAPH example VERTEX TABLES (vertices);
```

If other grammar extensions are in use, include their names in the same list. `RESET active_grammar_extensions`
restores the base grammar. Loading DuckPGQ no longer changes `allow_parser_override_extension` or intercepts every
SQL query. A new connection must activate the grammar independently.

Execute activation before sending queries containing PGQ syntax: DuckDB parses a query batch before executing
its `SET` statement. SQL `PREPARE` is currently unavailable for extension statements.

## Supported first slice

- `CREATE PROPERTY GRAPH`, `CREATE OR REPLACE PROPERTY GRAPH`, and `CREATE PROPERTY GRAPH IF NOT EXISTS`.
  `OR REPLACE` and `IF NOT EXISTS` cannot be combined.
- A required non-empty `VERTEX TABLES (...)` clause and an optional non-empty `EDGE TABLES (...)` clause.
- Quoted and qualified table names; `AS` table aliases; explicit labels and discriminator sublabels.
- Omitted properties, `ALL PROPERTIES`, `NO PROPERTIES`, `PROPERTIES [ARE] ALL COLUMNS`,
  `PROPERTIES [ARE] ALL COLUMNS EXCEPT (...)`, and explicit property lists with optional `AS` column aliases.
- Explicit source/destination key references, including composite keys, or short references inferred from table
  primary/foreign-key constraints.
- Existing create-graph binding, validation, metadata storage, and conflict policies.

As before, the property-graph registry is keyed by the graph's unqualified name. Table/reference qualification is
preserved. Temporary property graphs, vertex-key declarations, and arbitrary property expressions are not added by
this migration.

Edge metadata stores resolved physical vertex names and qualification rather than statement-local aliases, so
new connections can reconstruct graphs created with aliased references.

## Integration

`src/core/parser/grammar/create_property_graph.gram` defines the namespaced `DuckPGQ*` rules. Keep one complete rule
per non-comment line. CMake embeds those definitions in a generated header under the build directory and
automatically reconfigures when the `.gram` file changes. No grammar/transformer generation script is required.

`src/core/parser/create_property_graph_grammar.cpp` registers the embedded rules, attaches the handwritten transform
callbacks with `SetTransformProcess`, and prepends the create rule to the native `Statement` choice. Built-in
`QualifiedName`, `BaseTableName`, `TableAliasAs`, `ColId`, `Parens`, and `List` rules/transforms are reused rather than
copied. New keywords are unreserved, so ordinary SQL can still use them as identifiers. Transform wrappers remain
handwritten; reusable extension tooling can be added later.

The rule transforms to the existing `CreatePropertyGraphInfo`, wrapped in a native `ExtensionStatement` carrying
`DuckPGQParseData`. The existing parser-extension plan callback invokes the create-graph table function. The parser
extension is retained for planning only: no parser override or parse fallback is registered. DuckDB handles
tokenization, statement boundaries, native SQL transforms, and syntax-error locations.

## Verification and remaining migration

Build static/loadable extensions, the shell, and the test runner, then run:

```sh
./build/release/test/unittest 'test/sql/grammar_extension/*'
```

The focused suite verifies activation/reset and connection isolation, ordinary SQL coexistence, mixed statements,
quoted/schema-qualified names, table/property aliases, property modes, labels, inferred/explicit references,
conflict policies, the SQL `PREPARE` limitation, and syntax/semantic errors.

The legacy tests remain in place as a checklist, but many use grammar not yet migrated or assume automatic
activation. The full legacy suite is not a passing gate for this create-only branch. `GRAPH_TABLE`, `DROP`,
`DESCRIBE`, and `SUMMARIZE PROPERTY GRAPH` are deliberately unavailable through the new grammar. Their existing
implementation code is retained for subsequent slices.

Next, port graph-management statements, then graph table/path patterns and their query-planning integration. Extend
the focused tests as each syntax family returns; update legacy test activation when those families are supported.
