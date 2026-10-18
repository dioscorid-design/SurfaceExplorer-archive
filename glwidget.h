#ifndef GLWIDGET_H
#define GLWIDGET_H

#include "surfaceengine.h"
#include "geometrybuilder.h"   // WireframeRange (intervalli per parte di mesh)

#include <QWidget>
#include <QRhiWidget>
#include <private/qrhi_p.h>
#include <rhi/qshader.h>
#include <rhi/qshaderbaker.h>
#include <QTimer>
#include <memory>
#include <limits>
#include <functional>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QQuaternion>
#include <QHash>

class InputHandler;
class SurfaceEngine;

struct UboData {
    float mvpMatrix[16];
    float mvMatrix[16];
    float mMatrix[16];
    QVector4D dummyZero;
    QVector4D observerPos;
    QVector4D cameraPos4D;
    QVector4D mathParams;
    QVector4D mathParams2;
    QVector3D color;
    float alpha;
    QVector3D col1;
    float lightIntensity;
    QVector3D col2;
    float zoom;
    QVector2D center;
    float rotation;
    float omega;
    float phi;
    float psi;
    float time;
    int projMode;
    int lightingMode;
    int renderMode;
    int isFlat;
    int useTexture;
    int useSpecular;
    float u_min;
    float u_max;
    float v_min;
    float v_max;
    int hasExplicitW;
    int u_raySteps;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
    // MULTI-MESH: indice della parte in corso di disegno, esposto allo script
    // come u_meshIndex per distinguere il ramo (es. quale toro di Clifford).
    // E' float e non int per essere usabile direttamente nelle espressioni dello
    // script senza conversioni. Vale 0 per le superfici a mesh singola.
    float u_meshIndex;
    // 1 = nessuna immagine caricata nello slot sampler (c'e' solo la texture
    // "tappabuchi"). Gli script che campionano iChannel0 -- gli "Animated
    // Images" -- usano questo flag per cadere su una SCACCHIERA PROCEDURALE
    // mix(u_col1,u_col2) invece dei pixel del fallback: cosi' i picker Color1/2
    // agiscono davvero. Con un'immagine caricata vale 0 e non cambia nulla.
    // Occupa il primo slot dell'ex _pad0 (la coda di riserva), quindi il blocco
    // non cresce e gli offset precedenti restano tutti invariati.
    int u_noImage;
    // LUCE DI RIEMPIMENTO dall'osservatore (headlight), 0 = spenta.
    // Le due luci principali sono quasi opposte fra loro (dot = -0.79) e lasciano
    // scoperto un anello di direzioni perpendicolari a entrambe: misurato, il
    // 17.4% delle facce viste DI FRONTE non riceve luce diffusa e resta col solo
    // ambiente. Si nota lungo i path, dove la camera entra in tubi e cavita' e
    // quelle pareti restano nere -- e lo slider Light non le salva, perche'
    // MOLTIPLICA la luce totale (tre volte quasi-zero resta quasi-zero).
    // Regolabile dall'utente invece che fissa: ogni riempimento SEMPRE attivo
    // che abbiamo provato (two-sided, headlight fissa, luci ancorate al mondo)
    // migliorava i path peggiorando le superfici ferme. A 0 lo shader somma un
    // termine nullo, quindi il default e' identico al comportamento storico e
    // paga solo chi decide di accenderla.
    // Occupa il secondo slot dell'ex _pad0: il blocco non cresce e nessun offset
    // si sposta.
    float u_fillLight;
    // SPESSORE DEL GUSCIO (modalita' Shell), in unita' di scena. 0.005 = il
    // valore storico della sfera unitaria. Era una costante nello shader: da
    // quando il campo e' normalizzato col gradiente lo spessore e' una LUNGHEZZA
    // VERA e non dipende piu' da come e' scritta l'equazione -- corretto, ma
    // prima valeva 0.01/|grad| e le superfici con un fattore di scala davanti
    // (es. A*(...) con A=0.1) avevano gusci 20-50 volte piu' spessi (misurato:
    // Ding Dong 0.23, Steiner 0.47). Con una costante unica quelle superfici si
    // presentano assottigliate. E' quindi un parametro della SCENA, regolabile e
    // salvato nei preset. Occupa l'ultimo slot di riserva di _pad0.
    float u_shellThickness;
    // La coda di riserva (_pad0) e' ESAURITA: i suoi due slot sono diventati
    // u_fillLight e u_shellThickness. Un campo nuovo fara' quindi CRESCERE il
    // blocco -- va aggiunto in coda (std140 non consente di saltare campi, e un
    // inserimento intermedio sposta tutti gli offset successivi) e dichiarato in
    // ENTRAMBI gli shader parametrici, vedi CLAUDE.md.
    // Gli offset di questa struct combaciano con il blocco SceneUBO degli shader:
    // verificato che u_min=372, z_max=416, u_meshIndex=420.
    // Lo spazio fra i blocchi NON e' sizeof(UboData): il passo e' m_uboBlockStride,
    // ricavato da QRhi::ubufAlignment() (256 su Metal/Vulkan), quindi ogni blocco
    // e' comunque allineato come richiede l'API.

    // MARCHER DEL RAY MARCHING: 0 = sphere tracing storico, 1 = ibrido (avvicina
    // con lo sphere tracing, poi localizza la superficie col cambio di segno).
    // L'ibrido elimina le "saldature" (falsi hit nel vuoto dal clamp del
    // gradiente) sulle equazioni di grado alto -- indispensabile sul T^3 del
    // Cross Section, dove il 22% degli hit era falso -- ma introduce difetti sui
    // bordi di alcune superfici 3D, quindi la scelta resta dell'utente (radio
    // nel dock Ray Marching) e si salva nel preset.
    //
    // PRIMO CAMPO OLTRE LA FINE DEL BLOCCO STORICO: la riserva _pad0 era
    // esaurita, quindi il blocco CRESCE. Va in coda (std140 non consente di
    // saltare campi) e dichiarato in ENTRAMBI gli shader parametrici -- Adreno
    // rifiuta il link se un solo stage lo dichiara e su Android si vede schermo
    // vuoto senza alcun errore. Vedi CLAUDE.md.
    float u_marcherMode;
    // RAGGIO DEL TUBO (sotto-tab Tubes del parametrico): lo spessore scelto
    // con campo e slider. Uniforme e non costante nello shader, cosi' lo slider
    // agisce senza ricompilare. Occupa il primo slot della riserva _pad1: il
    // blocco non cresce. Dichiarato in ENTRAMBI gli shader parametrici (regola
    // Adreno, vedi CLAUDE.md); lo usa solo il vertex.
    float u_tubeRadius;
    // RAGGIO DEL BORDO (Border Thickness nel dock Renderer), in unita' di
    // scena; 0 = nessun bordo. Uniforme, cosi' lo slider agisce senza
    // ricompilare (si ricompila solo passando da 0 a un valore e viceversa:
    // il codice del bordo esiste solo con HAS_BORDER). Secondo slot della
    // riserva _pad1: il blocco non cresce. Dichiarato in ENTRAMBI gli shader
    // parametrici (regola Adreno, vedi CLAUDE.md); lo usa solo il vertex.
    float u_borderRadius;
    // 1 nei blocchi del BORDO, 0 altrove: il fragment ci sceglie la texture del
    // bordo (getCustomColor_border) e salta il cutout. Non si puo' usare
    // u_meshIndex: nel bordo vale l'indice della parte, che il vertex passa
    // agli script per costruire la geometria. Ultimo slot di _pad1; da qui il
    // blocco cresce di una riga (464 byte). Dichiarato in ENTRAMBI gli shader
    // parametrici (regola Adreno, vedi CLAUDE.md).
    int u_isBorder;
    // DIREZIONE DI RIFERIMENTO DELLA SEZIONE DEL TUBO (sotto-tab Tubes), per
    // parte: ogni tubo di uno script multi-mesh ha la sua, scelta al Run
    // campionando la sua curva. Nel blocco della parte e non come costante
    // nello shader, cosi' cambiare i tubi non ricompila. Occupano i tre posti
    // della riserva _pad2: il blocco non cresce (464 byte). Dichiarati in
    // ENTRAMBI gli shader parametrici (regola Adreno, vedi CLAUDE.md); li usa
    // solo il vertex.
    float u_tubeRefX;
    float u_tubeRefY;
    float u_tubeRefZ;
    // NUMERO DI COPIE della superficie o della curva (campo Meshes del
    // pannello Multi Mesh, o sezioni //MESH_BEGIN): le equazioni e gli script
    // lo leggono come `meshes`, accanto all'indice `mesh`. Uniforme, cosi'
    // non serve ricompilare quando cambia. In coda: il blocco cresce di una
    // riga (480 byte). Dichiarato in ENTRAMBI gli shader parametrici (regola
    // Adreno, vedi CLAUDE.md); lo usa solo il vertex.
    float u_meshCount;
    // SEZIONE TRASPORTATA DEI TUBI (vedi buildTubeFramePipeline): anelli della
    // parte (numU + 1) e 1 se la passata compute ha scritto per lei anelli e
    // sezione in m_tubeFrameTex. Occupano due posti della
    // riserva _pad3: il blocco non cresce (480 byte). Dichiarati in ENTRAMBI
    // gli shader parametrici (regola Adreno, vedi CLAUDE.md); li usano il
    // vertex e il compute.
    float u_tubeRings;
    float u_tubeFramesOn;
    // Quanto ogni anello si avvicina, in questo frame, alla sezione calcolata
    // (smorzamento degli scatti, vedi buildTubeFramePipeline): dal passo del
    // frame, quindi uguale a schermo e nel video. Ultimo posto della riserva:
    // il blocco resta di 480 byte.
    float u_tubeFrameBlend;
    // 1 nei blocchi del BORDO quando la passata dei LATI IN COMUNE (vedi
    // buildSharedEdgePipeline) ha scritto in questo frame quali lati di ogni
    // parte combaciano con un lato di un'altra: il vertex non li disegna.
    // In coda: il blocco cresce di una riga (496 byte, _pad4). Dichiarato in
    // ENTRAMBI gli shader parametrici (regola Adreno, vedi CLAUDE.md); lo usa
    // solo il vertex.
    float u_sharedEdgesOn;
    // TEXTURE GLOBALE: 1 nei blocchi delle parti di una multimesh in ambito
    // All (la texture e' una sola su tutta la figura, triplanare: vedi
    // surfaceTextureColor in surface.frag). Primo posto di _pad4: il blocco
    // resta di 496 byte. Dichiarato in ENTRAMBI gli shader parametrici (regola
    // Adreno, vedi CLAUDE.md); lo usa solo il fragment.
    float u_globalTexture;
    float _pad4[2];
};

class GLWidget : public QRhiWidget
{
    Q_OBJECT
    friend class ClockTest;   // legge il tempo arrivato allo shader (m_uboData)
    friend class ScenarioTest;   // confronta i sorgenti degli shader (ordine dei load)

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget();

    QRhi* getRhi() { return rhi(); }
    // La mesh e' gia' fatta di posizioni (flusso geodetico), non da parti.
    bool isCustomMesh() const { return m_isCustomMesh; }

    // ==========================================================
    // ENUMS & CONSTANTS
    // ==========================================================
    static const int Ortho4D = 0;
    static const int Perspective4D = 1;
    static const int PerspectiveWide4D = 2;

    enum MoveDir {
        MoveForward, MoveBack, MoveLeft, MoveRight,
        MoveUp, MoveDown,
        RollLeft, RollRight,
        ObsMoveXPos, ObsMoveXNeg,
        ObsMoveYPos, ObsMoveYNeg,
        ObsMoveZPos, ObsMoveZNeg,
        ObsMovePPos, ObsMovePNeg,
        RotOmegaPos, RotOmegaNeg,
        RotPhiPos,   RotPhiNeg,
        RotPsiPos,   RotPsiNeg
    };


    // ==========================================================
    // ENGINE MODES
    // ==========================================================
    enum EngineMode {
        ModeParametric = 0,
        ModeImplicit   = 1
    };

    void setEngineMode(EngineMode mode);
    EngineMode getEngineMode() const { return m_engineMode; }

    // true se il campo implicito corrente e' a prodotto/mal condizionato (es. "Chain"):
    // con alpha<1 sparisce. Su TUTTE le piattaforme la UI disabilita lo slider e il
    // render forza opaco. Vedi m_implicitIllConditioned.
    bool isImplicitIllConditioned() const { return m_implicitIllConditioned; }
    // Azzera il flag per gli script impliciti (GLSL grezzo non valutabile su CPU):
    // il ramo di load che applica lo script via setScriptCodeGLSL non passa da
    // validateAndApplyImplicitScript, che lo azzererebbe. Vedi il chiamante.
    void clearImplicitIllConditioned() { m_implicitIllConditioned = false; }

    // SOLO ANDROID: true se la trasparenza di questa superficie implicita potrebbe
    // degradare (superficie triplamente periodica come Gyroid, o QUALSIASI script RM
    // non valutabile su CPU) per via del budget facce ridotto su Android. A differenza
    // di isImplicitIllConditioned NON blocca e NON forza opaco: lo slider resta usabile,
    // la UI mostra solo un popup di avviso al primo tocco. Sempre false su desktop/iOS.
    bool implicitTransparencyMayDegrade() const { return m_implicitTransparencyWarn; }
    void setImplicitTransparencyWarn(bool w) { m_implicitTransparencyWarn = w; }


    // ==========================================================
    // EQUATIONS & MATHEMATICS
    // ==========================================================
    bool setParametricEquations(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    // TUBO attorno a una curva (sotto-tab Tubes): dalle equazioni (P vuoto =
    // curva 3D) o dal corpo GLSL di uno script del dock Script, gia' tradotto,
    // che restituisce vec4(x, y, z, p) e puo' leggere `mesh`. Con la curva, una
    // copia per tubo (direzione della sezione, chiusura in u). Provano il vertex
    // PRIMA di toccare lo stato, come setParametricEquations; false = non
    // compila (errore in getShaderError) e tutto resta com'era. Escludono la
    // modalita' script della superficie; equazioni e script della superficie,
    // applicati, escludono il tubo.
    bool setTubeCurve(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq,
                      const std::vector<SurfaceEngine::TubeCopy> &copies);
    bool setTubeScript(const QString &glslBody, const std::vector<SurfaceEngine::TubeCopy> &copies);
    // Solo le copie (cambio di dominio o di costanti): niente ricompilazione.
    void setTubeCopies(const std::vector<SurfaceEngine::TubeCopy> &copies);
    // La curva come funzione GLSL vec4 tubeCurve(float u, float v, float w):
    // dalle equazioni, o dal corpo di uno script se non e' vuoto. Unica sede
    // del testo: la usano il vertex (attorno ci costruisce il tubo) e il
    // campionatore sulla GPU (TubeCurveSampler), che al Run sceglie la
    // direzione della sezione -- due copie della curva divergerebbero.
    QString tubeCurveFunction(const QString &xEq, const QString &yEq, const QString &zEq,
                              const QString &wEq, const QString &scriptBody);
    void setImplicitEquation(const QString &eqF);
    void setEquationConstants(float a, float b, float c, float d, float e, float f, float s);
    void setRangeU(float min, float max);
    void setRangeV(float min, float max);
    void setRangeW(float min, float max);
    void setRangeX(float min, float max);
    void setRangeY(float min, float max);
    void setRangeZ(float min, float max);
    void setResolution(int n);
    // Massimo dei Ray Steps: tetto del ciclo del ray marching (%MAX_RAY_STEPS%)
    // e del campo Steps in Ray Marching. Il costo e' per pixel: piu' basso sui
    // dispositivi mobili, dove un frame troppo lungo fa chiudere l'app.
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    static constexpr int kRayStepsMax = 2000;
#else
    static constexpr int kRayStepsMax = 5000;
#endif
    void setRaySteps(int steps);
    int raySteps() const { return m_raySteps; }
    // uMin/uMax/vMin/vMax: il DOMINIO su cui la griglia e' stata integrata. Serve
    // ad ancorare le coordinate texture al dominio invece che agli indici di
    // griglia: senza, restringere l'intervallo ri-stira la texture sulla
    // superficie accorciata invece di tagliarla. Default NaN = "non passato":
    // si ricade sulle UV normalizzate di sempre.
    bool setCustomMesh(const QVector<QVector<QVector4D>>& grid, bool tolerateTruncated = false,
                       float uMin = std::numeric_limits<float>::quiet_NaN(),
                       float uMax = std::numeric_limits<float>::quiet_NaN(),
                       float vMin = std::numeric_limits<float>::quiet_NaN(),
                       float vMax = std::numeric_limits<float>::quiet_NaN());
    const QMap<QString, float>& getConstantsMap() const { return m_constants; }

    SurfaceEngine* getEngine() const { return engine.get(); }

    // Equazioni parametriche attualmente COMPILATE (x, y, z, w gia' composte e
    // tradotte): le lettere delle costanti restano identificatori, quindi dicono
    // quali costanti usa la superficie a schermo.
    QString parametricEquationsApplied() const
    {
        return m_eqX + QLatin1Char(' ') + m_eqY + QLatin1Char(' ') + m_eqZ + QLatin1Char(' ') + m_eqW;
    }

    // Campo implicito attualmente compilato nel ray marcher.
    QString implicitEquation() const { return m_eqImplicitF; }

    // Equazione APPLICATA del ramo davvero attivo nel marcher: nel Cross Section
    // il campo compilato e' m_eqCrossSectionF, e implicitEquation() (che torna
    // sempre quella del sotto-tab 3D) descriverebbe una superficie che a schermo
    // non c'e'. Serve a chi deve giudicare lo STATO della scena e non puo'
    // leggere i campi UI, che possono contenere testo non ancora confermato
    // (vedi MainWindow::isSceneEmpty).
    QString activeImplicitEquation() const {
        return m_implicitUsesCrossSection ? m_eqCrossSectionF : m_eqImplicitF;
    }
    // Limiti spaziali x/y/z del Ray Marching come li usa lo shader (-1000/1000
    // = nessun taglio).
    QVector3D spaceRangeMin() const { return QVector3D(m_uboData.x_min, m_uboData.y_min, m_uboData.z_min); }
    QVector3D spaceRangeMax() const { return QVector3D(m_uboData.x_max, m_uboData.y_max, m_uboData.z_max); }
    // Ramo attivo nel marcher: Cross Section (sezione 4D) o 3D.
    bool implicitUsesCrossSection() const { return m_implicitUsesCrossSection; }
    // Riporta il marcher sul ramo 3D senza toccare le equazioni. setImplicitEquation
    // lo fa per le superfici a equazione; quelle DA SCRIPT non passano di li', e
    // dopo un Cross Section il ramo restava il suo.
    void useImplicit3DBranch();


    // ==========================================================
    // RENDERING & VISUALS
    // ==========================================================
    void updateSurfaceData();
    // SCENA VUOTA: svuota la geometria e la sua ombra wireframe, senza
    // ricalcolare nulla. updateSurfaceData() non va bene qui -- chiama
    // computeMesh(), che rigenererebbe la griglia parametrica dalle equazioni.
    // Il wireframe va azzerato INSIEME alla mesh e non prima: e' costruito da
    // engine (buildWireframe), quindi un buildWireframeGeometry() chiamato
    // mentre la mesh vecchia e' ancora in pancia -- come fa
    // resetWireframeDensity() piu' a monte nel reset -- lo ripopola con le
    // linee della superficie che stiamo buttando via, e a schermo restano.
    void clearSceneGeometry() {
        if (engine) engine->clear();
        m_wireframeIndices.clear();
        m_wireframeRanges.clear();
        m_wireframeIndexCount = 0;
        meshNeedsUpdate = true;
        wireframeNeedsUpdate = true;
        update();
    }
    void resetVisuals();
    // Inquadratura 2D (zoom, pan, rotazione) della texture di superficie e dello
    // sfondo al default. E' della scena: la usano il load di una superficie
    // (resetVisuals) e il reset di scena, cosi' la scena nuova non la eredita.
    void resetTextureFraming();
    void setProjectionMode(int mode);
    // ASPETTO PER-MESH: se una parte e' selezionata con lo spinbox del dock
    // renderer, setColor/setAlpha scrivono su QUELLA parte invece che sullo
    // stato globale (vedi applyToActiveMeshPart). setLightIntensity no: la luce
    // e' della scena, globale anche in ambito "Mesh" (vedi glwidget.cpp).
    //
    // I SETTER DI GLWIDGET SONO DI DUE FAMIGLIE, con contratti OPPOSTI. Sbagliare
    // famiglia e' la radice di un'intera serie di bug (il colore del preset che
    // finiva dentro una fascia, il wireframe di una fascia che spegneva la
    // texture di tutta la superficie, i colori di una texture per-mesh addosso a
    // quella globale). Il prefisso nel nome dice a quale appartengono:
    //
    //   setColor / setAlpha
    //       DIROTTANO sulla parte attiva. Per scrivere davvero il globale (stato
    //       del preset, reset automatici del motore) serve il BYPASS.
    //   setGlobal* (setGlobalRenderMode / setGlobalTextureEnabled /
    //   setGlobalTextureColors)
    //       scrivono SEMPRE lo stato globale, qualunque mesh sia selezionata.
    //       Chi sta agendo su una fascia NON deve chiamarli: esistono i
    //       setActiveMesh* corrispondenti.
    //   setActiveMesh* (setActiveMeshRenderMode / setActiveMeshTexture /
    //   setActiveMeshTexColors / setActiveMeshWireframeDensity)
    //       scrivono SOLO sulla parte; tornano false in "All", dove il chiamante
    //       deve ricadere sul setGlobal* corrispondente.
    void setActiveMeshPart(int index);
    // Sospende temporaneamente il dirottamento sulla mesh attiva: i reset
    // AUTOMATICI del motore (es. il ripristino di alpha/luce quando si entra in
    // wireframe) devono agire sullo stato GLOBALE, non essere scambiati per una
    // scelta dell'utente su quella parte. Vedi MeshAppearanceBypass.
    void setMeshAppearanceBypass(bool on) { m_meshAppearanceBypass = on; }
    bool meshAppearanceBypass() const { return m_meshAppearanceBypass; }
    // AMBITO "ALL": la superficie si comporta come UNA SOLA. L'aspetto proprio
    // delle parti viene IGNORATO dal render (non cancellato), quindi colore,
    // trasparenza, luce e wireframe globali valgono per tutte. Tornando su
    // "Mesh" le differenze ricompaiono: i valori sono rimasti in MeshPart.
    // NB: ricostruisce la geometria delle linee, perche' anche le DENSITA'
    // per-parte vengono sospese (sono indici, non uniform).
    // NB2: ricompila anche lo SHADER, perche' in "All" le texture per-mesh sono
    // sospese e il dispatcher su u_meshIndex non va nemmeno generato (vedi
    // createFragmentShaderSource). Definita nel .cpp: rebuildShader non e'
    // visibile a questo punto dell'header.
    void setMeshAppearanceUniform(bool on);
    bool meshAppearanceUniform() const { return m_meshAppearanceUniform; }
    // TEXTURE GLOBALE: su una multimesh in ambito All la texture e' una sola
    // su tutta la figura (triplanare, u_globalTexture), non una copia sulle
    // coordinate di ogni parte. Sulle superfici a mesh singola mai.
    bool globalTextureActive() const;
    int activeMeshPart() const { return m_activeMeshPart; }
    int meshPartCount() const { return engine ? engine->getMeshPartCount() : 0; }

    void setGlobalRenderMode(int mode);
    // Modalita' di rendering GLOBALE (0=Base, 1=Phong, 2=Wireframe). E' la fonte
    // di verita' per cio' che una parte "eredita": i radio dell'interfaccia NON
    // lo sono, perche' mostrano la modalita' della mesh selezionata.
    int globalRenderMode() const { return renderMode; }
    // Colore GLOBALE della superficie, come globalRenderMode: e' quello che una
    // parte SENZA colore proprio sta mostrando, e quindi quello che i controlli
    // devono visualizzare quando si seleziona una mesh che eredita.
    void globalColor(float &r, float &g, float &b) const { r = red; g = green; b = blue; }
    // Trasparenza e luce GLOBALI, stessa logica: sono i valori in vigore in
    // ambito "All" e quelli che una parte senza valore proprio eredita.
    float globalAlpha() const { return alpha; }
    float globalLightIntensity() const { return m_lightIntensity; }
    // COLORI u_col1/u_col2 della texture di SUPERFICIE. Sono i due slot globali,
    // che il ramo per-mesh non tocca mai: sono l'unica memoria di questi colori
    // (MainWindow::surfaceTexColor li legge da qui per i picker, il Save del
    // record li scrive) e quelli che eredita una fascia senza colori propri.
    QColor globalTexColor1() const { return QColor::fromRgbF(texRed1, texGreen1, texBlue1); }
    QColor globalTexColor2() const { return QColor::fromRgbF(texRed2, texGreen2, texBlue2); }
    // COMANDO esplicito dell'utente sulla mesh selezionata: e' l'UNICA via che
    // scrive una modalita' PROPRIA su una parte. setGlobalRenderMode() (chiamata da
    // updateRenderState a ogni cambio tab/proiezione/load) resta invece sempre
    // GLOBALE: e' la separazione fra display e comando che rende stabile il
    // wireframe per-mesh. Con "All" selezionato ricade sul globale.
    void setActiveMeshRenderMode(int mode);

    // TEXTURE PROCEDURALE PER-MESH. Ritornano true se hanno scritto su una
    // parte; false in "All", dove il chiamante deve applicare al globale.
    bool setActiveMeshTexture(const QString &code, bool enabled);
    // Spegne/riaccende la texture della parte CONSERVANDO lo script: lo spegnimento
    // NON deve passare da setActiveMeshTexture, che e' la via di COMANDO e
    // riscriverebbe textureCode forzando hasCustomTexture=true (rimetterebbe "in
    // vita" la texture che si sta togliendo). Usato dal wireframe, che la texture
    // non la disegna, e dal checkbox. True se ha cambiato qualcosa.
    bool setActiveMeshTextureEnabled(bool on);
    // COLORI u_col1/u_col2 della parte selezionata. Stesso contratto degli altri
    // setActiveMesh*: true se ha scritto su una parte, false in "All", dove il
    // chiamante deve usare setGlobalTextureColors (che e' SEMPRE globale).
    // Senza questa via, muovere gli slider colore su una fascia texturizzata
    // passava da setGlobalTextureColors e riscriveva i due slot GLOBALI: cambiava i
    // colori della texture di superficie e di ogni altra fascia che li eredita.
    bool setActiveMeshTexColors(const QColor &c1, const QColor &c2);
    // Zoom/pan/rotazione 2D della parte attiva, da FUORI la vista 2D: la usa chi
    // APPLICA una texture per darle l'inquadratura del suo preset, come il ramo
    // globale fa con setFlatZoom/setFlatPan/setFlatRotation. True se ha scritto
    // su una parte, false in "All".
    bool setActiveMeshTexTransform(float zoom, const QVector2D &pan, float rotation);
    // Nome della voce di libreria da cui viene la texture della parte attiva
    // (MeshPart::textureLibName). Lo scrive chi APPLICA una texture dalla
    // Library; true se ha scritto su una parte, false in "All".
    bool setActiveMeshTextureLibName(const QString &name);
    // Codice della texture della fascia di indice dato (non la parte attiva),
    // per il Sync di tutte le fasce. Non ricompila: rebuildShader lo fa il
    // chiamante una volta sola. False se la parte non ha una texture propria.
    bool setMeshPartTextureCode(int index, const QString &code, bool animating);
    // OROLOGIO TEXTURE PER-MESH (Stop/Run della sola parte selezionata).
    // Contratto degli altri setActiveMesh*: true se ha scritto su una parte,
    // false in "All", dove decide il chiamante.
    bool setActiveMeshTextureAnimating(bool animating);
    // Azzera l'orologio della SOLA parte attiva (riclic sulla texture della
    // fascia dalla Library). Contratto degli altri setActiveMesh*: true se ha
    // scritto su una parte, false in "All".
    bool resetActiveMeshTextureTime();
    // Riavvia, se fermo, il timer che produce i tick di animazione (m_animTimer).
    // I tre setter globali (setSurface/SurfaceTexture/BackgroundTextureAnimating)
    // lo fanno da se' quando ACCENDONO; gli orologi PER-MESH vivono nelle
    // MeshPart e vengono scritti anche da fuori (restartAnimatedMeshTextures),
    // quindi hanno bisogno di questa via esplicita: un flag alzato con
    // m_animTimer fermo dice "in moto" senza che nulla si muova.
    void ensureTextureClockRunning();
    // Scrive l'orologio su TUTTE le parti (ambito "All" e master Start/Stop).
    // ADOZIONE ESPLICITA: nessuna parte eredita il clock globale, o ne
    // erediterebbe anche il freeze. Vedi la nota su timeTex in MeshPart.
    void setAllMeshTexturesAnimating(bool animating);
    // Stato dell'orologio della parte attiva: e' cio' che i tasti Run/Stop
    // mostrano in ambito "Mesh".
    bool isActiveMeshTextureAnimating() const;
    // C'e' almeno una parte con texture propria, accesa e in movimento?
    // Il master button la considera attivita' da fermare/riavviare.
    bool anyMeshTextureAnimating() const;
    QString activeMeshTextureCode() const;
    // Come sopra, ma vuoto se la texture della parte e' SPENTA (lo script resta
    // conservato). E' cio' che l'editor deve mostrare: vedi la nota nel .cpp.
    QString activeMeshEffectiveTextureCode() const;
    // Il percorso nominato dal tag //IMG: di uno script di texture ("" se non
    // c'e'). E' la regola unica con cui il renderer decide l'immagine di una
    // fascia.
    static QString imagePathInTextureCode(const QString &code);
    // La fascia attiva ha un'immagine da campionare (la sua o, in mancanza,
    // quella della superficie)? Vedi il .cpp.
    bool activeMeshSamplesImage() const;
    // Percorso dell'immagine PROPRIA caricata in GPU per la fascia `index`
    // ("" se disegna con quella della superficie o senza immagine).
    QString meshPartLoadedImagePath(int index) const;
    // Texture EFFICACE della parte attiva (propria E accesa): e' cio' che il
    // render disegna, quindi decide se gli slider colore editano u_col1/u_col2
    // o la tinta della superficie, e se il tasto 2D ha qualcosa da mostrare.
    bool activeMeshTextureActive() const;
    // Torna a ereditare dal globale (voce "Inherit" del selettore).
    void clearActiveMeshRenderMode();
    // Modalita' EFFICACE della parte selezionata (globale se "All"/eredita):
    // e' cio' che i radio devono MOSTRARE.
    int  activeMeshEffectiveRenderMode() const;
    // Congela nelle parti che EREDITANO la modalita' globale attuale, rendendola
    // loro (hasCustomRenderMode = true) senza cambiare cio' che si vede adesso.
    // Serve prima di cambiare il globale dall'ambito "All": senza questo passo le
    // parti che ereditano seguirebbero il nuovo globale e l'aspetto misto
    // impostato per-mesh andrebbe perso. Chi ha gia' una modalita' propria non
    // viene toccato. Vedi la nota in MainWindow::onUserRenderModeChosen.
    void pinInheritedRenderModes();
    // Gemelle per TRASPARENZA e COLORE: stesso difetto, stessa cura. Le parti
    // senza alpha/colore proprio (valore negativo) seguono il globale, che e'
    // proprio cio' che "All" modifica. Le chiamano setAlpha/setColor quando il
    // globale cambia davvero fuori dal bypass (load e reset automatici).
    void pinInheritedAlphas();
    void pinInheritedColors();
    // Densita' wireframe della parte selezionata (0,0 = eredita dal globale).
    void setActiveMeshWireframeDensity(int uStep, int vStep);
    void activeMeshWireframeDensity(int &uStep, int &vStep) const;

    // DOMINIO PARAMETRICO DELLA PARTE SELEZIONATA (u_min/u_max/v_min/v_max).
    // A differenza di colore, texture e densita' wireframe, il dominio NON e'
    // "aspetto": e' cio' da cui i vertici vengono generati, quindi cambiarlo
    // impone di rifare la griglia (updateSurfaceData), non solo di riscrivere
    // l'UBO. Per questo non passa da applyToActiveMeshPart.
    //
    // L'ALTRA DIFFERENZA, ed e' quella che conta: il dominio NON viene
    // ricopiato nelle parti DICHIARATE (syncPartAppearance non lo tocca, ed e'
    // deliberato). Lo script resta percio' l'autorita': un Run di
    // //MESH_BEGIN riscrive i domini e cancella queste modifiche, esattamente
    // come lo slider Steps convive con la direttiva "steps :=" dichiarata.
    // Il contrario -- preservarlo come si fa con l'aspetto -- farebbe sembrare
    // rotto il Run: si modifica la sezione //MESH_BEGIN e non cambia nulla.
    // Ritorna false se non c'e' una parte attiva ("All") o se i limiti sono
    // incoerenti (min >= max), cosi' il chiamante sa che non ha applicato.
    bool setActiveMeshDomain(float uMin, float uMax, float vMin, float vMax);
    // Gemelli per l'ambito "All": il dominio vale per tutte le mesh insieme e
    // SOSPENDE quelli per-parte senza cancellarli (come m_meshAppearanceUniform
    // fa per colore e texture). Vedi SurfaceEngine::setAllDomain.
    bool setAllMeshDomain(float uMin, float uMax, float vMin, float vMax);
    bool allMeshDomain(float &uMin, float &uMax, float &vMin, float &vMax) const;
    // Dominio della parte selezionata, per il DISPLAY dei campi. false se non
    // c'e' parte attiva: i campi vanno allora svuotati e disabilitati.
    bool activeMeshDomain(float &uMin, float &uMax, float &vMin, float &vMax) const;
    void setColor(float r, float g, float b);
    void setAlpha(float a);
    void setSpecularEnabled(bool enabled);
    void setLightIntensity(float intensity);
    // Luce di riempimento dall'osservatore (0 = spenta, default). Vedi
    // u_fillLight in UboData per il perche'. GLOBALE, non per-mesh: e' una
    // proprieta' dell'illuminazione della scena, non dell'aspetto di una parte.
    void setFillLight(float v);
    float fillLight() const { return m_fillLight; }
    // Spessore del guscio (modalita' Shell). Vedi u_shellThickness in UboData.
    // Serve un rebuildShader? No: e' un uniform, non entra nel sorgente.
    void setShellThickness(float v);
    float shellThickness() const { return m_shellThickness; }
    // MARCHER del ray marching: false = sphere tracing storico, true = ibrido
    // (elimina le saldature sulle equazioni di grado alto, vedi u_marcherMode).
    // E' un uniform: cambiarlo NON richiede di ricompilare lo shader.
    void setHybridMarcher(bool on);
    bool hybridMarcher() const { return m_hybridMarcher; }
    // RAGGIO DEL TUBO (u_tubeRadius): un uniform, quindi lo slider dello
    // spessore agisce senza ricompilare lo shader.
    void setTubeRadius(float r) { m_tubeRadius = r; update(); }
    float tubeRadius() const { return m_tubeRadius; }
    // BORDO DELLA SUPERFICIE (parametrico): tubi attorno ai lati del dominio,
    // raggio in unita' di scena, 0 = nessun bordo. Il raggio e' un uniform
    // (u_borderRadius), ma il codice del bordo esiste nello shader solo con
    // HAS_BORDER: si ricompila soltanto passando da 0 a un valore e viceversa.
    void setBorderRadius(float r);
    float borderRadius() const { return m_borderRadius; }
    void setBorderColor(const QColor &c) { m_borderColor = c; update(); }
    QColor borderColor() const { return m_borderColor; }
    // Aspetto PROPRIO del bordo, indipendente da quello della superficie: Base
    // (0), Phong (1) o Wireframe (2), e in wireframe la densita' delle linee
    // (un anello ogni wireAlong punti lungo il lato, una linea lungo il tubo
    // ogni wireAround lati del poligono). Il bordo ha il suo blocco di uniform,
    // quindi anche Base/Phong sono suoi, non della scena.
    void setBorderRenderMode(int mode);
    int borderRenderMode() const { return m_borderRenderMode; }
    // Tasti Density u (lungo il lato) e Density v (attorno al tubo) col
    // bersaglio Border: delta < 0 = piu' fitto.
    void adjustBorderWireStep(bool along, int delta);
    struct BorderStyle {
        float radius = 0.0f;
        QColor color = QColor::fromRgbF(1.0f, 1.0f, 1.0f);
        int mode = 0;
        int wireAlong = kBorderWireAlongDefault;
        int wireAround = kBorderWireAroundDefault;
        // TEXTURE del bordo (dalla Library): codice (o il solo tag //IMG:),
        // accesa/spenta, i due colori, l'inquadratura 2D della voce e il suo
        // nome (focus dell'albero).
        QString texCode;
        bool texEnabled = false;
        QColor texCol1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
        QColor texCol2 = QColor(Qt::black);
        float texZoom = 1.0f;
        QVector2D texPan = QVector2D(0.0f, 0.0f);
        float texRotation = 0.0f;
        QString texLibName;
        // Messaggio della texture del bordo (hintText della voce): si compone
        // a schermo con quelli di superficie, texture e sfondo
        // (MainWindow::composedHintText) e va nel blocco "border" del record.
        QString texHint;
        float texHintSeconds = 6.0f;
    };
    static constexpr int kBorderWireAlongDefault = 8;
    static constexpr int kBorderWireAroundDefault = 2;
    static constexpr int kBorderWireAlongMax = 50;
    static constexpr int kBorderWireAroundMax = 6;   // in dodicesimi di giro: 2 linee
    BorderStyle borderStyle() const;
    void setBorderStyle(const BorderStyle &s);
    // Il bordo, acceso, si vedrebbe? Su una superficie chiusa (toro, sfera,
    // tori di Hopf, Oloid) ogni lato e' una cucitura, un polo o giace su
    // un'altra parte: il bordo non compare, e il dock ne spegne i comandi (che
    // altrimenti sporcherebbero la scena senza effetto). Lo dice la passata dei
    // lati coperti, riletta come SONDA poco dopo ogni cambio di geometria
    // (m_borderProbeKey; a bordo spento gira apposta); sulla mesh custom il test della CPU
    // (setBorderEdges). Unknown finche' non si sa: tubo, niente compute, prima
    // sonda non ancora arrivata. Cambiando emette borderAvailabilityChanged.
    enum class BorderAvailability { Unknown, Visible, None };
    BorderAvailability borderAvailability() const;
    // TEXTURE DEL BORDO. Coordinate: lungo il lato (0..1 su ciascun lato) e
    // attorno al tubo (0..1). Il codice entra nel fragment come
    // getCustomColor_border solo col bordo acceso: cambiarlo ricompila.
    void setBorderTexture(const QString &code, bool enabled);
    void setBorderTextureEnabled(bool on) { m_borderTexEnabled = on; update(); }
    QString borderTextureCode() const { return m_borderTexCode; }
    bool borderTextureEnabled() const { return m_borderTexEnabled; }
    // Disegnata davvero: bordo acceso, texture accesa e con codice, non in
    // wireframe (li' il fragment esce col colore pieno).
    bool borderTextureShown() const {
        return m_borderRadius > 0.0f && m_borderTexEnabled
               && !m_borderTexCode.trimmed().isEmpty() && m_borderRenderMode != 2;
    }
    void setBorderTexColors(const QColor &c1, const QColor &c2) { m_borderTexCol1 = c1; m_borderTexCol2 = c2; update(); }
    QColor borderTexColor1() const { return m_borderTexCol1; }
    QColor borderTexColor2() const { return m_borderTexCol2; }
    void setBorderTexTransform(float zoom, const QVector2D &pan, float rotation) {
        m_borderTexZoom = zoom; m_borderTexPan = pan; m_borderTexRotation = rotation; update();
    }
    void setBorderTextureLibName(const QString &n) { m_borderTexLibName = n; }
    QString borderTextureLibName() const { return m_borderTexLibName; }
    void setBorderTextureHint(const QString &text, float seconds) {
        m_borderTexHint = text.trimmed(); m_borderTexHintSeconds = seconds;
    }
    QString borderTextureHint() const { return m_borderTexHint; }
    float borderTextureHintSeconds() const { return m_borderTexHintSeconds; }
    // Orologio proprio, avanzato da advanceClocksBy come gli altri.
    void setBorderTextureAnimating(bool on);
    bool isBorderTextureAnimating() const { return m_borderTexAnimating; }
    // Il bordo ha un'immagine da campionare: la sua, o in mancanza quella
    // della superficie (senza nessuna delle due c'e' la scacchiera di ripiego).
    bool borderSamplesImage() const;
    void increaseWireframeUDensity();
    void decreaseWireframeUDensity();
    void increaseWireframeVDensity();
    void decreaseWireframeVDensity();
    // Riporta la densità wireframe (wfStepU/V) al valore di default. Va azzerata a ogni
    // nuova superficie (cambio tab / caricamento senza densità salvata) per non ereditare
    // quella della precedente. I preset che HANNO la densità salvata la ripristinano
    // invece via setWireframeDensity dopo il load (vedi applyCommonData).
    void resetWireframeDensity();
    // Densità wireframe corrente (passi di campionamento U/V). Serializzata nei preset
    // per riprodurre a schermo lo stesso numero di linee al reload.
    int getWireframeUStep() const { return wfStepU; }
    int getWireframeVStep() const { return wfStepV; }
    void setWireframeDensity(int uStep, int vStep);
    // Ricostruisce la geometria delle linee. Serve dopo aver scritto densita'
    // per-parte (che cambiano gli indici, non un uniform), p.es. al load di un
    // preset. Innocua se la mesh non e' ancora pronta.
    void rebuildWireframeGeometry() { buildWireframeGeometry(); update(); }
    // Chiamato dal popup di rallentamento quando l'utente sceglie "Keep going":
    // sopprime ogni ulteriore avviso finche' l'animazione non si ferma/riparte.
    void acknowledgePerformanceWarning() { m_perfWarnDismissed = true; }
    // Riarma il watchdog dopo che era stato zittito senza passare da rebuildShader
    // (es. la guardia displacement lo ha zittito disattivando la trasparenza; se
    // l'utente riabbassa lo slider vogliamo che torni a vigilare). Azzera anche il
    // livello mostrato cosi' un nuovo rallentamento riavvisa da capo.
    void rearmPerformanceWarning() { m_perfWarnDismissed = false; m_perfWarnLevelMs = 0.0f; }
    // true se un'animazione e' in corso e l'EMA del watchdog misura GIA' un
    // carico pesante (~sotto i 6-7 fps). Usato dalla UI per chiedere conferma
    // PRIMA di attivare effetti che moltiplicano il costo per pixel (ramo
    // trasparente RM: ~4-12x l'opaco -> dal carico pesante si passerebbe
    // dritti al collasso, che il watchdog fermerebbe solo DOPO il magenta).
    // A riposo m_avgFrameMs vale 16 -> false: mai popup su scene fluide o ferme.
    bool renderingUnderHeavyLoad() const { return m_avgFrameMs > 150.0f; }
    // Displacement RM attualmente applicato: la UI lo legge PRIMA di un apply per
    // capire se lo sta introducendo/cambiando (guardia trasparenza mobile: il
    // displacement gira dentro map() e col ramo trasparente il costo esplode).
    QString currentDisplacementCode() const { return m_displacementCode; }
    // Texture RM attualmente applicata. Serve a ricommittare lo shader senza
    // perderla: validateAndApplyImplicitShader prende equazione, texture e
    // displacement insieme, quindi chi ricommitta la sola equazione (es. il load
    // di una superficie Cross Section) deve poter ripassare le altre due com'erano.
    QString currentTextureCode() const { return m_textureCode; }
    // Texture PARAMETRICA globale attualmente compilata nel fragment. E' il
    // canale gemello di currentTextureCode() per l'altro modo: i due non si
    // leggono mai a vicenda, e vederli affiancati e' l'unico modo di accorgersi
    // che uno dei due e' rimasto indietro (vedi la sonda SE_TEX_PROBE).
    QString currentParametricTextureCode() const { return m_customFragmentCode; }
    float getSurfaceScale() const { return m_surfaceScale; }
    void rebuildShader();


    // ==========================================================
    // TEXTURES, SCRIPTS & BACKGROUND
    // ==========================================================
    void loadTextureFromFile(const QString &filename);
    void loadTextureFromImage(const QImage &img);
    void setGlobalTextureEnabled(bool enable);
    void setGlobalTextureColors(const QColor& c1, const QColor& c2);
    void clearTexture();

    void setScriptCheck(bool enabled);
    bool loadCustomShader(const QString &customCode);

    void setBackgroundColor(const QColor &color);
    void setBackgroundTexture(const QString &path);
    // La sola IMMAGINE nel sampler dello sfondo, senza cambiare modo: uno
    // script attivo resta attivo e la campiona da iChannel0. setBackgroundTexture
    // e' questa piu' il ritorno al modo immagine. Serve a togliere l'immagine
    // ("background.png", la default) quando lo script non la nomina piu'.
    void setBackgroundImage(const QString &path);
    // Percorso dell'ultima immagine caricata come sfondo ("background.png" per
    // la default, vuoto prima del primo caricamento): cio' che e' a schermo, e
    // quindi cio' che il Save deve scrivere nel tag //IMG: dello sfondo. Lo
    // verifica il test degli scenari.
    QString backgroundImagePath() const { return m_bgImagePath; }
    // L'immagine dell'UTENTE nello sfondo: vuoto se c'e' la default o niente.
    QString backgroundUserImagePath() const
    {
        return m_bgImagePath == QLatin1String("background.png") ? QString() : m_bgImagePath;
    }
    // Gemello per la texture di SUPERFICIE: il file dell'immagine dell'utente
    // caricata nel sampler, vuoto se non ce n'e' (scacchiera generata o
    // niente). E' cio' che il tag //IMG: del Save deve dire.
    QString surfaceImagePath() const { return m_surfaceImagePath; }
    bool backgroundIsScript() const { return m_bgIsScript; }
    void setBackgroundTextureEnabled(bool enabled);
    bool isBackgroundTextureEnabled() const { return m_useBackgroundTexture; }
    // FORMA DELLO SFONDO (radio del gruppo "Background Controls" del Renderer).
    // Fixed (default): lo sfondo e' un poster incollato allo schermo. Le altre
    // tre lo rendono SOLIDALE con la scena: e' un cielo all'infinito, campionato
    // per DIREZIONE di vista -- ruotando la camera (path 3D/4D, camera libera)
    // scorre; traslandola no, come le stelle viste da un'auto in corsa. La forma
    // decide solo come l'immagine si distribuisce sulle direzioni:
    //   Sphere   -- equirettangolare, per i panorami 360 (2:1); stirata ai poli.
    //   Cylinder -- avvolta in orizzontale, dritta in verticale, proporzioni
    //               dell'immagine rispettate; aperta sopra e sotto (si ripete).
    //   Cube     -- la stessa immagine sulle sei facce, ognuna piatta: un'immagine
    //               qualsiasi non si deforma.
    // I valori sono quelli che lo shader legge (vedi bgBaseUV): non riordinare.
    // Nessun rebuild: la scelta viaggia nell'UBO dello sfondo (vedi render()).
    enum BgSkyMode { BgFixed = 0, BgSphere = 1, BgCylinder = 2, BgCube = 3 };
    // Nome nel JSON dei record ("background"/"skyMode"). Parole e non numeri: il
    // file resta leggibile e sopravvive a un riordino dell'enum. Una sola tabella
    // per salvataggio e caricamento; un nome sconosciuto o assente = Fixed.
    static QString bgSkyModeName(int mode) {
        static const char *names[] = { "fixed", "sphere", "cylinder", "cube" };
        return (mode >= BgFixed && mode <= BgCube) ? QString(names[mode]) : QString("fixed");
    }
    static int bgSkyModeFromName(const QString &name) {
        for (int m = BgFixed; m <= BgCube; ++m)
            if (name.compare(bgSkyModeName(m), Qt::CaseInsensitive) == 0) return m;
        return BgFixed;
    }
    void setBackgroundSkyMode(int mode) { if (m_bgSkyMode == mode) return; m_bgSkyMode = mode; update(); }
    int backgroundSkyMode() const { return m_bgSkyMode; }
    // Stato GLOBALE della texture di superficie (quello che le parti senza
    // texture propria ereditano).
    bool isTextureEnabled() const { return m_textureEnabled; }
    void loadBackgroundScript(const QString &scriptCode);

    void setTextureCode(const QString& code);
    void setDisplacementCode(const QString& code);

    bool validateAndApplyTextureDisplacement(const QString &texCode, const QString &dispCode);


    // ==========================================================
    // 2D FLAT VIEW
    // ==========================================================
    void setFlatView(bool active);
    bool isFlatView() const { return m_isFlatView; }
    // Passando da Superficie (0) a Sfondo (1) si abbandona l'editing della
    // texture della parte attiva: va fissata prima, o resta nei membri globali.
    // Tornando su Superficie si ricarica quella della parte.
    // Il commit SOLO in vista 2D, come in setActiveMeshPart: fuori dalla vista
    // 2D il buffer non segue ne' la mesh selezionata ne' il preset caricato, e
    // contiene l'inquadratura di chissa' quale texture precedente. Scriverlo
    // sulla parte attiva la corrompeva -- lo faceva ogni Save di un record (che
    // passa da Sfondo per leggerne lo zoom): la mesh 0 di Brieskorn-Pham (3,5)
    // prendeva lo zoom 0.57 della texture di Clifford Labyrinth aperto prima.
    // Le modifiche vere passano dai setFlat*(), che fanno il commit da se'.
    void setFlatViewTarget(int target) {
        if (target == m_flatViewTarget) { update(); return; }
        if (m_isFlatView) commitFlatTransformToActivePart();
        m_flatViewTarget = target;
        loadFlatTransformFromActivePart();
        update();
    }
    // 0 = texture di superficie, 1 = sfondo, 2 = texture del bordo: su cosa
    // agiscono zoom, pan e rotazione 2D (getFlat*/setFlat*) e cosa mostra la
    // vista 2D. Lo legge il test degli scenari.
    int flatViewTarget() const { return m_flatViewTarget; }
    float getFlatZoom() const;
    void setFlatZoom(float z);
    float getFlatRotation() const;
    void setFlatRotation(float angle);
    void addFlatRotation(float angle);
    void rotateFlat90();
    QVector2D getFlatPan() const;
    // Inquadratura dello SFONDO senza passare dal bersaglio della vista 2D (la
    // legge il Save: prima commutava il bersaglio avanti e indietro).
    float backgroundZoom() const { return m_bgZoom; }
    QVector2D backgroundPan() const { return m_bgPan; }
    float backgroundRotation() const { return m_bgRotation; }
    // Inquadratura dello sfondo scritta dalla scena (load, record, script).
    void setBackgroundFraming(float zoom, const QVector2D &pan, float rotation)
    { m_bgZoom = zoom; m_bgPan = pan; m_bgRotation = rotation; }
    // Colori della texture di sfondo (u_col1/u_col2 della sua copia dell'UBO).
    void setBackgroundTexColors(const QColor &c1, const QColor &c2)
    {
        m_bgTexCol1 = QVector3D(c1.redF(), c1.greenF(), c1.blueF());
        m_bgTexCol2 = QVector3D(c2.redF(), c2.greenF(), c2.blueF());
        m_bgTexColorsSet = true;
    }
    void setFlatPan(float x, float y);

    // TRASFORMAZIONE 2D GLOBALE della texture di superficie, per la PERSISTENZA.
    // I getFlat*() qui sopra espongono il buffer di lavoro della vista 2D (lo usa
    // inputhandler per il trascinamento incrementale), che durante l'editing di
    // una fascia contiene l'inquadratura di QUELLA. Il preset deve invece salvare
    // quella della superficie, o riaprendolo l'ultima fascia toccata imporrebbe
    // il proprio zoom a tutte le altre.
    float globalTexZoom() const { return m_globalTexZoom; }
    float globalTexRotation() const { return m_globalTexRotation; }
    QVector2D globalTexPan() const { return m_globalTexPan; }
    void setGlobalTexTransform(float zoom, const QVector2D &pan, float rotation) {
        m_globalTexZoom = zoom;
        m_globalTexPan = pan;
        m_globalTexRotation = rotation;
        // In ambito "All" il buffer di lavoro E' la trasformazione globale:
        // tenerli allineati evita che il load di un preset lasci la vista 2D
        // sull'inquadratura precedente.
        if (m_activeMeshPart < 0) {
            m_flatZoom = zoom;
            m_flatPan = pan;
            m_flatRotation = rotation;
        }
        update();
    }

    // Trasferimento della trasformazione 2D (zoom/pan/rotazione della texture)
    // fra i membri globali e la parte di mesh attiva. Sono il perno del
    // per-mesh: senza il commit la trasformazione resta globale e finisce nel
    // blocco UBO di TUTTE le parti (e dell'ambito All).
    void commitFlatTransformToActivePart();
    void loadFlatTransformFromActivePart();


    // ==========================================================
    // CAMERA 3D & 4D STATE
    // ==========================================================
    void set4DLighting(bool enable);
    void setLightingMode4D(int mode);
    bool is4DActive() const;

    void setRotation4D(float o, float p, float ps);
    // Sposta la quota del piano di sezione (Cross Section). Cumulativo.
    void moveCrossSectionP(float delta) { m_crossSectionP += delta; update(); }
    float crossSectionP() const { return m_crossSectionP; }
    // Valore ASSOLUTO, per il caricamento di un preset: moveCrossSectionP e'
    // incrementale (i tasti di spostamento) e non puo' ripristinare una posizione
    // salvata senza conoscere quella corrente.
    void setCrossSectionP(float p) { m_crossSectionP = p; update(); }
    float getOmega() const { return omega; }
    float getPhi() const { return phi; }
    float getPsi() const { return psi; }

    void setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& target, float roll);
    void setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D);
    // Handoff camera tra path 3D<->4D: cattura la camera CORRENTE e per i primi
    // tick del nuovo path fonde (smoothstep) la vista catturata con quella del
    // path, evitando il teletrasporto al cambio. Chiamato dai Departure quando
    // l'ALTRO path era attivo al click.
    void beginPathHandoff();

    void zoomCamera(float delta);
    void addCameraRotation(float dYaw, float dPitch);
    // Inquadratura e orientamento neutri (salvavita sulle distanze 3D e 4D),
    // FOV 45: la vista di partenza del reset di scena.
    void resetTransformations();

    // LE DUE META' DELLA POSA, che il tasto RESET rimette separatamente.
    // Inquadratura: camera 3D (la posizione e' anche lo zoom: rotella e tasti
    // Forward/Backward la spostano), osservatore 4D e quota della sezione.
    // Orientamento: cio' che muovono le rotazioni (quaternione dell'oggetto e
    // angoli 4D). Il FOV non e' qui: lo governa MainWindow::applyCameraFov.
    struct Framing {
        QVector3D cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        QVector4D observerPos = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);
        float crossSectionP = 0.0f;
    };
    struct Orientation {
        QQuaternion rotation;
        // Mai zero secco: offset contro lo z-fighting in 4D. Deve combaciare col
        // valore usato all'avvio e nel guard di onStartClicked (0.0001): un 0.1
        // introdurrebbe una rotazione XW/YW/ZW reale che, proiettata 4D->3D,
        // deforma in modo asimmetrico le superfici con componente P.
        float omega = 0.0001f, phi = 0.0001f, psi = 0.0001f;
        // Il quaternione e' voluto (record, mouse): il primo Departure non lo
        // neutralizza (neutralizeDefaultRotationForPath).
        bool userRotated = false;
    };
    Framing framing() const;
    Orientation orientation() const;
    // Rimettono una posa. resetFraming torna anche alla vista libera (fuori
    // dalla modalita' path, pan e memorie per modalita' azzerati): un path in
    // corsa si riprende la camera al suo tick.
    void resetFraming(const Framing &f);
    void resetOrientation(const Orientation &o);
    void virtualMove(MoveDir dir, float speed3D, float speed4D);

    QVector3D getCameraPos() const { return m_cameraPos; }
    float getCameraYaw() const { return m_cameraYaw; }
    float getCameraPitch() const { return m_cameraPitch; }
    float getCameraRoll() const { return m_cameraRoll; }

    // YAW/PITCH EFFETTIVI, cioe' quelli che descrivono DAVVERO la direzione di
    // vista in questo istante.
    //
    // Servono al SALVATAGGIO. In modalita' path la vista non passa da
    // m_cameraYaw/m_cameraPitch: paintGL usa lookAt(m_cameraPos, m_pathTarget),
    // e quei due campi restano al valore che avevano prima -- tipicamente 0.
    // Salvare i campi grezzi produceva quindi un record con la posizione giusta
    // e la direzione sbagliata: al ricaricamento, con il path non riavviato, la
    // vista ricadeva su front=(0,0,-1) e il soggetto usciva dall'inquadratura.
    // MISURATO sul record "Calabi-Yau Orbit": camera in (2.19, 0.20, 1.89) con
    // yaw=pitch=0 -- 130.8 gradi di scarto dalla direzione che inquadra
    // l'origine, soggetto spostato a sinistra e in parte fuori campo.
    //
    // ATTENZIONE: m_isPathFollowing e' la MODALITA', non "il path sta
    // animando", e PERSISTE dopo lo stop del path (la spegne solo
    // resetTransformations). E' proprio questo a rendere il caso frequente:
    // basta aver usato un path, fermarlo, avviare le rotazioni e salvare.
    float getEffectiveCameraYaw() const;
    float getEffectiveCameraPitch() const;
    void setCameraPos(const QVector3D& pos) { m_cameraPos = pos; meshNeedsUpdate = true; update(); }
    void setCameraYaw(float y) { m_cameraYaw = y; meshNeedsUpdate = true; update(); }
    void setCameraPitch(float p) { m_cameraPitch = p; meshNeedsUpdate = true; update(); }
    void setCameraRoll(float r) { m_cameraRoll = r; meshNeedsUpdate = true; update(); }
    // Attiva/disattiva la soppressione del watchdog di performance durante
    // l'esportazione video (vedi m_isRecording). Chiamato dal VideoRecorder.
    // Al ritorno live (on=false) riarma lo STATO DI MISURA del watchdog per non
    // scambiare il transitorio di ripristino export per un rallentamento reale.
    void setRecordingActive(bool on);
    QQuaternion getRotationQuat() const { return m_rotationQuat; }
    // setRotationQuat imposta la rotazione "di default" (load preset / stato iniziale):
    // azzera m_userRotatedManually perche' questa NON e' una rotazione dell'utente.
    // Un quaternione nullo (rot_w = rot_x = rot_y = rot_z = 0 nel camera3D di un
    // file: il Twisted Fiber Tube) vale l'identita': moltiplicato da ogni
    // trascinamento restava nullo e l'oggetto non ruotava piu', si poteva solo
    // zoomare. Gli altri restano come sono (i Save devono riscriverli uguali).
    void setRotationQuat(const QQuaternion& q) {
        m_rotationQuat = (q.lengthSquared() > 1e-12f) ? q : QQuaternion();
        m_userRotatedManually = false; meshNeedsUpdate = true; update();
    }
    // All'avvio di un path: se l'orientamento corrente e' quello di default del
    // preset/avvio (l'utente non ha ruotato a mano), lo riporta a neutro; se invece
    // l'utente ha ruotato col mouse, lo conserva.
    void neutralizeDefaultRotationForPath() {
        if (!m_userRotatedManually) { m_rotationQuat = QQuaternion(); meshNeedsUpdate = true; update(); }
    }
    // Marcato dalla rotazione manuale (mouse/touch) via InputHandler.
    void markUserRotated() { m_userRotatedManually = true; }
    float getObserverPos4D() const { return m_observerPos.w(); }
    // Posizione COMPLETA dell'osservatore 4D. I tasti X/Y/Z/P del dock 4D la
    // scrivono in modalita' parametrica (in Ray Marching X/Y/Z sono spenti e P
    // muove invece la quota della sezione, m_crossSectionP): serve al readout
    // numerico del dock, che deve mostrare il valore giusto in ENTRAMBI i modi.
    QVector4D observerPos() const { return m_observerPos; }
    void setObserverPos4D(float pos) { m_observerPos.setW(pos); m_cameraPos4D.setW(pos); meshNeedsUpdate = true; update(); }


    // ==========================================================
    // ANIMATION & MOTION CONTROL
    // ==========================================================
    void addObjectRotation(float dPrecession, float dNutation, float dSpin);
    // Avanza rotazioni oggetto (prec/nut/spin) e 4D (omega/phi/psi) di
    // dtSeconds di tempo d'animazione, alle velocita' correnti. Unica
    // implementazione: la usa il tick live (updateRotation, dt=16ms) e il
    // loop di registrazione (dt del frame virtuale) — niente copie della
    // cinematica nel recorder (vedi CLAUDE.md).
    void advanceRotationsBy(float dtSeconds);
    // Clock esterno (registrazione): con il flag attivo i tick live NON
    // avanzano i moti — li avanza il loop del recorder col tempo virtuale del
    // frame. Lo STATO pero' resta vero (rotationTimer attivo, isAnimating()
    // sincero), cosi' bottoni, mutua esclusivita' e handler dei moti
    // funzionano normalmente anche durante il REC.
    void setExternalClockActive(bool on) { m_externalClockActive = on; }
    void setNutationSpeed(float v) { nutationSpeed = v; }
    void setPrecessionSpeed(float v) { precessionSpeed = v; }
    void setSpinSpeed(float v) { spinSpeed = v; }
    void setOmegaSpeed(float v) { omegaSpeed = v; }
    void setPhiSpeed(float v) { phiSpeed = v; }
    void setPsiSpeed(float v) { psiSpeed = v; }

    float getNutationSpeed() const { return nutationSpeed; }
    float getPrecessionSpeed() const { return precessionSpeed; }
    float getSpinSpeed() const { return spinSpeed; }
    float getOmegaSpeed() const { return omegaSpeed; }
    float getPhiSpeed() const { return phiSpeed; }
    float getPsiSpeed() const { return psiSpeed; }

    bool isAnimating() const { return rotationTimer && rotationTimer->isActive(); }
    void pauseMotion();
    void resumeMotion();

    // "Il path sta ANIMANDO". Distinto da m_isPathFollowing, che e' la MODALITA'
    // CAMERA (tangent vs center, render() riga ~298) e deve restare attiva anche
    // dopo lo stop perche' l'utente non perda l'orientamento tangente. Questo flag
    // serve SOLO al watchdog di performance per sapere se c'e' rendering continuo;
    // MainWindow lo spegne allo stop del path. Vedi render().
    void setPathAnimating(bool animating) {
        m_pathAnimating = animating;
        // Stop del path a handoff in corso: il blend va annullato, altrimenti
        // un Departure successivo SENZA subentro riprenderebbe la scivolata
        // da una vista catturata ormai stantia.
        if (!animating) m_pathHandoffActive = false;
    }
    bool isPathAnimating() const { return m_pathAnimating; }
    // La MODALITA' path (camera guidata da un path, anche fermo): vedi
    // m_isPathFollowing.
    bool isPathFollowing() const { return m_isPathFollowing; }

    void startAnimationTimer();
    void stopAnimationTimer();
    void stopAllTimers();
    void resetTime();
    // Azzera il SOLO orologio della texture: quello di SUPERFICIE (colore +
    // displacement, che nello shader leggono lo stesso clock) o quello dello
    // SFONDO, che e' separato. Serve al riclic in Library sulla texture gia'
    // attiva: come per superfici, suoni e record, ricliccare riavvia il preset
    // dall'inizio. resetTime() non va bene, azzererebbe TUTTI gli orologi --
    // geometria compresa -- che quel gesto non riguarda.
    void resetTextureTime(bool background = false);
    // Tutti gli orologi all'origine, fasce comprese: e' il "ricomincia da
    // t = 0" del tasto RESET. resetTime() lascia le fasce ai loro reset (load
    // del record, riclic della texture). I flag di moto non si toccano: cio'
    // che gira riparte dall'inizio, cio' che e' fermo resta fermo a t = 0.
    void resetAllClocks();
    void setSurfaceAnimating(bool animating);
    bool isSurfaceAnimating() const { return m_surfaceAnimating; }

    void setBackgroundTextureAnimating(bool animating);
    void setSurfaceTextureAnimating(bool animating);
    bool isBackgroundTextureAnimating() const { return m_bgAnimating; }
    bool isSurfaceTextureAnimating() const { return m_texAnimating; }

    // OROLOGI DI ANIMAZIONE (geometria, texture, sfondo, texture per-mesh).
    // Ognuno E' il tempo che il suo modulo mostra, e avanza solo col proprio
    // flag. Unica implementazione dell'avanzamento, condivisa fra il tick live
    // (dt reale, dal render) e il loop di registrazione (dt del frame
    // virtuale): stesso contratto di advanceRotationsBy (vedi CLAUDE.md).
    void advanceClocksBy(float dt);
    // In registrazione gli orologi li avanza SOLO il recorder, con
    // advanceClocksBy: il render non aggiunge il dt reale. Spento a fine REC,
    // lo schermo prosegue da dove il video e' arrivato.
    void setClocksDrivenByRecorder(bool on) { m_clocksDrivenByRecorder = on; }
    // Tempo che arriva allo shader per ciascun modulo globale (il render li
    // legge da qui; servono anche al test degli orologi).
    struct ClockTimes { float geom; float tex; float bg; };
    ClockTimes clockTimes() const;
    // Mai t = 0 esatto allo shader: e' l'origine storica degli orologi (la
    // vecchia base m_manualTime partiva da qui), e uno shader che divide per t
    // non deve vedere un infinito al primo frame.
    static constexpr float kClockOrigin = 0.00001f;


    // ==========================================================
    // UTILITIES
    // ==========================================================
    int projectionMode = 0;
    // FOV verticale (gradi) dell'obiettivo 3D (slider Fov del dock Path 3D).
    // Letto in render() -> vale identico per tick live e registrazione.
    void setCameraFov(float deg);
    float cameraFov() const { return m_cameraFov; }
    QImage getFrameForVideo(int targetW = -1, int targetH = -1, bool useFbo = false);
    // Cattura a risoluzione PIENA (vero "FBO"): fissa la dimensione in pixel del
    // color buffer offscreen di QRhiWidget alla risoluzione di export, così la
    // scena viene renderizzata NATIVAMENTE a quella risoluzione e grabFramebuffer()
    // restituisce pixel nitidi senza upscale (fix wireframe sfocato nei video).
    // begin va chiamata una volta prima del loop di registrazione, end alla fine.
    void beginHiResCapture(int w, int h);
    void endHiResCapture();
    QString getShaderError() const { return m_lastCompilationError; }
    bool validateAndApplyParametricShader(const QString &customLogic);
    bool validateAndApplyImplicitShader(const QString &eqF, const QString &texCode, const QString &dispCode,
                                        bool useCrossSection = false);
      bool validateAndApplyImplicitScript(const QString &scriptCodeGLSL);
    bool validateAndApplyBackgroundShader(const QString &scriptCode);
    bool validateAndApplyParametricScript(const QString &scriptCodeGLSL);


signals:
    void rotationChanged();
    // L'utente ha toccato/cliccato la scena in quel punto (coordinate locali al
    // widget). Serve a MainWindow per chiudere il messaggio in sovrimpressione
    // quando il tocco cade dentro il suo riquadro: su iOS il gesto passa TUTTO
    // da handleTouch, che consuma l'evento e ritorna true, quindi ne' la QLabel
    // ne' un eventFilter sul widget vedono mai un evento di mouse.
    void scenePressedAt(const QPoint &pos);
    // Il fragment shader non compila e le pipeline sono state azzerate: a schermo
    // la superficie SPARISCE. Senza questo segnale l'unica traccia era un
    // qWarning sulla console, invisibile all'utente, che vedeva solo il vuoto.
    // Il caso tipico sono due texture per-mesh i cui script dichiarano lo stesso
    // simbolo (vedi la rinomina in buildTextureFunction): il generatore copre le
    // forme note, ma uno script NUOVO puo' sempre introdurne una non prevista.
    void shaderCompilationFailed(const QString &error);
    // Un'immagine di texture non si e' potuta CARICARE: il file esiste ed e'
    // leggibile (chi chiama lo verifica prima), ma Qt non ne ricava pixel —
    // formato non supportato o file corrotto. Senza questo segnale
    // loadTextureFromFile usciva in SILENZIO, lasciando a schermo la texture
    // precedente: all'utente sembrava che il preset avesse una texture
    // "sbagliata" o "di default", senza alcun indizio del perche'.
    void textureImageLoadFailed(const QString &path);
    // Emesso dopo ogni rigenerazione della griglia, quando le parti di mesh
    // possono essere cambiate (nuovo script, nuove sezioni //MESH_BEGIN).
    // MainWindow lo usa per riallineare il selettore di mesh del dock renderer e
    // per riversare l'aspetto per-mesh di un preset appena caricato. E' un
    // segnale e non una chiamata diretta perche' updateSurfaceData() ha otto
    // chiamanti sparsi (script, equazioni, cambio tab, load...): agganciarsi al
    // punto in cui le parti NASCONO copre tutti i percorsi, incluso quello degli
    // script, che non passa da checkAndTriggerMeshUpdate.
    void meshPartsChanged();
    // borderAvailability() e' cambiata, o il bordo si e' acceso o spento: il
    // dock riallinea il radio Border (MainWindow::updateBorderControlsGate).
    // Puo' partire da render(): va collegato in coda (Qt::QueuedConnection).
    void borderAvailabilityChanged();
    // Emesso quando il throughput di rendering resta basso (animazione che
    // rallenta sensibilmente) abbastanza a lungo da segnalare un carico GPU
    // eccessivo. MainWindow lo intercetta per avvisare l'utente. Vedi la logica
    // di misura intervallo-frame in render().
    void performanceWarning();
    // L'UTENTE ha mosso la scena col mouse: trascinamento (rilascio) o rotella.
    // Distinto da rotationChanged(), che viene emesso a ogni frame anche dal moto
    // AUTOMATICO (advanceRotationsBy) e quindi non distingue chi ha mosso cosa.
    // MainWindow lo usa per marcare il lavoro non salvato.
    void userMovedView();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

public slots:
    void rebuildBackgroundShader(bool isTextureMode, const QString &customCode = "");

    // ==========================================================
    // GEODESIC FLOW CALCULATIONS
    // ==========================================================

    void setConstants(const QMap<QString, float>& constants) { m_constants = constants; }

private slots:
    void updateRotation();

private:
    // ==========================================================
    // CORE ARCHITECTURE
    // ==========================================================
    std::unique_ptr<SurfaceEngine> engine;
    std::unique_ptr<InputHandler> m_inputHandler;


    // ==========================================================
    // ENGINE STATE & REFACTORING
    // ==========================================================
    EngineMode m_engineMode = ModeParametric;

    // Campo implicito "mal condizionato": equazioni a PRODOTTO di piu' fattori
    // (es. preset "Chain" = -(f1*f2*...*f6)) hanno un range dinamico enorme
    // (~1e9) e cambiano segno per PARITA' del prodotto anche lontano da ogni
    // superficie reale. Il ramo trasparente (marchNextLayer) rileva le facce col
    // cambio di segno GREZZO del campo, quindi aggancia questi crossing FANTASMA
    // e il compositing esce con alpha~0 -> la superficie SPARISCE con alpha<1.
    // Il ramo OPACO usa d=val/gradLen e non ne soffre. Quando questo flag e' true
    // forziamo alpha=1.0 (fallback a opaco) e la UI disabilita lo slider alpha.
    // Rilevato empiricamente campionando il campo su CPU in setImplicitEquation:
    // NON e' un'euristica sulla stringa (un legittimo (x)*(y) non lo attiva).
    bool m_implicitIllConditioned = false;
    // SOLO ANDROID: avviso (non blocco) per la trasparenza che potrebbe degradare.
    // Sempre false su desktop/iOS. Vedi implicitTransparencyMayDegrade().
    bool m_implicitTransparencyWarn = false;
    void detectImplicitConditioning(const QString &eqF);

    // ==========================================================
    // TEXTURE
    // ==========================================================
    QRhiTexture *m_surfaceTexture = nullptr;
    // Percorso dell'immagine dell'utente in m_surfaceTexture (loadTextureFromFile);
    // vuoto se non ce n'e'. NON basta guardare m_surfaceTexture != nullptr:
    // MainWindow::generateTexture() ci mette anche la scacchiera di default,
    // disegnata su CPU, che e' un fallback e non un'immagine. Non vuoto governa
    // u_noImage, cioe' se gli script che campionano iChannel0 cadono sulla
    // scacchiera procedurale (colori vivi da u_col1/u_col2) o leggono la
    // texture. Era un bool: il percorso dice anche QUALE immagine e' a schermo
    // (vedi surfaceImagePath).
    QString m_surfaceImagePath;

    // IMMAGINI PROPRIE DELLE FASCE (multi-mesh, ambito Mesh). L'immagine di una
    // fascia e' quella nominata dal tag //IMG: nel suo script (textureCode),
    // come per la texture di superficie. Ogni fascia e' gia' una draw call col
    // suo blocco UBO: per darle un'immagine sua basta legarle, allo slot 1, la
    // sua texture al posto di quella della superficie -- stesso layout, nessun
    // campo nuovo nel blocco uniforme, nessuna pipeline in piu'.
    // Una texture per PERCORSO: la stessa immagine su piu' fasce si carica una
    // volta sola. Si creano e si distruggono nel frame (syncPartImages).
    struct PartImage {
        QRhiTexture *tex = nullptr;
        QRhiShaderResourceBindings *srb = nullptr;
        bool failed = false;   // file illeggibile: non si riprova a ogni frame
    };
    QHash<QString, PartImage> m_partImages;
    void syncPartImages(QRhiResourceUpdateBatch *resourceUpdates);
    void rebindPartImages();
    void releasePartImages();
    // La texture propria della fascia, se ne ha una caricata e l'aspetto
    // per-fascia e' in vigore (ambito Mesh); altrimenti nullptr.
    const PartImage *partImageFor(const MeshPart &p) const;
    // I binding con cui disegnare la fascia: i suoi, o quelli della superficie.
    QRhiShaderResourceBindings *bindingsForPart(const MeshPart &p) const;
    QImage m_pendingSurfaceImage;
    bool m_surfaceTextureNeedsUpload = false;
    // Richiesta di scarico della texture di superficie dalla GPU: la distruzione
    // dell'oggetto RHI va fatta DENTRO il render loop (non sul thread GUI), come
    // per l'upload. Senza questo, una texture caricata resta residente e ricompare
    // stantia cambiando superficie o riaccendendo il checkbox.
    bool m_surfaceTextureNeedsClear = false;

    QRhiTexture *m_backgroundTexture = nullptr;
    QImage m_pendingBackgroundImage;
    bool m_backgroundTextureNeedsUpload = false;
    QRhiBuffer *m_bgVbo = nullptr;
    QRhiGraphicsPipeline *m_bgPipeline = nullptr;
    QRhiShaderResourceBindings *m_bgBindings = nullptr;
    bool m_bgVboUploaded = false;
    QString m_customFragmentCode;
    QString m_bgScriptCode;

    QString m_textureCode;
    QString m_displacementCode;


    // ==========================================================
    // RISORSE QRHI
    // ==========================================================
    QRhiBuffer *m_vbo = nullptr;
    QRhiBuffer *m_ibo = nullptr;
    QRhiBuffer *m_ubo = nullptr;

    // Avviso di shader non compilato gia' emesso: impedisce la RAFFICA di popup.
    // buildPipeline puo' fallire a ogni frame, e il popup desktop e' modale
    // (event loop annidato), quindi ogni segnale in coda ne aprirebbe un altro
    // sopra il precedente. Si riarma appena uno shader torna valido.
    bool m_shaderErrorReported = false;
    QRhiTexture *m_dummyTexture = nullptr;
    QRhiSampler *m_sampler = nullptr;

    QRhiShaderResourceBindings *m_bindings = nullptr;

    QRhiBuffer *m_wireframeIbo = nullptr;
    // Bordo: buffer a parte, riempiti insieme alla mesh (vedi
    // SurfaceEngine::generateBorder).
    QRhiBuffer *m_borderVbo = nullptr;
    QRhiBuffer *m_borderIbo = nullptr;
    QRhiBuffer *m_borderWireIbo = nullptr;
    bool m_borderUploaded = false;
    QRhiGraphicsPipeline *m_wireframePipeline = nullptr;
    std::vector<unsigned int> m_wireframeIndices;
    bool wireframeNeedsUpdate = true;
    int m_wireframeIndexCount = 0;
    // Un intervallo per parte di mesh: il wireframe condivide il VBO della
    // superficie (indici assoluti) ma gli uniform del dominio sono per-parte.
    std::vector<WireframeRange> m_wireframeRanges;

    QRhiGraphicsPipeline *m_pipelineOpaque = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspBack = nullptr;
    QRhiGraphicsPipeline *m_pipelineTranspFront = nullptr;
    // BORDO: pipeline proprie, opaca e a linee, con lo stato di m_pipelineOpaque
    // e m_wireframePipeline ma col fragment del bordo (HAS_BORDER): solo lui
    // contiene il discard dei tratti coperti. Nel fragment della superficie un
    // discard, anche mai eseguito, spegne lo scarto anticipato dei frammenti
    // nascosti sulle GPU a tessere (HSR di Apple, FPK di Mali, LRZ di Adreno):
    // gli strati opachi coperti si colorano lo stesso. Nulle col bordo spento;
    // se il loro fragment non compila, il bordo si disegna con le pipeline della
    // superficie, senza il taglio dei tratti coperti.
    QRhiGraphicsPipeline *m_borderPipelineOpaque = nullptr;
    QRhiGraphicsPipeline *m_borderPipelineWire = nullptr;

    QRhiBuffer *m_bgUbo = nullptr;

    UboData m_uboData;

    // ==========================================================
    // MULTI-MESH: UBO ad array + dynamic offset
    // ==========================================================
    // I limiti u_min/u_max/v_min/v_max e u_meshIndex sono per-parte, ma sono
    // uniform: servono N blocchi nello stesso buffer, uno per parte, e ogni draw
    // call ne seleziona uno con un dynamic offset. Alternativa scartata: N
    // updateDynamicBuffer sullo stesso blocco, che serializzerebbe i draw.
    //
    // m_uboBlockStride e' allineato con QRhi::ubufAlignment(): NON e'
    // sizeof(UboData) (su Metal/Vulkan l'allineamento minimo puo' essere 256).
    quint32 m_uboBlockStride = 0;
    int m_uboBlockCapacity = 0;   // quante parti entrano nel buffer attuale
    // Binding con dynamic offset per la superficie multi-parte. Restano separati
    // da m_bindings: quello serve ai percorsi che disegnano a offset 0 (sfondo
    // flat, ray marching) e non deve cambiare comportamento.
    QRhiShaderResourceBindings *m_bindingsDyn = nullptr;
    QRhiTexture *m_bindingsDynTexture = nullptr;  // texture con cui e' stato creato
    void ensureDynamicBindings(QRhiTexture *tex);
    bool ensureUboCapacity(int partCount);

    // SEZIONE TRASPORTATA DEI TUBI. Una passata compute a ogni frame, prima
    // del disegno, distribuisce gli anelli lungo la curva PROIETTATA (fitti
    // dove la tangente gira in fretta), porta la sezione da un anello al
    // successivo (trasporto parallelo) e scrive in m_tubeFrameTex, due texel
    // per anello e una riga per tubo, sezione + u dell'anello e tangente; il
    // vertex li legge (slot 2, tubeTransportedRing in surface.vert). Il compute
    // e' il vertex stesso ridotto a libreria di funzioni, quindi curva,
    // rotazioni e proiezione sono le stesse del disegno. Senza compute (o
    // RGBA32F in load/store) la texture non esiste e restano gli anelli
    // uniformi in u con l'orientamento locale di sempre.
    static constexpr int kTubeFrameMaxRings = 1024;   // = TUBE_MAX_RINGS nel compute
    static constexpr int kTubeFrameMaxParts = 64;
    // Campioni fitti della curva per tubo, per distribuire gli anelli (=
    // TUBE_MAX_SAMPLES nel compute: al piu' 2048 intervalli).
    static constexpr int kTubeFrameMaxSamples = 2049;
    QRhiTexture *m_tubeFrameTex = nullptr;
    QRhiSampler *m_tubeFrameSampler = nullptr;
    // Lavoro del compute e stato fra un frame e l'altro: per ogni anello
    // tangente, direzione locale e sezione mostrata al frame prima; per ogni
    // tubo partenza e angolo di chiusura; in coda i campioni fitti di ogni
    // tubo (tangente e svolta cumulata). Lo stato si azzera quando
    // la pipeline viene rifatta (curva nuova) o cambiano tubi e anelli
    // (m_tubeFrameLayout): m_tubeFrameStateReset, consumato al frame dopo.
    QRhiBuffer *m_tubeFrameWork = nullptr;
    bool m_tubeFrameStateReset = true;
    quint64 m_tubeFrameLayout = 0;
    // Passo dell'ultimo frame, da advanceClocksBy (live o REC): da qui lo
    // smorzamento degli scatti della sezione (u_tubeFrameBlend).
    float m_tubeFrameDt = 0.0f;
    static constexpr int kTubeFrameWorkVec4 = kTubeFrameMaxParts * kTubeFrameMaxRings * 3 + kTubeFrameMaxParts * 2
                                              + kTubeFrameMaxParts * kTubeFrameMaxSamples;
    QRhiShaderResourceBindings *m_tubeFrameSrb = nullptr;
    QRhiComputePipeline *m_tubeFramePipeline = nullptr;
    void createTubeFrameResources();
    void buildTubeFramePipeline(const QString &vertexSource);
    void releaseTubeFramePipeline();
    // La passata gira in questo frame: tubo a schermo e pipeline pronta.
    bool tubeFramesActive() const;
    // Lo slot 2 di ogni binding della superficie: la texture delle direzioni,
    // o la dummy dove non c'e' (il layout deve essere lo stesso per tutte).
    QRhiShaderResourceBinding tubeFramesBinding() const;

    // LATI DELLA MESH CUSTOM (flusso geodetico) PER IL BORDO. I vertici della
    // mesh custom sono posizioni, non parametri: lo shader non sa calcolare il
    // punto in (u, v) da cui il bordo costruisce i suoi tubi. setCustomMesh
    // copia allora i quattro lati della griglia, ciascuno con la riga accanto
    // verso l'interno, in m_borderEdgeTex (RGBA32F, un texel per punto, righe
    // in kBorderEdgeRows; nell'ultima, texel 0, numU e numV della griglia), e il
    // vertex li legge (slot 3, borderRawPoint in surface.vert). Senza RGBA32F la
    // texture non esiste e il flusso geodetico resta senza bordo.
    static constexpr int kBorderEdgeMaxSamples = 1024;   // griglia geodetica: al piu' 501 punti per lato
    static constexpr int kBorderEdgeRows = 9;
    QRhiTexture *m_borderEdgeTex = nullptr;
    QRhiSampler *m_borderEdgeSampler = nullptr;
    std::vector<float> m_borderEdgeData;   // larghezza m_borderEdgeWidth, kBorderEdgeRows righe
    int m_borderEdgeWidth = 0;
    bool m_borderEdgeDirty = false;
    void createBorderEdgeResources();
    void setBorderEdges(const QVector<QVector<QVector4D>> &grid, const std::vector<char> &drawnQuad);
    // Il bordo si puo' disegnare sulla mesh corrente: sempre sulle superfici,
    // sulla mesh custom solo se c'e' la texture dei lati.
    bool borderSupported() const { return !m_isCustomMesh || m_borderEdgeTex; }
    // Lo slot 3 di ogni binding della superficie: i lati della mesh custom, o
    // la dummy.
    QRhiShaderResourceBinding borderEdgesBinding() const;

    // LATI DEL BORDO COPERTI. Un tratto di lato non e' un bordo se un'altra
    // parte della superficie ci passa sopra e prosegue in un'altra direzione:
    // le cuciture, le giunture fra le patch del Calabi-Yau, i lati che si
    // ripiegano (Oloid) o che avvolgono piu' volte la stessa curva (Wound
    // Sphere), i poli, e i tratti di orlo che passano DENTRO un'altra mesh (le
    // fasce dell'Octahedron Bands dove si incrociano). La CPU non sa valutare
    // gli script, e nel vertex ogni punto del bordo dovrebbe confrontarsi con
    // tutta la superficie: una passata compute in quattro tempi
    // (buildSharedEdgePipeline) campiona i lati e una griglia dell'interno di
    // ogni parte in m_sharedEdgeSamples, racchiude blocchi di celle in sfere
    // (m_sharedEdgeTiles), decide per ogni campione dei lati se e' coperto
    // (m_sharedEdgeRaw) e, ripuliti i tratti corti, lo scrive in
    // m_sharedEdgeTex (colonna = campione, riga = parte * 4 + lato). Il vertex nasconde i lati coperti per intero e, finche' la
    // passata e' attiva, non rifa' il suo test delle cuciture; il fragment
    // scarta i tratti coperti (slot 4, borderEdgeShared e borderFragmentCovered
    // negli shader). Come i tubi, il compute e' il vertex stesso ridotto a
    // libreria, con `mesh` dalla tabella delle parti (m_sharedEdgeParts:
    // dominio e indice di ciascuna). Si costruisce al primo frame col bordo
    // acceso o alla sonda, e gira solo quando la geometria cambia
    // (m_sharedEdgeKey: costanti, tempo, parti; le rotazioni no). Le quattro
    // passate leggono la fase da blocchi UBO propri, dopo quelli del bordo
    // (u_isBorder = 10, 11, 12, 13; quattro per istante della sonda).
    // Senza compute la texture non esiste e resta il test del vertex
    // (cuciture diritte e ritorte, poli).
    // Le mesh custom (flusso geodetico) fanno il test sulla CPU, per lato
    // intero, sulla mesh disegnata: vedi setBorderEdges.
    static constexpr int kSharedEdgeMaxParts = 64;
    static constexpr int kSharedEdgeSamples = 256;    // intervalli per lato (= EDGE_SAMPLES negli shader)
    static constexpr int kSharedEdgeGrid = 64;        // celle per lato del dominio (= GRID)
    static constexpr int kSharedEdgeTileCells = 8;    // celle per lato di un blocco (= TILE)
    static constexpr int kSharedEdgeTiles = (kSharedEdgeGrid / kSharedEdgeTileCells) * (kSharedEdgeGrid / kSharedEdgeTileCells);
    static constexpr int kSharedEdgePartVec4 = 8 * (kSharedEdgeSamples + 1) + (kSharedEdgeGrid + 1) * (kSharedEdgeGrid + 1);
    QRhiTexture *m_sharedEdgeTex = nullptr;
    QRhiSampler *m_sharedEdgeSampler = nullptr;
    QRhiBuffer *m_sharedEdgeParts = nullptr;
    QRhiBuffer *m_sharedEdgeSamples = nullptr;
    QRhiBuffer *m_sharedEdgeTiles = nullptr;
    // I flag grezzi della terza passata, uno per campione dei lati: la quarta
    // li ripulisce (tratti corti) e scrive m_sharedEdgeTex.
    QRhiBuffer *m_sharedEdgeRaw = nullptr;
    std::vector<QVector4D> m_sharedEdgeTable;   // l'ultima tabella caricata
    QByteArray m_sharedEdgeKey;                 // la geometria dell'ultima passata
    QRhiShaderResourceBindings *m_sharedEdgeSrb = nullptr;
    QRhiComputePipeline *m_sharedEdgePipeline = nullptr;
    // Il vertex da cui costruire il compute (vuoto se non si puo': tubo, mesh
    // custom, bordo spento) e se va ancora costruito.
    QString m_sharedEdgeSource;
    bool m_sharedEdgeSourceDirty = false;
    // Solo per gli scenari: spegne la passata, per confrontare col test del vertex.
    bool m_sharedEdgesOff = false;
    // Solo per gli scenari (border-audit): nel prossimo frame con la passata
    // attiva si rileggono dalla GPU i flag dei lati coperti (RGBA8,
    // kSharedEdgeSamples + 1 colonne, riga = mesh * 4 + lato) e i campioni
    // delle parti (kSharedEdgePartVec4 vec4 per parte). Ogni lettura finita
    // incrementa m_sharedEdgeReadbacks.
    bool m_sharedEdgeReadbackRequested = false;
    QRhiReadbackResult m_sharedEdgeFlagsResult;
    QRhiReadbackResult m_sharedEdgeSamplesResult;
    int m_sharedEdgeReadbacks = 0;
    // SONDA (vedi borderAvailability), a bordo spento e acceso: la geometria
    // (tabella delle parti, costanti, vincoli; il tempo no) dell'ultima sonda,
    // quella in attesa che si fermi (m_borderProbeTimer: caricamento, slider
    // trascinato) e, a timer scaduto, il via per il prossimo frame. La lettura
    // dei flag e' asincrona: finishBorderProbe la scarta se nel frattempo il
    // vertex e' cambiato (m_borderProbeSerial).
    QByteArray m_borderProbeKey;
    QByteArray m_borderProbePending;
    QTimer *m_borderProbeTimer = nullptr;
    bool m_borderProbeArmed = false;
    bool m_borderProbeReading = false;
    int m_borderProbeSerial = 0;
    int m_borderProbeReadSerial = 0;
    std::vector<int> m_borderProbeMeshes;   // indice di mesh di ogni parte, alla lettura
    // Su una superficie animata la sonda guarda piu' istanti di t in un colpo
    // solo (vedi render): letture e arrivi.
    static constexpr int kBorderProbeInstants = 8;
    static constexpr float kBorderProbeStep = 0.83f;   // passo in t, non una frazione tonda di pi
    QRhiReadbackResult m_borderProbeResults[kBorderProbeInstants];
    int m_borderProbeInstantsRead = 1;
    int m_borderProbeArrived = 0;
    // Superficie animata che si apre in qualche istante ma ora e' chiusa: si
    // guarda l'istante corrente ogni kBorderProbeWatchMs finche' non si apre,
    // poi Border resta acceso (scelta dell'utente, 10-17).
    static constexpr int kBorderProbeWatchMs = 250;
    bool m_borderProbeWatch = false;
    QElapsedTimer m_borderProbeWatchClock;
    BorderAvailability m_borderAvailability = BorderAvailability::Unknown;
    void finishBorderProbe();
    void setBorderAvailability(BorderAvailability a);
    void createSharedEdgeResources();
    void buildSharedEdgePipeline();
    void releaseSharedEdgePipeline();
    void setSharedEdgeBindings();
    // La passata si puo' usare in questo frame: parametrico (niente tubo ne'
    // mesh custom), pipeline pronta. Anche a bordo spento, come sonda.
    bool sharedEdgesActive();
    // Lo slot 4 di ogni binding della superficie: i lati coperti, o la dummy.
    QRhiShaderResourceBinding sharedEdgesBinding() const;
    // Il vertex ridotto a libreria per un compute: ingressi e uscite globali,
    // main rinominato in `mainName`, gl_Position sostituita da `position`.
    // Vuoto se manca #version.
    static QString vertexAsComputeLibrary(const QString &vertexSource, const QString &define,
                                          const QString &mainName, const QString &position);


    // ==========================================================
    // RISORSE QRHI IMPLICIT (RAY MARCHING)
    // ==========================================================
    void buildImplicitPipeline();
    QRhiGraphicsPipeline *m_pipelineImplicit = nullptr;
    QRhiShaderResourceBindings *m_bindingsImplicit = nullptr;


    // ==========================================================
    // MATHEMATICAL & GEOMETRY STATE
    // ==========================================================
    QString m_eqX, m_eqY, m_eqZ, m_eqW;
    bool meshNeedsUpdate = true;
    int m_indexCount = 0;
    int wfStepU = 4;
    int wfStepV = 4;
    int m_raySteps = 100;
    float m_surfaceScale = 2.0f;
    bool m_isCustomMesh = false;

    // DOMINIO DI RIFERIMENTO delle coordinate texture della mesh geodetica.
    // Le texCoord sono la frazione di QUESTO dominio, non dell'intervallo
    // corrente: e' cio' che tiene la texture ancorata alla geometria quando si
    // restringe l'intervallo (la si vede TAGLIATA, non compressa). Fissato al
    // primo calcolo dopo un load/reset (m_uvRefValid=false) e tenuto fermo
    // finche' non se ne carica un altro, cosi' il preset conserva l'aspetto con
    // cui e' stato salvato.
    bool  m_uvRefValid = false;
    float m_uvRefUMin = 0.0f, m_uvRefUSpan = 1.0f;
    float m_uvRefVMin = 0.0f, m_uvRefVSpan = 1.0f;

public:
    // Riarma il dominio di riferimento delle texCoord geodetiche: il prossimo
    // calcolo lo rifissa sull'intervallo corrente. La chiamano i percorsi che
    // stabiliscono un nuovo "stato di partenza" -- load di preset/record e
    // reset di modalita' -- MAI un semplice cambio di limiti, che e' proprio il
    // caso in cui il riferimento deve restare fermo.
    void resetGeodesicUvReference() { m_uvRefValid = false; }

private:


    // ==========================================================
    // IMPLICIT EQUATIONS STATE
    // ==========================================================
    QString m_eqImplicitF = "x*x + y*y + z*z - 1.0";
    // Equazione a 4 variabili (x,y,z,p) del sotto-tab Cross Section. Stato
    // separato da m_eqImplicitF: i due sotto-tab (3D / Cross Section) non si
    // sovrascrivono a vicenda. p e' la quarta coordinata del punto nel
    // riferimento della superficie, ricavata applicando la rotazione 4D INVERSA
    // al punto (x,y,z,0) della camera — vedi createImplicitFragmentShader().
    // Default: 3-toro T^3 (toro-di-tori, S=x^2+y^2+z^2+A^2, M=S+p^2+B^2-C^2),
    // A=raggio esterno, B=raggio intermedio, C=raggio tubo. A p=0 NON si
    // riduce al toro 2D standard: e' proprio l'oggetto 4D nel suo riferimento.
    // DEVE combaciare con kT3Equation in MainWindow::loadCrossSectionDefaultSurface
    // (fattore di scala compreso): sono due copie della stessa equazione, e una
    // che resta indietro e' un default incoerente con cio' che l'utente vede.
    QString m_eqCrossSectionF =
        "0.5*(((x*x+y*y+z*z+p*p+A*A+B*B-C*C)^2 + 4*(A*A-B*B)*(x*x+y*y) - 4*B*B*(z*z+A*A))^2 "
        "- 16*A*A*(x*x+y*y)*(x*x+y*y+z*z+p*p+A*A-B*B-C*C)^2)";
    // Quale delle due equazioni sopra e' quella attiva nello shader compilato.
    bool m_implicitUsesCrossSection = false;

    QString createImplicitFragmentShader();
    QString createBackgroundFragmentShader(bool isTextureMode, const QString &customCode);


    // ==========================================================
    // GEODESIC FLOW STATE
    // ==========================================================
    // Il flusso geodetico si calcola in geodesiccalculator + compute shader:
    // qui resta solo la mappa delle costanti. (Undici campi relitto di una
    // vecchia versione rimossi in 2026-08: nessuno li leggeva piu'.)
    QMap<QString, float> m_constants;


    // ==========================================================
    // RENDERING & TEXTURE STATE
    // ==========================================================
    int renderMode = 0;
    // Indice della mesh su cui agiscono i controlli di aspetto; -1 = "All"
    // (stato globale). Vedi setActiveMeshPart.
    int m_activeMeshPart = -1;
    bool m_meshAppearanceBypass = false;
    // Vero in ambito "All": l'aspetto per-parte e' SOSPESO (ignorato dal render,
    // non cancellato), cosi' la superficie si disegna come una sola e i valori
    // per-mesh restano disponibili per quando si torna su "Mesh".
    bool m_meshAppearanceUniform = false;
    bool applyToActiveMeshPart(const std::function<void(MeshPart&)> &fn);
    // Modalita' con cui una parte va DISEGNATA: la sua se dichiarata, ma in
    // ambito "All" (uniform) vince sempre il globale. Punto unico, cosi' render
    // e partizione wireframe/solido non possono divergere.
    int effectivePartRenderMode(const MeshPart &p) const {
        return m_meshAppearanceUniform ? renderMode : p.effectiveRenderMode(renderMode);
    }
    // Sposta di 'delta' il passo wireframe della parte selezionata. Ritorna
    // false se non c'e' una parte attiva: il chiamante ricade sul globale.
    bool adjustActiveWireframeStep(bool isU, int delta);
    // True quando lo script ray marching definisce la direttiva Inner:=
    // (seconda superficie opaca interna, es. orizzonte di Kerr). Settato in
    // createImplicitFragmentShader() e inviato alla GPU via u_dummyZero.y.
    bool m_raymarchHasInner = false;
    bool m_textureEnabled = false;
    bool m_isSpecularEnabled = false;
    float alpha = 0.5f;
    float red = 1, green = 1, blue = 1;
    float m_lightIntensity = 1.0f;
    // 0 = spenta: il default riproduce esattamente l'illuminazione storica.
    float m_fillLight = 0.0f;
    // 0.005 = il guscio storico della sfera unitaria (dove |grad| = 2).
    float m_shellThickness = 0.005f;
    // MARCHER: false = sphere tracing storico. Default conservativo, cosi' ogni
    // superficie che non chiede esplicitamente l'ibrido si disegna come sempre
    // (i record 3D esistenti, che con l'ibrido mostravano difetti sui bordi).
    // Il T^3 del Cross Section lo accende da loadCrossSectionDefaultSurface, e i
    // preset lo portano nella chiave "hybridMarcher".
    bool m_hybridMarcher = false;
    float m_tubeRadius = 0.1f;
    float m_borderRadius = 0.0f;
    QColor m_borderColor = QColor::fromRgbF(1.0f, 1.0f, 1.0f);
    int m_borderRenderMode = 0;
    int m_borderWireAlong = kBorderWireAlongDefault;
    int m_borderWireAround = kBorderWireAroundDefault;
    QString m_borderTexCode;
    bool m_borderTexEnabled = false;
    QColor m_borderTexCol1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f);
    QColor m_borderTexCol2 = QColor(Qt::black);
    float m_borderTexZoom = 1.0f;
    QVector2D m_borderTexPan = QVector2D(0.0f, 0.0f);
    float m_borderTexRotation = 0.0f;
    QString m_borderTexLibName;
    QString m_borderTexHint;
    float m_borderTexHintSeconds = 6.0f;
    bool m_borderTexAnimating = false;
    float m_borderTimeTex = 0.0f;
    // Binding del bordo: quelli della sua immagine, se ne ha una caricata.
    QRhiShaderResourceBindings *borderBindings() const;

    float texRed1 = 1.0f, texGreen1 = 1.0f, texBlue1 = 1.0f;
    float texRed2 = 0.0f, texGreen2 = 0.0f, texBlue2 = 0.0f;

    bool m_useBackgroundTexture = false;
    int m_bgSkyMode = BgFixed;      // vedi setBackgroundSkyMode
    bool m_bgIsScript = false;
    QString m_bgImagePath;          // vedi backgroundImagePath
    QVector3D m_bgColor = QVector3D(0.3f, 0.3f, 0.3f);
    int m_lightingMode4D = 0;


    // ==========================================================
    // 2D / FLAT VIEW STATE
    // ==========================================================
    bool m_isFlatView = false;
    // true tra beginHiResCapture/endHiResCapture: il color buffer è già fissato
    // alla risoluzione di export, quindi getFrameForVideo NON deve riscalare.
    bool m_hiResCapture = false;
    // true durante l'esportazione video: il rendering e' frame-by-frame (con
    // cattura + scrittura su disco tra i frame), quindi gli intervalli sono
    // naturalmente lentissimi -> il watchdog di performance darebbe un FALSO
    // avviso "il rendering rallenta". Lo si disattiva mentre e' true.
    bool m_isRecording = false;
    int m_flatViewTarget = 0;
    // BUFFER DI LAVORO della vista 2D: e' cio' che il mouse muove e cio' che il
    // fragment legge per la parte in editing. NON e' la trasformazione globale:
    // vedi m_globalTexZoom/Pan/Rotation qui sotto.
    float m_flatZoom = 1.0f;
    float m_flatRotation = 0.0f;
    QVector2D m_flatPan;
    // TRASFORMAZIONE 2D GLOBALE della texture di superficie (ambito "All", e
    // valore ereditato dalle parti che non ne hanno una propria).
    // Vive separata dai tre membri qui sopra perche' quelli fanno DUE lavori:
    // sono il buffer che il mouse muove in vista 2D, e finivano anche in
    // m_uboData come stato globale. Manipolando la texture di UNA mesh li si
    // sovrascriveva, e la trasformazione globale -- insieme a quella di ogni
    // fascia che la eredita -- veniva trascinata dietro.
    float m_globalTexZoom = 1.0f;
    float m_globalTexRotation = 0.0f;
    QVector2D m_globalTexPan;
    // Inquadratura e colori della texture di SFONDO: unica, non per-parte.
    // Finche' nessuno scrive i colori lo sfondo usa quelli della superficie.
    float m_bgZoom = 1.0f;
    float m_bgRotation = 0.0f;
    QVector2D m_bgPan;
    QVector3D m_bgTexCol1, m_bgTexCol2;
    bool m_bgTexColorsSet = false;


    // ==========================================================
    // CAMERA & TRANSFORMATIONS STATE
    // ==========================================================
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
    QQuaternion m_rotationQuat;
    // true se l'utente ha ruotato la superficie a mano (mouse/touch) dopo l'ultima
    // rotazione "di default" (setRotationQuat). Governa neutralizeDefaultRotationForPath.
    bool m_userRotatedManually = false;

    QVector3D m_cameraPos;
    QVector4D m_cameraPos4D = QVector4D(0.0f, 0.0f, 4.0f, 4.0f);
    QVector4D m_observerPos = QVector4D(0.0f, 0.0f, 0.0f, 4.0f);
    // QUOTA DEL PIANO DI SEZIONE (sotto-tab Cross Section): il valore di p a cui
    // si taglia l'ipersuperficie. Stato PROPRIO e non m_observerPos.w, che e' la
    // DISTANZA dell'osservatore 4D per la proiezione parametrica e parte da 4.0:
    // usarla come quota farebbe nascere la sezione fuori dall'oggetto (schermo
    // vuoto). I tasti P+/P- del dock 4D scrivono questa.
    float m_crossSectionP = 0.0f;

    float m_cameraYaw;
    float m_cameraPitch;
    float m_cameraRoll = 0.0f;
    float m_cameraFov = 45.0f; // gradi, vedi setCameraFov()
    bool m_externalClockActive = false; // vedi setExternalClockActive()

    bool m_isPathFollowing = false;   // MODALITA' camera tangent (persiste dopo stop)
    bool m_pathAnimating = false;     // path in animazione (solo per il watchdog)
    QVector3D m_pathTarget;
    QVector3D m_pathUp;
    float m_pathRoll = 0.0f;

    // --- Handoff camera al cambio path 3D<->4D (vedi beginPathHandoff) ---
    float advancePathHandoff();      // avanza il blend, torna il fattore smoothstep
    bool m_pathHandoffActive = false;
    float m_pathHandoffK = 0.0f;      // 0..1, avanzato a ogni tick del nuovo path
    QVector3D m_handoffPos;
    QVector3D m_handoffTarget;
    QVector3D m_handoffUp;
    float m_handoffRoll = 0.0f;
    QVector4D m_handoffObserver;
    QVector4D m_handoffCam4D;


    // ==========================================================
    // ANIMATION & MOTION STATE
    // ==========================================================
    // Periodo del tick di rotationTimer: UNICA fonte, usata sia dal
    // setInterval sia dalla conversione tempo->tick di advanceRotationsBy.
    // Mai duplicarlo come letterale: se i due valori divergono, live e video
    // ruotano a velocita' diverse (vedi CLAUDE.md).
    static constexpr int kRotationTickMs = 16;
    QTimer* rotationTimer;
    QTimer* m_animTimer = nullptr;
    QElapsedTimer m_surfaceTimer;

    // --- Watchdog di performance (avviso da rallentamento) ---
    // Misura l'intervallo TRA frame consecutivi (non la durata di render(): in
    // QRhiWidget render() registra solo i comandi, la GPU li esegue dopo, quindi
    // il throughput reale si legge dal ritmo dei frame, non dall'encoding CPU).
    QElapsedTimer m_frameClock;          // delta dall'ultimo frame
    QElapsedTimer m_perfGraceClock;      // finestra di grazia dopo l'export: entro
                                         // kPerfGraceMs il watchdog scarta le misure
                                         // (il transitorio di ripristino, di durata
                                         // variabile su mobile, non e' carico GPU)
    float m_avgFrameMs = 16.0f;          // media mobile (EMA) dell'intervallo, ms
    float m_slowAccumMs = 0.0f;          // tempo accumulato sotto soglia
    int   m_slowFrameRun = 0;            // campioni lenti (dt > kSlowFrameMs) consecutivi:
                                         // con l'EMA asimmetrica (salita 0.35) un singolo
                                         // picco fin quasi a 2 s porta la media oltre
                                         // soglia da solo, quindi l'immunita' ai picchi
                                         // isolati la garantisce QUESTO contatore
                                         // (avviso solo con >= kSlowRunToWarn di fila)
    int   m_hugeFrameRun = 0;            // frame consecutivi > kHugeFrameMs (>2 s): un
                                         // frame isolato = buco da inattivita' (scartato),
                                         // una SEQUENZA = collasso GPU reale (es. iPad a
                                         // 0.2 fps con RM+trasparenza) -> va segnalata
    float m_perfWarnLevelMs = 0.0f;      // livello del primo avviso (0 = armato per la
                                         // scena corrente); riarmato solo da rebuildShader()
    bool m_perfWarnDismissed = false;    // gia' avvisato per QUESTA scena: sopprime ogni
                                         // ulteriore avviso finche' non cambiano le
                                         // impostazioni (riarmo SOLO in rebuildShader(),
                                         // NON su stop/riavvio moti ne' su oscillazione)
    bool m_wasAnimating = false;         // stato anim. al frame precedente: per saltare
                                         // il primo frame dopo lo start (transitorio)

    bool m_surfaceAnimating = false;

    float nutation = 0, precession = 0, spin = 0;
    float omega = 0, phi = 0, psi = 0;
    float nutationSpeed = 0, precessionSpeed = 0, spinSpeed = 0;
    float omegaSpeed = 0, phiSpeed = 0, psiSpeed = 0;

    QVector3D m_lastValidUp{0.0f, 1.0f, 0.0f};
    bool m_isFirstPathRun{true};

    float m_lastRealTime = 0.0f;
    // Orologi dei tre moduli globali: ciascuno E' il tempo mostrato (piu'
    // kClockOrigin). Nessuna base comune: prima c'era m_manualTime, sommata a
    // tutti, con due sorgenti incompatibili (elapsed reale allo stop, tempo del
    // recorder in REC). Nel video il tempo del recorder entrava due volte
    // (velocita' doppia, partenza da zero) e ogni stop/start dell'orologio
    // faceva saltare avanti le animazioni.
    float m_timeGeom = 0.0f;
    float m_timeTex = 0.0f;
    float m_timeBg = 0.0f;
    bool  m_clocksDrivenByRecorder = false;   // vedi setClocksDrivenByRecorder

    bool m_bgAnimating = false;
    bool m_texAnimating = false;


    // ==========================================================
    // MEMORIA DI STATO DELLA VISTA (Parametrica vs Implicita)
    // ==========================================================
    struct ViewState {
        QVector3D cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
        float cameraYaw = 0.0f;
        float cameraPitch = 0.0f;
        float cameraRoll = 0.0f;
        QQuaternion rotationQuat;
        float flatZoom = 1.0f;
        float flatRotation = 0.0f;
        QVector2D flatPan = QVector2D(0.0f, 0.0f);
        // Anche la trasformazione 2D GLOBALE e' per-vista, come il buffer di
        // lavoro qui sopra: senza, passando da Parametric a Ray Marching
        // l'inquadratura globale dell'una restava applicata all'altra.
        float globalTexZoom = 1.0f;
        float globalTexRotation = 0.0f;
        QVector2D globalTexPan = QVector2D(0.0f, 0.0f);
    };

    // Array con i 2 slot di memoria (0 = Parametrico, 1 = Implicito)
    ViewState m_viewStates[2];

    // ==========================================================
    // PRIVATE HELPER METHODS
    // ==========================================================
    // --- Geometry & Mesh Builders ---
    void buildWireframeGeometry();

    // --- Shader Generation & Compilation ---
    QString createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq);
    // forBorder: la variante delle pipeline del bordo (m_borderPipelineOpaque).
    QString createFragmentShaderSource(const QString &customCode, bool forBorder = false);
    // Genera UNA funzione texture procedurale col nome dato. Chiamata piu' volte
    // nello stesso shader per le texture per-mesh (getCustomColor_<k>).
    QString buildTextureFunction(const QString &customLogic, const QString &funcName);
    QString generateGlslHelperVars(const QString& sourceCode);
    // Compila per il solo backend in uso; gli shader validi gia' compilati
    // si riprendono da m_bakedShaders.
    QShader bakeShader(const QByteArray &source, QShader::Stage stage);
    // Shader compilati da bakeShader, per sorgente e stadio. Accendere e
    // spegnere il bordo, cambiare texture o tornare a una superficie gia' vista
    // ricompila sorgenti gia' visti: la traduzione (glslang + SPIRV-Cross) e'
    // la parte lenta del rebuild sulla CPU. Oltre kBakedShadersMax voci si
    // riparte da vuoto.
    QHash<QByteArray, QShader> m_bakedShaders;
    static constexpr int kBakedShadersMax = 48;
    bool applyTubeSource(const std::function<void()> &setCurve,
                         const std::vector<SurfaceEngine::TubeCopy> &copies);

    // --- Pipeline & Resource Initialization ---
    void buildPipeline();
    void initBackgroundShader();

    // --- Texture Utilities ---
    // cb: necessario per sottomettere l'upload della scacchiera di fallback.
    void createDummyTexture(QRhiCommandBuffer *cb);

    // --- Math & Projections ---
    QVector3D projectPoint4Dto3D(const QVector4D& point4D);


    // ==========================================================
    // UTILITIES
    // ==========================================================
    bool m_useFbo = false;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    QString m_lastCompilationError;
};

#endif // GLWIDGET_H
