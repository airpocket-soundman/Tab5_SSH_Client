#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

class PythonRunner {
public:
    using Output = void (*)(const String& line);

    void reset();
    bool runLine(const String& line, Output output);
    bool runFile(fs::FS& fs, const String& path, Output output);
    String lastError() const { return _lastError; }

private:
    enum class ValueKind : uint8_t {
        None,
        Integer,
        Text,
        Pin,
    };

    struct Value {
        ValueKind kind{ValueKind::None};
        int integer{0};
        String text;
    };

    struct Variable {
        String name;
        Value value;
    };

    bool runLines(const std::vector<String>& lines, size_t begin, size_t end, Output output);
    bool executeStatement(String line, Output output);
    bool executeForBlock(const std::vector<String>& lines, size_t& index, size_t end, int indent, String line, Output output);
    bool executeAssignment(const String& name, String expr, Output output);
    bool eval(String expr, Value& value);
    bool evalInteger(String expr, int& value);
    bool evalPin(String expr, Value& value);
    bool callPrint(String args, Output output);
    bool callPinMethod(const String& line, Output output);
    bool callFunction(String line, Output output);
    Variable* findVariable(const String& name);
    void setVariable(const String& name, const Value& value);
    void setError(const String& text);

    std::vector<Variable> _variables;
    String _lastError;
};
