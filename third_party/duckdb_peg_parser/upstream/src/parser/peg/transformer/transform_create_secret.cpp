#include "duckdb/parser/parsed_data/create_secret_info.hpp"
#include "duckpgq/compat/alter_access.hpp"
#include "duckpgq/third_party/duckdb_peg_parser/peg/transformer/peg_transformer.hpp"

namespace duckdb {
namespace duckpgq_peg {

static void SetSecretName(CreateSecretInfo &info, const string &name) {
#if __has_include("duckdb/common/identifier.hpp")
	info.SetSecretName(Identifier(name));
#else
	info.name = name;
#endif
}

static bool SecretNameEmpty(const CreateSecretInfo &info) {
#if __has_include("duckdb/common/identifier.hpp")
	return info.GetSecretName().empty();
#else
	return info.name.empty();
#endif
}

Value PEGTransformerFactory::GetConstantExpressionValue(unique_ptr<ParsedExpression> &expr) {
	if (expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
		return duckpgq_compat::ConstantValue(expr->Cast<ConstantExpression>());
	}
	if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
		return expr->Cast<ColumnRefExpression>().GetName();
	}
	return Value();
}

// Match the canonical secret transformer: bare identifiers are literal option
// values, except in SCOPE. Work on an owned copy, never the caller's AST.
static unique_ptr<ParsedExpression> SecretOptionExpression(const GenericCopyOption &option) {
	auto expr = option.GetFirstChildOrExpression();
	if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
		return make_uniq<ConstantExpression>(PEGTransformerFactory::GetConstantExpressionValue(expr));
	}
	return expr;
}

unique_ptr<CreateStatement> PEGTransformerFactory::TransformCreateSecretStmt(
    PEGTransformer &transformer, const optional<bool> &if_not_exists, const optional<Identifier> &secret_name,
    const optional<Identifier> &secret_storage_specifier, const vector<GenericCopyOption> &generic_copy_option_list) {
	auto result = make_uniq<CreateStatement>();
	auto on_conflict = if_not_exists ? OnCreateConflict::IGNORE_ON_CONFLICT : OnCreateConflict::ERROR_ON_CONFLICT;
	auto info = make_uniq<CreateSecretInfo>(on_conflict, SecretPersistType::DEFAULT);
	if (secret_name) {
		SetSecretName(*info, StringUtil::Lower(secret_name->GetIdentifierName()));
	}
	if (secret_storage_specifier) {
		info->storage_type = duckpgq_compat::HostName(Identifier(StringUtil::Lower(secret_storage_specifier->GetIdentifierName())));
	}
	for (const auto &option : generic_copy_option_list) {
		auto lower_name = StringUtil::Lower(option.name.GetIdentifierName());
		if (lower_name == "scope") {
			info->scope = option.GetFirstChildOrExpression();
			continue;
		}
		if (lower_name == "type") {
			info->type = SecretOptionExpression(option);
			continue;
		}
		if (lower_name == "provider") {
			info->provider = SecretOptionExpression(option);
			continue;
		}
		if (info->options.find(lower_name) != info->options.end()) {
			throw BinderException("Duplicate query param found while parsing create secret: '%s'", lower_name);
		}
		info->options.insert({lower_name, SecretOptionExpression(option)});
	}
	if (!info->type) {
		throw ParserException("Failed to create secret - secret must have a type defined");
	}
	if (SecretNameEmpty(*info)) {
		auto value = GetConstantExpressionValue(info->type);
		if (value.IsNull()) {
			throw InvalidInputException(
			    "Can not combine a non-constant expression for the secret type with a default-named secret. Either "
			    "provide an explicit secret name or use a constant expression for the secret type.");
		}
		SetSecretName(*info, "__default_" + StringUtil::Lower(value.ToString()));
	}
	result->info = std::move(info);
	return result;
}

Identifier PEGTransformerFactory::TransformSecretStorageSpecifier(PEGTransformer &transformer,
                                                                  const Identifier &identifier) {
	return identifier;
}

Identifier PEGTransformerFactory::TransformSecretName(PEGTransformer &transformer, const Identifier &col_id) {
	return Identifier(col_id);
}

} // namespace duckpgq_peg
} // namespace duckdb
