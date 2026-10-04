#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/statement/transaction_statement.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb.hpp"
#include <iostream>
#include <stdexcept>
using namespace duckdb; using namespace duckdb::duckpgq_peg; using F=PEGTransformerFactory;
static void Check(bool b,const char*m){if(!b)throw std::runtime_error(m);}
static unique_ptr<SQLStatement> Parse(const string&s){Parser p;p.ParseQuery(s);return std::move(p.statements[0]);}
static void SameTx(const SQLStatement&a,const SQLStatement&b,const string&m){
 Check(a.type==StatementType::TRANSACTION_STATEMENT&&b.type==a.type,(m+": statement type").c_str());
 auto &x=*a.Cast<TransactionStatement>().info;auto &y=*b.Cast<TransactionStatement>().info;
 Check(x.type==y.type && x.modifier==y.modifier && x.invalidation_policy==y.invalidation_policy && x.auto_rollback==y.auto_rollback,(m+": fields").c_str());
 if(a.ToString()!=b.ToString())std::cerr<<m<<" ToString: pgq "<<a.ToString()<<" | canonical "<<b.ToString()<<std::endl;
 Check(a.ToString()==b.ToString(),(m+": ToString").c_str());
}
void TestTransaction(PEGTransformer&t){
 DuckDB db(nullptr);Connection c(db);
 Check(F::TransformReadOnly(t)==TransactionModifierType::TRANSACTION_READ_ONLY,"read only marker");
 Check(F::TransformReadWrite(t)==TransactionModifierType::TRANSACTION_READ_WRITE,"read write marker");
 Check(F::TransformReadOrWrite(t,TransactionModifierType::TRANSACTION_READ_ONLY)==TransactionModifierType::TRANSACTION_READ_ONLY,"read or write passthrough");
 // BEGIN with and without TRANSACTION keyword and access mode
 struct Case{bool keyword;optional<TransactionModifierType> mode;string sql;};
 for(auto&cs:vector<Case>{{false,{},"BEGIN"},{true,{},"BEGIN TRANSACTION"},
   {true,TransactionModifierType::TRANSACTION_READ_ONLY,"BEGIN TRANSACTION READ ONLY"},
   {true,TransactionModifierType::TRANSACTION_READ_WRITE,"BEGIN TRANSACTION READ WRITE"}}){
  auto actual=F::TransformBeginTransaction(t,cs.keyword,cs.mode);
  SameTx(*actual,*Parse(cs.sql),cs.sql);
  auto copy=actual->Copy();
  Check(copy->Cast<TransactionStatement>().info.get()!=actual->Cast<TransactionStatement>().info.get(),"begin independent copy");
 }
 for(bool keyword:{false,true}){
  SameTx(*F::TransformCommitTransaction(t,keyword),*Parse(keyword?"COMMIT TRANSACTION":"COMMIT"),"commit");
  SameTx(*F::TransformRollbackTransaction(t,keyword),*Parse(keyword?"ROLLBACK TRANSACTION":"ROLLBACK"),"rollback");
 }
 // live: rollback discards, commit keeps, read-only blocks writes exactly like the canonical statement
 Check(!c.Query(F::TransformBeginTransaction(t,true,{}))->HasError(),"begin runtime");
 Check(!c.Query("CREATE TABLE rolled_back(i INT)")->HasError(),"create inside transaction");
 Check(!c.Query(F::TransformRollbackTransaction(t,true))->HasError(),"rollback runtime");
 Check(c.Query("SELECT * FROM rolled_back")->HasError(),"rollback discarded the table");
 Check(!c.Query(F::TransformBeginTransaction(t,false,{}))->HasError(),"begin runtime 2");
 Check(!c.Query("CREATE TABLE committed(i INT)")->HasError(),"create inside second transaction");
 Check(!c.Query(F::TransformCommitTransaction(t,false))->HasError(),"commit runtime");
 Check(!c.Query("SELECT * FROM committed")->HasError(),"commit kept the table");
 Check(!c.Query(F::TransformBeginTransaction(t,true,TransactionModifierType::TRANSACTION_READ_ONLY))->HasError(),"begin read only");
 auto write=c.Query("INSERT INTO committed VALUES (1)");
 Check(!c.Query(F::TransformRollbackTransaction(t,false))->HasError(),"rollback read only");
 Check(!c.Query("BEGIN TRANSACTION READ ONLY")->HasError(),"reference begin read only");
 auto write_ref=c.Query("INSERT INTO committed VALUES (1)");
 Check(!c.Query("ROLLBACK")->HasError(),"reference rollback");
 Check(write->HasError()&&write_ref->HasError()&&write->GetError()==write_ref->GetError(),"read-only write rejection identical");
 // COMMIT / ROLLBACK without an open transaction fail identically
 auto commit=c.Query(F::TransformCommitTransaction(t,false));auto commit_ref=c.Query("COMMIT");
 Check(commit->HasError()&&commit_ref->HasError()&&commit->GetError()==commit_ref->GetError(),"commit without transaction identical");
 auto rollback=c.Query(F::TransformRollbackTransaction(t,false));auto rollback_ref=c.Query("ROLLBACK");
 Check(rollback->HasError()&&rollback_ref->HasError()&&rollback->GetError()==rollback_ref->GetError(),"rollback without transaction identical");
 std::cout<<"PASS TRANSACTION: four BEGIN forms, COMMIT/ROLLBACK with and without keyword, live rollback/commit/read-only effects, identical no-transaction errors"<<std::endl;
}
#endif
