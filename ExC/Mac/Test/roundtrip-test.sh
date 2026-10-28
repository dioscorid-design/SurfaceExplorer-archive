#!/usr/bin/env bash
#
# roundtrip-test.sh - Test di andata e ritorno dei preset (vedi presetroundtrip.h).
#
# Apre l'app (build dei test, build-tests.sh), carica da sola ogni superficie e ogni record della
# libreria tre volte -- in ordine, al contrario e in ordine rimescolato (sempre
# lo stesso) -- e confronta cio' che scriverebbe un Save col file, e fra i
# passaggi (= stato rimasto dal preset caricato prima). Conta anche gli shader
# che non compilano durante il caricamento, letti dal log (shadercompilelog.h):
# il Save puo' essere giusto con la texture invisibile a schermo. Non scrive
# nulla nella libreria.
# Dura circa 20 minuti: nel frattempo non usare l'app, e meglio non caricare la
# macchina (un giro molto piu' lento del solito va rifatto).
#
# Quando lanciarlo:
#   - prima di un rilascio (beta, App Store);
#   - dopo modifiche a load/save o quando si aggiunge uno stato alla scena;
#   - prima e dopo ogni tappa del refactoring "stato unico della scena";
#   - dopo aver sistemato molti preset.
#
# Uso:
#   ./ExC/Mac/Test/roundtrip-test.sh                          tutta la libreria C/presets
#   ./ExC/Mac/Test/roundtrip-test.sh --filter "Paths|Kerr"    solo i preset che combaciano
#   ./ExC/Mac/Test/roundtrip-test.sh --single-pass            un solo passaggio (un terzo del
#                                                        tempo, niente controllo dell'ordine)
#   ./ExC/Mac/Test/roundtrip-test.sh --no-shuffle             senza il terzo passaggio rimescolato
#   ./ExC/Mac/Test/roundtrip-test.sh --shuffle-seed 7         un altro ordine rimescolato (il
#                                                        seme di default e' fisso)
#   ./ExC/Mac/Test/roundtrip-test.sh --data-only              SOLO DATI, pochi secondi: file ->
#                                                        parser -> Save, senza caricare nulla
#                                                        nell'app. Trova cio' che il parser
#                                                        legge ma il Save non scrive (o
#                                                        viceversa); non vede i bug del load.
#   PRESETS=/altra/libreria ./ExC/Mac/Test/roundtrip-test.sh
#
# Report in build/test-reports/roundtrip-<data>/report.txt (roundtrip-dati-<data>
# con --data-only), aperto alla fine.
# Come leggerlo: "Dipendono dal preset caricato prima" e "Con un popup di errore"
# devono restare a 0 ("Ricaricati perche' il watchdog..." conta i load ripetuti
# a macchina carica: non e' un difetto, ma se sono tanti il giro va rifatto a
# macchina libera); "Il Save cambia o perde qualcosa" oggi non e' 0 (residui
# nei dati che il Save ripulisce): si confronta col report precedente, il cui
# riepilogo viene stampato accanto a quello nuovo.

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
[ -d "$PRESETS/surfaces" ] || err "libreria non trovata o senza surfaces/: $PRESETS"

# Build stantia: un binario piu' vecchio dei sorgenti prova il codice di prima.
STALE="$(find "$PROJECT_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name '*.ui' \) -newer "$BIN" | head -3)"
if [ -n "$STALE" ]; then
    printf 'ATTENZIONE: il binario e'"'"' piu'"'"' vecchio di questi sorgenti -- ricompila prima:\n%s\n\n' "$STALE"
fi

# Il report precedente con cui confrontarsi e' dello STESSO tipo: un giro con
# l'app e uno solo sui dati misurano cose diverse.
KIND="roundtrip"
case " $* " in *" --data-only "*) KIND="roundtrip-dati" ;; esac
OUT="$REPORTS/$KIND-$(date +%Y%m%d-%H%M%S)"
PREV="$(ls -td "$REPORTS"/"$KIND"-2* 2>/dev/null | head -1 || true)"
mkdir -p "$OUT"

echo "Libreria: $PRESETS"
echo "Report:   $OUT"
echo "Test in corso (l'app si apre da sola e si chiude alla fine)..."

check_machine_load inizio
"$BIN" --roundtrip-test "$PRESETS" "$OUT" "$@" > "$OUT/app.log" 2>&1 &
PID=$!
# Avanzamento ogni 20 s, dall'ultima riga di progress.log (controllo ogni 2 s,
# cosi' un test breve come --data-only non aspetta a vuoto).
TICK=0
while kill -0 "$PID" 2>/dev/null; do
    sleep 2
    TICK=$((TICK + 2))
    if [ $((TICK % 20)) -eq 0 ] && [ -f "$OUT/progress.log" ]; then
        printf '  %s\n' "$(tail -1 "$OUT/progress.log" | cut -c1-110)"
    fi
done
RC=0
wait "$PID" || RC=$?
check_machine_load fine

[ -f "$OUT/report.txt" ] || err "il test non ha prodotto il report (codice $RC): vedi $OUT/app.log"

echo
echo "== Riepilogo =="
sed -n '3p;5,13p' "$OUT/report.txt"
if [ -n "$PREV" ] && [ -f "$PREV/report.txt" ]; then
    echo
    echo "== Riepilogo del report precedente ($(basename "$PREV")) =="
    sed -n '3p;5,13p' "$PREV/report.txt"
fi
echo
case "$RC" in
    0) echo "ESITO: tutto identico, nessuna dipendenza dall'ordine, nessun popup di errore, nessuno shader che non compila." ;;
    1) echo "ESITO: ci sono differenze -- confronta col report precedente (sopra)." ;;
    *) echo "ESITO: il test non e' partito correttamente (codice $RC): vedi $OUT/app.log" ;;
esac
open "$OUT/report.txt" 2>/dev/null || true
