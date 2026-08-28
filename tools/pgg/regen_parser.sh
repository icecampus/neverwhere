#!/usr/bin/env bash
# Regenerates src/libs/pgg/parser_gen from grammar/Pgg.g4 with the pinned ANTLR
# tool (4.13.2 == vcpkg antlr4 runtime, spec §13.1). Generated artifacts are
# committed; run this only after editing the grammar.
#
#   regen_parser.sh           regenerate parser_gen/ in place
#   regen_parser.sh --check   regenerate into a temp dir and diff against the
#                             committed artifacts (0 = fresh, 1 = stale,
#                             77 = skipped: no java/jar, used as ctest skip)
set -euo pipefail

ANTLR_VERSION=4.13.2
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GRAMMAR="$REPO_ROOT/src/libs/pgg/grammar/Pgg.g4"
GEN_DIR="$REPO_ROOT/src/libs/pgg/parser_gen"
JAR="$REPO_ROOT/tools/pgg/antlr-${ANTLR_VERSION}-complete.jar"
JRE_DIR="$REPO_ROOT/toolchain/jre"

find_java() {
    if command -v java >/dev/null 2>&1 && java -version >/dev/null 2>&1; then
        echo java; return 0
    fi
    # macOS JDK layout keeps binaries under Contents/Home.
    for candidate in "$JRE_DIR/bin/java" "$JRE_DIR/Contents/Home/bin/java"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"; return 0
        fi
    done
    return 1
}

bootstrap_jre() {
    # Temurin 21 JRE tarball, cached in toolchain/jre (gitignored).
    local os arch url
    case "$(uname -s)" in
        Darwin) os=mac ;;
        Linux)  os=linux ;;
        *) echo "no JRE bootstrap for $(uname -s); install Java manually" >&2; return 1 ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64) arch=aarch64 ;;
        x86_64)        arch=x64 ;;
        *) echo "unsupported arch $(uname -m)" >&2; return 1 ;;
    esac
    url="https://api.adoptium.net/v3/binary/latest/21/ga/${os}/${arch}/jre/hotspot/normal/eclipse"
    echo "Downloading Temurin JRE 21 for ${os}/${arch}..." >&2
    mkdir -p "$JRE_DIR"
    curl -fL "$url" -o "$JRE_DIR/jre.tar.gz"
    tar -xzf "$JRE_DIR/jre.tar.gz" -C "$JRE_DIR" --strip-components 1
    rm -f "$JRE_DIR/jre.tar.gz"
}

fetch_jar() {
    echo "Downloading antlr-${ANTLR_VERSION}-complete.jar..." >&2
    mkdir -p "$(dirname "$JAR")"
    curl -fL "https://www.antlr.org/download/antlr-${ANTLR_VERSION}-complete.jar" -o "$JAR"
}

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

JAVA="$(find_java)" || {
    if [ "$CHECK" -eq 1 ]; then exit 77; fi
    bootstrap_jre
    JAVA="$(find_java)" || { echo "java bootstrap failed" >&2; exit 1; }
}
if [ ! -f "$JAR" ]; then
    if [ "$CHECK" -eq 1 ]; then exit 77; fi
    fetch_jar
fi

run_tool() {
    local out="$1"
    rm -rf "$out"
    mkdir -p "$out"
    "$JAVA" -jar "$JAR" -Dlanguage=Cpp -no-listener -no-visitor \
        -o "$out" "$GRAMMAR"
}

if [ "$CHECK" -eq 1 ]; then
    TMP_DIR="$(mktemp -d)"
    trap 'rm -rf "$TMP_DIR"' EXIT
    run_tool "$TMP_DIR"
    if diff -r "$TMP_DIR" "$GEN_DIR" >/dev/null 2>&1; then
        echo "parser_gen is fresh"
        exit 0
    fi
    echo "parser_gen is STALE: run tools/pgg/regen_parser.sh and commit the result" >&2
    diff -rq "$TMP_DIR" "$GEN_DIR" >&2 || true
    exit 1
fi

run_tool "$GEN_DIR"
echo "Regenerated $GEN_DIR:"
ls -1 "$GEN_DIR"
