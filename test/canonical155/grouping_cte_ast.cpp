#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/statement/insert_statement.hpp"
#include "duckdb/parser/query_node/recursive_cte_node.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestGroupingCTE(PEGTransformer &t) {
    vector<unique_ptr<ParsedExpression>> rowargs;
    rowargs.push_back(make_uniq<ColumnRefExpression>("x"));
    rowargs.push_back(make_uniq<ColumnRefExpression>("x"));
    vector<GroupByExpressionInfo> duplicates;
    duplicates.push_back(PEGTransformerFactory::TransformGroupByBaseExpression(t, make_uniq<FunctionExpression>("row", std::move(rowargs))));
    auto dedup = PEGTransformerFactory::TransformGroupByList(t, std::move(duplicates));
    if (dedup.group_expressions.size()!=1 || dedup.grouping_sets.size()!=1 || dedup.grouping_sets[0].size()!=1) throw std::runtime_error("row grouping deduplication failed");
    int group_rejections=0;
    for (int size : {0,16}) {
        vector<unique_ptr<ParsedExpression>> args;
        for(int i=0;i<size;++i) args.push_back(make_uniq<ColumnRefExpression>("x"+std::to_string(i)));
        vector<GroupByExpressionInfo> items;
        items.push_back(PEGTransformerFactory::TransformCubeOrRollupClause(t,"CUBE",std::move(args)));
        try { PEGTransformerFactory::TransformGroupByList(t,std::move(items)); } catch(const ParserException &) { ++group_rejections; }
    }
    if(group_rejections!=2) throw std::runtime_error("empty/excessive CUBE must reject");
    Parser recursive; recursive.ParseQuery("SELECT 1 UNION ALL SELECT 2");
    auto node=std::move(recursive.statements[0]->Cast<SelectStatement>().node);
    auto *left=node->Cast<SetOperationNode>().children[0].get();
    auto *right=node->Cast<SetOperationNode>().children[1].get();
    vector<Identifier> aliases{Identifier("Value X")};
    vector<unique_ptr<ParsedExpression>> keys; keys.push_back(make_uniq<ColumnRefExpression>("Value X"));
    auto *key=keys[0].get();
    auto converted=PEGTransformerFactory::ToRecursiveCTE(std::move(node),Identifier("Rec Ö"),aliases,keys);
    auto &rec=converted->Cast<RecursiveCTENode>();
    if(!rec.union_all || rec.left.get()!=left || rec.right.get()!=right || rec.ctename!="Rec Ö" || rec.aliases!=vector<string>{"Value X"} || rec.key_targets[0].get()==key || !rec.key_targets[0]->Equals(*key)) throw std::runtime_error("recursive CTE move/copy/name mismatch");
    Parser limited; limited.ParseQuery("SELECT 1 UNION ALL SELECT 2 LIMIT 1");
    bool limit_rejected=false;
    try { PEGTransformerFactory::ToRecursiveCTE(std::move(limited.statements[0]->Cast<SelectStatement>().node),Identifier("r"),aliases,keys); } catch(const ParserException &) { limit_rejected=true; }
    if(!limit_rejected) throw std::runtime_error("recursive LIMIT accepted");
    Parser percent; percent.ParseQuery("SELECT 1 UNION ALL SELECT 2 LIMIT 25%");
    bool percent_rejected=false;
    try { PEGTransformerFactory::ToRecursiveCTE(std::move(percent.statements[0]->Cast<SelectStatement>().node),Identifier("r"),aliases,keys); } catch(const ParserException &) { percent_rejected=true; }
    if(!percent_rejected) throw std::runtime_error("recursive percentage LIMIT silently lost");
    for (const string kind : {"CUBE", "ROLLUP"}) {
        vector<unique_ptr<ParsedExpression>> expressions;
        expressions.push_back(make_uniq<ColumnRefExpression>("x")); expressions.push_back(make_uniq<ColumnRefExpression>("y"));
        vector<GroupByExpressionInfo> items;
        items.push_back(PEGTransformerFactory::TransformCubeOrRollupClause(t, kind, std::move(expressions)));
        auto actual = PEGTransformerFactory::TransformGroupByList(t, std::move(items));
        Parser p; p.ParseQuery("SELECT x, y FROM t GROUP BY " + kind + "(x, y)");
        auto &expected = p.statements[0]->Cast<SelectStatement>().node->Cast<SelectNode>().groups;
        if (actual.grouping_sets != expected.grouping_sets || actual.group_expressions.size() != 2) throw std::runtime_error("grouping sets mismatch");
        for (idx_t i=0;i<2;++i) if (!actual.group_expressions[i]->Equals(*expected.group_expressions[i])) throw std::runtime_error("grouping expression mismatch");
    }
    for (const auto mode : {CTEMaterialize::CTE_MATERIALIZE_ALWAYS, CTEMaterialize::CTE_MATERIALIZE_NEVER}) {
        Parser p; p.ParseQuery("SELECT 73 AS x");
        auto query = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(p.statements[0]));
        auto *identity = query.get();
        auto body = PEGTransformerFactory::TransformCTESelectBody(t, std::move(query));
        auto cte = PEGTransformerFactory::TransformWithStatement(t, Identifier("Name Ö"), vector<string>{"Alias X"}, {}, mode == CTEMaterialize::CTE_MATERIALIZE_NEVER, std::move(body));
        if (cte.second->query.get() != identity || cte.second->aliases != vector<string>{"Alias X"} || cte.second->materialized != mode) throw std::runtime_error("CTE names/materialization/ownership lost");
    }
    bool rejected=false;
    try { PEGTransformerFactory::TransformCTEDMLBody(t, make_uniq<InsertStatement>()); }
    catch (const ParserException &e) { rejected=string(e.what()).find("1.5.5") != string::npos; }
    if (!rejected) throw std::runtime_error("canonical DML CTE must diagnose host limitation");
    std::cout << "PASS GROUPING/CTE: CUBE/ROLLUP AST, aliases, materialization, ownership, DML diagnostic\n";
}
#endif
