#!/usr/bin/env bash
#
# clock-test.sh - Test degli orologi di animazione (vedi clocktest.h).
#
# Apre l'app (build dei test, build-tests.sh) e verifica gli orologi delle animazioni in t
# (geometria, texture, sfondo, fasce), dal vivo e in registrazione video: il
# video parte dal frame a schermo e scorre alla stessa velocita', i moduli
# fermati a mano restano fermi, stop/start dell'orologio (Save Record) non fa
# saltare nulla, dopo il REC lo schermo riprende da dove il video e' arrivato.
# Legge il tempo che arriva davvero allo shader. Dura circa 20 secondi.
# Usa due record della libreria C/presets: t_motions/3D/Dynamic Mobius Band e
# Solid Wireframe/Multi Mesh/Hopf Tori.
#
# Quando lanciarlo: dopo ogni modifica al recorder (videorecorder.cpp), agli
# orologi o ai moti in GLWidget, e prima di un rilascio. Non sostituisce la
# prova a occhio su un mp4 esportato, ma trova in 20 secondi le regressioni
# che a schermo non si vedono.
#
# Uso:
#   ./ExC/Mac/Test/clock-test.sh
#   PRESETS=/altra/libreria ./ExC/Mac/Test/clock-test.sh
#
# Report in build/test-reports/clock-<data>/clock-report.txt, stampato alla fine.

set -euo pipefail
source "$(dirname "$0")/machine-load.sh"
keep_machine_awake

# --- root del progetto: lo script vive in ExC/Mac/Test/, la root e' tre cartelle sopra ---
PROJECT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
# La build dei test: ottimizzata, coi test dentro (build-tests.sh).
BIN="${BIN:-$PROJECT_DIR/build/Tests/SurfaceExplorer.app/Contents/MacOS/SurfaceExplorer}"
# La libreria su cui si lavora e' C/presets, accanto al repo (NON quella dentro il repo).
PRESETS="${PRESETS:-$(cd "$PROJECT_DIR/.." && pwd)/presets}"
REPORTS="$PROJECT_DIR/build/test-reports"

err() { printf 'ERRORE: %s\n' "$*" >&2; exit 1; }

[ -x "$BIN" ] || err "binario non trovato: $BIN
Compila la build dei test:
  ./ExC/Mac/Test/build-tests.sh"
[ -d "$PRESETS/records" ] || err "libreria non trovata o senza records/: $PRESETS"

# Build stantia: un binario piu' vecchio dei sorgenti prova il codice di prima.
STALE="$(find "$PROJECT_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) -newer "$BIN" | head -3)"
if [ -n "$STALE" ]; then
    printf 'ATTENZIONE: il binario e'"'"' piu'"'"' vecchio di questi sorgenti -- ricompila prima:\n%s\n\n' "$STALE"
fi

OUT="$REPORTS/clock-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

echo "Libreria: $PRESETS"
echo "Test in corso (l'app si apre da sola e si chiude alla fine, ~20 s)..."

check_machine_load inizio
RC=0
"$BIN" --clock-test "$PRESETS" "$OUT" > "$OUT/app.log" 2>&1 || RC=$?
check_machine_load fine

[ -f "$OUT/clock-report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
cat "$OUT/clock-report.txt"
echo
case "$RC" in
    0) echo "ESITO: tutte le verifiche passate." ;;
    1) echo "ESITO: qualche verifica FALLITA -- vedi le righe FALLITO qui sopra." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
exit "$RC"
