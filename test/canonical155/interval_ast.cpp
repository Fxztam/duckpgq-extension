#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb.hpp"
#include "duckdb/common/enums/date_part_specifier.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;
static unique_ptr<ParsedExpression> P(const string&s){auto v=Parser::ParseExpressionList(s);return std::move(v[0]);}
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void TestInterval(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 const vector<DatePartSpecifier> units={DatePartSpecifier::YEAR,DatePartSpecifier::MONTH,DatePartSpecifier::DAY,
 DatePartSpecifier::HOUR,DatePartSpecifier::MINUTE,DatePartSpecifier::SECOND,DatePartSpecifier::MILLISECONDS,DatePartSpecifier::MICROSECONDS};
 const vector<string> names={"YEAR","MONTH","DAY","HOUR","MINUTE","SECOND","MILLISECONDS","MICROSECONDS"};
 for(idx_t i=0;i<units.size();i++)for(const string number:{"2","-2","1.5"}){
   auto input=P(number);auto *identity=input.get();
   auto expr=PEGTransformerFactory::TransformIntervalLiteral(t,std::move(input),units[i]);
   idx_t seen=0;std::function<void(ParsedExpression&)> visit=[&](ParsedExpression &e){
     if(&e==identity)seen++;ParsedExpressionIterator::EnumerateChildren(e,visit);
   };visit(*expr);Check(seen==1,"interval child ownership");
   auto a=c.Query("SELECT "+expr->ToString());
   auto b=c.Query("SELECT INTERVAL ("+number+") "+names[i]);
   Check(!a->HasError()&&!b->HasError(),"interval execution failed");
   Check(a->GetValue(0,0)==b->GetValue(0,0),"interval result mismatch");
 }
 auto child=P("'2 days 03:04:05'");auto *id=child.get();
 auto expr=PEGTransformerFactory::TransformIntervalLiteral(t,std::move(child),{});
 Check(expr->Cast<CastExpression>().child.get()==id,"interval string ownership");
 auto a=c.Query("SELECT "+expr->ToString());auto b=c.Query("SELECT INTERVAL '2 days 03:04:05'");
 Check(!a->HasError()&&!b->HasError()&&a->GetValue(0,0)==b->GetValue(0,0),"interval string value");
 auto invalid=PEGTransformerFactory::TransformIntervalLiteral(t,P("'invalid interval'"),{});
 Check(c.Query("SELECT "+invalid->ToString())->HasError(),"invalid interval accepted");
 std::cout<<"PASS interval: 24 unit/sign/fraction results, moved child identity, string interval and invalid text\n";
}
#endif
