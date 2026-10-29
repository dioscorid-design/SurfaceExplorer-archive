#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QLabel>
#include <QColor>
#include <functional>
#include <optional>
#include <array>
#include <QButtonGroup>
#include <QProgressBar>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMap>
#include <QJsonValue>

#include "glwidget.h"
#include "scenestate.h"
#include "camerapaths.h"
#include "texturecode.h"
#include "libraryfolders.h"
#include "librarytreefocus.h"
#include "librarymanager.h"
#include "synthesizer.h"

class QLineEdit;
class QSlider;
class QPushButton;
class QCheckBox;
class QRadioButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QMenu;
class SceneHintOverlay;
class QMenuBar;
class VideoRecorder;
class LibraryMenuController;
class PresetSerializer;
class LibraryFileOperations;
class LibraryDragDropHandler;
class AudioController;
class QJsonObject;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class VideoRecorder;
    friend class LibraryMenuController;
    friend class PresetSerializer;
    friend class LibraryFileOperations;
    friend class LibraryDragDropHandler;
    friend class AudioController;
    friend class DesktopInputFilter;
    friend class MobileInputFilter;
    friend class PresetRoundTrip;   // test di andata e ritorno dei preset
    friend class ClockTest;         // test degli orologi di animazione
    friend class ScenarioTest;      // test degli scenari d'uso
    friend class ImageTest;         // test delle immagini dei preset

    // I TIPI DELLA SCENA vivono in scenestate.h (le classi estratte da
    // MainWindow li usano senza includere questa intestazione); qui i nomi
    // storici, MainWindow::SceneState & co.
    using DiscreteRange = ::DiscreteRange;
    using EquationTexts = ::EquationTexts;
    using EqField = ::EqField;
    using ImplicitTexts = ::ImplicitTexts;
    using RmField = ::RmField;
    using ConstantTexts = ::ConstantTexts;
    using ConstField = ::ConstField;
    using LimitTexts = ::LimitTexts;
    using PathTexts = ::PathTexts;
    using TubeTexts = ::TubeTexts;
    using TubeField = ::TubeField;
    using CameraPathMode = ::CameraPathMode;
    using SceneState = ::SceneState;

public:
    // CHE COSA sta per essere perso: decide sia quando chiedere sia dove il
    // dialogo di salvataggio si apre. Vedi confirmDiscardUnsaved.
    // NB: dichiarato QUI, non accanto alla funzione che lo usa: quella vive in
    // una sezione slot, dove moc rifiuta le dichiarazioni di tipo (stesso
    // motivo di RunOutcomeGuard), e un tipo usato in una firma deve comunque
    // essere gia' noto al compilatore in quel punto.
    enum DiscardScope {
        ScopeScene,     // tutta la scena (altro preset, default, NEW, cambio modalita')
        ScopeTexture,   // la sola texture (se ne sta caricando un'altra)
        ScopeSound      // il solo suono
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    AudioController *m_audioController;


private slots:
    // ==========================================================
    // UI & STATE MANAGEMENT
    // ==========================================================
    void switchToMainMode();
    void switchTo3DMode();
    void switchTo4DMode();
    void update4DButtonState();
    void updateRenderState();
    // Slider trasparenza su campo implicito a prodotto: tooltip al caricamento (slider
    // abilitato), riabilitazione tornando a un campo sano. Il blocco+popup avviene al
    // TOCCO utente in onAlphaSliderMovedIllCheck. Vedi definizioni in .cpp.
    void syncImplicitAlphaSlider(bool isImplicitMode, bool newSurface = false);
    void onAlphaSliderMovedIllCheck(int value);
    // SOLO ANDROID: popup di avviso (NON blocca) al primo tocco dell'alpha su una
    // superficie implicita la cui trasparenza puo' degradare (Gyroid, script RM).
    void onAlphaSliderMovedWarnCheck();
    void applyModeDependentStepUI(bool isImplicit);
    // Pulizia totale + superficie di default della modalita' indicata (0 =
    // parametrico, 1 = ray marching). Gestore di tabModeSelector::currentChanged,
    // richiamato anche dal clic sulla linguetta GIA' attiva ("ricomincia da
    // capo"): unica sede del reset di modalita', mai duplicarne il contenuto.
    void applyModeTabReset(int index);
    // Il corpo di applyModeTabReset, parametrizzato sulla superficie finale.
    // loadDefaultSurface = true  -> comportamento storico (toro/sfera a schermo);
    //                    = false -> SCENA VUOTA: stessa identica pulizia, ma i
    //                      campi restano vuoti e non si compila/carica nulla
    //                      (tasto New). La configurazione di MODALITA' (engine
    //                      mode, label e range di S/Steps) e il reset della VISTA
    //                      girano in entrambi i casi: senza, la scena vuota
    //                      resterebbe con slider etichettati e tarati sull'altra
    //                      modalita'. Unica sede del reset: mai duplicarne il
    //                      contenuto in un secondo percorso.
    void resetScene(int index, bool loadDefaultSurface);
    // Un edit dell'utente su un campo che definisce la superficie: se il campo
    // appartiene a un dock diverso da quello che ha prodotto la superficie a
    // schermo, mostra UNA VOLTA l'avviso non bloccante che consiglia di
    // resettare prima. `source` e' il campo editato. (Il lavoro non salvato non
    // passa piu' di qui: si confronta lo stato, vedi sceneFingerprint.)
    void noteSceneEdited(QWidget *source = nullptr);
    bool hasUnsavedWork() const;
    // Save / Don't save / Cancel prima di un'azione che scarta il lavoro.
    // false = l'utente ha annullato: il chiamante NON deve procedere.
    // UNICA conferma per il lavoro non salvato. Con ScopeScene elenca tutto
    // cio' che e' sporco (scena, texture, suono) e salva una volta sola dalla
    // radice dell'albero, dove "records" tiene insieme l'intera scena; con
    // ScopeTexture/ScopeSound guarda il solo flag di quel modulo e punta
    // diritto al suo ramo. false se l'utente annulla: non si procede.
    bool confirmDiscardUnsaved(DiscardScope scope);
    // La texture di libreria non puo' andare sul bersaglio corrente: le Ray
    // Marching (funzioni di un punto 3D) non vanno su una superficie
    // parametrica, sullo sfondo o sul bordo; le parametriche non vanno su una
    // superficie Ray Marching. Le immagini vanno ovunque. Unica regola: la
    // usano il click nella Library (niente domanda sul lavoro non salvato per
    // una texture che non verra' applicata) e handleTextureSelection.
    bool textureIncompatible(const LibraryItem &data) const;
    // Se incompatibile: un avviso, il focus della Library torna sulla texture
    // in vigore, e true (la scena resta com'e').
    bool refuseIncompatibleTexture(const LibraryItem &data);
    // Codice della superficie da cui si deducono le sue costanti A-F (equazioni
    // o script, secondo il modo). Unica sede, vedi la definizione.
    QString surfaceConstantSource() const;
    // Il testo dei TUBI da cui si deducono le costanti della superficie (curva
    // e spessore; lo script, se da' lui la curva, sta gia' con la superficie),
    // piu' il campo Meshes, che vale anche per le superfici. Unica sede per i tre controlli: slider
    // accesi (updateConstantsUIState), costanti libere azzerate al load di una
    // texture (constantsNotUsedBySurface), costanti contese
    // (surfaceConstantSource).
    QString tubeConstantSource() const;
    // Avviso INFORMATIVO al caricamento di un record: lettere A-F usate da piu'
    // parti (superficie, texture, sfondo), che uno stesso slider muove insieme.
    // Il controllo al caricamento di una texture non copriva i record, che
    // portano le tre parti gia' combinate. "Don't show again" vale per quel
    // record e per quelle lettere.
    void warnSharedConstantsOnRecordLoad(const QString &recordPath);
    // Unico punto da cui si mostra un errore di compilazione all'utente.
    void showShaderError(const QString &title, const QString &errorLog);
    // La POSA mossa dall'utente (mouse, touch, tasti di camera): l'unico
    // lavoro che si segue per eventi, perche' la posa la muovono anche i moti
    // automatici. A scena vuota non marca nulla.
    void noteViewControlUsed();
    // Collega i comandi di posa a noteViewControlUsed().
    void wireViewControlsTracking();

    // true quando il motore non ha niente da disegnare (stato prodotto dal
    // tasto NEW). Guarda il MOTORE, non i campi della UI: l'utente puo' aver
    // gia' ricominciato a scrivere senza aver ancora premuto Run.
    bool isSceneEmpty() const;
    // La scena vuota di NEW per intero (con una sola superficie e' isSceneEmpty;
    // nome gemello della 2.0, dove conta anche l'assenza di altri oggetti).
    bool isWholeSceneEmpty() const;
    // Le costanti A..F/S che la SUPERFICIE non usa (equazioni, geodetica,
    // script, limiti, path): quelle che un caricamento di texture puo'
    // riportare al default senza cambiare la forma a schermo.
    QSet<QString> constantsNotUsedBySurface() const;
    // Scena vuota + click su una texture: ricostruisce la superficie di DEFAULT
    // del tab corrente (percorso resetScene, lo stesso del cambio tab) cosi' la
    // texture ha su cosa apparire. Ritorna true se ha ricostruito.
    bool ensureSurfaceForTexture();
    // Applica (o toglie) il blocco dei controlli di resa sulla scena vuota.
    // Sede unica: la chiamano updateRenderState e updateMasterButtonState.
    void applyEmptySceneGating();
    void checkParametricDependency();
    void updateConstraintState();
    void updateConstantsUIState();
    void performMasterStop();
    void performEquationsStop();
    void applyStartSideEffects();

    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void onColorTargetChanged();
    // Seleziona "Surface" come target colore a segnali bloccati, garantendo
    // l'esclusività cross-gruppo (deseleziona Color1/Color2, che vivono in un
    // QButtonGroup diverso dalla coppia Surface/Background). Sostituisce
    // il vecchio radioEditSurf->setChecked(true), che otteneva la deselezione
    // gratis dall'esclusività del singolo m_colorGroup a quattro.
    void selectSurfaceColorTarget();
    // Deseleziona un radio che appartiene a un QButtonGroup ESCLUSIVO. setChecked(false)
    // diretto è un no-op sull'unico bottone acceso di un gruppo esclusivo (Qt insiste a
    // tenerne uno selezionato): bisogna togliere temporaneamente l'esclusività. Serve per
    // l'esclusività MANUALE fra tripla (m_bgTargetGroup) e color slot (m_colorGroup).
    void uncheckInExclusiveGroup(QAbstractButton *btn);
    // Evidenzia nell'albero texture la voce corrispondente al codice attivo
    // (sfondo col bersaglio Background, altrimenti texture superficie).
    void syncTextureTreeSelection();
    void scheduleTextureGeneration();
    void handleTextureSelection(int index);
    // Riporta la trasparenza all'opacità piena (alpha=1). Va chiamata quando si
    // ricade su una superficie di DEFAULT (toro/sfera) per cui la trasparenza
    // del preset/superficie precedente non ha più senso: cambio tab o texture
    // incompatibile col modo corrente (RM su parametrica e viceversa).
    void resetTransparency();

    // ==========================================================
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    // LIMITI SPAZIALI X/Y/Z (Ray Marching): legge i sei campi, valida e applica
    // il taglio della scena. Chiamata dal RUN, come per ogni altra modifica del
    // modulo equazioni -- non dall'Invio. notify=true mostra il popup sul primo
    // campo illeggibile; false applica in silenzio (chiamate di inizializzazione).
    // Ritorna false se un campo non e' valutabile: in quel caso NON si applica
    // nulla e il Run si ferma, come per un'equazione con errore di sintassi.
    // reapply: rivaluta i testi dell'ultimo taglio applicato (costanti cambiate)
    // invece di quelli scritti; li tiene m_spaceLimitsApplied.
    bool applySpaceLimits(bool notify, bool reapply = false);
    bool updateULimits();
    bool updateVLimits();
    bool updateWLimits();
    void refreshLimitsFromConstants();

    // ==========================================================
    // ANIMATION, MOTION & TIMERS
    // ==========================================================
    void onStartClicked();
    void onStopClicked();
    // Tasto RESET della status bar. Se nella scena c'e' qualcosa che si muove
    // (rotazioni, path, animazioni in t) chiede che cosa rimettere all'inizio:
    // la vista, i moti o entrambi. Altrimenti rimette tutto senza chiedere.
    void onResetClicked();
    // Vista: l'inquadratura con cui la scena e' stata aperta -- camera (zoom
    // compreso), FOV, osservatore 4D, quota della sezione.
    void resetView();
    // Moti, alla posizione iniziale: rotazioni alla posa d'apertura, path a
    // t = 0, orologi di geometria, texture, sfondo e fasce all'origine, flusso
    // geodetico a geoTime 0. Cio' che gira riparte dall'inizio, cio' che e'
    // fermo resta fermo; un path fermo ripartira' da capo al Departure (che da
    // solo riprende da dove si era fermato).
    // `camPath`: il path che ha la camera (cameraPath()), letto dal chiamante
    // prima di un eventuale reset della vista, che a path fermo gliela toglie.
    void resetMotions(std::optional<CameraPaths::Path> camPath);
    void resetMotions() { resetMotions(cameraPath()); }
    // Cosa rimetterebbero View e Motions: il RESET chiede solo quando entrambi
    // hanno qualcosa da fare (se uno solo cambia, fa quello senza domande).
    bool viewResetChangesSomething() const;
    bool motionsResetChangesSomething() const;
    // Il path che ha la camera: quello in corsa, o l'ultimo partito se la
    // camera lo segue ancora da fermo (la modalita' path persiste dopo lo stop).
    std::optional<CameraPaths::Path> cameraPath() const;
    // Tasto NEW della status bar: scena vuota (campi e schermo). Stessa pulizia
    // del reset di modalita', ma senza superficie di default e senza cambiare
    // tab. Chiede conferma se c'e' lavoro non salvato.
    void onNewSceneClicked();
    void onNavTimerTick();
    // Readout numerico dei 7 tasti a scatto del dock 4D (X, Y, Z, P, Omega,
    // Phi, Psi). Mostra la VARIAZIONE rispetto a un riferimento, non il valore
    // assoluto del motore: e' cio' che serve a chi preme i tasti ("di quanto mi
    // sono mosso"), e evita di esporre numeri che sembrano sporchi ma non lo
    // sono -- la P parametrica e' una DISTANZA di camera che parte da 4.0, il
    // T^3 del Cross Section nasce con omega=0.5 e P=0.015 di inquadratura.
    // UNA SOLA funzione, chiamata da ogni punto che tocca quello stato (tasti
    // nav, path 4D, load/reset di scena), cosi' un nuovo percorso non rischia
    // di dimenticare l'aggiornamento -- non e' un timer a se stante.
    void updateNav4DReadout();
    // Fotografa lo stato 4D corrente come nuovo ZERO dei campi. Si chiama dove
    // la scena cambia per una scelta dell'utente che non sia premere i tasti:
    // load di preset/record (il preset diventa il riferimento), reset, New,
    // cambio tab/sotto-tab.
    void resetNav4DBaseline();
    void onDepartureClicked();
    void checkPathFields();
    void onDeparture3DClicked();
    void checkPath3DFields();
    // (Camera dei path al tempo t: CameraPaths::applyCameraAt, unica
    // implementazione per i tick dal vivo e il loop di registrazione.)
    // Avanzamento del flusso geodetico di dtSeconds: unica implementazione,
    // condivisa tra il tick di m_geoAnimTimer e il loop di registrazione
    // (VideoRecorder passa il dt virtuale del frame). Converte il dt nella
    // stessa velocita' vista a schermo (0.015 unita' per tick nominale).
    bool advanceGeodesicFlowBy(double dtSeconds);
    // Compilazione delle equazioni path dai campi UI (pulizia input +
    // validazione + popup d'errore): unica implementazione, usata dal
    // Departure e dal commit live con Invio a path in corsa (nuova costante
    // o espressione entra subito, senza stop/ripartenza; i VALORI delle
    // costanti sono gia' live via symbol table per riferimento).
    bool compilePath4DFromFields();
    bool compilePath3DFromFields();
    // Mutua esclusivita' GO / Departure 3D / Departure 4D: attivando uno di questi
    // tre moti gli altri due si spengono. Questi helper fermano gli "altri" senza
    // duplicare la logica di pulizia UI. Ognuno e' un no-op se il suo moto e' fermo.
    void stopPathAnimations();   // ferma i due path (4D e 3D), coi loro tasti
    void stopRotationMotion();   // ferma il moto GO (rotazioni superficie/4D)
    void onToggleViewClicked();    // toggle vista path 4D (pushView)
    void onToggleView3DClicked();  // toggle vista path 3D (pushView3D)
    // I pulsanti Tangent/Center View rispecchiano i rispettivi Departure:
    // attivi se il proprio path e' in corsa o i suoi campi sono compilati
    // (pre-selezione della vista, anche per il subentro con handoff mentre
    // l'altro path gira). Chiamato agli avvii/stop e a ogni modifica dei
    // campi path (via checkPathFields/checkPath3DFields).
    void updateViewButtonsEnabled();
    // >=3 dei 4 campi X/Y/Z/P compilati: sotto quella soglia non c'e' una
    // superficie da costruire. Gate del Run parametrico e del master START,
    // omologo di hasPath4DInput per il tasto Departure.
    bool hasParametricEquationInput() const;
    // Gate del tasto RUN del dock Equations: oltre a >=3 campi X/Y/Z/P pretende
    // il dominio (limiti degli assi IN USO, compilati e con min<max) e i campi
    // obbligatori del tab attivo del pannello (Composition: le definizioni
    // delle maiuscole citate; Geodesic Flow: almeno una direzione iniziale non
    // nulla). I vincoli sono facoltativi e non entrano nel conto, e un campo
    // vuoto vale zero: non si pretende di riempire tutto. Nessun popup: e' un
    // predicato di UI chiamato a ogni carattere.
    // NON const: legge i limiti con parseLimitField, che risolve la cascata
    // delle costanti A..F/S e non e' un metodo const.
    bool hasCompleteParametricInput();
    bool hasPath4DInput() const;   // >=2 campi path 4D compilati (gate Departure/View 4D)
    bool hasPath3DInput() const;   // >=2 campi path 3D compilati (gate Departure/View 3D)
    // Il MODULO EQUAZIONI e' in moto: geometria animata da 't' col suo orologio
    // acceso, oppure flusso geodetico in corsa. E' il criterio con cui il tasto
    // Run del dock diventa "Stop", ed e' cosa diversa dal master button, che e'
    // su STOP solo quando TUTTI i moduli accendibili sono accesi (masterActivity).
    // UNICO lettore autorizzato di questo stato: lo usano updateMasterButtonState
    // (testo/abilitazione dei Run) e i filtri tastiera (l'Invio applica al volo
    // solo a modulo in moto). Mai ricostruirlo a mano: e' la stessa famiglia di
    // bug delle copie di stato live nel recorder.
    bool isEquationModuleMoving() const;
    // Routing GEODETICO attivo: le equazioni citano U/V/W maiuscole (o c'e' uno
    // script metrico, che rende la mappa X/Y/Z/P libera di non citarle), i campi
    // del flusso sono compilati e siamo nel tab Parametric. E' la condizione con
    // cui updateGeodesicMesh sostituisce la mesh parametrica: la stessa che
    // decidono checkAndTriggerMeshUpdate, commitFieldsOnEnter,
    // commitUiFieldsDuringMotion e commitLimitFieldOnEnter, che la ricostruivano
    // ognuno per conto proprio (con metricScriptActive incluso in alcuni e non in
    // altri). Sede unica, cosi' non divergono.
    bool isGeodesicRoutingActive() const;
    // Le equazioni X/Y/Z/P a schermo sono ancora quelle dell'ultimo Run
    // (snapshot m_eqApplied)? Serve al COMMIT DI SERVIZIO del ramo parametrico
    // standard (RunOrigin::ServiceCommit: Invio o uscita da una costante) per decidere se
    // puo' spegnere il tasto Run one-shot: puo' solo se la MAPPA non ha
    // modifiche in sospeso, perche' quelle il Run le deve ancora applicare.
    // Snapshot assente = mai fatto un Run: non si puo' affermare che coincidano.
    bool mapEquationsMatchSnapshot() const;
    // Moto GO (rotazioni) davvero in corsa: timer attivo E tasto non su "GO".
    // UNICO lettore autorizzato del testo di btnStart_2 come stato — master
    // button e VideoRecorder passano da qui, mai confronti locali sul testo.
    bool isRotationMotionRunning() const;

    // ==========================================================
    // SCRIPTING ENGINE
    // ==========================================================
    void onToggleScriptMode();
    void onRunCurrentScript();
    void onRunScriptClicked();
    void runMetricScript(const QString& fullText);
    void exitMetricScriptMode();
    void onApplyTextureScriptClicked();
    void onRunRaymarchTextureClicked();
    void onRunSoundClicked();

    // ==========================================================
    // LIBRARY & WORKSPACE MANAGEMENT
    // ==========================================================
    void onExampleItemClicked(QTreeWidgetItem *item, int column);
    // PER VALORE, NON PER RIFERIMENTO -- e non e' una svista.
    // L'argomento e' un elemento di m_surfaces/m_motions dentro LibraryManager.
    // Queste funzioni aprono un QMessageBox (immagine del record non trovata):
    // il suo exec() fa girare il ciclo di eventi, e li' possono scattare i timer
    // che chiamano refreshRepositories() -> m_libraryManager.clear() distrugge
    // la lista mentre l'argomento la sta ancora leggendo. Con un riferimento,
    // tutto cio' che viene letto DOPO il popup (FOV, proiezione, velocita',
    // angoli iniziali, hasPath4D/3D) arriva da memoria liberata: la scena si
    // carica con l'inquadratura e il moto di un ALTRO record. La copia rende il
    // caricamento indipendente da quante volte la libreria si ricostruisce nel
    // frattempo. Il costo e' una copia di stringhe per caricamento: irrilevante
    // rispetto a ricostruire mesh e shader.
    void applySurfaceExample(LibraryItem data);
    void applyMotionExample(LibraryItem data);
    void deleteSelectedExample();
    void onUndoDelete();
    // I quattro bool sono lo stato dei moti PRIMA che showMenu li fermasse: la
    // voce di menu lo cattura e lo passa, perche' dopo lo stop non e' piu'
    // leggibile (vedi refreshLibraryPreservingMotion). I default false valgono
    // per l'altro chiamante, actionSelectFolder, che non ferma nulla.
    void onAddRepositoryClicked(bool wasRotating = false, bool wasPath4D = false,
                                bool wasPath3D = false, bool wasTimeAnimating = false);
    void onCreateFolderClicked();
    // RENAME dal menu contestuale della Library (file o cartella): chiede il
    // nome, avvisa se la voce e' un'immagine o un audio (chi la cita per nome
    // non la trova piu'), poi renameLibraryPath e la Library riletta con la
    // voce rinominata selezionata.
    void renameLibraryItem(QTreeWidgetItem *item);
    // Il lavoro, senza dialoghi (lo usa anche il test): rinomina sul disco e
    // riallinea i percorsi che l'app ricorda (record in scena, texture
    // caricata, appunti di Copia/Taglia). Ritorna il percorso nuovo, o vuoto
    // con il motivo in *error.
    QString renameLibraryPath(const QString &path, QString newName, QString *error);
    // I percorsi che l'app ricorda (record e texture caricati, appunti di
    // Taglia) seguono una voce della Library rinominata o spostata: senza, un
    // Save successivo riscriverebbe il file nel posto vecchio.
    void remapLibraryPaths(const QString &oldPath, const QString &newPath, bool isDir);
    // Library riletta con le voci appena spostate o incollate selezionate, la
    // prima col focus. Il refresh ritrova la selezione dal percorso
    // nell'albero, che per una voce spostata e' cambiato: il focus si perdeva.
    void refreshAndFocusPaths(const QStringList &paths);
    void onSyncPresetsClicked();

    // ==========================================================
    // FILE I/O & CLIPBOARD
    // ==========================================================
    void saveSurfaceToFile(const QString &suggestedPath = QString());
    void onPasteExample(const QString &destDirOverride = QString());
    void onPasteTexture(const QString &destDirOverride = QString());
    void performCut(QTreeWidgetItem* item = nullptr);
    void performCopy(QTreeWidgetItem* targetItem = nullptr);
    void onSaveTextureClicked();
    void onSaveScriptClicked();
    void onSaveMotionClicked();

    // ==========================================================
    // AUDIO & MEDIA
    // ==========================================================
    void onSoundItemClicked(QTreeWidgetItem *item, int column);


private:
    // Estrae la sezione opzionale //CUTOUT_BEGIN..//CUTOUT_END da uno script
    // parametrico (dock Script): la traduce con GlslTranslator e la rimuove dal
    // testo restituito, cosi' NON finisce mai iniettata in getRawPosition() nel
    // vertex shader (romperebbe la compilazione: due "return" di tipo diverso
    // nella stessa funzione). Un solo punto per i due chiamanti che oggi
    // rialimentano lo script parametrico (onRunScriptClicked e la ripresa da
    // Master Start/dock Run): erano diventati due copie della stessa estrazione
    // e la seconda non toglieva mai il cutout -> errore di compilazione.
    QString extractCutoutSection(const QString &fullText, QString *outCutoutGlsl);

    // Estrae le sezioni ripetibili //MESH_BEGIN..//MESH_END (multi-mesh) e le
    // rimuove dal testo restituito, per lo stesso motivo del cutout: non devono
    // finire iniettate in getRawPosition(). Ogni sezione dichiara il dominio e
    // la risoluzione di UNA parte di mesh; lo script distingue il ramo con
    // u_meshIndex. Senza sezioni l'elenco resta vuoto = una mesh sola come
    // sempre. Va chiamata negli STESSI punti dell'estrazione del cutout,
    // altrimenti si ripete il bug "funziona da Run ma non da Master Start".
    QString extractMeshSections(const QString &fullText, std::vector<MeshPart> *outParts,
                                QVector<bool> *outHasU = nullptr);

    // ==========================================================
    // CORE UI COMPONENTS
    // ==========================================================
    Ui::MainWindow *ui;
    // Campo implicito della scena vuota in Ray Marching: una costante positiva
    // non viene mai intersecata dal marcher (niente hit, solo sfondo). Non si
    // puo' usare la stringa vuota: createImplicitFragmentShader la sostituisce
    // con la sfera di default. Costante condivisa fra chi la SCRIVE (resetScene)
    // e chi la RICONOSCE (isSceneEmpty): separarle romperebbe il gate in
    // silenzio.
    static constexpr const char *kEmptyImplicitField = "1.0";

    // true finche' il blocco "scena vuota" e' applicato ai controlli di resa.
    // Serve a riaccenderli UNA sola volta quando la superficie torna: senza,
    // il ramo di riabilitazione girerebbe a ogni giro e scavalcherebbe le
    // regole normali (wireframe, ray marching, ambito mesh).
    bool m_emptySceneGated = false;

    QPushButton *m_btnStart;
    QPushButton *m_btnNew;
    QPushButton *m_btnResetView;
    QPushButton *m_btnProjection;
    QPushButton *m_btnRec;
    QLabel *m_statusLabel;
    // Messaggio in sovrimpressione sulla scena: la VISTA dei tre messaggi qui
    // sotto (vedi scenehintoverlay.h). Creato al primo messaggio.
    SceneHintOverlay *m_hintOverlay = nullptr;
    // Messaggio del record attualmente caricato: va ricordato qui perche' un
    // risalvataggio non lo perda. Si edita dal dialogo che compare al
    // salvataggio (askSceneHint in presetserializer.cpp): non ha un campo fisso
    // nell'interfaccia, che per un messaggio occasionale sarebbe ingombro.
    QString m_currentHintText;
    float   m_currentHintSeconds = 6.0f;
    // La scena viene da un preset del ramo Surfaces: il suo messaggio, da
    // solo, non ha bisogno dell'etichetta "Surface" (vedi composedHintText).
    bool    m_sceneHintFromSurfacePreset = false;

    // Messaggio della TEXTURE caricata, tenuto SEPARATO da quello della scena.
    // I due vivono in file diversi (il .json della texture e quello della
    // superficie/record) e non devono mischiarsi: caricare una texture con hint
    // non deve far finire quel testo nel preset della superficie al primo Save,
    // ne' viceversa. Come per la scena non c'e' UI di editing, quindi si ricorda
    // qui perche' un risalvataggio della texture non lo perda.
    QString m_currentTextureHintText;
    float   m_currentTextureHintSeconds = 6.0f;
    // Messaggio della texture DI SFONDO: terzo, separato dagli altri due per la
    // stessa ragione. Prima non esisteva, e lo sfondo non poteva dire a cosa
    // serve un suo slider: caricandolo dalla libreria il suo hint non compariva,
    // e scriverlo in m_currentTextureHintText l'avrebbe fatto passare per quello
    // della superficie al primo salvataggio. Nel record: "background"/"hintText".
    QString m_currentBgTextureHintText;
    float   m_currentBgTextureHintSeconds = 6.0f;
    // I messaggi uniti, come li vede l'utente: scena, texture e sfondo possono
    // nominare costanti diverse e vanno mostrati TUTTI. Sede unica, cosi' non
    // si torna a farne vincere uno solo. Vuoto = niente da mostrare.
    QString composedHintText() const;
    QString shownBorderTextureHint() const;
    // Ridisegna l'overlay coi soli messaggi delle TEXTURE (superficie e sfondo),
    // sospendendo per la chiamata quello della scena: serve al Sync, dove sono
    // cambiate le texture e non la scena. Non modifica nessuno dei tre testi.
    void showTextureHintsOnly(float seconds);
    // Il messaggio della texture appena presa dalla Library, da solo, nel suo
    // posto (texture di superficie o di fascia, sfondo, bordo); vuoto =
    // toglie da schermo quello che c'era. Non modifica nessuno dei testi.
    void showLoadedTextureHint(const QString &texture, const QString &background,
                               const QString &border, float seconds);
    // Ridisegna l'overlay dalla coppia corrente senza modificare i due testi.
    void refreshSceneHint(float seconds);


    // Minimi CONTINUI dichiarati con "F := min(0.3);": la costante resta
    // frazionaria ma non scende sotto la soglia. Serve dove sotto un certo
    // valore la figura degenera (i tubi di Clifford collassano sull'asse).
    // Applicati insieme ai discreti, dagli stessi punti.

    // MESH_VISIBLE := <espressione>;  (es. "MESH_VISIBLE := E;")
    // Quante delle mesh dichiarate sono davvero A SCHERMO. Serve perche' un
    // //CUTOUT puo' spegnerne una parte (nei tori di Hopf lo slider E ne accende
    // da 1 a 9), ma il cutout e' GLSL eseguito sulla GPU: la CPU non puo' sapere
    // quante ne sopravvivono senza rieseguirne la logica, ed e' proprio il tipo
    // di duplicazione che va evitato. Percio' lo dichiara lo SCRIPT.
    // Si tiene l'ESPRESSIONE, non il numero: dipende dalle costanti (qui E) e va
    // rivalutata a ogni loro cambio. Vuota = nessuna direttiva, valgono tutte le
    // mesh dichiarate (comportamento storico).
    QString m_meshVisibleExpr;
    // Valore corrente della direttiva, 0 = non dichiarata/non valutabile.
    int meshVisibleCount() const;

    QProgressBar *m_renderProgress;
    QButtonGroup *m_colorGroup;
    QButtonGroup *m_modeGroup;
    // Coppia esclusiva Surface / Background: scelta del target di editing (cosa
    // pilotano slider/texture/colore). radioSurface = superficie, radioBackground = sfondo.
    // Sono la VISTA di m_editTarget (editingBackground()): non leggerli. I color slot
    // (radioTexColor1/2) stanno in m_colorGroup a parte; l'esclusività fra i due gruppi
    // è manuale.
    QButtonGroup *m_bgTargetGroup;

    // Coppia esclusiva All / Mesh (ambito dell'aspetto: tutta la superficie
    // oppure la sola mesh scelta nello spinbox).
    // Serve un QButtonGroup ESPLICITO: l'esclusivita' automatica dei
    // QRadioButton vale solo fra fratelli con lo STESSO genitore, e nella .ui
    // radioMeshOne e' finito dentro il contenitore groupMeshOne (insieme a
    // spinMeshSel) mentre radioMeshAll e' rimasto in widgetMeshSel. Diversi
    // genitori = due gruppi da uno: si potevano selezionare e deselezionare
    // entrambi. Il gruppo li riunisce a prescindere da dove stanno nel layout,
    // quindi il riquadro attorno a "Mesh" si puo' spostare liberamente.
    QButtonGroup *m_meshScopeGroup = nullptr;

    // Le DUE coppie dei controlli di resa: Shell/Solid e Fast/Precise.
    // Servono gruppi ESPLICITI per il motivo OPPOSTO a m_meshScopeGroup qui
    // sopra: non genitori diversi da riunire, ma lo STESSO genitore da separare.
    // I quattro radio vivono nella griglia gridRenderControls (che li allinea in
    // colonne condivise), quindi sono fratelli -- e l'esclusivita' automatica
    // dei QRadioButton li tratta come UN UNICO gruppo da quattro: accendere
    // "Precise" spegneva "Shell". Finche' stavano in due pannelli separati i
    // pannelli facevano da gruppi impliciti e il problema non si vedeva.
    QButtonGroup *m_shellSolidGroup = nullptr;
    QButtonGroup *m_marcherGroup = nullptr;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    const float TWO_PI = 6.28318530718f;
    float uMin = 0.0f;
    float uMax = TWO_PI;
    float vMin = 0.0f;
    float vMax = TWO_PI;
    float wMin = 0.0f;
    float wMax = 0.1f;
    int m_lastParametricSteps = 100;
    int m_lastImplicitSteps = 400;
    double m_lastImplicitS = 0.4;
    // Spessore del guscio com'era prima del clic su Solid (-1 = niente da
    // ricordare). In Solid lo slider va al minimo, ma tornando a Shell la parete
    // riprende questo valore invece del default. Lo azzera setShellThicknessUI,
    // cioe' ogni load e reset: non deve riemergere su un'altra scena.
    float m_shellThicknessBeforeSolid = -1.0f;
    QTimer* m_stepsDebounce = nullptr;
    QTimer* m_meshDebounce = nullptr;
    bool m_constantPopupActive = false;
    // True mentre il popup di rallentamento e' a schermo: evita che segnali
    // performanceWarning gia' in coda aprano box sovrapposti (raffica).
    bool m_perfPopupActive = false;
    // Stessa ragione: buildPipeline puo' fallire piu' volte di fila (un tentativo
    // per ricostruzione) e senza guardia si aprirebbe un popup per ciascuno.
    bool m_shaderErrorPopupActive = false;
    // Stessa ragione, per l'immagine di texture non decodificabile: un preset
    // puo' innescare piu' caricamenti in sequenza (superficie e sfondo).
    bool m_textureImageErrorPopupActive = false;
    // Stessa ragione, per i preset non scaricati da iCloud: il fsWatcher puo'
    // far ripartire refreshRepositories molte volte di fila sulla stessa
    // cartella. Si riarma quando la libreria torna completa.
    bool m_datalessWarningShown = false;
    // Profondita', non un bool: refreshRepositories puo' annidarsi (il rebuild
    // riemette segnali che ne innescano un altro), e un bool verrebbe azzerato
    // dal primo a uscire lasciando scoperti i livelli ancora aperti.
    // Vedi libraryRebuildInProgress().
    int m_libraryRebuildDepth = 0;
    // True mentre una guardia trasparenza (forceOpaqueForHeavyRM) sta mostrando
    // il suo popup: fa scartare un performanceWarning gia' in coda (emesso sui
    // primi frame trasparenti prima dell'ack) che altrimenti aprirebbe il box del
    // watchdog SOPRA quello della guardia. Vedi il gestore di performanceWarning.
    bool m_transparencyGuardActive = false;
    QHash<QLineEdit*, float> m_lastValidConst;
    QTimer* m_geoAnimTimer = nullptr;   // timer del flusso geodetico (creato al primo updateGeodesicMesh)
    bool m_geodesicErrorPending = false;
    bool m_inGeoAnimTick = false;
    // Esito dell'ultimo calcolo del flusso geodetico e popup gia' mostrato per
    // l'azione in corso (riarmato da runScene e dai load).
    enum class GeoError { None, Singularity, NonFinite, Syntax };
    GeoError m_geoErrorType = GeoError::None;
    bool m_geoErrorShown = false;
    double m_geoTime = 0.0;          // tempo del flusso: advanceGeodesicFlowBy
    bool m_geoInitialLoad = false;   // primo calcolo dopo il load di un preset
    bool m_geoAnimTimerWasRunning = false;  // fermato da hideEvent, riparte in showEvent
    bool m_collapseErrorShown = false;      // popup di collasso gia' mostrato
    // Script di texture toccato a mano dopo l'ultimo load/applicazione: chiede
    // conferma prima di spegnere la texture.
    bool m_textureModified = false;
    // Campi in cui l'utente ha DIGITATO (textEdited) senza ancora confermare:
    // solo questi si validano all'uscita. I setText del programma li tolgono.
    QSet<const QObject*> m_pendingUserEdits;
    void setUserEditPending(const QObject *w, bool on)
    { if (!w) return; if (on) m_pendingUserEdits.insert(w); else m_pendingUserEdits.remove(w); }
    bool userEditPending(const QObject *w) const { return m_pendingUserEdits.contains(w); }

    // ==========================================================
    // RENDERING & COLOR STATE
    // ==========================================================
    // True quando il ramo Texture del dock Library e' collassato+grigio perche'
    // siamo in surface-wireframe. Traccia la transizione: applichiamo il grigio
    // (o lo togliamo) solo quando lo stato cambia, non a ogni updateRenderState.
    bool m_textureLibraryGrayed = false;
    // Guardia "popup gia' mostrato / slider bloccato per campo a prodotto": il popup
    // compare UNA volta, al primo TOCCO dell'utente sullo slider su un campo a prodotto.
    // Resettata tornando a un campo sano (syncImplicitAlphaSlider). Vedi onAlphaSliderMovedIllCheck.
    bool m_implicitAlphaDisabled = false;
    // SOLO ANDROID. Guardia "avviso trasparenza gia' mostrato": il popup di avviso
    // (NON bloccante) compare UNA volta, al primo tocco dell'alpha su Gyroid / script
    // RM. Resettata per ogni nuova superficie (syncImplicitAlphaSlider newSurface=true).
    bool m_implicitWarnShown = false;
    // Guardia "conferma trasparenza-su-scena-pesante gia' accettata" (tutte le
    // piattaforme, MISURATA: scatta solo se renderingUnderHeavyLoad). Alzata solo
    // se l'utente sceglie "Apply anyway" — un "Keep it opaque" NON la alza, cosi'
    // un nuovo tentativo richiede di nuovo. Resettata per ogni nuova superficie
    // (syncImplicitAlphaSlider newSurface=true).
    bool m_alphaHeavyWarnShown = false;
    // Anti-rientranza del box di conferma: i valueChanged della PRESA ancora in
    // corso, consegnati nel loop annidato di exec(), non devono impilare altri box.
    bool m_alphaHeavyPopupActive = false;
    // "Keep it opaque" dato durante la presa corrente: i valueChanged residui dello
    // stesso gesto vengono riassorbiti a 100 SENZA riaprire il box (falso positivo:
    // finestra che permane/ricompare). Riarmata da una nuova presa (sliderPressed)
    // o dal cambio superficie (syncImplicitAlphaSlider).
    bool m_alphaHeavyDeclined = false;
    // SOLO MOBILE (no-op su desktop): dopo un apply RM riuscito, se il displacement
    // e' stato introdotto/cambiato mentre alpha<1 porta l'alpha a 1 (il costo arriva
    // CON la texture: la conferma misurata non puo' prevederlo). prevDisp = valore
    // di currentDisplacementCode() catturato PRIMA dell'apply.
    void guardTransparencyOnDisplacementApply(const QString &prevDisp);
    // SOLO MOBILE (no-op su desktop): guardia POST-LOAD, chiamata a fine
    // applyCommonData quando lo STATO FINALE caricato e' RM + alpha<1 +
    // displacement. Il load imposta alpha e displacement in modo PROGRAMMATICO
    // (m_settingAlphaProgrammatic), quindi ne' la conferma misurata ne' la
    // guardia interattiva scattano: e' la falla del "record con alpha<1 nel JSON".
    // Porta alpha a 1, avvisa, zittisce il watchdog (come la guardia interattiva).
    void guardTransparencyOnImplicitLoad();
    // Corpo condiviso dalle due guardie displacement: porta alpha a 1 (opaco),
    // zittisce il watchdog per questa scena (acknowledgePerformanceWarning) e
    // mostra `message`. Presuppone il contesto gia' verificato dal chiamante.
    void forceOpaqueForHeavyRM(const QString &message);
    // TUTTE LE PIATTAFORME (desktop compreso). Guardia sul commit RM quando la
    // scena e' gia' trasparente e il codice appena applicato porta un
    // DISPLACEMENT: e' il displacement il moltiplicatore, non la texture di
    // colore. Nel template (raymarch_template.txt) %DISPLACEMENT_CODE% e'
    // iniettato in rawField(), quindi gira dentro OGNI map() del marcher --
    // MAX_FACES x 3 marchNextLayer x MAX_LAYER_STEPS x 4 map() per pixel sul ramo
    // trasparente; %TEXTURE_CODE% sta invece nello shading finale e costa UNA
    // valutazione per pixel, trasparenza o meno. Una texture di colore, per
    // quanto elaborata, non giustifica di fermare la scena.
    // Copre la falla del Cross Section di default, che parte da solo ad alpha
    // 0.75 + Ray Steps 600 (set PROGRAMMATICO, quindi ne' la conferma misurata
    // dell'alpha slider ne' le guardie displacement mobile, che sono #if mobile,
    // lo vedono mai): il collasso e' istantaneo e il watchdog, che lavora su una
    // EMA, puo' solo constatarlo DOPO il magenta.
    // Ferma il moto, porta alpha a 1 e CHIEDE: ripristinare trasparenza+moto a
    // proprio rischio, oppure restare opaco e fermo. Torna true se l'utente ha
    // scelto di proseguire trasparente (o se la guardia non si applicava).
    bool guardTransparencyOnHeavyTextureApply(const QString &newDispCode);
    // Anti-rientranza di guardTransparencyOnHeavyTextureApply: il suo box e'
    // modale e fa girare l'event loop (un commit puo' rientrare da li').
    bool m_heavyTexGuardActive = false;
    // Displacement per cui la guardia ha GIA' chiesto, qualunque sia stata la
    // risposta. Senza, il box si riapriva a raffica senza fine: scegliendo
    // "Restore" l'alpha torna <1 e il commit successivo (che il riavvio stesso
    // puo' innescare) ritrova le identiche condizioni e richiede. Confrontare il
    // CODICE e non un bool rende il riarmo automatico e preciso: un displacement
    // davvero diverso richiede, ricompilare la stessa scena no. Vuota = mai
    // chiesto. Vedi guardTransparencyOnHeavyTextureApply.
    QString m_heavyTexGuardAskedFor;
    // Alzata mentre impostiamo lo slider trasparenza DA CODICE (load preset,
    // resetTransparency): l'handler valueChanged distingue cosi' il set programmatico
    // dall'interazione utente e non fa scattare il blocco/popup sui load. Vedi setAlphaSliderProgrammatic.
    bool m_settingAlphaProgrammatic = false;
    // (Colore e alpha GLOBALI della superficie vivono solo nel motore:
    // globalColor / globalAlpha. Colore e colori 1/2 dello sfondo stanno nella
    // scena, m_scene.bgColor / bgTexColor1/2.)

    // ==========================================================
    // TEXTURE & SCRIPTING STATE
    // ==========================================================
    enum ScriptMode {
        ScriptModeSurface,
        ScriptModeTexture,
        ScriptModeSound
    };
    ScriptMode m_currentScriptMode = ScriptModeSurface;

    // ----------------------------------------------------------
    // DOCK SCRIPT: STATO E VISTA
    // ----------------------------------------------------------
    // L'editor (txtScriptEditor) e' UN widget per cinque testi. Lo STATO sono
    // gli slot: ognuno e' il testo del suo modulo COSI' COM'E' SCRITTO, eseguito
    // o no (l'intenzione: e' cio' che il Save scrive). L'editor e' la VISTA
    // dello slot che il dock mostra adesso (shownScriptSlot): chi scrive uno
    // slot passa da setScriptText, chi digita nell'editor scrive lo slot
    // mostrato, e nessuno legge l'editor.
    // Cio' che e' A SCHERMO sta altrove: m_scene.surfaceScriptApplied (script di
    // superficie dell'ultimo Run o del preset), m_scene.surfaceTextureCode e
    // m_scene.bgTextureCode (texture compilate), MeshPart::textureCode (fasce).
    // "Modificato e non eseguito" = slot diverso dall'applicato.
    // Prima l'editor era anche stato: il testo in sospeso viveva solo li', e
    // ogni cambio di modulo o di bersaglio lo travasava nello slot -- che
    // faceva pure da "ultima versione eseguita". Bastava un giro dei moduli
    // perche' uno script mai eseguito risultasse eseguito (tasto Run spento) e
    // la superficie risultasse "da script" (tasti Run delle equazioni spenti).
    enum ScriptSlot {
        SlotNone,               // Texture di superficie in Ray Marching: sta nel dock Equations
        SlotSurface,            // script della superficie (parametrica o implicita)
        SlotSurfaceTexture,     // texture parametrica di superficie
        SlotMeshTexture,        // texture della fascia selezionata
        SlotBorderTexture,      // texture del bordo (bersaglio Border)
        SlotBackgroundTexture,  // texture di sfondo
        SlotSound               // suono
    };
    // Lo slot della TEXTURE su cui agiscono i comandi adesso: sfondo, fascia
    // selezionata o superficie (bersaglio del dock Renderer e ambito All/Mesh).
    ScriptSlot targetTextureSlot() const;
    // Lo slot che il dock Script mostra adesso (modulo + bersaglio + fascia).
    ScriptSlot shownScriptSlot() const;
    QString scriptText(ScriptSlot slot) const;
    // Scrive lo slot; se e' quello mostrato, l'editor lo segue (a segnali
    // bloccati: e' una scrittura del programma, non una digitazione).
    void setScriptText(ScriptSlot slot, const QString &text);
    // La VISTA: porta nell'editor lo slot mostrato. La chiama
    // updateScriptButtonText, quindi ogni riallineamento dei tasti riallinea
    // anche l'editor; non tocca nulla se il testo e' gia' quello (cursore e
    // undo restano dove sono).
    void refreshScriptEditor();
    // L'applicato che fa da riferimento per "modificato e non eseguito".
    QString appliedScriptText(ScriptSlot slot) const;
    // Slot della FASCIA: non e' della scena (la texture della fascia sta nella
    // MeshPart), e' il testo in lavorazione per la fascia selezionata. Si
    // ricarica dal motore quando cambia la fascia o la sua texture.
    void syncMeshTextureSlot() const;
    mutable QString m_meshTextureScriptText;
    mutable QString m_meshTextureScriptBase;   // texture efficace della fascia al caricamento dello slot
    mutable int m_meshTextureScriptPart = -1;
    // Slot del BORDO, stesso schema: la texture del bordo sta nel motore
    // (GLWidget::borderTextureCode), lo slot e' il testo in lavorazione e si
    // ricarica quando la texture del bordo cambia.
    void syncBorderTextureSlot() const;
    mutable QString m_borderTextureScriptText;
    mutable QString m_borderTextureScriptBase;
    // Il testo che la vista ha messo nell'editor (o che l'utente vi ha scritto):
    // refreshScriptEditor confronta con questo, non col contenuto del widget,
    // che normalizza gli a capo e riaprirebbe il confronto a ogni giro.
    QString m_scriptEditorText;

    // ----------------------------------------------------------
    // DOCK EQUATIONS: STATO (superficie parametrica e flusso geodetico)
    // ----------------------------------------------------------
    // I testi del dock Equations COSI' COME SONO SCRITTI (m_scene.eq) e com'erano
    // all'ultimo Run o al load (m_eqApplied: cio' che e' a schermo). La logica
    // legge m_scene.eq, non i widget: i campi ne sono l'editor, e bindEquationFields
    // li tiene allineati a livello di DOCUMENTO -- quindi anche quando un campo
    // viene scritto a segnali bloccati. Chi scrive un campo dal programma
    // senza volerne le reazioni (textChanged) passa da setEqText.
    // L'applicato erano 17 property dinamiche ("active_lineX", ...) scritte e
    // lette per nome: ora lo snapshot e' una copia della struct.
    // Vuoto finche' non c'e' stato un Run, un load o un reset.
    std::optional<EquationTexts> m_eqApplied;
    // Aggancia i campi a m_scene.eq (e quelli Ray Marching a m_scene.rm, qui sotto). Subito
    // dopo setupUi: il suo gestore deve girare PRIMA di ogni altro textChanged,
    // che lo stato lo legge.
    void bindEquationFields();
    class QPlainTextEdit *equationFieldEdit(EqField field) const;
    // Scrive un campo dal programma, a segnali bloccati (m_scene.eq segue).
    void setEqText(EqField field, const QString &text);

    // ----------------------------------------------------------
    // RAY MARCHING: STATO dei testi
    // ----------------------------------------------------------
    // Come m_scene.eq, per i quattro campi del Ray Marching: lo SCRITTO. L'applicato
    // qui non e' una copia in MainWindow: sta nel motore (GLWidget::
    // activeImplicitEquation, currentTextureCode, currentDisplacementCode).
    class QPlainTextEdit *implicitFieldEdit(RmField field) const;
    // Scrive un campo dal programma, a segnali bloccati (m_scene.rm segue).
    void setRmText(RmField field, const QString &text);

    // ----------------------------------------------------------
    // COSTANTI A..F, S E STEPS: STATO
    // ----------------------------------------------------------
    // I testi dei campi delle costanti COSI' COME SONO SCRITTI (numeri o
    // espressioni a cascata, "A/10"): sono la fonte. Gli slider sono la vista
    // dei valori risolti (syncConstantSliders), il motore il derivato
    // (setEngineConstants). In Ray Marching il campo S e' lo Step Relax del
    // marcher. La logica legge m_scene.constants, non i widget.
    // Un QLineEdit non ha un documento separato che segua le scritture a
    // segnali bloccati: chi scrive un campo dal programma passa da
    // setConstText, la digitazione arriva dal textChanged (bindConstantFields).
    // I sette campi nell'ordine della cascata (B puo' citare A, S tutte).
    static const std::array<ConstField, 7> &constantFields();
    static QString constantName(ConstField field);
    QLineEdit *constantFieldEdit(ConstField field) const;
    QSlider *constantSlider(ConstField field) const;
    // Aggancia i campi a m_scene.constants e lo slider Steps a m_scene.steps. Subito dopo
    // setupUi, come bindEquationFields.
    void bindConstantFields();
    // Scrive un campo dal programma, a segnali bloccati. Lo slider NON lo
    // tocca: dopo una o piu' scritture, refreshConstantSliders (o una
    // pushConstantsToEngine, che li riallinea anche lei).
    void setConstText(ConstField field, const QString &text);
    void setConstValue(ConstField field, double value);   // formato 'g', 6
    // Tutti e sette in blocco (load: constantTextsFromItem), stessa regola.
    void setConstTexts(const ConstantTexts &k);
    void refreshConstantSliders();
    // ----------------------------------------------------------
    // LIMITI E PATH: STATO dei testi dei campi a una riga
    // ----------------------------------------------------------
    // Il dominio u/v/w, il taglio spaziale x/y/z del Ray Marching e i campi
    // dei path 4D e 3D COSI' COME SONO SCRITTI (numeri o espressioni). La
    // logica e il Save leggono m_scene.lim e m_scene.path, non i QLineEdit. L'aggancio e'
    // il textChanged del campo (bindLineFields): arriva per la digitazione e
    // per setText/clear a segnali vivi, i cui gestori sono solo derivazioni.
    // Le scritture a segnali BLOCCATI passano da setLineText. I validatori che
    // mettono il cursore sul campo sbagliato ricevono ancora il widget.
    // I testi x/y/z (min, max) dell'ultimo taglio applicato da applySpaceLimits:
    // lo slider di una costante li rivaluta, lo scritto dopo aspetta il Run.
    QString m_spaceLimitsApplied[6];
    // I 23 campi con la loro stringa nello stato.
    QList<QPair<QLineEdit *, QString *>> lineFieldTable();
    void bindLineFields();
    // Il testo dello stato per il campo `edit`, per le funzioni che ricevono
    // il campo come puntatore. Per un campo che non e' fra questi: il suo testo.
    QString lineText(const QLineEdit *edit) const;
    // Dal programma, a segnali bloccati: `state` e' un membro di m_scene.lim o m_scene.path.
    void setLineText(QString &state, const QString &text);
    // La VISTA dei 23 campi: i testi dello stato nei campi, a segnali bloccati
    // (nessuna conferma pendente, cursore all'inizio), poi le derivazioni che
    // i campi accenderebbero coi loro textChanged -- costanti in uso e tasti
    // Departure -- una volta sola. Chi assegna m_scene.lim / m_scene.path in
    // blocco (load, reset) la chiama dopo.
    void showLineFields();
    // I testi dei campi a una riga DAL PRESET, funzioni pure del file.
    static LimitTexts limitTextsFromItem(const LibraryItem &d);
    static PathTexts pathTextsFromItem(const LibraryItem &d);
    // Il ramo SCRIPT del load: il preset ha uno script e lo dichiara, o non ha
    // equazioni X valide (stessa condizione di applyCommonData).
    static bool loadsFromScript(const LibraryItem &d);
    // Equazioni, composizione, vincoli e flusso geodetico DAL PRESET.
    static EquationTexts equationTextsFromItem(const LibraryItem &d);
    // Domini delle costanti DAL PRESET: discrete ("A := int(1,6)") e minimi
    // ("F := min(0.3)"). Dallo script se la scena viene dallo script,
    // altrimenti dalla chiave "discreteConstants" del file (i minimi non ne
    // hanno una).
    static void constantDomainsFromItem(const LibraryItem &d,
                                        QHash<QString, DiscreteRange> *discrete,
                                        QHash<QString, float> *mins);
    // Le costanti DAL PRESET: i valori del file nel formato del load ('g', 6),
    // gia' scattati sui domini qui sopra.
    static ConstantTexts constantTextsFromItem(const LibraryItem &d);
    // Lo scatto di UNA costante sul suo dominio: intero piu' vicino dentro
    // [lo, hi], poi la soglia del minimo. Unica sede della regola.
    static float snapConstant(float v, const QString &letter,
                              const QHash<QString, DiscreteRange> &discrete,
                              const QHash<QString, float> &mins);
    // Equazione 3D, sezione 4D e rilievo DAL PRESET (la texture no: e' del
    // concetto texture). Un preset parametrico li lascia vuoti.
    static ImplicitTexts implicitTextsFromItem(const LibraryItem &d);
    // L'equazione 3D da dare al MOTORE: vuota o senza '=' (formato vecchio)
    // diventa la sfera di default, che non fa scattare il popup d'errore.
    static QString safeImplicitEquation(const QString &eq);
    // Steps: in parametrico la risoluzione della mesh, in Ray Marching i Ray
    // Steps. Lo slider e il campo ne sono la vista; il motore (setResolution /
    // setRaySteps) lo scrive chi chiama. setSteps: dal programma, a segnali
    // bloccati, col fondo corsa dello slider sul valore (pushStepsRange).
    void setSteps(int steps);
    // Fondo corsa dello slider Steps quando il valore si SCRIVE (campo, load):
    // kStepsSliderMax, o il valore se e' piu' grande. Lo slider stesso non lo
    // cambia: trascinandolo il fondo corsa resta quello scritto, come negli
    // slider delle costanti. A segnali bloccati: chi chiama non deve
    // stringerlo sotto il valore corrente.
    void pushStepsRange(int steps);
    // Il massimo che il campo Steps accetta (vedi kStepsPointBudget).
    int stepsLimit() const;

    bool m_blockTextureGen = false;
    // Alzato durante un cambio tab AUTOMATICO, cioe' deciso dal preset che si
    // sta caricando e non dall'utente. Due casi, stessa ragione:
    //  - una texture incompatibile col modo corrente (parametrica su RM o
    //    viceversa), che per essere mostrata impone il cambio;
    //  - un RECORD di modo opposto a quello a schermo (applyMotionExample
    //    forza il tab in base a data.isImplicitMode).
    // Quel setCurrentIndex fa scattare applyModeTabReset -> resetScene, che
    // richiude i rami della Library e ne azzera la selezione: giusto quando
    // l'utente scarta una superficie (tasto NEW, riclic sulla linguetta),
    // sbagliato qui, dove sta CARICANDO qualcosa e l'albero gli si chiuderebbe
    // sotto le dita perdendo il focus sull'item appena scelto.
    bool m_texModeSwitchInProgress = false;
    // Il ramo della Library da cui arriva cio' che ha IMPOSTO il cambio:
    // treeTextures per la texture incompatibile, treeMotions per il record di
    // modo opposto. resetScene non lo deseleziona. Lo dichiara chi alza il flag
    // qui sopra, invece di dedurlo da m_lastLoadedLibraryItem: le texture non
    // aggiornano quel puntatore, quindi dopo un record una texture incompatibile
    // lasciava "protetto" il ramo Record e il vecchio record restava evidenziato
    // su una scena che non era piu' la sua.
    QTreeWidget *m_modeSwitchSourceTree = nullptr;
    // Alzato quando l'utente ANNULLA il cambio di modalita' dal dialogo del
    // lavoro non salvato: tabBarClicked non puo' impedire a Qt di cambiare
    // linguetta, quindi si lascia scattare currentChanged e si dice ad
    // applyModeTabReset di non resettare nulla; subito dopo il tab torna
    // dov'era. Senza, "Cancel" avrebbe comunque distrutto la scena.
    bool m_suppressNextModeTabReset = false;
    // Gemello del precedente per il sotto-tab implicito (3D <-> Cross Section):
    // il cambio di sotto-tab carica la superficie di default e butta via la
    // scena esattamente come il cambio di modalita', quindi la conferma del
    // lavoro non salvato passa dallo stesso schema. Vedi il connect su
    // subTabImplicit->tabBar().
    bool m_suppressNextSubTabReset = false;
    // Gemello per il sotto-tab parametrico (Surface <-> Tubes), stesso schema:
    // vedi il connect su subTabParametric->tabBar().
    bool m_suppressNextTubesTabReset = false;

    // Riclic sulla linguetta GIA' ATTIVA ("ricomincia da capo"), distinto dal
    // cambio di modalita' vero. resetScene riceve solo l'indice di arrivo e non
    // puo' dedurlo: senza questo flag, tornare in Parametrico ripristinava
    // nell'editor lo script della texture appena azzerata, perche' il ramo di
    // allineamento non sa che questa volta la texture non sopravvive.
    bool m_sameTabRestart = false;

    QString lastTextureFolder;
    // NOME della texture di libreria attualmente in uso, salvato nel record
    // come texture["libName"] e riletto per ritrovarla nell'albero.
    // Serve perche' il focus in libreria si decide per UGUAGLIANZA DEL CODICE
    // (TextureCode::itemMatchesCode): un record porta la sua copia del sorgente, e
    // BASTA ritoccare la texture in libreria -- aggiungere uno slider, correggere
    // un commento -- perche' i record che la usano non la riconoscano piu'.
    // E' lo stesso schema con cui le texture IMMAGINE non hanno mai avuto il
    // problema: li' il confronto passa dal nome file nel tag //IMG:, che
    // sopravvive a qualunque modifica del codice attorno.
    // Vuoto = texture scritta a mano (non viene da libreria): in quel caso
    // l'unico aggancio possibile resta il codice.
    // Vale per la SOLA texture globale di superficie: lo sfondo ha la sua ancora,
    // qui sotto. Leggerla per cercare lo sfondo portava il focus sulla superficie.
    // Ancora gemella per la texture DI SFONDO: stesso ruolo, stesso ciclo di vita
    // (scritta caricando la texture dalla libreria col bersaglio Background,
    // salvata nel record in "background"/"libName", letta al load, azzerata dal
    // reset di scena). Senza, uno sfondo il cui codice e' cambiato in libreria
    // perdeva il focus e nessun Sync poteva riallinearlo.
    // Ancora del SUONO: nome della voce della Library Sounds da cui viene
    // l'audio in scena, salvato nel record come "soundLibName". Il suono vive
    // solo come codice dentro texture.code (//MUSIC: o blocco SOUND_BEGIN),
    // quindi un ritocco dello script in libreria (es. il volume finale) gli
    // faceva perdere il focus. Scritta dal click in Library, letta al load,
    // azzerata dal reset di scena e dal load di una superficie.

    // File del RECORD attualmente in scena. Serve a "Sync Focused Texture", che
    // deve agire solo quando il click destro cade sul record caricato: la
    // selezione dell'albero non basta, segue il click e non dice cosa c'e'
    // davvero a schermo. Vuoto = nessun record caricato (scena costruita a mano,
    // superficie sola, o dopo un NEW).
    QString m_currentRecordPath;
    QString m_currentTexturePresetPath;

    // Lo script di superficie dell'ultimo Run o del preset: quello a schermo, e
    // quello che il Save scrive. (Era la property dinamica "rawSurfaceScript".)
    // Corpo GLSL dello script metrico (direttive := rimosse, non tradotto).
    // Non vuoto = il flusso geodetico usa il tensore g_ij dello script invece
    // della metrica indotta dall'embedding X/Y/Z/P.
    QString m_metricScriptBody;
    // Run dello script lanciato dal LOAD di un preset: lo stato salvato
    // (limiti, costanti, steps e condizioni iniziali, eventualmente modificati
    // dall'utente dopo il Run), gia' assegnato da applyCommonData, ha la
    // precedenza sulle direttive := dello script, che valgono solo al Run
    // dell'utente (parseAndApplyScriptParams non applica i valori; lo script
    // metrico riempie solo le condizioni iniziali vuote).
    bool m_scriptRunFromLoad = false;
    // Firma dell'ultima combinazione metrica+condizioni per cui è già stato
    // mostrato l'avviso "costante ambigua": evita di ripeterlo a ogni frame di
    // animazione o a ogni tweak di slider con la stessa configurazione.
    QString m_lastAmbiguousConstSig;
    void checkMetricConstantAmbiguity();
    // COSTANTE CONTESA fra superficie e texture. Le sette costanti A..F/S sono
    // un solo set globale (un unico mathParams nell'UBO): se la texture che si
    // sta applicando rivendica una lettera gia' usata dalla superficie, quello
    // slider ne muove due insieme -- es. la massa del buco nero e la densita'
    // delle scanline. Non e' un errore (puo' essere voluto), quindi si CHIEDE:
    // false = l'utente ha annullato, la texture non va applicata.
    // La libreria di fabbrica non ha conflitti: il caso nasce quando l'utente
    // unisce una superficie e una texture scritte separatamente.
    // forBackground: la texture in arrivo va sullo SFONDO. Cambia solo CONTRO
    // COSA si confronta -- sfondo vs (superficie + texture), texture vs
    // (superficie + sfondo) -- perche' i tre moduli leggono lo stesso
    // mathParams e ogni coppia puo' contendersi una lettera.
    bool confirmTextureConstantClash(const QString& texCode, const QString& dispCode,
                                     bool forBackground);
    // Mappa di visualizzazione (embedding) di uno script metrico. Salva i campi
    // x/y/z/p in root solo se sono una mappa custom (non la carta identità);
    // applica al load reimpostando i campi. Vuoto = identità, non serializzato.
    void writeMetricDisplayMap(QJsonObject& root) const;
    bool metricDisplayMapIsCustom() const;

    // ==========================================================
    // MOTION & PATHS
    // ==========================================================
    QTimer *navTimer;
    QSet<int> activeNavActions;
    // Tasti di spostamento a click dei dock 3D/4D (X±, Y±, left, right, roll, ...),
    // raccolti in connectNavButton per abilitarli/disabilitarli in blocco: durante
    // un path la telecamera segue il percorso e questi comandi non hanno senso.
    QVector<QPushButton*> m_navButtons;

    // I PATH DELLA CAMERA (4D e 3D): orologi, tempi, posa del path 4D e la
    // camera che ne segue la traiettoria (camerapaths.h). Creati in
    // setupMotionDocks; nullptr fino ad allora, e qualcuno chiede gia' prima se
    // un path e' in corso (updateProjectionButtonText, ~700 righe prima nel
    // costruttore): per questo si chiede SEMPRE a pathRunning, che regge il
    // nullptr. Un puntatore non inizializzato faceva crashare l'avvio su iOS
    // (EXC_BAD_ACCESS in QBindingStorage::registerDependency).
    CameraPaths *m_paths = nullptr;
    bool pathRunning(CameraPaths::Path p) const { return m_paths && m_paths->isRunning(p); }

    // Avanzamento per tick dei due path. STATO: m_scene.pathSpeed3D/4D, in unita'
    // dello slider (1..100); speed3DSlider/speed4DSlider ne sono la vista
    // (setPathSpeed3D/4D a segnali bloccati; il trascinamento scrive lo stato).
    float pathSpeed3D() const;
    float pathSpeed4D() const;
    void setPathSpeed3D(int speed);
    void setPathSpeed4D(int speed);

    // (FOV dei path: m_scene.fov.)

    // (Base dell'orientamento 4D del path e "primo Departure della sessione":
    // in CameraPaths, captureBase4D e takeFirstStart.)

    // Vista dei due path: UNICO punto che scrive m_scene.pathViewMode4D /
    // m_scene.pathViewMode3D e il testo dei loro tasti (pushView / pushView3D).
    void setPathViewModes(CameraPathMode mode4D, CameraPathMode mode3D);
    // Comandi dei moti camera ai default della scena nuova (NEW, cambio di
    // modalita' o di sotto-tab, load): viste Tangent, velocita' dei path 10,
    // nessun moto "ultimo avviato", nessun path compilato nel motore. I record
    // riscrivono subito dopo i propri valori.
    void resetMotionControls();
    // Ultimo moto camera avviato ("rotation" | "path4D" | "path3D", "" = mai):
    // con rotazioni e path entrambi compilati, applyStartSideEffects riavvia
    // SOLO questo (la vecchia cascata faceva vincere sempre il path 3D). Al
    // load di un record viene impostato dalla chiave JSON "activeMotion".

    bool m_masterStopped = false;
    // L'utente ha fermato il suono ESPLICITAMENTE (tasto Stop Sound): in tal caso
    // applyStartSideEffects() non deve riaccenderlo a ogni re-commit di equazione/
    // texture (Enter ad animazione attiva passa per onStartClicked). Si riarma solo
    // su un vero master Start. Stesso pattern di m_masterStopped.
    bool m_userStoppedSound = false;

    // Stessa famiglia di m_userStoppedSound, per gli altri moduli: l'utente ha
    // fermato ESPLICITAMENTE il clock (Stop del dock texture/script o del dock
    // Equations). Senza questi flag lo stop viveva solo nel GLWidget e ogni
    // ricalcolo globale (applyAnimationState) lo sovrascriveva: con path,
    // rotazioni o t-motion in corso, accendere lo sfondo o togglare la checkbox
    // Texture faceva RIPARTIRE la texture (o la geometria) fermata a mano.
    // Si riarmano su Run esplicito del proprio modulo, load di preset/record
    // e master Start (il master governa tutti i moduli).
    bool m_userStoppedTexClock  = false;
    bool m_userStoppedBgClock   = false;
    bool m_userStoppedGeomClock = false;
    // CHI CHIEDE IL RUN (punto 5, tappa 5.5). Prima lo si deduceva da sender(),
    // da flag alzati attorno alla chiamata (Invio, commit di servizio) e da
    // m_populatingFields (load): ora e' un argomento di runScene.
    enum class RunOrigin {
        Program,        // altri percorsi del programma (texture dalla libreria...)
        MasterButton,   // tasto master START/STOP
        DockRun,        // tasti Run del dock Equations (parametrico / Ray Marching)
        EnterKey,       // Invio su un campo equazione: come il Run, riaccende il
                        // clock della geometria fermato a mano (gli altri
                        // percorsi senza tasto non devono)
        ServiceCommit,  // commit di servizio (Invio su una costante o su un
                        // limite): applica senza far ripartire moto, rotazioni
                        // e audio
        Load            // il Run lanciato dal load di un preset: la scena e' il
                        // file, niente popup di equazioni incomplete
    };
    // Il corpo del Run. onStartClicked (lo slot dei tasti) ricava l'origine dal
    // sender; dockBtn e' il tasto del dock premuto (per il suo STOP).
    void runScene(RunOrigin origin, QPushButton *dockBtn = nullptr);

    // Campi numerici del dock 4D: quanto l'utente ha mosso COI TASTI da
    // resetNav4DBaseline in poi. Si ACCUMULA qui a ogni scatto, invece di
    // dedurlo per differenza da una fotografia dello stato 4D.
    // La differenza non funzionava: omega/phi/psi avanzano anche da soli
    // (GLWidget::advanceRotationsBy, rotazioni 4D in corsa) e la stessa camera
    // e' mossa dal path, quindi "stato corrente - baseline" conteneva pure il
    // moto automatico. Bastava premere un tasto qualsiasi perche' il campo di
    // omega saltasse di colpo a tutta la rotazione accumulata nel frattempo --
    // un numero che l'utente non aveva prodotto.
    // Accumulando solo gli scatti, ogni campo riporta i suoi e nient'altro, e
    // i moti automatici non lo toccano.
    QVector4D m_nav4DDeltaObs = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // X/Y/Z, e W = P parametrico
    float     m_nav4DDeltaCsP   = 0.0f;   // P in Ray Marching (quota della sezione)
    float     m_nav4DDeltaOmega = 0.0f;
    float     m_nav4DDeltaPhi   = 0.0f;
    float     m_nav4DDeltaPsi   = 0.0f;
    // Stesso ruolo, per gli orologi delle SINGOLE mesh: alzato dallo Stop in
    // ambito "Mesh", impedisce ai ricalcoli di applyAnimationState (commit di
    // equazione, load, toggle sfondo) di riaccendere una fascia fermata a mano.
    // Non distingue QUALE fascia: e' un gate sul ricalcolo di massa, non lo
    // stato dei singoli orologi, che vivono in MeshPart::texAnimating.
    bool m_userStoppedMeshTexClock = false;
    // Stesso ruolo per l'orologio della texture del BORDO: alzato dallo Stop
    // del dock Script col bersaglio Border, riarmato da master Start, Run,
    // Library e da una scena nuova (setBorderUI).
    bool m_userStoppedBorderTexClock = false;

    // Moto CAMERA (path 4D/3D o rotazioni GO) fermato ESPLICITAMENTE: STOP su
    // Departure, pausa del GO o master STOP. Senza questo flag un commit di
    // equazione (Enter -> onStartClicked -> applyStartSideEffects) riavviava
    // m_scene.lastCameraMotion pur con tutto fermo a mano. Si riarma su master Start,
    // avvio esplicito di un moto camera e load di preset/record (applyCommonData).
    bool m_userStoppedCameraMotion = false;

    // LAVORO NON SALVATO, PER VALORE. La scena e' "da salvare" quando il suo
    // stato DIFFERISCE da quello dell'ultimo momento pulito (load di un preset o
    // di un record, reset, salvataggio), non quando un controllo e' stato
    // toccato: riportare uno slider dov'era la rende di nuovo pulita, e un
    // comando che nessuno aveva cablato e' protetto lo stesso.
    //
    // Lo stato si legge dall'IMPRONTA della scena (sceneFingerprint): cio' che
    // Save Record scriverebbe adesso, appiattito in chiave -> valore. Ogni
    // chiave appartiene a una parte, e le parti seguono le regole decise per
    // l'avviso "vuoi salvare?":
    //   PartCore        comandi che agiscono subito (aspetto, costanti, moti,
    //                   path, limiti per-mesh...): contano appena cambiano.
    //   PartSurfaceText equazioni, limiti, script della superficie: contano
    //                   solo DOPO che sono andati a schermo (m_appliedSurface),
    //                   non mentre sono testo scritto e mai eseguito o che da'
    //                   errore.
    //   PartTexture     texture di superficie, di sfondo e delle fasce: lavoro
    //                   del MODULO texture, col suo avviso e il suo ramo.
    //   PartSound       il suono: lavoro del MODULO suono.
    // La POSA (camera, angoli 4D, osservatore) non e' nell'impronta: la muovono
    // anche rotazioni e path, da soli. Che l'abbia mossa l'utente lo dicono gli
    // eventi (m_viewTouched), come lo stato di esecuzione in genere.
    using SceneFingerprint = QMap<QString, QJsonValue>;
    enum FingerprintPart { PartCore, PartSurfaceText, PartTexture, PartSound };
    static FingerprintPart fingerprintPart(const QString &key);
    SceneFingerprint sceneFingerprint() const;
    static SceneFingerprint fingerprintSlice(const SceneFingerprint &fp, FingerprintPart part);

    SceneFingerprint m_cleanScene;      // tutta l'impronta all'ultimo momento pulito della scena
    SceneFingerprint m_appliedSurface;  // PartSurfaceText com'era l'ultima volta che e' andato a schermo
    SceneFingerprint m_cleanTexture;    // PartTexture all'ultimo momento pulito del MODULO
    SceneFingerprint m_cleanSound;      // PartSound, idem
    // Texture e suono presi dalla LIBRARY dopo l'ultimo momento pulito della
    // scena: non sono lavoro del modulo (sono gia' su disco), ma la scena che li
    // usa e' cambiata, e il record e' l'unico file che lo conserva.
    SceneFingerprint m_pickedTexture;
    SceneFingerprint m_pickedSound;
    bool m_viewTouched = false;         // posa mossa dall'utente (mouse, touch, tasti di camera)
    bool m_textureViewTouched = false;  // inquadratura 2D della texture mossa nella vista piatta
    bool m_lastSaveSucceeded = false;   // esito dell'ultimo salvataggio (lo legge confirmDiscardUnsaved)

    // I momenti PULITI. Scena: tutto e' su disco o appena caricato. Surface:
    // Save Surface (i moduli texture/suono restano com'erano). Texture/suono
    // "picked": presi dalla Library; "saved": scritti nel loro file.
    void markSceneClean();
    void markSurfaceSaved();
    void markTexturePicked();
    void markTextureSaved();
    void markSoundPicked();
    void markSoundSaved();
    // Il testo della superficie e' andato a schermo (Run riuscito, limite
    // confermato): da qui conta per l'avviso. `prefixes` vuoto = tutte le chiavi.
    void noteSurfaceApplied(const QStringList &prefixes = QStringList());
    bool textureModuleDirty() const;
    bool soundModuleDirty() const;
    // Le chiavi che rendono sporchi scena e moduli ("scena:colors/alpha"...):
    // per i test e per la diagnosi.
    QStringList unsavedKeys() const;
    // Cambi che NON sono lavoro dell'utente (il checkbox che mostra/nasconde la
    // texture, la trasparenza tolta dal watchdog): cio' che cambia mentre la
    // guardia e' viva entra anche nei riferimenti puliti, cosi' una scena pulita
    // resta pulita e una sporca resta sporca.
    void absorbChangesSince(const SceneFingerprint &before);
    struct AbsorbChangesGuard {
        MainWindow *w;
        SceneFingerprint before;
        explicit AbsorbChangesGuard(MainWindow *mw) : w(mw), before(mw->sceneFingerprint()) {}
        ~AbsorbChangesGuard() { w->absorbChangesSince(before); }
    };
    // Momento pulito all'uscita da un load o da un reset, qualunque uscita sia.
    // E' anche l'apertura della scena: la sua posa e' quella a cui torna il RESET.
    struct SceneCleanGuard {
        MainWindow *w;
        ~SceneCleanGuard() { w->markSceneClean(); w->captureStartPose(); }
    };
    // POSA D'APERTURA DELLA SCENA (load, reset, NEW, avvio). Non al Save: il
    // RESET riporta a come la scena si e' presentata, non all'istante in cui
    // e' stata salvata.
    void captureStartPose();
    GLWidget::Framing m_startFraming;
    GLWidget::Orientation m_startOrientation;
    float m_startFov = 45.0f;

    // Salvataggio in corso DENTRO la conferma "vuoi salvare?": la libreria si
    // aggiorna ma il focus non si sposta sul file salvato, perche' subito dopo
    // viene caricato un altro preset ed e' QUELLO che si sta guardando. Lo
    // legge refreshAndSelectPreset, che i writer chiamano a 100ms -- cioe' a
    // caricamento gia' avvenuto. Rilasciato con lo stesso ritardo, dopo che
    // quei timer sono scattati.
    bool m_suppressSelectAfterSave = false;

    // ULTIMO ITEM DELLA LIBRARY EFFETTIVAMENTE CARICATO (superficie o record).
    // Serve a rimettere il focus dov'era quando l'utente ANNULLA il caricamento
    // di un preset: senza, l'albero resta deselezionato e non indica piu' nulla.
    // Puo' vivere in un albero diverso da quello cliccato (una superficie mentre
    // si clicca un record), per questo si risale a `previous->treeWidget()`.
    // Azzerato dove gli alberi vengono ricostruiti (refreshLibrary): non essendo
    // QTreeWidgetItem un QObject, nessun QPointer lo azzera da solo e il
    // puntatore resterebbe pendente.
    QTreeWidgetItem *m_lastLoadedLibraryItem = nullptr;

    // All'uscita dello scope, se durante la sua vita nessun errore e' stato
    // mostrato all'utente, il Run e' RIUSCITO: le costanti si riallineano e il
    // testo della superficie conta come andato a schermo (noteSurfaceApplied). Da istanziare in cima a OGNI
    // funzione che applica la superficie (onStartClicked, onRunCurrentScript):
    // funziona qualunque sia l'uscita presa, e quelle funzioni ne hanno decine.
    // NB: dichiarata fuori dalle sezioni slot -- moc rifiuta le struct li'.
    struct RunOutcomeGuard {
        MainWindow *w;
        quint64 before;
        bool armed;
        // false quando la chiamata non ha portato a schermo il testo della
        // superficie (Stop del dock o del master) o non le equazioni (commit di
        // servizio, che riusa quelle gia' applicate).
        bool surfaceApplied = true;
        bool equationsApplied = true;
        explicit RunOutcomeGuard(MainWindow *mw, bool arm = true);
        ~RunOutcomeGuard();
    };

    // QUALE DOCK DEFINISCE LA SUPERFICIE A SCHERMO. Stato ESPLICITO, non
    // dedotto: la versione precedente lo indovinava a ogni battitura guardando
    // se i campi erano pieni ("default a schermo" + "script non vuoto"), e
    // la deduzione si sfasava dalla realta' in entrambi i versi -- m_scene.surfaceScriptText
    // non veniva mai svuotato da un Run delle equazioni (avviso a sproposito),
    // e chi costruiva nel dock Script partendo dal default lasciava il flag di
    // default acceso per sempre (avviso mai piu' mostrato).
    //
    // Cambia SOLO al load di preset/record e al reset di modalita'. Il Run NON
    // la sposta: all'altro dock non ci si arriva in stato scrivibile senza aver
    // gia' visto l'avviso, e farla trasferire dal Run generava ereditarieta' di
    // impostazioni fra i due moduli.
    enum SurfaceOrigin {
        OriginDefault,     // toro/sfera di partenza: nessun dock e' "impegnato"
        OriginEquations,   // dock Equations (x/y/z/p o equazione implicita)
        OriginScript,      // dock Script (script di superficie)
        // SCRIPT METRICO (return mat3): i due dock non si contendono la scena,
        // la descrivono INSIEME -- la metrica g_ij nello script, la carta e le
        // condizioni iniziali del flusso geodetico nelle Equations (vedi
        // doc_geodesic, "Script & Equations: who decides"). Un preset del genere
        // popola legittimamente entrambi i dock, quindi non deve mai generare
        // l'avviso di conflitto: e' compatibile con qualunque dock si scriva.
        OriginBoth
    };
    SurfaceOrigin m_surfaceOrigin = OriginDefault;

    // AVVISO "un altro dock e' gia' carico": mostrato una volta per SITUAZIONE,
    // non a ogni tasto premuto. La situazione e' la coppia (dock in cui scrivo,
    // dock sorgente): una coppia diversa e' un conflitto diverso e merita il suo
    // avviso. Riarmato dal reset e da ogni nuovo load.
    // OriginDefault = nessun avviso ancora mostrato.
    SurfaceOrigin m_warnedEditedDock = OriginDefault;
    SurfaceOrigin m_warnedOrigin     = OriginDefault;

    // true mentre un load (applyCommonData, applyMotionExample) o un reset
    // (resetScene) riempie la scena. NON serve piu' a distinguere la
    // digitazione dalle scritture del programma: load e reset scrivono i campi
    // a segnali bloccati (setEqText, setRmText, setScriptText, setConstText),
    // e i gestori pensati per l'utente stanno su segnali che solo lui emette
    // (textChanged dei campi multilinea, textEdited, clic); il Run del load ha
    // la sua origine (RunOrigin::Load). Lo legge soltanto il test degli
    // scenari, che controlla che nessun campo di testo emetta textChanged
    // mentre e' alzato.
    bool m_populatingFields = false;
    // Load e reset scartano la digitazione in attesa di conferma nei campi
    // limite (userEditPending): la scena che l'utente stava modificando non c'e'
    // piu', e un editingFinished arrivato dopo (il Run del load sposta il
    // focus) non deve applicare un testo che non ha scritto.
    void discardPendingLimitEdits();

    // Run del dock Equations (tab Parametric) senza animazione (nessun 't'):
    // dopo aver applicato la modifica grafica il tasto va DISABILITATO finché le
    // equazioni non vengono modificate di nuovo. true = già applicato, niente da
    // rieseguire. Per le equazioni animate il tasto resta Run/Stop e questo flag
    // non lo tocca (vedi updateMasterButtonState). Parte da true: all'avvio la
    // superficie di default è già renderizzata, quindi non c'è nulla da applicare.
    bool m_parametricApplied = true;

    // Stessa logica per il Run del tab Ray Marching (btnImplicit): senza 't'
    // nell'EQUAZIONE implicita (il displacement è del modulo texture, non conta)
    // il tasto si disabilita dopo l'applicazione finché l'equazione non cambia.
    // Parte da true: all'avvio la sfera implicita di default è già renderizzata.
    bool m_implicitApplied = true;

    // Stessa logica one-shot per il Run della TEXTURE Ray Marching (btnTextureCode,
    // alimentato da lineTexture + lineVariations): senza 't' negli script il tasto
    // si disabilita dopo l'applicazione finché uno dei due script non cambia. Con
    // animazione resta Run/Stop. Se entrambi i campi sono vuoti il tasto è
    // disabilitato a prescindere (vedi updateMasterButtonState). Parte da true:
    // all'avvio non c'è texture da applicare.
    bool m_rmTextureApplied = true;

    // false durante la costruzione di MainWindow, true alla fine. Scrivere le
    // equazioni di default in costruzione emette textChanged, che invocherebbe
    // updateMasterButtonState() quando sotto-oggetti come m_audioController
    // (QMediaPlayer interno) non sono ancora pronti -> crash in Release. La
    // guardia in updateMasterButtonState() salta finché la UI non è completa.
    bool m_uiReady = false;

    // ==========================================================
    // LIBRARY & FILE SYSTEM
    // ==========================================================
    LibraryManager m_libraryManager;
    LibraryMenuController* m_menuController;
    PresetSerializer *m_presetSerializer;
    LibraryFileOperations *m_fileOps;
    LibraryDragDropHandler *m_dragDropHandler;

    QList<DeletionBackup> m_undoStack;
    QStringList m_cutFilePaths;
    QStringList m_cutTexturePaths;
    bool m_isCopyOperation = false;
    bool m_libraryInitialized = false;

    QFileSystemWatcher *m_fsWatcher = nullptr;
    QTimer *m_fsSyncTimer = nullptr;

    // ==========================================================
    // MEDIA & RECORDING
    // ==========================================================
    VideoRecorder *m_videoRecorder;

    bool m_isRecording = false;
    bool m_stopRecordingRequested = false;
    bool m_isProcessingVideo = false;
    QString m_recFolder;

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================

    // --- Data & Initialization ---
    void setupDefaultFolders();
    // Sezioni del costruttore, chiamate nell'ordine (vedi MainWindow::MainWindow).
    void setupStyling();
    void setupDockLayout();
    void setupActionsAndMenus();
    void setupStatusBar();
    void setupEquationsDock();
    void setupRendererDock();
    void setupMotionDocks();
    void setupScriptDock();
    void setupLibraryDock();
    void finishStartup();
    void setupDesktopFilters();
    void connectSidePanels();
    void connectNavButton(QPushButton *btn, int action);
    // Abilita/disabilita in blocco i tasti di spostamento dei dock 3D/4D e blocca i
    // comandi mouse 3D (rotazione/zoom) del glWidget mentre un path e' attivo.
    void setNavControlsEnabled(bool enabled);

    // --- Library & File I/O ---
    void refreshRepositories();

    // RILETTURA DELLA LIBRERIA CHE NON SPEGNE I MOTI.
    //
    // Da usare al posto di refreshRepositories() nelle voci di menu che cambiano
    // cartella. Rilegge la libreria e poi RIMETTE i moti come stavano PRIMA che
    // showMenu li fermasse -- stato che il chiamante deve catturare prima, e
    // passare qui, perche' a quel punto i timer sono gia' fermi e non lo direbbero
    // piu'. Vedi la spiegazione estesa in librarymenucontroller.cpp.
    void refreshLibraryPreservingMotion(bool wasRotating, bool wasPath4D,
                                        bool wasPath3D, bool wasTimeAnimating);

    // Rimette i moti come erano, SENZA ricostruire la libreria. Per le uscite
    // anticipate delle voci di menu (dialogo annullato, cartella rifiutata):
    // li' non e' cambiato nulla, ma showMenu li ha gia' fermati e il suo
    // ripristino gira prima della singleShot che non verra' mai schedulata.
    void restoreMotionState(bool wasRotating, bool wasPath4D,
                            bool wasPath3D, bool wasTimeAnimating);

    // "E' in corso una ricostruzione degli alberi della libreria."
    //
    // Il rebuild fa riemettere customContextMenuRequested e RIENTRA in
    // showMenu, che ferma i moti, ne legge lo stato e lo ripristina in fondo.
    // Ma quella lettura avviene a timer GIA' fermi -- li ha fermati la
    // chiamata esterna -- quindi il rientro registra "fermo" e alla sua uscita
    // spegne moti che l'utente aveva acceso.
    // Con questo flag showMenu non tocca affatto i moti durante un rebuild: ne'
    // li ferma ne' li ripristina, quindi non puo' falsarli. Chi ha avviato il
    // rebuild sa qual era lo stato vero e lo rimette lui.
    // NON e' la guardia di rientranza tentata (e revertita) il 2026-08-19:
    // quella impediva l'APERTURA del menu, che qui resta intatta.
    bool libraryRebuildInProgress() const { return m_libraryRebuildDepth > 0; }
    void refreshAndSelectPreset(QTreeWidget *tree, const QString &path);
    void updateWatcherPaths();
    void copyPath(QString src, QString dst);
    QTreeWidgetItem* getCurrentLibraryItem();
    // Collassa e ingrigisce (grayed=true) o ripristina (false) il ramo Texture del
    // dock Library. Usato per riflettere che in surface-wireframe la texture non e'
    // applicabile. Idempotente sul colore; il collasso e' one-shot (non riespande).
    void setTextureLibraryGrayed(bool grayed);
    // Per valore come applySurfaceExample/applyMotionExample: e' chiamata da
    // entrambe e ne condivide il rischio (vedi il commento la' sopra).
    // `file` e' la scena del preset (sceneFromItem), calcolata una volta dal
    // chiamante: la testa la assegna a m_scene, concetto per concetto.
    void applyCommonData(LibraryItem data, const SceneState &file);
    // Shell/Solid del Ray Marching: UNICA implementazione condivisa fra i due
    // rami di load (equazione implicita e script implicito) e il reset alla
    // sfera di default. I due rami avevano il ripristino solo nel ramo
    // EQUAZIONE, e siccome quasi tutti i record RM sono da SCRIPT i radio non
    // si resettavano mai caricando un record; resetScene non lo faceva affatto.
    void applyImplicitShellMode(bool shell);
    // Shell/Solid senza il motore: stato + radio a segnali bloccati. Per un
    // preset parametrico, dove la modalita' globale del motore e' la resa
    // (Basic/Phong) e non va toccata.
    void setImplicitShell(bool shell);
    // Shell/Solid: lo STATO (m_scene.implicitShell); i radio ne sono la vista, la
    // modalita' globale del motore in Ray Marching (1 = Shell) il derivato.
    // Lo scrivono solo setImplicitShell e applyImplicitShellMode.
    bool implicitShellSelected() const { return m_scene.implicitShell; }
    // Testo dell'equazione implicita del sotto-tab ATTIVO (3D o Cross Section).
    // Da usare ovunque serva "l'equazione a schermo": leggere ui->lineEquation
    // fisso ignora il Cross Section.
    QString activeImplicitEquationText() const;
    // Porta motore, slider ed etichetta allo spessore di guscio indicato. Lo
    // slider ha una curva QUADRATICA (piu' risoluzione sui valori sottili),
    // quindi posizionarlo richiede la funzione inversa: sta qui, in un solo
    // posto, invece di essere ricalcolata a ogni punto di caricamento.
    // A segnali bloccati: il valore arriva da un preset, non dall'utente.
    void setShellThicknessUI(float thickness);
    // Radio Fast/Precise del marcher: scrive motore e radio a segnali bloccati.
    void setMarcherUI(bool precise);
    // Luce di riempimento: motore + slider + etichetta, a segnali bloccati.
    void setFillLightUI(float v);
    // BORDO (dock Renderer): spessore, colore, modalita' e densita' nel motore,
    // slider ed etichetta a segnali bloccati. Per load e reset, che non sono
    // modifiche dell'utente.
    void setBorderUI(const GLWidget::BorderStyle &style);
    // Superficie di default del sotto-tab Cross Section (T^3, 3-toro): stessa
    // idea della sfera di default per il tab 3D, ma per l'equazione a 4
    // variabili (x,y,z,p). Riusata sia da resetScene (arrivo su Implicit) sia
    // dal cambio di sotto-tab (3D <-> Cross Section) — vedi CLAUDE.md, mai
    // duplicare logica live: unica implementazione condivisa.
    void loadCrossSectionDefaultSurface();
    // ACCENSIONE DELLA TEXTURE DI SUPERFICIE nel motore, derivata dall'intenzione
    // m_scene.surfaceTextureState. Vedi l'implementazione.
    void applySurfaceTextureToEngine();
    // ----------------------------------------------------------
    // DOCK RENDERER: BERSAGLIO E CHECKBOX TEXTURE
    // ----------------------------------------------------------
    // Il checkbox "Texture" e' UN widget per tre texture: lo sfondo (bersaglio
    // Background), la fascia selezionata (ambito Mesh) o la superficie. E' una
    // VISTA: lo stato sta altrove (lo sfondo nel motore, la fascia nella sua
    // MeshPart, la superficie in m_scene.surfaceTextureState), e chi vuole sapere se
    // una texture e' accesa lo chiede allo stato, non al checkbox -- che col
    // bersaglio su Background mostra lo sfondo. Leggendolo come "la texture di
    // superficie e' accesa", master Start e Run non riavviavano la texture
    // della superficie se lo sfondo era spento.
    //
    // La texture che il checkbox mostra col bersaglio SURFACE: quella della
    // fascia selezionata (ambito Mesh) o della superficie; spenta in wireframe,
    // dove non si disegna.
    bool surfaceTextureShown() const;
    // La texture del bersaglio corrente: lo sfondo, o surfaceTextureShown().
    bool targetTextureOn() const;
    // Il MODULO texture di superficie disegna qualcosa: la texture mostrata
    // qui sopra o quella di una fascia qualunque. Vale col dock su qualunque
    // bersaglio: e' la domanda di chi governa l'orologio della texture.
    bool surfaceTextureModuleActive() const;
    // Il codice del modulo texture di superficie nella modalita' corrente: in
    // Ray Marching colore e rilievo, in parametrico la texture globale e quelle
    // delle fasce (allSurfaceTextureCode).
    QString surfaceTextureModuleCode() const;
    // LA VISTA: etichetta, abilitazione e spunta del checkbox dal bersaglio e
    // dallo stato della sua texture. A segnali bloccati.
    void refreshTextureCheckbox();
    // Riporta il bersaglio su Surface dal programma (reset, load), a segnali
    // bloccati, e riallinea il checkbox. Il resto del dock lo riallinea chi
    // chiama, come prima.
    void showSurfaceTarget();
    // MODALITA' Parametric / Implicit (Ray Marching): STATO. La linguetta
    // principale (ui->tabModeSelector) ne e' la vista; la modalita' del motore
    // (GLWidget::getEngineMode) il derivato, scritto da resetScene e dai load.
    // Chi vuole sapere la modalita' chiede a implicitMode(), non alla linguetta.
    bool implicitMode() const { return m_scene.implicitMode; }
    // Dal programma: stato + linguetta a segnali bloccati, SENZA il reset del
    // clic (load che riscrivono la scena da se', ripristino dopo Annulla). Chi
    // vuole il reset cambia la linguetta a segnali vivi, come un clic.
    void setImplicitMode(bool on);
    // AMBITO All / Mesh: STATO. I radio ne sono la vista; nel motore ne
    // derivano setMeshAppearanceUniform e la parte attiva (-1 in All). Chi
    // vuole sapere l'ambito chiede a meshScopeAll(), non ai radio.
    bool meshScopeAll() const { return m_scene.meshScopeAll; }
    // Dal programma (gate del selettore, ambito del preset al load): stato +
    // radio a segnali bloccati. Il motore lo scrive chi chiama.
    void setMeshScopeAll(bool all);
    // SOTTO-TAB DEL RAY MARCHING (3D / Cross Section): STATO. La linguetta
    // (ui->subTabImplicit) ne e' la vista; il ramo che il motore compila
    // (GLWidget::implicitUsesCrossSection) e' l'applicato. Chi vuole sapere
    // quale sotto-tab e' attivo chiede a crossSectionTab(), non alla linguetta.
    bool crossSectionTab() const { return m_scene.crossSectionTab; }
    // Dal programma (load, ripristino dopo Annulla): stato + linguetta a
    // segnali bloccati, senza il reset che fa il clic.
    void setCrossSectionTab(bool on);
    // SOTTO-TAB DEL PARAMETRICO (Surface / Tubes): STATO, m_scene.tubesTab.
    // La linguetta (ui->subTabParametric) ne e' la vista. Sulla linguetta Tubes
    // i limiti v e w non servono (il tubo ha come variabili il parametro della
    // curva e lo spessore): le loro righe lasciano il posto alla riga
    // Thickness. tubesShown(): Tubes e' davanti (tab Parametric e sotto-tab
    // Tubes), cioe' il Run costruisce un tubo.
    bool tubesShown() const { return m_scene.tubesTab && !implicitMode(); }
    // Dal programma: stato + linguetta a segnali bloccati, poi la vista.
    void setTubesTab(bool on);
    void refreshTubeControls();
    bool m_tubeControlsShown = false;   // ultima vista applicata da refreshTubeControls
    // Campi del sotto-tab Tubes: QPlainTextEdit per la curva, QLineEdit per
    // lo spessore (lo scritto sta in m_scene.tube, bindEquationFields).
    QWidget *tubeFieldEdit(TubeField field) const;
    void setTubeText(TubeField field, const QString &text);
    // SPESSORE DEL TUBO: il numero del campo; il raggio nel motore e'
    // spessore * kTubeRadiusUnit (1 = il tubo del trifoglio di default, tarato
    // con l'utente il 2026-10-07). Minimo kTubeThicknessMin (a 0 il tubo
    // sparisce). kTubeThicknessMax e' il massimo dello SLIDER, non del campo:
    // come per le costanti, un valore scritto piu' grande allarga lo slider.
    static constexpr float kTubeRadiusUnit = 0.12f;
    static constexpr float kTubeThicknessMin = 0.01f;
    static constexpr float kTubeThicknessMax = 3.0f;
    // Spessore dal suo campo, almeno kTubeThicknessMin (vuoto o illeggibile
    // -> ok false: Run spento).
    float tubeThicknessValue(bool *ok = nullptr) const;
    // Invio sul campo dello spessore (dai filtri tastiera): vuoto -> 1, il
    // default (campo, slider e tubo riallineati); illeggibile -> popup e focus
    // di nuovo sul campo. Solo all'Invio: uscire dal campo (anche cliccando
    // NEW) non tocca nulla, il Run resta spento.
    void commitTubeThicknessOnEnter();
    // Spessore -> slider (range e valore, a segnali bloccati) e motore
    // (u_tubeRadius).
    void pushTubeThickness();
    // IL BERSAGLIO Surface / Border / Background: su cosa agiscono slider
    // colore, checkbox Texture, Library, dock Script e vista 2D. E' STATO; i tre
    // radio, lo slider dello spessore del bordo e il bersaglio della vista 2D
    // nel motore (GLWidget::setFlatViewTarget) ne sono le viste. Chi vuole
    // sapere dove si edita chiede a editingBackground() / editingBorder(), non
    // al radio. Col bersaglio Border gli slider RGB editano il colore del bordo
    // (o il Color 1/2 della sua texture), lo slider Border Thickness il suo
    // spessore, il checkbox Texture e la Library la sua texture.
    enum class EditTarget { Surface, Border, Background };
    EditTarget m_editTarget = EditTarget::Surface;
    bool editingBackground() const { return m_editTarget == EditTarget::Background; }
    bool editingBorder() const { return m_editTarget == EditTarget::Border; }
    // Unico scrittore del bersaglio e delle sue viste (radio a segnali
    // bloccati, slider dello spessore, vista 2D). NON fa la transizione del
    // dock (editor, checkbox, colori, comandi spenti): quella la fa il gestore
    // del clic, o chi chiama.
    void setEditTarget(EditTarget target);

    SceneState m_scene;
    // LA SCENA DAL FILE: cio' che il load deve lasciare in m_scene (punto 5,
    // tappa 5.3b). Non tocca la scena; legge solo il preset e, per texture e
    // suono, il disco (immagini mancanti, brani da ritrovare nella libreria).
    // Il test di andata e ritorno la confronta, preset per preset, con la
    // scena che il load produce davvero (PresetRoundTrip::sceneDiff).
    SceneState sceneFromItem(const LibraryItem &d, bool isRecord);
    // Due parti di sceneFromItem che il load usa anche da sole. Le SCELTE:
    // modalita', sotto-tab (Cross Section solo con l'equazione 4D e senza
    // script: la SDF di uno script e' un campo 3D), Shell/Solid e resa (dal
    // renderMode composito: >= 10 = Shell, solo nei preset impliciti). I MOTI
    // di un record: viste e velocita' dei path (0 = chiave assente: il
    // default), ultimo moto camera ("none" = fermo: nessuno).
    static void choicesFromItem(const LibraryItem &d, SceneState *s);
    static void motionFromItem(const LibraryItem &d, SceneState *s);
    // La superficie -- o la fascia selezionata, in ambito Mesh -- e' in
    // wireframe: la texture non si disegna. Vedi l'implementazione.
    bool textureTargetInWireframe() const;
    // Alpha globale del preset: slider E motore, anche a valore invariato.
    void applyPresetAlpha(float alpha);
    // defU/V/W e explicitU/V/W dal preset, per entrambi i rami di applyCommonData.
    // Limiti X/Y/Z (condivisi fra i due sotto-tab impliciti) riportati al default
    // "nessun taglio". Chiamata dal cambio di sotto-tab.
    void resetImplicitSharedFields();
    // Cambio di sotto-tab implicito (3D <-> Cross Section). Chiamata sia dal
    // cambio vero (currentChanged) sia dal riclic sulla linguetta gia' attiva,
    // che currentChanged non emette: stesso ruolo di applyModeTabReset.
    void applyImplicitSubTabReset(int subIndex);
    // Cambio di sotto-tab parametrico (Surface <-> Tubes), e riclic sulla
    // linguetta attiva: reset della scena, che disegna la superficie di
    // default della linguetta (il toro, o il trifoglio di defaultTubeTexts).
    void applyParametricSubTabReset();
    // Il tubo di default: un nodo trifoglio, spessore 0.12.
    static TubeTexts defaultTubeTexts();

    // --- Parsing, Strings & Scripts ---
    float parseMath(const QString &text, bool *ok = nullptr);
    float parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S, bool* ok = nullptr);
    struct CascadeConstants { float a, b, c, d, e, f, s; };
    // PASSI DI runScene (le guardie e il prologo Stop/Start restano a lei).
    // Validazioni comuni: campi X/Y/Z/P e costanti; false = Run fermato.
    bool validateRunInputs(RunOrigin origin);
    // Ramo TUBES del Run: curva asse, limiti u, direzione di riferimento,
    // motore (GLWidget::setTubeCurve) e spessore.
    void runSceneTube(RunOrigin origin, bool runDockOnly, const CascadeConstants &kc);
    // Il tubo di m_scene.tube nel motore (curva, riferimento, spessore,
    // dominio), senza popup: la validazione la fa chi chiama. false = la curva
    // non compila (errore in getShaderError). Usata dal Run e dal reset.
    bool applyTubeToEngine(float uMin, float uMax, const CascadeConstants &kc);
    // Almeno un campo della curva (i vuoti valgono 0), curva che cita u,
    // limiti u compilati e coerenti, spessore scritto: gate del tasto Run su
    // Tubes.
    bool hasCompleteTubeInput();
    // La curva di m_scene.tube cita u: senza, non c'e' curva (i limiti u si
    // spengono, come in Surface).
    bool tubeCurveUsesU() const;
    // Campiona la curva delle equazioni sulla CPU e ne ricava la copia del
    // tubo (tubeCopyFromCurve). false = la curva non si valuta (copia di
    // default: asse z, aperta).
    static bool sampleTubeCurve(const TubeTexts &tube, float uMin, float uMax,
                                const CascadeConstants &kc, int mesh, int meshes,
                                SurfaceEngine::TubeCopy *copy);
    // La copia di UN tubo dai punti della sua curva: direzione lontana da
    // tutte le tangenti (orienta la sezione) e chiusura in u. `curves` = un
    // vettore di punti (x, y, z, p) per istante di t, il primo a t = 0. Unica
    // sede della scelta: punti dalla CPU (equazioni) o dalla GPU (script).
    static SurfaceEngine::TubeCopy tubeCopyFromCurve(const std::vector<std::vector<QVector4D>> &curves);
    // SCRIPT DEL TUBO (dock Script sul sotto-tab Tubes): il corpo restituisce
    // vec4(x, y, z, p) in funzione di u e di `mesh`, le sezioni //MESH_BEGIN
    // dichiarano i tubi (u: e thickness:). Nel motore, senza popup: curva
    // campionata sulla GPU (TubeCurveSampler) per la copia di ogni tubo, parti,
    // dominio, spessore. false = non compila (errore in getShaderError).
    bool applyTubeScript(const QString &fullText, const CascadeConstants &kc);
    // Run del dock Script sul sotto-tab Tubes (onRunScriptClicked).
    void runTubeScriptFromDock(const QString &fullText);
    // Ripresa dello script del tubo da runScene (master Start, Invio su una
    // costante), come runSceneScript per le superfici.
    void runSceneTubeScript(RunOrigin origin, bool runDockOnly, const CascadeConstants &kc);
    // Coda comune dei Run del tubo: orologio, mesh e collasso.
    void finishTubeRun(RunOrigin origin, bool runDockOnly, const QString &curveText);
    // Sotto-tab Tubes con i campi della curva vuoti e uno script: la curva la
    // da' lo script (come una superficie da script, dove lo script prende il
    // posto delle equazioni). `applied` = lo script eseguito, non l'editor.
    bool tubeSceneFromScript(bool applied = true) const;
    bool tubeCurveFieldsEmpty() const;
    void runSceneScript(RunOrigin origin, bool runDockOnly);
    void runSceneImplicit(RunOrigin origin, bool runDockOnly);
    struct RunLimits { float uMin = 0, uMax = 0, vMin = 0, vMax = 0, wMin = 0, wMax = 0; };
    void runSceneParametric(RunOrigin origin, bool runDockOnly,
                            const CascadeConstants &kc, RunOutcomeGuard &outcome);
    // Variabili, composizione e limiti del ramo parametrico; *geodesic = la
    // scena e' un flusso geodetico (ha il suo ramo, runSceneGeodesic).
    bool checkParametricRunInputs(RunLimits *lim, bool *geodesic);
    void runSceneGeodesic(bool runDockOnly);
    // La sonda dei poli: false se l'equazione esplode sul dominio u/v.
    static bool equationStaysRenderable(const QString &eq, const CascadeConstants &kc,
                                        const RunLimits &lim);
    // Cio' che la cascata ha trovato di sbagliato nei campi. Lo mostra solo chi
    // parte da un gesto dell'utente (evaluateCascade): i percorsi programmatici
    // non aprono popup.
    struct CascadeIssues {
        QLineEdit *invalidEdit = nullptr;   // primo campo che non si valuta
        QString invalidName;
        QString negativeName;               // prima costante A..F negativa
    };
    // UNICO PARSER DELLA CASCATA: i campi lineA..lineS, ognuno valutato con le
    // costanti che lo precedono. A..F negative tornano all'ultimo valore valido
    // (S i negativi li accetta).
    CascadeConstants resolveCascadeConstants(bool restoreTextOnNegative,
                                             CascadeIssues *issues = nullptr);
    // Il gesto dell'utente su una costante (slider o campo): cascata, popup
    // degli errori, slider, motore e ricalcolo della mesh.
    void evaluateCascade();
    // COSTANTI NEL MOTORE: i valori dei campi (cascata risolta) diventano quelli
    // dell'UBO. E' la derivazione a senso unico campi -> motore.
    // Gli SLIDER sui valori risolti (passo 0.01, estremi che si allargano se
    // serve). Unica implementazione: la usano i gesti dell'utente
    // (evaluateCascade) e i percorsi programmatici.
    void syncConstantSliders(const CascadeConstants &k);
    // UNICO PUNTO che scrive le costanti nel motore. onlyIfChanged: non tocca
    // nulla a valori invariati (setEquationConstants segna la mesh da rifare).
    void setEngineConstants(const CascadeConstants &k, bool onlyIfChanged);
    // Campi -> slider + motore. always: scrive nel motore anche a valori
    // invariati (i Run, che la mesh la rifanno comunque).
    CascadeConstants pushConstantsToEngine(bool restoreTextOnNegative = true,
                                           bool always = false);
    // Ricalcola quali costanti sono in uso (updateConstantsUIState, che riporta
    // al valore neutro quelle cadute in disuso scrivendo a segnali BLOCCATI) e
    // porta i valori nel motore. Da chiamare dopo ogni cambio di codice che non
    // passa da un textChanged: era ripetuto a mano, e dove mancava lo slider
    // restava spento (sfondo dalla Library) o acceso a vuoto (texture spenta).
    void refreshConstants(bool restoreTextOnNegative = true);
    // Il testo delle equazioni dell'ULTIMO Run (m_eqApplied): cio' che
    // e' a schermo per equazioni e flusso geodetico, a differenza dei campi.
    QString activeEquationsText() const;
    // true da quando l'utente modifica un campo fino al prossimo load, reset,
    // scelta in Library o Run riuscito: in quell'intervallo cio' che e' scritto
    // non e' cio' che e' a schermo, e updateConstantsUIState conta anche
    // l'applicato prima di dichiarare una costante in disuso.
    bool m_constantsEditPending = false;
    // Limiti U/V/W: come parseMath ma con A..F/S registrate, cosi' "2*A" o
    // "pi/B" sono limiti validi. Risolve la cascata delle costanti a ogni
    // chiamata (i limiti stanno a valle: B puo' dipendere da A, e uMax da
    // entrambe). Usare SEMPRE questa e mai parseMath sui sei campi limite.
    float parseLimitField(const QString &text, bool *ok = nullptr);
    QString composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef);
    void parseAndApplyScriptParams(const QString &scriptCode, bool restartAudio = true,
                                   bool onlyFillEmptyLimits = false);
    struct ScriptDirectives {
        QList<QPair<QString, QString>> values;   // ("u_min", "-3.0"), ("a", "1.2")... in ordine
        QHash<QString, DiscreteRange> discrete;  // "A := int(1,6);"
        QHash<QString, float> mins;              // "F := min(0.3);"
        QString meshVisible;                     // "MESH_VISIBLE := E;"
    };
    static ScriptDirectives parseScriptDirectives(const QString &scriptCode);
    // const: e' pura analisi del testo, non tocca stato. Serve tale ai lettori
    // const che devono sapere se un codice e' animato (anyMeshTextureCodeAnimated).
    bool hasTimeVariable(const QString& code) const;
    QString extractAudioDirectives(const QString& fullText);
    // Il codice GRAFICO di uno slot: toglie cio' che extractAudioDirectives
    // estrae (//MUSIC:, blocchi //SOUND_BEGIN..//SOUND_END, anche coi
    // marcatori spaziati "// SOUND_BEGIN") e i marcatori rimasti orfani. Le
    // due regole devono combaciare: un blocco estratto ma non tolto finiva
    // compilato come texture.
    static QString stripAudioDirectives(QString code);

    // Immagini citate da un record e non piu' trovabili su disco. La scansione
    // gira PRIMA che applyMotionExample tocchi qualunque cosa, cosi' il popup
    // si apre sulla scena del record PRECEDENTE ancora intatta invece che su
    // una mezza scena nuova (vedi il commento in cima ad applyMotionExample).
    struct MissingImageScan {
        QStringList paths;              // percorsi mancanti, senza duplicati
        bool surfaceKeptScript = false; // la texture superficie perde la sola foto: lo script resta
        bool bgKeptScript = false;      // idem per lo sfondo
        bool surfaceMissing = false;
        bool bgMissing = false;
        bool meshKeptScript = false;    // una fascia perde la sola foto: il suo script resta
    };
    // Sola lettura di data e del suo JSON: non modifica nulla, quindi puo'
    // girare prima del caricamento vero.
    MissingImageScan scanRecordForMissingImages(const LibraryItem &data);
    // SCRIPT, TEXTURE, SFONDO E SUONO dal preset (parte di sceneFromItem): lo
    // script della superficie (slot e applicato) se la scena viene dallo
    // script; per un record i codici di texture e sfondo senza il suono (che va
    // nel suo slot), senza il tag //IMG: di un'immagine mancante (scan), lo
    // sfondo vuoto se spento, l'accensione e le ancore della Library. Una
    // superficie riparte senza texture, sfondo e suono.
    void textureTextsFromItem(const LibraryItem &d, bool isRecord,
                              const MissingImageScan &scan, SceneState *s);
    // sceneFromItem con la scansione delle immagini gia' fatta (il load di un
    // record la fa una volta sola, prima del popup).
    SceneState sceneFromItem(const LibraryItem &d, bool isRecord, const MissingImageScan &scan);
    // PASSI DEL LOAD DI UN RECORD, nell'ordine in cui applyMotionExample li
    // chiama (le guardie RAII del load restano a lei: valgono per tutti).
    void warnMissingRecordImages(const MissingImageScan &scan);
    void stopMotionForRecordLoad();
    void applyRecordMode(const LibraryItem &data, const SceneState &file);
    void applyRecordColors(const LibraryItem &data);
    void applyRecordCamera(const LibraryItem &data, const SceneState &file);
    void applyRecordSurfaceTexture(const LibraryItem &data, const MissingImageScan &scan);
    void applyRecordBackgroundTexture(const LibraryItem &data);
    void syncTextureControlsAfterRecordLoad(const LibraryItem &data);
    void applyRecordSpeedsAndAngles(const LibraryItem &data);
    void startRecordCameraMotion(const LibraryItem &data);
    bool runRecordGeometry(const LibraryItem &data);   // true: scena da script
    void restartRecordClocks(const LibraryItem &data);
    void startRecordSound();
    // PASSI DEL CLIC SU UNA VOCE DELLA LIBRARY (onExampleItemClicked).
    // false = l'utente ha annullato nel popup del lavoro non salvato.
    // Confermato: una texture su scena vuota ritrova la superficie di default,
    // un preset che sostituisce la scena diventa m_lastLoadedLibraryItem.
    bool confirmLibraryClick(QTreeWidget *src, QTreeWidgetItem *item);
    void clearOtherLibraryTrees(QTreeWidget *src);
    // La texture cliccata e' gia' quella in vigore sul bersaglio (codice,
    // rilievo, file del preset, editor non svuotato).
    bool libraryTextureIsActive(const LibraryItem &data);
    // Riclic su una texture gia' attiva: orologio, colori e inquadratura del preset.
    void restartActiveLibraryTexture(const LibraryItem &data);
    // LA SCENA DEL RESET (punto 5, tappa 5.4): cio' che resetScene(index,
    // loadDefaultSurface) lascia in m_scene, a partire dalla scena attuale --
    // al cambio di linguetta alcune parti sopravvivono (X/Y/Z/P verso il Ray
    // Marching, i campi RM verso il parametrico, lo slot della texture verso
    // il parametrico se non e' un riclic) -- e dalle memorie per modalita'
    // (steps, S). Non tocca nulla.
    SceneState defaultScene(int index, bool loadDefaultSurface, bool sameTabRestart) const;
    // PASSI DI resetScene, nell'ordine in cui li chiama.
    void stopSceneForReset();
    void resetScriptEngineForNewScene();
    void resetTextureAndSoundForNewScene();
    void resetAppearanceForNewScene();
    void resetRotationsForNewScene();
    void resetEngineToImplicit(bool loadDefaultSurface);
    void resetEngineToParametric(bool loadDefaultSurface);
    void finishSceneReset();
    void clearLibrarySelectionAfterReset();
    // ASSEGNA una scena intera a m_scene, con la vista dei campi: equazioni,
    // campi RM, costanti e domini, Shell/Solid e resa, slot di script,
    // texture, sfondo e suono, codice e colori dello sfondo, accensione e
    // ancore; per ultimi limiti e path (showLineFields, che fa il giudizio sulle costanti
    // a scena completa). NON tocca: modalita' e sotto-tab (li cambia chi
    // gestisce la linguetta), steps (setSteps, col motore), moti, applicato
    // della texture (commitSurfaceTextureCode). La usano la testa del load e
    // il reset: e' la sola strada per cui una scena intera entra in m_scene.
    void assignSceneTexts(const SceneState &s);
    // PASSI DI applyCommonData, nell'ordine in cui li chiama.
    void resetEngineBeforePresetLoad(const LibraryItem &d);
    void applyPresetRenderAndLimits(const LibraryItem &d, bool isShell);
    void preparePresetScriptEngine(const LibraryItem &d, bool isScript);
    void applyPresetScript(const LibraryItem &d, bool isShell);
    void applyPresetEquations(const LibraryItem &d, bool isShell);
    void finishPresetLoad(const LibraryItem &d, bool isScript);

    // (Scansione dell'albero texture e selezione della voce corrispondente:
    // LibraryTreeFocus::selectTexture.)

public:
    // "Sync Focused Texture" (menu contestuale dei record): riporta nella scena
    // il codice aggiornato delle texture di libreria da cui il record proviene --
    // superficie e/o sfondo, quelle rimaste indietro -- lasciando intatti colori,
    // costanti, zoom e pan: cio' che ricaricare la texture dal dock
    // sovrascriverebbe. Non salva: il record va risalvato.
    bool syncFocusedTextureFromLibrary();
    bool syncSurfaceTextureFrom(const LibraryItem *lib);
    bool syncBackgroundTextureFrom(const LibraryItem *lib);
    // SFONDO SENZA TEXTURE, in un colpo solo: codice, testo dello script,
    // percorso dell'immagine, ancora di libreria, messaggio e immagine nella
    // GPU (torna background.png). Regola unica: sfondo spento = niente texture.
    // Prima ogni punto di spegnimento azzerava un pezzo diverso, e cio' che
    // restava generava lo "sfondo-immagine perso nei record": percorso rimasto
    // -> Save con enabled false e l'//IMG: di un'immagine tolta; immagine
    // rimasta in GPU -> riaccendendo si vedeva quella ma il Save, senza
    // percorso, scriveva enabled true e codice VUOTO (al reload: default).
    // Non tocca l'accensione (setBackgroundTextureEnabled): e' del chiamante.
    void forgetBackgroundTexture();
    // Una fascia da sincronizzare: indice della parte e voce da cui viene.
    struct MeshTextureSync { int part; const LibraryItem *lib; };
    bool syncMeshTexturesFrom(const QVector<MeshTextureSync> &items);
    // Voce di libreria da sincronizzare, o nullptr se non c'e' nulla da fare
    // (texture non da libreria, voce sparita, codice gia' uguale): una per la
    // superficie, una per lo sfondo, e una lista per le fasce (ognuna con la
    // SUA ancora, MeshPart::textureLibName). Il menu si accende se almeno una
    // risponde, e il suo tooltip dice quali.
    const LibraryItem *focusedTextureLibraryItem() const;
    const LibraryItem *focusedBgTextureLibraryItem() const;
    QVector<MeshTextureSync> focusedMeshTextureLibraryItems() const;
    // File del record in scena: il menu confronta con quello cliccato.
    QString currentRecordPath() const { return m_currentRecordPath; }

    // SONDA DELLO STATO TEXTURE (vedi SE_TEX_PROBE in mainwindow.cpp). Stampa in
    // UNA riga i tre canali del codice, i due slot di testo, l'ambito, F e i
    // flag: e' pensata per essere chiamata negli stessi punti di una sequenza e
    // letta in colonna, dove il guasto si vede come DIVERGENZA fra due campi.
    // Inerte quando la macro e' a 0.
    void dumpTextureState(const char *tag) const;
private:

    // --- UI State & Graphics ---
    // Mostra il messaggio della SCENA (con quelli di texture e sfondo) per
    // 'seconds' secondi; testo vuoto e niente altro da mostrare = nasconde.
    void showSceneHint(const QString &text, float seconds);
    void hideSceneHint();
    // Porta le costanti dichiarate discrete ("A := int(min,max);") all'intero
    // piu' vicino nel loro range. Chiamata al RILASCIO dello slider e all'Enter
    // nel campo, mai durante il trascinamento (renderebbe lo slider a scatti
    // mentre lo si muove). Ritorna true se ha modificato qualcosa.
    bool applyDiscreteConstants();
    void updateLayoutForMode(int mode);
    void setupSpeedControl(QPushButton* btnPlus, QPushButton* btnMinus,
                           std::function<float()> getter, std::function<void(float)> setter);
    // Le sei etichette delle velocita' di rotazione, scritte dal MOTORE (unica
    // copia delle velocita'): nessuno le scrive a mano e i tasti +/- non le
    // rileggono piu'.
    void refreshRotationSpeedLabels();
    void updateProjectionButtonText();
    void updateScriptButtonText();
    void updateTextureUIState(bool isTextureOn, bool resetColorTargetToFirst = false);
    bool activeTextureUsesColors() const;
    // COLORI u_col1/u_col2 (slot 1 o 2) della texture di superficie che i
    // picker mostrano e gli slider editano. Non sono stato ma una VISTA del
    // motore, l'unica fonte: i colori propri della fascia selezionata se ne ha,
    // altrimenti i due slot globali -- quelli che la fascia eredita, e quelli
    // della superficie in ambito All. Prima erano due membri (m_texColor1/2)
    // riallineati a mano a ogni cambio di fascia: su una fascia senza colori
    // propri restavano quelli della fascia guardata prima.
    QColor surfaceTexColor(int slot) const;
    // SCRIPT della texture di superficie / di sfondo (parametrico): cio' che
    // l'utente ha scritto, eseguito o no -- l'INTENZIONE, quella che il Save
    // scrive. E' il suo slot (vedi "DOCK SCRIPT: STATO E VISTA").
    // Solo lettura: la copia APPLICATA (m_scene.surfaceTextureCode / m_scene.bgTextureCode)
    // la scrive solo chi la manda al motore. Prima il Save ci travasava
    // l'editor, e dopo un salvataggio uno script mai eseguito risultava
    // applicato mentre il motore disegnava ancora il vecchio.
    // SCRIPT DI SUPERFICIE: via lo scritto (m_scene.surfaceScriptText) e l'applicato
    // (m_scene.surfaceScriptApplied), insieme. Svuotando solo il primo, caricando una
    // scena a equazioni dopo una da script il Save aveva ancora in mano lo
    // script della scena di prima.
    void clearSurfaceScript();
    QString surfaceTextureScript() const { return m_scene.surfaceTextureScriptText; }
    QString backgroundTextureScript() const { return m_scene.bgTextureScriptText; }
    // IMMAGINE della texture di superficie: il file caricato nel motore
    // (GLWidget::surfaceImagePath), vuoto se non c'e'. E' l'unica copia: prima
    // c'erano anche m_isImageMode e m_currentTexturePath, riallineati a mano in
    // una trentina di punti, e dove uno dei due restava indietro il Save
    // perdeva l'immagine (riclic su una texture di sfondo) o la GPU se la
    // teneva (tasto NEW). Chi carica o toglie l'immagine passa dal motore
    // (loadTextureFromFile, clearTexture, generateTexture).
    QString surfaceImagePath() const;
    bool surfaceHasImage() const { return !surfaceImagePath().isEmpty(); }
    // Gemello per lo SFONDO: l'immagine dell'utente caricata nel motore, vuoto
    // con la default (background.png). Dice QUALE immagine campiona iChannel0
    // sotto uno script, ed e' da qui che il Save ricostruisce il tag //IMG:.
    // Prima era una copia in MainWindow (m_currentBgTexturePath), azzerata in
    // punti che lasciavano l'immagine in GPU.
    QString backgroundImagePath() const;
    // La texture parametrica di superficie APPLICATA e' codice custom (e non la
    // scacchiera di default o la sola immagine)? Si deriva dal codice applicato:
    // era un flag (m_isCustomMode) scritto in una quindicina di punti, e il Run
    // dello script di una FASCIA lo scriveva col codice della fascia -- tornando
    // ad All i picker Colore della scacchiera di default restavano spenti.
    bool surfaceTextureIsCustom() const { return TextureCode::hasLogic(m_scene.surfaceTextureCode); }
    // PROVA E APPLICA il codice della texture PARAMETRICA di superficie: il
    // motore lo compila (un codice senza logica -- vuoto, o il solo tag //IMG:
    // -- lascia lo shader standard) e, solo se regge, diventa la copia applicata
    // m_scene.surfaceTextureCode. "" toglie la texture. Sede unica della coppia
    // "motore, poi copia applicata" per i GESTI (Run, Sync, Library, checkbox,
    // reset): era ripetuta a mano in ciascuno. I due load (applySurfaceExample,
    // applyMotionExample) restano a parte: li' la copia serve PRIMA che il
    // motore possa compilare (giudizio delle costanti, equazioni non ancora
    // applicate) e si riallinea in coda al load.
    bool commitSurfaceTextureCode(const QString &code);
    // LA SCELTA DI UNA TEXTURE DALLA LIBRARY (handleTextureSelection), a passi:
    // una texture incompatibile si rifiuta subito (refuseIncompatibleTexture);
    // sullo sfondo o sul bordo e basta; altrimenti la texture parametrica o Ray
    // Marching (false = finito li': messa su una fascia, o non compila), gli
    // orologi della texture e la coda (tasti, colori, ancora, messaggio,
    // costanti).
    void applyLibraryTextureToBackground(const LibraryItem &data);
    void applyLibraryTextureToBorder(const LibraryItem &data);
    bool applyLibraryTextureParametric(const LibraryItem &data, const QString &imgSrc,
                                       bool texGoesToMesh);
    bool applyLibraryTextureRM(const LibraryItem &data, const QString &imgSrc);
    void restartLibraryTextureClocks();
    void finishLibraryTexturePick(const LibraryItem &data);
    // SUONO della scena nella forma che il player e il Save capiscono: una riga
    // //MUSIC: o un blocco //SOUND_BEGIN..//SOUND_END. Il dock Sound accetta
    // anche GLSL nudo (un mainSound scritto senza marcatori): wrapSoundCode lo
    // avvolge. Prima lo avvolgeva solo il Run Sound, componendolo DENTRO il
    // codice della texture (m_scene.surfaceTextureCode / m_scene.bgTextureCode): l'audio
    // viveva in due posti, il Run Sound "applicava" in memoria uno script
    // texture mai eseguito, e il Save -- che legge m_scene.soundScriptText -- scriveva
    // il GLSL nudo in mezzo alla texture. L'audio vive solo in m_scene.soundScriptText.
    static QString wrapSoundCode(const QString &sound);
    QString soundCode() const { return wrapSoundCode(m_scene.soundScriptText); }
    // Tutto il codice in cui cercare l'audio da suonare (player, avvio
    // automatico, video): il suono per primo, poi gli script che possono
    // ancora portarne uno (uno script di superficie scritto a mano, una
    // texture utente salvata col suono dentro). Vuoto -> il testo dell'editor.
    // UNICO PUNTO: erano cinque concatenazioni, in ordini diversi.
    QString sceneAudioSource() const;
    // Granulari: la texture attiva referenzia u_col1 / u_col2 (token = "u_col1"/"u_col2").
    // Servono per abilitare i due picker INDIPENDENTEMENTE: una texture che usa solo
    // u_col1 (es. "Xor") non deve lasciare attivo il picker di col2, che non avrebbe
    // effetto. activeTextureUsesColors() resta la OR delle due (true se almeno una).
    bool activeTextureUsesColorToken(const QString &token) const;
    // true se lo script campiona iChannel0 (Animated Images) MA nessuna immagine
    // e' caricata: in quel caso mostra la scacchiera procedurale di fallback,
    // che usa u_col1/u_col2, quindi i picker colore vanno accesi.
    bool samplesImageWithoutOne(const QString &code) const;
    // true se la texture attiva (superficie/sfondo, parametrico o ray marching)
    // ha del codice salvabile: rispecchia la selezione dei campi di
    // PresetSerializer::saveTexture(). Pilota l'abilitazione del tasto Save.
    bool hasSavableTexture() const;
    void updateFlatPreviewButton();
    // Il testo del modulo equazioni in cui cercare 't': equazioni (o
    // l'equazione implicita del sotto-tab), script di superficie applicato e,
    // col pannello geodetico, i campi del flusso.
    QString equationModuleCode() const;
    // MODULI DEL MASTER, uno per uno: il master START avrebbe qualcosa da
    // accendere (available) e lo e' gia' (running)? Le condizioni sono quelle
    // con cui START accende ciascuno. Una sede sola per il testo del tasto --
    // STOP solo quando TUTTI i moduli accendibili sono accesi: accende tutto,
    // spegne tutto, i singoli moduli si comandano coi loro tasti -- e per chi
    // deve sapere se qualcosa si muove (isAnythingMoving).
    struct MasterActivity {
        bool eqAvailable = false, eqRunning = false;          // geometria con 't', flusso geodetico
        // Texture di superficie e delle fasce. texRunning = girano TUTTI i loro
        // orologi (il master dice STOP solo cosi'); texAnyRunning = ne gira
        // almeno uno (qualcosa si muove).
        bool texAvailable = false, texRunning = false, texAnyRunning = false;
        bool bgAvailable = false, bgRunning = false;          // texture di sfondo
        // Texture del BORDO: un modulo a se', col suo orologio. Sommata alla
        // texture di superficie la faceva risultare "in moto" anche ferma, e
        // il master diceva STOP con un modulo spento.
        bool borderAvailable = false, borderRunning = false;
        bool cameraAvailable = false, cameraRunning = false;  // rotazioni e path (uno alla volta)
        bool audioAvailable = false, audioRunning = false;
        bool anyRunning() const {
            return eqRunning || texAnyRunning || bgRunning || borderRunning || cameraRunning || audioRunning;
        }
        bool allRunning() const {
            return (!eqAvailable || eqRunning) && (!texAvailable || texRunning)
                && (!bgAvailable || bgRunning) && (!borderAvailable || borderRunning)
                && (!cameraAvailable || cameraRunning) && (!audioAvailable || audioRunning);
        }
    };
    MasterActivity masterActivity() const;
    // Qualcosa si muove o suona. Era il significato del "STOP" del master prima
    // che il tasto seguisse la regola di tutti i moduli: chi lo leggeva per
    // sapere se fermare e poi ripristinare le animazioni chiede a questa.
    bool isAnythingMoving() const { return masterActivity().anyRunning(); }
    void updateMasterButtonState();
    void applyAnimationState(bool animated, bool dockOnly = false);
    bool hasAnyRotationSpeed() const;
    void generateTexture();
    void applyDefaultCheckerShader();
    // Codice texture di superficie GLOBALE + quello delle mesh con texture
    // propria accesa: e' cio' su cui va deciso se il clock texture deve girare.
    QString allSurfaceTextureCode() const;
    // Codici delle texture delle FASCE, per decidere quali costanti sono in uso:
    // tutte le parti con texture propria (accese o no, in qualunque ambito) piu'
    // quelle del record ancora in m_pendingMeshParts. Vedi la definizione.
    QStringList meshTextureCodesForConstants() const;
    // Il codice GLSL usa la costante 'letter'? Maiuscola e non dichiarata come
    // variabile locale. Unica sede della regola.
    static bool glslUsesConstant(const QString &glsl, const QString &letter);
    // FOV UNICO (slider nel dock renderer, sotto Light). Unico punto che imposta
    // il campo visivo: allinea slider + etichetta e applica SEMPRE il valore
    // alla proiezione, senza dipendere da quale moto stia guidando la camera.
    // Sostituisce applyPathFov3D/applyPathFov4D, che agivano solo con la
    // rispettiva path in corsa (da fermo il FOV era inerte e non correggibile,
    // e i due slider si contendevano lo stesso m_cameraFov).
    void applyCameraFov(float deg);

    // ASPETTO PER-MESH (spinbox nel dock renderer). updateMeshSelectorRange va
    // chiamata dopo ogni rigenerazione della mesh: con una superficie a mesh
    // singola il massimo resta 0 e lo spinbox mostra solo "All", quindi la
    // funzionalita' resta invisibile dove non serve.
    void updateMeshSelectorRange();
    void syncAppearanceControlsToActiveMesh();
    // BASE / PHONG / WIREFRAME. Lo stato e' m_scene.renderMode (globale: quello
    // che le mesh senza modalita' propria ereditano e che il preset salva) e,
    // per una fascia, la sua MeshPart. I radio ne sono la VISTA: chi vuole
    // sapere la modalita' chiede a shownRenderMode(), non ai radio.
    // La modalita' che i radio mostrano: quella efficace della fascia
    // selezionata (parametrico, multi-mesh), altrimenti la globale.
    int shownRenderMode() const;
    // L'unica scrittura dei radio: su shownRenderMode(), a segnali bloccati
    // (il loro toggled e' il clic dell'utente).
    void refreshRenderRadios();
    // Forma dello sfondo (GLWidget::BgSkyMode): porta radio E motore sullo stato
    // indicato, a segnali bloccati. Unica via per load e reset -- il clic
    // dell'utente passa dal toggled dei radio.
    void applyBackgroundSkyMode(int mode);
    // I quattro radio della forma, nell'ordine di GLWidget::BgSkyMode.
    QList<QRadioButton*> bgSkyRadios() const;
    // Gruppo Background Controls acceso solo col bersaglio Background, e tooltip
    // che dicono perche' quando e' spento. Chiamata da updateRenderState e da
    // onColorTargetChanged (vedi la definizione per il perche' di entrambe).
    void updateBackgroundControlsGate();
    // Il contrario: comandi della SUPERFICIE spenti col bersaglio Background.
    // Ultima chiamata di updateRenderState (vedi la definizione).
    void updateSurfaceControlsGate();
    // Il bordo e' dei lati del dominio parametrico: in Ray Marching il radio
    // Border si spegne, e col bersaglio Border si torna su Surface.
    void updateBorderControlsGate();
    // COMANDO: l'utente ha cliccato un radio Base/Phong/Wireframe. E' l'unico
    // punto che puo' scrivere una modalita' PROPRIA sulla mesh selezionata; con
    // "All" agisce sul globale come da sempre. Tenuto separato dal DISPLAY (i
    // radio mossi da syncAppearanceControlsToActiveMesh) perche' e' proprio la
    // confusione fra i due ruoli che rendeva instabile il wireframe per-mesh.
    void onUserRenderModeChosen();
    // Vera mentre syncAppearanceControlsToActiveMesh sta muovendo i controlli:
    // impedisce che i segnali di quei widget la facciano rientrare.
    bool m_syncingMeshControls = false;
    // Aspetto per-mesh letto da un preset: al momento di applyCommonData le parti
    // non esistono ancora, quindi resta in sospeso qui e viene riversato dopo la
    // rigenerazione della griglia.
    std::vector<MeshPart> m_pendingMeshParts;
    // Ambito All/Mesh letto dal preset, applicato quando le parti esistono.
    // m_meshScopePending lo rende un'azione UNA-TANTUM: il punto di applicazione
    // e' agganciato a meshPartsChanged, che scatta a ogni rigenerazione della
    // griglia, e senza il consumo l'ambito veniva riportato allo stato del
    // preset a ogni cambio di costante.
    bool m_pendingMeshScopeAll = false;
    bool m_meshScopePending = false;
    // Alzato da applyPendingMeshScope quando ha appena impostato la parte attiva
    // del preset, consumato da updateMeshSelectorRange nello stesso giro.
    // Serve perche' quei due girano in sequenza sullo stesso meshPartsChanged:
    // il primo porta la parte attiva a 0, e il secondo trova percio' "moved"
    // FALSO (spinbox 1 -> indice 0, gia' uguale) e salta il sync degli slider,
    // che restano sul colore GLOBALE mentre la mesh 1 ne ha uno proprio.
    bool m_meshScopeJustApplied = false;
    void applyPendingMeshAppearance();
    // Ripristina l'ambito All/Mesh salvato col preset. Chiamata da
    // applyPendingMeshAppearance PRIMA dell'early-return, cosi' vale anche per i
    // preset che non portano aspetto per-mesh.
    void applyPendingMeshScope();
    // Il selettore All/Mesh e' utilizzabile solo con piu' fasce e fuori dal
    // Background; updateMeshScopeEnabled applica il gating e, se non lo e',
    // riporta l'ambito su "All".
    bool meshScopeUsable() const;
    // CAMPO MESHES (riga del pannello Multi Mesh): quante copie della
    // superficie o della curva. Il testo sta in m_scene.meshCount (un numero o
    // un'espressione con le costanti, come i limiti); lo slider va da 1 a
    // kMeshCountSliderMax e si allarga coi valori scritti, fino a
    // kMeshCountMax. Le equazioni e gli script distinguono le copie con `mesh`
    // e ne leggono il numero con `meshes`.
    static constexpr int kMeshCountMax = 64;
    static constexpr int kMeshCountSliderMax = 24;
    void setMeshCountText(const QString &text);
    // Il numero valutato, 1..kMeshCountMax (testo vuoto = 1).
    int meshCountValue(bool *ok = nullptr);
    bool meshCountUsesConstants() const;
    // LA REGOLA DELLE PARTI, unica sede: senza sezioni //MESH_BEGIN, N copie
    // sul dominio corrente (followsGlobalDomain); con UNA sezione, N copie di
    // quella; con piu' sezioni comandano le sezioni e il campo e' spento.
    // `source` e' il testo appena applicato (equazioni, script o curva del
    // tubo): se non usa `mesh` le copie sarebbero identiche e sovrapposte,
    // quindi resta una mesh sola e il campo si spegne.
    std::vector<MeshPart> countMeshParts(const std::vector<MeshPart> &sections,
                                         const QString &source);
    bool meshCountUsable() const;
    // Il numero di mesh che il campo e lo slider MOSTRANO: le sezioni se sono
    // piu' d'una, 1 se la sorgente non usa `mesh`, altrimenti il campo.
    int shownMeshCount();
    void pushMeshCountSlider();
    void commitMeshCountOnEnter();
    // Il numero nuovo a schermo: riapplica la scena com'e' (commit di servizio).
    void applyMeshCount();
    // Un'espressione con le costanti cambia valore con loro (evaluateCascade).
    void refreshMeshCountFromConstants();
    void updateMeshCountControls();
    int m_lastAppliedMeshCount = 1;   // copie dell'ultima applicazione
    int m_lastSectionCount = 0;       // sezioni //MESH_BEGIN della scena
    bool m_lastSourceUsesMesh = false; // la sorgente applicata usa `mesh`
    // true se almeno una fascia ha una texture propria accesa: il modulo
    // "texture di superficie" e' attivo anche senza texture globale.
    bool anyMeshTextureActive() const;
    // Almeno una fascia con texture accesa il cui codice usa il tempo: e' cio'
    // che il master button deve considerare "in movimento" per le per-mesh.
    bool anyMeshTextureCodeAnimated() const;
    // Riaccende l'orologio di ogni fascia con texture propria ANIMATA (e spegne
    // le altre): gesto simmetrico dello Stop in ambito "All", senza il quale le
    // texture per-mesh non ripartono piu'.
    void restartAnimatedMeshTextures();
    void updateMeshScopeEnabled();
    void toggleProjection();
    bool applyBackgroundTextureIfNeeded();

    // --- Geometry & Geodesic Flow ---
    // L'applicato = lo scritto di adesso (m_eqApplied = m_scene.eq).
    void snapshotActiveEquations();
    void commitUiFieldsDuringMotion();
    // Ritornano true se la modifica e' stata APPLICATA, false se rifiutata
    // (limite non valido, min>=max, snapshot assente, singolarita').
    // L'aggiornamento avviene SOLO all'Invio o al Run: cambiare campo senza
    // Invio non applica nulla, per nessun tipo di campo. Vedi il commento in
    // mainwindow.cpp sopra commitLimitFieldOnEnter.
    bool commitFieldsOnEnter();
    // Quante volte commitFieldsOnEnter ha girato. I filtri tastiera lo leggono
    // PRIMA di w->clearFocus() e dopo: se e' cambiato, il clearFocus() ha gia'
    // fatto scattare editingFinished -- il cui handler (costanti) chiama gia'
    // commitFieldsOnEnter -- e il filtro NON deve chiamarlo una seconda volta.
    // Senza questo onStartClicked girava due volte per un solo Invio, e OGNI
    // validatore al suo interno (sintassi, parentesi, identificatori, uso di W,
    // campi incompleti...) mostrava il suo popup DUE volte di fila.
    // Un contatore e non un flag a tempo: i popup sono exec() con un loop di
    // eventi annidato, che consumerebbe un QTimer::singleShot(0) mentre il box
    // e' ancora aperto: al ritorno la guardia sarebbe gia' scaduta.
    quint64 m_commitOnEnterCount = 0;
public:
    quint64 commitOnEnterCount() const { return m_commitOnEnterCount; }
private:
    // Invio su un campo path (chiamata dai filtri tastiera desktop/mobile,
    // che CONSUMANO il Return: returnPressed non arriva mai ai QLineEdit):
    // a moto attivo ricompila le equazioni del path del campo al volo.
    void commitPathFieldOnEnter(const QString& fieldName);
    // Stessa provenienza (filtri tastiera): l'Invio su un limite U/V/W applica
    // subito il nuovo dominio, senza toccare le equazioni in sospeso.
    bool commitLimitFieldOnEnter(const QString& fieldName);
    // GEMELLO PER-MESH del precedente: i quattro campi u/v del pannello Multi
    // Mesh scrivono il dominio della SOLA parte selezionata.
    // Due differenze deliberate rispetto ai limiti globali:
    //  - si applica SEMPRE subito (niente attesa del Run). I limiti globali
    //    aspettano perche' fanno parte della definizione della superficie
    //    INSIEME a equazioni e vincoli, e applicarli da soli mostrerebbe la
    //    superficie vecchia tagliata dai limiti nuovi. Qui non c'e' quella
    //    coppia: le equazioni sono gia' applicate e si cambia il dominio di una
    //    sola parte, esattamente come il colore o la densita' wireframe.
    //  - non tocca m_parametricApplied: il Run dello script RISCRIVE questi
    //    domini (lo script e' l'autorita'), quindi accendere il Run come
    //    "modifica da applicare" indicherebbe il contrario di cio' che accade.
    bool commitMeshLimitFieldOnEnter(const QString& fieldName);
    // Display dei quattro campi u/v per-mesh sul dominio della parte attiva.
    void syncMeshLimitFields();
    // useAppliedLimits: legge il dominio GIA' APPLICATO dall'engine invece dei
    // campi UI. Serve al tick del moto (advanceGeodesicFlowBy), che gira di
    // continuo su un record animato: rileggendo il testo dei campi raccoglieva
    // le cifre a meta' digitazione e il limite si applicava senza attendere
    // l'Invio. I percorsi interattivi (Run, Invio sui limiti) leggono i campi.
    // useAppliedEquations: usa le equazioni GIA' APPLICATE (snapshot m_eqApplied)
    // invece del testo dei campi, anche a moto FERMO. Serve all'Invio sui
    // limiti: quello deve applicare SOLO il dominio, mai equazioni modificate e
    // non ancora confermate col Run. A moto attivo il disaccoppiamento c'e'
    // gia'; questo flag lo estende al caso fermo.
    bool updateGeodesicMesh(bool useAppliedLimits = false, bool useAppliedEquations = false);
    // useAppliedEquations: nel ramo geodetico usa le equazioni X/Y/Z/P
    // dell'ultimo Run (snapshot m_eqApplied) invece del testo dei campi. Lo
    // passano i chiamanti che NON sono un Run -- debounce di Steps e costanti,
    // navigazione, ripristino path -- perche' un ricalcolo innescato da loro
    // non deve applicare equazioni ancora in corso di scrittura. I Run veri
    // (runMetricScript, load preset) lo lasciano a false.
    void checkAndTriggerMeshUpdate(bool useAppliedEquations = false);
    void stopGeodesicAnimation();
    bool isGeodesicMotionActive() const;
    bool hasGeodesicText() const;
    bool geodesicFieldsAreFinite(const QStringList& exprs, float uMin, float uMax, float vMin, float vMax, float A, float B, float C, float D, float E, float F, float S);



protected:
    // Chiusura della finestra: ultimo percorso distruttivo che non chiedeva
    // niente. Passa dalla stessa confirmDiscardUnsaved(ScopeScene) di tutti
    // gli altri -- se l'utente annulla, l'evento viene ignorato e l'app resta
    // aperta. Non duplicare qui la logica dei flag: la decisione sta tutta in
    // hasUnsavedWork().
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

#endif // MAINWINDOW_H
