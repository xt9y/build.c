#!/bin/sh
set -eu

C_BIN="$1"
INC="$2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

mkdir -p "$TMP/project/src" "$TMP/local"
cat >"$TMP/local/value.c" <<'SRC'
int value(void) { return 42; }
SRC
cat >"$TMP/project/src/main.c" <<'SRC'
int value(void);
int main(void) { return value() == 42 ? 0 : 1; }
SRC
cat >"$TMP/project/build.c" <<'SRC'
#include <cbuild.h>
void build(C_Build *b) {
    C_Dependency *lib = c_path(b, "local", "../local");
    c_dep_source(lib);
    c_dep_sources(lib, "*.c");
    C_Target *app = c_executable(b, "app");
    c_sources(app, "src/main.c");
    c_use(app, lib);
}
SRC
(
    cd "$TMP/project"
    C_INCLUDE_DIR="$INC" "$C_BIN" check >check.log 2>&1
    grep -q "1 targets" check.log
    C_INCLUDE_DIR="$INC" "$C_BIN" tree >tree.log
    grep -q "local" tree.log
    C_INCLUDE_DIR="$INC" "$C_BIN" compdb >/dev/null
    grep -q '"file":"src/main.c"' compile_commands.json
    C_INCLUDE_DIR="$INC" "$C_BIN" build >build.log 2>&1
    C_INCLUDE_DIR="$INC" "$C_BIN" run >/dev/null
    test ! -f c.lock
)

mkdir -p "$TMP/public/src" "$TMP/public/include"
cat >"$TMP/public/include/value.h" <<'SRC'
#ifndef EXPORTED_VALUE
#error missing public definition
#endif
int answer(void);
SRC
cat >"$TMP/public/src/lib.c" <<'SRC'
#include "value.h"
int answer(void) { return EXPORTED_VALUE; }
SRC
cat >"$TMP/public/src/main.c" <<'SRC'
#include "value.h"
int main(void) { return answer() == 42 ? 0 : 1; }
SRC
cat >"$TMP/public/build.c" <<'SRC'
#include <cbuild.h>
void build(C_Build *b) {
    C_Target *lib = c_static_library(b, "lib");
    c_sources(lib, "src/lib.c");
    c_public_include(lib, "include");
    c_public_define(lib, "EXPORTED_VALUE=42");
    C_Target *app = c_executable(b, "app");
    c_sources(app, "src/main.c");
    c_link_target(app, lib);
}
SRC
(
    cd "$TMP/public"
    C_INCLUDE_DIR="$INC" "$C_BIN" build app >build.log 2>&1
    C_INCLUDE_DIR="$INC" "$C_BIN" run app >/dev/null
)
echo "additional-features: ok"
