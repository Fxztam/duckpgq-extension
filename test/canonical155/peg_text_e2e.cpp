#ifdef PGQ_TRANSFORMER_TEST
#include "duckpgq/third_party/duckdb_peg_parser/peg/matcher.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/tokenizer/parser_tokenizer.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/create_statement.hpp"
#include "duckdb/parser/statement/drop_statement.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckpgq/parser/parsed_data/create_property_graph_info.hpp"
#include "duckpgq/parser/parsed_data/drop_property_graph_info.hpp"
#include "duckpgq/parser/tableref/matchref.hpp"
#include "duckdb.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
using namespace duckdb;

// Text-to-AST end-to-end: SQL text -> ParserTokenizer -> PEG matcher -> generated transformer glue ->
// SQLStatement, the same entry sequence as duckpgq_parser_override, compared with the canonical parser.
static string NoSpace(string s){s.erase(std::remove_if(s.begin(),s.end(),[](unsigned char c){return std::isspace(c)||c=='"'||c==';';}),s.end());return s;}

static vector<unique_ptr<SQLStatement>> PegParse(const string &sql){
 static duckpgq_peg::ParserCache cache;
 auto matcher=cache.GetMatcher();
 auto factory=cache.GetTransformerFactory();
 ParserOptions options;
 // Parser::NormalizeSQLString (used by duckpgq_parser.cpp) does not exist in canonical 1.5.5; the ASCII corpus needs none.
 const string &normalized=sql;
 vector<duckpgq_peg::MatcherToken> tokens;
 duckpgq_peg::ParserTokenizer tokenizer(normalized,tokens);
 tokenizer.TokenizeInput();
 vector<unique_ptr<SQLStatement>> result;
 idx_t cursor=0;
 while(cursor<tokens.size()){
  auto statement=factory->TransformTopLevelStatement(tokens,options,matcher->TopLevelStatementMatcher(),cursor);
  if(statement)result.push_back(std::move(statement));
 }
 return result;
}

void TestPegTextEndToEnd(){
 // statements covered by the gated blocks; each is parsed by both parsers
 const vector<string> corpus={
  "SELECT 1","SELECT 1 + 2 * 3 AS x","SELECT a, b FROM t WHERE a > 1 ORDER BY b LIMIT 5",
  "SELECT count(*) FROM t GROUP BY a HAVING count(*) > 1","SELECT * FROM t1 JOIN t2 ON t1.a = t2.a",
  "WITH c AS (SELECT 1 AS a) SELECT a FROM c","SELECT CAST(1 AS VARCHAR), 'a' || 'b'",
  "CREATE TABLE t(a INTEGER, b VARCHAR DEFAULT 'x')","CREATE TABLE t2 AS SELECT 1 AS a","CREATE VIEW v AS SELECT 1",
  "CREATE INDEX i ON t(a)","DROP TABLE IF EXISTS t","INSERT INTO t VALUES (1, 'a')","DELETE FROM t WHERE a = 1","UPDATE t SET a = 2 WHERE a = 1",
  "COPY t TO 'x.csv'","EXPLAIN SELECT 1","EXPLAIN ANALYZE SELECT 1","PREPARE p AS SELECT 1","EXECUTE p","DEALLOCATE p",
  "CALL range(3)","DESCRIBE t","SET threads = 2","SET VARIABLE v = 7","RESET threads","PRAGMA table_info('t')","PRAGMA threads = 2",
  "BEGIN TRANSACTION","COMMIT","ROLLBACK","VACUUM","VACUUM ANALYZE t","ANALYZE t","USE memory","LOAD httpfs","INSTALL spatial",
  "ATTACH ':memory:' AS m","DETACH DATABASE m","EXPORT DATABASE 'x'","IMPORT DATABASE 'x'","COMMENT ON TABLE t IS 'c'"};
 idx_t same=0,text_diff=0,type_diff=0,peg_error=0,canonical_error=0;
 for(auto &sql:corpus){
  vector<unique_ptr<SQLStatement>> peg;string peg_message;bool peg_failed=false;
  try{peg=PegParse(sql);}catch(const std::exception &ex){peg_failed=true;peg_message=ex.what();}
  unique_ptr<SQLStatement> canonical;string canonical_message;bool canonical_failed=false;
  try{Parser p;p.ParseQuery(sql);canonical=std::move(p.statements[0]);}catch(const std::exception &ex){canonical_failed=true;canonical_message=ex.what();}
  if(canonical_failed){++canonical_error;std::cerr<<"CANONICAL_ERROR "<<sql<<" : "<<canonical_message.substr(0,120)<<std::endl;continue;}
  if(peg_failed||peg.size()!=1){++peg_error;std::cerr<<"PEG_ERROR "<<sql<<" : "<<(peg_failed?peg_message.substr(0,160):"statement count "+std::to_string(peg.size()))<<std::endl;continue;}
  if(peg[0]->type!=canonical->type){++type_diff;std::cerr<<"TYPE_MISMATCH "<<sql<<std::endl;continue;}
  if(NoSpace(peg[0]->ToString())!=NoSpace(canonical->ToString())){++text_diff;std::cerr<<"TEXT_DIFF "<<sql<<" | peg "<<peg[0]->ToString()<<" | canonical "<<canonical->ToString()<<std::endl;continue;}
  ++same;
 }

 // negative controls: the comparison must be able to fail, and the PEG statements must be non-trivial
 {
  auto one=PegParse("SELECT 1");auto two=PegParse("SELECT 2");
  Parser p;p.ParseQuery("SELECT 2");
  if(one.size()!=1||two.size()!=1||one[0]->ToString().empty())throw std::runtime_error("e2e control: PEG statement empty");
  if(NoSpace(one[0]->ToString())==NoSpace(p.statements[0]->ToString()))throw std::runtime_error("e2e control: comparison cannot fail");
  if(NoSpace(two[0]->ToString())!=NoSpace(p.statements[0]->ToString()))throw std::runtime_error("e2e control: SELECT 2 differs");
 }
 // DuckPGQ's own syntax, from text: no canonical parser reference exists, so expected structures are explicit
 {
  auto create=PegParse("CREATE PROPERTY GRAPH g VERTEX TABLES (Person) EDGE TABLES (Knows SOURCE KEY (a) REFERENCES Person (id) DESTINATION KEY (b) REFERENCES Person (id))");
  if(create.size()!=1||create[0]->type!=StatementType::CREATE_STATEMENT)throw std::runtime_error("e2e pgq: CREATE PROPERTY GRAPH statement");
  auto &info=dynamic_cast<CreatePropertyGraphInfo&>(*create[0]->Cast<CreateStatement>().info);
  if(info.property_graph_name!="g"||info.vertex_tables.size()!=1||info.edge_tables.size()!=1||info.edge_tables[0]->source_pg_table!=info.vertex_tables[0]||
     info.edge_tables[0]->destination_pg_table!=info.vertex_tables[0]||info.label_map.size()!=2)throw std::runtime_error("e2e pgq: CREATE PROPERTY GRAPH structure");
  auto drop=PegParse("DROP PROPERTY GRAPH IF EXISTS g");
  if(drop.size()!=1||drop[0]->type!=StatementType::DROP_STATEMENT||!dynamic_cast<DropPropertyGraphInfo&>(*drop[0]->Cast<DropStatement>().info).missing_ok)throw std::runtime_error("e2e pgq: DROP PROPERTY GRAPH");
  auto select=PegParse("SELECT * FROM GRAPH_TABLE (g MATCH (a:Person)-[k:Knows]->(b:Person) COLUMNS (a.id, b.id))");
  if(select.size()!=1||select[0]->type!=StatementType::SELECT_STATEMENT)throw std::runtime_error("e2e pgq: GRAPH_TABLE statement");
  auto &node=select[0]->Cast<SelectStatement>().node->Cast<SelectNode>();
  auto ref=dynamic_cast<TableFunctionRef*>(node.from_table.get());
  if(!ref)throw std::runtime_error("e2e pgq: GRAPH_TABLE is a table function ref");
  auto &call=ref->function->Cast<FunctionExpression>();
  if(call.children.size()!=1)throw std::runtime_error("e2e pgq: duckpgq_match arity");
  auto &match=call.children[0]->Cast<MatchExpression>();
  if(match.pg_name!="g"||match.path_patterns.size()!=1||match.path_patterns[0]->path_elements.size()!=3||match.column_list.size()!=2)throw std::runtime_error("e2e pgq: GRAPH_TABLE structure");
  std::cout<<"PASS PEG text pipeline for graph syntax: CREATE/DROP PROPERTY GRAPH and GRAPH_TABLE parsed from text"<<std::endl;
 }
 if(same!=corpus.size())throw std::runtime_error("e2e corpus: some statements differ between the PEG pipeline and the canonical parser (see stderr)");
 std::cout<<"E2E corpus "<<corpus.size()<<": same "<<same<<", text diff "<<text_diff<<", type mismatch "<<type_diff<<", peg error "<<peg_error<<", canonical-only error "<<canonical_error<<std::endl;
}
#endif
