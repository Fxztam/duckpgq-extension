#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string&s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestAggregateStar(PEGTransformer &t) {
    DuckDB db(nullptr);Connection c(db);
    for(bool distinct:{false,true}) {
        MethodArguments args;auto child=P("x");auto *id=child.get();args.arguments.emplace_back(std::move(child));
        args.distinct=distinct;
        args.order_bys.emplace_back(OrderType::DESCENDING,OrderByNullType::NULLS_LAST,P("x"));
        auto expr=PEGTransformerFactory::TransformMethodExpression(t,"sum",std::move(args));
        auto ref=P(distinct?"sum(DISTINCT x ORDER BY x DESC NULLS LAST)":"sum(x ORDER BY x DESC NULLS LAST)");
        if(!expr->Equals(*ref))throw std::runtime_error("aggregate AST actual="+expr->ToString()+" expected="+ref->ToString());
        Check(expr->Cast<FunctionExpression>().children[0].get()==id,"aggregate ownership");
        const string from=" FROM (VALUES (1),(2),(1),(NULL)) t(x)";
        auto a=c.Query("SELECT "+expr->ToString()+from);auto b=c.Query("SELECT "+ref->ToString()+from);
        Check(!a->HasError()&&!b->HasError()&&a->GetValue(0,0)==b->GetValue(0,0),"aggregate live result");
        // Canonical parsing rewrites ordered list() to list_sort(list()).
        // Compare execution independently, retaining an order-sensitive oracle.
        expr->Cast<FunctionExpression>().function_name="list";
        auto ordered=c.Query("SELECT "+expr->ToString()+from);
        auto oracle=c.Query(string("SELECT ")+(distinct?"list(DISTINCT x ORDER BY x DESC NULLS LAST)":"list(x ORDER BY x DESC NULLS LAST)")+from);
        Check(!ordered->HasError()&&!oracle->HasError()&&ordered->GetValue(0,0)==oracle->GetValue(0,0),"ordered list result");
    }
    MethodArguments count;count.arguments.emplace_back(make_uniq<StarExpression>());
    auto cnt=PEGTransformerFactory::TransformMethodExpression(t,"count",std::move(count));
    Check(cnt->Cast<FunctionExpression>().children.empty(),"count star lowering");
    for(bool ignore:{false,true}) {
        MethodArguments args;args.has_ignore_nulls=true;args.ignore_nulls=ignore;bool bad=false;
        try{PEGTransformerFactory::TransformMethodExpression(t,"list",std::move(args));}catch(const ParserException&){bad=true;}
        Check(bad,"non-window NULL modifier accepted");
    }
    for(int mode=0;mode<5;mode++){
        optional<vector<string>> qualifier;if(mode==1)qualifier=vector<string>{"t"};
        optional<qualified_column_set_t> exclude;
        optional<case_insensitive_map_t<unique_ptr<ParsedExpression>>> replace;
        optional<qualified_column_map_t<string>> rename;
        string source="*";ParsedExpression *id=nullptr;
        if(mode==1)source="t.*";
        if(mode==2){exclude=qualified_column_set_t{};exclude->insert(QualifiedColumnName("y"));source="* EXCLUDE(y)";}
        if(mode==3){replace=case_insensitive_map_t<unique_ptr<ParsedExpression>>{};auto v=P("x+1");id=v.get();(*replace)["x"]=std::move(v);source="* REPLACE(x+1 AS x)";}
        if(mode==4){rename=qualified_column_map_t<string>{};(*rename)[QualifiedColumnName("x")]="Mixed Name";source="* RENAME(x AS \"Mixed Name\")";}
        auto expr=PEGTransformerFactory::TransformStarExpression(t,qualifier,exclude,std::move(replace),rename);
        Check(expr->Equals(*P(source)),"star modifier AST");
        if(id)Check(expr->Cast<StarExpression>().replace_list["x"].get()==id,"star replacement ownership");
        const string from=" FROM (VALUES (3,4)) t(x,y)";
        auto a=c.Query("SELECT "+expr->ToString()+from);auto b=c.Query("SELECT "+source+from);
        Check(!a->HasError()&&!b->HasError()&&a->names==b->names&&a->ColumnCount()==b->ColumnCount(),"star live schema");
        for(idx_t i=0;i<a->ColumnCount();i++)Check(a->GetValue(i,0)==b->GetValue(i,0),"star live values");
    }
    for(int mode=0;mode<3;mode++){
        qualified_column_set_t ex;case_insensitive_map_t<unique_ptr<ParsedExpression>> repl;qualified_column_map_t<string> ren;
        if(mode!=2)ex.insert(QualifiedColumnName("x"));
        if(mode!=1)repl["x"]=P("1");
        if(mode!=0)ren[QualifiedColumnName("x")]="z";
        bool bad=false;try{PEGTransformerFactory::TransformStarExpression(t,{},ex,std::move(repl),ren);}catch(const ParserException&){bad=true;}
        Check(bad,"conflicting star modifiers accepted");
    }
    bool bad=false;try{PEGTransformerFactory::TransformStarExpression(t,vector<string>{"a","b"},{},{},{});}catch(const ParserException&){bad=true;}
    Check(bad,"multiple star qualifiers accepted");
    for(const string selector:{"*","'x'"}) {
        auto expr=PEGTransformerFactory::TransformColumnsExpression(t,false,P(selector));
        auto ref=P("COLUMNS("+selector+")");
        Check(expr->Equals(*ref),"COLUMNS AST mismatch");
        auto a=c.Query("SELECT "+expr->ToString()+" FROM (VALUES (3,4)) t(x,y)");
        auto b=c.Query("SELECT COLUMNS("+selector+") FROM (VALUES (3,4)) t(x,y)");
        Check(!a->HasError()&&!b->HasError()&&a->names==b->names,"COLUMNS live schema");
        for(idx_t i=0;i<a->ColumnCount();i++)Check(a->GetValue(i,0)==b->GetValue(i,0),"COLUMNS live values");
    }
    auto unpack=PEGTransformerFactory::TransformColumnsExpression(t,true,P("*"));
    auto unpacked=c.Query("SELECT greatest("+unpack->ToString()+") FROM (VALUES (3,4)) t(x,y)");
    Check(!unpacked->HasError()&&unpacked->GetValue(0,0)==Value::INTEGER(4),"COLUMNS unpack result");
    std::cout<<"PASS aggregate/star: DISTINCT/order live parity, count, NULL rejection, five stars, ownership/schema and conflicts\n";
}
#endif
