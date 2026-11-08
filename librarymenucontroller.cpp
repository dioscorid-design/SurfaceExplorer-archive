#include "librarymenucontroller.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "presetserializer.h"
#include "libraryfileoperations.h"
#include "librarytreefocus.h"

#include <QMenu>
#include <QFileDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer> // Aggiunto per prevenire i crash su iOS/Android
#include <QMouseEvent>
#include <QTreeWidgetItemIterator>

LibraryMenuController::LibraryMenuController(MainWindow *parent)
    : QObject(parent), m_mainWindow(parent)
{
}

void LibraryMenuController::watchTree(QTreeWidget *tree)
{
    if (tree) tree->viewport()->installEventFilter(this);
}

bool LibraryMenuController::eventFilter(QObject *obj, QEvent *event)
{
    // Ogni pressione fotografa l'evidenziazione (anche il dito su mobile: la
    // pressione lunga che apre il menu e' un tasto sinistro sintetizzato). Il
    // clic sinistro semplice la scarta al rilascio: quello carica, non apre
    // menu, e la foto resterebbe stantia per un menu aperto da tastiera.
    // Ctrl/Cmd+clic no: su macOS puo' essere il tasto destro.
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        auto *tree = qobject_cast<QTreeWidget *>(obj->parent());
        if (tree) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseButtonPress)
                m_pressHighlight[tree] = LibraryTreeFocus::highlightOf(tree);
            else if (me->button() == Qt::LeftButton
                     && !(me->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
                m_pressHighlight.remove(tree);
        }
    }
    return QObject::eventFilter(obj, event);
}

void LibraryMenuController::showMenu(QTreeWidget *senderTree, const QPoint &pos)
{
    if (!senderTree) return;

    // L'EVIDENZIAZIONE DA RIMETTERE alla chiusura: quella della pressione che
    // ha aperto il menu (Qt l'ha gia' spostata sulla voce cliccata, e qui
    // sotto un clic sul vuoto la cancella); da tastiera, quella di adesso.
    // Non nei rientri da ricostruzione della libreria (vedi i moti, piu'
    // sotto): li' non c'e' un menu dell'utente da chiudere.
    const bool keepHighlight = !m_mainWindow->libraryRebuildInProgress();
    // Una selezione multipla (Cmd/Shift+clic) e' per il comando: alla chiusura
    // resta il solo preset a schermo (LibraryTreeFocus::resting).
    const LibraryTreeFocus::Highlight before = !keepHighlight ? LibraryTreeFocus::Highlight{}
        : LibraryTreeFocus::resting(m_pressHighlight.contains(senderTree) ? m_pressHighlight.take(senderTree)
                                                                          : LibraryTreeFocus::highlightOf(senderTree));

    QPoint localPos = pos;
    QPoint globalMenuPos = senderTree->mapToGlobal(pos);

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    globalMenuPos = QCursor::pos();
    localPos = senderTree->viewport()->mapFromGlobal(globalMenuPos);
#endif

    // Usiamo la coordinata "pulita" (localPos) per capire cosa hai toccato!
    QTreeWidgetItem *itemUnderMouse = senderTree->itemAt(localPos);

    if (itemUnderMouse) {
        if (!itemUnderMouse->isSelected()) {
            senderTree->clearSelection();
            itemUnderMouse->setSelected(true);
            senderTree->setCurrentItem(itemUnderMouse);
        }
    } else {
        senderTree->clearSelection();
    }
    // Menu aperto: la SELEZIONE (blu) e' il bersaglio dei comandi, che leggono
    // solo lei; l'elemento CORRENTE (grigio) resta sul preset a schermo, che
    // cosi' non sparisce dall'albero finche' il menu e' aperto.
    QTreeWidgetItem *onScreen = nullptr;
    if (keepHighlight) {
        const QString onScreenPath = !before.current.isEmpty() ? before.current
                                   : before.paths.isEmpty()    ? QString() : before.paths.first();
        onScreen = LibraryTreeFocus::itemAt(senderTree, onScreenPath);
        if (onScreen) LibraryTreeFocus::setCurrentQuietly(senderTree, onScreen);
    }

    QList<QTreeWidgetItem*> selectedItems = senderTree->selectedItems();
    int count = selectedItems.count();
    QTreeWidgetItem* refItem = itemUnderMouse ? itemUnderMouse : (selectedItems.isEmpty() ? nullptr : selectedItems.first());

    QMenu *contextMenu = new QMenu(m_mainWindow);
    // Il ramo :disabled NON e' decorativo. Il QSS fissa 'color: #ffffff' per
    // tutto il menu, e senza una regola propria una voce disabilitata resta
    // BIANCA: indistinguibile da una attiva, quindi sembra che il comando sia
    // cliccabile e non faccia nulla (Qt non emette triggered su un'azione
    // disabilitata). Stessa famiglia di trappole del QSS globale sui tab.
    // ':selected:disabled' serve perche' la voce si evidenzia comunque al
    // passaggio del mouse: senza, tornerebbe bianca proprio mentre la si punta.
    contextMenu->setStyleSheet(
        "QMenu { background-color: #2b2b2b; color: #ffffff; border: 1px solid #3a3a3a; }"
        "QMenu::item { padding: 5px 20px; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
        "QMenu::item:disabled { color: #6e6e6e; }"
        "QMenu::item:selected:disabled { color: #6e6e6e; background-color: #2b2b2b; }"
        );

    // IL GRIGIO DEL PRESET A SCHERMO, finche' il menu e' a schermo. Fusion
    // disegna l'elemento corrente (grigio) solo con l'albero che ha il fuoco,
    // e il menu glielo toglie: lo si dipinge come sfondo della voce, nello
    // stesso tono, e lo si toglie alla chiusura -- prima del comando, che puo'
    // ricostruire l'albero (indice persistente: si toglie solo se c'e' ancora).
    if (onScreen && !onScreen->isSelected()) {
        const QPersistentModelIndex idx(senderTree->indexFromItem(onScreen));
        const QBrush previous = onScreen->background(0);
        onScreen->setBackground(0, QColor(60, 65, 72));
        connect(contextMenu, &QMenu::aboutToHide, senderTree, [senderTree, idx, previous] {
            if (QTreeWidgetItem *item = idx.isValid() ? senderTree->itemFromIndex(idx) : nullptr)
                item->setBackground(0, previous);
        });
    }

    QTreeWidget* safeTree = senderTree;
    QTreeWidgetItem* safeRefItem = refItem;

    // STATO DEI MOTI CATTURATO QUI, non piu' in basso.
    //
    // Serve alle due voci che cambiano cartella: sono le uniche a ricostruire gli
    // alberi, e il rebuild rientra in showMenu (vedi
    // MainWindow::refreshLibraryPreservingMotion per il meccanismo completo).
    // Va letto ORA perche' i moti girano ancora: piu' sotto showMenu li ferma, e
    // da quel punto in poi isAnimating()/isActive() direbbero "fermo" anche
    // quando l'utente li aveva accesi -- e' proprio cosi' che il rientro
    // perdeva lo stato.
    // Le altre voci non ne hanno bisogno: non ricostruiscono gli alberi, quindi
    // il ripristino in fondo a showMenu basta.
    const bool motionRotating = m_mainWindow->ui->glWidget->isAnimating();
    const bool motionPath4D   = m_mainWindow->pathRunning(CameraPaths::Path4D);
    const bool motionPath3D   = m_mainWindow->pathRunning(CameraPaths::Path3D);
    const bool motionTimeAnim = m_mainWindow->isAnythingMoving();

    std::function<void()> pendingAction = nullptr;

    auto executeAction = [&pendingAction](std::function<void()> func) {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        pendingAction = func;
#else
        func();
#endif
    };

    if (count > 0 && refItem) {
        if (count > 1) {
            QAction *actCopyAll = contextMenu->addAction(QString("Copy %1 Items").arg(count));
            connect(actCopyAll, &QAction::triggered, m_mainWindow, [this, refItem, executeAction](){
                executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
            });

            QAction *actCutAll = contextMenu->addAction(QString("Cut %1 Items").arg(count));
            connect(actCutAll, &QAction::triggered, m_mainWindow, [this, refItem, executeAction](){
                executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
            });

            QAction *actDeleteAll = contextMenu->addAction(QString("Delete %1 Items").arg(count));
            connect(actDeleteAll, &QAction::triggered, m_mainWindow, [this, executeAction](){
                executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
            });
        }
        else {
            bool isSurface = refItem->data(0, Qt::UserRole).isValid();
            bool isTexture = refItem->data(0, Qt::UserRole + 1).isValid();
            bool isMotion  = refItem->data(0, Qt::UserRole + 2).isValid();
            bool isSound   = refItem->data(0, Qt::UserRole + 3).isValid();
            bool isFolder  = refItem->data(0, Qt::UserRole + 10).isValid();

            if (isSurface) {
                int index = refItem->data(0, Qt::UserRole).toInt();
                QString path = m_mainWindow->m_libraryManager.getSurface(index).filePath;

                contextMenu->addAction("Save Shape As...", m_mainWindow, [this, path, executeAction](){
                    executeAction([this, path](){ m_mainWindow->m_presetSerializer->saveSurfaceAs(QFileInfo(path).absolutePath(), path); });
                });
                contextMenu->addAction("Rename Shape...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy Shape", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Shape", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                });
                contextMenu->addAction("Delete Shape", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isTexture) {
                int index = refItem->data(0, Qt::UserRole + 1).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getTexture(index);

                contextMenu->addAction("Save Texture As...", m_mainWindow, [this, data, executeAction](){
                    executeAction([this, data](){ m_mainWindow->m_presetSerializer->saveTextureAs(QFileInfo(data.filePath).absolutePath(), data.filePath); });
                });

                contextMenu->addAction("Rename Texture...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy Texture", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Texture", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addAction("Delete Texture", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isMotion) {
                int index = refItem->data(0, Qt::UserRole + 2).toInt();
                QString path = m_mainWindow->m_libraryManager.getMotion(index).filePath;

                contextMenu->addAction("Save Record As...", m_mainWindow, [this, path, executeAction](){
                    executeAction([this, path](){ m_mainWindow->m_presetSerializer->saveMotionAs(QFileInfo(path).absolutePath(), path); });
                });

                // SYNC FOCUSED TEXTURE: solo sul record CARICATO, e solo se la sua
                // texture di libreria e' stata modificata dopo il salvataggio.
                // Il record cliccato puo' non essere quello in scena (la selezione
                // dell'albero segue il click), e agire su un altro significherebbe
                // aggiornare una scena che l'utente non sta guardando.
                // Voce sempre PRESENTE ma disabilitata quando non farebbe nulla:
                // farla sparire lascerebbe intendere che la funzione non esista.
                {
                    const bool isLoaded = !m_mainWindow->currentRecordPath().isEmpty()
                                          && QFileInfo(m_mainWindow->currentRecordPath()) == QFileInfo(path);
                    // Superficie O sfondo: basta che una delle due texture del
                    // record sia rimasta indietro rispetto alla sua voce di libreria.
                    // Il comando non segue il radio Surface/Background del Renderer:
                    // aggiorna quelle indietro, e il tooltip dice QUALI, perche' dal
                    // menu non si vede.
                    // ...e le FASCE, ognuna con la sua ancora: il comando le
                    // aggiorna tutte insieme, qualunque sia la mesh selezionata.
                    const bool surfUpdate = isLoaded && m_mainWindow->focusedTextureLibraryItem();
                    const bool bgUpdate   = isLoaded && m_mainWindow->focusedBgTextureLibraryItem();
                    const int  meshCount  = isLoaded ? m_mainWindow->focusedMeshTextureLibraryItems().size() : 0;
                    const bool hasUpdate  = surfUpdate || bgUpdate || meshCount > 0;

                    QStringList what;
                    if (surfUpdate)    what << QStringLiteral("surface");
                    if (bgUpdate)      what << QStringLiteral("background");
                    if (meshCount > 0) what << (meshCount == 1 ? QStringLiteral("1 mesh")
                                                               : QString("%1 meshes").arg(meshCount));

                    QAction *actSync = contextMenu->addAction("Sync Focused Texture", m_mainWindow, [this, executeAction](){
                        executeAction([this](){ m_mainWindow->syncFocusedTextureFromLibrary(); });
                    });
                    actSync->setEnabled(hasUpdate);
                    actSync->setToolTip(!isLoaded ? QStringLiteral("Only for the record in the scene.")
                                      : hasUpdate ? QStringLiteral("Updates textures: ") + what.join(", ") + "."
                                      : QStringLiteral("Textures already up to date."));
                    contextMenu->setToolTipsVisible(true);
                }
                contextMenu->addAction("Rename Record...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy Record", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Record", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->m_fileOps->performCut(refItem); });
                });
                contextMenu->addAction("Delete Record", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isSound) {
                int index = refItem->data(0, Qt::UserRole + 3).toInt();
                const LibraryItem &data = m_mainWindow->m_libraryManager.getSound(index);

                if (data.filePath.endsWith(".json", Qt::CaseInsensitive)) {
                    contextMenu->addAction("Save Sound As...", m_mainWindow, [this, data, executeAction](){
                        // Finestra di dialogo (2 argomenti: cartella base e file sorgente)
                        executeAction([this, data](){ m_mainWindow->m_presetSerializer->saveSoundAs(QFileInfo(data.filePath).absolutePath(), data.filePath); });
                    });
                }

                contextMenu->addAction("Rename Sound...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy Sound", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Sound", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addAction("Delete Sound", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
            else if (isFolder) {
                QString folderPath = refItem->data(0, Qt::UserRole + 10).toString();

                if (senderTree == m_mainWindow->ui->treeSurfaces) {
                    // folderPath passato anche su desktop (come Texture/Sound):
                    // il vecchio ramo senza argomento ripiegava sulla selezione
                    // corrente e, in sua assenza, su lastFolder ("ultima usata").
                    contextMenu->addAction("Save Shape Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->saveSurfaceToFile(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeTextures) {
                    contextMenu->addAction("Save Texture Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveTextureAs(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeMotions) {
                    // folderPath anche su desktop, vedi Save Surface Here sopra.
                    contextMenu->addAction("Save Record Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveMotion(folderPath); });
                    });
                } else if (senderTree == m_mainWindow->ui->treeSounds) {
                    contextMenu->addAction("Save Sound Here...", m_mainWindow, [this, folderPath, executeAction](){
                        executeAction([this, folderPath](){ m_mainWindow->m_presetSerializer->saveSoundAs(folderPath, ""); });
                    });
                }
                contextMenu->addSeparator();

                contextMenu->addAction("Open in File Explorer", m_mainWindow, [folderPath, executeAction](){
                    executeAction([folderPath](){ QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath)); });
                });
                contextMenu->addSeparator();

                if (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty()) {
                    contextMenu->addAction("Paste Here", m_mainWindow, [this, executeAction](){
                        executeAction([this](){
                            if (!m_mainWindow->m_cutFilePaths.isEmpty()) m_mainWindow->onPasteExample();
                            else m_mainWindow->onPasteTexture();
                        });
                    });
                    contextMenu->addSeparator();
                }
                contextMenu->addAction("Rename Folder...", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->renameLibraryItem(refItem); });
                });
                contextMenu->addAction("Copy Folder", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCopy(refItem); });
                });
                contextMenu->addAction("Cut Folder", m_mainWindow, [this, refItem, executeAction](){
                    executeAction([this, refItem](){ m_mainWindow->performCut(refItem); });
                });
                contextMenu->addSeparator();
                contextMenu->addAction("Delete Folder (Destroys Files!)", m_mainWindow, [this, executeAction](){
                    executeAction([this](){ m_mainWindow->deleteSelectedExample(); });
                });
            }
        }
        contextMenu->addSeparator();
    }

    if (!refItem) {
        // Click nel ramo PRINCIPALE (fuori da ogni cartella): il dialog deve aprirsi
        // nella root della categoria, MAI su una cartella secondaria o sull'ultima
        // usata. Per questo passiamo esplicitamente la root come suggestedPath
        // (come fanno gia' Texture e Sound); senza path, saveSurface/saveMotion
        // ricadono sull'item selezionato / lastDir -> il dialog si apre in un ramo
        // secondario.
        QString rootPath = QSettings().value("libraryRootPath").toString();
        if (senderTree == m_mainWindow->ui->treeSurfaces) {
            QString startDir = QSettings().value("pathSurfaces", rootPath + "/surfaces").toString();
            contextMenu->addAction("Save New Shape...", m_mainWindow, [this, startDir, executeAction](){
                executeAction([this, startDir](){ m_mainWindow->saveSurfaceToFile(startDir); });
            });
        } else if (senderTree == m_mainWindow->ui->treeTextures) {
            contextMenu->addAction("Save New Texture...", m_mainWindow, [this, executeAction](){
                executeAction([this](){
                    QString rootPath = QSettings().value("libraryRootPath").toString();
                    m_mainWindow->m_presetSerializer->saveTextureAs(QSettings().value("pathTextures", rootPath + "/textures").toString());
                });
            });
        } else if (senderTree == m_mainWindow->ui->treeMotions) {
            QString startDir = QSettings().value("pathRecords", rootPath + "/records").toString();
            contextMenu->addAction("Save New Record...", m_mainWindow, [this, startDir, executeAction](){
                executeAction([this, startDir](){ m_mainWindow->m_presetSerializer->saveMotion(startDir); });
            });
        } else if (senderTree == m_mainWindow->ui->treeSounds) {
            contextMenu->addAction("Save New Sound...", m_mainWindow, [this, executeAction](){
                executeAction([this](){
                    QString rootPath = QSettings().value("libraryRootPath").toString();
                    m_mainWindow->m_presetSerializer->saveSoundAs(QSettings().value("pathSounds", rootPath + "/sounds").toString(), "");
                });
            });
        }

        contextMenu->addSeparator();
    }

    bool clickedOnFolder = (refItem && refItem->data(0, Qt::UserRole + 10).isValid());
    if (!clickedOnFolder && (!m_mainWindow->m_cutFilePaths.isEmpty() || !m_mainWindow->m_cutTexturePaths.isEmpty())) {
        // Paste nel ramo PRINCIPALE: qui non c'e' un item da cui dedurre la
        // destinazione (su mobile getCurrentLibraryItem() e' nullo). Passiamo la
        // root della CATEGORIA corrente col case corretto (/surfaces, /records...
        // minuscoli, come nel resto del codice): il vecchio fallback su Surface
        // usava "/Surfaces" (maiuscolo, inesistente su iOS case-sensitive) -> copy
        // fallita -> paste inerte.
        QString rootPath = QSettings().value("libraryRootPath").toString();
        QString destRoot;
        // Case MINUSCOLO per tutte: le cartelle reali sono create minuscole
        // (mainwindow ~7430). Su iOS/APFS case-sensitive "/Textures"/"/Sounds"
        // non esistono -> copy fallita -> paste inerte (stesso bug di Surface).
        if (senderTree == m_mainWindow->ui->treeSurfaces)      destRoot = QSettings().value("pathSurfaces", rootPath + "/surfaces").toString();
        else if (senderTree == m_mainWindow->ui->treeMotions)  destRoot = QSettings().value("pathRecords",  rootPath + "/records").toString();
        else if (senderTree == m_mainWindow->ui->treeSounds)   destRoot = QSettings().value("pathSounds",   rootPath + "/sounds").toString();
        else if (senderTree == m_mainWindow->ui->treeTextures) destRoot = QSettings().value("pathTextures", rootPath + "/textures").toString();

        if (!m_mainWindow->m_cutFilePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Item(s)").arg(m_mainWindow->m_cutFilePaths.count()), m_mainWindow, [this, destRoot, executeAction](){
                executeAction([this, destRoot](){ m_mainWindow->onPasteExample(destRoot); });
            });
        } else if (!m_mainWindow->m_cutTexturePaths.isEmpty()) {
            contextMenu->addAction(QString("Paste %1 Texture(s)").arg(m_mainWindow->m_cutTexturePaths.count()), m_mainWindow, [this, destRoot, executeAction](){
                executeAction([this, destRoot](){ m_mainWindow->onPasteTexture(destRoot); });
            });
        }
        contextMenu->addSeparator();
    }

    if (!m_mainWindow->m_undoStack.isEmpty()) {
        contextMenu->addAction("Undo Delete", m_mainWindow, [this, executeAction](){
            executeAction([this](){ m_mainWindow->onUndoDelete(); });
        });
        contextMenu->addSeparator();
    }

    contextMenu->addAction("Refresh Library", m_mainWindow, [this, executeAction](){
        executeAction([this](){ m_mainWindow->refreshRepositories(); });
    });
    contextMenu->addSeparator();

    contextMenu->addAction("Create New Folder...", m_mainWindow, [this, executeAction](){
        executeAction([this](){ m_mainWindow->onCreateFolderClicked(); });
    });
    contextMenu->addSeparator();

#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // NOMI DELLE DUE VOCI: dicono la SCALA su cui operano.
    //
    // Erano "Open Workspace Folder..." e "Change Library Folder...": affiancate,
    // sembravano due varianti della stessa cosa, e non lo sono affatto -- la
    // prima cambia UN RAMO (quello dell'albero da cui si apre il menu), la
    // seconda cambia TUTTA la libreria e ignora l'albero. Chi le leggeva
    // sceglieva la seconda aspettandosi la prima, finendo per puntare la radice
    // della libreria a un ramo.
    // Ora la prima porta il nome del ramo corrente ("Change Folder for
    // Records...") e la seconda dice esplicitamente che vale per tutto
    // ("Change Library Folder..."): la differenza si vede dai nomi, senza
    // doverla scoprire per tentativi.
    QString branchLabel = "Shapes";
    if (senderTree == m_mainWindow->ui->treeTextures)     branchLabel = "Textures";
    else if (senderTree == m_mainWindow->ui->treeMotions) branchLabel = "Records";
    else if (senderTree == m_mainWindow->ui->treeSounds)  branchLabel = "Sounds";

    // Nascondiamo l'apertura cartella Workspace su Mobile per evitare problemi di file system
    contextMenu->addAction(QString("Change Folder for %1...").arg(branchLabel), m_mainWindow,
                           [this, senderTree, executeAction,
                            motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
        executeAction([this, senderTree,
                       motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
            QSettings settings;
            QString rootPath = settings.value("libraryRootPath").toString();
            QString key;
            QString currentPath;
            if (senderTree == m_mainWindow->ui->treeTextures) { key = "pathTextures"; currentPath = settings.value(key, rootPath + "/Textures").toString(); }
            else if (senderTree == m_mainWindow->ui->treeMotions) { key = "pathRecords"; currentPath = settings.value(key, rootPath + "/records").toString(); }
            else if (senderTree == m_mainWindow->ui->treeSounds) { key = "pathSounds"; currentPath = settings.value(key, rootPath + "/Sounds").toString(); }
            else { key = "pathSurfaces"; currentPath = settings.value(key, rootPath + "/Surfaces").toString(); }

            QString dir = QFileDialog::getExistingDirectory(m_mainWindow, "Select Workspace Folder", currentPath);
            // ANNULLATO: i moti vanno rimessi qui. showMenu li ha fermati e li
            // ripristina in fondo, ma quel ripristino gira PRIMA della
            // singleShot in coda a questa voce; uscendo di qui la singleShot non
            // viene mai schedulata e nessuno li riaccende. Stessa ragione della
            // lambda restoreMotion in onAddRepositoryClicked, che ha il commento
            // esteso.
            if (dir.isEmpty()) {
                m_mainWindow->restoreMotionState(motionRotating, motionPath4D,
                                                 motionPath3D, motionTimeAnim);
                return;
            }

            settings.setValue(key, QDir::cleanPath(dir));
            settings.remove("repoPathsSurfaces"); settings.remove("repoPathsTextures");
            settings.remove("repoPathsMotions"); settings.remove("repoPathsSounds");
            settings.remove("repositoryPaths");

            // RINVIO A MENU CHIUSO: stessa ragione di onAddRepositoryClicked
            // (mainwindow.cpp), che ha il commento esteso. In breve: siamo
            // dentro contextMenu->exec(), con sopra il loop del pannello nativo,
            // e rileggere la libreria qui terrebbe il menu vivo per tutta la
            // scansione. Fuori dal menu il lavoro gira dal loop principale.
            //
            // I moti li rimette a posto refreshLibraryPreservingMotion, DOPO il
            // rebuild: il ripristino in fondo a showMenu gira prima di questo
            // singleShot e per giunta su flag che la rientranza ha falsato.
            QTimer::singleShot(0, m_mainWindow,
                               [this, motionRotating, motionPath4D, motionPath3D, motionTimeAnim]() {
                m_mainWindow->refreshLibraryPreservingMotion(motionRotating, motionPath4D,
                                                            motionPath3D, motionTimeAnim);
            });
        });
    });

    // SPOSTARE LA LIBRERIA: unico punto da cui questo comando e' raggiungibile.
    //
    // onAddRepositoryClicked esisteva ed era connessa a actionSelectFolder, ma
    // quell'azione non e' mai stata aggiunta a un menu (l'unico e' menuHelp:
    // Documentation/Quit/About), quindi era CODICE MORTO: non c'era modo di
    // cambiare la cartella della libreria.
    // "Restore Factory Presets" non copre il caso -- chiede la cartella solo se
    // la radice manca o e' irraggiungibile; con una libreria valida reinstalla i
    // preset dove sono e non sposta niente.
    // Va qui e non in un tasto: e' un'operazione rara, e il menu contestuale della
    // libreria e' dove si guarda quando si ha a che fare con le cartelle.
    //
    // Separatore: e' l'unica voce del menu che agisce su TUTTA la libreria
    // invece che sul ramo corrente, e stava attaccata a quella per-ramo.
    contextMenu->addSeparator();

    // senderTree NON si cattura: questo comando e' globale e non guarda da quale
    // albero e' stato aperto il menu. Qui si calcolava un LibraryType dal
    // senderTree per passarlo a onAddRepositoryClicked, che lo IGNORAVA (la
    // firma era `onAddRepositoryClicked(LibraryType /*type*/)`): codice che
    // non faceva nulla ma faceva sembrare il comando selettivo per ramo.
    contextMenu->addAction("Change Library Folder...", m_mainWindow,
                           [this, executeAction,
                            motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
        executeAction([this, motionRotating, motionPath4D, motionPath3D, motionTimeAnim](){
            // Lo stato dei moti viaggia fino alla rilettura: vedi
            // MainWindow::refreshLibraryPreservingMotion.
            m_mainWindow->onAddRepositoryClicked(motionRotating, motionPath4D,
                                                 motionPath3D, motionTimeAnim);
        });
    });
#endif

    // MOTI: NON SI TOCCANO DURANTE UNA RICOSTRUZIONE DELLA LIBRERIA.
    //
    // Il rebuild degli alberi riemette customContextMenuRequested e rientra qui
    // (misurato: 4 rientri, uno per albero). Quel rientro leggerebbe lo stato
    // dei moti da timer GIA' fermi -- li ha fermati la chiamata esterna --
    // registrando "fermo", e in fondo alla funzione spegnerebbe moti che
    // l'utente aveva acceso. E' il bug che ricompariva: non basta ripristinare
    // dopo il rebuild, perche' il rientro puo' arrivare anche PIU' TARDI,
    // dall'fsSyncTimer del QFileSystemWatcher (500 ms dopo il cambio cartella,
    // e chiama refreshRepositories direttamente).
    // Qui si salta stop e ripristino in blocco: chi ha avviato il rebuild
    // conosce lo stato vero e lo rimette lui.
    const bool skipMotionHandling = m_mainWindow->libraryRebuildInProgress();

    bool wasTimeAnimating = false;
    bool wasPath4D = false;
    bool wasPath3D = false;
    bool wasRotating = false;

    if (!skipMotionHandling) {
        if (m_mainWindow->isAnythingMoving()) {
            wasTimeAnimating = true;
            m_mainWindow->ui->glWidget->setSurfaceAnimating(false);
            m_mainWindow->ui->glWidget->stopAnimationTimer();
        }

        wasPath4D = m_mainWindow->pathRunning(CameraPaths::Path4D);
        wasPath3D = m_mainWindow->pathRunning(CameraPaths::Path3D);
        wasRotating = m_mainWindow->ui->glWidget->isAnimating();

        if (wasPath4D) m_mainWindow->m_paths->stop(CameraPaths::Path4D);
        if (wasPath3D) m_mainWindow->m_paths->stop(CameraPaths::Path3D);
        if (wasRotating) m_mainWindow->ui->glWidget->pauseMotion();
    }

// --- ESECUZIONE MENU ---
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // Esecuzione specifica Mobile
    contextMenu->exec(globalMenuPos);
    contextMenu->deleteLater();

    if (pendingAction) {
        QTimer::singleShot(350, m_mainWindow, [safeTree, safeRefItem, pendingAction, keepHighlight, before]() {
            if (safeTree && safeRefItem) {
                safeTree->setCurrentItem(safeRefItem);
                safeRefItem->setSelected(true);
            }
            pendingAction();
            if (keepHighlight) LibraryTreeFocus::restoreHighlight(safeTree, before);
        });
    } else if (keepHighlight) {
        LibraryTreeFocus::restoreHighlight(senderTree, before);
    }
#else
    // Esecuzione standard Desktop
    contextMenu->exec(senderTree->mapToGlobal(pos));
    delete contextMenu;
    // Il comando e' gia' stato eseguito (dentro exec).
    if (keepHighlight) LibraryTreeFocus::restoreHighlight(senderTree, before);
#endif

    if (wasTimeAnimating) {
        m_mainWindow->ui->glWidget->setSurfaceAnimating(true);
        m_mainWindow->ui->glWidget->startAnimationTimer();
    }
    if (wasPath4D) m_mainWindow->m_paths->start(CameraPaths::Path4D);
    if (wasPath3D) m_mainWindow->m_paths->start(CameraPaths::Path3D);
    if (wasRotating) m_mainWindow->ui->glWidget->resumeMotion();
}
