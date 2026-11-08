// librarytreefocus.h - Quale voce degli alberi della Library corrisponde a
// cio' che e' in scena, e la sua evidenziazione. Senza stato: albero e
// libreria arrivano come argomenti (gli alberi esistono solo dopo setupUi).
// CHE COSA cercare -- la texture efficace della superficie, dello sfondo o
// della fascia, e la sua ancora -- lo decide chi chiama, che conosce la scena.
#pragma once

#include <QString>
#include <QStringList>

class LibraryManager;
class QTreeWidget;
class QTreeWidgetItem;
struct LibraryItem;

namespace LibraryTreeFocus {

// Evidenzia una voce: selezionata, corrente, genitori aperti, portata in
// vista. Non deseleziona le altre: lo fa chi chiama, se serve.
void focus(QTreeWidget *tree, QTreeWidgetItem *item);
// La voce di texture col nome dato (senza badare alle maiuscole), o nullptr.
// Unica scansione dell'albero per tutte le ancore (texture di superficie, di
// sfondo, delle fasce): due copie dello stesso ciclo finirebbero per divergere
// alla prima correzione.
const LibraryItem *textureNamed(QTreeWidget *tree, const LibraryManager &lib,
                                const QString &name);
// Evidenzia nell'albero texture la voce del codice attivo. libName: l'ancora
// della texture cercata (superficie, sfondo o fascia), che vince sul codice;
// VUOTO se non nota, e allora si cerca per solo codice. Codice vuoto: niente.
void selectTexture(QTreeWidget *tree, const LibraryManager &lib,
                   const QString &activeCode, const QString &libName);
// Deseleziona l'albero dei suoni ed evidenzia la voce del suono in scena:
// searchCode e' il testo in cui cercarlo (slot del suono e codici delle
// texture, che possono portare un //MUSIC:), libName la sua ancora (vuota se
// in scena non c'e' audio). Stessi due criteri delle texture.
void selectSound(QTreeWidget *tree, const LibraryManager &lib,
                 const QString &searchCode, const QString &libName);

// L'EVIDENZIAZIONE DI UN ALBERO, per PERCORSO: dopo una ricostruzione
// dell'albero (un comando che sposta o rinomina file) gli item sono nuovi e si
// ritrovano solo cosi'. La fotografano e la rimettono il menu contestuale e il
// trascinamento, perche' resti sul preset a schermo.
struct Highlight { QStringList paths; QString current; };
Highlight highlightOf(QTreeWidget *tree);
// LA SELEZIONE A RIPOSO: una selezione multipla (Cmd/Shift+clic) serve a un
// comando; finito il comando, o il trascinamento, resta il solo preset a
// schermo, cioe' l'elemento corrente (che Cmd/Shift+clic non spostano,
// LibraryTreeWidget::mousePressEvent). Una selezione singola resta com'e'.
Highlight resting(const Highlight &h);
// Rimette selezione ed elemento corrente. Se nessuna delle voci c'e' piu'
// (cancellata, rinominata, altra libreria) resta cio' che ha deciso il comando.
void restoreHighlight(QTreeWidget *tree, const Highlight &h);
// Percorso di una voce (le cartelle in UserRole+10, i file nel tooltip, come
// refreshAndFocusPaths) e voce di un percorso (nullptr se non c'e').
QString itemPath(QTreeWidgetItem *item);
QTreeWidgetItem *itemAt(QTreeWidget *tree, const QString &path);
// Sposta l'elemento CORRENTE senza toccare la selezione e senza autoscroll:
// spostarlo fa scrollTo, che riapre i rami chiusi.
void setCurrentQuietly(QTreeWidget *tree, QTreeWidgetItem *item);

}
