#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestJoinAST(PEGTransformer &t) {
    auto table=PEGTransformerFactory::TransformCatalogReservedSchemaTable(t,Identifier("Cat"),Identifier("Sch"),Identifier("Table X"));
    auto *table_identity=table.get(); TableAlias table_alias; table_alias.name=Identifier("Alias Ö"); table_alias.column_name_alias={Identifier("Column X")};
    auto aliased=PEGTransformerFactory::TransformBaseTableRef(t,{},std::move(table),table_alias,{},{});
    if(aliased.get()!=table_identity || aliased->alias!="Alias Ö" || aliased->column_name_alias!=vector<string>{"Column X"})throw std::runtime_error("table alias/path ownership lost");
    auto &bt=aliased->Cast<BaseTableRef>();
    if(bt.catalog_name!="Cat" || bt.schema_name!="Sch" || bt.table_name!="Table X")throw std::runtime_error("qualified table lost");
    auto expr=PEGTransformerFactory::TransformColIdExpression(t,Identifier("Expr Ö"),make_uniq<ConstantExpression>(Value(1)));
    if(expr->GetAlias()!="Expr Ö")throw std::runtime_error("expression alias lost");
    auto sample=PEGTransformerFactory::TransformSampleCount(t,make_uniq<ConstantExpression>(Value(25)),true);
    if(!sample->is_percentage || sample->sample_size.GetValue<double>()!=25)throw std::runtime_error("percentage sample lost");
    bool sample_rejected=false;
    try { PEGTransformerFactory::TransformSampleCount(t,make_uniq<ConstantExpression>(Value(101)),true); }catch(const ParserException &){sample_rejected=true;}
    if(!sample_rejected)throw std::runtime_error("invalid percentage sample accepted");
    Parser table_parser; table_parser.ParseQuery("SELECT * FROM range(3) WITH ORDINALITY AS \"Alias X\"(\"Value X\", ord)");
    auto &table_expected = table_parser.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().from_table;
    vector<FunctionArgument> args; args.emplace_back(make_uniq<ConstantExpression>(Value(3)));
    TableAlias alias; alias.name = Identifier("Alias X"); alias.column_name_alias = {Identifier("Value X"), Identifier("ord")};
    auto table_actual = PEGTransformerFactory::TransformTableFunctionLateralOpt(t, false,
        duckpgq_compat::MakeQualifiedName(Identifier("range")), std::move(args), true, alias);
    if (!table_actual->Equals(*table_expected)) throw std::runtime_error("table function alias/ordinality mismatch");
    for (bool percent : {false, true}) {
        Parser p; p.ParseQuery(percent ? "SELECT 1 LIMIT 25% OFFSET 2" : "SELECT 1 LIMIT 25 OFFSET 2");
        auto &expected = p.statements[0]->Cast<SelectStatement>().node->modifiers[0];
        LimitPercentResult limit, offset; limit.is_percent = percent;
        limit.expression = make_uniq<ConstantExpression>(Value(25));
        offset.expression = make_uniq<ConstantExpression>(Value(2));
        auto actual = PEGTransformerFactory::VerifyLimitOffset(limit, offset);
        if (!actual->Equals(*expected) || limit.expression || offset.expression) throw std::runtime_error("LIMIT AST/move mismatch");
    }
    LimitPercentResult empty, offset_only;
    if (PEGTransformerFactory::VerifyLimitOffset(empty, offset_only)) throw std::runtime_error("empty limit produced modifier");
    offset_only.is_percent = true;
    bool offset_rejected = false;
    try { PEGTransformerFactory::VerifyLimitOffset(empty, offset_only); } catch (const ParserException &) { offset_rejected = true; }
    if (!offset_rejected) throw std::runtime_error("percent OFFSET accepted");
    for (const char *sql : {"SELECT * FROM a JOIN b USING (\"Case X\", y)",
         "SELECT * FROM a LEFT JOIN b ON a.x = b.x", "SELECT * FROM a ASOF LEFT JOIN b ON a.x >= b.x",
         "SELECT * FROM a CROSS JOIN b", "SELECT * FROM a NATURAL LEFT JOIN b", "SELECT * FROM a POSITIONAL JOIN b"}) {
        Parser p; p.ParseQuery(sql);
        auto &expected = p.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().from_table->Cast<JoinRef>();
        unique_ptr<TableRef> actual;
        if (expected.ref_type == JoinRefType::CROSS || expected.ref_type == JoinRefType::NATURAL || expected.ref_type == JoinRefType::POSITIONAL) {
            JoinPrefix prefix; prefix.ref_type = expected.ref_type; prefix.join_type = expected.type;
            actual = PEGTransformerFactory::TransformJoinWithoutOnClause(t, prefix, expected.right->Copy());
        } else {
            JoinQualifier q;
            if (expected.condition) q = PEGTransformerFactory::TransformOnClause(t, expected.condition->Copy());
            else q = PEGTransformerFactory::TransformUsingClause(t, StringsToIdentifiers(expected.using_columns));
            auto right = expected.right->Copy(); auto *identity = right.get();
            actual = PEGTransformerFactory::TransformRegularJoinClause(t, expected.ref_type == JoinRefType::ASOF, expected.type, std::move(right), std::move(q));
            if (actual->Cast<JoinRef>().right.get() != identity) throw std::runtime_error("JOIN right ownership lost");
        }
        actual->Cast<JoinRef>().left = expected.left->Copy();
        if (!actual->Equals(expected)) throw std::runtime_error(string("JOIN canonical AST mismatch: ") + sql);
    }
    bool rejected = false;
    try { PEGTransformerFactory::TransformUsingClause(t, {Identifier("")}); } catch (const ParserException &) { rejected = true; }
    if (!rejected) throw std::runtime_error("empty USING column accepted");
    std::cout << "PASS JOIN/SELECT modifiers: six JOIN ASTs, LIMIT/percent/OFFSET, table-function aliases/ordinality, ownership and negatives\n";
}
#endif
