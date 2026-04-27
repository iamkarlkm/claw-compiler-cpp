// Minimal JSON stub for compilation
#ifndef JSON_JSON_H
#define JSON_JSON_H

#include <string>
#include <vector>
#include <map>

namespace Json {
    class Value {
    public:
        Value() = default;
        Value(const Value&) = default;
        
        std::string asString() const { return ""; }
        int asInt() const { return 0; }
        double asDouble() const { return 0.0; }
        bool asBool() const { return false; }
        
        Value& operator[](const std::string& key) { return *this; }
        Value& operator[](int index) { return *this; }
        
        bool isNull() const { return true; }
        bool isString() const { return false; }
        bool isInt() const { return false; }
        bool isDouble() const { return false; }
        bool isBool() const { return false; }
        bool isArray() const { return false; }
        bool isObject() const { return false; }
        
        int size() const { return 0; }
        
        typedef std::string::iterator iterator;
        iterator begin() { return iterator(); }
        iterator end() { return iterator(); }
    };
    
    class Reader {
    public:
        bool parse(const std::string& str, Value& root) { return true; }
    };
    
    class FastWriter {
    public:
        std::string write(const Value& root) { return "{}"; }
    };
    
    class StyledWriter {
    public:
        std::string write(const Value& root) { return "{}"; }
    };
}

#endif // JSON_JSON_H
