#include "PythonRunner.hpp"

#include <esp_heap_caps.h>

extern "C" {
#include "port/micropython_embed.h"
#include "py/runtime.h"
}

namespace {
constexpr size_t MicroPythonHeapSize = 256 * 1024;
constexpr const char* GfxMarker = "\x1f" "GFX ";
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

extern "C" __attribute__((weak)) bool tab5_python_gfx_command(const char* command)
{
    (void)command;
    return true;
}

extern "C" __attribute__((weak)) int tab5_python_gfx_width()
{
    return 800;
}

extern "C" __attribute__((weak)) int tab5_python_gfx_height()
{
    return 436;
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
            if (handleGfxOutputLine(_pendingOutput)) {
                _pendingOutput = "";
                return;
            }
            output(_pendingOutput);
            _pendingOutput = "";
            return;
        }
        String line = _pendingOutput.substring(0, newline);
        if (!handleGfxOutputLine(line)) {
            output(line);
        }
        _pendingOutput = _pendingOutput.substring(newline + 1);
    }
}

void PythonRunner::appendOutputChar(char c, Output output)
{
    if (c == '\n') {
        if (handleGfxOutputLine(_pendingOutput)) {
            _pendingOutput = "";
            return;
        }
        if (output) {
            output(_pendingOutput);
        }
        _pendingOutput = "";
        return;
    }
    _pendingOutput += c;
}

bool PythonRunner::handleGfxOutputLine(const String& line)
{
    if (!line.startsWith(GfxMarker)) {
        return false;
    }
    if (!tab5_python_gfx_command(line.substring(strlen(GfxMarker)).c_str())) {
        _interrupted = true;
        mp_raise_msg(&mp_type_KeyboardInterrupt, MP_ERROR_TEXT("interrupted"));
    }
    return true;
}

bool PythonRunner::runSource(const String& source, Output output)
{
    if (!ensureVm()) {
        return false;
    }

    _pendingOutput = "";
    _interrupted = false;
    activeRunner = this;
    activeOutput = output;
    int ok = mp_embed_exec_str_status(source.c_str());
    activeOutput = nullptr;
    activeRunner = nullptr;
    flushOutput(output);

    if (!ok) {
        setError(_interrupted ? "interrupted" : "MicroPython exception");
        return false;
    }
    _lastError = "";
    return true;
}

bool PythonRunner::shouldPrintExpression(const String& source) const
{
    if (!source.length() || source.indexOf('\n') >= 0 || source.indexOf(';') >= 0 || source.endsWith(":")) {
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
    _interrupted = false;
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
    source = buildGfxPrelude() + source;
    return runSource(source, output);
}

std::vector<String> PythonRunner::splitArgs(const String& args) const
{
    std::vector<String> out;
    String current;
    char quote = 0;
    for (size_t i = 0; i < args.length(); ++i) {
        char c = args[i];
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else if (c == '\\' && i + 1 < args.length()) {
                current += args[++i];
            } else {
                current += c;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ' ' || c == '\t') {
            if (current.length()) {
                out.push_back(current);
                current = "";
            }
        } else {
            current += c;
        }
    }
    if (current.length()) {
        out.push_back(current);
    }
    return out;
}

String PythonRunner::pythonStringLiteral(const String& value) const
{
    String out = "'";
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '\\' || c == '\'') {
            out += '\\';
        }
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

String PythonRunner::buildArgvPrelude(const String& path, const String& args) const
{
    String prelude = "argv=[";
    prelude += pythonStringLiteral(path);
    for (const String& arg : splitArgs(args)) {
        prelude += ",";
        prelude += pythonStringLiteral(arg);
    }
    prelude += "]\ntry:\n import sys\n sys.argv=argv\nexcept Exception:\n pass\n";
    return prelude;
}

String PythonRunner::buildGfxPrelude() const
{
    String prelude =
        "class _Tab5Gfx:\n"
        " def _cmd(self,s): print('\\x1fGFX '+s)\n"
        " def clear(self,c=0): self._cmd('clear '+str(int(c)))\n"
        " def present(self): self._cmd('present')\n"
        " def pixel(self,x,y,c): self._cmd('px '+str(int(x))+' '+str(int(y))+' '+str(int(c)))\n"
        " def line(self,x0,y0,x1,y1,c): self._cmd('line '+str(int(x0))+' '+str(int(y0))+' '+str(int(x1))+' '+str(int(y1))+' '+str(int(c)))\n"
        " def rect(self,x,y,w,h,c,fill=1): self._cmd('rect '+str(int(x))+' '+str(int(y))+' '+str(int(w))+' '+str(int(h))+' '+str(int(c))+' '+str(int(fill)))\n"
        " def fill(self,x,y,w,h,c): self.rect(x,y,w,h,c,1)\n"
        " def circle(self,x,y,r,c,fill=1): self._cmd('circle '+str(int(x))+' '+str(int(y))+' '+str(int(r))+' '+str(int(c))+' '+str(int(fill)))\n"
        " def text(self,x,y,s,c=16777215): self._cmd('text '+str(int(x))+' '+str(int(y))+' '+str(int(c))+' '+str(s).replace('\\n',' '))\n";
    prelude +=
        " def mono(self,x,y,cols,rows,cell,fg,bg,bits): self._cmd('mono '+str(int(x))+' '+str(int(y))+' '+str(int(cols))+' '+str(int(rows))+' '+str(int(cell))+' '+str(int(fg))+' '+str(int(bg))+' '+str(bits))\n";
    prelude += " _w=" + String(tab5_python_gfx_width()) + "\n";
    prelude += " _h=" + String(tab5_python_gfx_height()) + "\n";
    prelude +=
        " def size(self): return (self._w,self._h)\n"
        "gfx=_Tab5Gfx()\n";
    return prelude;
}

bool PythonRunner::runFile(fs::FS& fs, const String& path, Output output)
{
    return runFile(fs, path, "", output);
}

bool PythonRunner::runFile(fs::FS& fs, const String& path, const String& args, Output output)
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
    source = buildArgvPrelude(path, args) + buildGfxPrelude() + source;
    return runSource(source, output);
}
