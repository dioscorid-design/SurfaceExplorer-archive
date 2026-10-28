#pragma once

// DURANTE I TEST INTERNI (macOS): niente App Nap ne' timer accorpati.
// Con la finestra non visibile -- schermo spento o bloccato, nessuno davanti
// -- macOS ritarda i timer delle app "in secondo piano": le attese fisse dei
// test (assestamento, pause fra le catture) si allungano e i frame diradati
// fanno scattare il watchdog della GPU. Il 2026-10-20 la rete e' passata cosi'
// da ~50 a ~95 minuti, con 13-19 ricarichi per watchdog, senza che il codice
// c'entrasse. beginTestActivity dichiara il processo impegnato in un lavoro
// avviato dall'utente e sensibile ai tempi, per tutta la sua durata. Solo nei
// test: l'app normale resta un buon cittadino del risparmio energetico.
void beginTestActivity();
