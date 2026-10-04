#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parsed_data/create_index_info.hpp"
#include "duckdb/parser/expression/collate_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg;
using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<BaseTableRef> Table(){
 auto b=make_uniq<BaseTableRef>();b->schema_name="s.dot";b->table_name="Mixed";return b;
}
void TestIndex(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(!c.Query("CREATE SCHEMA \"s.dot\"; CREATE TABLE \"s.dot\".\"Mixed\"(x INTEGER); INSERT INTO \"s.dot\".\"Mixed\" VALUES (7)")->HasError(),"index fixture");
 for(bool unique:{false,true}){
   optional<bool> flag;if(unique)flag=true;
   string name=unique?"Unique_ö":"ordinary";
   auto actual=F::TransformCreateIndexStmt(t,flag,{},Identifier(name),Table(),vector<string>{"x"},{},{},{},{});
   auto &i=actual->info->Cast<CreateIndexInfo>();
   Check(i.schema=="s.dot" && i.table=="Mixed" && i.index_name==name && i.index_type=="ART","index names");
   Parser parser;parser.ParseQuery("CREATE "+string(unique?"UNIQUE ":"")+"INDEX \""+name+"\" ON \"s.dot\".\"Mixed\" (x)");
   auto &ref=parser.statements[0]->Cast<CreateStatement>().info->Cast<CreateIndexInfo>();
   Check(i.constraint_type==ref.constraint_type && i.on_conflict==ref.on_conflict &&
         i.expressions[0]->Equals(*ref.expressions[0]),"index canonical AST");
   Check(i.expressions[0].get()!=i.parsed_expressions[0].get() &&
         i.expressions[0]->Equals(*i.parsed_expressions[0]),"index independent parsed copy");
   auto r=c.Query(std::move(actual));if(r->HasError())throw std::runtime_error(r->GetError());
 }
 auto repeat=F::TransformCreateIndexStmt(t,true,true,Identifier("Unique_ö"),Table(),vector<string>{"x"},{},{},{},{});
 Check(!c.Query(std::move(repeat))->HasError(),"IF NOT EXISTS live");
 Check(c.Query("INSERT INTO \"s.dot\".\"Mixed\" VALUES (7)")->HasError(),"unique constraint not enforced");
 vector<unique_ptr<ParsedExpression>> exprs;exprs.push_back(make_uniq<ColumnRefExpression>("x"));
 auto *identity=exprs[0].get();
 case_insensitive_map_t<unique_ptr<ParsedExpression>> opts;opts["capacity"]=make_uniq<ConstantExpression>(Value(8));
 auto ex=F::TransformCreateIndexStmt(t,{},{},Identifier("expression"),Table(),{},Identifier("ART"),std::move(exprs),std::move(opts),{});
 auto &e=ex->info->Cast<CreateIndexInfo>();
 Check(e.parsed_expressions[0].get()==identity && e.expressions[0].get()!=identity &&
       e.options.at("capacity")==Value(8),"expression move/copy and options");
 int rejected=0;
 try{F::TransformCreateIndexStmt(t,{},{},{},Table(),{},{},{},{},{});}catch(const NotImplementedException&){rejected++;}
 try{F::TransformCreateIndexStmt(t,{},{},Identifier("partial"),Table(),{},{},{},{},make_uniq<ConstantExpression>(Value(true)));}catch(const NotImplementedException&){rejected++;}
 exprs={};exprs.push_back(make_uniq<CollateExpression>("nocase",make_uniq<ColumnRefExpression>("x")));
 try{F::TransformCreateIndexStmt(t,{},{},Identifier("collated"),Table(),{},{},std::move(exprs),{},{});}catch(const NotImplementedException&){rejected++;}
 opts.clear();opts["capacity"]=make_uniq<ColumnRefExpression>("x");
 try{F::TransformCreateIndexStmt(t,{},{},Identifier("bad"),Table(),{},{},{},std::move(opts),{});}catch(const InvalidInputException&){rejected++;}
 Check(rejected==4,"index negative diagnostics");
 std::cout<<"PASS CREATE INDEX: canonical AST, quoted names, copies/moves, options, live unique/IF NOT EXISTS and four rejections\n";
}
#endif
