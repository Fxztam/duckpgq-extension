#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string&s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestComprehension(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 for(const string list:{"[1,2,3]","[]::INTEGER[]","[NULL,1,3]"})
 for(const string filter:{"","x > 1","false","NULL::BOOLEAN"}){
   auto value=P("x*2");auto *v=value.get();auto source=P(list);auto *s=source.get();
   optional<unique_ptr<ParsedExpression>> predicate;ParsedExpression *f=nullptr;
   if(!filter.empty()){predicate=P(filter);f=predicate->get();}
   auto expr=PEGTransformerFactory::TransformListComprehensionExpression(t,std::move(value),vector<Identifier>{Identifier("x")},std::move(source),std::move(predicate));
   int values=0,sources=0,filters=0;bool aliases=false;
   std::function<void(ParsedExpression&)> visit=[&](ParsedExpression &e){
     values+=&e==v;sources+=&e==s;filters+=f&&&e==f;
     if(e.GetExpressionClass()==ExpressionClass::FUNCTION){
       auto &fn=e.Cast<FunctionExpression>();
       if(fn.function_name=="struct_pack"){Check(fn.children.size()==2,"struct argument arity");
         Check(fn.children[0].get()==f && fn.children[0]->GetAlias()=="filter" &&
               fn.children[1].get()==v && fn.children[1]->GetAlias()=="result","named argument order/ownership");aliases=true;}
     }
     ParsedExpressionIterator::EnumerateChildren(e,visit);
   };visit(*expr);
   Check(values==1&&sources==1&&filters==(f?1:0)&&aliases==!filter.empty(),"comprehension ownership/aliases");
   string ref="[x*2 FOR x IN ("+list+")"+(filter.empty()?"":" IF "+filter)+"]";
   auto a=c.Query("SELECT "+expr->ToString());auto b=c.Query("SELECT "+ref);
   if(a->HasError())throw std::runtime_error(a->GetError());
   if(b->HasError())throw std::runtime_error(b->GetError());
   Check(a->GetValue(0,0)==b->GetValue(0,0),"comprehension values/order");
 }
 auto f=P("x>0");auto *id=f.get();
 Check(PEGTransformerFactory::TransformListComprehensionFilter(t,std::move(f)).get()==id,"filter forwarding");
 std::cout<<"PASS comprehension: 12 filtered/unfiltered/empty/NULL results, named argument aliases/order and unique child ownership\n";
}
#endif
