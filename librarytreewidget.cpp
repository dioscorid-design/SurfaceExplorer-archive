// librarytreewidget.cpp - Il clic su una cartella della Library non prende il
// focus (vedi librarytreewidget.h).
#include "librarytreewidget.h"

#include <QApplication>
#include <QFileInfo>
#include <QMouseEvent>

// LE CARTELLE NON PRENDONO IL FOCUS. Qt seleziona la voce alla PRESSIONE: un
// clic su una cartella toglieva l'evidenziazione al preset a schermo, e
// rimetterla dopo (al rilascio, all'apertura del ramo) la faceva lampeggiare
// sulla cartella. Qui un clic sinistro semplice su una cartella non tocca
// selezione ne' elemento corrente; aprire e chiudere resta alla freccetta,
// al doppio clic e al tocco su mobile (onExampleItemClicked). La cartella si
// seleziona solo:
//  - con Cmd/Shift+clic, selezione multipla voluta;
//  - col tasto destro, che la rende bersaglio del menu (LibraryMenuController);
//  - quando un trascinamento parte davvero: Qt trascina le voci selezionate, e
//    senza si sarebbe portato dietro il preset.
// Finito il trascinamento l'evidenziazione torna sul preset a schermo, come
// dopo un comando del menu contestuale (startDrag).

// I modificatori della selezione multipla: Cmd su macOS e Ctrl altrove
// (ControlModifier), Shift, e Ctrl su macOS (MetaModifier: li' e' il tasto
// destro, che apre il menu).
static constexpr Qt::KeyboardModifiers kMultiSelection = Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier;

bool LibraryTreeWidget::isFolder(const QModelIndex &index) const
{
    if (!index.isValid()) return false;
    const QTreeWidgetItem *item = itemFromIndex(index);
    return item && (item->data(0, Qt::UserRole + 10).isValid() || item->childCount() > 0);
}

QItemSelectionModel::SelectionFlags LibraryTreeWidget::selectionCommand(const QModelIndex &index,
                                                                       const QEvent *event) const
{
    if (m_folderPress) return QItemSelectionModel::NoUpdate;
    return QTreeWidget::selectionCommand(index, event);
}

// Rimette l'elemento corrente su `current` senza toccare la selezione. Senza
// autoscroll: spostarlo fa scrollTo, che riapre i genitori chiusi.
void LibraryTreeWidget::keepCurrent(const QPersistentModelIndex &current)
{
    if (!current.isValid() || currentIndex() == current) return;
    const bool autoScroll = hasAutoScroll();
    setAutoScroll(false);
    selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    setAutoScroll(autoScroll);
}

void LibraryTreeWidget::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    const bool left = event->button() == Qt::LeftButton;
    const bool multi = event->modifiers() & kMultiSelection;
    if (left) m_pressHighlight = LibraryTreeFocus::highlightOf(this);
    // CLIC SINISTRO SUL VUOTO: toglie la selezione multipla e rimette quella
    // a riposo, il preset a schermo (l'elemento corrente). Qt toglieva tutto e
    // lasciava il grigio dove l'avevano messo l'ultimo Cmd+clic o un Incolla:
    // il preset a schermo non era piu' indicato, e un altro sembrava esserlo.
    if (left && !multi && !index.isValid()) {
        m_folderPress = false;
        m_pressMulti = false;
        QTreeWidgetItem *current = currentItem();
        clearSelection();
        if (current) current->setSelected(true);
        event->accept();
        return;
    }
    m_folderPress = left && !multi && isFolder(index);
    m_pressMulti = left && multi;
    if (!m_folderPress) {
        // CMD/SHIFT+CLIC: la selezione cresce, ma l'elemento corrente (il
        // grigio) resta sul preset a schermo. Qt lo portava sulla voce cliccata,
        // e il menu e i ripristini, che lo leggono come preset a schermo,
        // evidenziavano poi quella.
        const QPersistentModelIndex current = currentIndex();
        // IL PRIMO CMD+CLIC dalla selezione a riposo (il solo preset a schermo):
        // il preset a schermo esce dalla selezione e resta grigio, cosi' i
        // comandi (Delete!) agiscono sulle sole voci scelte. Per metterlo nella
        // selezione basta un Cmd+clic anche su di lui. Shift+clic no: un
        // intervallo comprende la voce da cui parte.
        if (m_pressMulti && (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
            && index.isValid() && current.isValid() && index != QModelIndex(current)) {
            const QList<QTreeWidgetItem *> selected = selectedItems();
            if (selected.size() == 1 && selected.first() == itemFromIndex(current))
                selectionModel()->select(current, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
        }
        QTreeWidget::mousePressEvent(event);
        if (m_pressMulti) keepCurrent(current);
        return;
    }
    m_folder = index;
    m_pressPos = event->position().toPoint();
    // Qt porta l'elemento corrente sulla voce premuta: lo si rimette dov'era
    // nello stesso evento, quindi nessun fotogramma lo mostra sulla cartella.
    // Senza autoscroll: spostarlo fa scrollTo, che riapre i genitori chiusi.
    const QPersistentModelIndex current = currentIndex();
    QTreeWidget::mousePressEvent(event);
    keepCurrent(current);
}

void LibraryTreeWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_folderPress) {
        // Un clic che trema di qualche pixel non e' un trascinamento, e dove il
        // trascinamento non c'e' (mobile) il dito che scorre non seleziona.
        if (dragDropMode() == QAbstractItemView::NoDragDrop || !(event->buttons() & Qt::LeftButton)
            || (event->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        // Trascinamento vero: si porta la cartella, quindi la si seleziona ora.
        m_folderPress = false;
        if (m_folder.isValid())
            selectionModel()->setCurrentIndex(m_folder, QItemSelectionModel::ClearAndSelect
                                                            | QItemSelectionModel::Rows);
    }
    QTreeWidget::mouseMoveEvent(event);
}

void LibraryTreeWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // Ancora col clic sulla cartella acceso: Qt chiede la selezione anche al
    // rilascio, e deve ricevere NoUpdate.
    // CMD/CTRL+CLIC E SHIFT+CLIC SU UN PRESET: la selezione multipla, non un
    // load. Qt emette itemClicked dentro questo rilascio, anche coi
    // modificatori, e il preset si caricava subito: il flag lo dice a chi lo
    // riceve, e vale solo qui dentro. Basta il modificatore alla pressione: Cmd
    // lasciato un attimo prima del tasto e' lo stesso clic.
    m_multiSelectionRelease = event->button() == Qt::LeftButton
                              && ((event->modifiers() & kMultiSelection) || m_pressMulti);
    QTreeWidget::mouseReleaseEvent(event);
    m_multiSelectionRelease = false;
    m_pressMulti = false;
    m_folderPress = false;
}

// Dove e' finito `path` se stava dentro (o era) una delle voci trascinate:
// il rilascio seleziona le voci spostate, che nella cartella nuova hanno lo
// stesso nome. Altrimenti resta com'e'.
static QString movedPath(const QString &path, const QStringList &dragged, const QStringList &moved)
{
    for (const QString &src : dragged) {
        if (src.isEmpty() || (path != src && !path.startsWith(src + QLatin1Char('/')))) continue;
        const QString name = QFileInfo(src).fileName();
        for (const QString &dst : moved)
            if (dst != src && QFileInfo(dst).fileName() == name) return dst + path.mid(src.size());
    }
    return path;
}

void LibraryTreeWidget::startDrag(Qt::DropActions supportedActions)
{
    // Il trascinamento e' un comando come quelli del menu contestuale: finito
    // -- spostamento, rilascio sul posto o annullato -- l'evidenziazione torna
    // com'era prima della pressione, cioe' sul preset a schermo. Senza, restava
    // sulla cartella trascinata anche lasciandola dov'era. Il rilascio puo'
    // aver ricostruito l'albero (refreshAndFocusPaths): si rimette per
    // percorso, seguendo il preset se si e' spostato con la cartella.
    // Una selezione multipla trascinata: finito, resta il solo preset a schermo.
    LibraryTreeFocus::Highlight before = LibraryTreeFocus::resting(m_pressHighlight);
    QStringList dragged;
    for (QTreeWidgetItem *item : selectedItems()) dragged << LibraryTreeFocus::itemPath(item);
    QTreeWidget::startDrag(supportedActions);
    const QStringList moved = LibraryTreeFocus::highlightOf(this).paths;
    for (QString &p : before.paths) p = movedPath(p, dragged, moved);
    before.current = movedPath(before.current, dragged, moved);
    LibraryTreeFocus::restoreHighlight(this, before);
}
