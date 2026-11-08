// librarytreewidget.h - Gli alberi della Library (Shapes, Textures, Sounds,
// Records): un QTreeWidget in cui il clic su una CARTELLA non prende il focus.
// Promosso nel .ui per i quattro alberi.
#pragma once

#include <QPersistentModelIndex>
#include <QPoint>
#include <QTreeWidget>

#include "librarytreefocus.h"

class LibraryTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
    // Il clic che sta emettendo itemClicked e' una selezione multipla
    // (Cmd/Ctrl o Shift premuti al rilascio): chi carica il preset lo salta
    // (MainWindow::onExampleItemClicked, onSoundItemClicked).
    bool multiSelectionClick() const { return m_multiSelectionRelease; }

protected:
    // Durante un clic semplice su una cartella: nessuna selezione, ne' alla
    // pressione ne' al rilascio.
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index,
                                                         const QEvent *event = nullptr) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Trascinamento finito (spostamento, rilascio sul posto o annullato):
    // l'evidenziazione torna com'era prima della pressione.
    void startDrag(Qt::DropActions supportedActions) override;

private:
    bool isFolder(const QModelIndex &index) const;
    void keepCurrent(const QPersistentModelIndex &current);

    bool m_folderPress = false;      // clic sinistro senza Cmd/Shift su una cartella, in corso
    bool m_multiSelectionRelease = false;   // dentro il rilascio di un Cmd/Shift+clic
    bool m_pressMulti = false;              // la pressione aveva Cmd/Shift
    QPersistentModelIndex m_folder;
    QPoint m_pressPos;
    LibraryTreeFocus::Highlight m_pressHighlight;   // prima della pressione sinistra
};
