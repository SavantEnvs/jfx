#!/usr/bin/env bash
#
# mayhem/build.sh — fuzz harnesses + known-answer oracle for the native libraries JavaFX bundles
# (libjpeg in javafx.graphics; libxml2, libxslt in javafx.web). Runs in the commit image as `mayhem`
# in /mayhem. Everything is compiled straight from the in-tree sources (no network, no downloads), so
# the air-gapped PATCH re-run works; outputs go to /mayhem/build/ (gitignored upstream), so re-runs are idempotent.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

: "${SRC:=/mayhem}"
cd "$SRC"

TP=modules/javafx.web/src/main/native/Source/ThirdParty
JPEG=modules/javafx.graphics/src/main/native-iio/libjpeg
OUT=$SRC/build/mayhem          # `build/` is gitignored upstream, so `git clean -ffdX` removes it
BIN=$SRC/build/bin
mkdir -p "$OUT" "$BIN"

XML_SRCS="buf chvalid dict encoding entities error globals hash HTMLparser HTMLtree list parser parserInternals
          pattern SAX2 threads tree uri valid xmlIO xmlmemory xmlreader xmlsave xmlwriter xpath xmlstring"
XSLT_SRCS="attributes documents extensions extra functions imports keys namespaces numbers pattern preproc
           security templates transform variables xslt xsltlocale xsltutils attrvt"

JPEG_INC="-I$JPEG"
XML_INC="-I$TP/libxml/src/include -I$TP/libxml/linux/include -I$TP/libxml/linux -I$TP/libxml/src"
XSLT_INC="-I$TP/libxslt/src -I$TP/libxslt/linux $XML_INC"
XML_DEF="-DHAVE_CONFIG_H -D_REENTRANT -DLIBXML_STATIC"
XSLT_DEF="-DHAVE_CONFIG_H -DLIBXSLT_STATIC $XML_DEF"
WARN="-w"

# build_lib <variant> <name> <flags-for-this-variant+lib> <source files...>   -> $OUT/<variant>/lib<name>.a
# Objects are compiled by name through PATH (cache-safe) in parallel; ar is also called by name.
build_lib() {
  local variant="$1" name="$2" flags="$3"; shift 3
  local d="${OUT:?}/${variant:?}/${name:?}"
  rm -rf "$d"; mkdir -p "$d"
  printf '%s\n' "$@" | xargs -P "$MAYHEM_JOBS" -I{} sh -c \
    "$CC $flags $WARN -c {} -o $d/\$(basename {} .c).o"
  rm -f "${OUT:?}/${variant:?}/lib${name:?}.a"
  ar rcs "$OUT/$variant/lib$name.a" "$d"/*.o
}
xml_files()  { for f in $XML_SRCS;  do echo "$TP/libxml/src/$f.c"; done; }
xslt_files() { for f in $XSLT_SRCS; do echo "$TP/libxslt/src/libxslt/$f.c"; done; }

# ---- 1) sanitized, coverage-instrumented libraries for the fuzz targets --------------------------------
# The SanCov flag goes on the library compile unconditionally (even with an empty $SANITIZER_FLAGS) so the
# fuzzed code, not just the harness TU, carries coverage instrumentation.
SAN="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"
build_lib san xml  "$SAN $XML_DEF  $XML_INC"  $(xml_files)
build_lib san xslt "$SAN $XSLT_DEF $XSLT_INC" $(xslt_files)
# jpeg is a file-input target (libjpeg's error_exit never returns), so no in-process fuzzer runtime there:
# libjpeg shifts bit buffers / negative IDCT values on purpose (benign UB); only UBSan shift-base is relaxed
build_lib sanjpeg jpeg "$SANITIZER_FLAGS -fno-sanitize=shift-base $DEBUG_FLAGS -O1 $JPEG_INC" $(ls $JPEG/*.c)

# ---- 2) leak sanitizer off, at build time -------------------------------------------------------------
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/lsan_off.o"
# $STANDALONE_FUZZ_MAIN is plain C: compile it as C once so a harness never sees a mangled entry point.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

# ---- 3) harnesses: libFuzzer binary + run-once standalone reproducer ----------------------------------
ICU_LIBS="-licuuc -licudata"
SYS_LIBS="-lm -lpthread -ldl"
fuzz_target() {  # <name> <libs...>
  local t="$1"; shift
  local flags="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link $XSLT_DEF $XSLT_INC"
  $CC $flags -c "mayhem/harness/$t.c" -o "$OUT/$t.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/$t.o" "$OUT/lsan_off.o" "$@" $ICU_LIBS $SYS_LIBS -o "$BIN/$t"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/$t.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" "$@" $ICU_LIBS $SYS_LIBS -o "$BIN/$t-standalone"
}
fuzz_target xml_parse      "$OUT/san/libxml.a"
fuzz_target html_parse     "$OUT/san/libxml.a"
fuzz_target xpath_eval     "$OUT/san/libxml.a"
fuzz_target xslt_transform "$OUT/san/libxslt.a" "$OUT/san/libxml.a"

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $JPEG_INC -c mayhem/harness/jpeg_decode.c -o "$OUT/jpeg_decode.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/jpeg_decode.o" "$OUT/lsan_off.o" "$OUT/sanjpeg/libjpeg.a" -o "$BIN/jpeg_decode"

# ---- 4) oracle: known-answer probe built with the project's NORMAL flags (no sanitizer, no -gdwarf-3) ---
NORM="-O2 $COVERAGE_FLAGS"
build_lib norm jpeg "$NORM $JPEG_INC" $(ls $JPEG/*.c)
build_lib norm xml  "$NORM $XML_DEF  $XML_INC"  $(xml_files)
build_lib norm xslt "$NORM $XSLT_DEF $XSLT_INC" $(xslt_files)
$CC $NORM $JPEG_INC $XSLT_DEF $XSLT_INC mayhem/kat/kat.c \
    "$OUT/norm/libxslt.a" "$OUT/norm/libxml.a" "$OUT/norm/libjpeg.a" $ICU_LIBS $SYS_LIBS $COVERAGE_FLAGS -o "$BIN/kat"
file "$BIN/kat" | grep -q 'dynamically linked' || { echo "kat probe must be dynamically linked" >&2; exit 1; }
echo "build.sh done"
