#include "json/json_serialization.h"
#include <iostream>
#include <cassert>

using namespace claw::json;

void test_null() {
    std::cout << "Testing null... ";
    JsonValue v = nullptr;
    assert(v.is_null());
    assert(to_json(v) == "null");
    std::cout << "PASS\n";
}

void test_bool() {
    std::cout << "Testing bool... ";
    JsonValue t = true;
    JsonValue f = false;
    assert(t.is_bool() && t.as_bool() == true);
    assert(f.is_bool() && f.as_bool() == false);
    assert(to_json(t) == "true");
    assert(to_json(f) == "false");
    std::cout << "PASS\n";
}

void test_number() {
    std::cout << "Testing number... ";
    JsonValue n = 42.5;
    assert(n.is_number());
    assert(n.as_number() == 42.5);
    JsonValue neg = -123;
    assert(neg.as_number() == -123);
    std::cout << "PASS\n";
}

void test_string() {
    std::cout << "Testing string... ";
    JsonValue s = std::string("hello");
    assert(s.is_string());
    assert(s.as_string() == "hello");
    assert(to_json(s) == "\"hello\"");
    
    // Escaping
    JsonValue escaped = "line1\nline2\ttab";
    std::string result = to_json(escaped);
    assert(result.find("\\n") != std::string::npos);
    std::cout << "PASS\n";
}

void test_array() {
    std::cout << "Testing array... ";
    JsonArray arr = {1, 2, 3};
    JsonValue v = arr;
    assert(v.is_array());
    assert(v.size() == 3);
    assert(v[0].as_number() == 1);
    assert(v[2].as_number() == 3);
    
    // Array of mixed types
    JsonValue mixed = make_array();
    mixed.push_back(1);
    mixed.push_back("two");
    mixed.push_back(true);
    assert(mixed.size() == 3);
    std::cout << "PASS\n";
}

void test_object() {
    std::cout << "Testing object... ";
    JsonObject obj = {
        {"name", "Alice"},
        {"age", 30},
        {"active", true}
    };
    JsonValue v = obj;
    assert(v.is_object());
    assert(v["name"].as_string() == "Alice");
    assert(v["age"].as_number() == 30);
    assert(v["active"].as_bool() == true);
    std::cout << "PASS\n";
}

void test_nested() {
    std::cout << "Testing nested... ";
    JsonValue root = make_object();
    root["user"]["name"] = "Bob";
    root["user"]["age"] = 25;
    root["scores"].push_back(90);
    root["scores"].push_back(85);
    root["scores"].push_back(95);
    
    assert(root["user"]["name"].as_string() == "Bob");
    assert(root["user"]["age"].as_number() == 25);
    assert(root["scores"].size() == 3);
    assert(root["scores"][0].as_number() == 90);
    std::cout << "PASS\n";
}

void test_serialization_roundtrip() {
    std::cout << "Testing serialization roundtrip... ";
    JsonValue original = make_object();
    original["string"] = "hello";
    original["number"] = 42.5;
    original["bool"] = true;
    original["null"] = nullptr;
    original["array"] = JsonArray{1, 2, 3};
    original["object"]["nested"] = "value";
    
    std::string json_str = to_json(original, true);
    JsonValue parsed = from_json(json_str);
    
    assert(parsed["string"].as_string() == "hello");
    assert(parsed["number"].as_number() == 42.5);
    assert(parsed["bool"].as_bool() == true);
    assert(parsed["null"].is_null());
    assert(parsed["array"].size() == 3);
    assert(parsed["object"]["nested"].as_string() == "value");
    std::cout << "PASS\n";
}

void test_pretty_print() {
    std::cout << "Testing pretty print... ";
    JsonValue obj = make_object();
    obj["name"] = "Test";
    obj["items"].push_back(1);
    obj["items"].push_back(2);
    
    std::string pretty = to_json(obj, true);
    std::string compact = to_json(obj, false);
    
    // Pretty should have newlines
    assert(pretty.find('\n') != std::string::npos);
    // Compact should not
    assert(compact.find('\n') == std::string::npos);
    std::cout << "PASS\n";
}

void test_parse_primitives() {
    std::cout << "Testing parse primitives... ";
    assert(from_json("null").is_null());
    assert(from_json("true").as_bool() == true);
    assert(from_json("false").as_bool() == false);
    assert(from_json("42").as_number() == 42);
    assert(from_json("3.14").as_number() == 3.14);
    assert(from_json("\"hello\"").as_string() == "hello");
    std::cout << "PASS\n";
}

void test_parse_array() {
    std::cout << "Testing parse array... ";
    JsonValue arr = from_json("[1, 2, 3]");
    assert(arr.is_array());
    assert(arr.size() == 3);
    assert(arr[0].as_number() == 1);
    assert(arr[1].as_number() == 2);
    assert(arr[2].as_number() == 3);
    std::cout << "PASS\n";
}

void test_parse_object() {
    std::cout << "Testing parse object... ";
    JsonValue obj = from_json("{\"name\": \"Alice\", \"age\": 30}");
    assert(obj.is_object());
    assert(obj["name"].as_string() == "Alice");
    assert(obj["age"].as_number() == 30);
    std::cout << "PASS\n";
}

void test_parse_complex() {
    std::cout << "Testing parse complex... ";
    std::string json = R"({
        "users": [
            {"name": "Alice", "age": 30},
            {"name": "Bob", "age": 25}
        ],
        "count": 2,
        "success": true
    })";
    
    JsonValue v = from_json(json);
    assert(v["count"].as_number() == 2);
    assert(v["success"].as_bool() == true);
    assert(v["users"].is_array());
    assert(v["users"].size() == 2);
    assert(v["users"][0]["name"].as_string() == "Alice");
    std::cout << "PASS\n";
}

void test_utility_functions() {
    std::cout << "Testing utility functions... ";
    
    // to_json_value
    auto v1 = to_json_value(std::string("test"));
    assert(v1.as_string() == "test");
    
    auto v2 = to_json_value(42);
    assert(v2.as_number() == 42);
    
    auto v3 = to_json_value(3.14);
    assert(v3.as_number() == 3.14);
    
    auto v4 = to_json_value(std::vector<int>{1, 2, 3});
    assert(v4.is_array());
    assert(v4.size() == 3);
    
    std::cout << "PASS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw JSON Serialization Tests\n";
    std::cout << "========================================\n\n";
    
    test_null();
    test_bool();
    test_number();
    test_string();
    test_array();
    test_object();
    test_nested();
    test_serialization_roundtrip();
    test_pretty_print();
    test_parse_primitives();
    test_parse_array();
    test_parse_object();
    test_parse_complex();
    test_utility_functions();
    
    std::cout << "\n========================================\n";
    std::cout << "All tests passed!\n";
    std::cout << "========================================\n";
    
    return 0;
}
