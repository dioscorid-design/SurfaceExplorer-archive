// librarytreefocus.cpp - Le voci della Library che corrispondono alla scena.
// Vedi librarytreefocus.h.
#include "librarytreefocus.h"

#include "librarymanager.h"
#include "texturecode.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

void LibraryTreeFocus::focus(QTreeWidget *tree, QTreeWidgetItem *item)
{
    if (!tree || !item) return;
    item->setSelected(true);
    tree->setCurrentItem(item);
    for (QTreeWidgetItem *parent = item->parent(); parent; parent = parent->parent())
        parent->setExpanded(true);
    tree->scrollToItem(item);
}

const LibraryItem *LibraryTreeFocus::textureNamed(QTreeWidget *tree, const LibraryManager &lib,
                                                  const QString &name)
{
    if (name.isEmpty() || !tree) return nullptr;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        QVariant v = (*it)->data(0, Qt::UserRole + 1);
        if (v.isValid()) {
            const LibraryItem &item = lib.getTexture(v.toInt());
            if (QString::compare(name, item.name.trimmed(), Qt::CaseInsensitive) == 0)
                return &item;
        }
        ++it;
    }
    return nullptr;
}

void LibraryTreeFocus::selectTexture(QTreeWidget *tree, const LibraryManager &lib,
                                     const QString &activeCode, const QString &libName)
{
    // activeCode.trimmed(): cosi' anche un'immagine (solo tag //IMG:) entra.
    if (!tree || activeCode.trimmed().isEmpty()) return;
    const QString cleanedActive = TextureCode::cleanForComparison(activeCode);
    QTreeWidgetItemIterator itTex(tree);

    QTreeWidgetItem *byCode = nullptr;
    QTreeWidgetItem *byName = nullptr;
    while (*itTex) {
        QVariant vTex = (*itTex)->data(0, Qt::UserRole + 1);
        if (vTex.isValid()) {
            const LibraryItem &texItem = lib.getTexture(vTex.toInt());
            if (!byCode && TextureCode::itemMatchesCode(texItem, activeCode, cleanedActive))
                byCode = *itTex;
            if (!byName && !libName.isEmpty()
                && QString::compare(libName, texItem.name.trimmed(),
                                    Qt::CaseInsensitive) == 0)
                byName = *itTex;
            // NESSUN break anticipato sul codice. Prima si usciva appena una
            // voce rispondeva per codice, e questo IMPEDIVA di scoprire che piu'
            // avanti nell'albero c'era la voce col NOME esatto del record.
        }
        ++itTex;
    }

    // IL NOME VINCE SUL CODICE, quando c'e'. E' il contrario di quanto valeva
    // prima, ed e' il punto di questa modifica.
    //
    // "Il codice vince" ha una buona ragione -- il codice e' cio' che si DISEGNA,
    // e indicare una voce che renderebbe diversamente sarebbe una bugia -- ma
    // vale solo fra voci che il nome NON distingue. Quando il record porta un
    // libName e quella voce esiste, e' l'informazione piu' precisa che abbiamo:
    // dice da quale voce la texture VIENE, mentre il codice, da solo, non
    // distingue due preset che lo condividono.
    //
    // Caso misurato: "Fluid Iridescence" e "Fluid Iridescence TEST" hanno lo
    // STESSO codice colore e differiscono per il solo displacement. Il record
    // puntava (correttamente) a TEST, ma l'albero evidenziava Fluid Iridescence
    // -- la prima incontrata nella scansione. Cliccando la voce evidenziata si
    // caricava una texture DIVERSA da quella del record, e il disallineamento
    // che ne seguiva sembrava un bug del Sync: in realta' era il focus a
    // indicare la voce sbagliata.
    //
    // Il fallback sul codice resta per i record SENZA libName (tutti quelli
    // salvati prima che il campo esistesse) e per quando la voce col nome e'
    // stata rinominata o cancellata.
    QTreeWidgetItem *hit = byName ? byName : byCode;
    if (!hit) return;

    focus(tree, hit);
}

void LibraryTreeFocus::selectSound(QTreeWidget *tree, const LibraryManager &lib,
                                   const QString &searchCode, const QString &libName)
{
    if (!tree) return;
    tree->clearSelection();

    if (!searchCode.trimmed().isEmpty()) {

        // FUNZIONE DI PULIZIA AGGRESSIVA (Rimuove TUTTI i commenti e gli spazi)
        auto cleanAudioForComparison = [](QString str) {
            str.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
            str.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
            str.replace(QRegularExpression("\\s+"), "");
            return str;
        };

        QString normLoadedSound = cleanAudioForComparison(searchCode);

        // DUE CRITERI, come per le texture (selectTexture): il NOME salvato nel
        // record (l'ancora del suono) vince sul confronto del codice, che resta
        // il fallback per i record senza ancora e per una voce rinominata o
        // cancellata.
        QTreeWidgetItem *sndByCode = nullptr;
        QTreeWidgetItem *sndByName = nullptr;

        QTreeWidgetItemIterator itSnd(tree);
        while (*itSnd) {
            QVariant vSnd = (*itSnd)->data(0, Qt::UserRole + 3);
            if (vSnd.isValid()) {
                int idx = vSnd.toInt();
                const LibraryItem &sndItem = lib.getSound(idx);
                bool isMatch = false;

                if (!sndByName && !libName.isEmpty()
                    && QString::compare(libName, sndItem.name.trimmed(),
                                        Qt::CaseInsensitive) == 0)
                    sndByName = *itSnd;

                bool isMedia = sndItem.filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
                        sndItem.filePath.endsWith(".wav", Qt::CaseInsensitive) ||
                        sndItem.filePath.endsWith(".ogg", Qt::CaseInsensitive);

                if (isMedia) {
                    // MATCH ROBUSTO PER MEDIA: Estrae e confronta solo il nome del file (usando il codice NON pulito)
                    QString fileName = QFileInfo(sndItem.filePath).fileName();
                    if (!fileName.isEmpty() && searchCode.contains(fileName)) {
                        isMatch = true;
                    }
                } else if (!sndItem.scriptCode.isEmpty()) {
                    // MATCH PER SCRIPT PROCEDURALI: Usa il codice pulito (solo matematica GLSL)
                    QString normLibSound = cleanAudioForComparison(sndItem.scriptCode);

                    if (!normLibSound.isEmpty() && normLoadedSound.contains(normLibSound)) {
                        isMatch = true;
                    }
                }

                if (isMatch && !sndByCode) sndByCode = *itSnd;   // il primo, come prima
            }
            ++itSnd;
        }

        if (QTreeWidgetItem *hit = sndByName ? sndByName : sndByCode) focus(tree, hit);
    }
}

// Percorso di una voce: le cartelle lo portano in UserRole+10, i file nel
// tooltip (come refreshAndFocusPaths).
QString LibraryTreeFocus::itemPath(QTreeWidgetItem *item)
{
    const QVariant dir = item->data(0, Qt::UserRole + 10);
    return dir.isValid() ? dir.toString() : item->toolTip(0);
}

LibraryTreeFocus::Highlight LibraryTreeFocus::highlightOf(QTreeWidget *tree)
{
    Highlight h;
    for (QTreeWidgetItem *item : tree->selectedItems()) h.paths << itemPath(item);
    if (tree->currentItem()) h.current = itemPath(tree->currentItem());
    return h;
}

QTreeWidgetItem *LibraryTreeFocus::itemAt(QTreeWidget *tree, const QString &path)
{
    if (path.isEmpty()) return nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if (itemPath(*it) == path) return *it;
    return nullptr;
}

// Sposta l'elemento CORRENTE senza toccare la selezione e senza autoscroll:
// spostarlo fa scrollTo, che riapre i rami chiusi (stessa cautela del
// ripristino in apertura/chiusura di una cartella).
void LibraryTreeFocus::setCurrentQuietly(QTreeWidget *tree, QTreeWidgetItem *item)
{
    const bool autoScroll = tree->hasAutoScroll();
    tree->setAutoScroll(false);
    tree->selectionModel()->setCurrentIndex(item ? tree->indexFromItem(item) : QModelIndex(),
                                            QItemSelectionModel::NoUpdate);
    tree->setAutoScroll(autoScroll);
}

LibraryTreeFocus::Highlight LibraryTreeFocus::resting(const Highlight &h)
{
    if (h.paths.size() <= 1) return h;
    Highlight r;
    r.current = h.current;
    if (!h.current.isEmpty()) r.paths << h.current;
    return r;
}

void LibraryTreeFocus::restoreHighlight(QTreeWidget *tree, const Highlight &h)
{
    QList<QTreeWidgetItem *> items;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if (h.paths.contains(itemPath(*it))) items << *it;
    // Voce sparita (cancellata, rinominata, altra libreria): resta cio' che
    // ha deciso il comando.
    if (!h.paths.isEmpty() && items.isEmpty()) return;
    tree->clearSelection();
    for (QTreeWidgetItem *item : items) item->setSelected(true);
    setCurrentQuietly(tree, itemAt(tree, h.current));
}
