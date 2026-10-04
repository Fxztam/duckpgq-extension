#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include <stdexcept>
#include <iostream>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<SelectStatement> Select(const string&s){Parser p;p.ParseQuery(s);return unique_ptr_cast<SQLStatement,SelectStatement>(std::move(p.statements[0]));}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static bool Same(const Value&a,const Value&b){return a.IsNull()||b.IsNull()?a.IsNull()&&b.IsNull():a==b;}
void TestSubqueries(PEGTransformer&t){
    DuckDB db(nullptr);Connection c(db);
    for(const string sql:{"SELECT 73","SELECT 1 WHERE false","SELECT NULL::INTEGER","SELECT x FROM (VALUES (1),(2)) t(x)"})
    for(bool exists:{false,true})for(bool neg:{false,true}){
        auto stmt=Select(sql);auto *id=stmt.get();
        auto expr=PEGTransformerFactory::TransformSubqueryExpression(t,neg,exists,make_uniq<SubqueryRef>(std::move(stmt)));
        auto &sub=(neg?*expr->Cast<OperatorExpression>().children[0]:*expr).Cast<SubqueryExpression>();
        Check(sub.subquery.get()==id,"subquery statement ownership");
        Check(sub.subquery_type==(exists?SubqueryType::EXISTS:SubqueryType::SCALAR),"subquery type");
        string ref=(neg?"NOT ":"")+string(exists?"EXISTS ":"")+"("+sql+")";
        auto expected=Parser::ParseExpressionList(ref);
        Check(expr->Equals(*expected[0]),"subquery AST differs");
        auto a=c.Query("SELECT "+expr->ToString());auto b=c.Query("SELECT "+ref);
        Check(a->HasError()==b->HasError(),"subquery error parity");
        const bool multi=sql.find("VALUES")!=string::npos;
        Check(a->HasError()==(!exists&&multi),"scalar cardinality rejection");
        if(!a->HasError())Check(Same(a->GetValue(0,0),b->GetValue(0,0)),"subquery result differs");
    }
    Check(!c.Query("CREATE TABLE values_table(x INTEGER); INSERT INTO values_table VALUES (42)")->HasError(),"table fixture");
    for(bool exists:{false,true}){
        auto table=make_uniq<BaseTableRef>();table->table_name="values_table";auto *id=table.get();
        auto expr=PEGTransformerFactory::TransformSubqueryExpression(t,{},exists,std::move(table));
        auto &node=expr->Cast<SubqueryExpression>().subquery->node->Cast<SelectNode>();
        Check(node.from_table.get()==id&&node.select_list.size()==1,"table-ref wrapper ownership");
        auto a=c.Query("SELECT "+expr->ToString());
        Check(!a->HasError(),"wrapped table execution");
        Check(a->GetValue(0,0)==(exists?Value::BOOLEAN(true):Value::INTEGER(42)),"wrapped table result");
    }
    auto inner=Select("SELECT x+10 FROM (VALUES (1)) t(x)");
    auto expr=PEGTransformerFactory::TransformSubqueryExpression(t,{},{},make_uniq<SubqueryRef>(std::move(inner)));
    Check(expr->Cast<SubqueryExpression>().subquery_type==SubqueryType::SCALAR,"omitted defaults");
    Check(PEGTransformerFactory::TransformSubqueryNot(t)&&PEGTransformerFactory::TransformSubqueryExists(t),"keyword mapping");
    std::cout<<"PASS subqueries: 16 scalar/EXISTS/NOT AST and live cases, NULL/empty/cardinality, table wrappers and ownership\n";
}
#endif
