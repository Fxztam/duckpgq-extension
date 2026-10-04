#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/drop_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb;using namespace duckdb::duckpgq_peg;using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
void TestDeallocate(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db),reference(db);
 Check(F::TransformDeallocatePrepare(t),"DEALLOCATE PREPARE marker");
 for(bool keyword:{false,true}){
   string name=keyword?"Mixed_ö":"plain";
   optional<bool> flag;if(keyword)flag=F::TransformDeallocatePrepare(t);
   auto actual=F::TransformDeallocateStatement(t,flag,Identifier(name));
   auto expected=Parse("DEALLOCATE "+string(keyword?"PREPARE ":"")+"\""+name+"\"");
   auto &a=*actual->Cast<DropStatement>().info;auto &b=*expected->Cast<DropStatement>().info;
   Check(a.type==CatalogType::PREPARED_STATEMENT && a.name==name &&
     a.type==b.type && a.name==b.name && a.if_not_found==b.if_not_found &&
     a.cascade==b.cascade && a.catalog==b.catalog && a.schema==b.schema,"deallocate canonical AST fields");
   auto copy=actual->Copy();
   Check(copy->Cast<DropStatement>().info.get()!=actual->Cast<DropStatement>().info.get(),"deallocate independent info copy");
   Check(!c.Query(F::TransformPrepareStatement(t,Identifier(name),{},Parse("SELECT 73")))->HasError(),"PGQ PREPARE setup");
   Check(!reference.Query("PREPARE \""+name+"\" AS SELECT 73")->HasError(),"reference PREPARE setup");
   auto before=c.Query(F::TransformExecuteStatement(t,Identifier(name),{}));
   Check(!before->HasError() && before->GetValue(0,0)==Value(73),"execute before deallocate");
   Check(!c.Query(std::move(actual))->HasError() && !reference.Query(std::move(expected))->HasError(),"deallocate runtime");
   auto after=c.Query(F::TransformExecuteStatement(t,Identifier(name),{}));
   auto ref_after=reference.Query("EXECUTE \""+name+"\"");
   Check(after->HasError() && ref_after->HasError() && after->GetErrorType()==ref_after->GetErrorType(),"execute after deallocate rejection");
   auto repeat=c.Query(std::move(copy));
   auto ref_repeat=reference.Query("DEALLOCATE \""+name+"\"");
   Check(repeat->HasError()==ref_repeat->HasError(),"repeated deallocate canonical behavior");
   if(repeat->HasError())Check(repeat->GetErrorType()==ref_repeat->GetErrorType(),"repeated deallocate error class");
   Check(!c.Query(F::TransformPrepareStatement(t,Identifier(name),{},Parse("SELECT 91")))->HasError(),"reuse prepared name");
   auto reused=c.Query(F::TransformExecuteStatement(t,Identifier(name),{}));
   Check(!reused->HasError() && reused->GetValue(0,0)==Value(91),"reused prepared result");
 }
 std::cout<<"PASS DEALLOCATE: both forms, canonical AST/copy, PGQ PREPARE/EXECUTE lifecycle, repeat and name reuse\n";
}
#endif
