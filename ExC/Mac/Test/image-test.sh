#!/usr/bin/env bash
#
# image-test.sh - Test delle immagini dei preset (vedi imagetest.h).
#
# Apre l'app (build dei test, build-tests.sh), carica ogni superficie e ogni record della
# libreria, ferma tutto (path, rotazioni, orologi) e fa una foto a risoluzione
# fissa. Con --ref confronta pixel per pixel con le foto fatte dal codice di un
# commit precedente: e' la rete delle tappe che toccano il renderer, dove la
# modalita' Superficie deve restare IDENTICA (scenari e andata e ritorno
# guardano lo stato, non i pixel).
#
# Il binario di riferimento si costruisce da solo: worktree del commit in
# build/image-ref/<sha>/src, build dei test in build/image-ref/<sha>/build, foto in
# build/image-ref/<sha>/photos-<opzioni> (riusate ai giri successivi con le
# stesse opzioni). Il commit di riferimento deve gia' contenere il test.
# Stessa macchina, stessa GPU, stessa Qt, stessa libreria per le due serie.
#
# Durata: circa 2 s a preset per serie (~12 minuti per tutta la libreria, il
# doppio la prima volta con --ref). Nel frattempo non usare l'app.
#
# Uso:
#   ./ExC/Mac/Test/image-test.sh                        solo le foto, binario attuale
#   ./ExC/Mac/Test/image-test.sh --ref 1a2b3c4          confronto col commit 1a2b3c4
#   ./ExC/Mac/Test/image-test.sh --ref HEAD~1 --filter "Klein|Hopf"
#   ./ExC/Mac/Test/image-test.sh --settle 2000          assestamento piu' lungo (ms)
#   PRESETS=/altra/libreria ./ExC/Mac/Test/image-test.sh
#
# Report in build/test-reports/images-<data>/image-report.txt; col confronto le
# immagini diverse in diff/ (pixel diversi in rosso). Righe:
#   uguale / DIVERSA         confronto col riferimento
#   rumore                   pochi pixel di 1-2 livelli (sezione dei tubi): non conta
#   INSTABILE                due foto dello stesso giro differiscono: fuori dal confronto
#   DIFFERITO                un moto e' partito dopo il fermo (flusso geodetico):
#                            posa dal tempo reale, fuori dal confronto

set -euo pipefail
source "$(dirname "$0")/machine-load.sh"
keep_machine_awake

PROJECT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
# La build dei test: ottimizzata, coi test dentro (build-tests.sh).
BIN="${BIN:-$PROJECT_DIR/build/Tests/SurfaceExplorer.app/Contents/MacOS/SurfaceExplorer}"
PRESETS="${PRESETS:-$(cd "$PROJECT_DIR/.." && pwd)/presets}"
REPORTS="$PROJECT_DIR/build/test-reports"
CMAKE="${CMAKE:-$HOME/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"
# Qt e ninja: gli stessi di build-tests.sh.
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.10.1/macos}"
NINJA="${NINJA:-$HOME/Qt/Tools/Ninja/ninja}"

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

REF="" FILTER="" SETTLE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --ref)    REF="${2:-}"; shift 2 ;;
        --filter) FILTER="${2:-}"; shift 2 ;;
        --settle) SETTLE="${2:-}"; shift 2 ;;
        *) err "opzione sconosciuta: $1" ;;
    esac
done

[ -x "$BIN" ] || err "binario non trovato: $BIN
Compila la build dei test:
  ./ExC/Mac/Test/build-tests.sh"
[ -d "$PRESETS/surfaces" ] || err "libreria non trovata o senza surfaces/: $PRESETS"

STALE="$(find "$PROJECT_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) -newer "$BIN" | head -3)"
if [ -n "$STALE" ]; then
    printf 'ATTENZIONE: il binario e'"'"' piu'"'"' vecchio di questi sorgenti -- ricompila prima:\n%s\n\n' "$STALE"
fi

ARGS=()
[ -n "$FILTER" ] && ARGS+=(--filter "$FILTER")
[ -n "$SETTLE" ] && ARGS+=(--settle "$SETTLE")

# --- Riferimento: worktree, build e foto del commit indicato ---
COMPARE=()
if [ -n "$REF" ]; then
    SHA="$(git -C "$PROJECT_DIR" rev-parse --short "$REF^{commit}")" || err "commit non trovato: $REF"
    REF_DIR="$PROJECT_DIR/build/image-ref/$SHA"
    SRC="$REF_DIR/src"
    BLD="$REF_DIR/build"
    KEY="$(printf '%s|%s' "$FILTER" "$SETTLE" | shasum | cut -c1-8)"
    PHOTOS="$REF_DIR/photos-$KEY"
    REF_BIN="$BLD/SurfaceExplorer.app/Contents/MacOS/SurfaceExplorer"

    if [ ! -f "$PHOTOS/image-report.txt" ]; then
        if [ ! -x "$REF_BIN" ]; then
            [ -d "$SRC" ] || git -C "$PROJECT_DIR" worktree add --detach "$SRC" "$SHA"
            grep -q "image-test" "$SRC/main.cpp" || err "il commit $SHA non contiene ancora il test delle immagini"
            echo "Build di riferimento ($SHA) in $BLD ..."
            if grep -q "option(SE_SELF_TESTS" "$SRC/CMakeLists.txt"; then
                # Stessa configurazione della build dei test: fra Debug e
                # Release i calcoli in virgola mobile possono differire
                # nell'ultima cifra, e darebbero immagini "diverse" per finta.
                "$PROJECT_DIR/ExC/Mac/Test/build-tests.sh" "$SRC" "$BLD" > "$REF_DIR/build.log" 2>&1 \
                    || err "build di riferimento fallita: vedi $REF_DIR/build.log"
            else
                # Commit di prima di build/Tests: i test esistono solo in Debug.
                echo "ATTENZIONE: $SHA non ha SE_SELF_TESTS, riferimento in Debug (possibile rumore)."
                "$CMAKE" -S "$SRC" -B "$BLD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
                    -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_MAKE_PROGRAM="$NINJA" > "$REF_DIR/configure.log" 2>&1 \
                    || err "configurazione fallita: vedi $REF_DIR/configure.log"
                "$CMAKE" --build "$BLD" -j8 > "$REF_DIR/build.log" 2>&1 \
                    || err "build di riferimento fallita: vedi $REF_DIR/build.log"
            fi
            # La build stessa carica la macchina: si aspetta che il carico scenda.
            wait_machine_load 180
        fi
        echo "Foto di riferimento ($SHA)..."
        check_machine_load "inizio riferimento"
        RC=0
        "$REF_BIN" --image-test "$PRESETS" "$PHOTOS" "${ARGS[@]+"${ARGS[@]}"}" > "$REF_DIR/photos-$KEY.log" 2>&1 || RC=$?
        [ -f "$PHOTOS/image-report.txt" ] || err "foto di riferimento non riuscite (codice $RC): vedi $REF_DIR/photos-$KEY.log"
    else
        echo "Foto di riferimento ($SHA) gia' pronte: $PHOTOS"
    fi
    COMPARE=(--compare "$PHOTOS")
fi

OUT="$REPORTS/images-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
echo "Libreria: $PRESETS"
echo "Foto in corso (l'app si apre da sola e si chiude alla fine)..."

check_machine_load inizio
RC=0
"$BIN" --image-test "$PRESETS" "$OUT" "${COMPARE[@]+"${COMPARE[@]}"}" "${ARGS[@]+"${ARGS[@]}"}" > "$OUT/app.log" 2>&1 || RC=$?
check_machine_load fine
[ -f "$OUT/image-report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
# Intestazione e conteggi (fino alla seconda riga vuota), poi solo le righe
# che non sono "uguale"/"foto".
awk '/^$/ { b++ } b < 2 { print }' "$OUT/image-report.txt"
awk '/^$/ { b++; next } b >= 2 && !/^(uguale|foto) / { print }' "$OUT/image-report.txt"
echo
echo "Report: $OUT/image-report.txt"
case "$RC" in
    0) echo "ESITO: nessuna differenza." ;;
    1) echo "ESITO: immagini DIVERSE dal riferimento -- vedi $OUT/diff/" ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
exit "$RC"
