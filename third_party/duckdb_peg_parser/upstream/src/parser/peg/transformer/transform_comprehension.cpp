#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"
#include "duckpgq/compat/name_metadata.hpp"
#include "duckdb/parser/expression/lambda_expression.hpp"
namespace duckdb {
namespace duckpgq_peg {
unique_ptr<ParsedExpression> PEGTransformerFactory::TransformListComprehensionExpression(
    PEGTransformer &transformer, unique_ptr<ParsedExpression> expression, const vector<Identifier> &col_id_or_string,
    unique_ptr<ParsedExpression> expression_1, optional<unique_ptr<ParsedExpression>> list_comprehension_filter) {
	auto result_expr = std::move(expression);
	auto in_expr = std::move(expression_1);
	vector<string> lambda_columns;
	for (auto &col : col_id_or_string) {
		lambda_columns.push_back(col.GetIdentifierName());
	}

	if (!list_comprehension_filter || !*list_comprehension_filter) {
		auto lambda_expression = make_uniq<LambdaExpression>(lambda_columns, std::move(result_expr));
		vector<unique_ptr<ParsedExpression>> apply_children;
		apply_children.push_back(std::move(in_expr));
		apply_children.push_back(std::move(lambda_expression));

		return make_uniq<FunctionExpression>("list_apply", std::move(apply_children));
	}

	auto filter_expr = std::move(*list_comprehension_filter);

	// STAGE 1: list_apply(in_expr, x -> struct_pack(filter := ..., result := ...))
	vector<FunctionArgument> struct_children;
	struct_children.emplace_back("filter", std::move(filter_expr));
	struct_children.emplace_back("result", std::move(result_expr));
	auto struct_pack = BuildFunctionExpression(duckpgq_compat::MakeQualifiedName(Identifier("struct_pack")), std::move(struct_children));

	auto stage1_lambda = make_uniq<LambdaExpression>(lambda_columns, std::move(struct_pack));
	vector<unique_ptr<ParsedExpression>> stage1_apply_args;
	stage1_apply_args.push_back(std::move(in_expr));
	stage1_apply_args.push_back(std::move(stage1_lambda));
	auto stage1_apply = make_uniq<FunctionExpression>("list_apply", std::move(stage1_apply_args));

	// STAGE 2: list_filter(stage1, elem -> struct_extract(elem, 'filter'))
	auto elem_ref_filter = make_uniq<ColumnRefExpression>("elem");
	auto filter_const = make_uniq<ConstantExpression>(Value("filter"));
	vector<unique_ptr<ParsedExpression>> extract_filter_args;
	extract_filter_args.push_back(std::move(elem_ref_filter));
	extract_filter_args.push_back(std::move(filter_const));
	auto filter_extract = make_uniq<FunctionExpression>("struct_extract", std::move(extract_filter_args));

	auto stage2_lambda = make_uniq<LambdaExpression>(vector<string> {"elem"}, std::move(filter_extract));
	vector<unique_ptr<ParsedExpression>> stage2_filter_args;
	stage2_filter_args.push_back(std::move(stage1_apply));
	stage2_filter_args.push_back(std::move(stage2_lambda));
	auto stage2_filter = make_uniq<FunctionExpression>("list_filter", std::move(stage2_filter_args));

	// STAGE 3: list_apply(stage2, elem -> struct_extract(elem, 'result'))
	auto elem_ref_result = make_uniq<ColumnRefExpression>("elem");
	auto result_const = make_uniq<ConstantExpression>(Value("result"));
	vector<unique_ptr<ParsedExpression>> extract_result_args;
	extract_result_args.push_back(std::move(elem_ref_result));
	extract_result_args.push_back(std::move(result_const));
	auto result_extract = make_uniq<FunctionExpression>("struct_extract", std::move(extract_result_args));

	auto stage3_lambda = make_uniq<LambdaExpression>(vector<string> {"elem"}, std::move(result_extract));
	vector<unique_ptr<ParsedExpression>> stage3_apply_args;
	stage3_apply_args.push_back(std::move(stage2_filter));
	stage3_apply_args.push_back(std::move(stage3_lambda));

	return make_uniq<FunctionExpression>("list_apply", std::move(stage3_apply_args));
}

unique_ptr<ParsedExpression>
PEGTransformerFactory::TransformListComprehensionFilter(PEGTransformer &transformer,
                                                        unique_ptr<ParsedExpression> expression) {
	return expression;
}
} // namespace duckpgq_peg
} // namespace duckdb
