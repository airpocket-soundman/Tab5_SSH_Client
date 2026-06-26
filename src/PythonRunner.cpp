#include "PythonRunner.hpp"

#include <esp_heap_caps.h>

extern "C" {
#include "port/micropython_embed.h"
}

namespace {
constexpr size_t MicroPythonHeapSize = 256 * 1024;
PythonRunner* activeRunner = nullptr;
PythonRunner::Output activeOutput = nullptr;

bool containsStatementAssignment(const String& source)
{
    for (size_t i = 0; i < source.length(); ++i) {
        if (source[i] != '=') {
            continue;
        }
        char prev = i > 0 ? source[i - 1] : '\0';
        char next = i + 1 < source.length() ? source[i + 1] : '\0';
        if (prev != '=' && prev != '!' && prev != '<' && prev != '>' && next != '=') {
            return true;
        }
    }
    return false;
}
}

extern "C" void micropython_host_stdout(const char* str, size_t len)
{
    if (!activeRunner || !activeOutput || !str || !len) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c == '\r') {
            continue;
        }
        activeRunner->appendOutputChar(c, activeOutput);
    }
}

void PythonRunner::setError(const String& text)
{
    _lastError = text;
}

bool PythonRunner::ensureVm()
{
    if (_started) {
        return true;
    }
    if (!_heap) {
        _heap = heap_caps_malloc(MicroPythonHeapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_heap) {
            _heap = heap_caps_malloc(MicroPythonHeapSize, MALLOC_CAP_8BIT);
        }
        if (!_heap) {
            setError("failed to allocate MicroPython heap");
            return false;
        }
        _heapSize = MicroPythonHeapSize;
    }
    int stackTop = 0;
    mp_embed_init(_heap, _heapSize, &stackTop);
    _started = true;
    _lastError = "";
    return true;
}

void PythonRunner::flushOutput(Output output)
{
    if (!output) {
        _pendingOutput = "";
        return;
    }
    while (_pendingOutput.length()) {
        int newline = _pendingOutput.indexOf('\n');
        if (newline < 0) {
            output(_pendingOutput);
            _pendingOutput = "";
            return;
        }
        output(_pendingOutput.substring(0, newline));
        _pendingOutput = _pendingOutput.substring(newline + 1);
    }
}

void PythonRunner::appendOutputChar(char c, Output output)
{
    if (c == '\n') {
        if (output) {
            output(_pendingOutput);
        }
        _pendingOutput = "";
        return;
    }
    _pendingOutput += c;
}

bool PythonRunner::runSource(const String& source, Output output)
{
    if (!ensureVm()) {
        return false;
    }

    _pendingOutput = "";
    activeRunner = this;
    activeOutput = output;
    int ok = mp_embed_exec_str_status(source.c_str());
    activeOutput = nullptr;
    activeRunner = nullptr;
    flushOutput(output);

    if (!ok) {
        setError("MicroPython exception");
        return false;
    }
    _lastError = "";
    return true;
}

bool PythonRunner::shouldPrintExpression(const String& source) const
{
    if (!source.length() || source.indexOf('\n') >= 0 || source.endsWith(":")) {
        return false;
    }
    String lower = source;
    lower.toLowerCase();
    const char* statementPrefixes[] = {
        "assert ", "break", "class ", "continue", "def ", "del ", "for ", "from ",
        "global ", "if ", "import ", "nonlocal ", "pass", "raise", "return",
        "try", "while ", "with ", "yield",
    };
    for (const char* prefix : statementPrefixes) {
        if (lower == prefix || lower.startsWith(prefix)) {
            return false;
        }
    }
    return !containsStatementAssignment(source);
}

void PythonRunner::reset()
{
    if (_started) {
        mp_embed_deinit();
        _started = false;
    }
    _pendingOutput = "";
    _lastError = "";
}

bool PythonRunner::runLine(const String& line, Output output)
{
    String source = line;
    source.trim();
    if (!source.length()) {
        return true;
    }
    if (shouldPrintExpression(source)) {
        source = String("__tab5_repl_result=(") + source +
                 ")\nif __tab5_repl_result is not None:\n    print(repr(__tab5_repl_result))";
    }
    return runSource(source, output);
}

bool PythonRunner::runFile(fs::FS& fs, const String& path, Output output)
{
    File file = fs.open(path, FILE_READ);
    if (!file) {
        setError(String("cannot open ") + path);
        return false;
    }
    String source;
    while (file.available()) {
        source += static_cast<char>(file.read());
        if (source.length() > 64 * 1024) {
            setError("script is too large");
            return false;
        }
    }
    return runSource(source, output);
}
