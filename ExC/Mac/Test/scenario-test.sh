#!/usr/bin/env bash
#
# scenario-test.sh - Test degli scenari d'uso (vedi scenariotest.h).
#
# Apre l'app (build dei test, build-tests.sh), carica alcuni record della libreria C/presets e
# preme i controlli veri come farebbe l'utente -- checkbox Texture, Base/Phong/
# Wireframe, Surface/Background, texture e suoni dalla Library, slider dei
# colori, fasce di una multi-mesh, dock Script, Run -- verificando dopo ogni
# gesto che le copie dello stato della scena (motore, controlli, cio' che
# scriverebbe il Save) restino coerenti: accensione della texture, colori
# u_col1/u_col2, codice e immagine della texture, displacement del Ray
# Marching, immagine di sfondo, costanti, equazioni, path e moti, lavoro non
# salvato, reset della scena, immagini sulle singole mesh. Alla fine: nessun
# campo di testo scritto a segnali vivi durante load e reset, e nessuno shader
# che non compila (dal log, shadercompilelog.h). Dura circa nove minuti.
#
# Quando lanciarlo: dopo ogni modifica alla gestione della texture o dei
# controlli di resa, e prima di un rilascio. E' la rete dei GESTI: il test dei
# preset copre load e save, non i clic.
#
# Uso:
#   ./ExC/Mac/Test/scenario-test.sh
#   ./ExC/Mac/Test/scenario-test.sh --scenario-only reset-scene   una sola sezione
#       (script-dock, texture-target, mesh-image, mesh-clocks, record-texture,
#        library-folder, library-paste, library-rename, master-records[:filtro],
#        renderer-all[:filtro], reset-scene, reset-button, tubes, border, meshes,
#        animated-load, load-order, steps, shape-background, border-audit[:filtro])
#       border-audit: bordo acceso su ogni superficie parametrica, flag dei lati
#       coperti riletti dalla GPU (~30 min); i dati restano in border-audit/ del
#       report e ExC/Mac/Test/border-replica.py li rifa' in Python.
#   PRESETS=/altra/libreria ./ExC/Mac/Test/scenario-test.sh
#
# Report in build/test-reports/scenario-<data>/scenario-report.txt, stampato alla fine.

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

OUT="$REPORTS/scenario-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

echo "Libreria: $PRESETS"
echo "Test in corso (l'app si apre da sola e si chiude alla fine, ~8 min)..."

check_machine_load inizio
RC=0
"$BIN" --scenario-test "$PRESETS" "$OUT" ${@+"$@"} > "$OUT/app.log" 2>&1 || RC=$?
check_machine_load fine

[ -f "$OUT/scenario-report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
cat "$OUT/scenario-report.txt"
echo
case "$RC" in
    0) echo "ESITO: tutte le verifiche passate." ;;
    1) echo "ESITO: qualche verifica FALLITA -- vedi le righe FALLITO qui sopra." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
exit "$RC"
