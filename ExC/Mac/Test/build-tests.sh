#!/usr/bin/env bash
#
# build-tests.sh - Compila build/Tests, la build dei TEST INTERNI: ottimizzata
# (Release) ma con i test dentro (-DSE_SELF_TESTS=ON, vedi CMakeLists.txt).
# E' quella che usano gli script di ExC/Mac/Test.
#
# Perche' una cartella sua: e' piu' veloce della Debug (campione di 52 preset
# dell'andata e ritorno: 52 s contro 62), e non tocca le cartelle Debug e
# Release di Qt Creator, cosi' chi lavora nell'app e chi lancia i test non
# compilano mai nella stessa cartella.
#
# Uso:
#   ./ExC/Mac/Test/build-tests.sh                       sorgenti del repo -> build/Tests
#   ./ExC/Mac/Test/build-tests.sh <sorgenti> <build>    altro albero (es. il worktree
#                                                       di riferimento del test immagini)

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="${1:-$PROJECT_DIR}"
BLD="${2:-$PROJECT_DIR/build/Tests}"
CMAKE="${CMAKE:-$HOME/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.10.1/macos}"
NINJA="${NINJA:-$HOME/Qt/Tools/Ninja/ninja}"

if [ ! -f "$BLD/build.ninja" ]; then
    "$CMAKE" -S "$SRC" -B "$BLD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSE_SELF_TESTS=ON \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_MAKE_PROGRAM="$NINJA" > /dev/null
fi
"$CMAKE" --build "$BLD" -j8
