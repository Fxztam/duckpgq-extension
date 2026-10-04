#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
void TestArraySubquery(PEGTransformer &t) {
    DuckDB db(nullptr); Connection con(db);
    for(const string source : {"SELECT 73", "SELECT 1 WHERE false", "SELECT NULL::INTEGER",
        "SELECT x FROM (VALUES (3),(1),(2)) t(x) ORDER BY 1 DESC",
        "SELECT x FROM (VALUES (3),(1),(2)) t(x) ORDER BY x",
        "SELECT 3 AS x UNION ALL SELECT 1 ORDER BY 1",
        "SELECT x FROM (VALUES (3),(1),(2)) t(x) ORDER BY x LIMIT 2"}) {
        Parser p;p.ParseQuery(source);
        auto select=unique_ptr_cast<SQLStatement,SelectStatement>(std::move(p.statements[0]));
        auto *identity=select.get();
        auto actual=PEGTransformerFactory::TransformArrayParensSelect(t,std::move(select));
        auto &sub=actual->Cast<SubqueryExpression>();
        auto &outer=sub.subquery->node->Cast<SelectNode>();
        auto &inner=outer.from_table->Cast<SubqueryRef>();
        if(inner.subquery.get()!=identity || sub.subquery_type!=SubqueryType::SCALAR)throw std::runtime_error("array subquery ownership/type lost");
        auto expected=con.Query("SELECT ARRAY("+source+")");
        auto generated=con.Query("SELECT "+actual->ToString());
        if(expected->HasError())throw std::runtime_error("canonical ARRAY oracle: "+expected->GetError());
        if(generated->HasError())throw std::runtime_error("array query execution: "+generated->GetError());
        if(expected->GetValue(0,0)!=generated->GetValue(0,0))throw std::runtime_error("array runtime result differs: "+source);
    }
    Parser multi;multi.ParseQuery("SELECT 1,2");bool rejected=false;
    try { PEGTransformerFactory::TransformArrayParensSelect(t,unique_ptr_cast<SQLStatement,SelectStatement>(std::move(multi.statements[0]))); }
    catch(const BinderException &){rejected=true;}
    if(!rejected)throw std::runtime_error("multi-column ARRAY accepted");
    std::cout<<"PASS ARRAY subquery: seven live canonical result comparisons, ordering/empty/NULL/UNION/LIMIT, ownership and arity\n";
}
#endif
