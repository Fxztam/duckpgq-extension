#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/function/scalar_macro_function.hpp"
#include "duckdb/function/table_macro_function.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SelectStatement> Select(const string &sql){
 Parser p;p.ParseQuery(sql);return unique_ptr_cast<SQLStatement,SelectStatement>(std::move(p.statements[0]));
}
void TestMacro(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);Check(!c.Query("CREATE SCHEMA \"s.dot\"")->HasError(),"macro schema");
 auto name=duckpgq_compat::MakeQualifiedName({Identifier("s.dot")},Identifier("Echo_ö"));
 auto expr=make_uniq<ColumnRefExpression>("x");auto *identity=expr.get();
 auto body=F::TransformScalarMacroDefinition(t,std::move(expr));
 Check(body->Cast<ScalarMacroFunction>().expression.get()==identity,"scalar body move");
 vector<MacroParameter> params;auto param=F::TransformSimpleParameter(t,Identifier("x"),{});
 param.is_default=true;param.expression=make_uniq<ConstantExpression>(Value(73));params.push_back(std::move(param));
 body=F::TransformMacroDefinition(t,std::move(params),std::move(body));
 Check(body->parameters.size()==1 && body->types.size()==1 &&
       body->default_parameters["x"]->Cast<ConstantExpression>().value==Value(73),"default parameter lowering");
 vector<unique_ptr<MacroFunction>> defs;defs.push_back(std::move(body));
 auto actual=F::TransformCreateMacroStmt(t,true,{},name,std::move(defs));
 auto &info=actual->info->Cast<CreateMacroInfo>();
 Check(info.schema=="s.dot" && info.name=="Echo_ö" && info.type==CatalogType::MACRO_ENTRY,"macro names/type");
 Parser ref;ref.ParseQuery("CREATE MACRO \"s.dot\".\"Echo_ö\"(x := 73) AS x");
 Check(actual->ToString()==ref.statements[0]->ToString(),"macro canonical AST rendering");
 auto repeat=actual->Copy();
 repeat->Cast<CreateStatement>().info->on_conflict=OnCreateConflict::IGNORE_ON_CONFLICT;
 auto r=c.Query(std::move(actual));if(r->HasError())throw std::runtime_error(r->GetError());
 Check(!c.Query(std::move(repeat))->HasError(),"macro ignore conflict");
 for(auto sql:{"SELECT \"s.dot\".\"Echo_ö\"()","SELECT \"s.dot\".\"Echo_ö\"(x := 73)"}){
  auto v=c.Query(sql);Check(!v->HasError() && v->GetValue(0,0)==Value(73),"scalar macro live default/explicit");
 }
 auto select=Select("SELECT x AS value");auto *node=select->node.get();
 auto table=F::TransformTableMacroDefinition(t,std::move(select));
 Check(table->Cast<TableMacroFunction>().query_node.get()==node,"table body move");
 params.clear();params.push_back(F::TransformSimpleParameter(t,Identifier("x"),{}));
 table=F::TransformMacroDefinition(t,std::move(params),std::move(table));
 defs.clear();defs.push_back(std::move(table));
 auto statement=F::TransformCreateMacroStmt(t,true,{},duckpgq_compat::MakeQualifiedName(Identifier("rows_macro")),std::move(defs));
 r=c.Query(std::move(statement));if(r->HasError())throw std::runtime_error(r->GetError());
 r=c.Query("SELECT value FROM rows_macro(19)");
 Check(!r->HasError() && r->GetValue(0,0)==Value(19),"table macro live parameter");
 int bad=0;
 for(bool duplicate:{false,true}){
   params.clear();auto a=F::TransformSimpleParameter(t,Identifier("X"),{});
   if(!duplicate){a.is_default=true;a.expression=make_uniq<ConstantExpression>(Value(1));}
   params.push_back(std::move(a));params.push_back(F::TransformSimpleParameter(t,Identifier(duplicate?"x":"y"),{}));
   try{F::TransformMacroDefinition(t,std::move(params),F::TransformScalarMacroDefinition(t,make_uniq<ConstantExpression>(Value(1))));}
   catch(const ParserException&){bad++;}
 }
 defs.clear();defs.push_back(F::TransformScalarMacroDefinition(t,make_uniq<ConstantExpression>(Value(1))));
 defs.push_back(F::TransformTableMacroDefinition(t,Select("SELECT 1")));
 try{F::TransformCreateMacroStmt(t,true,{},name,std::move(defs));}catch(const ParserException&){bad++;}
 Check(bad==3,"macro duplicate/order/mixed-kind rejection");
 std::cout<<"PASS CREATE MACRO: canonical AST, names, body moves, scalar defaults, table parameters and three rejections\n";
}
#endif
