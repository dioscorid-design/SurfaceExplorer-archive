// mainwindow_library.cpp - MainWindow: Library -- click sugli alberi, cartelle e
// repository, copia/incolla, salvataggi, risorse di fabbrica.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

// Library: alberi, menu contestuali, osservatore del file system. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupLibraryDock()
{
    // =========================================================================
    // 10. LIBRARY TREES & FILE SYSTEM
    // =========================================================================
    QSettings settings;
    QStringList repos = settings.value("repositoryPaths").toStringList();

    if (!repos.isEmpty() && QDir(repos.first()).exists()) lastTextureFolder = repos.first();
    else {
        QString osBaseDir;
#ifdef Q_OS_ANDROID
        osBaseDir = "/storage/emulated/0/Download";
#elif defined(Q_OS_LINUX)
        osBaseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (osBaseDir.isEmpty()) osBaseDir = QDir::homePath();
#else
        osBaseDir = QDir::homePath();
#endif
        QString potentialPath = osBaseDir + "/Texture";
        if (QDir(potentialPath).exists()) lastTextureFolder = potentialPath;
        else lastTextureFolder = osBaseDir;
    }

    m_menuController = new LibraryMenuController(this);
    m_presetSerializer = new PresetSerializer(this);
    m_fileOps = new LibraryFileOperations(this);
    m_dragDropHandler = new LibraryDragDropHandler(this);
    m_audioController = new AudioController(this);
    // Il suono parte e si ferma anche da solo rispetto ai tasti (la musica
    // passa in riproduzione un attimo dopo play()): il master va rifatto
    // allora, o resterebbe su START con tutti i moduli accesi.
    connect(m_audioController, &AudioController::playingChanged,
            this, &MainWindow::updateMasterButtonState);

    auto initTree = [this](QTreeWidget* tree) {
        tree->setHeaderHidden(true);
        tree->setColumnCount(1);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Su mobile la selezione multipla col dito e' involontaria (muovendo il dito
        // si selezionano piu' file) e comunque il long-press per aprire il menu la
        // riduceva a 1 -> "Copy/Cut/Delete N items" agiva su un solo file. Selezione
        // SINGOLA: un tocco = un item, menu sempre coerente. Su desktop resta
        // ExtendedSelection (Ctrl/Shift+click funziona bene col mouse).
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        // Drag&drop diretto disabilitato su mobile (competeva con lo scroll a dito):
        // lo spostamento resta via menu Cut/Paste (long-press).
        tree->setDragDropMode(QAbstractItemView::NoDragDrop);
#else
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setDragDropMode(QAbstractItemView::InternalMove);
#endif

        // 1. FORZA lo scroll per pixel
        tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

        // 2. IL VERO SEGRETO: Dichiara che le righe sono tutte alte uguali.
        // Senza questo, Qt annulla lo scroll fluido e torna agli "scatti"!
        tree->setUniformRowHeights(true);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Niente scroll cinetico a dito sul viewport: si scrolla solo con la scroll
        // bar (lo scroll a dito animava di moto proprio ed evidenziava gli item).
        // Il TapAndHold resta: apre il menu contestuale.
        tree->grabGesture(Qt::TapAndHoldGesture);

        tree->setIndentation(12);
#endif

        tree->installEventFilter(m_dragDropHandler);
        tree->viewport()->installEventFilter(m_dragDropHandler);
        // Il menu contestuale riporta l'evidenziazione sul preset a schermo.
        m_menuController->watchTree(tree);
    };
    initTree(ui->treeSurfaces);
    connect(ui->treeSurfaces, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSurfaces->itemAt(pos)) { ui->treeSurfaces->clearSelection(); ui->treeSurfaces->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSurfaces, pos);
    });
    connect(ui->treeSurfaces, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeTextures);
    connect(ui->treeTextures, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeTextures->itemAt(pos)) { ui->treeTextures->clearSelection(); ui->treeTextures->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeTextures, pos);
    });
    connect(ui->treeTextures, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeMotions);
    connect(ui->treeMotions, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeMotions->itemAt(pos)) { ui->treeMotions->clearSelection(); ui->treeMotions->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeMotions, pos);
    });
    connect(ui->treeMotions, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeSounds);
    connect(ui->treeSounds, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSounds->itemAt(pos)) { ui->treeSounds->clearSelection(); ui->treeSounds->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSounds, pos);
    });
    connect(ui->treeSounds, &QTreeWidget::itemClicked, this, &MainWindow::onSoundItemClicked);

    // APRIRE O CHIUDERE UNA CARTELLA NON SPOSTA L'EVIDENZIAZIONE. Il clic sulla
    // RIGA di una cartella (tap su mobile, doppio clic su desktop; solo la
    // freccetta non seleziona) passa prima da Qt, che sposta la selezione sulla
    // cartella: il preset in vigore perdeva il focus anche se il clic serviva
    // solo ad aprire o chiudere il ramo. Qui si ricorda cio' che la cartella ha
    // scalzato e lo si rimette quando il ramo si apre o si chiude.
    // Il ripristino scatta solo se la cartella e' ancora l'intera selezione:
    // le espansioni del programma (load, refresh) arrivano con la selezione
    // gia' sul preset nuovo. Il clic semplice su una cartella non la seleziona
    // affatto (LibraryTreeWidget): qui restano i casi in cui una cartella e'
    // finita selezionata per altre vie (tastiera, tasto destro).
    // Indici persistenti, non puntatori: se l'albero viene ricostruito
    // (refreshLibrary) diventano invalidi da soli.
    auto keepHighlightOnToggle = [this](QTreeWidget *tree) {
        struct Displaced { QPersistentModelIndex folder; QList<QPersistentModelIndex> leaves; };
        auto d = std::make_shared<Displaced>();
        connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                [tree, d](const QItemSelection &, const QItemSelection &deselected) {
            d->folder = QPersistentModelIndex();
            d->leaves.clear();
            const QList<QTreeWidgetItem *> sel = tree->selectedItems();
            if (sel.size() != 1 || sel.first()->childCount() == 0) return;
            d->folder = tree->indexFromItem(sel.first());
            for (const QModelIndex &i : deselected.indexes()) {
                QTreeWidgetItem *it = tree->itemFromIndex(i);
                if (it && it->childCount() == 0) d->leaves << QPersistentModelIndex(i);
            }
        });
        auto restore = [tree, d](QTreeWidgetItem *folder) {
            if (d->leaves.isEmpty() || tree->indexFromItem(folder) != d->folder) return;
            const QList<QTreeWidgetItem *> sel = tree->selectedItems();
            if (sel.size() != 1 || sel.first() != folder) return;
            const QList<QPersistentModelIndex> leaves = d->leaves;   // la selectionChanged qui sotto lo azzera
            QItemSelectionModel *sm = tree->selectionModel();
            sm->clearSelection();
            QModelIndex current;
            for (const QPersistentModelIndex &i : leaves) {
                if (!i.isValid()) continue;
                sm->select(i, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                if (!current.isValid()) current = i;
            }
            // Senza autoscroll: spostare l'elemento corrente fa scrollTo, che
            // RIAPRE i genitori chiusi -- cioe' la cartella appena chiusa.
            if (current.isValid()) {
                const bool autoScroll = tree->hasAutoScroll();
                tree->setAutoScroll(false);
                sm->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
                tree->setAutoScroll(autoScroll);
            }
        };
        connect(tree, &QTreeWidget::itemExpanded, this, restore);
        connect(tree, &QTreeWidget::itemCollapsed, this, restore);
    };
    for (QTreeWidget *tree : { ui->treeSurfaces, ui->treeTextures, ui->treeMotions, ui->treeSounds })
        keepHighlightOnToggle(tree);

    connect(ui->btnSyncLibrary, &QPushButton::clicked, this, &MainWindow::onSyncPresetsClicked);

    m_fsWatcher = new QFileSystemWatcher(this);
    m_fsSyncTimer = new QTimer(this);
    m_fsSyncTimer->setSingleShot(true);
    m_fsSyncTimer->setInterval(500);

    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsSyncTimer, &QTimer::timeout, this, &MainWindow::refreshRepositories);
}




// ==========================================================
// LIBRARY & WORKSPACE MANAGEMENT
// ==========================================================

// ---- Passi di onExampleItemClicked ----

bool MainWindow::confirmLibraryClick(QTreeWidget *src, QTreeWidgetItem *item)
{
    const bool replacesScene = (src == ui->treeSurfaces || src == ui->treeMotions);
    // Solo le FOGLIE caricano qualcosa: sulle cartelle il click apre il ramo
    // e non tocca la scena, quindi non deve chiedere nulla.
    const bool isLeaf = (item && item->childCount() == 0);

    if (src == ui->treeTextures && isLeaf) {
        // Si sta perdendo la sola TEXTURE, non la scena: si guarda il flag
        // di quel modulo e il salvataggio punta al ramo textures/. Chiedere
        // qui della scena intera (cosa che una texture pure cambia) faceva
        // uscire un popup a ogni modifica, anche quando la texture non era
        // stata toccata.
        // ...tranne quando la texture non puo' andare sul bersaglio: verra'
        // rifiutata con un avviso (refuseIncompatibleTexture) e nulla cambia,
        // quindi non c'e' niente da difendere.
        const QVariant texIdx = item->data(0, Qt::UserRole + 1);
        const bool incompatible = texIdx.isValid()
            && textureIncompatible(m_libraryManager.getTexture(texIdx.toInt()));
        if (!incompatible && !confirmDiscardUnsaved(ScopeTexture)) {
            // Annullato: l'item cliccato e' gia' selezionato (la selezione
            // la fa il click) e indicherebbe una texture che non e' stata
            // caricata. Si rimette il focus su quella realmente in vigore,
            // con la stessa funzione usata dal load di texture incompatibili.
            syncTextureTreeSelection();
            return false;
        }

        // SCENA VUOTA (dopo NEW): si ricostruisce la superficie di default
        // del tab, cosi' la texture che si sta per caricare ha su cosa
        // apparire. Va fatto QUI, prima di leggere lo stato della scena piu'
        // sotto (isMatch, chkBoxTexture, ambito mesh): quel codice
        // presuppone una superficie, e su scena vuota deciderebbe sul nulla.
        ensureSurfaceForTexture();
    }

    if (replacesScene && isLeaf) {
        // L'item cliccato risulta gia' selezionato quando arriviamo qui (la
        // selezione la fa il click). Se l'utente annulla, va rimesso in
        // evidenza il preset REALMENTE a schermo -- non basta deselezionare,
        // o l'albero resta senza focus e non indica piu' nulla.
        //
        // Il test e' la selezione VIVA nell'albero d'origine, non la memoria
        // storica di m_lastLoadedLibraryItem: quest'ultima sopravvive anche
        // a un preset non piu' a schermo.
        // (Storico: l'evidenziazione cadeva alla prima modifica delle
        // equazioni, quindi qui poteva non esserci nulla da rievidenziare.
        // Ora RESTA -- si veda markUserEdit ~1798 -- e questo ramo trova
        // sempre l'item giusto.)
        QTreeWidgetItem *previous = m_lastLoadedLibraryItem;

        if (!confirmDiscardUnsaved(ScopeScene)) {
            bool b = src->blockSignals(true);
            src->clearSelection();
            // Il preset in vigore puo' stare in un ALTRO albero (una
            // superficie mentre si clicca un record): si rievidenzia dove
            // vive davvero, altrimenti solo si deseleziona.
            if (previous) {
                if (QTreeWidget *owner = previous->treeWidget()) {
                    bool b2 = (owner == src) ? false : owner->blockSignals(true);
                    previous->setSelected(true);
                    owner->setCurrentItem(previous);
                    if (owner != src) owner->blockSignals(b2);
                }
            } else {
                src->setCurrentItem(nullptr);
            }
            src->blockSignals(b);
            return false;
        }

        // Confermato: da qui in poi il preset caricato e' quello cliccato.
        m_lastLoadedLibraryItem = item;
    }
    return true;
}

void MainWindow::clearOtherLibraryTrees(QTreeWidget *src)
{
    if (src == ui->treeSurfaces) {
        for (QTreeWidget *tree : { ui->treeTextures, ui->treeMotions, ui->treeSounds }) {
            if (tree) {
                bool b = tree->blockSignals(true);
                tree->clearSelection();
                tree->setCurrentItem(nullptr);
                tree->blockSignals(b);
            }
        }
    } else if (src == ui->treeMotions && ui->treeSurfaces) {
        bool b = ui->treeSurfaces->blockSignals(true);
        ui->treeSurfaces->clearSelection();
        ui->treeSurfaces->setCurrentItem(nullptr);
        ui->treeSurfaces->blockSignals(b);
    }
}

bool MainWindow::libraryTextureIsActive(const LibraryItem &data)
{
    // BORDO: niente ramo del riclic (pensato per superficie, fasce e sfondo):
    // la voce si riapplica sempre al bordo. Senza, una texture uguale a quella
    // della superficie veniva presa per un riclic sulla superficie e al bordo
    // non arrivava.
    if (editingBorder()) return false;

    // 1. Recuperiamo il codice attualmente in uso nel tab attivo
    QString activeCode;
    if (editingBackground()) {
        activeCode = m_scene.bgTextureCode;
    } else if (implicitMode()) {
        activeCode = m_scene.rm.texture;
    } else if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0
               && ui->glWidget->activeMeshTextureActive()) {
        // AMBITO "MESH": la texture in uso e' quella della FASCIA, non
        // m_scene.surfaceTextureCode (che e' la texture di SUPERFICIE e qui
        // contiene altro, o niente). Leggendo lo slot sbagliato isMatch
        // risultava sempre falso, il toggle veniva saltato e il PRIMO click
        // ricaricava gia' resettando: sulla singola mesh mancava la fase di
        // stop che c'e' sulla superficie intera.
        activeCode = ui->glWidget->activeMeshTextureCode();
    } else {
        activeCode = m_scene.surfaceTextureCode;
    }

    // 2. Verifichiamo se la texture cliccata è già quella visualizzata
    bool isMatch = false;
    if (data.isImage) {
        QString fileName = QFileInfo(data.filePath).fileName();
        isMatch = (!fileName.isEmpty() && activeCode.contains(fileName));
    } else {
        isMatch = (TextureCode::cleanForComparison(activeCode) == TextureCode::cleanForComparison(data.scriptCode));
        // Il DISPLACEMENT distingue due texture quanto il colore: due preset
        // possono avere lo stesso codice di colore e rilievi diversi, e
        // guardando il solo colore l'app concludeva "e' gia' quella attiva",
        // entrava nel ramo del ri-click e NON ricaricava nulla -- ne' rilievo
        // ne' colori. Stessa ragione del confronto su m_currentTexturePresetPath
        // qui sotto, che copre il caso gemello dello zoom.
        if (isMatch && ui->lineVariations
            && TextureCode::cleanForComparison(m_scene.rm.displacement)
               != TextureCode::cleanForComparison(data.displacementCode))
            isMatch = false;
    }
    // Se il file preset è diverso da quello attualmente caricato, non è mai un match
    // (es. due preset con lo stesso codice ma zoom/rotazione diversi)
    if (data.filePath != m_currentTexturePresetPath) {
        isMatch = false;
    }

    // Se l'editor script (che in modalità texture mostra questo codice) è
    // stato svuotato dall'utente, la texture non è più "visualizzata":
    // senza questo, il ramo toggle qui sotto non ripristinava il testo e
    // bisognava caricare un'ALTRA texture per rivederlo.
    if (isMatch && m_currentScriptMode == ScriptModeTexture
        && scriptText(shownScriptSlot()).trimmed().isEmpty()) {
        isMatch = false;
    }

#if SE_TEX_PROBE
    // VERDETTO del gate con i pezzi del confronto: dice se si andra' nel
    // ramo ri-click (che non riapplica codice e displacement) o nel
    // caricamento pieno, e su quale dei quattro criteri si e' deciso.
    qDebug().noquote() << QString(
        "TEXP   isMatch=%1 | codice uguale=%2 | disp uguale=%3 | "
        "presetPath uguale=%4 | chk=%5 | preset='%6'")
        .arg(isMatch)
        .arg(TextureCode::cleanForComparison(activeCode) == TextureCode::cleanForComparison(data.scriptCode))
        .arg(ui->lineVariations
             && TextureCode::cleanForComparison(m_scene.rm.displacement)
                == TextureCode::cleanForComparison(data.displacementCode))
        .arg(data.filePath == m_currentTexturePresetPath)
        .arg(ui->chkBoxTexture && ui->chkBoxTexture->isChecked())
        .arg(QFileInfo(data.filePath).fileName());
#endif
    return isMatch;
}

void MainWindow::restartActiveLibraryTexture(const LibraryItem &data)
{
    // RAMO RI-CLICK: NON riapplica codice ne' displacement (per scelta:
    // sono gia' quelli). Se ci si entra quando in realta' il preset ha
    // un displacement DIVERSO, il rilievo resta quello di prima -- ed e'
    // il sospetto da verificare in questo giro.
    SE_TEXP("libTex:RAMO-RICLIC(isMatch)");
    bool isBg = editingBackground();
    // Il riclic rimette colori e inquadratura del preset: e' di nuovo la
    // texture della Library, come dopo una scelta (markTexturePicked).
    struct TexturePickedGuard {
        MainWindow *w;
        ~TexturePickedGuard() { w->markTexturePicked(); }
    } texturePickedGuard{this};

    // RIAVVIO DALL'INIZIO. Ricliccare il preset gia' attivo lo fa
    // ripartire da capo: e' la regola della Library, che superfici,
    // suoni e record seguono gia' (il loro ramo ricarica il preset e con
    // esso azzera l'orologio). Le texture avevano un ramo dedicato --
    // serve a non riapplicare il codice e a resettare la manipolazione
    // 2D -- che pero' l'orologio non lo toccava: il click non produceva
    // alcun effetto visibile su una texture animata.
    // Si azzera il SOLO clock interessato -- superficie o sfondo, che
    // sono separati: la geometria non la riguarda questo gesto
    // (resetTime() fermerebbe anche quella).
    // AMBITO "MESH": il gesto riguarda la SOLA fascia selezionata, quindi
    // nemmeno l'azzeramento puo' essere globale -- resetTextureTime
    // rimette a zero anche il clock di superficie e quello di TUTTE le
    // altre parti, che questo click non tocca.
    const bool onMeshScope = !isBg && ui->glWidget
                             && ui->glWidget->activeMeshPart() >= 0;
    if (onMeshScope) ui->glWidget->resetActiveMeshTextureTime();
    else             ui->glWidget->resetTextureTime(/*background=*/isBg);

    // COLORI DEL PRESET, RIAPPLICATI. Questo ramo dichiara di fare
    // "rivoglio questa texture com'e' nel preset" -- e lo faceva per il
    // clock e per la manipolazione 2D, ma NON per i colori: li lasciava
    // com'erano, dando per scontato che una texture "gia' attiva" avesse
    // gia' addosso i propri.
    //
    // Non e' piu' vero da quando esiste "Sync Focused Texture", che
    // crea di proposito uno stato misto: CODICE del preset + COLORI del
    // record. Li' il codice combacia, isMatch e' vero, e il ri-click --
    // l'unico gesto con cui si possono rivolere i colori originali --
    // non li riportava: la texture continuava a mostrarsi con la tinta
    // del record (verde del preset -> arancio del record, nel caso
    // segnalato). E' lo stesso motivo per cui il Sync NON allinea
    // m_currentTexturePresetPath: il click deve restare un caricamento
    // vero. Mancava solo che lo fosse anche per i colori.
    //
    // Stessi valori e stesso default del ramo che applica una texture
    // NUOVA (~8829): un preset senza colori propri riparte dal
    // verde/nero, o si terrebbe addosso quelli di prima.
    // In ambito "Mesh" si scrivono nella PARTE, non nei due slot
    // globali, che appartengono alla texture di superficie.
    if (!isBg) {
        const QColor presetC1 = data.hasCustomColors
                ? QColor(data.color1) : QColor::fromRgbF(0.20f, 0.80f, 0.20f);
        const QColor presetC2 = data.hasCustomColors
                ? QColor(data.color2) : QColor(Qt::black);
        if (onMeshScope) {
            ui->glWidget->setActiveMeshTexColors(presetC1, presetC2);
        } else if (ui->glWidget) {
            ui->glWidget->setGlobalTextureColors(presetC1, presetC2);
        }
        // DISPLAY degli slider: li rilegge dal motore (surfaceTexColor).
        onColorTargetChanged();
        // I colori sono uniform (blocco UBO): non serve ricompilare, ma
        // un frame va chiesto -- setActiveMeshTexColors non lo fa da se'.
        if (ui->glWidget) ui->glWidget->update();
    }

    if (isBg) {
        m_userStoppedBgClock = false;
        ui->glWidget->setBackgroundTextureAnimating(true);
    } else if (onMeshScope) {
        // RICLIC IN AMBITO "MESH": stessa portata del Run/Stop del dock
        // Script in questo ambito -- riguarda l'orologio della SOLA
        // fascia selezionata. Il ramo globale qui sotto passa invece da
        // setSurfaceTextureAnimating + restartAnimatedMeshTextures, che
        // riaccendono la texture di superficie e quelle di TUTTE le
        // parti: cliccare nella Library la texture della mesh corrente
        // faceva partire le animazioni di tutte le altre.
        // NESSUNA guardia su m_masterStopped: cliccare una texture in
        // ambito Mesh e' un comando esplicito su quella fascia, e parte
        // subito anche a scena ferma. Stessa scelta del ramo che APPLICA
        // una texture nuova (~7690) e del Run per-mesh del dock Script
        // (~10200): i tre gesti per-mesh si comportano allo stesso modo.
        // Stesso criterio di meshTextureAnimated: texture propria,
        // ACCESA e che usa il tempo. activeMeshTextureCode() da solo
        // ignora textureEnabled e accenderebbe un orologio a vuoto su
        // una fascia con la texture spenta (script conservato).
        m_userStoppedMeshTexClock = false;
        ui->glWidget->setActiveMeshTextureAnimating(
            ui->glWidget->activeMeshTextureActive()
            && hasTimeVariable(ui->glWidget->activeMeshTextureCode()));

        // RESET DELLA MANIPOLAZIONE 2D della sola fascia, come fa il
        // ramo globale per la superficie.
        ui->glWidget->setActiveMeshTexTransform(
            data.zoom, QVector2D(data.panX, data.panY), data.rotation);
        ui->glWidget->update();
    } else {
        // Ricarica del MODULO TEXTURE: colore E displacement condividono
        // lo stesso orologio texture, quindi basta riaccendere quello. Il
        // clock geometria/SDF NON va mai toccato da qui (lo governano dock
        // Equations e master): è proprio quel coupling che faceva "partire
        // la superficie" e bloccava i tasti.
        // La ricarica governa il MODULO texture, quindi anche gli orologi
        // delle fasce: riaccendere solo il globale lascerebbe le texture
        // per-mesh ferme sotto un tasto che dice "in moto".
        // Si guarda il CODICE (usa il tempo?), non lo stato del clock:
        // e' l'unico dato che dice se c'e' un'animazione possibile.
        //
        // In RAY MARCHING il codice sta in lineTexture/lineVariations,
        // NON in allSurfaceTextureCode() (che raccoglie la globale
        // parametrica e le per-mesh). Leggendo solo quella, su una
        // texture RM animata texIsAnimated risultava sempre false e il
        // riclic la FERMAVA invece di riavviarla -- e restava ferma,
        // perche' ogni click successivo ricadeva qui e rispegneva il
        // clock. E' la stessa distinzione che fa handleTextureSelection
        // (~7548) per decidere l'orologio al caricamento.
        const bool isRMTex = (implicitMode());
        const bool texIsAnimated = isRMTex
            ? (m_scene.rm.texture.contains(kReTimeVar)
               || m_scene.rm.displacement.contains(kReTimeVar))
            : (hasTimeVariable(allSurfaceTextureCode()) || anyMeshTextureCodeAnimated());

        // NON azzeriamo m_masterStopped (come il ramo background sopra):
        // il clock texture parte da solo, sbloccare lo stop globale
        // farebbe ripartire la geometria ferma dopo un master STOP.
        m_userStoppedTexClock = false;
        m_userStoppedMeshTexClock = false;
        // Il clock si accende solo se c'e' davvero un'animazione: su
        // una texture statica resterebbe acceso a vuoto.
        ui->glWidget->setSurfaceTextureAnimating(texIsAnimated);
        restartAnimatedMeshTextures();

        // RESET DELLA MANIPOLAZIONE 2D. Ricaricare dalla Library la
        // texture gia' attiva e' l'unico gesto che puo' significare
        // "rivoglio questa texture com'e' nel preset": senza, zoom/pan/
        // rotazione fatti col mouse restavano e per azzerarli bisognava
        // caricare un'ALTRA texture e poi tornare su questa.
        // Lo Stop/Start che lascia le modifiche intatte resta quello del
        // dock Script e del master.
        // Vale per la fascia selezionata E per la superficie intera:
        // setActiveMeshTexTransform scrive sulla parte attiva e
        // ritorna false in "All", dove tocca al ramo globale.
        if (!ui->glWidget->setActiveMeshTexTransform(
                data.zoom, QVector2D(data.panX, data.panY), data.rotation)) {
            // setGlobalTexTransform allinea da se' il buffer di
            // lavoro della vista 2D quando l'ambito e' "All".
            ui->glWidget->setGlobalTexTransform(
                data.zoom, QVector2D(data.panX, data.panY), data.rotation);
        }
        ui->glWidget->update();
    }

    updateMasterButtonState();
}

void MainWindow::onExampleItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    // Azzeramento incrociato ASIMMETRICO. Prima un click su QUALSIASI albero
    // azzerava tutti gli altri: cosi' caricare una texture dopo una superficie
    // toglieva l'evidenziazione alla superficie. Ora la superficie e' il
    // contesto principale: solo cliccando una SUPERFICIE si azzerano gli altri
    // alberi (texture/motion/sound), perche' la nuova superficie riparte da uno
    // stato pulito. Cliccare una texture/motion/sound invece LASCIA evidenziata
    // la superficie caricata (ognuno di questi conserva la propria selezione).
    //
    // ECCEZIONE: i RECORD. Texture e suoni si applicano SOPRA la superficie
    // corrente, quindi lasciarla evidenziata e' corretto; un record invece la
    // SOSTITUISCE (applyMotionExample ricarica equazioni, costanti e camera).
    // Senza questo ramo l'item della superficie precedente restava evidenziato
    // pur non essendo piu' a schermo: stessa selezione ingannevole gia' corretta
    // per le texture incompatibili (vedi il clearSelection del treeSurfaces piu'
    // sopra). blockSignals evita di ri-emettere itemClicked, che ricaricherebbe
    // la superficie appena sostituita dal record.
    // CONFERMA PRIMA DI SOSTITUIRE LA SCENA.
    // Superfici e record la SOSTITUISCONO (un record ricarica equazioni,
    // costanti e camera): sono distruttivi quanto NEW o il cambio di modalita',
    // ma molto piu' frequenti, ed erano l'unico percorso distruttivo che non
    // chiedeva nulla. La domanda va fatta PRIMA dell'azzeramento incrociato
    // degli alberi qui sotto: su Cancel non deve cambiare niente, nemmeno le
    // evidenziazioni.
    //
    // Superfici e record riscrivono anche texture e suono, quindi si usa
    // ScopeScene: UN popup che elenca tutti i moduli sporchi e salva una volta
    // sola, dalla radice dell'albero (dove "records" li tiene insieme).
    //
    // Le TEXTURE si applicano sopra la scena e non la sostituiscono, ma
    // scartano il lavoro fatto sulla texture corrente: hanno una conferma loro,
    // che protegge il solo modulo (textureModuleDirty) e apre il salvataggio sul
    // ramo textures/ anziche' sulla radice dell'albero. Stessa cosa per i
    // suoni, in onSoundItemClicked (albero servito da un altro slot).
    QTreeWidget *src = qobject_cast<QTreeWidget*>(sender());
    if (!confirmLibraryClick(src, item)) return;

    // Solo per le FOGLIE: il clic su una cartella apre o chiude il ramo e non
    // cambia la scena, quindi non deve togliere l'evidenziazione agli altri
    // alberi (aprire una cartella delle Surfaces la toglieva alla texture e al
    // suono in vigore, una dei Records alla superficie).
    if (src && item && item->childCount() == 0) clearOtherLibraryTrees(src);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
#endif

    // ==========================================
    // 1. SURFACES
    // ==========================================
    QVariant vSurf = item->data(0, Qt::UserRole);
    if (vSurf.isValid()) {
        static QString lastSurfacePath = "";
        int index = vSurf.toInt();

        // L'indice posizionale è fragile: dopo un refresh (fsWatcher dopo un save)
        // la lista m_surfaces viene ricostruita e l'ordine può cambiare, mentre il
        // nodo albero conserva l'indice VECCHIO -> si caricava un preset omonimo di
        // un'altra cartella (bug del bordo: 3 "Mobius Strip" diversi). Il tooltip del
        // nodo porta sempre il filePath univoco: cerchiamo per quello, con fallback
        // all'indice per retrocompatibilità se il tooltip fosse vuoto.
        const QString nodePath = item->toolTip(0);
        const LibraryItem *byPath = nodePath.isEmpty() ? nullptr
                                    : m_libraryManager.getSurfaceByPath(nodePath);
        const LibraryItem &data = byPath ? *byPath : m_libraryManager.getSurface(index);

        if (!data.name.isEmpty()) {
            // La conferma "vuoi salvare?" e' gia' stata chiesta in cima alla
            // funzione, prima di toccare qualunque stato.
            lastSurfacePath = data.filePath;
            applySurfaceExample(data);
        }
        return;
    }

    // ==========================================
    // 2. TEXTURES
    // ==========================================
    // --- SEZIONE TEXTURE ---
    QVariant vTex = item->data(0, Qt::UserRole + 1);
    if (vTex.isValid()) {
        int index = vTex.toInt();
        const LibraryItem &data = m_libraryManager.getTexture(index);

        const bool isMatch = libraryTextureIsActive(data);

        // 3. RICARICA DEL PRESET GIA' ATTIVO (nessun toggle)
        // Cliccare nella Library una texture gia' caricata significa SEMPRE
        // "rivoglio questa texture com'e' nel preset": si riavvia l'animazione e
        // si azzera la manipolazione 2D. Prima il primo click fermava l'orologio
        // e solo il secondo rigenerava; fermare l'animazione ha gia' i suoi tasti
        // dedicati (dock Script e master), quindi lo stop qui era solo un passo
        // in piu' prima del gesto utile.
        if (isMatch && targetTextureOn()) {
            restartActiveLibraryTexture(data);
            return;
        }

        // 4. Se non è un match (o se la texture era spenta), carichiamola normalmente

        // NB: NON azzeriamo m_masterStopped qui. Il clock del modulo TEXTURE
        // (setSurface/BackgroundTextureAnimating in GLWidget) parte da solo e NON
        // e' gated da m_masterStopped, quindi una texture animata si anima comunque.
        // Azzerare il flag globale sbloccava invece anche la GEOMETRIA: dopo un
        // master STOP, caricare una texture di sfondo e poi spegnerla faceva
        // RIPARTIRE la superficie (quando 't' e' nelle composizioni defU/defV/defW).
        // Caricare una texture tocca SOLO il modulo texture.

        handleTextureSelection(index);

        // FIX 2: Rimosso il blocco if/else che forzava setSurfaceTextureAnimating(true).
        // handleTextureSelection() sa già calcolare perfettamente se serve l'animazione
        // e imposta tutti i flag necessari in modo coerente per Parametric e Ray Marching.

        updateMasterButtonState();
        return;
    }

    // ==========================================
    // 3. RECORDS (MOTIONS)
    // ==========================================
    QVariant vMot = item->data(0, Qt::UserRole + 2);
    if (vMot.isValid()) {
        int index = vMot.toInt();

        // Come nel ramo SURFACES qui sopra: l'indice posizionale e' fragile.
        // Dopo un refresh (salvataggio, o modifica della cartella da Finder) la
        // lista m_motions viene ricostruita e l'ordine puo' cambiare, mentre il
        // nodo dell'albero conserva l'indice VECCHIO -> si caricava un record
        // diverso da quello cliccato. Il tooltip del nodo porta sempre il
        // filePath univoco: si cerca per quello, con fallback all'indice se il
        // tooltip fosse vuoto.
        const QString nodePath = item->toolTip(0);
        const LibraryItem *byPath = nodePath.isEmpty() ? nullptr
                                    : m_libraryManager.getMotionByPath(nodePath);
        const LibraryItem &data = byPath ? *byPath : m_libraryManager.getMotion(index);

        if (!data.name.isEmpty()) {
            // NESSUN TOGGLE: cliccare un record nella Library significa sempre
            // "riparti da questo preset dall'inizio", anche se e' gia' quello
            // attivo e in movimento. Prima il primo click lo metteva in pausa
            // (m_btnStart->click()) e solo il secondo ricaricava; per fermare il
            // moto ci sono gia' i tasti dedicati (START/STOP e master), quindi
            // la pausa qui era solo un passo in piu' prima del gesto utile.
            // Non serve fermare prima: applyMotionExample apre con lo STOP TOTALE
            // (pauseMotion, stopAll, path fermati, tempo dei path azzerato).
            // La conferma "vuoi salvare?" e' gia' stata chiesta in cima alla
            // funzione, prima di toccare qualunque stato.
            applyMotionExample(data);
        }
        return;
    }
}

void MainWindow::deleteSelectedExample() {
    m_fileOps->deleteSelected();
}

void MainWindow::onUndoDelete() {
    m_fileOps->undoDelete();
}

void MainWindow::onAddRepositoryClicked(bool wasRotating, bool wasPath4D,
                                        bool wasPath3D, bool wasTimeAnimating)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // Su mobile la funzione esce subito: lo stato dei moti non serve a nessuno.
    Q_UNUSED(wasRotating); Q_UNUSED(wasPath4D);
    Q_UNUSED(wasPath3D);   Q_UNUSED(wasTimeAnimating);
    QMessageBox::information(this, "Library Management",
                             "On iPhone and iPad your library is managed automatically by the system.\n"
                             "Open the iOS 'Files' app to organize your folders and presets.");
    return;
#else
    QSettings settings;
    QString currentRoot = settings.value("libraryRootPath", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();

    // Titolo: "Select Location for Presets Folder" prometteva la cosa sbagliata
    // -- suggeriva di indicare DOVE creare una cartella, mentre qui si indica LA
    // cartella della libreria, che deve gia' esistere e contenere i quattro rami.
    // RIPRISTINO DEI MOTI SU OGNI USCITA ANTICIPATA.
    //
    // Quando questa funzione arriva in fondo, i moti li rimette
    // refreshLibraryPreservingMotion dalla singleShot in coda. Ma ogni "return"
    // qui in mezzo -- dialogo annullato, cartella non valida, avviso rifiutato
    // -- salta quella singleShot, e i moti restano fermi: showMenu li ha gia'
    // fermati e non ha modo di sapere che l'azione non e' arrivata in fondo.
    // MISURATO: record in movimento, "Change Library Folder..." poi Annulla ->
    // tutto fermo, e ripartiva solo riaprendo e richiudendo il menu (la seconda
    // apertura rilegge il tasto master, ancora su "STOP", e in chiusura
    // riaccende).
    // La lambda va chiamata PRIMA di ogni return di questa funzione.
    auto restoreMotion = [this, wasRotating, wasPath4D, wasPath3D, wasTimeAnimating]() {
        restoreMotionState(wasRotating, wasPath4D, wasPath3D, wasTimeAnimating);
    };

    QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Your Library Folder", currentRoot,
                                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedPath.isEmpty()) { restoreMotion(); return; }

    // LA CARTELLA DEVE ESSERE UNA RADICE DI LIBRERIA, altrimenti non si fa nulla.
    //
    // Questo comando non installa e non crea: punta e basta. Quindi l'unica
    // cartella sensata e' una che contenga gia' almeno uno dei quattro rami.
    // Senza questo controllo si poteva eleggere a radice una cartella qualsiasi
    // -- compreso un RAMO della libreria stessa: refreshRepositories cerca poi
    // i rami come radice+"/records" ecc., che li' dentro non esistono, e la
    // libreria appariva VUOTA senza dire perche'. MISURATO scegliendo
    // .../presets/records: quattro alberi vuoti e nessun messaggio.
    // Si avvisa e si esce lasciando la radice attuale INTATTA: nessuna
    // cartella creata, nessun preset installato, niente da disfare.
    if (!LibraryFolders::isLibraryRoot(QDir(QDir::cleanPath(selectedPath)))) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Not a Library Folder");
        box.setText("This folder is not a library.");
        box.setInformativeText(
            QDir::cleanPath(selectedPath) + "\n\n"
            "A library folder contains the surfaces, textures, records and sounds "
            "folders. Choose the folder that contains them — not one of them.\n\n"
            "Nothing has been changed: your library still points to its current folder.");
        box.exec();
        restoreMotion();          // uscita anticipata: vedi restoreMotion
        return;
    }

    // CARTELLA SINCRONIZZATA: si avvisa, ma il testo e' diverso da quello del
    // primo avvio. Li' la libreria si sta per CREARE e il consiglio e' scegliere
    // altrove; qui esiste gia' in quella cartella, quindi il punto non e' dove
    // metterla ma sapere che i suoi preset possono sparire dal disco -- e che
    // l'avviso "N preset non caricati" avra' quell'origine.
    if (LibraryFolders::isCloudSynced(selectedPath)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Library Synced to the Cloud");
        box.setText("This library is stored in a folder synced to iCloud Drive.");
        box.setInformativeText(
            QDir::cleanPath(selectedPath) + "\n\n"
            "macOS may remove the local copies of these presets to free up space. "
            "Those that are not on this Mac cannot be opened, and Surface Explorer "
            "will report them as missing until they are downloaded again "
            "(in Finder: right-click → Download Now).\n\n"
            "Do you want to use this folder anyway?");
        box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        box.button(QMessageBox::Ok)->setText("Use This Folder");
        box.setDefaultButton(QMessageBox::Ok);
        // Predefinito su Ok, al contrario del primo avvio: qui l'utente ha
        // indicato una libreria che esiste gia' e sa cosa contiene -- il piu'
        // delle volte e' esattamente quella che vuole.
        // uscita anticipata: vedi restoreMotion
        if (box.exec() != QMessageBox::Ok) { restoreMotion(); return; }   // radice attuale intatta
    }

    // LA CARTELLA SELEZIONATA E' LA RADICE. Punto. Nessuna trasformazione del
    // percorso: la libreria punta esattamente a quello che l'utente ha indicato,
    // sia essa uguale o diversa dalla radice attuale.
    //
    // Qui si passava da LibraryFolders::resolveRoot, che e' fatta per il PRIMO AVVIO --
    // dove la domanda "dove installo la libreria?" e' legittima e la risposta
    // puo' ragionevolmente essere una sottocartella. Ma questo comando fa
    // un'altra cosa: SPOSTA il puntatore su una libreria che gia' esiste, e
    // l'utente che sceglie una cartella si aspetta quella, non una sua figlia.
    // MISURATO, due varianti dello stesso danno: scegliendo .../presets/records
    // la radice e' finita prima in records/presets (vecchia LibraryFolders::resolveRoot,
    // che scendeva in una "presets" qualsiasi) e poi in records/records
    // (nuova: restituisce records, ma setupDefaultFolders ci crea dentro i
    // quattro rami e uno si chiama "records"). In entrambi i casi la libreria
    // mostrava preset di fabbrica appena reinstallati e il lavoro dell'utente,
    // rimasto un livello sopra, spariva dall'albero.
    selectedPath = QDir::cleanPath(selectedPath);

    QDir().mkpath(selectedPath);
    settings.setValue("libraryRootPath", selectedPath);
    settings.sync();                     // vedi setupDefaultFolders: cfprefsd
    SecurityBookmark::save(selectedPath);   // vedi setupDefaultFolders

    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathRecords");
    settings.remove("pathSounds");

    // NON setupDefaultFolders(): creerebbe i quattro rami dentro la cartella
    // scelta e ci reinstallerebbe i preset di fabbrica -- cioe' esattamente il
    // livello di troppo che questo comando non deve produrre. Qui la libreria
    // esiste gia': si punta e si rilegge, senza scrivere niente sul disco.
    //
    // RILETTURA RINVIATA A MENU CHIUSO (singleShot(0)): questa funzione parte da
    // una voce del menu contestuale della libreria, che su desktop viene
    // eseguita DENTRO contextMenu->exec() -- un event loop annidato, con sopra
    // quello del pannello nativo di scelta cartella. Rileggere l'intera libreria
    // in fondo a quella pila significa tenere il menu vivo per tutta la durata
    // della scansione, e qualunque finestra di sistema debba comparire nel
    // frattempo (autorizzazioni, avvisi) arriva in uno stato annidato.
    // Con il rinvio i due lavori girano a menu chiuso e a pila smontata, dal
    // loop principale. Stesso schema gia' usato dal ramo mobile in
    // librarymenucontroller.cpp.
    // NB: non e' questo il rimedio al blocco su cartella iCloud -- quello era
    // una read() sospesa sui file non scaricati, impedita alla fonte da
    // isDatalessFile (librarymanager.cpp). Qui si toglie solo fragilita'.
    // I moti li rimette a posto refreshLibraryPreservingMotion, DOPO il rebuild:
    // il ripristino in fondo a showMenu gira prima di questo singleShot, e per
    // giunta su flag che la rientranza ha falsato.
    QTimer::singleShot(0, this, [this, wasRotating, wasPath4D, wasPath3D, wasTimeAnimating]() {
        refreshLibraryPreservingMotion(wasRotating, wasPath4D, wasPath3D, wasTimeAnimating);
    });
#endif
}

void MainWindow::onCreateFolderClicked()
{
    bool ok;
    QString folderName = QInputDialog::getText(this, "New Folder", "Folder Name:", QLineEdit::Normal, "NewFolder", &ok);
    if (!ok || folderName.isEmpty()) return;

    folderName.replace("/", "_");
    folderName.replace("\\", "_");

    QString basePath;
    QTreeWidgetItem *item = getCurrentLibraryItem();
    QSettings settings;

    // A. C'è un item selezionato? Usiamo la sua cartella madre.
    if (item) {
        if (item->data(0, Qt::UserRole).isValid()) basePath = QFileInfo(m_libraryManager.getSurface(item->data(0, Qt::UserRole).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 1).isValid()) basePath = QFileInfo(m_libraryManager.getTexture(item->data(0, Qt::UserRole + 1).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 2).isValid()) basePath = QFileInfo(m_libraryManager.getMotion(item->data(0, Qt::UserRole + 2).toInt()).filePath).absolutePath();
        else if (item->data(0, Qt::UserRole + 3).isValid()) basePath = QFileInfo(m_libraryManager.getSound(item->data(0, Qt::UserRole + 3).toInt()).filePath).absolutePath();
        // Se è una cartella
        else if (item->data(0, Qt::UserRole + 10).isValid()) basePath = item->data(0, Qt::UserRole + 10).toString();
    }

    // B. Nessun item selezionato? Inseriamo nella root della categoria corretta.
    if (basePath.isEmpty()) {
        QWidget *currentTab = ui->tabWidget->currentWidget();

        // --- UNICO BLOCCO rootPath (Scopo limitato a dove serve davvero) ---
    QString rootPath = LibraryFolders::root();

        if (currentTab == ui->Texture) basePath = settings.value("pathTextures", rootPath + "/textures").toString();
        else if (currentTab == ui->Motions) basePath = settings.value("pathRecords", rootPath + "/records").toString();
        else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) basePath = settings.value("pathSounds", rootPath + "/sounds").toString();
        else basePath = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
    }

    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        QMessageBox::warning(this, "Warning", "The destination library folder does not exist. Try resetting the Library.");
        return;
    }

    if (baseDir.mkdir(folderName)) {
        refreshRepositories();
        updateWatcherPaths();
    } else {
        QMessageBox::critical(this, "Error", "Could not create folder. A folder with this name might already exist.");
    }
}

QString MainWindow::renameLibraryPath(const QString &path, QString newName, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return QString(); };

    const QFileInfo fi(path);
    if (!fi.exists()) return fail(QStringLiteral("The item no longer exists on disk."));
    const bool isDir = fi.isDir();

    newName = newName.trimmed();
    // Come "Create New Folder": un separatore farebbe un percorso, non un nome.
    newName.replace(QLatin1Char('/'), QLatin1Char('_'));
    newName.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (newName.isEmpty()) return fail(QStringLiteral("The name is empty."));
    // Per i FILE il nome mostrato e' QFileInfo::baseName(), che si ferma al primo
    // punto (LibraryManager): "Torus v1.2" comparirebbe come "Torus v1".
    if (!isDir && newName.contains(QLatin1Char('.')))
        return fail(QStringLiteral("A file name cannot contain a dot: the Library would show it cut at the dot."));

    const QString suffix = isDir || fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    const QString newPath = fi.absolutePath() + QLatin1Char('/') + newName + suffix;
    if (newPath == path) return fail(QString());          // stesso nome: niente da fare
    // Un altro elemento con quel nome. Sui dischi che non distinguono maiuscole e
    // minuscole "torus" esiste gia' quando si rinomina "Torus": e' lo stesso file,
    // e cambiare solo le maiuscole e' una rinomina legittima.
    const bool sameIgnoringCase = newPath.compare(path, Qt::CaseInsensitive) == 0;
    if (QFileInfo::exists(newPath) && !sameIgnoringCase)
        return fail(QStringLiteral("An item named \"%1\" already exists here.").arg(newName + suffix));

    const bool ok = isDir ? QDir().rename(path, newPath) : QFile::rename(path, newPath);
    if (!ok) return fail(QStringLiteral("The item could not be renamed (permissions, or the file is in use)."));

    remapLibraryPaths(path, newPath, isDir);

    if (error) error->clear();
    return newPath;
}

void MainWindow::remapLibraryPaths(const QString &oldPathRef, const QString &newPathRef, bool isDir)
{
    // COPIE: l'Incolla passa un elemento di m_cutFilePaths, che qui sotto viene
    // riscritto -- per riferimento, oldPath cambierebbe a meta' funzione.
    const QString oldPath = QDir::cleanPath(oldPathRef);
    const QString newPath = newPathRef;
    // La voce stessa o, per una cartella, cio' che sta dentro di lei.
    auto remap = [&](QString &p) {
        if (p.isEmpty()) return;
        if (QDir::cleanPath(p) == oldPath) p = newPath;
        else if (isDir && LibraryFileOperations::isSameOrInside(p, oldPath))
            p = newPath + QDir::cleanPath(p).mid(oldPath.length());
    };
    remap(m_currentRecordPath);
    remap(m_currentTexturePresetPath);
    for (QString &p : m_cutFilePaths) remap(p);
    for (QString &p : m_cutTexturePaths) remap(p);
}

void MainWindow::refreshAndFocusPaths(const QStringList &paths)
{
    refreshRepositories();
    QSet<QString> wanted;
    for (const QString &p : paths) wanted.insert(QDir::cleanPath(p));
    if (wanted.isEmpty()) return;
    bool focused = false;
    for (QTreeWidget *tree : { ui->treeSurfaces, ui->treeTextures, ui->treeMotions, ui->treeSounds }) {
        bool cleared = false;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const QVariant dir = (*it)->data(0, Qt::UserRole + 10);
            const QString p = dir.isValid() ? dir.toString() : (*it)->toolTip(0);
            if (p.isEmpty() || !wanted.contains(QDir::cleanPath(p))) continue;
            if (!cleared) { tree->clearSelection(); cleared = true; }
            if (!focused) { LibraryTreeFocus::focus(tree, *it); focused = true; }
            else (*it)->setSelected(true);
        }
    }
}

void MainWindow::renameLibraryItem(QTreeWidgetItem *item)
{
    if (!item) return;
    QTreeWidget *tree = item->treeWidget();

    // Il percorso della voce, come lo leggono Copia e Taglia.
    QString path;
    if (item->data(0, Qt::UserRole + 10).isValid())     path = item->data(0, Qt::UserRole + 10).toString();
    else if (item->data(0, Qt::UserRole).isValid())     path = m_libraryManager.getSurface(item->data(0, Qt::UserRole).toInt()).filePath;
    else if (item->data(0, Qt::UserRole + 1).isValid()) path = m_libraryManager.getTexture(item->data(0, Qt::UserRole + 1).toInt()).filePath;
    else if (item->data(0, Qt::UserRole + 2).isValid()) path = m_libraryManager.getMotion(item->data(0, Qt::UserRole + 2).toInt()).filePath;
    else if (item->data(0, Qt::UserRole + 3).isValid()) path = m_libraryManager.getSound(item->data(0, Qt::UserRole + 3).toInt()).filePath;
    if (path.isEmpty() || !tree) return;

    const QFileInfo fi(path);
    const bool isDir = fi.isDir();

    // Immagini e audio si citano per NOME: un record ritrova //IMG: e //MUSIC:
    // cercando il nome del file nella libreria, quindi dopo la rinomina non li
    // trova piu'. Le cartelle no: la ricerca guarda in tutte le sottocartelle.
    const QString ext = fi.suffix().toLower();
    const bool citedByName = !isDir && ext != QLatin1String("json");
    if (citedByName) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Rename"));
        box.setText(QStringLiteral("Records and textures use this file by its name."));
        box.setInformativeText(QStringLiteral("After renaming it they will not find it any more. Rename anyway?"));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) return;
    }

    bool ok = false;
    const QString current = isDir ? fi.fileName() : fi.completeBaseName();
    const QString newName = QInputDialog::getText(this, QStringLiteral("Rename"),
                                                  isDir ? QStringLiteral("Folder name:") : QStringLiteral("Name:"),
                                                  QLineEdit::Normal, current, &ok);
    if (!ok) return;

    QString error;
    const QString newPath = renameLibraryPath(path, newName, &error);
    if (newPath.isEmpty()) {
        if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Rename"), error);
        return;
    }

    // Library riletta con la voce rinominata selezionata, come dopo un Save.
    if (!isDir) {
        refreshAndSelectPreset(tree, newPath);
    } else {
        refreshRepositories();
        updateWatcherPaths();
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole + 10).toString() != newPath) continue;
            tree->clearSelection();
            LibraryTreeFocus::focus(tree, *it);
            break;
        }
    }
}

void MainWindow::onSyncPresetsClicked()
{
    QSettings settings;

    // 1. AMNESIA FORZATA: Cancelliamo le vecchie configurazioni sballate
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathMotions");
    settings.remove("pathRecords");
    settings.remove("pathSounds");

    // 2. PERCORSO DINAMICO (La chiave per iOS!)
    QString rootPath = LibraryFolders::root();

    // VIA DI RIENTRO dopo un pannello annullato: questo tasto e' l'unico modo di
    // installare la libreria quando non c'e', quindi deve CHIEDERE la cartella.
    //
    // Non basta il controllo su isEmpty(): la radice puo' essere valorizzata e
    // puntare a una cartella che non esiste piu' (annullamento precedente,
    // cartella spostata o cancellata, disco scollegato). In quel caso si cadeva
    // sui mkpath qui sotto, che la ricreavano dal nulla in un posto che l'utente
    // non aveva scelto -- e sotto sandbox non sarebbe stata nemmeno autorizzata.
    // isAccessible (non QDir::exists) perche' sotto sandbox una cartella puo'
    // esistere e non essere raggiungibile: fuori dalla sandbox e' esattamente
    // QDir::exists().
    if (rootPath.isEmpty() || !SecurityBookmark::isAccessible(rootPath)) {
        setupDefaultFolders();   // chiede la cartella, installa e fa il refresh
        return;
    }

    auto reply = QMessageBox::question(this, "Restore Presets",
                                       "Do you want to restore the factory presets?\n"
                                       "This will organize your Library into the correct folders.",
                                       QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) return;

    // 3. PERCORSI ASSOLUTI
    QString pathSurf = rootPath + "/surfaces";
    QString pathTex  = rootPath + "/textures";
    QString pathRec  = rootPath + "/records";
    QString pathSnd  = rootPath + "/sounds";

    settings.setValue("pathSurfaces", pathSurf);
    settings.setValue("pathTextures", pathTex);
    settings.setValue("pathRecords", pathRec);
    settings.setValue("pathSounds", pathSnd);

    // 4. CREAZIONE FISICA CARTELLE
    QDir().mkpath(pathSurf);
    QDir().mkpath(pathTex);
    QDir().mkpath(pathRec);
    QDir().mkpath(pathSnd);

    // 5. ESTRAZIONE RICORSIVA
    int overwriteState = 0; // 0 = Chiedi, 1 = Yes to All, 2 = No to All

    LibraryFolders::syncResources(":/library/presets/surfaces", pathSurf, true, &overwriteState, this);
    LibraryFolders::syncResources(":/library/presets/textures", pathTex, true, &overwriteState, this);
    LibraryFolders::syncResources(":/library/presets/records", pathRec, true, &overwriteState, this);
    LibraryFolders::syncResources(":/library/presets/sounds", pathSnd, true, &overwriteState, this);

    refreshRepositories();
    updateWatcherPaths();
    QMessageBox::information(this, "Completed", "Library successfully updated and repaired!");
}


// ==========================================================
// FILE I/O & CLIPBOARD
// ==========================================================

void MainWindow::saveSurfaceToFile(const QString &suggestedPath) {
    m_presetSerializer->saveSurface(suggestedPath);
}

void MainWindow::onPasteExample(const QString &destDirOverride) {
    m_fileOps->performPasteExample(destDirOverride);
}

void MainWindow::onPasteTexture(const QString &destDirOverride) {
    m_fileOps->performPasteTexture(destDirOverride);
}

void MainWindow::performCut(QTreeWidgetItem* targetItem) {
    m_fileOps->performCut(targetItem);
}

void MainWindow::performCopy(QTreeWidgetItem* targetItem) {
    m_fileOps->performCopy(targetItem);
}

void MainWindow::onSaveTextureClicked()
{
    QSettings settings;
    QString rootPath = settings.value("libraryRootPath").toString();

    // Cartella di partenza: ultima usata per le texture, con fallback a /textures
    // (stessa logica di PresetSerializer::saveScript()).
    QString startDir = settings.value("lastCustomTexDir").toString();
    if (startDir.isEmpty() || startDir.contains("build", Qt::CaseInsensitive) || !QDir(startDir).exists()) {
        startDir = settings.value("pathTextures", rootPath + "/textures").toString();
    }

    // Stesso dialog navigabile del dock Library: MobileSaveDialog su mobile,
    // QFileDialog su desktop. saveTextureAs() poi chiama saveTexture().
    m_presetSerializer->saveTextureAs(startDir, m_currentTexturePresetPath);
}

void MainWindow::onSaveScriptClicked() {
    m_presetSerializer->saveScript();
}

void MainWindow::onSaveMotionClicked() {
    m_presetSerializer->saveMotion();
}


// ==========================================================
// AUDIO & MEDIA
// ==========================================================

void MainWindow::onSoundItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
#endif

    QVariant vSound = item->data(0, Qt::UserRole + 3);
    if (!vSound.isValid()) return;

    int index = vSound.toInt();
    const LibraryItem &soundData = m_libraryManager.getSound(index);

    // 1. AGGIUNGIAMO LA FUNZIONE DI PULIZIA QUI
    auto cleanAudioCode = [](QString str) {
        str.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
        str.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));
        str.replace(QRegularExpression("\\s+"), "");
        return str;
    };

    QString audioSnippet;
    bool isAlreadyPresent = false;

    bool isMedia = soundData.filePath.endsWith(".mp3", Qt::CaseInsensitive) ||
            soundData.filePath.endsWith(".wav", Qt::CaseInsensitive) ||
            soundData.filePath.endsWith(".ogg", Qt::CaseInsensitive);

    if (isMedia) {
        audioSnippet = "//MUSIC: " + soundData.filePath;
        isAlreadyPresent = m_scene.soundScriptText.contains(soundData.filePath);
    } else {
        audioSnippet = "//SOUND_BEGIN\n" + soundData.scriptCode.trimmed() + "\n//SOUND_END";

        // 2. MODIFICHIAMO SOLO QUESTA RIGA PER GLI SCRIPT:
        // Usiamo la pulizia per confrontare il codice in memoria con quello della libreria
        isAlreadyPresent = (cleanAudioCode(m_scene.soundScriptText) == cleanAudioCode(soundData.scriptCode));
    }
    // 3. SUONO GIA' CARICATO: il click lo TOGLIE (secondo click: riparte).
    // E' il solo modo di togliere il suono a un record che ne ha uno (prima si
    // poteva solo fermarlo, o salvare senza). Il suono torna a un click di
    // distanza: toltolo, la voce non e' piu' "gia' presente" e il click
    // successivo lo carica e lo fa suonare dall'inizio.
    if (isAlreadyPresent) {
        m_audioController->stopAll();
        // Anche le copie nei codici delle texture, come nel ramo che lo sostituisce.
        for (QString *code : { &m_scene.surfaceTextureScriptText, &m_scene.bgTextureScriptText,
                               &m_scene.surfaceTextureCode, &m_scene.bgTextureCode }) {
            *code = stripAudioDirectives(*code);
        }
        setScriptText(SlotSound, QString());
        m_scene.soundLibName.clear();
        refreshScriptEditor();
        // La scena e' cambiata rispetto al suo file: lo conserva il record.
        markSoundPicked();
        if (QTreeWidget *tree = ui->treeSounds) {
            const QSignalBlocker blocker(tree);
            tree->clearSelection();
            tree->setCurrentItem(nullptr);
        }
        updateScriptButtonText();
        updateMasterButtonState();
        return;
    }

    // Da qui in poi il suono corrente viene SOSTITUITO: si chiede prima cosa
    // fare del lavoro non salvato, come per superfici e record. La domanda
    // arriva dopo il ramo "suono gia' caricato" qui sopra, che si limita a
    // rigenerarlo e non scarta nulla, e prima di qualunque modifica di stato:
    // su Cancel non deve cambiare niente. Si perde il solo SUONO, non la
    // scena: popup del modulo, salvataggio puntato al ramo sounds/.
    if (!confirmDiscardUnsaved(ScopeSound)) return;

    // PULIZIA ASSOLUTA: Rimuove l'audio da eventuali vecchi caricamenti spuri

    // Anche dalle copie APPLICATE, e solo il suono: una texture utente salvata
    // col suono dentro lo terrebbe in scena, e il player (che prende la prima
    // //MUSIC: ovunque) suonerebbe quello invece del suono appena scelto.
    // Prima le copie applicate si riscrivevano per intero (suono + slot dello
    // script), e uno script texture in sospeso risultava applicato.
    for (QString *code : { &m_scene.surfaceTextureScriptText, &m_scene.bgTextureScriptText,
                           &m_scene.surfaceTextureCode, &m_scene.bgTextureCode }) {
        *code = stripAudioDirectives(*code);
    }
    refreshScriptEditor();   // gli slot qui sopra sono cambiati sotto la vista

    // AGGIORNAMENTO MEMORIA AUDIO
    setScriptText(SlotSound, audioSnippet);
    m_scene.soundLibName = soundData.name.trimmed();   // ancora del focus (vedi mainwindow.h)

    // A schermo c'e' ora un suono di libreria, non lavoro dell'utente: per il
    // MODULO suono niente piu' da proteggere (la conferma per il suono
    // PRECEDENTE e' gia' stata chiesta qui sopra). La SCENA che lo usa invece
    // e' cambiata rispetto al suo file, e l'unico che la conserva e' il record:
    // simmetrico al load di una texture da libreria (markTexturePicked).
    // Qui e non prima: dopo il ramo "isAlreadyPresent", che esce senza
    // sostituire nulla. All'uscita, a editor aggiornato.
    struct SoundPickedGuard {
        MainWindow *w;
        ~SoundPickedGuard() { w->markSoundPicked(); }
    } soundPickedGuard{this};

    // (L'editor segue da se' lo slot del suono, se e' quello che mostra.)

    m_audioController->stopAll();

    // Imposta lo stato visivo e abilita i tasti per il nuovo suono
    if (m_currentScriptMode == ScriptModeSound) {
        ui->btnRunCurrentScript->setText("Run Sound");
        ui->btnRunCurrentScript->setEnabled(true);
        ui->btnSaveScript->setEnabled(true);
    }

    onRunSoundClicked();
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

// --- Data & Initialization ---

void MainWindow::setupDefaultFolders()
{
    QSettings settings;

    QString rootPath;

#if defined(Q_OS_ANDROID)
    rootPath = "/storage/emulated/0/Documents/SurfaceExplorer_Presets";
    QDir().mkpath(rootPath);
    settings.setValue("libraryRootPath", rootPath);
#elif defined(Q_OS_IOS)
    QString docPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    rootPath = docPath + "/SurfaceExplorer_Presets";
    bool mkpathResult = QDir().mkpath(rootPath);
    settings.setValue("libraryRootPath", rootPath);
#else
    // Su Desktop leggiamo la memoria e mostriamo il popup se manca.
    // isAccessible (non QDir::exists) perche' sotto sandbox la cartella puo'
    // esistere e non essere raggiungibile: risolve prima il bookmark, e chiede
    // all'utente solo se non c'e' piu' nulla da riaprire. Fuori dalla sandbox
    // equivale esattamente al vecchio exists().
    rootPath = settings.value("libraryRootPath").toString();
    // NB: una radice salvata pari a una cartella di sistema (~/Documents e
    // simili) e' gia' stata corretta all'avvio, nel COSTRUTTORE (non in
    // showEvent, come diceva questo commento): qui arriva
    // sempre un percorso sensato. Vedi il commento esteso li'.
    if (rootPath.isEmpty() || !SecurityBookmark::isAccessible(rootPath)) {
        // Il testo diceva "A 'Presets' folder will be automatically created
        // there": non e' piu' vero e prometteva la cosa sbagliata. La libreria
        // si installa NELLA cartella indicata (vedi LibraryFolders::resolveRoot), quindi
        // il messaggio deve dire esattamente questo -- chi sceglie deve poter
        // prevedere dove finiranno i file.
        QMessageBox::information(this, "Welcome to Surface Explorer",
                                 "Choose the folder for your Library.\n"
                                 "The surfaces, textures, records and sounds folders "
                                 "will be created inside it.");

        // Cartella di PARTENZA del pannello: la home, MAI
        // QStandardPaths::DocumentsLocation. Sotto sandbox quest'ultima e' la
        // Documents PRIVATA del container -- vuota e senza rapporto con la
        // ~/Documents che l'utente conosce -- quindi il pannello si apriva su una
        // cartella vuota e spaesante. La home reale il Powerbox la mostra
        // correttamente, ed e' anche la base della destinazione predefinita se
        // l'utente non sceglie (vedi sotto): le due cose restano coerenti.
        QString defaultPath = QDir::homePath();
        QString selectedPath = QFileDialog::getExistingDirectory(this, "Select Master Folder", defaultPath);

        // PANNELLO CHIUSO SENZA SCEGLIERE: non si installa nulla, su NESSUN canale.
        //
        // Nessun fallback e' difendibile, e sotto sandbox non ne esiste uno che
        // funzioni: qualunque cartella l'app scelga da se' e' o non autorizzata
        // (fuori dal container il bookmark si salva ma non concede nulla) o
        // invisibile all'utente. E nell'invisibile ci si finiva senza volerlo:
        // sotto sandbox QDir::homePath() NON e' /Users/<nome> ma la home del
        // container, quindi il vecchio fallback installava la libreria in
        // .../Containers/<UUID>/Data/SurfaceExplorer_Presets -- popolata e
        // funzionante, ma introvabile dal Finder e impossibile da riempire coi
        // propri preset (MISURATO sul bundle sandboxed).
        //
        // Anche fuori dalla sandbox il fallback faceva danni: creava
        // ~/SurfaceExplorer_Presets in silenzio e la registrava come radice, da
        // cui le "librerie fantasma" e i salvataggi che finivano in una cartella
        // mentre l'utente ne guardava un'altra.
        //
        // Un solo comportamento su DMG e App Store: si dice che non e' stato
        // installato niente e come rimediare. La libreria resta vuota -- stato
        // legittimo e reversibile, non un guasto.
        if (selectedPath.isEmpty()) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle("Library Not Installed");
            box.setText("No folder was selected, so nothing was installed.");
            box.setInformativeText(
                "Your library is empty for now — nothing has been created or changed.\n\n"
                "To install the presets, click 'Restore Factory Presets' in the Library "
                "panel: it will ask you where to keep them.");
            box.exec();
            return;
        }

        // CARTELLA SINCRONIZZATA: si avvisa PRIMA di installarci la libreria.
        //
        // E' il punto in cui il problema nasce, e l'unico in cui costa un solo
        // messaggio: dopo, una libreria dentro iCloud produce l'avviso "N preset
        // non caricati" a ogni avvio, e l'utente non ha piu' modo di collegarlo
        // alla scelta fatta settimane prima.
        // Il caso non e' raro: il pannello si apre sulla home e "Documenti" e' la
        // scelta naturale, ma con "Scrivania e Documenti in iCloud" -- attiva per
        // impostazione predefinita quando si abilita iCloud Drive -- quella
        // cartella E' iCloud. I preset appena installati restano leggibili
        // finche' il sistema non li rimuove per fare spazio; da li' in poi non lo
        // sono piu'.
        // Si AVVISA e si lascia decidere: la cartella puo' essere quella giusta
        // per chi vuole la libreria su piu' Mac ed e' disposto a tenerla
        // scaricata. Imporre un rifiuto sarebbe sbagliato.
        if (LibraryFolders::isCloudSynced(selectedPath)) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle("Folder Synced to the Cloud");
            box.setText("This folder is synced to iCloud Drive.");
            box.setInformativeText(
                QDir::cleanPath(selectedPath) + "\n\n"
                "Your presets would be uploaded to the cloud, and macOS may later "
                "remove the local copies to free up space. When that happens they "
                "cannot be opened until they are downloaded again, and Surface "
                "Explorer will report them as missing.\n\n"
                "Choose a folder outside iCloud Drive, Desktop and Documents to "
                "avoid it — or keep this one, if you want the library on several "
                "Macs and will keep the files downloaded.");
            box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            box.button(QMessageBox::Ok)->setText("Use This Folder");
            box.button(QMessageBox::Cancel)->setText("Choose Another");
            box.setDefaultButton(QMessageBox::Cancel);
            if (box.exec() != QMessageBox::Ok) {
                // Si torna al pannello, dalla stessa cartella: cosi' l'utente
                // vede dove si trovava e puo' spostarsi, invece di ripartire
                // dalla home e dover ritrovare la strada.
                selectedPath = QFileDialog::getExistingDirectory(this, "Select Master Folder",
                                                                 selectedPath);
                if (selectedPath.isEmpty()) return;   // come la rinuncia qui sopra
            }
        }

        // CARTELLA CHE E' GIA' UNA LIBRERIA: si avverte prima di adottarla.
        //
        // E' l'unico caso in cui NON si crea la sottocartella "presets": i
        // quattro rami ci sono gia', appendere un livello produrrebbe una
        // libreria annidata dentro quella esistente -- il difetto che la regola
        // ha sempre dovuto impedire.
        // Ma la differenza va DETTA: negli altri casi l'utente ottiene una
        // "presets" nuova, qui invece l'applicazione si insedia in una cartella
        // che contiene gia' del lavoro, e i preset di fabbrica mancanti vengono
        // installati li' dentro. Senza avviso, chi sceglie una cartella per
        // sbaglio non ha modo di accorgersene prima che sia fatta.
        if (LibraryFolders::isLibraryRoot(QDir(QDir::cleanPath(selectedPath)))) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle("Folder Already Contains a Library");
            box.setText("This folder already contains a library.");
            box.setInformativeText(
                QDir::cleanPath(selectedPath) + "\n\n"
                "It will be used as it is: no \"presets\" folder will be created "
                "inside it, and the presets already there are kept. Any factory "
                "preset that is missing will be added.\n\n"
                "Do you want to use this folder?");
            box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            box.button(QMessageBox::Ok)->setText("Use This Library");
            box.button(QMessageBox::Cancel)->setText("Choose Another");
            box.setDefaultButton(QMessageBox::Ok);
            if (box.exec() != QMessageBox::Ok) {
                selectedPath = QFileDialog::getExistingDirectory(this, "Select Master Folder",
                                                                 selectedPath);
                if (selectedPath.isEmpty()) return;   // come la rinuncia qui sopra
            }
        }

        // DOVE FINISCE LA LIBRERIA. Unico punto che decide: LibraryFolders::resolveRoot.
        //  - cartella che e' gia' una libreria -> si prende com'e' (vedi sopra);
        //  - cartella che ne contiene una in "presets" -> si scende li';
        //  - qualunque altra -> si crea "<scelta>/presets" e la libreria va li',
        //    invece di rovesciare i quattro rami nella cartella indicata.
        rootPath = LibraryFolders::resolveRoot(selectedPath);
        selectedPath = QDir::cleanPath(selectedPath);

        QDir().mkpath(rootPath);
        settings.setValue("libraryRootPath", rootPath);
        // FLUSH IMMEDIATO, non alla distruzione dell'oggetto.
        //
        // Misurato: senza questo la radice appena scelta non sopravviveva alla
        // sessione -- all'avvio dopo `libraryRootPath` risultava assente, il gate
        // qui sotto la leggeva vuota e l'utente si ritrovava la domanda "dove
        // installo la libreria" a ogni apertura, con i preset regolarmente
        // presenti su disco. Su macOS le preferenze passano da cfprefsd, che
        // decide LUI quando scrivere: il valore resta in memoria e una
        // terminazione che non lo aspetta lo perde. sync() lo forza subito, ed e'
        // il momento giusto per farlo -- l'utente ha appena scelto la cartella e
        // tutto il resto (creazione rami, estrazione risorse) dipende da questo
        // valore.
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            qWarning() << "Libreria: impossibile salvare la radice" << rootPath
                       << "-- status" << settings.status();
        }
        // Bookmark SUBITO dopo il pannello di sistema: e' l'unico momento in cui
        // sotto sandbox abbiamo il diritto su questa cartella. Senza, al prossimo
        // avvio il percorso verrebbe risolto dentro il container (vuoto) e la
        // libreria tornerebbe vuota. No-op fuori dalla sandbox.
        //
        // Si bookmarka ANCHE la cartella che l'utente ha realmente indicato nel
        // pannello, non solo la "presets" creata da noi qui dentro: sotto
        // sandbox il diritto nasce dalla SCELTA dell'utente, e una sottocartella
        // creata dal codice puo' non essere coperta. Avendo il bookmark del
        // genitore, i figli ne ereditano l'accesso e il ramo resta raggiungibile
        // anche se quello della sottocartella non si risolve.
        if (!selectedPath.isEmpty()) SecurityBookmark::save(selectedPath);
        SecurityBookmark::save(rootPath);
    }
#endif

    // --- CREAZIONE DELLE 4 SOTTOCARTELLE FISSE ---
    QString surfDirUser = rootPath + "/surfaces";
    QString texDirUser  = rootPath + "/textures";
    QString recDirUser  = rootPath + "/records";
    QString sndDirUser  = rootPath + "/sounds";

    QDir().mkpath(surfDirUser);
    QDir().mkpath(texDirUser);
    QDir().mkpath(recDirUser);
    QDir().mkpath(sndDirUser);

    // --- ESTRAZIONE RISORSE ---
    LibraryFolders::syncResources(":/library/presets/surfaces", surfDirUser, false, nullptr, this);
    LibraryFolders::syncResources(":/library/presets/textures", texDirUser, false, nullptr, this);
    LibraryFolders::syncResources(":/library/presets/records", recDirUser, false, nullptr, this);
    LibraryFolders::syncResources(":/library/presets/sounds", sndDirUser, false, nullptr, this);

    // --- AMNESIA FORZATA: PULIZIA VECCHIA MEMORIA ---
    settings.remove("pathSurfaces");
    settings.remove("pathTextures");
    settings.remove("pathMotions");
    settings.remove("pathRecords");
    settings.remove("pathSounds");
    settings.remove("repoPathsSurfaces");
    settings.remove("repoPathsTextures");
    settings.remove("repoPathsMotions");
    settings.remove("repoPathsSounds");
    settings.remove("repositoryPaths");

    refreshRepositories();
    updateWatcherPaths();
}

// --- Library & File I/O ---


void MainWindow::refreshRepositories()
{
    // REBUILD IN CORSO: vedi libraryRebuildInProgress() in mainwindow.h.
    // Ricostruire gli alberi fa riemettere customContextMenuRequested e rientra
    // in showMenu; con questo flag alzato quel rientro non tocca i moti, quindi
    // non puo' spegnerli leggendone lo stato da timer gia' fermi.
    // Struct con distruttore invece di un decremento a fine funzione: qui sotto
    // ci sono piu' vie d'uscita, e una sola dimenticata lascerebbe il flag
    // alzato per sempre -- da quel momento il menu non fermerebbe piu' i moti.
    struct RebuildGuard {
        int &d;
        explicit RebuildGuard(int &depth) : d(depth) { ++d; }
        ~RebuildGuard() { --d; }
    } rebuildGuard(m_libraryRebuildDepth);

    if (m_fsWatcher) m_fsWatcher->blockSignals(true);

    // 0. Blocchiamo i segnali della UI per non innescare eventi a catena durante la pulizia
    ui->treeSurfaces->blockSignals(true);
    ui->treeTextures->blockSignals(true);
    ui->treeMotions->blockSignals(true);
    ui->treeSounds->blockSignals(true);

    // 1. LAMBDA AVANZATA: Ricostruisce il percorso completo dell'albero (es. "Cartella/MioFile")
    auto getItemPath = [](QTreeWidgetItem* item) -> QString {
        QString path = item->text(0);
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            path = parent->text(0) + "/" + path;
            parent = parent->parent();
        }
        return path;
    };

    // 2. SALVATAGGIO STATO ESPANSIONE E SELEZIONE (Ora basato sul percorso univoco)
    QSet<QString> expSurfaces, expTextures, expMotions, expSounds;
    QSet<QString> selSurfaces, selTextures, selMotions, selSounds;

    auto saveState = [&](QTreeWidget* tree, QSet<QString>& expSet, QSet<QString>& selSet) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QString path = getItemPath(*it);
            if ((*it)->isExpanded()) expSet.insert(path);
            if ((*it)->isSelected()) selSet.insert(path);
            ++it;
        }
    };

    saveState(ui->treeSurfaces, expSurfaces, selSurfaces);
    saveState(ui->treeTextures, expTextures, selTextures);
    saveState(ui->treeMotions, expMotions, selMotions);
    saveState(ui->treeSounds, expSounds, selSounds);

    // 3. SALVATAGGIO POSIZIONE BARRE DI SCORRIMENTO (Impedisce il salto visivo)
    int scrollSurfaces = ui->treeSurfaces->verticalScrollBar()->value();
    int scrollTextures = ui->treeTextures->verticalScrollBar()->value();
    int scrollMotions  = ui->treeMotions->verticalScrollBar()->value();
    int scrollSounds   = ui->treeSounds->verticalScrollBar()->value();

    // 4. PULIZIA SICURA
    ui->treeSurfaces->clearSelection();
    ui->treeTextures->clearSelection();
    ui->treeMotions->clearSelection();
    ui->treeSounds->clearSelection();

    ui->treeSurfaces->clear();
    ui->treeTextures->clear();
    ui->treeMotions->clear();
    ui->treeSounds->clear();
    m_libraryManager.clear();
    // Gli item appena distrutti: il puntatore all'ultimo preset caricato
    // resterebbe pendente (QTreeWidgetItem non e' un QObject, niente QPointer
    // che lo azzeri da solo).
    m_lastLoadedLibraryItem = nullptr;

    // 5. CARICAMENTO DAL FILE SYSTEM
    QSettings settings;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/SurfaceExplorer_Presets";
#else
    QString rootPath = settings.value("libraryRootPath").toString();
#endif

    QString pathSurf = settings.value("pathSurfaces", rootPath + "/surfaces").toString();
    QString pathTex  = settings.value("pathTextures", rootPath + "/textures").toString();
    QString pathRec  = settings.value("pathRecords",  rootPath + "/records").toString();
    QString pathSnd  = settings.value("pathSounds",   rootPath + "/sounds").toString();

    // Riapre l'accesso alla radice PRIMA di leggere i rami: sotto sandbox, senza
    // questo, i quattro exists() qui sotto sono tutti falsi (il percorso viene
    // risolto nella Documents PRIVATA del container) e la libreria appare vuota
    // senza alcun errore. Bastano la radice e i figli ne ereditano l'accesso.
    // No-op fuori dalla sandbox.
    if (!rootPath.isEmpty()) SecurityBookmark::restore(rootPath);

    if (QDir(pathSurf).exists()) m_libraryManager.loadFromDirectory(pathSurf, ui->treeSurfaces, LibraryType::Surface);
    if (QDir(pathTex).exists())  m_libraryManager.loadFromDirectory(pathTex,  ui->treeTextures, LibraryType::Texture);
    if (QDir(pathRec).exists())  m_libraryManager.loadFromDirectory(pathRec,  ui->treeMotions,  LibraryType::Motion);
    if (QDir(pathSnd).exists())  m_libraryManager.loadFromDirectory(pathSnd,  ui->treeSounds,   LibraryType::Sound);

    // PRESET SOLO IN CLOUD: dirlo, invece di mostrare una libreria incompleta.
    // Sono i file che loadFromDirectory ha saltato perche' non materializzati su
    // questo disco (vedi isDatalessFile in librarymanager.cpp): leggerli
    // bloccherebbe l'applicazione. Saltarli in silenzio riprodurrebbe pero' il
    // sintomo storico "mancano dei preset e non si capisce perche'".
    //
    // Avviso RINVIATO e NON ripetuto:
    //  - singleShot perche' refreshRepositories parte anche dal costruttore
    //    (finestra non ancora visibile) e dal fsWatcher, dentro la gestione di
    //    un evento: un box modale qui bloccherebbe il chiamante;
    //  - il flag perche' il watcher puo' far ripartire il refresh molte volte
    //    di fila sulla stessa cartella, e ne uscirebbe una raffica di popup.
    //    Si riarma da solo quando la libreria torna completa.
    {
        const int skipped = m_libraryManager.skippedDataless().size();
        if (skipped > 0 && !m_datalessWarningShown) {
            m_datalessWarningShown = true;
            const QString sample = m_libraryManager.skippedDataless().first();
            QTimer::singleShot(0, this, [this, skipped, sample]() {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle("Presets Only in the Cloud");
                box.setText(QString("%1 presets could not be loaded.").arg(skipped));
                box.setInformativeText(
                    "They are stored in a synced folder (iCloud Drive or similar) and "
                    "their contents have not been downloaded to this Mac, so opening "
                    "them would freeze the application.\n\n"
                    "In Finder, open the library folder and download the files (right-click "
                    "→ Download Now), or move your library to a folder that is not synced.\n\n"
                    "First one skipped:\n" + sample);
                box.exec();
            });
        }
        else if (skipped == 0) {
            m_datalessWarningShown = false;
        }
    }

    // 6. RIPRISTINO STATO ESPANSIONE E SELEZIONE
    auto restoreState = [&](QTreeWidget* tree, const QSet<QString>& expSet, const QSet<QString>& selSet) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QString path = getItemPath(*it);
            if (expSet.contains(path)) (*it)->setExpanded(true);
            if (selSet.contains(path)) (*it)->setSelected(true);
            ++it;
        }
    };

    restoreState(ui->treeSurfaces, expSurfaces, selSurfaces);
    restoreState(ui->treeTextures, expTextures, selTextures);
    restoreState(ui->treeMotions, expMotions, selMotions);
    restoreState(ui->treeSounds, expSounds, selSounds);

    // 7. RIPRISTINO BARRE DI SCORRIMENTO
    ui->treeSurfaces->verticalScrollBar()->setValue(scrollSurfaces);
    ui->treeTextures->verticalScrollBar()->setValue(scrollTextures);
    ui->treeMotions->verticalScrollBar()->setValue(scrollMotions);
    ui->treeSounds->verticalScrollBar()->setValue(scrollSounds);

    // 8. RIATTIVIAMO I SEGNALI
    ui->treeSurfaces->blockSignals(false);
    ui->treeTextures->blockSignals(false);
    ui->treeMotions->blockSignals(false);
    ui->treeSounds->blockSignals(false);

    if (m_fsWatcher) m_fsWatcher->blockSignals(false);

    // Il refresh ha ricostruito gli item (colore di default) e riespanso le
    // cartelle via restoreState: se siamo in surface-wireframe riapplichiamo
    // collasso + grigio, altrimenti l'albero apparirebbe attivo pur essendo
    // la texture non applicabile.
    if (m_textureLibraryGrayed) setTextureLibraryGrayed(true);
}

void MainWindow::refreshAndSelectPreset(QTreeWidget *tree, const QString &path)
{
    // Salvataggio fatto DENTRO la conferma "vuoi salvare?", mentre si sta
    // caricando un altro preset: la libreria va aggiornata (il file nuovo deve
    // comparire) ma il FOCUS no. Questa funzione e' chiamata a 100ms dai
    // writer, quindi scatta a caricamento gia' avvenuto e sposterebbe
    // l'evidenziazione dal preset appena caricato al file appena salvato --
    // che non e' cio' che si sta guardando.
    if (m_suppressSelectAfterSave) {
        refreshRepositories();
        return;
    }

    refreshRepositories();
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->toolTip(0) == path) {
            tree->clearSelection();
            LibraryTreeFocus::focus(tree, *it);
            break;
        }
        ++it;
    }
}

// RILETTURA DELLA LIBRERIA CHE NON SPEGNE I MOTI.
//
// IL BUG: dopo "Change Folder for <Ramo>..." e "Change Library Folder..." i moti
// restavano fermi, col tasto master che continuava a mostrare "STOP". Tutte le
// altre voci del menu li ripristinavano correttamente.
//
// PERCHE' SOLO QUELLE DUE: sono le uniche che chiamano refreshRepositories(),
// che ricostruisce i quattro alberi -- e il rebuild fa riemettere
// customContextMenuRequested, RIENTRANDO in showMenu (misurato: 4 rientri di
// fila, uno per albero).
//
// PERCHE' LA RIENTRANZA DISTRUGGE LO STATO: showMenu ferma i moti in cima, ne
// salva lo stato in variabili LOCALI e ripristina in fondo. Ma quello stato non
// e' una copia indipendente: GLWidget::isAnimating() E' rotationTimer->isActive()
// (glwidget.h), cioe' si LEGGE DAI TIMER. Il rientro esegue la stessa cattura
// quando i timer sono gia' stati fermati dalla chiamata esterna, quindi registra
// "fermo" -- e al suo ritorno non riaccende niente. Lo stato vero e' perso.
// Vedi [[rotationtimer-e-stato-non-solo-clock]]: qui un QTimer e' insieme clock
// e flag di stato.
//
// IL RIMEDIO: le due voci catturano lo stato PRIMA (in showMenu, dove i moti
// girano ancora) e lo passano qui; questa funzione rilegge la libreria e poi
// rimette i moti come stavano. Il ripristino avviene DOPO il rebuild, quindi
// nessun rientro puo' piu' interporsi fra la lettura e il ripristino.
//
// NON si e' usata una guardia di rientranza in showMenu: gia' provata e
// revertita il 2026-08-19 -- nei log sembrava risolvere, nell'uso reale i moti
// restavano fermi e in piu' il menu contestuale non si riapriva.
// RIMETTE I MOTI COME ERANO. Unica implementazione della sequenza: la usano sia
// il percorso completo (refreshLibraryPreservingMotion) sia le uscite anticipate
// delle voci di menu, che devono ripristinare SENZA ricostruire la libreria --
// annullando un dialogo non e' cambiato niente, e un rebuild sarebbe lavoro
// inutile su una libreria che puo' essere grande.
void MainWindow::restoreMotionState(bool wasRotating, bool wasPath4D,
                                    bool wasPath3D, bool wasTimeAnimating)
{
    // Stesso ordine del ripristino in fondo a showMenu, per non introdurre una
    // seconda versione della stessa sequenza.
    if (wasTimeAnimating) {
        ui->glWidget->setSurfaceAnimating(true);
        ui->glWidget->startAnimationTimer();
    }
    if (wasPath4D)  m_paths->start(CameraPaths::Path4D);
    if (wasPath3D)  m_paths->start(CameraPaths::Path3D);
    if (wasRotating) ui->glWidget->resumeMotion();
}

void MainWindow::refreshLibraryPreservingMotion(bool wasRotating, bool wasPath4D,
                                                bool wasPath3D, bool wasTimeAnimating)
{
    refreshRepositories();
    updateWatcherPaths();
    restoreMotionState(wasRotating, wasPath4D, wasPath3D, wasTimeAnimating);
}

void MainWindow::updateWatcherPaths()
{
    if (!m_fsWatcher) return;

    // Rimuove i vecchi percorsi sorvegliati
    if (!m_fsWatcher->directories().isEmpty()) m_fsWatcher->removePaths(m_fsWatcher->directories());
    if (!m_fsWatcher->files().isEmpty()) m_fsWatcher->removePaths(m_fsWatcher->files());

    // Helper per aggiungere la cartella radice e tutte le sue sottocartelle
    auto addDirsToWatcher = [this](const QString &root) {
        if (!QDir(root).exists()) return;
        m_fsWatcher->addPath(root); // Aggiunge la root

        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            m_fsWatcher->addPath(it.next()); // Aggiunge ogni sottocartella
        }
    };

    QSettings settings;

    QString rootPath = LibraryFolders::root();

    // Come in refreshRepositories: senza accesso, addDirsToWatcher esce subito
    // sul suo QDir::exists() e NIENTE viene sorvegliato, quindi le modifiche
    // fatte da Finder non aggiornerebbero piu' la libreria. No-op fuori sandbox.
    if (!rootPath.isEmpty()) SecurityBookmark::restore(rootPath);

    addDirsToWatcher(settings.value("pathSurfaces", rootPath + "/surfaces").toString());
    addDirsToWatcher(settings.value("pathTextures", rootPath + "/textures").toString());
    // I Record usano la chiave "pathRecords" come il loader (refreshRepositories):
    // "pathMotions" era una chiave divergente -> la cartella .../records non veniva
    // mai osservata, quindi un record salvato in radice non innescava il refresh.
    addDirsToWatcher(settings.value("pathRecords", rootPath + "/records").toString());
    addDirsToWatcher(settings.value("pathSounds", rootPath + "/sounds").toString());
}



void MainWindow::copyPath(QString src, QString dst) {
    m_fileOps->copyPath(src, dst);
}

QTreeWidgetItem* MainWindow::getCurrentLibraryItem() {
    QWidget* currentTab = ui->tabWidget->currentWidget();

    // Ci fidiamo SOLO della selezione esplicita e reale, ignorando il focus invisibile!
    if (currentTab == ui->Surface) {
        if (!ui->treeSurfaces->selectedItems().isEmpty()) return ui->treeSurfaces->selectedItems().first();
    }
    else if (currentTab == ui->Texture) {
        if (!ui->treeTextures->selectedItems().isEmpty()) return ui->treeTextures->selectedItems().first();
    }
    else if (currentTab == ui->Motions) {
        if (!ui->treeMotions->selectedItems().isEmpty()) return ui->treeMotions->selectedItems().first();
    }
    else if (currentTab->objectName().contains("Sound", Qt::CaseInsensitive)) {
        if (!ui->treeSounds->selectedItems().isEmpty()) return ui->treeSounds->selectedItems().first();
    }
    return nullptr;
}

void MainWindow::setTextureLibraryGrayed(bool grayed)
{
    if (!ui->treeTextures) return;

    // Entrando in wireframe chiudiamo tutte le cartelle (restano chiuse anche
    // all'uscita: nessun ripristino dello stato di espansione, per scelta).
    if (grayed) {
        ui->treeTextures->collapseAll();
    }

    // Grigio esplicito sul testo di ogni voce (cartelle e file). All'uscita
    // rimuoviamo l'override col QVariant vuoto, cosi' l'item torna al colore di
    // default della palette (tema-indipendente) invece di un grigio hardcodato.
    QTreeWidgetItemIterator it(ui->treeTextures);
    while (*it) {
        if (grayed) (*it)->setForeground(0, QBrush(Qt::gray));
        else        (*it)->setData(0, Qt::ForegroundRole, QVariant());
        ++it;
    }
}





