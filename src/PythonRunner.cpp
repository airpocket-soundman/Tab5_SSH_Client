#include "PythonRunner.hpp"

#include <algorithm>
#include <cctype>

namespace {
String trimCopy(String value)
{
    value.trim();
    return value;
}

int leadingIndent(const String& line)
{
    int indent = 0;
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == ' ') {
            ++indent;
        } else if (c == '\t') {
            indent += 4;
        } else {
            break;
        }
    }
    return indent;
}

bool isIdentifier(const String& text)
{
    if (!text.length()) {
        return false;
    }
    char first = text[0];
    if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_')) {
        return false;
    }
    for (size_t i = 1; i < text.length(); ++i) {
        char c = text[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

bool isQuoted(const String& text)
{
    if (text.length() < 2) {
        return false;
    }
    char first = text[0];
    char last = text[text.length() - 1];
    return (first == '"' && last == '"') || (first == '\'' && last == '\'');
}

String unquote(const String& text)
{
    String out;
    for (size_t i = 1; i + 1 < text.length(); ++i) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.length() - 1) {
            char n = text[++i];
            if (n == 'n') out += '\n';
            else if (n == 't') out += '\t';
            else out += n;
        } else {
            out += c;
        }
    }
    return out;
}

bool parseIntegerLiteral(const String& text, int& value)
{
    String s = trimCopy(text);
    if (!s.length()) {
        return false;
    }
    char* end = nullptr;
    long parsed = strtol(s.c_str(), &end, 0);
    if (!end || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::vector<String> splitArgs(const String& args)
{
    std::vector<String> parts;
    String current;
    char quote = 0;
    int depth = 0;
    for (size_t i = 0; i < args.length(); ++i) {
        char c = args[i];
        if (quote) {
            current += c;
            if (c == '\\' && i + 1 < args.length()) {
                current += args[++i];
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current += c;
        } else if (c == '(') {
            ++depth;
            current += c;
        } else if (c == ')') {
            --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            parts.push_back(trimCopy(current));
            current = "";
        } else {
            current += c;
        }
    }
    if (current.length() || args.length()) {
        parts.push_back(trimCopy(current));
    }
    return parts;
}

}

void PythonRunner::reset()
{
    _lastError = "";
    _variables.clear();
}

void PythonRunner::setError(const String& text)
{
    _lastError = text;
}

PythonRunner::Variable* PythonRunner::findVariable(const String& name)
{
    for (auto& variable : _variables) {
        if (variable.name == name) {
            return &variable;
        }
    }
    return nullptr;
}

void PythonRunner::setVariable(const String& name, const Value& value)
{
    if (auto* variable = findVariable(name)) {
        variable->value = value;
        return;
    }
    _variables.push_back({name, value});
}

bool PythonRunner::evalPin(String expr, Value& value)
{
    expr.trim();
    if (!expr.startsWith("Pin(") || !expr.endsWith(")")) {
        return false;
    }
    String args = expr.substring(4, expr.length() - 1);
    auto parts = splitArgs(args);
    if (parts.empty()) {
        setError("Pin() needs a pin number");
        return false;
    }
    int pin = 0;
    if (!evalInteger(parts[0], pin)) {
        setError("Pin() pin number must be an integer");
        return false;
    }
    if (parts.size() >= 2) {
        String mode = parts[1];
        mode.trim();
        if (mode == "Pin.OUT" || mode == "OUT" || mode == "OUTPUT" || mode == "1") {
            pinMode(pin, OUTPUT);
        } else if (mode == "Pin.IN" || mode == "IN" || mode == "INPUT" || mode == "0") {
            pinMode(pin, INPUT);
        } else if (mode == "Pin.PULL_UP" || mode == "INPUT_PULLUP") {
            pinMode(pin, INPUT_PULLUP);
        } else {
            setError(String("unknown Pin mode: ") + mode);
            return false;
        }
    }
    value.kind = ValueKind::Pin;
    value.integer = pin;
    return true;
}

bool PythonRunner::eval(String expr, Value& value)
{
    expr.trim();
    if (!expr.length()) {
        value = {};
        return true;
    }
    if (isQuoted(expr)) {
        value.kind = ValueKind::Text;
        value.text = unquote(expr);
        return true;
    }
    if (expr == "True") {
        value.kind = ValueKind::Integer;
        value.integer = 1;
        return true;
    }
    if (expr == "False") {
        value.kind = ValueKind::Integer;
        value.integer = 0;
        return true;
    }
    if (expr == "HIGH" || expr == "OUTPUT") {
        value.kind = ValueKind::Integer;
        value.integer = 1;
        return true;
    }
    if (expr == "LOW" || expr == "INPUT") {
        value.kind = ValueKind::Integer;
        value.integer = 0;
        return true;
    }
    if (evalPin(expr, value)) {
        return true;
    }
    int parsed = 0;
    if (parseIntegerLiteral(expr, parsed)) {
        value.kind = ValueKind::Integer;
        value.integer = parsed;
        return true;
    }
    if (auto* variable = findVariable(expr)) {
        value = variable->value;
        return true;
    }
    setError(String("cannot evaluate: ") + expr);
    return false;
}

bool PythonRunner::evalInteger(String expr, int& value)
{
    Value evaluated;
    if (!eval(expr, evaluated)) {
        return false;
    }
    if (evaluated.kind == ValueKind::Integer || evaluated.kind == ValueKind::Pin) {
        value = evaluated.integer;
        return true;
    }
    setError(String("integer expected: ") + expr);
    return false;
}

bool PythonRunner::callPrint(String args, Output output)
{
    auto parts = splitArgs(args);
    String line;
    for (size_t i = 0; i < parts.size(); ++i) {
        Value value;
        if (!eval(parts[i], value)) {
            return false;
        }
        if (i) {
            line += " ";
        }
        if (value.kind == ValueKind::Text) {
            line += value.text;
        } else if (value.kind == ValueKind::Integer) {
            line += value.integer;
        } else if (value.kind == ValueKind::Pin) {
            line += String("Pin(") + value.integer + ")";
        } else {
            line += "None";
        }
    }
    output(line);
    return true;
}

bool PythonRunner::executeAssignment(const String& name, String expr, Output)
{
    if (!isIdentifier(name)) {
        setError(String("invalid variable name: ") + name);
        return false;
    }
    Value value;
    if (!eval(expr, value)) {
        return false;
    }
    setVariable(name, value);
    return true;
}

bool PythonRunner::callPinMethod(const String& line, Output output)
{
    int dot = line.indexOf('.');
    int open = line.indexOf('(', dot + 1);
    if (dot <= 0 || open <= dot || !line.endsWith(")")) {
        return false;
    }
    String name = line.substring(0, dot);
    String method = line.substring(dot + 1, open);
    String args = line.substring(open + 1, line.length() - 1);
    name.trim();
    method.trim();
    auto* variable = findVariable(name);
    if (!variable || variable->value.kind != ValueKind::Pin) {
        return false;
    }
    int pin = variable->value.integer;
    if (method == "on") {
        digitalWrite(pin, HIGH);
        return true;
    }
    if (method == "off") {
        digitalWrite(pin, LOW);
        return true;
    }
    if (method == "value") {
        args.trim();
        if (!args.length()) {
            output(String(digitalRead(pin)));
            return true;
        }
        int state = 0;
        if (!evalInteger(args, state)) {
            return false;
        }
        digitalWrite(pin, state ? HIGH : LOW);
        return true;
    }
    setError(String("unknown Pin method: ") + method);
    return false;
}

bool PythonRunner::callFunction(String line, Output output)
{
    if (!line.endsWith(")")) {
        return false;
    }
    int open = line.indexOf('(');
    if (open <= 0) {
        return false;
    }
    String name = line.substring(0, open);
    String args = line.substring(open + 1, line.length() - 1);
    name.trim();
    if (name == "print") {
        return callPrint(args, output);
    }
    if (name == "sleep_ms" || name == "time.sleep_ms") {
        int ms = 0;
        if (!evalInteger(args, ms)) {
            return false;
        }
        delay(max(0, ms));
        return true;
    }
    if (name == "sleep" || name == "time.sleep") {
        int seconds = 0;
        if (!evalInteger(args, seconds)) {
            return false;
        }
        delay(max(0, seconds) * 1000);
        return true;
    }
    if (name == "pinMode") {
        auto parts = splitArgs(args);
        if (parts.size() != 2) {
            setError("pinMode(pin, mode) expected");
            return false;
        }
        int pin = 0;
        if (!evalInteger(parts[0], pin)) {
            return false;
        }
        String mode = parts[1];
        mode.trim();
        pinMode(pin, (mode == "INPUT" || mode == "0") ? INPUT : OUTPUT);
        return true;
    }
    if (name == "digitalWrite" || name == "pin") {
        auto parts = splitArgs(args);
        if (parts.size() != 2) {
            setError(String(name) + "(pin, value) expected");
            return false;
        }
        int pin = 0;
        int state = 0;
        if (!evalInteger(parts[0], pin) || !evalInteger(parts[1], state)) {
            return false;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, state ? HIGH : LOW);
        return true;
    }
    if (name == "digitalRead") {
        int pin = 0;
        if (!evalInteger(args, pin)) {
            return false;
        }
        output(String(digitalRead(pin)));
        return true;
    }
    if (name == "blink") {
        auto parts = splitArgs(args);
        if (parts.size() < 1 || parts.size() > 3) {
            setError("blink(pin, count=1, ms=250) expected");
            return false;
        }
        int pin = 0;
        int count = 1;
        int ms = 250;
        if (!evalInteger(parts[0], pin)) {
            return false;
        }
        if (parts.size() >= 2 && !evalInteger(parts[1], count)) {
            return false;
        }
        if (parts.size() >= 3 && !evalInteger(parts[2], ms)) {
            return false;
        }
        pinMode(pin, OUTPUT);
        for (int i = 0; i < max(0, count); ++i) {
            digitalWrite(pin, HIGH);
            delay(max(0, ms));
            digitalWrite(pin, LOW);
            delay(max(0, ms));
        }
        return true;
    }
    if (name == "help") {
        output("MicroPython-compatible subset:");
        output("  print(...), sleep_ms(ms), sleep(s)");
        output("  from machine import Pin");
        output("  led = Pin(pin, Pin.OUT); led.on(); led.off(); led.value(0|1)");
        output("  pin(pin, value), digitalWrite(pin, value), digitalRead(pin)");
        output("  blink(pin, count, ms), for i in range(n):");
        output("  exit() leaves py repl");
        return true;
    }
    setError(String("unknown function: ") + name);
    return false;
}

bool PythonRunner::executeStatement(String line, Output output)
{
    line.trim();
    if (!line.length() || line.startsWith("#")) {
        return true;
    }
    if (line.startsWith("from ") || line.startsWith("import ")) {
        return true;
    }
    if (line == "Pin.OUT" || line == "Pin.IN") {
        return true;
    }
    if (callPinMethod(line, output)) {
        return true;
    }
    int assign = line.indexOf('=');
    if (assign > 0 && line.indexOf("==") < 0) {
        return executeAssignment(trimCopy(line.substring(0, assign)), line.substring(assign + 1), output);
    }
    if (callFunction(line, output)) {
        return true;
    }
    Value value;
    if (eval(line, value)) {
        if (value.kind == ValueKind::Text) output(value.text);
        else if (value.kind == ValueKind::Integer) output(String(value.integer));
        else if (value.kind == ValueKind::Pin) output(String("Pin(") + value.integer + ")");
        else output("None");
        return true;
    }
    return false;
}

bool PythonRunner::executeForBlock(const std::vector<String>& lines, size_t& index, size_t end, int indent, String line, Output output)
{
    line.trim();
    if (!line.startsWith("for ") || !line.endsWith(":")) {
        return false;
    }
    int inPos = line.indexOf(" in range(");
    if (inPos < 0) {
        setError("only for <name> in range(n): is supported");
        return false;
    }
    String name = line.substring(4, inPos);
    name.trim();
    if (!isIdentifier(name)) {
        setError(String("invalid loop variable: ") + name);
        return false;
    }
    String arg = line.substring(inPos + strlen(" in range("), line.length() - 2);
    int count = 0;
    if (!evalInteger(arg, count)) {
        return false;
    }
    size_t blockStart = index + 1;
    size_t blockEnd = blockStart;
    while (blockEnd < end) {
        String child = lines[blockEnd];
        String stripped = trimCopy(child);
        if (stripped.length() && leadingIndent(child) <= indent) {
            break;
        }
        ++blockEnd;
    }
    if (blockStart == blockEnd) {
        setError("for block is empty");
        return false;
    }
    for (int i = 0; i < max(0, count); ++i) {
        Value value;
        value.kind = ValueKind::Integer;
        value.integer = i;
        setVariable(name, value);
        if (!runLines(lines, blockStart, blockEnd, output)) {
            return false;
        }
    }
    index = blockEnd - 1;
    return true;
}

bool PythonRunner::runLines(const std::vector<String>& lines, size_t begin, size_t end, Output output)
{
    for (size_t i = begin; i < end; ++i) {
        String raw = lines[i];
        String line = trimCopy(raw);
        if (!line.length() || line.startsWith("#")) {
            continue;
        }
        int indent = leadingIndent(raw);
        if (line.startsWith("for ")) {
            if (!executeForBlock(lines, i, end, indent, line, output)) {
                return false;
            }
            continue;
        }
        if (!executeStatement(line, output)) {
            return false;
        }
    }
    return true;
}

bool PythonRunner::runLine(const String& line, Output output)
{
    _lastError = "";
    return executeStatement(line, output);
}

bool PythonRunner::runFile(fs::FS& fs, const String& path, Output output)
{
    _lastError = "";
    File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        setError(String("cannot open ") + path);
        return false;
    }
    if (file.size() > 32768) {
        file.close();
        setError("script too large");
        return false;
    }
    std::vector<String> lines;
    String line;
    while (file.available()) {
        char c = static_cast<char>(file.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            lines.push_back(line);
            line = "";
        } else {
            line += c;
        }
    }
    if (line.length()) {
        lines.push_back(line);
    }
    file.close();
    return runLines(lines, 0, lines.size(), output);
}
