#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/expression/type_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/common/extra_type_info.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;
using namespace duckdb::duckpgq_peg;
using F = PEGTransformerFactory;
static void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static unique_ptr<ParsedExpression> Num(int64_t n) { return make_uniq<ConstantExpression>(Value::BIGINT(n)); }
template<class E, class FN> static void Reject(FN fn) {
 bool caught=false; try { fn(); } catch(const E &) { caught=true; }
 Check(caught,"common transformer accepted invalid input");
}
void TestCommon(PEGTransformer &t) {
 Check(F::TransformSquareBracketsArray(t,{})==-1,"unsized array sentinel");
 for(int64_t n:{1,7,1024})
   Check(F::TransformSquareBracketsArray(t,Num(n))==n,"fixed array bound");
 Reject<ParserException>([&]{F::TransformSquareBracketsArray(t,Num(-1));});
 Reject<BinderException>([&]{F::TransformSquareBracketsArray(t,make_uniq<ConstantExpression>(Value("bad")));});
 Reject<ParserException>([&]{F::TransformSquareBracketsArray(t,make_uniq<ColumnRefExpression>("n"));});
 auto one=F::TransformTypeNameAsQualifiedName(t,Identifier("Mixed"));
 auto two=F::TransformSchemaReservedTypeName(t,Identifier("s.dot"),Identifier("T"));
 auto three=F::TransformCatalogReservedSchemaTypeName(t,Identifier("cat"),Identifier("s"),Identifier("T"));
 Check(one.name=="Mixed" && two.schema=="s.dot" && two.name=="T" &&
       three.catalog=="cat" && three.schema=="s" && three.name=="T","qualified type names");
 auto qualified=F::TransformQualifiedSimpleType(t,three,{});
 auto &q=qualified->Cast<TypeExpression>();
 Check(q.GetCatalog()=="cat" && q.GetSchema()=="s" && q.GetTypeName()=="T","qualified type expression");
 vector<pair<Identifier,LogicalType>> input;
 input.emplace_back(Identifier("Mixed Name"),F::TransformType(t,F::TransformSimpleNumericType(t,"INTEGER"),{}));
 input.emplace_back(Identifier("unicode_ö"),F::TransformType(t,F::TransformCharacterSimpleType(t,{}),{}));
 auto fields=F::TransformColIdTypeList(t,input);
 Check(fields.size()==2 && fields[0].first=="Mixed Name" && fields[1].first=="unicode_ö","field spelling/order");
 auto row=F::TransformRowType(t,fields);
 auto un=F::TransformUnionType(t,fields);
 for(auto *expr:{row.get(),un.get()}) {
   auto &children=expr->Cast<TypeExpression>().GetChildren();
   Check(children.size()==2 && children[0]->GetAlias()=="Mixed Name" &&
         children[1]->GetAlias()=="unicode_ö","aggregate aliases");
   Check(children[0].get()!=UnboundType::GetTypeExpression(fields[0].second).get(),"aggregate child copy");
 }
 DuckDB db(nullptr); Connection c(db);
 for(auto *expr:{row.get(),un.get()}) {
   auto r=c.Query("SELECT CAST(NULL AS "+expr->ToString()+")");
   Check(!r->HasError(),"aggregate type binding");
 }
 for(int precision:{0,3,6,9}) {
   vector<unique_ptr<ParsedExpression>> mods;mods.push_back(Num(precision));
   auto time=F::TransformTimeType(t,LogicalTypeId::TIMESTAMP,std::move(mods),false);
   const char *expected=precision==0?"TIMESTAMP_S":precision==3?"TIMESTAMP_MS":precision==6?"TIMESTAMP":"TIMESTAMP_NS";
   Check(time->Cast<TypeExpression>().GetTypeName()==expected,"timestamp precision lowering");
   Check(!c.Query("SELECT CAST(NULL AS "+time->ToString()+")")->HasError(),"timestamp type binding");
 }
 for(int precision:{-1,11}) Reject<ParserException>([&] {
   vector<unique_ptr<ParsedExpression>> mods;mods.push_back(Num(precision));
   F::TransformTimeType(t,LogicalTypeId::TIMESTAMP,std::move(mods),false);
 });
 std::cout<<"PASS common: array bounds, qualified types, aggregate name/copy ownership and live type binding, timestamp precision\n";
}
#endif
