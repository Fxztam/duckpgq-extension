#include "duckdb/common/serializer/serializer.hpp"
#include "duckdb/common/serializer/deserializer.hpp"
#include "duckpgq/parser/path_element.hpp"
#include "duckpgq/parser/path_reference.hpp"

namespace duckdb {

bool PathElement::Equals(const PathReference *other_p) const {
	if (!PathReference::Equals(other_p)) {
		return false;
	}
	auto other = dynamic_cast<const PathElement *>(other_p);
	if (!other) {
		return false;
	}
	if (match_type != other->match_type) {
		return false;
	}
	if (label != other->label) {
		return false;
	}
	if (variable_binding != other->variable_binding) {
		return false;
	}

	return true;
}

void PathElement::Serialize(Serializer &serializer) const {
	PathReference::Serialize(serializer);
	serializer.WriteProperty(200, "match_type", uint8_t(match_type));
	serializer.WriteProperty(201, "label", label);
	serializer.WriteProperty(202, "variable_binding", variable_binding);
}

unique_ptr<PathReference> PathElement::Deserialize(Deserializer &deserializer) {
	auto result = make_uniq<PathElement>(PGQPathReferenceType::PATH_ELEMENT);
	uint8_t match_type = uint8_t(PGQMatchType::MATCH_VERTEX);
	deserializer.ReadProperty(200, "match_type", match_type);
	if (match_type > uint8_t(PGQMatchType::MATCH_EDGE_LEFT_RIGHT)) {
		throw SerializationException("Invalid PGQ match type: %u", unsigned(match_type));
	}
	result->match_type = PGQMatchType(match_type);
	deserializer.ReadProperty(201, "label", result->label);
	deserializer.ReadProperty(202, "variable_binding", result->variable_binding);
	return std::move(result);
}

unique_ptr<PathReference> PathElement::Copy() {
	auto result = make_uniq<PathElement>(PGQPathReferenceType::PATH_ELEMENT);
	result->path_reference_type = path_reference_type;
	result->match_type = match_type;
	result->label = label;
	result->variable_binding = variable_binding;
	return std::move(result);
}
string PathElement::ToString() const {
	string result = "";
	switch (match_type) {
	case PGQMatchType::MATCH_VERTEX:
		result += "(" + variable_binding + ":" + label + ")";
		break;
	case PGQMatchType::MATCH_EDGE_ANY:
		result += "-[" + variable_binding + ":" + label + "]-";
		break;
	case PGQMatchType::MATCH_EDGE_LEFT:
		result += "<-[" + variable_binding + ":" + label + "]-";
		break;
	case PGQMatchType::MATCH_EDGE_RIGHT:
		result += "-[" + variable_binding + ":" + label + "]->";
		break;
	case PGQMatchType::MATCH_EDGE_LEFT_RIGHT:
		result += "<-[" + variable_binding + ":" + label + "]->";
		break;
	}
	return result;
}

} // namespace duckdb
