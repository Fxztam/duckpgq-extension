cmake_minimum_required(VERSION 3.20)
project(duckpgq_canonical_adapter_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
option(PGQ_CHECK_TRANSFORMERS "Compile PEG-facing Connect/Trigger/USE and core metadata translation units" OFF)
if(NOT DUCKDB_INCLUDE OR NOT DUCKDB_IMPORT_LIBRARY)
  message(FATAL_ERROR "Explicit canonical DuckDB headers and import library required")
endif()
if(PGQ_CHECK_TRANSFORMERS)
  set(PGQ_PEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/duckdb_peg_parser/upstream")
  add_library(pgq_transformer_compile OBJECT
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_select.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/peg_transformer.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_pivot.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_pivot_table.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_join.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_grouping_cte.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_window_case.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_window_function.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_function.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_expression_basics.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_array_subquery.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_struct_comparison.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_regex_between.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_arithmetic.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_collate_parameters.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_unary_numbers.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_aggregate_star.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_child_operators.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_subqueries.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_replace_entry.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_interval.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_comprehension.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_comment.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_common.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_copy.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_index.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_macro.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_secret.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_table.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_view.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_describe.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_drop.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_execute.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_prepare.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_deallocate.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_explain.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_call.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_detach.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_export.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_load.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_pragma.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_set.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_transaction.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_vacuum.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_pgq.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_expression.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/peg_transformer_factory.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_generated.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_generated_trampoline.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_window_named.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_parse_helpers.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_select_modifiers.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_constant.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_connect.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_use.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_alter.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_delete.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_insert.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_merge_into.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_update.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_analyze.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_attach.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_checkpoint.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_generic_copy_option.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_type.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_schema.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_sequence.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_create_trigger.cpp)
  target_include_directories(pgq_transformer_compile PRIVATE ../../src/include ${DUCKDB_INCLUDE} ${PGQ_PEG_ROOT}/src/include
    ${DUCKDB_INCLUDE}/../../third_party/utf8proc/include
    ${DUCKDB_INCLUDE}/../../third_party/fmt/include)
  set_property(TARGET pgq_transformer_compile PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_transformer_compile PRIVATE DUCKDB_STATIC_BUILD)
  # Explicit in-progress gate; not evidence of completed SELECT/Expression parity.
  add_library(pgq_select_expression_compile OBJECT EXCLUDE_FROM_ALL
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_select.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/transform_expression.cpp)
  target_include_directories(pgq_select_expression_compile PRIVATE ../../src/include ${DUCKDB_INCLUDE} ${PGQ_PEG_ROOT}/src/include
    ${DUCKDB_INCLUDE}/../../third_party/utf8proc/include
    ${DUCKDB_INCLUDE}/../../third_party/fmt/include)
  set_property(TARGET pgq_select_expression_compile PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_select_expression_compile PRIVATE DUCKDB_STATIC_BUILD)
  file(GLOB PGQ_BINDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/functions/function_data/*_function_data.cpp")
  # In-progress gate: the PEG matcher/tokenizer runtime needed for text-to-AST end-to-end tests.
  add_library(pgq_peg_runtime_compile OBJECT EXCLUDE_FROM_ALL
    ${PGQ_PEG_ROOT}/src/parser/peg/matcher.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/peg_parser.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/keyword_helper.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/keyword_map.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/tokenizer/base_tokenizer.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/tokenizer/parser_tokenizer.cpp)
  target_include_directories(pgq_peg_runtime_compile PRIVATE ../../src/include ${DUCKDB_INCLUDE} ${PGQ_PEG_ROOT}/src/include
    ${DUCKDB_INCLUDE}/../../third_party/utf8proc/include
    ${DUCKDB_INCLUDE}/../../third_party/fmt/include)
  set_property(TARGET pgq_peg_runtime_compile PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_peg_runtime_compile PRIVATE DUCKDB_STATIC_BUILD)
  add_library(pgq_binders_compile OBJECT ${PGQ_BINDER_SOURCES})
  target_include_directories(pgq_binders_compile PRIVATE ../../src/include ${DUCKDB_INCLUDE})
  set_property(TARGET pgq_binders_compile PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_binders_compile PRIVATE DUCKDB_STATIC_BUILD)
  add_library(pgq_metadata_compile OBJECT
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/peg_transformer.cpp
    ${PGQ_PEG_ROOT}/src/parser/peg/transformer/peg_transformer_factory.cpp)
  target_include_directories(pgq_metadata_compile PRIVATE ../../src/include ${DUCKDB_INCLUDE} ${PGQ_PEG_ROOT}/src/include
    ${DUCKDB_INCLUDE}/../../third_party/utf8proc/include
    ${DUCKDB_INCLUDE}/../../third_party/fmt/include)
  set_property(TARGET pgq_metadata_compile PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_metadata_compile PRIVATE DUCKDB_STATIC_BUILD)
endif()
add_executable(pgq_adapter_test function_access.cpp expression_access.cpp bind_access.cpp name_metadata.cpp serialization.cpp create_ast.cpp dml_ast.cpp alter_ast.cpp pivot_ast.cpp join_ast.cpp grouping_cte_ast.cpp window_case_ast.cpp window_function_ast.cpp array_subquery_ast.cpp
  struct_comparison_ast.cpp
  regex_between_ast.cpp
  arithmetic_ast.cpp
  collate_parameters_ast.cpp
  unary_numbers_ast.cpp
  aggregate_star_ast.cpp
  child_operators_ast.cpp
  subqueries_ast.cpp
  replace_entry_ast.cpp
  interval_ast.cpp
  comprehension_ast.cpp
  comment_ast.cpp
  common_ast.cpp
  copy_ast.cpp
  index_ast.cpp
  macro_ast.cpp
  secret_ast.cpp
  table_ast.cpp
  table_matrix.cpp
  view_ast.cpp
  describe_ast.cpp
  drop_ast.cpp
  execute_ast.cpp
  prepare_ast.cpp
  deallocate_ast.cpp
  explain_ast.cpp
  call_ast.cpp
  detach_ast.cpp
  export_ast.cpp
  load_ast.cpp
  pragma_ast.cpp
  set_ast.cpp
  transaction_ast.cpp
  vacuum_ast.cpp
  pgq_graph_ast.cpp
  peg_text_e2e.cpp
  ../../src/parser/tableref/matchref.cpp ../../src/parser/path_pattern.cpp
  ../../src/parser/property_graph_table.cpp
  ../../src/parser/parsed_data/create_property_graph_info.cpp ../../src/parser/parsed_data/drop_property_graph_info.cpp
  path_serialization.cpp ../../src/parser/path_element.cpp
  ../../src/parser/path_reference.cpp ../../src/parser/subpath_element.cpp)
target_include_directories(pgq_adapter_test PRIVATE ../../src/include ${DUCKDB_INCLUDE})
if(PGQ_CHECK_TRANSFORMERS)
  target_sources(pgq_adapter_test PRIVATE $<TARGET_OBJECTS:pgq_transformer_compile>)
  target_sources(pgq_adapter_test PRIVATE $<TARGET_OBJECTS:pgq_peg_runtime_compile>)
  target_compile_definitions(pgq_adapter_test PRIVATE PGQ_TRANSFORMER_TEST)
  target_include_directories(pgq_adapter_test PRIVATE ${PGQ_PEG_ROOT}/src/include
    ${DUCKDB_INCLUDE}/../../third_party/utf8proc/include
    ${DUCKDB_INCLUDE}/../../third_party/fmt/include)
endif()
if(DUCKDB_STATIC_LIBRARY_DIR)
  set_property(TARGET pgq_adapter_test PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_compile_definitions(pgq_adapter_test PRIVATE DUCKDB_STATIC_BUILD)
  file(GLOB DUCKDB_TEST_STATIC_LIBRARIES "${DUCKDB_STATIC_LIBRARY_DIR}/*.lib")
  list(FILTER DUCKDB_TEST_STATIC_LIBRARIES EXCLUDE REGEX "/duckdb\\.lib$")
  list(FILTER DUCKDB_TEST_STATIC_LIBRARIES EXCLUDE REGEX "/dummy_static_extension_loader\\.lib$")
  target_link_libraries(pgq_adapter_test PRIVATE ${DUCKDB_TEST_STATIC_LIBRARIES} ws2_32 rstrtmgr bcrypt)
else()
  target_link_libraries(pgq_adapter_test PRIVATE ${DUCKDB_IMPORT_LIBRARY})
endif()
