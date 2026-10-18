#ifndef LIBRARYMANAGER_H
#define LIBRARYMANAGER_H

#include "surfaceengine.h"
#include <QColor>
#include <QString>
#include <QList>
#include <QHash>
#include <QPair>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>

// ==========================================================
// ENUMS
// ==========================================================
enum class LibraryType {
    Surface,
    Texture,
    Motion,
    Sound
};

// ==========================================================
// DATA STRUCTURES
// ==========================================================
struct LibraryItem {
    // ==========================================================
    // CORE PROPERTIES
    // ==========================================================
    QString name;
    QString filePath;
    LibraryType type;

    // ==========================================================
    // EQUATIONS & CONSTRAINTS
    // ==========================================================
    QString x, y, z, w;
    QString explicitU, explicitV, explicitW;
    QString defU, defV, defW;

    // ==========================================================
    // GEODESIC FLOW
    // ==========================================================
    QString geoU0, geoV0, geoW0;
    QString geoDU, geoDV, geoDW;
    QString geoConform;

    // Mappa di visualizzazione (embedding) di uno script metrico: x/y/z/p custom
    // (non carta identità). hasMetricMap=false => identità, niente override.
    bool hasMetricMap = false;
    QString metricMapX, metricMapY, metricMapZ, metricMapP;

    // ==========================================================
    // TUBO (sotto-tab Tubes del parametrico)
    // ==========================================================
    // La scena e' un tubo attorno alla curva X/Y/Z/P(u) (P vuoto = curva 3D),
    // con lo spessore del campo Thickness. Chiave JSON "tube", presente solo
    // nelle scene tubo: assente = sotto-tab Surface, cioe' ogni preset di prima.
    bool isTube = false;
    QString tubeX, tubeY, tubeZ, tubeP, tubeThickness;
    // Campo Meshes (pannello Multi Mesh): numero di copie, numero o
    // espressione con le costanti. Chiave "meshCount" solo se diverso da 1.
    QString meshCount = QStringLiteral("1");

    // ==========================================================
    // IMPLICIT EQUATIONS
    // ==========================================================
    bool isImplicitMode = false;
    QString implicitEq;
    // Sotto-tab implicito attivo al salvataggio: false = "3D" (equazione a 3
    // variabili in implicitEq), true = "Cross Section" (equazione a 4 variabili
    // x,y,z,p in crossSectionEq). I due sotto-tab hanno editor e stato separati
    // (vedi CLAUDE.md): senza questi due campi il salvataggio scriveva SEMPRE
    // l'editor 3D, e una superficie Cross Section si ricaricava come la sfera di
    // default. Assenti nei record piu' vecchi -> false, cioe' il ramo 3D di
    // sempre: la compatibilita' e' preservata.
    // Spessore del guscio (modalita' Shell). Assente nei preset precedenti allo
    // slider -> 0.005, il valore storico della sfera unitaria.
    float shellThickness = 0.005f;
    // Luce di riempimento dall'osservatore (dock Renderer), 0 = spenta.
    // Assente nei preset precedenti allo slider -> 0, cioe' l'illuminazione
    // storica: quei preset si riaprono esattamente come sono stati salvati.
    float fillLight = 0.0f;
    // BORDO della superficie (parametrico): raggio dei tubi sui lati del
    // dominio, 0 = nessun bordo, e il loro colore. Chiave "border" assente ->
    // nessun bordo: i preset precedenti si riaprono come sono stati salvati.
    float borderThickness = 0.0f;
    QColor borderColor = QColor::fromRgbF(1.0f, 1.0f, 1.0f);
    // Modalita' propria del bordo (0 Base, 1 Phong, 2 Wireframe) e densita'
    // del suo wireframe: anello ogni N punti lungo il lato, linea ogni N lati
    // attorno al tubo. Gli stessi default di GLWidget::BorderStyle.
    int borderMode = 0;
    int borderWireAlong = 8;
    int borderWireAround = 2;
    // Texture del bordo (dalla Library): codice o tag //IMG:, accesa, colori,
    // inquadratura e nome della voce. Gli stessi default di
    // GLWidget::BorderStyle.
    QString borderTexCode;
    bool borderTexEnabled = false;
    QColor borderTexCol1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    QColor borderTexCol2 = QColor(Qt::black);
    float borderTexZoom = 1.0f;
    float borderTexPanX = 0.0f, borderTexPanY = 0.0f;
    float borderTexRotation = 0.0f;
    QString borderTexLibName;
    QString borderTexHintText;
    float borderTexHintSeconds = 6.0f;
    bool usesCrossSection = false;
    QString crossSectionEq;
    // MARCHER del ray marching: false = sphere tracing storico ("Fast"), true =
    // ibrido ("Precise", elimina le saldature sulle equazioni di grado alto).
    // Assente nei record precedenti ai radio -> il default NON e' una costante:
    // lo decide il sotto-tab salvato (vedi la lettura in librarymanager.cpp).
    bool hybridMarcher = false;

    float xMin = -1.0f, xMax = 1.0f;
    float yMin = -1.0f, yMax = 1.0f;
    float zMin = -1.0f, zMax = 1.0f;

    // ==========================================================
    // MATHEMATICAL CONSTANTS & LIMITS
    // ==========================================================
    int steps = 100;
    float uMin = 0.0f, uMax = 0.0f;
    float vMin = 0.0f, vMax = 0.0f;
    float wMin = 0.0f, wMax = 0.0f;
    // Forma TESTUALE dei limiti, quando l'utente ha scritto un'espressione
    // ("2*A", "pi/B"). Vuota sui record vecchi e su quelli con limiti numerici:
    // in tal caso vale il float qui sopra. Le chiavi numeriche uMin/uMax/...
    // restano sempre scritte, cosi' i lettori precedenti continuano a leggere
    // un preset nuovo senza vedere formule che non saprebbero valutare.
    QString uMinExpr, uMaxExpr;
    QString vMinExpr, vMaxExpr;
    QString wMinExpr, wMaxExpr;
    // Idem per il taglio x/y/z del Ray Marching (xMin..zMax, sopra).
    QString xMinExpr, xMaxExpr;
    QString yMinExpr, yMaxExpr;
    QString zMinExpr, zMaxExpr;
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f, e = 0.0f, f = 0.0f, s = 0.0f;

    // ==========================================================
    // CAMERA & 3D/4D TRANSFORMATIONS
    // ==========================================================
    bool hasCamera3D = false;
    float camX = 0.0f, camY = 0.0f, camZ = 4.0f;
    float camYaw = 0.0f, camPitch = 0.0f, camRoll = 0.0f;
    float rotW = 1.0f, rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;

    // ==========================================================
    // MOTION, ANGLES & PATHS
    // ==========================================================
    float speedNut = 0.0f, speedPrec = 0.0f, speedSpin = 0.0f;
    float speedOmega = 0.0f, speedPhi = 0.0f, speedPsi = 0.0f;

    // Angoli 4D della scena (chiave "angles"). restoreAngles = il preset li
    // porta (i preset molto vecchi li avevano in radice, o per niente).
    bool  restoreAngles = false;
    float startOmega = 0.0f, startPhi = 0.0f, startPsi = 0.0f;
    // Traslazione del piano di sezione lungo p (solo Cross Section): insieme a
    // omega/phi/psi definisce QUALE sezione dell'ipersuperficie 4D si vede.
    // Assente nei record piu' vecchi -> 0, la sezione centrale di sempre.
    float crossSectionP = 0.0f;

    QString path3D_x, path3D_y, path3D_z, path3D_roll;
    QString path4D_x, path4D_y, path4D_z, path4D_w;
    QString path4D_alpha, path4D_beta, path4D_gamma;

    // Messaggio in sovrimpressione mostrato al caricamento del record (chiavi
    // JSON "hintText"/"hintSeconds"). Vuoto = nessun messaggio.
    QString hintText;
    float   hintSeconds = 6.0f;

    // Secondo messaggio, quello della TEXTURE della scena (chiavi
    // "textureHintText"/"textureHintSeconds"), usato dai soli record: la scena
    // che catturano ha due sorgenti di costanti indipendenti -- la superficie e
    // la texture -- e con un campo solo il messaggio dell'una scacciava quello
    // dell'altra. I due si mostrano insieme (MainWindow::composedHintText).
    // Vuoto = la texture non ha nulla da dire.
    QString textureHintText;
    float   textureHintSeconds = 6.0f;

    // Costanti discrete dichiarate dal PRESET (chiave JSON "discreteConstants",
    // es. {"A": [2,6]}): la costante assume solo valori interi nel range e il
    // campo scatta all'intero piu' vicino. Equivale a "A := int(2,6);" nello
    // script, ma vale anche per i preset che lo script non ce l'hanno (equazioni
    // scritte nel dock Equations). Vuota = tutte le costanti continue.
    QHash<QString, QPair<int,int>> discreteConstants;

    // ==========================================================
    // TEXTURES & BACKGROUNDS
    // ==========================================================
    bool textureEnabled = false;
    bool isTextureCustom = false;
    QString textureCode;

    bool bgTextureEnabled = false;
    QString bgTextureCode;
    QString displacementCode;

    // Texture transformations
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    float rotation = 0.0f;

    // ==========================================================
    // COLORS & APPEARANCE
    // ==========================================================
    bool isImage = false;
    // Path dell'IMMAGINE vera (PNG/JPG). Per una texture-immagine salvata come
    // JSON, filePath e' il .json mentre imagePath e' l'immagine estratta dal tag
    // //IMG:. Per un file immagine caricato direttamente coincide con filePath.
    QString imagePath;
    bool hasCustomColors = false;

    // Colore dello sfondo. Nei record sempre; nelle Shape solo se diverso da
    // defaultBackgroundColor() (chiave "background" assente = grigio di
    // default, cosi' le Shape salvate prima restano identiche).
    QString bgColor;
    QString color1;
    QString color2;
    QString texColor1;
    QString texColor2;

    // ==========================================================
    // SCRIPTING
    // ==========================================================
    bool isScript = false;
    QString scriptCode;

    // ==========================================================
    // RENDERING & LIGHTING STATE
    // ==========================================================
    int renderMode = 0;
    int projectionMode = 1;
    // FOV 3D (gradi). 45 = default storico: i JSON senza chiave lo ripristinano.
    // cameraFov = chiave legacy (unico FOV); fov3D/fov4D = FOV indipendenti dei
    // due path (i JSON vecchi li ereditano da cameraFov in fase di load).
    float cameraFov = 45.0f;
    float fov3D = 45.0f;
    float fov4D = 45.0f;

    // Densità wireframe (passi campionamento U/V). hasWireframe=false => il preset non
    // la contiene (formato vecchio): il load usa il default (resetWireframeDensity).
    bool hasWireframe = false;
    int wireframeUStep = 4;
    int wireframeVStep = 4;

    // ASPETTO PER-MESH (multi-mesh): un elemento per parte, nell'ordine in cui
    // lo script le dichiara. Assente nei preset che non lo personalizzano, e in
    // quel caso ogni parte eredita dallo stato globale (comportamento storico).
    // I valori negativi dentro MeshPart significano "eredita", quindi qui si
    // riempiono solo i campi realmente presenti nel JSON.
    std::vector<MeshPart> meshParts;
    // Ambito All/Mesh al salvataggio (chiave "meshScopeAll"). Assente nei preset
    // vecchi: li' si apre in "Mesh" se il preset porta un aspetto per-mesh,
    // com'e' sempre stato.
    bool meshScopeAll = false;
    // Dominio dell'ambito "All" (chiavi allUMin/allUMax/allVMin/allVMax):
    // il taglio che vale per tutte le mesh insieme, indipendente da quelli
    // per-parte. Assente nei preset che non l'hanno mai impostato.
    bool  hasAllDomain = false;
    float allUMin = 0.0f, allUMax = 0.0f, allVMin = 0.0f, allVMax = 0.0f;

    bool hasLightingState = false;
    bool use4DLighting = false;
    int lightingMode = -1;

    // DOUBLE, non float: il Save li scrive come valori calcolati in doppia
    // precisione (alpha arrotondata ai centesimi, luce = slider/100). Un float
    // li cambierebbe nel file (0.45 -> 0.4499999881).
    double alpha = 1.0;
    double lightIntensity = 1.0;

    // ==========================================================
    // STATO CATTURATO DAL SAVE (tappa 2 dello "stato unico della scena")
    // ==========================================================
    // Il Save fotografa la scena in un LibraryItem (PresetSerializer::
    // captureSurfaceState / captureMotionState) e lo traduce in JSON
    // (LibraryManager::toJson), simmetrico a parseJson. I campi qui sotto sono
    // quelli che il Save scrive ma che il load dei record legge ancora
    // direttamente dal JSON (applyMotionExample): la tappa 3 li fara' leggere
    // anche a lui da qui.
    // Colore globale della superficie a PIENA precisione: color1 ne e' il nome
    // (8 bit per canale), che il load usa; il Save delle superfici scrive i
    // canali r/g/b come double e col nome cambierebbe il file.
    QColor surfaceColor;
    QString textureLibName;         // texture["libName"]: ancora in libreria
    QString soundLibName;           // "soundLibName": solo se il record ha audio
    QString activeMotion;           // moto camera da riavviare al load
    int pathMode4D = 0;             // "pathMode"   (vista del path 4D)
    int pathMode3D = 0;             // "pathMode3D" (vista del path 3D)
    int speedPath3D = 0;            // speeds["path3D"] (slider)
    int speedPath4D = 0;            // speeds["path4D"] (slider)
    float observer4D = 4.0f;        // default del load per i record senza chiave
    // Sfondo (blocco "background" del record); bgColor, bgTextureEnabled e
    // bgTextureCode sono piu' sopra. Default = quelli del load per i record
    // senza la chiave (applyMotionExample).
    QString bgCol1 = QStringLiteral("#3333cc"), bgCol2 = QStringLiteral("#000000");
    QString bgLibName;
    QString bgHintText;
    float   bgHintSeconds = 6.0f;
    QString bgSkyMode = QStringLiteral("fixed");   // "fixed" / "sphere" / "cylinder" / "cube"
    float bgZoom = 1.0f, bgPanX = 0.0f, bgPanY = 0.0f, bgRotation = 0.0f;
};

struct DeletionBackup {
    bool isValid = false;
    LibraryItem data;
    QString originalPath;
    QString backupPath;
};

// Lo sfondo di una scena nuova, di una Shape senza colore di sfondo e di un
// record senza la chiave: grigio scuro.
inline QColor defaultBackgroundColor() { return QColor::fromRgbF(0.3f, 0.3f, 0.3f); }

// ==========================================================
// MANAGER CLASS
// ==========================================================
class LibraryManager
{
    // Il test di andata e ritorno legge i preset con lo stesso parseJson
    // dell'albero, senza costruire un albero.
    friend class PresetRoundTrip;
    friend class ClockTest;
    friend class ScenarioTest;

public:
    LibraryManager();

    // ==========================================================
    // PUBLIC API
    // ==========================================================
    void clear();
    void loadFromDirectory(const QString &dirPath, QTreeWidget *tree, LibraryType type);

    // Il JSON di una superficie o di un record (secondo d.type), inverso di
    // parseJson: traduzione PURA, non legge ne' interfaccia ne' motore. Lo
    // stato lo fotografa PresetSerializer::capture*State.
    static QJsonObject toJson(const LibraryItem &d);
    // I sei limiti u/v/w di d in `limits` (numero sempre, "...Expr" se formula).
    // La usa anche il Save degli script, che ha un formato suo.
    static void writeParametricLimits(const LibraryItem &d, QJsonObject &limits);
    // I sei limiti x/y/z del Ray Marching, con la stessa regola.
    static void writeSpaceLimits(const LibraryItem &d, QJsonObject &limits);

    // ==========================================================
    // GETTERS
    // ==========================================================
    const LibraryItem& getSurface(int index) const;
    const LibraryItem& getTexture(int index) const;
    const LibraryItem& getMotion(int index) const;
    const LibraryItem& getSound(int index) const;

    // Lookup per percorso file (univoco), robusto al disallineamento degli indici
    // posizionali: dopo un refresh (fsWatcher) la lista m_surfaces viene ricostruita
    // e l'ordine può cambiare, ma i vecchi nodi albero conservano l'indice vecchio.
    // Il tooltip del nodo porta sempre il filePath, quindi cerchiamo per quello.
    // Ritorna nullptr se non trovato.
    const LibraryItem* getSurfaceByPath(const QString &filePath) const;

    // Stesso ruolo di getSurfaceByPath per il ramo RECORD. Mancava, quindi il
    // click su un record risolveva ancora per indice posizionale: dopo un
    // refresh (salvataggio o modifica da Finder) la lista m_motions e'
    // ricostruita e l'ordine puo' cambiare, ma il nodo dell'albero conserva
    // l'indice VECCHIO -> si caricava un record diverso da quello cliccato.
    const LibraryItem* getMotionByPath(const QString &filePath) const;

    // PRESET SALTATI perche' non materializzati su questo disco (segnaposto di
    // iCloud o di un altro file provider): leggerli bloccherebbe il thread della
    // UI a tempo indeterminato. Vedi isDatalessFile in librarymanager.cpp.
    // Serve a MainWindow per dire quanti e quali, invece di lasciare una
    // libreria incompleta e inspiegata. Azzerata da clear().
    const QStringList& skippedDataless() const { return m_skippedDataless; }

    // ==========================================================
    // FILE OPERATIONS & BACKUP
    // ==========================================================
    DeletionBackup softDelete(int index, LibraryType type);
    bool restore(const DeletionBackup &backup);
    bool moveFile(const QString &oldPath, const QString &newFolder);

private:
    // ==========================================================
    // INTERNAL DATA STORAGE
    // ==========================================================
    QList<LibraryItem> m_surfaces;
    QList<LibraryItem> m_textures;
    QList<LibraryItem> m_motions;
    QList<LibraryItem> m_sounds;

    QStringList m_skippedDataless;   // vedi skippedDataless()

    // ==========================================================
    // PRIVATE HELPERS
    // ==========================================================
    QTreeWidgetItem* getOrCreateSubCategory(QTreeWidget* tree, QTreeWidgetItem* parent, const QString& name);
    LibraryItem parseJson(const QString &filePath, LibraryType type);
};

#endif // LIBRARYMANAGER_H
