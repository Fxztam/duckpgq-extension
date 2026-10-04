#include "duckpgq/parser/property_graph_table.hpp"
#include "duckpgq/parser/parsed_data/create_property_graph_info.hpp"
#include "duckpgq/compat/identifier_serialization.hpp"
#include "duckdb/common/serializer/binary_serializer.hpp"
#include "duckdb/common/serializer/binary_deserializer.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include <stdexcept>
#include <cstring>
#include <iostream>
using namespace duckdb;
static void CheckSerialization(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static shared_ptr<PropertyGraphTable> Roundtrip(const PropertyGraphTable &table) {
    MemoryStream bytes;
    BinarySerializer::Serialize(table, bytes);
    MemoryStream input(bytes.GetData(), bytes.GetPosition());
    BinaryDeserializer reader(input);
    reader.Begin();
    auto result = PropertyGraphTable::Deserialize(reader);
    reader.End();
    CheckSerialization(input.GetPosition() == bytes.GetPosition(), "unconsumed table bytes");
    return result;
}
static void CheckTable(const PropertyGraphTable &expected, const PropertyGraphTable &actual) {
    CheckSerialization(expected.discriminator.GetIdentifierName() == actual.discriminator.GetIdentifierName(),
        "discriminator lost or spelling changed");
    // Compare exact spelling; Identifier equality alone is case insensitive.
    MemoryStream a, b;
    BinarySerializer::Serialize(expected, a);
    BinarySerializer::Serialize(actual, b);
    CheckSerialization(a.GetPosition() == b.GetPosition() &&
        std::memcmp(a.GetData(), b.GetData(), a.GetPosition()) == 0, "table roundtrip bytes changed");
}
void TestSerialization() {
    for (const string spelling : vector<string>{"", "MixedCase", "a.b", "a\"b", "Gr\xc3\xb6\xc3\x9f\x65", "\xf0\x9f\x8c\x8d"}) {
        MemoryStream actual, expected;
        BinarySerializer a(actual), e(expected);
        a.Begin(); e.Begin();
        duckpgq_compat::WriteIdentifierProperty(a, 100, "scalar", Identifier(spelling));
        duckpgq_compat::WriteIdentifierProperty(a, 101, "list", StringsToIdentifiers({spelling, ""}));
        e.WriteProperty(100, "scalar", spelling);
        e.WriteProperty(101, "list", vector<string>{spelling, ""});
        a.End(); e.End();
        CheckSerialization(actual.GetPosition() == expected.GetPosition() &&
            std::memcmp(actual.GetData(), expected.GetData(), actual.GetPosition()) == 0,
            "Identifier wire format differs from plain strings");
        MemoryStream input(expected.GetData(), expected.GetPosition());
        BinaryDeserializer reader(input);
        reader.Begin();
        Identifier scalar; vector<Identifier> list;
        duckpgq_compat::ReadIdentifierProperty(reader, 100, "scalar", scalar);
        duckpgq_compat::ReadIdentifierProperty(reader, 101, "list", list);
        reader.End();
        CheckSerialization(scalar.GetIdentifierName() == spelling &&
            IdentifiersToStrings(list) == vector<string>({spelling, ""}), "identifier spelling lost");
    }
    PropertyGraphTable vertex("Mixed.Table", {"Id", "Name"}, {"Person"}, "Catalog", "schema");
    vertex.is_vertex_table = true;
    vertex.main_label = Identifier("Person");
    vertex.table_name_alias = Identifier("Alias");
    vertex.column_aliases = StringsToIdentifiers({"ID", "Display"});
    vertex.except_columns = StringsToIdentifiers({"hidden"});
    vertex.all_columns = true;
    CheckTable(vertex, *Roundtrip(vertex));
    // Independent vertex wire oracle: fixed tags and plain string fields.
    MemoryStream vertex_actual, vertex_expected;
    BinarySerializer::Serialize(vertex, vertex_actual);
    auto WriteVertexFixture = [](MemoryStream &bytes, const string *discriminator) {
    BinarySerializer oracle(bytes);
    oracle.Begin();
    oracle.WriteProperty(100, "catalog_name", string("Catalog"));
    oracle.WriteProperty(101, "schema_name", string("schema"));
    oracle.WriteProperty(102, "table_name", string("Mixed.Table"));
    oracle.WriteProperty(103, "table_name_alias", string("Alias"));
    oracle.WriteProperty(104, "column_names", vector<string>{"Id", "Name"});
    oracle.WriteProperty(105, "column_aliases", vector<string>{"ID", "Display"});
    oracle.WriteProperty(106, "except_columns", vector<string>{"hidden"});
    oracle.WriteProperty(107, "sub_labels", vector<string>{"Person"});
    oracle.WriteProperty(108, "main_label", string("Person"));
    oracle.WriteProperty(109, "is_vertex_table", true);
    oracle.WriteProperty(110, "all_columns", true);
    oracle.WriteProperty(111, "no_columns", false);
    if (discriminator) oracle.WriteProperty(124, "discriminator", *discriminator);
    oracle.End();
    };
    WriteVertexFixture(vertex_expected, nullptr);
    CheckSerialization(vertex_actual.GetPosition() == vertex_expected.GetPosition() &&
        std::memcmp(vertex_actual.GetData(), vertex_expected.GetData(), vertex_actual.GetPosition()) == 0,
        "vertex wire fields differ from fixed contract");
    // Old bytes, produced independently without field 124, must remain readable.
    MemoryStream legacy_input(vertex_expected.GetData(), vertex_expected.GetPosition());
    BinaryDeserializer legacy_reader(legacy_input);
    legacy_reader.Begin();
    auto legacy_vertex = PropertyGraphTable::Deserialize(legacy_reader);
    legacy_reader.End();
    CheckSerialization(legacy_vertex->discriminator.empty(), "missing discriminator must default to empty");
    for (const string spelling : vector<string>{"Kind", ""}) {
        MemoryStream fixture;
        WriteVertexFixture(fixture, &spelling);
        MemoryStream input(fixture.GetData(), fixture.GetPosition());
        BinaryDeserializer reader(input);
        reader.Begin();
        auto decoded = PropertyGraphTable::Deserialize(reader);
        reader.End();
        CheckSerialization(decoded->discriminator.GetIdentifierName() == spelling,
            "independent field-124 fixture not read correctly");
        vertex.discriminator = Identifier(spelling);
        MemoryStream emitted;
        SerializationOptions options;
        options.serialize_default_values = true;
        BinarySerializer::Serialize(vertex, emitted, options);
        CheckSerialization(emitted.GetPosition() == fixture.GetPosition() &&
            std::memcmp(emitted.GetData(), fixture.GetData(), emitted.GetPosition()) == 0,
            "field-124 wire contract changed");
    }
    for (const string spelling : vector<string>{"Kind", "Mixed.Case", "a\"b",
             "Gr\xc3\xb6\xc3\x9f\x65", "\xf0\x9f\x8c\x8d", ""}) {
        vertex.discriminator = Identifier(spelling);
        CheckTable(vertex, *Roundtrip(vertex));
    }
    vertex.discriminator = Identifier("VertexKind");
    PropertyGraphTable edge("Links", {"Weight"}, {"Knows"});
    edge.source_pk = StringsToIdentifiers({"Id"});
    edge.source_fk = StringsToIdentifiers({"Source"});
    edge.destination_pk = StringsToIdentifiers({"Id"});
    edge.destination_fk = StringsToIdentifiers({"Target"});
    edge.source_catalog = Identifier("SourceCatalog");
    edge.source_schema = Identifier("SourceSchema");
    edge.source_reference = Identifier("SourceRef");
    edge.destination_catalog = Identifier("TargetCatalog");
    edge.destination_schema = Identifier("TargetSchema");
    edge.destination_reference = Identifier("TargetRef");
    edge.discriminator = Identifier("EdgeKind");
    edge.source_pg_table = make_shared_ptr<PropertyGraphTable>(vertex);
    edge.destination_pg_table = make_shared_ptr<PropertyGraphTable>(vertex);
    CheckTable(edge, *Roundtrip(edge));
    CreatePropertyGraphInfo graph("MyGraph");
    graph.vertex_tables.push_back(make_shared_ptr<PropertyGraphTable>(vertex));
    graph.edge_tables.push_back(make_shared_ptr<PropertyGraphTable>(edge));
    graph.label_map["Person"] = graph.vertex_tables[0];
    graph.label_map["Knows"] = graph.edge_tables[0];
    MemoryStream graph_bytes;
    BinarySerializer::Serialize(graph, graph_bytes);
    MemoryStream graph_input(graph_bytes.GetData(), graph_bytes.GetPosition());
    BinaryDeserializer graph_reader(graph_input);
    graph_reader.Begin();
    auto restored = CreatePropertyGraphInfo::Deserialize(graph_reader);
    graph_reader.End();
    auto &restored_graph = static_cast<CreatePropertyGraphInfo &>(*restored);
    CheckSerialization(restored_graph.property_graph_name == "MyGraph" &&
        restored_graph.vertex_tables.size() == 1 && restored_graph.edge_tables.size() == 1,
        "graph table lists lost");
    CheckTable(vertex, *restored_graph.vertex_tables[0]);
    CheckTable(edge, *restored_graph.edge_tables[0]);
    CheckTable(vertex, *restored_graph.label_map.at("person"));
    CheckTable(edge, *restored_graph.label_map.at("knows"));
    for (idx_t length = 0; length < graph_bytes.GetPosition(); length++) {
        bool rejected = false;
        try {
            MemoryStream short_input(graph_bytes.GetData(), length);
            BinaryDeserializer short_reader(short_input);
            short_reader.Begin();
            auto ignored = CreatePropertyGraphInfo::Deserialize(short_reader);
            short_reader.End();
        } catch (const Exception &) { rejected = true; }
        CheckSerialization(rejected, "truncated graph accepted");
    }
    CreatePropertyGraphInfo empty_graph("Empty");
    MemoryStream empty_bytes;
    BinarySerializer::Serialize(empty_graph, empty_bytes);
    MemoryStream empty_input(empty_bytes.GetData(), empty_bytes.GetPosition());
    BinaryDeserializer empty_reader(empty_input);
    empty_reader.Begin();
    auto empty_restored = CreatePropertyGraphInfo::Deserialize(empty_reader);
    empty_reader.End();
    auto &empty = static_cast<CreatePropertyGraphInfo &>(*empty_restored);
    CheckSerialization(empty.vertex_tables.empty() && empty.edge_tables.empty() && empty.label_map.empty(),
        "empty graph lists must stay empty");
    edge.source_pg_table.reset();
    edge.destination_pg_table.reset();
    CheckTable(edge, *Roundtrip(edge));
    std::cout << "PASS PGQ serialization: string wire parity, vertex/edge graphs, empty graphs, "
              << graph_bytes.GetPosition() << " rejected truncation boundaries\n";
}
