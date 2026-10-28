# machine-load.sh - Stato della macchina per i test (da includere con "source").
#
# Cose che rallentano un giro senza che il codice c'entri (2026-10-20: rete
# passata da ~50 a ~95 minuti con lo STESSO binario, 13-19 ricarichi per il
# watchdog della GPU):
#  1. APP NAP: senza nessuno davanti (schermo spento o bloccato) macOS ritarda
#     i timer dell'app, cioe' le attese dei test. Campione di 52 preset: 99 s
#     contro 62. Rimedio nell'app: beginTestActivity (testactivity.h);
#  2. la macchina che va in STOP durante un giro lungo: keep_machine_awake
#     tiene un'asserzione di caffeinate finche' lo script e' vivo (lo schermo
#     invece si spegne comunque, se il Mac e' bloccato);
#  3. la macchina CARICA (compilazioni in Xcode o Qt Creator, clangd che
#     reindicizza dopo un cambio a un header incluso ovunque): il carico si
#     stampa all'inizio e alla fine del giro.

# Soglia: i core di prestazione (sugli Apple Silicon il test gira li').
LOAD_LIMIT="$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || sysctl -n hw.ncpu)"

# Carico medio dell'ultimo minuto.
machine_load() { sysctl -n vm.loadavg | awk '{ print $2 }'; }

# Stampa il carico; sopra la soglia avvisa e mostra i processi piu' attivi.
#   check_machine_load inizio|fine
check_machine_load() {
    local load
    load="$(machine_load)"
    if awk -v l="$load" -v m="$LOAD_LIMIT" 'BEGIN { exit !(l > m) }'; then
        printf 'ATTENZIONE (%s): macchina carica, load %s con %s core di prestazione.\n' "$1" "$load" "$LOAD_LIMIT"
        printf 'Tempi e ricarichi per watchdog di questo giro non sono confrontabili. Piu'"'"' attivi:\n'
        # || true: head chiude il tubo e ps esce per SIGPIPE, che con
        # "set -o pipefail" negli script fermerebbe il giro.
        ps -Ao pcpu=,comm= -r | head -5 | sed -E 's|^ *([0-9.]+) +(.*/)?([^/]+)$|  \1%  \3|' || true
    else
        printf 'Carico della macchina (%s): %s (soglia %s)\n' "$1" "$load" "$LOAD_LIMIT"
    fi
}

# Aspetta (al piu' $1 secondi) che il carico scenda sotto la soglia: serve
# dopo un lavoro dello script stesso, come la build di riferimento.
wait_machine_load() {
    local waited=0
    while awk -v l="$(machine_load)" -v m="$LOAD_LIMIT" 'BEGIN { exit !(l > m) }' \
          && [ "$waited" -lt "$1" ]; do
        sleep 5
        waited=$((waited + 5))
    done
}

# Niente stop del sistema finche' lo script che la chiama e' vivo.
keep_machine_awake() {
    caffeinate -i -w $$ >/dev/null 2>&1 &
}
