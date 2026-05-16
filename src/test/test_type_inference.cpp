// test/test_type_inference.cpp - Unit tests for TypeInference

#include <iostream>
#include <cassert>
#include "../type/type_inference.h"
#include "../type/type_system.h"

using namespace claw;
using namespace claw::type;

static void test_infer_identity() {
    std::cout << "test_infer_identity... ";

    // fn id<T>(x: T) -> T
    // id(42) => T = Int64
    std::vector<std::string> type_params = {"T"};
    std::vector<TypePtr> param_types = {
        TypeCache::instance().get_type_var("T")
    };
    std::vector<TypePtr> arg_types = {
        Type::int64()
    };

    TypeInference inference;
    auto result = inference.infer_generic_args(type_params, param_types, arg_types);

    assert(result.size() == 1);
    assert(result[0]->equals(Type::int64()));
    std::cout << "PASSED\n";
}

static void test_infer_array() {
    std::cout << "test_infer_array... ";

    // fn first<T>(arr: Array<T>) -> T
    // first([1, 2, 3]) => T = Int64
    std::vector<std::string> type_params = {"T"};
    std::vector<TypePtr> param_types = {
        TypeCache::instance().get_array(TypeCache::instance().get_type_var("T"), -1)
    };
    std::vector<TypePtr> arg_types = {
        TypeCache::instance().get_array(Type::int64(), 3)
    };

    TypeInference inference;
    auto result = inference.infer_generic_args(type_params, param_types, arg_types);

    assert(result.size() == 1);
    assert(result[0]->equals(Type::int64()));
    std::cout << "PASSED\n";
}

static void test_infer_two_params() {
    std::cout << "test_infer_two_params... ";

    // fn pair<A, B>(a: A, b: B) -> (A, B)
    // pair(42, "hello") => A = Int64, B = String
    std::vector<std::string> type_params = {"A", "B"};
    std::vector<TypePtr> param_types = {
        TypeCache::instance().get_type_var("A"),
        TypeCache::instance().get_type_var("B")
    };
    std::vector<TypePtr> arg_types = {
        Type::int64(),
        Type::string()
    };

    TypeInference inference;
    auto result = inference.infer_generic_args(type_params, param_types, arg_types);

    assert(result.size() == 2);
    assert(result[0]->equals(Type::int64()));
    assert(result[1]->equals(Type::string()));
    std::cout << "PASSED\n";
}

static void test_infer_mismatch() {
    std::cout << "test_infer_mismatch... ";

    // fn id<T>(x: T) -> T
    // id(42, "hello") => arity mismatch, should fail
    std::vector<std::string> type_params = {"T"};
    std::vector<TypePtr> param_types = {
        TypeCache::instance().get_type_var("T")
    };
    std::vector<TypePtr> arg_types = {
        Type::int64(),
        Type::string()
    };

    TypeInference inference;
    auto result = inference.infer_generic_args(type_params, param_types, arg_types);

    assert(result.empty());
    std::cout << "PASSED\n";
}

static void test_infer_no_generics() {
    std::cout << "test_infer_no_generics... ";

    // fn add(a: Int, b: Int) -> Int
    // add(1, 2) => no generic params
    std::vector<std::string> type_params;
    std::vector<TypePtr> param_types = {Type::int64(), Type::int64()};
    std::vector<TypePtr> arg_types = {Type::int64(), Type::int64()};

    TypeInference inference;
    auto result = inference.infer_generic_args(type_params, param_types, arg_types);

    assert(result.empty());
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== TypeInference Tests ===\n";

    test_infer_identity();
    test_infer_array();
    test_infer_two_params();
    test_infer_mismatch();
    test_infer_no_generics();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
