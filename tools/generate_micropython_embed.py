#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MPY = ROOT / "external" / "micropython"
OUT = ROOT / "lib" / "micropython_embed"
BUILD = ROOT / ".pio" / "micropython_embed_build"


PY_CORE = [
    "mpstate", "nlr", "nlrx86", "nlrx64", "nlrthumb", "nlraarch64", "nlrmips",
    "nlrpowerpc", "nlrxtensa", "nlrrv32", "nlrrv64", "nlrloong64", "nlrsetjmp",
    "malloc", "gc", "pystack", "qstr", "vstr", "mpprint", "unicode", "mpz",
    "reader", "lexer", "parse", "scope", "compile", "emitcommon", "emitbc",
    "asmbase", "asmx64", "emitnx64", "asmx86", "emitnx86", "asmthumb",
    "emitnthumb", "emitinlinethumb", "asmarm", "emitnarm", "asmxtensa",
    "emitnxtensa", "emitinlinextensa", "emitnxtensawin", "asmrv32", "emitnrv32",
    "emitinlinerv32", "emitndebug", "formatfloat", "parsenumbase", "parsenum",
    "emitglue", "persistentcode", "runtime", "runtime_utils", "scheduler",
    "nativeglue", "pairheap", "ringbuf", "cstack", "stackctrl", "argcheck",
    "warning", "profile", "map", "obj", "objarray", "objattrtuple", "objbool",
    "objboundmeth", "objcell", "objclosure", "objcode", "objcomplex", "objdeque",
    "objdict", "objenumerate", "objexcept", "objfilter", "objfloat", "objfun",
    "objgenerator", "objgetitemiter", "objint", "objint_longlong", "objint_mpz",
    "objlist", "objmap", "objmodule", "objobject", "objpolyiter", "objproperty",
    "objnone", "objnamedtuple", "objrange", "objreversed", "objringio", "objset",
    "objsingleton", "objslice", "objstr", "objstrunicode", "objstringio",
    "objtemplate", "objtuple", "objtype", "objzip", "opmethods", "sequence",
    "stream", "binary", "builtinimport", "builtinevex", "builtinhelp", "modarray",
    "modbuiltins", "modcollections", "modgc", "modio", "modmath", "modcmath",
    "modmicropython", "modstring", "modstruct", "modsys", "moderrno", "modthread",
    "modweakref", "vm", "bc", "showbc", "repl", "smallint", "frozenmod",
]


def run(cmd, cwd=None, input_text=None, stdout=None):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run(
        [str(c) for c in cmd],
        cwd=cwd,
        input=input_text,
        text=input_text is not None,
        stdout=stdout,
        check=True,
    )


def copy_file(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def main():
    if not MPY.exists():
        raise SystemExit("external/micropython is missing; clone it first")

    gcc = ROOT / ".pio" / "packages" / "toolchain-riscv32-esp" / "bin" / "riscv32-esp-elf-gcc.exe"
    if not gcc.exists():
        gcc = Path.home() / ".platformio" / "packages" / "toolchain-riscv32-esp" / "bin" / "riscv32-esp-elf-gcc.exe"
    if not gcc.exists():
        raise SystemExit("riscv32-esp-elf-gcc.exe not found")

    if OUT.exists():
        shutil.rmtree(OUT)
    if BUILD.exists():
        shutil.rmtree(BUILD)

    for sub in ["py", "extmod", "shared/runtime", "genhdr", "port"]:
        (OUT / sub).mkdir(parents=True, exist_ok=True)
    (BUILD / "genhdr").mkdir(parents=True, exist_ok=True)

    for name in PY_CORE:
        copy_file(MPY / "py" / f"{name}.c", OUT / "py" / f"{name}.c")
    for src in (MPY / "py").glob("*.h"):
        copy_file(src, OUT / "py" / src.name)

    copy_file(MPY / "extmod" / "modplatform.h", OUT / "extmod" / "modplatform.h")
    copy_file(MPY / "shared" / "runtime" / "gchelper.h", OUT / "shared" / "runtime" / "gchelper.h")
    copy_file(MPY / "shared" / "runtime" / "gchelper_generic.c", OUT / "shared" / "runtime" / "gchelper_generic.c")
    for src in (MPY / "ports" / "embed" / "port").glob("*.[ch]"):
        copy_file(src, OUT / "port" / src.name)
    copy_file(MPY / "examples" / "embedding" / "mpconfigport.h", OUT / "mpconfigport.h")
    mpconfig = OUT / "mpconfigport.h"
    mpconfig_text = mpconfig.read_text(encoding="utf-8").replace(
            "#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)",
            "#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)",
        ).replace(
            "#define MICROPY_PY_SYS                          (0)",
            "#define MICROPY_PY_SYS                          (1)",
        )
    if "MICROPY_PY_SYS_PLATFORM" not in mpconfig_text:
        mpconfig_text += '\n#define MICROPY_PY_SYS_PLATFORM              "tab5"\n'
    if "MICROPY_PY_MATH" not in mpconfig_text:
        mpconfig_text += "#define MICROPY_PY_MATH                      (1)\n"
    if "MICROPY_FLOAT_IMPL" not in mpconfig_text:
        mpconfig_text += "#define MICROPY_FLOAT_IMPL                   (MICROPY_FLOAT_IMPL_DOUBLE)\n"
    if "MICROPY_ENABLE_EXTERNAL_IMPORT" not in mpconfig_text:
        mpconfig_text += "#define MICROPY_ENABLE_EXTERNAL_IMPORT       (0)\n"
    if "MICROPY_PY_IO" not in mpconfig_text:
        mpconfig_text += "#define MICROPY_PY_IO                        (0)\n"
    mpconfig.write_text(mpconfig_text, encoding="utf-8")
    embed_header = OUT / "port" / "micropython_embed.h"
    embed_header.write_text(
        embed_header.read_text(encoding="utf-8").replace(
            "void mp_embed_exec_str(const char *src);\n",
            "void mp_embed_exec_str(const char *src);\nint mp_embed_exec_str_status(const char *src);\n",
        ),
        encoding="utf-8",
    )
    embed_util = OUT / "port" / "embed_util.c"
    embed_util_text = embed_util.read_text(encoding="utf-8").replace(
            "void mp_embed_exec_str(const char *src) {\n",
            "int mp_embed_exec_str_status(const char *src) {\n",
        ).replace(
            "        nlr_pop();\n    } else {\n        // Uncaught exception: print it out.\n        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);\n    }\n}\n#endif\n",
            "        nlr_pop();\n        return 1;\n    } else {\n        // Uncaught exception: print it out.\n        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);\n        return 0;\n    }\n}\n\nvoid mp_embed_exec_str(const char *src) {\n    (void)mp_embed_exec_str_status(src);\n}\n#endif\n",
    )
    embed_util_text = re.sub(
        r"\n#ifndef NDEBUG\n// Used when debugging is enabled\.\nvoid __assert_func[\s\S]*?#endif\n?",
        "\n",
        embed_util_text,
    )
    embed_util.write_text(embed_util_text, encoding="utf-8")
    (OUT / "port" / "micropython_host_io.h").write_text(
        """#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void micropython_host_stdout(const char *str, size_t len);

#ifdef __cplusplus
}
#endif
""",
        encoding="utf-8",
    )

    run([sys.executable, MPY / "py" / "makeversionhdr.py", BUILD / "genhdr" / "mpversion.h"])
    (OUT / "port" / "mphalport.c").write_text(
        """#include "py/mphal.h"
#include "port/micropython_host_io.h"

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    micropython_host_stdout(str, len);
}
""",
        encoding="utf-8",
    )

    qstr_sources = [str(MPY / "py" / f"{name}.c") for name in PY_CORE if not name.startswith("nlr")]
    cflags = [
        "-E",
        "-DNO_QSTR",
        "-I" + str(OUT),
        "-I" + str(MPY),
        "-I" + str(BUILD),
        "-I" + str(BUILD / "genhdr"),
        "-I" + str(MPY / "ports" / "embed"),
        "-include",
        str(OUT / "mpconfigport.h"),
    ]
    qstr_i_last = BUILD / "genhdr" / "qstr.i.last"
    run(
        [
            sys.executable,
            MPY / "py" / "makeqstrdefs.py",
            "pp",
            "pp",
            gcc,
            "output",
            qstr_i_last,
            "cflags",
            *cflags,
            "sources",
            *qstr_sources,
            "changed_sources",
            *qstr_sources,
            "dependencies",
            MPY / "py" / "mpconfig.h",
            OUT / "mpconfigport.h",
        ]
    )

    for mode in ["qstr", "module", "root_pointer"]:
        split_dir = BUILD / "genhdr" / mode
        run([sys.executable, MPY / "py" / "makeqstrdefs.py", "split", mode, qstr_i_last, split_dir, "_"])
        collected = BUILD / "genhdr" / f"{mode}.collected"
        run([sys.executable, MPY / "py" / "makeqstrdefs.py", "cat", mode, "_", split_dir, collected])

    qstrdefs_pre = BUILD / "genhdr" / "qstrdefs.preprocessed.h"
    qstr_input = (MPY / "py" / "qstrdefs.h").read_text(encoding="utf-8")
    qstr_input += "\n"
    qstr_input += (BUILD / "genhdr" / "qstr.collected").read_text(encoding="utf-8")
    wrapped = "\n".join(f'"{line}"' if line.startswith("Q(") else line for line in qstr_input.splitlines())
    proc = subprocess.run(
        [str(gcc), *cflags, "-"],
        input=wrapped,
        text=True,
        stdout=subprocess.PIPE,
        check=True,
    )
    unwrapped = "\n".join(
        line[1:-1] if line.startswith('"Q(') and line.endswith('"') else line
        for line in proc.stdout.splitlines()
    )
    qstrdefs_pre.write_text(unwrapped, encoding="utf-8")
    with open(OUT / "genhdr" / "qstrdefs.generated.h", "w", encoding="utf-8") as out:
        run([sys.executable, MPY / "py" / "makeqstrdata.py", qstrdefs_pre], stdout=out)

    copy_file(BUILD / "genhdr" / "mpversion.h", OUT / "genhdr" / "mpversion.h")
    with open(OUT / "genhdr" / "moduledefs.h", "w", encoding="utf-8") as out:
        run([sys.executable, MPY / "py" / "makemoduledefs.py", BUILD / "genhdr" / "module.collected"], stdout=out)
    with open(OUT / "genhdr" / "root_pointers.h", "w", encoding="utf-8") as out:
        run([sys.executable, MPY / "py" / "make_root_pointers.py", BUILD / "genhdr" / "root_pointer.collected"], stdout=out)

    (OUT / "library.json").write_text(
        """{
  "name": "micropython_embed",
  "version": "1.0.0",
  "build": {
    "includeDir": ".",
    "srcDir": ".",
    "flags": [
      "-I.",
      "-Ipy",
      "-Igenhdr",
      "-Iport",
      "-Ishared/runtime",
      "-std=gnu99"
    ]
  }
}
""",
        encoding="utf-8",
    )
    print(f"generated {OUT}")


if __name__ == "__main__":
    main()
