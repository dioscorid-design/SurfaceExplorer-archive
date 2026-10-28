#include "glwidget.h"
#include "geometrybuilder.h"
#include "inputhandler.h"
#include "surfaceengine.h"
#include "glsltranslator.h"
#include "expressionparser.h"
#include "tubecurvesampler.h"

#include <QCryptographicHash>
#include <QTimer>
#include <cmath>
#include <vector>
#include <limits>
#include <QtMath>
#include <QMouseEvent>
#include <algorithm>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QSet>
#include <QFileInfo>
#include <QCoreApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QDebug>
#include <cstddef>
#include <cstring>

// Diagnostica del "triangolo-artefatto" geodetico: 1 = ispeziona la mesh FINALE
// (vertices+indices effettivamente inviati alla GPU) in setCustomMesh e logga i
// triangoli con un lato spropositato rispetto alla mediana + il numero di
// sentinelle (0,0,0,0) intercettate. Puramente osservativo, NON altera la mesh.
// Servì a localizzare il triangolo (vertice grid[0][0]=(0,0,0) triangolato su
// Mali, intermittente). Lasciato a 0; rimettere a 1 se ricompare un artefatto.
#define GEO_DIAG_TRIANGLE 0
#include <rhi/qrhi.h>

#define STEP_MIN 1
#define STEP_MAX 50
// Densità wireframe di default (deve coincidere con l'init di wfStepU/wfStepV in glwidget.h).
#define STEP_DEF 4

// MULTI-MESH: blocchi UBO pre-allocati all'avvio. Le superfici a mesh singola ne
// usano 1; il buffer cresce da solo se uno script dichiara piu' parti.
static const int kInitialUboBlocks = 8;

// Ultima risorsa di shader NON caricabile dal .qrc. Vuota = tutto bene.
// Serve a distinguere due guasti che a valle si presentano identici (shader non
// valido, pipeline non costruite, schermo vuoto) ma hanno cause opposte:
//   - lo script dell'utente non compila     -> il baker riporta l'errore GLSL;
//   - la RISORSA non c'e' proprio nel binario -> il baker riceve sorgente VUOTO
//     e fallisce con errorMessage() VUOTO, senza dire perche'.
// Nel secondo caso l'avviso all'utente parlava di conflitti fra texture - una
// spiegazione plausibile ma FALSA, che manda a cercare nella direzione sbagliata
// (successo davvero: build Xcode col qrc_resources_CMAKE_.cpp della sola
// configurazione Debug a 0 byte, mentre da Qt Creator andava tutto).
QString g_lastMissingShaderResource;

QString loadShaderSource(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Errore caricamento shader:" << path;
        // Si ricorda QUALE risorsa manca: e' l'informazione che rende
        // riconoscibile un binario costruito male, e a valle non e' piu'
        // ricavabile (il sorgente vuoto e' indistinguibile da un altro vuoto).
        g_lastMissingShaderResource = path;
        return "";
    }
    QTextStream in(&file);
    return in.readAll();
}

GLWidget::GLWidget(QWidget *parent)
    : QRhiWidget(parent)
{
    m_uboData = UboData();

    m_uboData.x_min = -10.0f;
    m_uboData.x_max =  10.0f;
    m_uboData.y_min = -10.0f;
    m_uboData.y_max =  10.0f;
    m_uboData.z_min = -10.0f;
    m_uboData.z_max =  10.0f;

    // --- 1. SETUP LOGICA APPLICATIVA E INPUT ---
    m_inputHandler = std::make_unique<InputHandler>(this);
    engine = std::make_unique<SurfaceEngine>();

    // --- 2. INIZIALIZZAZIONE VARIABILI DI STATO ---
    m_flatPan = QVector2D(0.0f, 0.0f);
    m_flatZoom = 1.0f;
    m_flatRotation = 0.0f;
    m_rotationQuat = QQuaternion();

    m_surfaceTimer.start();

    m_surfaceAnimating = false;

    m_textureEnabled = false;
    nutationSpeed = precessionSpeed = spinSpeed = 0.0f;
    omegaSpeed = phiSpeed = psiSpeed = 0.0f;
    precession = 0.0f; nutation = 0.0f; spin = 0.0f;
    red = green = blue = 0.5f; alpha = 1.0f;

    m_cameraPos = QVector3D(0.0f, 0.0f, 4.0f);
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraRoll = 0.0f;

    m_pathTarget = QVector3D(0.0f, 0.0f, 0.0f);
    m_pathUp = QVector3D(0.0f, 0.0f, 1.0f);

    // --- 3. ATTRIBUTI DEL WIDGET ---
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);

    // --- 4. TIMERS ---
    rotationTimer = new QTimer(this);
    rotationTimer->setInterval(kRotationTickMs);
    connect(rotationTimer, &QTimer::timeout, this, &GLWidget::updateRotation);
    rotationTimer->stop();

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33); // ~30 FPS

    // In RHI, chiamare update() farà scattare automaticamente l'evento render()
    connect(m_animTimer, &QTimer::timeout, this, QOverload<>::of(&GLWidget::update));

    // Sonda dei lati coperti (borderAvailability): parte quando la geometria
    // resta ferma per un po', non al primo fotogramma del caricamento.
    m_borderProbeTimer = new QTimer(this);
    m_borderProbeTimer->setSingleShot(true);
    m_borderProbeTimer->setInterval(400);
    connect(m_borderProbeTimer, &QTimer::timeout, this, [this] {
        m_borderProbeArmed = true;
        update();
    });
}

GLWidget::~GLWidget()
{
}


// ==========================================================
// PROTECTED
// ==========================================================

// ==========================================================
// MULTI-MESH: gestione dell'UBO ad array
// ==========================================================

// Garantisce che l'UBO contenga almeno partCount blocchi. Ritorna true se il
// buffer e' stato (ri)creato: in quel caso i binding che lo referenziano puntano
// a memoria liberata e vanno riagganciati (stessa trappola gia' documentata per
// m_surfaceTexture nel render).
bool GLWidget::ensureUboCapacity(int partCount)
{
    if (!rhi()) return false;

    const int wanted = std::max(1, partCount);
    if (m_ubo && wanted <= m_uboBlockCapacity) return false;

    // Cresce con margine per non ricreare il buffer a ogni parte in piu'.
    const int newCap = std::max(wanted, m_uboBlockCapacity * 2);

    if (m_ubo) {
        m_ubo->destroy();
        delete m_ubo;
        m_ubo = nullptr;
    }

    m_ubo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                             m_uboBlockStride * newCap);
    m_ubo->create();
    m_uboBlockCapacity = newCap;

    // Riaggancia i binding al NUOVO buffer senza distruggere l'oggetto SRB: le
    // pipeline ne tengono il puntatore, quindi un delete+new le lascerebbe con
    // un riferimento pendente. Il layout non cambia, cambia solo la risorsa.
    if (m_bindingsDyn) {
        m_bindingsDyn->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubo, sizeof(UboData)),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_bindingsDynTexture ? m_bindingsDynTexture : m_dummyTexture, m_sampler),
            tubeFramesBinding(),
            borderEdgesBinding(),
            sharedEdgesBinding()
        });
        m_bindingsDyn->updateResources();
    }
    // ...e la passata compute dei tubi, che legge gli stessi blocchi.
    if (m_tubeFrameSrb) {
        m_tubeFrameSrb->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                0, QRhiShaderResourceBinding::ComputeStage, m_ubo, sizeof(UboData)),
            QRhiShaderResourceBinding::imageStore(1, QRhiShaderResourceBinding::ComputeStage, m_tubeFrameTex, 0),
            QRhiShaderResourceBinding::bufferLoadStore(2, QRhiShaderResourceBinding::ComputeStage, m_tubeFrameWork)
        });
        m_tubeFrameSrb->updateResources();
    }
    // ...e quella dei lati del bordo coperti.
    if (m_sharedEdgeSrb) {
        setSharedEdgeBindings();
        m_sharedEdgeSrb->updateResources();
    }

    // Anche i binding statici (sfondo flat, ray marching) puntavano al vecchio UBO.
    if (m_bindings) {
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                m_surfaceTexture ? m_surfaceTexture : m_dummyTexture, m_sampler)
        });
        m_bindings->updateResources();
    }

    // ...e quelli delle immagini di fascia.
    rebindPartImages();

    return true;
}

// Binding della superficie con dynamic offset sul blocco UBO. La dimensione
// dichiarata e' sizeof(UboData) (la finestra visibile su un blocco), non lo
// stride fra blocchi.
void GLWidget::ensureDynamicBindings(QRhiTexture *tex)
{
    if (!rhi() || !m_ubo) return;
    if (!tex) tex = m_dummyTexture;
    if (!tex || !m_sampler) return;

    // Niente da fare se il binding esiste gia' con la stessa texture.
    if (m_bindingsDyn && m_bindingsDynTexture == tex) return;

    const bool isNew = (m_bindingsDyn == nullptr);
    if (isNew) {
        m_bindingsDyn = rhi()->newShaderResourceBindings();
    }

    m_bindingsDyn->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubo, sizeof(UboData)),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, tex, m_sampler),
        tubeFramesBinding(),
        borderEdgesBinding(),
        sharedEdgesBinding()
    });

    if (isNew) {
        m_bindingsDyn->create();
    } else {
        // IMPORTANTE: updateResources() e non create(). Le pipeline tengono il
        // PUNTATORE a questo oggetto: ricrearlo (delete + new) le lascerebbe con
        // un puntatore pendente e obbligherebbe a ricostruirle tutte. Qui il
        // layout non cambia (stessi tipi/binding), solo la risorsa allo slot 1.
        m_bindingsDyn->updateResources();
    }

    m_bindingsDynTexture = tex;
}

// ==========================================================
// SEZIONE TRASPORTATA DEI TUBI (vedi glwidget.h e tubeSceneFrame in surface.vert)
// ==========================================================

void GLWidget::createTubeFrameResources()
{
    if (m_tubeFrameTex || !rhi()) return;
    if (!rhi()->isFeatureSupported(QRhi::Compute)
        || !rhi()->isTextureFormatSupported(QRhiTexture::RGBA32F, QRhiTexture::UsedWithLoadStore))
        return;
    // Due texel per anello: sezione + u dell'anello, tangente.
    m_tubeFrameTex = rhi()->newTexture(QRhiTexture::RGBA32F, QSize(2 * kTubeFrameMaxRings, kTubeFrameMaxParts),
                                       1, QRhiTexture::UsedWithLoadStore);
    if (!m_tubeFrameTex->create()) {
        delete m_tubeFrameTex;
        m_tubeFrameTex = nullptr;
        return;
    }
    // Texel interi letti con texelFetch: niente filtro (RGBA32F non e'
    // filtrabile ovunque, e con Linear su GLES la texture risulterebbe
    // incompleta).
    m_tubeFrameSampler = rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_tubeFrameSampler->create();
    const quint32 workBytes = quint32(kTubeFrameWorkVec4) * 16u;
    m_tubeFrameWork = rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, workBytes);
    if (!m_tubeFrameWork->create()) {
        delete m_tubeFrameWork;
        m_tubeFrameWork = nullptr;
        m_tubeFrameTex->destroy();
        delete m_tubeFrameTex;
        m_tubeFrameTex = nullptr;
    }
    m_tubeFrameStateReset = true;
}

QRhiShaderResourceBinding GLWidget::tubeFramesBinding() const
{
    return QRhiShaderResourceBinding::sampledTexture(
        2, QRhiShaderResourceBinding::VertexStage,
        m_tubeFrameTex ? m_tubeFrameTex : m_dummyTexture,
        m_tubeFrameSampler ? m_tubeFrameSampler : m_sampler);
}

// ==========================================================
// LATI DELLA MESH CUSTOM PER IL BORDO (vedi glwidget.h e borderRawPoint in surface.vert)
// ==========================================================

void GLWidget::createBorderEdgeResources()
{
    if (m_borderEdgeTex || !rhi()) return;
    if (!rhi()->isTextureFormatSupported(QRhiTexture::RGBA32F)) return;
    m_borderEdgeTex = rhi()->newTexture(QRhiTexture::RGBA32F, QSize(kBorderEdgeMaxSamples, kBorderEdgeRows));
    if (!m_borderEdgeTex->create()) {
        delete m_borderEdgeTex;
        m_borderEdgeTex = nullptr;
        return;
    }
    // Letta con texelFetch, come la texture dei tubi: niente filtro.
    m_borderEdgeSampler = rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_borderEdgeSampler->create();
    m_borderEdgeDirty = !m_borderEdgeData.empty();
}

QRhiShaderResourceBinding GLWidget::borderEdgesBinding() const
{
    return QRhiShaderResourceBinding::sampledTexture(
        3, QRhiShaderResourceBinding::VertexStage,
        m_borderEdgeTex ? m_borderEdgeTex : m_dummyTexture,
        m_borderEdgeSampler ? m_borderEdgeSampler : m_sampler);
}

// Distanza di p dal triangolo abc (4D): proiezione sul piano del triangolo,
// o il lato piu' vicino se cade fuori.
static float pointTriangleDistance(const QVector4D &p, const QVector4D &a, const QVector4D &b, const QVector4D &c)
{
    const QVector4D e0 = b - a, e1 = c - a, v = p - a;
    const float m00 = QVector4D::dotProduct(e0, e0), m01 = QVector4D::dotProduct(e0, e1);
    const float m11 = QVector4D::dotProduct(e1, e1);
    const float r0 = QVector4D::dotProduct(v, e0), r1 = QVector4D::dotProduct(v, e1);
    const float det = m00 * m11 - m01 * m01;
    if (std::abs(det) > 1e-30f) {
        const float s = (r0 * m11 - r1 * m01) / det;
        const float t = (r1 * m00 - r0 * m01) / det;
        if (s >= 0.0f && t >= 0.0f && s + t <= 1.0f) return (a + s * e0 + t * e1 - p).length();
    }
    auto seg = [&p](const QVector4D &x, const QVector4D &y) {
        const QVector4D d = y - x;
        const float L = QVector4D::dotProduct(d, d);
        const float u = (L < 1e-30f) ? 0.0f : std::clamp(QVector4D::dotProduct(p - x, d) / L, 0.0f, 1.0f);
        return (x + u * d - p).length();
    };
    return std::min({ seg(a, b), seg(b, c), seg(c, a) });
}

// Righe: 0 lato v = vMin (lungo u), 1 la riga accanto; 2 lato v = vMax, 3 la
// riga accanto; 4 lato u = uMin (lungo v), 5 la riga accanto; 6 lato u = uMax,
// 7 la riga accanto; 8 texel 0 = (numU, numV), texel 1 = i lati coperti, uno
// per canale (1 = non si disegna, borderEdgeShared in surface.vert).
// I LATI SONO QUELLI DELLA MESH DISEGNATA, non della griglia: setCustomMesh
// salta i quad con punti morti o congelati, e sul Kruskal (traiettorie ferme
// all'inizio e alla fine: si disegna la fascia j = 37..66) il bordo seguiva
// righe della griglia lontane dalla superficie. Per ogni traiettoria si
// prende il primo e l'ultimo punto disegnato (j0, j1); le traiettorie senza
// punti disegnati in testa e in coda si saltano (i0, i1). Un lato lungo u
// (righe 0-3) va da i0 a i1 sui punti j0 o j1; un lato lungo v (righe 4-7) e'
// la traiettoria i0 o i1, da j0 a j1. La sentinella del punto morto (0,0,0,0
// esatto) prende comunque il punto valido piu' vicino della riga.
// LATI COPERTI, come nella passata compute delle superfici
// (buildSharedEdgePipeline) ma sulla mesh vera, che qui c'e': un lato non e'
// un bordo se in ciascuno dei suoi tre punti di prova passa un altro foglio
// della mesh, lontano dal punto lungo la superficie, che vi prosegue in
// un'altra direzione (nel piano normale al lato). Sul Plucker Conoid le
// ultime traiettorie ripercorrono le prime: i lati u = uMin e u = uMax stanno
// DENTRO la superficie e si vedevano come linee che la attraversano. Le
// cuciture valgono lo stesso. Resta un bordo il lato percorso due volte
// dallo stesso foglio: le direzioni coincidono.
// "Lontano lungo la superficie": i punti della griglia raggiungibili dal
// punto di prova restando entro R non contano. Un intorno in indici non
// basta: al bordo del Plucker in H3 le geodetiche rallentano (passi di 2e-5)
// e il punto stesso, qualche passo piu' in la', sembrava un altro foglio.
void GLWidget::setBorderEdges(const QVector<QVector<QVector4D>> &grid, const std::vector<char> &drawnQuad)
{
    const int numU = grid.size() - 1;
    const int numV = grid.isEmpty() ? -1 : grid[0].size() - 1;
    if (numU < 1 || numV < 1 || numU >= kBorderEdgeMaxSamples || numV >= kBorderEdgeMaxSamples
        || drawnQuad.size() != size_t(numU) * size_t(numV)) {
        m_borderEdgeData.clear();
        m_borderEdgeWidth = 0;
        emit borderAvailabilityChanged();
        return;
    }
    auto at = [numV](int i, int j) { return size_t(i) * size_t(numV + 1) + size_t(j); };
    auto quadDrawn = [&](int i, int j) {
        return i >= 0 && i < numU && j >= 0 && j < numV && drawnQuad[size_t(i) * numV + j] != 0;
    };
    std::vector<char> drawn(size_t(numU + 1) * (numV + 1), 0);
    for (int i = 0; i < numU; ++i)
        for (int j = 0; j < numV; ++j)
            if (quadDrawn(i, j))
                drawn[at(i, j)] = drawn[at(i + 1, j)] = drawn[at(i, j + 1)] = drawn[at(i + 1, j + 1)] = 1;

    std::vector<int> j0(numU + 1, -1), j1(numU + 1, -1);
    for (int i = 0; i <= numU; ++i)
        for (int j = 0; j <= numV; ++j)
            if (drawn[at(i, j)]) {
                if (j0[i] < 0) j0[i] = j;
                j1[i] = j;
            }
    int i0 = 0, i1 = numU;
    while (i0 <= numU && j0[i0] < 0) ++i0;
    while (i1 >= 0 && j0[i1] < 0) --i1;
    if (i0 > i1) {
        m_borderEdgeData.clear();
        m_borderEdgeWidth = 0;
        emit borderAvailabilityChanged();
        return;
    }
    // Traiettorie senza punti disegnati fra i0 e i1: quelle della vicina.
    for (int i = i0; i <= i1; ++i) {
        if (j0[i] >= 0) continue;
        int k = i - 1;
        while (k > i0 && j0[k] < 0) --k;
        j0[i] = j0[k];
        j1[i] = j1[k];
    }

    // Punto del lato `side` al texel k, `depth` passi verso l'interno.
    auto lerp = [](const QVector4D &a, const QVector4D &b, float t) { return a + (b - a) * t; };
    auto alongTrajectory = [&](int i, float jf) {
        const int j = std::clamp(int(std::floor(jf)), 0, numV);
        const int jn = std::min(j + 1, numV);
        return lerp(grid[i][j], grid[i][jn], jf - float(j));
    };
    auto sidePoint = [&](int side, int k, int depth) -> QVector4D {
        if (side < 2) {
            const float x = float(i0) + float(i1 - i0) * float(k) / float(numU);
            const int i = std::clamp(int(std::floor(x)), i0, i1);
            const int in = std::min(i + 1, i1);
            auto pt = [&](int t) {
                const int j = (side == 0) ? std::min(j0[t] + depth, j1[t]) : std::max(j1[t] - depth, j0[t]);
                return grid[t][j];
            };
            return lerp(pt(i), pt(in), x - float(i));
        }
        const int t = (side == 2) ? std::min(i0 + depth, i1) : std::max(i1 - depth, i0);
        return alongTrajectory(t, float(j0[t]) + float(j1[t] - j0[t]) * float(k) / float(numV));
    };
    // Nodo della griglia piu' vicino al texel k del lato.
    auto sideNode = [&](int side, int k) -> std::pair<int, int> {
        if (side < 2) {
            const int i = std::clamp(int(std::lround(float(i0) + float(i1 - i0) * float(k) / float(numU))), i0, i1);
            return { i, (side == 0) ? j0[i] : j1[i] };
        }
        const int t = (side == 2) ? i0 : i1;
        return { t, std::clamp(int(std::lround(float(j0[t]) + float(j1[t] - j0[t]) * float(k) / float(numV))), 0, numV) };
    };

    const int width = std::max(numU, numV) + 1;
    m_borderEdgeWidth = width;
    m_borderEdgeData.assign(size_t(width) * kBorderEdgeRows * 4, 0.0f);
    auto fillRow = [&](int row, int count, auto pointAt) {
        std::vector<QVector4D> pts(count);
        for (int k = 0; k < count; ++k) pts[k] = pointAt(k);
        const QVector4D dead(0.0f, 0.0f, 0.0f, 0.0f);
        for (int k = 0; k < count; ++k) {
            if (pts[k] != dead) continue;
            for (int d = 1; d < count; ++d) {
                if (k - d >= 0 && pts[k - d] != dead) { pts[k] = pts[k - d]; break; }
                if (k + d < count && pts[k + d] != dead) { pts[k] = pts[k + d]; break; }
            }
        }
        float *out = m_borderEdgeData.data() + size_t(row) * width * 4;
        for (int k = 0; k < count; ++k) {
            out[k * 4 + 0] = pts[k].x();
            out[k * 4 + 1] = pts[k].y();
            out[k * 4 + 2] = pts[k].z();
            out[k * 4 + 3] = pts[k].w();
        }
    };
    for (int side = 0; side < 4; ++side) {
        const int count = (side < 2 ? numU : numV) + 1;
        fillRow(2 * side, count, [&](int k) { return sidePoint(side, k, 0); });
        fillRow(2 * side + 1, count, [&](int k) { return sidePoint(side, k, 1); });
    }
    float *info = m_borderEdgeData.data() + size_t(8) * width * 4;
    info[0] = float(numU);
    info[1] = float(numV);

    // --- Lati coperti ---
    float scale = 1.0f;
    for (int i = 0; i <= numU; ++i)
        for (int j = 0; j <= numV; ++j)
            if (drawn[at(i, j)]) {
                const QVector4D &q = grid[i][j];
                scale = std::max({ scale, std::abs(q.x()), std::abs(q.y()), std::abs(q.z()) });
            }
    const float tol = 1e-3f * scale;
    const float reach = 20.0f * tol;
    const size_t nodes = size_t(numU + 1) * (numV + 1);
    // Candidati a passo `stride` (circa 20000), poi i quad attorno ai migliori.
    const int stride = std::max(1, int(std::sqrt(double(nodes) / 20000.0)));
    std::vector<int> ownStamp(nodes, -1);
    int stamp = 0;
    static const float kTestAt[3] = { 0.1834f, 0.3912f, 0.8259f };
    for (int side = 0; side < 4; ++side) {
        const int n = side < 2 ? numU : numV;
        float len = 0.0f;
        for (int k = 0; k < n; ++k) len += (sidePoint(side, k + 1, 0) - sidePoint(side, k, 0)).length();
        bool covered = len < tol;   // polo
        for (int t = 0; t < 3 && !covered; ++t) {
            ++stamp;   // ogni punto di prova ha il suo intorno
            const int k = std::clamp(int(std::lround(kTestAt[t] * n)), 0, n);
            const QVector4D P = sidePoint(side, k, 0);
            QVector4D T = sidePoint(side, std::min(k + 1, n), 0) - sidePoint(side, std::max(k - 1, 0), 0);
            if (T.lengthSquared() > 1e-30f) T.normalize();
            // Verso l'interno: il primo punto oltre R sulla stessa riga di nodi.
            QVector4D inward;
            for (int depth = 1; depth <= 64; ++depth) {
                inward = sidePoint(side, k, depth) - P;
                if (inward.length() > reach) break;
            }
            inward -= QVector4D::dotProduct(inward, T) * T;
            if (inward.lengthSquared() > 1e-30f) inward.normalize();
            // Intorno del punto lungo la superficie (al piu' 50000 nodi).
            const auto [ni, nj] = sideNode(side, k);
            std::vector<std::pair<int, int>> queue{ { ni, nj } };
            ownStamp[at(ni, nj)] = stamp;
            for (size_t q = 0; q < queue.size() && queue.size() < 50000; ++q) {
                const auto [a, b] = queue[q];
                const int na[4] = { a + 1, a - 1, a, a };
                const int nb[4] = { b, b, b + 1, b - 1 };
                for (int d = 0; d < 4; ++d) {
                    if (na[d] < 0 || na[d] > numU || nb[d] < 0 || nb[d] > numV) continue;
                    if (ownStamp[at(na[d], nb[d])] == stamp) continue;
                    if ((grid[na[d]][nb[d]] - P).length() >= reach) continue;
                    ownStamp[at(na[d], nb[d])] = stamp;
                    queue.push_back({ na[d], nb[d] });
                }
            }
            auto own = [&](int i, int j) { return ownStamp[at(i, j)] == stamp; };
            // I nodi piu' vicini fuori dall'intorno. L'ultima riga e l'ultima
            // colonna ci sono sempre, anche fuori passo: nel Twisted Torus
            // Ribbon la traiettoria gemella della prima (cucitura) e' la 423,
            // e a passo 2 non si guardava mai -- il lato u = uMin restava col
            // bordo, u = uMax (gemella la 0) no.
            std::vector<std::pair<float, int>> best;
            auto next = [stride](int x, int last) { return (x < last && x + stride > last) ? last : x + stride; };
            for (int i = 0; i <= numU; i = next(i, numU))
                for (int j = 0; j <= numV; j = next(j, numV)) {
                    if (!drawn[at(i, j)] || own(i, j)) continue;
                    best.push_back({ (grid[i][j] - P).lengthSquared(), int(at(i, j)) });
                }
            const size_t keep = std::min<size_t>(8, best.size());
            std::partial_sort(best.begin(), best.begin() + keep, best.end());
            float bestDist = 1e30f;
            int bi = -1, bj = -1;
            for (size_t c = 0; c < keep; ++c) {
                const int ci = best[c].second / (numV + 1), cj = best[c].second % (numV + 1);
                for (int qi = ci - stride; qi < ci + stride; ++qi)
                    for (int qj = cj - stride; qj < cj + stride; ++qj) {
                        if (!quadDrawn(qi, qj)) continue;
                        if (own(qi, qj) || own(qi + 1, qj) || own(qi, qj + 1) || own(qi + 1, qj + 1)) continue;
                        const QVector4D &p0 = grid[qi][qj], &p1 = grid[qi + 1][qj];
                        const QVector4D &p2 = grid[qi][qj + 1], &p3 = grid[qi + 1][qj + 1];
                        const float d = std::min(pointTriangleDistance(P, p0, p1, p2),
                                                 pointTriangleDistance(P, p2, p1, p3));
                        if (d < bestDist) { bestDist = d; bi = qi; bj = qj; }
                    }
            }
            if (bestDist >= tol) break;   // un punto scoperto: e' un bordo
            // Il foglio che copre parte in un'altra direzione?
            float minCos = 2.0f;
            for (int i = std::max(0, bi - 3); i <= std::min(numU, bi + 4); ++i)
                for (int j = std::max(0, bj - 3); j <= std::min(numV, bj + 4); ++j) {
                    if (!drawn[at(i, j)] || own(i, j)) continue;
                    QVector4D w = grid[i][j] - P;
                    w -= QVector4D::dotProduct(w, T) * T;
                    const float L = w.length();
                    if (L > tol) minCos = std::min(minCos, QVector4D::dotProduct(w, inward) / L);
                }
            if (minCos >= 0.5f) break;   // stesso foglio (o percorso due volte), come nel compute
            if (t == 2) covered = true;
        }
        info[4 + side] = covered ? 1.0f : 0.0f;
    }
    m_borderEdgeDirty = true;
    emit borderAvailabilityChanged();   // borderAvailability legge questi flag
}

QString GLWidget::vertexAsComputeLibrary(const QString &vertexSource, const QString &define,
                                         const QString &mainName, const QString &position)
{
    QString src = vertexSource;
    static const QRegularExpression ioRe(QStringLiteral(R"(layout\s*\(\s*location\s*=\s*\d+\s*\)\s*(in|out)\s+)"));
    src.remove(ioRe);
    static const QRegularExpression mainRe(QStringLiteral(R"(\bvoid\s+main\s*\(\s*\))"));
    src.replace(mainRe, QStringLiteral("void ") + mainName + QStringLiteral("()"));
    src.replace(QLatin1String("gl_Position"), position);
    const int versionAt = src.indexOf(QLatin1String("#version"));
    const int eol = (versionAt < 0) ? -1 : src.indexOf(QLatin1Char('\n'), versionAt);
    if (eol < 0) return QString();
    src.insert(eol + 1, QStringLiteral("#define ") + define + QStringLiteral("\nvec4 ") + position
                            + QStringLiteral(";\n"));
    return src;
}

// ==========================================================
// LATI DEL BORDO COPERTI (vedi glwidget.h e borderEdgeShared in surface.vert)
// ==========================================================

void GLWidget::createSharedEdgeResources()
{
    if (m_sharedEdgeTex || !rhi()) return;
    if (!rhi()->isFeatureSupported(QRhi::Compute)
        || !rhi()->isTextureFormatSupported(QRhiTexture::RGBA8, QRhiTexture::UsedWithLoadStore))
        return;
    // Un texel per campione del lato: colonna = campione, riga = parte * 4 + lato;
    // l'ultima colonna (kSharedEdgeSamples + 1) dice se il lato e' coperto per
    // intero (quinta passata). Sorgente di copia: gli scenari e la sonda ne
    // rileggono i flag.
    m_sharedEdgeTex = rhi()->newTexture(QRhiTexture::RGBA8, QSize(kSharedEdgeSamples + 2, 4 * kSharedEdgeMaxParts),
                                        1, QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource);
    if (!m_sharedEdgeTex->create()) {
        delete m_sharedEdgeTex;
        m_sharedEdgeTex = nullptr;
        return;
    }
    m_sharedEdgeSampler = rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_sharedEdgeSampler->create();
    // In testa (numero di parti, 0, 0, 0), poi due vec4 per parte.
    m_sharedEdgeParts = rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                         quint32(1 + 2 * kSharedEdgeMaxParts) * 16u);
    // Per parte: i campioni dei lati (punto e punto appena dentro), poi la
    // griglia dell'interno.
    m_sharedEdgeSamples = rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                           quint32(kSharedEdgeMaxParts * kSharedEdgePartVec4) * 16u);
    // Per parte e blocco di celle: centro e raggio della sfera che lo contiene.
    // Grezzi della terza passata, poi (seconda meta') ripuliti dalla quarta.
    m_sharedEdgeRaw = rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                       quint32(2 * kSharedEdgeMaxParts * 4 * (kSharedEdgeSamples + 1)) * 4u);
    m_sharedEdgeTiles = rhi()->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer,
                                         quint32(kSharedEdgeMaxParts * kSharedEdgeTiles * 2) * 16u);
    if (!m_sharedEdgeParts->create() || !m_sharedEdgeSamples->create() || !m_sharedEdgeTiles->create()
        || !m_sharedEdgeRaw->create()) {
        delete m_sharedEdgeParts;
        m_sharedEdgeParts = nullptr;
        delete m_sharedEdgeSamples;
        m_sharedEdgeSamples = nullptr;
        delete m_sharedEdgeTiles;
        m_sharedEdgeTiles = nullptr;
        delete m_sharedEdgeRaw;
        m_sharedEdgeRaw = nullptr;
        m_sharedEdgeTex->destroy();
        delete m_sharedEdgeTex;
        m_sharedEdgeTex = nullptr;
    }
    m_sharedEdgeTable.clear();
    m_sharedEdgeKey.clear();
}

QRhiShaderResourceBinding GLWidget::sharedEdgesBinding() const
{
    // Anche il fragment: scarta i tratti coperti di un lato.
    return QRhiShaderResourceBinding::sampledTexture(
        4, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        m_sharedEdgeTex ? m_sharedEdgeTex : m_dummyTexture,
        m_sharedEdgeSampler ? m_sharedEdgeSampler : m_sampler);
}

void GLWidget::setSharedEdgeBindings()
{
    m_sharedEdgeSrb->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0, QRhiShaderResourceBinding::ComputeStage, m_ubo, sizeof(UboData)),
        QRhiShaderResourceBinding::imageStore(1, QRhiShaderResourceBinding::ComputeStage, m_sharedEdgeTex, 0),
        QRhiShaderResourceBinding::bufferLoad(2, QRhiShaderResourceBinding::ComputeStage, m_sharedEdgeParts),
        QRhiShaderResourceBinding::bufferLoadStore(3, QRhiShaderResourceBinding::ComputeStage, m_sharedEdgeSamples),
        QRhiShaderResourceBinding::bufferLoadStore(4, QRhiShaderResourceBinding::ComputeStage, m_sharedEdgeTiles),
        QRhiShaderResourceBinding::bufferLoadStore(5, QRhiShaderResourceBinding::ComputeStage, m_sharedEdgeRaw)
    });
}

void GLWidget::releaseSharedEdgePipeline()
{
    if (m_sharedEdgePipeline) { m_sharedEdgePipeline->destroy(); delete m_sharedEdgePipeline; m_sharedEdgePipeline = nullptr; }
    if (m_sharedEdgeSrb) { m_sharedEdgeSrb->destroy(); delete m_sharedEdgeSrb; m_sharedEdgeSrb = nullptr; }
    m_sharedEdgeKey.clear();
}

GLWidget::BorderAvailability GLWidget::borderAvailability() const
{
    if (!engine || m_engineMode != ModeParametric || engine->isTubeModeActive()) return BorderAvailability::Unknown;
    if (m_isCustomMesh) {
        // Flusso geodetico: i lati coperti li decide la CPU (setBorderEdges).
        if (m_borderEdgeWidth <= 0) return BorderAvailability::Unknown;
        const float *info = m_borderEdgeData.data() + size_t(8) * m_borderEdgeWidth * 4;
        for (int side = 0; side < 4; ++side)
            if (info[4 + side] < 0.5f) return BorderAvailability::Visible;
        return BorderAvailability::None;
    }
    return m_borderAvailability;
}

void GLWidget::setBorderAvailability(BorderAvailability a)
{
    if (a == m_borderAvailability) return;
    m_borderAvailability = a;
    emit borderAvailabilityChanged();
}

// I flag della sonda: un lato si vede se il vertex non lo nasconde per intero
// (la colonna del lato, come borderEdgeShared). Se nessun lato di nessuna
// parte si vede, il bordo non compare. Scartata se nel frattempo e' cambiata la
// superficie: arrivera' la sonda della nuova.
void GLWidget::finishBorderProbe()
{
    // Una chiamata per lettura: si decide quando sono arrivate tutte (piu'
    // istanti su una superficie animata).
    if (++m_borderProbeArrived < m_borderProbeInstantsRead) return;
    m_borderProbeReading = false;
    if (m_borderProbeReadSerial != m_borderProbeSerial) return;
    // Un istante: 1 se almeno un lato di una parte si vede, 0 se no, -1 se la
    // lettura non e' valida.
    auto visibleAt = [&](int i) -> int {
        const QByteArray &data = m_borderProbeResults[i].data;
        const int bpl = data.size() / (4 * kSharedEdgeMaxParts);
        if (bpl < (kSharedEdgeSamples + 2) * 4 || m_borderProbeMeshes.empty()) return -1;
        for (int mesh : m_borderProbeMeshes) {
            if (mesh < 0 || mesh >= kSharedEdgeMaxParts) continue;
            for (int side = 0; side < 4; ++side) {
                // L'ultima colonna: il lato e' coperto per intero (quinta passata).
                const int row = mesh * 4 + side;
                if (quint8(data.at(row * bpl + (kSharedEdgeSamples + 1) * 4)) <= 127) return 1;
            }
        }
        return 0;
    };
    const int count = m_borderProbeInstantsRead;
    bool visible = false, read = false;
    for (int i = 0; i < count; ++i) {
        const int v = visibleAt(i);
        if (v < 0) continue;
        read = true;
        if (v > 0) visible = true;
    }
    if (!read) {
        m_borderProbeWatch = false;
        setBorderAvailability(BorderAvailability::Unknown);
        return;
    }
    // SUPERFICIE ANIMATA (scelta dell'utente, 10-17): sempre chiusa -> Border
    // spento; si apre in qualche istante -> spento finche' e' chiusa, poi dalla
    // prima apertura sempre acceso, senza lampeggi. Si sa dagli istanti della
    // sonda (l'ultimo e' ora); finche' non si apre, la si sorveglia
    // (m_borderProbeWatch: solo l'istante corrente, ogni kBorderProbeWatchMs).
    if (count > 1) {
        const bool now = visibleAt(count - 1) > 0;
        m_borderProbeWatch = visible && !now;
        if (m_borderProbeWatch) m_borderProbeWatchClock.restart();
        setBorderAvailability(now ? BorderAvailability::Visible : BorderAvailability::None);
        return;
    }
    if (m_borderProbeWatch) {
        if (visible) {
            m_borderProbeWatch = false;
            setBorderAvailability(BorderAvailability::Visible);
        }
        return;
    }
    setBorderAvailability(visible ? BorderAvailability::Visible : BorderAvailability::None);
}

bool GLWidget::sharedEdgesActive()
{
    if (m_sharedEdgesOff || !m_sharedEdgeTex || !m_sharedEdgeParts || !m_sharedEdgeSamples || !m_sharedEdgeTiles
        || !m_sharedEdgeRaw || !engine
        || m_engineMode != ModeParametric || m_isCustomMesh || engine->isTubeModeActive())
        return false;
    const int parts = (int)engine->getMeshParts().size();
    if (parts < 1 || parts > kSharedEdgeMaxParts) return false;
    if (m_sharedEdgeSourceDirty) buildSharedEdgePipeline();
    return m_sharedEdgePipeline && m_sharedEdgeSrb;
}

// Il compute e' il vertex della superficie ridotto a libreria, come quello dei
// tubi: la superficie, i suoi vincoli e lo script sono gli stessi del disegno.
// L'indice `mesh` dello script non viene dal blocco UBO (il compute ne vede
// uno solo) ma dalla tabella delle parti: cosi' una sola invocazione valuta
// qualunque mesh. Punti NON ruotati: le rotazioni 4D conservano le distanze
// e gli angoli, il risultato non dipende dalla posa (e si riusa finche' la
// geometria non cambia, m_sharedEdgeKey).
//
// UN LATO NON E' UN BORDO se in ciascuno dei suoi tre punti di prova passa
// un ALTRO lato -- della stessa parte o di un'altra, anche il lato stesso
// piu' in la' -- il cui foglio parte in una direzione DIVERSA dal proprio,
// confrontando le due direzioni verso l'interno nel piano normale al lato.
// Cosi' si riconoscono con un test solo:
//  - le cuciture diritte e ritorte (toro, Moebius) e i lati in comune fra
//    mesh (Calabi-Yau): l'altro foglio prosegue dall'altra parte;
//  - i lati che si ripiegano su se' stessi (Oloid: v = 1 vale per w e per
//    -w) e quelli che avvolgono piu' volte la stessa curva (Wound Sphere:
//    v = pi/2 gira cinque volte su un cerchio, dove si incontrano cinque
//    lembi): i fogli partono con un angolo, anche stretto (gli archi
//    dell'Oloid sono uno spigolo vivo di 30 gradi);
//  - i poli: il lato e' lungo meno della tolleranza;
//  - le cuciture punto per punto, anche dove la superficie degenera in una
//    linea e non c'e' un altro foglio (Cuspid Koranyi Sphere).
// Resta un bordo il lato percorso due volte dallo STESSO foglio (una
// superficie rivestita due volte): le due direzioni coincidono. E resta un
// bordo l'orlo che ATTRAVERSA un altro foglio senza giacervi (Enneper in H3,
// Richmond): coperto e' solo il lato che giace nel foglio per un tratto.
// Il comportamento si verifica rileggendo i flag dalla GPU su tutta la
// libreria: --scenario-only border-audit (ScenarioTest::auditLoadedBorder).
// Due passate. Nella prima (blocco di una parte, u_isBorder = 0) ogni lato
// si campiona in kSharedEdgeSamples intervalli, col punto appena dentro;
// nella seconda (blocco del bordo, u_isBorder = 1) i punti di prova si
// confrontano con i campioni di tutti i lati, senza piu' valutare la
// superficie: una sola chiamata allo script in tutto il compute (Mali la
// ricopia per ogni punto di chiamata).
// I punti di prova NON stanno a frazioni tonde (1/2, 1/3...): sui domini
// simmetrici e' li' che cadono i punti singolari. Nel Calabi-Yau il mezzo del
// lato u = pi/2 e' v = 0, dove z1 = 0 e si toccano tutte le patch: cos(pi/2)
// in float vale -4e-8 e la potenza 2/n lo gonfiava a 1.3e-3, oltre la
// tolleranza. E non vicini al mezzo: il riflesso di un lato ripiegato
// cadrebbe nell'intorno escluso del punto stesso.
void GLWidget::buildSharedEdgePipeline()
{
    m_sharedEdgeSourceDirty = false;
    releaseSharedEdgePipeline();
    if (m_sharedEdgeSource.isEmpty() || !m_sharedEdgeTex || !m_sharedEdgeParts || !m_sharedEdgeSamples
        || !m_sharedEdgeTiles || !m_sharedEdgeRaw || !m_ubo)
        return;

    QString src = vertexAsComputeLibrary(m_sharedEdgeSource, QStringLiteral("SHARED_EDGE_COMPUTE"),
                                         QStringLiteral("surfaceVertexMain"), QStringLiteral("g_sharedEdgePosition"));
    if (src.isEmpty()) return;
    src.replace(QLatin1String("ubuf.u_meshIndex"), QLatin1String("g_sharedEdgeMesh"));
    const int versionAt = src.indexOf(QLatin1String("#version"));
    src.insert(src.indexOf(QLatin1Char('\n'), versionAt) + 1, QStringLiteral("float g_sharedEdgeMesh = 0.0;\n"));
    src += QStringLiteral(R"(
layout(local_size_x = 64) in;
layout(rgba8, binding = 1) writeonly uniform image2D u_sharedOut;
// In testa (numero di parti, 0, 0, 0); per parte (u_min, u_max, v_min, v_max)
// e (indice della mesh, 0, 0, 0).
layout(std430, binding = 2) readonly buffer SharedEdgeParts { vec4 p[]; } parts;
// Per parte: i campioni dei lati (punto, punto appena dentro), poi la griglia
// dell'interno.
layout(std430, binding = 3) buffer SharedEdgeSamples { vec4 s[]; } samples;
// Per parte e blocco di celle: centro (4D) e raggio della sfera che lo contiene.
layout(std430, binding = 4) buffer SharedEdgeTiles { vec4 t[]; } tiles;
// I flag grezzi della terza passata: per parte e lato, uno per campione; dopo
// RAW_HALF quelli ripuliti dalla quarta, per la quinta.
layout(std430, binding = 5) buffer SharedEdgeRaw { float r[]; } rawFlags;

const int EDGE_SAMPLES = %1;   // intervalli per lato
const int GRID = %2;           // celle per lato del dominio
const int TILE = %3;           // celle per lato di un blocco
const int RAW_HALF = %4 * 4 * (EDGE_SAMPLES + 1);   // parti massime * lati * campioni
// Un lato si nasconde per intero se coperto in ogni campione da SIDE_MARGIN a
// EDGE_SAMPLES - SIDE_MARGIN: ai capi, vicino ai poli, restano scarti innocui
// (la Toupie ha i capi scoperti fino a 12).
const int SIDE_MARGIN = 16;
const int TILES = GRID / TILE;
const int SIDE_VEC4 = 8 * (EDGE_SAMPLES + 1);
const int PART_VEC4 = SIDE_VEC4 + (GRID + 1) * (GRID + 1);
// Passo verso l'interno, in frazione del dominio.
const float EDGE_INWARD = 0.002;

int edgeSlot(int part, int side, int m) { return part * PART_VEC4 + (side * (EDGE_SAMPLES + 1) + m) * 2; }
int gridSlot(int part, int i, int j) { return part * PART_VEC4 + SIDE_VEC4 + i * (GRID + 1) + j; }

// Distanza di p dal triangolo abc (4D) e il punto piu' vicino in coordinate
// del triangolo: (distanza, s, t), punto = a + s (b - a) + t (c - a).
vec3 pointTriangle(vec4 p, vec4 a, vec4 b, vec4 c) {
    vec4 e0 = b - a, e1 = c - a, v = p - a;
    float m00 = dot(e0, e0), m01 = dot(e0, e1), m11 = dot(e1, e1);
    float r0 = dot(v, e0), r1 = dot(v, e1);
    float det = m00 * m11 - m01 * m01;
    if (abs(det) > 1.0e-30) {
        float s = (r0 * m11 - r1 * m01) / det;
        float t = (r1 * m00 - r0 * m01) / det;
        if (s >= 0.0 && t >= 0.0 && s + t <= 1.0) return vec3(length(a + s * e0 + t * e1 - p), s, t);
    }
    vec4 ab = b - a, bc = c - b, ca = a - c;
    float ta = clamp(dot(p - a, ab) / max(dot(ab, ab), 1.0e-30), 0.0, 1.0);
    float tb = clamp(dot(p - b, bc) / max(dot(bc, bc), 1.0e-30), 0.0, 1.0);
    float tc = clamp(dot(p - c, ca) / max(dot(ca, ca), 1.0e-30), 0.0, 1.0);
    vec3 best = vec3(length(a + ta * ab - p), ta, 0.0);
    float db = length(b + tb * bc - p);
    if (db < best.x) best = vec3(db, 1.0 - tb, tb);
    float dc = length(c + tc * ca - p);
    if (dc < best.x) best = vec3(dc, 0.0, 1.0 - tc);
    return best;
}

// Punto della griglia dell'interno della parte, interpolato, alla posizione g
// (in celle) e le sue derivate lungo i e lungo j.
vec4 gridAt(int part, vec2 g, out vec4 di, out vec4 dj, out float diag) {
    int gi = clamp(int(floor(g.x)), 0, GRID - 1), gj = clamp(int(floor(g.y)), 0, GRID - 1);
    vec2 t = clamp(g - vec2(float(gi), float(gj)), 0.0, 1.0);
    vec4 x00 = samples.s[gridSlot(part, gi, gj)];
    vec4 x10 = samples.s[gridSlot(part, gi + 1, gj)];
    vec4 x01 = samples.s[gridSlot(part, gi, gj + 1)];
    vec4 x11 = samples.s[gridSlot(part, gi + 1, gj + 1)];
    di = mix(x10 - x00, x11 - x01, t.y);
    dj = mix(x01 - x00, x11 - x10, t.x);
    diag = max(length(x11 - x00), length(x10 - x01));
    return mix(mix(x00, x10, t.x), mix(x01, x11, t.x), t.y);
}

// Il campione Q di un lato giace sul foglio della parte j? Si parte dal punto
// g della sua griglia (il punto piu' vicino a un campione accanto) e ci si
// avvicina a Q con Gauss-Newton, senza uscire dal dominio: il foglio segue Q
// anche dove le sue celle sono strette in una direzione (le fasce
// dell'Octahedron Bands, attraversate di sbieco da un orlo). Margine come nella
// ricerca delle celle; sulla parte di Q non conta il suo stesso intorno.
bool onSheet(int j, vec2 g, vec4 Q, float margin0, bool ownPart, vec2 ownQ) {
    float tolQ = 1.0e-3 * max(1.0, length(Q));
    for (int it = 0; it < 6; ++it) {
        vec4 di, dj;
        float diag;
        vec4 r = Q - gridAt(j, g, di, dj, diag);
        bool within = length(r) < tolQ + max(0.06 * diag, margin0);
        if (within && !(ownPart && abs(g.x - ownQ.x) < 1.5 && abs(g.y - ownQ.y) < 1.5)) return true;
        float a11 = dot(di, di), a12 = dot(di, dj), a22 = dot(dj, dj);
        float det = a11 * a22 - a12 * a12;
        if (it == 5 || det <= 1.0e-12 * max(a11 * a22, 1.0e-30)) break;
        vec2 d = vec2(a22 * dot(di, r) - a12 * dot(dj, r), a11 * dot(dj, r) - a12 * dot(di, r)) / det;
        g = clamp(g + clamp(d, vec2(-16.0), vec2(16.0)), vec2(0.0), vec2(float(GRID)));
    }
    return false;
}

// PULIZIA DEI TRATTI CORTI (quarta passata). Dove due falde si incontrano con
// un angolo vicino alla soglia, la decisione dipende da dove cade il campione
// nella cella della griglia: sulla S3 Moving Plane erano coperti i campioni sui
// nodi, uno ogni quattro, e il tubo usciva a fette -- anelli che comparivano e
// sparivano con l'animazione. Chiusura poi apertura, lungo il lato: si
// riempiono i buchi piu' corti di SMOOTH campioni fra due coperti, poi si
// tolgono i tratti coperti piu' corti di SMOOTH. Il bordo compare e sparisce a
// tratti interi.
const int SMOOTH = 6;
bool rawAt(int base, int m) { return m >= 0 && m <= EDGE_SAMPLES && rawFlags.r[base + m] > 0.5; }
bool closedAt(int base, int m) {
    if (rawAt(base, m)) return true;
    int dl = 0, dr = 0;
    for (int a = 1; a < SMOOTH && dl == 0; ++a) if (rawAt(base, m - a)) dl = a;
    for (int b = 1; b < SMOOTH && dr == 0; ++b) if (rawAt(base, m + b)) dr = b;
    return dl > 0 && dr > 0 && dl + dr <= SMOOTH;
}
bool smoothedAt(int base, int m) {
    if (!closedAt(base, m)) return false;
    int n = 1;
    for (int a = 1; a < SMOOTH && m - a >= 0 && closedAt(base, m - a); ++a) ++n;
    for (int b = 1; b < SMOOTH && m + b <= EDGE_SAMPLES && closedAt(base, m + b); ++b) ++n;
    return n >= SMOOTH;
}

// P sta sull'orlo (lato `side` della parte), cioe' sulla polilinea dei suoi
// campioni, entro tol?
bool onEdgeChain(int part, int side, vec4 P, float tol) {
    vec4 a = samples.s[edgeSlot(part, side, 0)];
    for (int k = 1; k <= EDGE_SAMPLES; ++k) {
        vec4 b = samples.s[edgeSlot(part, side, k)];
        vec4 d = b - a;
        float x = clamp(dot(P - a, d) / max(dot(d, d), 1.0e-30), 0.0, 1.0);
        if (length(a + x * d - P) < tol) return true;
        a = b;
    }
    return false;
}

// Il campione m di un lato nella griglia della sua parte, in celle.
vec2 ownCell(int side, int m) {
    float f = float(m) / float(EDGE_SAMPLES) * float(GRID);
    return (side == 0) ? vec2(f, 0.0) : (side == 1) ? vec2(f, float(GRID))
         : (side == 2) ? vec2(0.0, f) : vec2(float(GRID), f);
}

void main() {
    int count = int(parts.p[0].x + 0.5);
    int id = int(gl_GlobalInvocationID.x);
    int phase = ubuf.u_isBorder;

    if (phase == 10) {
        // PRIMA PASSATA: un campione per invocazione. Dei lati (0 v = v_min,
        // 1 v = v_max, 2 u = u_min, 3 u = u_max) col punto appena dentro,
        // o della griglia dell'interno. Una sola chiamata alla superficie.
        int perPart = 4 * (EDGE_SAMPLES + 1) + (GRID + 1) * (GRID + 1);
        int part = id / perPart;
        if (part >= count) return;
        int local = id % perPart;
        vec4 d = parts.p[1 + 2 * part];
        g_sharedEdgeMesh = parts.p[2 + 2 * part].x;
        int n = 1;
        vec2 uv[2];
        int slot[2];
        if (local < 4 * (EDGE_SAMPLES + 1)) {
            int side = local / (EDGE_SAMPLES + 1);
            int m = local % (EDGE_SAMPLES + 1);
            float f = float(m) / float(EDGE_SAMPLES);
            n = 2;
            for (int c = 0; c < 2; ++c) {
                float in_ = (c == 0) ? 0.0 : EDGE_INWARD;
                float u = (side < 2) ? mix(d.x, d.y, f) : ((side == 2) ? mix(d.x, d.y, in_) : mix(d.y, d.x, in_));
                float v = (side < 2) ? ((side == 0) ? mix(d.z, d.w, in_) : mix(d.w, d.z, in_)) : mix(d.z, d.w, f);
                uv[c] = vec2(u, v);
                slot[c] = edgeSlot(part, side, m) + c;
            }
        } else {
            int g = local - 4 * (EDGE_SAMPLES + 1);
            int i = g / (GRID + 1);
            int j = g % (GRID + 1);
            uv[0] = vec2(mix(d.x, d.y, float(i) / float(GRID)), mix(d.z, d.w, float(j) / float(GRID)));
            slot[0] = gridSlot(part, i, j);
        }
        for (int c = 0; c < n; ++c) samples.s[slot[c]] = getRawPositionAuto(uv[c].x, uv[c].y);
        return;
    }

    if (phase == 13) {
        // QUARTA PASSATA: un campione del lato per invocazione, dai flag grezzi
        // della terza ripuliti dei tratti corti (smoothedAt), nella texture.
        int part = id / (4 * (EDGE_SAMPLES + 1));
        if (part >= count) return;
        int side = (id / (EDGE_SAMPLES + 1)) % 4;
        int m = id % (EDGE_SAMPLES + 1);
        int base = (part * 4 + side) * (EDGE_SAMPLES + 1);
        int row = int(parts.p[2 + 2 * part].x + 0.5) * 4 + side;
        bool covered = smoothedAt(base, m);
        imageStore(u_sharedOut, ivec2(m, row), vec4(covered ? 1.0 : 0.0));
        rawFlags.r[RAW_HALF + base + m] = covered ? 1.0 : 0.0;
        return;
    }

    if (phase == 14) {
        // QUINTA PASSATA: un lato per invocazione. Coperto per intero (il vertex
        // lo nasconde tutto) se lo e' ogni campione ripulito fuori dai capi; se
        // no si disegna e il fragment ne scarta i tratti coperti. Con tre
        // campioni fissi (47, 100, 211) sulla S3 Moving Plane un lato scoperto
        // per il 15% spariva intero, e ricompariva di colpo quando uno dei tre
        // si scopriva.
        int part = id / 4;
        if (part >= count) return;
        int side = id % 4;
        int base = (part * 4 + side) * (EDGE_SAMPLES + 1);
        bool all = true;
        for (int m = SIDE_MARGIN; m <= EDGE_SAMPLES - SIDE_MARGIN && all; ++m)
            all = rawFlags.r[RAW_HALF + base + m] > 0.5;
        int row = int(parts.p[2 + 2 * part].x + 0.5) * 4 + side;
        imageStore(u_sharedOut, ivec2(EDGE_SAMPLES + 1, row), vec4(all ? 1.0 : 0.0));
        return;
    }

    if (phase == 11) {
        // SECONDA PASSATA: la sfera di ogni blocco di TILE x TILE celle.
        int part = id / (TILES * TILES);
        if (part >= count) return;
        int ti = (id / TILES) % TILES;
        int tj = id % TILES;
        vec4 lo = vec4(1.0e30), hi = vec4(-1.0e30);
        for (int i = ti * TILE; i <= ti * TILE + TILE; ++i)
            for (int j = tj * TILE; j <= tj * TILE + TILE; ++j) {
                vec4 x = samples.s[gridSlot(part, i, j)];
                lo = min(lo, x);
                hi = max(hi, x);
            }
        vec4 c = 0.5 * (lo + hi);
        float r = 0.0;
        for (int i = ti * TILE; i <= ti * TILE + TILE; ++i)
            for (int j = tj * TILE; j <= tj * TILE + TILE; ++j)
                r = max(r, length(samples.s[gridSlot(part, i, j)] - c));
        int slot = (part * TILES * TILES + ti * TILES + tj) * 2;
        tiles.t[slot] = c;
        tiles.t[slot + 1] = vec4(r, 0.0, 0.0, 0.0);
        return;
    }

    // TERZA PASSATA: un campione del lato per invocazione. Coperto se una
    // cella della superficie -- di un'altra parte o della stessa, lontana nel
    // dominio -- passa per il punto e il suo foglio parte in una direzione
    // diversa da quella del lato verso l'interno (nel piano normale al lato).
    int part = id / (4 * (EDGE_SAMPLES + 1));
    if (part >= count) return;
    int side = (id / (EDGE_SAMPLES + 1)) % 4;
    int m = id % (EDGE_SAMPLES + 1);
    vec4 P = samples.s[edgeSlot(part, side, m)];
    vec4 T = samples.s[edgeSlot(part, side, min(m + 1, EDGE_SAMPLES))]
           - samples.s[edgeSlot(part, side, max(m - 1, 0))];
    T = (dot(T, T) > 1.0e-30) ? normalize(T) : vec4(0.0);
    vec4 inward = samples.s[edgeSlot(part, side, m) + 1] - P;
    inward -= dot(inward, T) * T;
    inward = (dot(inward, inward) > 1.0e-30) ? normalize(inward) : vec4(0.0);
    // Tolleranza in proporzione alla distanza dall'origine: l'errore del
    // float cresce con lei.
    float tol = 1.0e-3 * max(1.0, length(P));
    // Il punto nella griglia della sua parte, per escluderne l'intorno.
    vec2 own = ownCell(side, m);
    // Campioni del lato a circa una diagonale di cella prima e dopo (vedi
    // GIACE NEL FOGLIO, sotto): passo medio del lato attorno al punto.
    int mPrev = max(m - 1, 0), mNext = min(m + 1, EDGE_SAMPLES);
    float edgeStep = max(length(samples.s[edgeSlot(part, side, mNext)] - samples.s[edgeSlot(part, side, mPrev)])
                         / float(mNext - mPrev), 1.0e-30);

    // STESSO FOGLIO DEL PUNTO. Una cella della sua parte che passa per il punto
    // non lo copre se ci si arriva restando sulla superficie vicino al punto:
    // lungo la retta del dominio dal punto al punto della cella piu' vicino
    // la superficie (dalla griglia, interpolata) non si allontana piu' di R.
    // Un intorno fisso di celle non bastava: sulla Hyperbolic Saddle, verso il
    // lato u = 8, tanh satura e la superficie e' schiacciata in diagonale nel
    // dominio (verso l'interno in u e insieme lungo v si resta quasi fermi), e
    // celle a qualche passo coprivano il lato. E non il CENTRO della cella:
    // dove cos(2v) = 0 la cella e' alta (z va come u cos 2v) e il suo centro
    // sta lontano, mentre il suo punto vicino al lato e' raggiungibile; col
    // centro l'orlo u = 8 restava tratteggiato. R e' anche tre volte il
    // margine con cui una cella si accetta (tol + 0.06 della diagonale, la
    // curvatura fra i nodi), misurato sulla cella del punto.
    int ci0 = clamp(int(floor(own.x + 0.5)), 0, GRID - 1);
    int cj0 = clamp(int(floor(own.y + 0.5)), 0, GRID - 1);
    float diagP = max(length(samples.s[gridSlot(part, ci0 + 1, cj0 + 1)] - samples.s[gridSlot(part, ci0, cj0)]),
                      length(samples.s[gridSlot(part, ci0 + 1, cj0)] - samples.s[gridSlot(part, ci0, cj0 + 1)]));
    // Piu' quanto la griglia stessa sbaglia il punto: dove il lato e' mal
    // campionato dai nodi (la punta della Kuen, z come log v: la corda fra due
    // nodi passa a 0.16 dal punto vero) la camminata usciva da R al primo passo
    // e la punta, avvolta dal giro accanto, risultava coperta.
    vec4 gdi, gdj;
    float gdiag;
    float R = max(20.0 * tol, 3.0 * (tol + 0.06 * diagP)) + length(gridAt(part, own, gdi, gdj, gdiag) - P);

    // Polo: il lato e' (quasi) un punto.
    float len = 0.0;
    for (int k = 0; k < EDGE_SAMPLES; ++k)
        len += length(samples.s[edgeSlot(part, side, k + 1)] - samples.s[edgeSlot(part, side, k)]);
    bool found = len < tol;
    // CUCITURA qui e nel campione accanto: il lato coincide col lato opposto
    // della parte, diritto (toro) o ritorto (Moebius). Le cuciture di solito
    // le copre gia' l'altro foglio, ma dove la superficie degenera in una linea
    // (Cuspid Koranyi Sphere: per meta' giro di v il raggio e' zero e i lati
    // u = 0 e u = 2 pi corrono sull'asse) non c'e' un altro foglio -- i punti
    // del giro in u coincidono, e' lo stesso -- e il tubo restava dentro la
    // superficie, sporgendo dalle cuspidi. Il campione accanto esclude i lati
    // che si toccano in un punto solo (sul Moebius la fine di v = -1 e'
    // l'inizio di v = +1).
    // Terzo caso (tw = 2): il lato ripiegato su se' stesso, il punto m
    // coincide con N - m (Oloid: lo stesso punto per w e -w). Al centro della
    // piega lo specchio e' il punto stesso e la ricerca delle celle lo prende
    // per lo stesso foglio: decide il campione accanto, che deve coincidere col
    // suo specchio (un altro campione) con l'altro foglio che parte in una
    // direzione diversa, come nella ricerca -- un orlo percorso due volte
    // dallo stesso foglio resta un orlo.
    for (int tw = 0; tw < 3 && !found; ++tw) {
        bool here = false, beside = false;
        for (int dm = -1; dm <= 1; ++dm) {
            int mm = m + dm;
            if (mm < 0 || mm > EDGE_SAMPLES) continue;
            int other = (tw == 0) ? mm : EDGE_SAMPLES - mm;
            vec4 A = samples.s[edgeSlot(part, side, mm)];
            vec4 B = samples.s[edgeSlot(part, (tw == 2) ? side : (side ^ 1), other)];
            bool on = length(A - B) < 1.0e-3 * max(1.0, length(A));
            if (dm == 0) {
                here = on;
                continue;
            }
            if (tw == 2 && on) {
                vec4 ia = samples.s[edgeSlot(part, side, mm) + 1] - A;
                vec4 ib = samples.s[edgeSlot(part, side, other) + 1] - B;
                ia -= dot(ia, T) * T;
                ib -= dot(ib, T) * T;
                on = other != mm && dot(ia, ib) < 0.5 * length(ia) * length(ib);
            }
            beside = beside || on;
        }
        found = here && beside;
    }
    // INCOLLATO ALL'ORLO OPPOSTO con uno scorrimento qualunque: il campione e
    // uno accanto stanno sulla polilinea del lato opposto. Due orli che corrono
    // uno sull'altro sono un'incollatura, qualunque sia l'angolo fra le falde.
    // Sulla S3 Moving Plane i due "poli" v = +-pi/2 sono stirati sullo stesso
    // cerchio massimo, percorso in versi opposti e con uno scorrimento che
    // dipende da t: la superficie e' chiusa, ma le falde vi si incontrano con
    // un angolo che va da 180 a 0 gradi (strozzature), e con la soglia dei 60
    // gradi ne usciva un bordo a frammenti che lampeggiava. Il campione accanto
    // esclude i contatti in un punto (un orlo che ne attraversa un altro, la
    // giunzione degli orli del Moebius).
    if (!found && onEdgeChain(part, side ^ 1, P, tol)) {
        bool beside = false;
        for (int dm = -1; dm <= 1 && !beside; dm += 2) {
            int mm = m + dm;
            if (mm < 0 || mm > EDGE_SAMPLES) continue;
            vec4 A = samples.s[edgeSlot(part, side, mm)];
            beside = onEdgeChain(part, side ^ 1, A, 1.0e-3 * max(1.0, length(A)));
        }
        found = beside;
    }
    // FORELLINO A UN POLO: il lato e' un anello chiuso piu' corto dell'1% della
    // riga piu' lunga della griglia parallela a lui (stessa famiglia di
    // anelli). Il dominio che si ferma appena prima di un polo singolare
    // lasciava un anellino: Heisenberg Sphere (u da 0.005 a pi - 0.005, 0.2%),
    // Berestovskii Sphere (v fino a 6.263, 0.3%), col bordo un puntino e il
    // radio Border acceso su una sfera. Rispetto alla famiglia e non alla
    // superficie: in un tubo sottile aperto gli anelli sono tutti uguali e le
    // estremita' restano orli. Le punte ad ago della Pseudosphere (3.7%)
    // restano col bordo. Le righe della griglia sottostimano la lunghezza:
    // l'errore va verso "non e' un polo".
    vec4 P0 = samples.s[edgeSlot(part, side, 0)];
    if (!found && length(samples.s[edgeSlot(part, side, EDGE_SAMPLES)] - P0) < 1.0e-3 * max(1.0, length(P0))) {
        bool longer = false;
        for (int l = 0; l <= GRID && !longer; ++l) {
            float run = 0.0;
            for (int k = 0; k < GRID; ++k) {
                run += (side < 2) ? length(samples.s[gridSlot(part, k + 1, l)] - samples.s[gridSlot(part, k, l)])
                                  : length(samples.s[gridSlot(part, l, k + 1)] - samples.s[gridSlot(part, l, k)]);
            }
            longer = run > 100.0 * len;
        }
        found = longer;
    }

    for (int j = 0; j < count && !found; ++j) {
        for (int tt = 0; tt < TILES * TILES && !found; ++tt) {
            int tslot = (j * TILES * TILES + tt) * 2;
            float r = tiles.t[tslot + 1].x;
            // Fra due nodi la superficie si scosta dalle celle: margine sul raggio.
            if (length(P - tiles.t[tslot]) > 1.1 * r + tol) continue;
            int ti = tt / TILES, tj = tt % TILES;
            for (int ci = ti * TILE; ci < ti * TILE + TILE && !found; ++ci) {
                for (int cj = tj * TILE; cj < tj * TILE + TILE && !found; ++cj) {
                    float du = float(ci) + 0.5 - own.x, dv = float(cj) + 0.5 - own.y;
                    if (j == part && abs(du) < 1.5 && abs(dv) < 1.5) continue;
                    vec4 x00 = samples.s[gridSlot(j, ci, cj)];
                    vec4 x10 = samples.s[gridSlot(j, ci + 1, cj)];
                    vec4 x01 = samples.s[gridSlot(j, ci, cj + 1)];
                    vec4 x11 = samples.s[gridSlot(j, ci + 1, cj + 1)];
                    float diag = max(length(x11 - x00), length(x10 - x01));
                    vec3 near0 = pointTriangle(P, x00, x10, x01);
                    vec3 near1 = pointTriangle(P, x01, x10, x11);
                    float dist = min(near0.x, near1.x);
                    // Margine: la curvatura fra i nodi (0.06 della diagonale).
                    // Sulla PROPRIA parte non oltre un quarto del passo del lato:
                    // alla punta della Dini's Surface le celle sono lunghe 0.73 (z
                    // va come log v) e il giro accanto della spirale, a 0.023 dal
                    // lato, cadeva nel margine (0.045) -- una tacca nella punta.
                    // Le cuciture e i lembi della stessa parte ci passano sopra
                    // esattamente; fra parti diverse (patch con passi diversi) no.
                    float margin = 0.06 * diag;
                    if (j == part) margin = min(margin, 0.25 * edgeStep);
                    if (dist >= tol + margin) continue;
                    // Il punto della cella piu' vicino, in celle della parte j.
                    vec2 hit = (near0.x <= near1.x) ? vec2(float(ci) + near0.y, float(cj) + near0.z)
                                                    : vec2(float(ci) + near1.y + near1.z, float(cj) + 1.0 - near1.y);
                    if (j == part) {
                        bool same = true;
                        for (int q = 1; q <= 16 && same; ++q) {
                            vec2 g = own + (hit - own) * (float(q) / 16.0);
                            int gi = clamp(int(floor(g.x)), 0, GRID - 1), gj = clamp(int(floor(g.y)), 0, GRID - 1);
                            vec2 t = clamp(g - vec2(float(gi), float(gj)), 0.0, 1.0);
                            vec4 x = mix(mix(samples.s[gridSlot(part, gi, gj)], samples.s[gridSlot(part, gi + 1, gj)], t.x),
                                         mix(samples.s[gridSlot(part, gi, gj + 1)], samples.s[gridSlot(part, gi + 1, gj + 1)], t.x),
                                         t.y);
                            same = length(x - P) < R;
                        }
                        if (same) continue;
                    }
                    // Direzioni dei vertici della cella nel piano normale al
                    // lato, tranne quelli che vi cadono quasi sopra (i vertici
                    // lungo il lato stesso, dove la direzione e' rumore). La
                    // soglia e' relativa al piu' lontano dei quattro, non alla
                    // diagonale: nei tori di Hopf v entra come v + u, la cella
                    // e' inclinata lungo la fibra e la sua parte normale al
                    // lato e' piccola rispetto alla diagonale -- con un quarto
                    // della diagonale si scartavano tutti e quattro e la
                    // cucitura restava col bordo.
                    // Il foglio deve partire ad almeno 60 gradi dalla direzione
                    // verso l'interno: con una soglia stretta (0.995) bastava
                    // la curvatura dell'orlo a far "coprire" il lato dalla riga
                    // di celle subito dentro (le fasce strette dell'Octahedron
                    // Bands, righe a 0.005: tutto il bordo spariva). Cuciture,
                    // giunture e pieghe stanno ben oltre (coseno da -1 a -0.2),
                    // i cinque lembi della Wound Sphere a 72 e 144 gradi.
                    vec4 w4[4];
                    float L4[4];
                    float Lmax = 0.0;
                    for (int c = 0; c < 4; ++c) {
                        vec4 w = ((c == 0) ? x00 : (c == 1) ? x10 : (c == 2) ? x01 : x11) - P;
                        w -= dot(w, T) * T;
                        w4[c] = w;
                        L4[c] = length(w);
                        Lmax = max(Lmax, L4[c]);
                    }
                    bool apart = false;
                    for (int c = 0; c < 4 && !apart; ++c)
                        apart = L4[c] > max(0.25 * Lmax, tol) && dot(w4[c], inward) < 0.5 * L4[c];
                    if (!apart) continue;
                    // GIACE NEL FOGLIO. Un orlo che ATTRAVERSA un altro foglio
                    // resta un orlo: coperto e' solo il lato che vi giace, almeno
                    // da una parte del punto. Si guarda il campione del lato a
                    // circa una diagonale di cella prima e dopo: deve stare sul
                    // foglio della cella (onSheet). Attraversandolo con un angolo
                    // a, a quella distanza se ne scosta di diag sin a, oltre il
                    // margine gia' da 4-5 gradi; giacendovi, ne dista quanto la
                    // curvatura fra i nodi, dentro il margine. Senza, ogni
                    // incrocio lasciava una tacca nel tubo (Enneper in H3, dove
                    // l'orlo passa a 10 gradi dentro l'altra falda; Richmond),
                    // tanto piu' lunga quanto piu' l'incrocio era radente.
                    // Vicino a un capo del lato il campione si ferma al capo; se
                    // ne resta meno di mezzo k, non dice nulla: il capo puo'
                    // essere un punto singolare per cui passano piu' fogli (i due
                    // poli della Spiral Shell, entrambi nell'origine), e giudicato
                    // da li' copriva i primi campioni (una tacca nell'angolo).
                    int k = clamp(int(ceil(diag / edgeStep)), 1, 32);
                    for (int q = 0; q < 2 && !found; ++q) {
                        int mq = (q == 0) ? max(m - k, 0) : min(m + k, EDGE_SAMPLES);
                        if (2 * abs(mq - m) < k) continue;
                        found = onSheet(j, hit, samples.s[edgeSlot(part, side, mq)], 0.06 * diag,
                                        j == part, ownCell(side, mq));
                    }
                }
            }
        }
    }
    // Grezzo: la quarta passata lo ripulisce e scrive la texture.
    rawFlags.r[(part * 4 + side) * (EDGE_SAMPLES + 1) + m] = found ? 1.0 : 0.0;
}
)").arg(kSharedEdgeSamples).arg(kSharedEdgeGrid).arg(kSharedEdgeTileCells).arg(kSharedEdgeMaxParts);

    QShader shader;
    QString error;
    if (!TubeCurveSampler::bakeCompute(rhi(), src, &shader, &error)) {
        qWarning() << "Bordo: passata dei lati coperti non compilata, resta il test del vertex:" << error;
        return;
    }
    m_sharedEdgeSrb = rhi()->newShaderResourceBindings();
    setSharedEdgeBindings();
    m_sharedEdgePipeline = rhi()->newComputePipeline();
    m_sharedEdgePipeline->setShaderResourceBindings(m_sharedEdgeSrb);
    m_sharedEdgePipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, shader));
    if (!m_sharedEdgeSrb->create() || !m_sharedEdgePipeline->create()) {
        qWarning() << "Bordo: pipeline dei lati coperti non creata, resta il test del vertex.";
        releaseSharedEdgePipeline();
    }
}

bool GLWidget::tubeFramesActive() const
{
    return m_tubeFramePipeline && m_tubeFrameSrb && m_tubeFrameWork && engine && m_engineMode == ModeParametric
           && engine->isTubeModeActive() && !engine->isScriptModeActive() && !m_isCustomMesh;
}

void GLWidget::releaseTubeFramePipeline()
{
    if (m_tubeFramePipeline) { m_tubeFramePipeline->destroy(); delete m_tubeFramePipeline; m_tubeFramePipeline = nullptr; }
    if (m_tubeFrameSrb) { m_tubeFrameSrb->destroy(); delete m_tubeFrameSrb; m_tubeFrameSrb = nullptr; }
}

// Il compute e' il vertex del tubo ridotto a libreria: ingressi e uscite
// diventano variabili globali, main e gl_Position spariscono, e in coda va il
// main che trasporta la sezione. Cosi' curva, rotazioni 4D e proiezione sono
// LE STESSE del disegno, senza una seconda copia da tenere allineata.
void GLWidget::buildTubeFramePipeline(const QString &vertexSource)
{
    releaseTubeFramePipeline();
    if (!m_tubeFrameTex || !m_tubeFrameWork || !m_ubo
        || !vertexSource.contains(QLatin1String("#define IS_TUBE"))) return;

    QString src = vertexAsComputeLibrary(vertexSource, QStringLiteral("TUBE_FRAME_COMPUTE"),
                                         QStringLiteral("tubeVertexMain"), QStringLiteral("g_tubeFramePosition"));
    if (src.isEmpty()) return;

    // Un workgroup per tubo.
    // ANELLI DOVE SERVONO. A passo uniforme in u, vicino a una cuspide della
    // curva proiettata (Lissajous Layers Wave, mentre la fase corre) la
    // tangente girava fino a 150 gradi fra due anelli: i quadrilateri fra i
    // due tagliavano la punta, piatta e con una tacca. Infittire non basta:
    // a una cuspide vera la svolta sta tutta in un tratto piccolo a piacere.
    // Percio' la curva si campiona fitta e ogni intervallo pesa quanto il
    // massimo fra la sua parte di passo uniforme e la sua svolta divisa per
    // TUBE_TURN_STEP; gli anelli vanno a pesi uguali. Dove la curva e'
    // tranquilla la distribuzione resta uniforme in u (identica a prima); dove
    // la tangente gira in fretta gli anelli si addensano, e dentro un
    // intervallo la tangente dell'anello e' interpolata sulla sfera fra i due
    // campioni: alla cuspide il centro quasi non si muove, gli anelli ruotano
    // attorno alla punta e la chiudono con una calotta tonda.
    // Poi, in parallelo, per ogni anello la direzione locale (con la sua
    // affidabilita'); un thread solo porta la sezione da un anello al
    // successivo (proiezione sul piano normale: nessuna rotazione attorno alla
    // tangente).
    // CONTINUITA' NEL TEMPO, dallo stato dei frame precedenti (nel buffer di
    // lavoro, azzerato quando cambiano curva o anelli). Gli scatti che restano
    // col solo trasporto vengono dalla curva PROIETTATA: dove forma o scioglie
    // una cuspide (svolte di 100-140 gradi fra due anelli) il trasporto che la
    // attraversa cambia di colpo, e lo scarto di chiusura lo ripartisce su
    // lunghi tratti del tubo. E' una proprieta' della curva, non un errore di
    // calcolo, quindi si toglie dalla vista:
    //  - ogni anello si avvicina alla sezione calcolata di una frazione per
    //    frame (u_tubeFrameBlend, costante di tempo 0.1 s): un salto di 20 gradi
    //    diventa una rotazione fluida di un terzo di secondo. Un limite fisso di
    //    gradi per frame non bastava: nel moto normale la sezione ruota gia' di
    //    6-8 gradi per tick (misurato), quanto i salti piu' piccoli. Il prezzo
    //    e' un piccolo ritardo della texture nel moto, senza artefatti;
    //  - l'ANGOLO DI CHIUSURA prosegue quello di prima invece di essere ripreso
    //    fra -180 e +180 gradi (scatta solo oltre un giro e mezzo): senza, al
    //    passaggio per 180 meta' tubo ruoterebbe in un verso e meta' nell'altro;
    //  - la PARTENZA e' quella di prima riportata sul piano normale di adesso,
    //    avvicinata a poco a poco alla direzione locale quando questa e'
    //    affidabile.
    src += QStringLiteral(R"(
layout(local_size_x = 64) in;
layout(rgba32f, binding = 1) writeonly uniform image2D u_frameOut;
// Per tubo 3 vec4 per anello (tangente + affidabilita', direzione locale + u
// dell'anello, sezione mostrata al frame prima + valida), poi 2 vec4 di stato
// per tubo (partenza + valida, angolo di chiusura + valido), in coda i
// campioni fitti di ogni tubo (tangente + svolta cumulata).
layout(std430, binding = 2) coherent buffer TubeFrameWork { vec4 w[]; } work;
const int TUBE_MAX_RINGS = %1;
const int TUBE_MAX_PARTS = %2;
const int TUBE_MAX_SAMPLES = %3;
const float TUBE_TWO_PI = 6.28318530718;
// Svolta massima di un anello sul successivo, dove gli anelli bastano.
const float TUBE_TURN_STEP = 0.2618;   // 15 gradi
shared vec3 s_first;
shared vec3 s_last;
shared float s_chunk[64];

int tubeRingSlot(int row, int i) { return (row * TUBE_MAX_RINGS + i) * 3; }
int tubeStateSlot(int row) { return TUBE_MAX_PARTS * TUBE_MAX_RINGS * 3 + row * 2; }
int tubeSampleSlot(int row, int j) { return TUBE_MAX_PARTS * (TUBE_MAX_RINGS * 3 + 2) + row * TUBE_MAX_SAMPLES + j; }

vec3 tubeCarry(vec3 r, vec3 T) {
    vec3 q = r - dot(r, T) * T;
    return (dot(q, q) > 1.0e-12) ? normalize(q) : r;
}
vec3 tubeTurn(vec3 r, vec3 T, float a) { return cos(a) * r + sin(a) * cross(T, r); }
float tubeAngle(vec3 from, vec3 to, vec3 T) { return atan(dot(cross(from, to), T), dot(from, to)); }
float tubeSpread(vec3 a, vec3 b) { return atan(length(cross(a, b)), dot(a, b)); }

// Da a verso b sulla sfera, frazione f. Opposte (cuspide vera): un asse
// qualunque perpendicolare, l'anello che gira attorno descrive la stessa sfera.
vec3 tubeSlerp(vec3 a, vec3 b, float f) {
    vec3 k = cross(a, b);
    float s = length(k);
    float ang = atan(s, dot(a, b));
    if (ang < 1.0e-4) return normalize(mix(a, b, f));
    if (s > 1.0e-6) {
        k /= s;
    } else {
        vec3 alt = (abs(a.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        k = normalize(cross(a, alt));
    }
    return cos(f * ang) * a + sin(f * ang) * cross(k, a);
}

void main() {
    int n = int(ubuf.u_tubeRings + 0.5);
    int row = int(ubuf.u_meshIndex + 0.5);
    if (ubuf.u_tubeFramesOn < 0.5 || n < 2 || n > TUBE_MAX_RINGS || row < 0 || row >= TUBE_MAX_PARTS) return;
    float span = ubuf.u_max - ubuf.u_min;
    uint lid = gl_LocalInvocationID.x;

    // 0a. Campioni fitti: sub intervalli per anello, al piu' TUBE_MAX_SAMPLES - 1.
    int sub = clamp((TUBE_MAX_SAMPLES - 1) / (n - 1), 2, 16);
    int m = sub * (n - 1);
    for (int j = int(lid); j <= m; j += 64) {
        vec4 T4;
        work.w[tubeSampleSlot(row, j)] = vec4(tubeProjectedTangent(ubuf.u_min + span * float(j) / float(m), T4), 0.0);
    }
    memoryBarrierBuffer();
    barrier();

    // 0b. Peso cumulato (in .w del campione dopo): blocchi consecutivi per
    //     thread, poi le basi dei blocchi da un thread solo.
    int len = (m + 63) / 64;
    int j0 = int(lid) * len;
    int j1 = min(j0 + len, m);
    float acc = 0.0;
    vec3 ta = (j0 < j1) ? work.w[tubeSampleSlot(row, j0)].xyz : vec3(0.0);
    for (int j = j0; j < j1; ++j) {
        vec3 tb = work.w[tubeSampleSlot(row, j + 1)].xyz;
        acc += max(1.0 / float(sub), tubeSpread(ta, tb) / TUBE_TURN_STEP);
        ta = tb;
    }
    s_chunk[lid] = acc;
    memoryBarrierShared();
    barrier();
    if (lid == 0u) {
        float base = 0.0;
        for (int k = 0; k < 64; ++k) { float c = s_chunk[k]; s_chunk[k] = base; base += c; }
    }
    memoryBarrierShared();
    barrier();
    acc = s_chunk[lid];
    ta = (j0 < j1) ? work.w[tubeSampleSlot(row, j0)].xyz : vec3(0.0);
    for (int j = j0; j < j1; ++j) {
        vec3 tb = work.w[tubeSampleSlot(row, j + 1)].xyz;
        acc += max(1.0 / float(sub), tubeSpread(ta, tb) / TUBE_TURN_STEP);
        ta = tb;
        work.w[tubeSampleSlot(row, j + 1)].w = acc;
    }
    memoryBarrierBuffer();
    barrier();

    // 0c. Anelli a pesi uguali: u e tangente di ognuno, poi la direzione
    //     locale riportata sul piano normale a quella tangente.
    float total = work.w[tubeSampleSlot(row, m)].w;
    for (int i = int(lid); i < n; i += 64) {
        float target = total * float(i) / float(n - 1);
        int lo = 0, hi = m - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (work.w[tubeSampleSlot(row, mid)].w <= target) lo = mid; else hi = mid - 1;
        }
        vec4 a = work.w[tubeSampleSlot(row, lo)];
        vec4 b = work.w[tubeSampleSlot(row, lo + 1)];
        float f = (b.w > a.w) ? clamp((target - a.w) / (b.w - a.w), 0.0, 1.0) : 0.0;
        float u = (i == 0) ? ubuf.u_min
                : (i == n - 1) ? ubuf.u_max
                : ubuf.u_min + span * (float(lo) + f) / float(m);
        vec3 T = tubeSlerp(a.xyz, b.xyz, f);
        vec3 C, Tn, E1;
        vec4 C4, T4;
        float trust;
        tubeLocalFrame(u, C, Tn, E1, C4, T4, trust);
        E1 = tubeNormalTo(E1, T);
        work.w[tubeRingSlot(row, i)] = vec4(T, trust);
        work.w[tubeRingSlot(row, i) + 1] = vec4(E1, u);
        if (i == 0) s_first = C;
        if (i == n - 1) s_last = C;
    }
    memoryBarrierBuffer();
    memoryBarrierShared();
    barrier();
    if (lid != 0u) return;

    // 1. Partenza continua nel tempo.
    vec4 t0 = work.w[tubeRingSlot(row, 0)];
    vec3 T0 = t0.xyz;
    vec3 local0 = work.w[tubeRingSlot(row, 0) + 1].xyz;
    vec4 st = work.w[tubeStateSlot(row)];
    vec3 start = local0;
    if (st.w > 0.5) {
        vec3 prev = tubeCarry(st.xyz, T0);
        float pull = 0.15 * smoothstep(0.02, 0.25, t0.w);
        start = normalize(tubeTurn(prev, T0, pull * tubeAngle(prev, local0, T0)));
    }
    work.w[tubeStateSlot(row)] = vec4(start, 1.0);

    // 2. Giro di prova: dove arriva la sezione, e quanto manca per richiudersi.
    vec3 r = start;
    for (int i = 1; i < n; ++i) r = tubeCarry(r, work.w[tubeRingSlot(row, i)].xyz);
    vec4 sp = work.w[tubeStateSlot(row) + 1];
    float phi = 0.0;
    if (length(s_last - s_first) < 1.0e-4 * (1.0 + length(s_first))) {
        float p = tubeAngle(r, start, T0);
        phi = p;
        if (sp.y > 0.5) {
            float c = p + TUBE_TWO_PI * round((sp.x - p) / TUBE_TWO_PI);
            if (abs(c) <= 1.5 * TUBE_TWO_PI) phi = c;
        }
    }
    work.w[tubeStateSlot(row) + 1] = vec4(phi, 1.0, 0.0, 0.0);

    // 3. Giro vero, con lo scarto ripartito lungo la curva; ogni anello si
    //    avvicina alla sezione calcolata di u_tubeFrameBlend.
    r = start;
    for (int i = 0; i < n; ++i) {
        vec3 T = work.w[tubeRingSlot(row, i)].xyz;
        if (i > 0) r = tubeCarry(r, T);
        vec3 target = tubeTurn(r, T, phi * float(i) / float(n - 1));
        vec4 last = work.w[tubeRingSlot(row, i) + 2];
        vec3 shown = target;
        if (last.w > 0.5) {
            vec3 before = tubeCarry(last.xyz, T);
            shown = tubeTurn(before, T, ubuf.u_tubeFrameBlend * tubeAngle(before, target, T));
        }
        work.w[tubeRingSlot(row, i) + 2] = vec4(shown, 1.0);
        imageStore(u_frameOut, ivec2(2 * i, row), vec4(shown, work.w[tubeRingSlot(row, i) + 1].w));
        imageStore(u_frameOut, ivec2(2 * i + 1, row), vec4(T, 0.0));
    }
}
)").arg(kTubeFrameMaxRings).arg(kTubeFrameMaxParts).arg(kTubeFrameMaxSamples);

    QShader shader;
    QString error;
    if (!TubeCurveSampler::bakeCompute(rhi(), src, &shader, &error)) {
        qWarning() << "Tubi: passata della sezione non compilata, resta l'orientamento locale:" << error;
        return;
    }
    m_tubeFrameSrb = rhi()->newShaderResourceBindings();
    m_tubeFrameSrb->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0, QRhiShaderResourceBinding::ComputeStage, m_ubo, sizeof(UboData)),
        QRhiShaderResourceBinding::imageStore(1, QRhiShaderResourceBinding::ComputeStage, m_tubeFrameTex, 0),
        QRhiShaderResourceBinding::bufferLoadStore(2, QRhiShaderResourceBinding::ComputeStage, m_tubeFrameWork)
    });
    // Curva nuova: lo stato dei frame precedenti non le appartiene.
    m_tubeFrameStateReset = true;
    m_tubeFramePipeline = rhi()->newComputePipeline();
    m_tubeFramePipeline->setShaderResourceBindings(m_tubeFrameSrb);
    m_tubeFramePipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, shader));
    if (!m_tubeFrameSrb->create() || !m_tubeFramePipeline->create()) {
        qWarning() << "Tubi: pipeline della sezione non creata, resta l'orientamento locale.";
        releaseTubeFramePipeline();
    }
}

// ==========================================================
// IMMAGINI PROPRIE DELLE FASCE
// ==========================================================

QString GLWidget::imagePathInTextureCode(const QString &code)
{
    static const QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)",
                                          QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = imgRe.match(code);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

const GLWidget::PartImage *GLWidget::partImageFor(const MeshPart &p) const
{
    // In ambito "All" l'aspetto per-fascia e' sospeso: vale la superficie.
    if (m_meshAppearanceUniform || !p.hasCustomTexture) return nullptr;
    const QString path = imagePathInTextureCode(p.textureCode);
    if (path.isEmpty()) return nullptr;
    const auto it = m_partImages.constFind(path);
    return (it != m_partImages.constEnd() && it->tex && it->srb) ? &it.value() : nullptr;
}

// La fascia attiva ha un'immagine da campionare? La sua (nominata e non
// risultata illeggibile) oppure, in mancanza, quella della superficie. Serve al
// DISPLAY dei picker Colore: senza immagine gli script che campionano iChannel0
// mostrano la scacchiera di ripiego, che e' fatta di u_col1/u_col2.
bool GLWidget::activeMeshSamplesImage() const
{
    if (!m_surfaceImagePath.isEmpty()) return true;
    if (m_activeMeshPart < 0 || !engine) return false;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return false;
    const MeshPart &p = parts[m_activeMeshPart];
    if (!p.hasCustomTexture) return false;
    const QString path = imagePathInTextureCode(p.textureCode);
    if (path.isEmpty()) return false;
    const auto it = m_partImages.constFind(path);
    // Non ancora caricata (lo fa il prossimo frame) conta come presente.
    return it == m_partImages.constEnd() || !it->failed;
}

// L'immagine propria della fascia `index` ("" se non ne ha una caricata): per
// i test, che verificano cio' che la GPU ha davvero in mano.
QString GLWidget::meshPartLoadedImagePath(int index) const
{
    if (!engine) return QString();
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (index < 0 || index >= (int)parts.size()) return QString();
    return partImageFor(parts[index]) ? imagePathInTextureCode(parts[index].textureCode)
                                      : QString();
}

QRhiShaderResourceBindings *GLWidget::bindingsForPart(const MeshPart &p) const
{
    if (const PartImage *img = partImageFor(p)) return img->srb;
    return m_bindingsDyn ? m_bindingsDyn : m_bindings;
}

// Allinea le texture GPU alle immagini nominate dalle fasce: carica quelle
// nuove, distrugge quelle che nessuna fascia nomina piu'. Nel frame, dove le
// risorse RHI si possono creare e distruggere.
void GLWidget::syncPartImages(QRhiResourceUpdateBatch *resourceUpdates)
{
    if (!rhi() || !engine || !m_ubo || !m_sampler) return;

    // Le immagini in uso. Contano anche le fasce con la texture SPENTA: lo
    // script (e il suo tag) restano, e riaccendendola l'immagine e' gia' li'.
    // STESSO FILE DELLA SUPERFICIE: non si carica due volte. La fascia disegna
    // coi binding della superficie, che allo slot 1 hanno gia' quell'immagine
    // (e' il caso dei record salvati prima di questa feature, dove il tag della
    // fascia ripeteva quello della superficie). Se la superficie cambia
    // immagine i percorsi non coincidono piu' e la fascia carica la propria.
    QSet<QString> used;
    for (const MeshPart &p : engine->getMeshParts()) {
        if (!p.hasCustomTexture) continue;
        const QString path = imagePathInTextureCode(p.textureCode);
        if (!path.isEmpty() && path != m_surfaceImagePath) used.insert(path);
    }
    // L'immagine della texture del BORDO vive nella stessa cache: stessi
    // binding, e una sola copia se coincide con quella di una fascia.
    if (m_borderRadius > 0.0f) {
        const QString path = imagePathInTextureCode(m_borderTexCode);
        if (!path.isEmpty() && path != m_surfaceImagePath) used.insert(path);
    }

    for (auto it = m_partImages.begin(); it != m_partImages.end(); ) {
        if (used.contains(it.key())) { ++it; continue; }
        if (it->srb) { it->srb->destroy(); delete it->srb; }
        if (it->tex) { it->tex->destroy(); delete it->tex; }
        it = m_partImages.erase(it);
    }

    for (const QString &path : used) {
        if (m_partImages.contains(path)) continue;
        PartImage entry;
        QImage img(path);
        if (img.isNull()) {
            // Immagine non caricabile: la fascia ricade su quella della
            // SUPERFICIE (o sulla scacchiera, se non c'e' nemmeno quella), come
            // prima che le fasce potessero averne una propria. Si prova una
            // volta sola, finche' il tag non cambia.
            // Il file MANCA (record aperto su un altro dispositivo): in
            // silenzio. Il file c'e' ma non e' un'immagine: si avvisa, come
            // loadTextureFromFile.
            qWarning() << "GLWidget: immagine della fascia non caricabile:" << path;
            entry.failed = true;
            m_partImages.insert(path, entry);
            if (QFileInfo::exists(path)) emit textureImageLoadFailed(path);
            continue;
        }
        // Stesso formato e stesso verso dell'immagine di superficie
        // (loadTextureFromImage).
        img = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));
        entry.tex = rhi()->newTexture(QRhiTexture::RGBA8, img.size(), 1);
        entry.tex->create();
        QRhiTextureSubresourceUploadDescription subresDesc(img.constBits(), img.sizeInBytes());
        resourceUpdates->uploadTexture(entry.tex,
                                       QRhiTextureUploadDescription({ QRhiTextureUploadEntry(0, 0, subresDesc) }));

        // Stesso LAYOUT di m_bindingsDyn (UBO con dynamic offset allo slot 0,
        // texture allo slot 1): le pipeline costruite su quello accettano
        // questi binding al draw.
        entry.srb = rhi()->newShaderResourceBindings();
        entry.srb->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubo, sizeof(UboData)),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, entry.tex, m_sampler),
            tubeFramesBinding(),
            borderEdgesBinding(),
            sharedEdgesBinding()
        });
        entry.srb->create();
        m_partImages.insert(path, entry);
    }
}

// L'UBO e' stato ricreato: i binding delle immagini di fascia puntavano al
// vecchio buffer (stessa trappola di m_bindingsDyn in ensureUboCapacity).
void GLWidget::rebindPartImages()
{
    for (auto it = m_partImages.begin(); it != m_partImages.end(); ++it) {
        if (!it->srb || !it->tex) continue;
        it->srb->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubo, sizeof(UboData)),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, it->tex, m_sampler),
            tubeFramesBinding(),
            borderEdgesBinding(),
            sharedEdgesBinding()
        });
        it->srb->updateResources();
    }
}

void GLWidget::releasePartImages()
{
    for (auto it = m_partImages.begin(); it != m_partImages.end(); ++it) {
        if (it->srb) { it->srb->destroy(); delete it->srb; }
        if (it->tex) { it->tex->destroy(); delete it->tex; }
    }
    m_partImages.clear();
}

void GLWidget::initialize(QRhiCommandBuffer *cb)
{
    if (m_ubo) {
        return;
    }

    // --- 1. CREAZIONE UBO (Uniform Buffer Object) ---
    // MULTI-MESH: l'UBO non contiene piu' un blocco solo ma un ARRAY di blocchi,
    // uno per parte di mesh (i limiti u_min/u_max/v_min/v_max e u_meshIndex sono
    // per-parte). Ogni draw call seleziona il suo blocco con un dynamic offset.
    // Lo stride e' allineato secondo l'API grafica: ubufAlignment() vale 256 su
    // Metal/Vulkan, quindi NON si puo' usare sizeof(UboData) come passo.
    m_uboBlockStride = rhi()->ubufAlignment();
    while (m_uboBlockStride < sizeof(UboData)) {
        m_uboBlockStride += rhi()->ubufAlignment();
    }
    ensureUboCapacity(kInitialUboBlocks);

    // --- 2. CREAZIONE VBO E IBO ---
    // In RHI è meglio pre-allocare un buffer grande per evitare di ricrearlo a ogni frame.
    // 500.000 vertici sono un limite di sicurezza abbondante per la tua griglia.
    int maxVertexSize = 500000 * sizeof(Vertex);
    int maxIndexSize = 1000000 * sizeof(unsigned int);

    m_vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, maxVertexSize);
    m_vbo->create();

    m_ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, maxIndexSize);
    m_ibo->create();

    m_wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, maxIndexSize);
    m_wireframeIbo->create();

    // Bordo: piccolo, cresce al primo upload che non ci sta.
    m_borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 65536 * sizeof(Vertex));
    m_borderVbo->create();
    m_borderIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, 262144 * sizeof(unsigned int));
    m_borderIbo->create();
    m_borderWireIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, 65536 * sizeof(unsigned int));
    m_borderWireIbo->create();
    m_borderUploaded = false;

    // --- 3. CREAZIONE TEXTURE E BINDINGS ---
    // A. Creiamo la texture "tappabuchi" (scacchiera: serve il cb per l'upload)
    createDummyTexture(cb);
    // Prima dei binding della superficie, che le referenziano agli slot 2-4.
    createTubeFrameResources();
    createBorderEdgeResources();
    createSharedEdgeResources();

    // B. Colleghiamo l'UBO al binding 0 e la TEXTURE al binding 1
    m_bindings = rhi()->newShaderResourceBindings();
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
                                                 QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubo),
        QRhiShaderResourceBinding::sampledTexture(1,
                                                  QRhiShaderResourceBinding::FragmentStage,
                                                  m_dummyTexture, m_sampler)
    });
    m_bindings->create();

    // C. Binding della superficie con dynamic offset (multi-mesh). Va creato
    // PRIMA di buildPipeline(): le pipeline parametriche si costruiscono su
    // questo layout, e il layout dichiarato dalla pipeline deve combaciare con
    // quello passato a setShaderResources al draw.
    ensureDynamicBindings(m_dummyTexture);

    // Inizializzazioni di base dell'engine che avevi prima
    if (m_eqX.isEmpty()) {
        m_eqX = "sin(u)"; m_eqY = "cos(u)"; m_eqZ = "v"; m_eqW = "0";
        engine->setEquations(m_eqX, m_eqY, m_eqZ, m_eqW);
    }

    buildPipeline();
}

void GLWidget::render(QRhiCommandBuffer *cb)
{
    // ==========================================================
    // WATCHDOG DI PERFORMANCE (avviso da rallentamento)
    // ==========================================================
    // Misuriamo l'intervallo tra frame consecutivi: quando un'animazione e'
    // attiva ma il throughput resta sotto ~13 fps abbastanza a lungo, il carico
    // GPU e' eccessivo e avvisiamo l'utente (una sola volta). Non modifichiamo la
    // qualita' (l'adaptive su raySteps fu provata e rimossa perche' peggiorava):
    // qui solo avviso. Si misura SOLO con animazione attiva, altrimenti gli
    // intervalli non rappresentano un throughput continuo.
    {
        // Durante l'esportazione video il rendering e' frame-by-frame (cattura +
        // scrittura su disco tra i frame): gli intervalli sono lentissimi per
        // costruzione, non per carico GPU. Saltiamo il watchdog per non dare un
        // falso avviso "il rendering rallenta" durante la registrazione.
        const bool animating = !m_isRecording &&
                               (isAnimating() || m_surfaceAnimating || m_pathAnimating);
        if (animating) {
            // Il PRIMO frame dopo l'avvio (o ri-avvio) dell'animazione non e' una
            // misura valida: l'intervallo dal frame precedente include il tempo da
            // FERMI (click di start, pausa UI), non il costo GPU. Misurarlo dava un
            // dtMs enorme che inquinava l'EMA e faceva scattare un FALSO avviso
            // proprio allo start/stop di preset scorrevoli. Lo saltiamo.
            const bool justResumed = !m_wasAnimating;
            // Finestra di grazia post-export: entro kPerfGraceMs dal ritorno live
            // (m_perfGraceClock avviato in setRecordingActive(false)) scartiamo le
            // misure — il transitorio di ripristino puo' durare piu' frame lenti
            // su mobile e NON e' carico GPU. Scaduta la finestra, la fermiamo
            // (invalidate) cosi' non ricade mai piu' in questo ramo.
            constexpr qint64 kPerfGraceMs = 500;
            const bool inGrace = m_perfGraceClock.isValid() &&
                                 m_perfGraceClock.elapsed() < kPerfGraceMs;
            if (!inGrace && m_perfGraceClock.isValid()) m_perfGraceClock.invalidate();
            if (m_frameClock.isValid() && !justResumed && !inGrace) {
                float dtMs = (float)m_frameClock.nsecsElapsed() / 1.0e6f;

                // Soglia per piattaforma: su Android il watchdog del driver GPU
                // puo' uccidere l'app prima dell'avviso, quindi avvisiamo PRIMA
                // (2 fps); su desktop, dove non c'e' kill, scendiamo a ~1.5 fps.
#if defined(Q_OS_ANDROID)
                constexpr float kSlowFrameMs = 500.0f;  // ~2 fps
#else
                constexpr float kSlowFrameMs = 667.0f;  // ~1.5 fps
#endif
                constexpr float kSlowDwellMs = 600.0f;  // sostenuto per >0.6 s
                constexpr int   kSlowRunToWarn = 2;     // campioni lenti consecutivi minimi
                                                        // per avvisare: e' il guard che
                                                        // conserva l'immunita' ai picchi
                                                        // isolati con l'EMA asimmetrica
                // Oltre questo, un dtMs NON e' piu' classificabile come frame lento
                // vs. buco da inattivita' guardando il solo valore: serve il contesto
                // (isolato vs. sequenza). Vedi ramo "frame enorme" sotto.
                constexpr float kHugeFrameMs   = 2000.0f;
                constexpr int   kHugeRunToWarn = 3;      // N frame enormi consecutivi
                                                         // = collasso reale, non un buco.
                                                         // 3 (non 2): il ritorno da
                                                         // app-in-background produce 1-2
                                                         // frame enormi (ricompilazione/
                                                         // riscaldamento), non 3+ di fila.

                if (dtMs <= kHugeFrameMs) {
                    m_hugeFrameRun = 0;  // frame "normale": interrompe una sequenza enorme
                    m_slowFrameRun = (dtMs > kSlowFrameMs) ? m_slowFrameRun + 1 : 0;

                    // EMA ASIMMETRICA: in salita (campione sopra soglia) peso 0.35,
                    // cosi' un rallentamento vero viene agganciato in 2-3 frame e
                    // l'avviso precede (o quasi) gli artefatti da budget GPU
                    // (magenta/tremolii), invece di arrivare dopo ~5 s di media
                    // pigra; in discesa resta 0.15 (recupero morbido). ATTENZIONE:
                    // con 0.35 un SINGOLO picco fin quasi a 2 s porta l'EMA oltre
                    // soglia da solo (0.35*2000 = 700 > kSlowFrameMs): l'immunita'
                    // ai picchi isolati non la da' piu' l'aritmetica dell'EMA
                    // (0.15*2000 = 300 la garantiva), la da' m_slowFrameRun nella
                    // condizione di avviso (>= kSlowRunToWarn campioni lenti DI FILA).
                    const float emaW = (dtMs > kSlowFrameMs) ? 0.35f : 0.15f;
                    m_avgFrameMs = (1.0f - emaW) * m_avgFrameMs + emaW * dtMs;

                    if (m_avgFrameMs > kSlowFrameMs) {
                        m_slowAccumMs += dtMs;
                        // UN SOLO avviso per scena: dopo il primo, m_perfWarnDismissed
                        // (settato dal connect via acknowledgePerformanceWarning) zittisce
                        // TUTTO — anche un ulteriore peggioramento — finche' non cambiano
                        // le impostazioni. Il riarmo avviene SOLO al reale cambio
                        // scena/effetti, in rebuildShader() (azzera dismissed e level).
                        // Ne' lo stop/riavvio dei moti ne' l'oscillazione attorno alla
                        // soglia riarmano: e' cio' che elimina il vecchio popup "random".
                        // firstTime resta la condizione del PRIMO avviso di una scena.
                        const bool firstTime = (m_perfWarnLevelMs <= 0.0f);
                        if (!m_perfWarnDismissed && m_slowAccumMs >= kSlowDwellMs
                            && m_slowFrameRun >= kSlowRunToWarn && firstTime) {
                            m_perfWarnLevelMs = m_avgFrameMs;  // livello mostrato
                            emit performanceWarning();
                        }
                    } else {
                        // Tornati sopra soglia (fluidi): svuotiamo solo l'accumulo
                        // di dwell. NON azzeriamo m_perfWarnLevelMs qui: una scena
                        // che OSCILLA intorno alla soglia (fluida->lenta->fluida)
                        // non deve riavvisare a ogni ricaduta. Il guard
                        // !m_perfWarnDismissed sopra gia' zittisce dopo il primo
                        // avviso; il livello si riarma solo al reale cambio scena
                        // (rebuildShader azzera level e dismissed insieme).
                        m_slowAccumMs = 0.0f;
                    }
                } else {
                    // dtMs ENORME (> 2 s). Un frame isolato di questa entita' e' un
                    // buco da inattivita' (timer in pausa, finestra nascosta) e va
                    // scartato — NON inquiniamo l'EMA con 5000 ms. MA una SEQUENZA di
                    // frame enormi consecutivi NON e' inattivita': e' un collasso GPU
                    // reale (caso iPad: RM + trasparenza a ~0.2 fps, ogni frame ~5 s).
                    // Su desktop lo stesso record rende frame < 2 s e cade nel ramo
                    // sopra, quindi li' l'avviso scattava gia'; su iPad TUTTI i frame
                    // sforavano i 2 s e venivano scartati -> il watchdog restava muto.
                    // Contiamo i consecutivi: al kHugeRunToWarn-esimo, e' collasso.
                    // Un frame enorme e' comunque un campione LENTO: alimenta anche
                    // m_slowFrameRun, cosi' le sequenze miste (1.8 s, 2.5 s, 1.8 s...)
                    // non azzerano l'evidenza del ramo EMA a ogni sforamento dei 2 s.
                    ++m_slowFrameRun;
                    if (++m_hugeFrameRun >= kHugeRunToWarn) {
                        const bool firstTime = (m_perfWarnLevelMs <= 0.0f);
                        if (!m_perfWarnDismissed && firstTime) {
                            m_perfWarnLevelMs = dtMs;  // livello indicativo (frame enorme)
                            emit performanceWarning();
                        }
                    }
                } // chiude: if (dtMs <= kHugeFrameMs) / else
            }
            m_wasAnimating = true;
        } else {
            // Animazione ferma: azzeriamo SOLO gli accumulatori di misura, cosi'
            // la prossima animazione riparte da uno stato pulito. NON riarmiamo
            // m_perfWarnDismissed: fermare/riavviare la STESSA scena (o l'export
            // che ferma+riavvia i timer) non deve far ricomparire il popup. Il
            // riarmo avviene solo su reale cambio scena/effetti, in
            // rebuildShader(). ("Un avviso per scena finche' non cambiano le
            // impostazioni.")
            m_slowAccumMs = 0.0f;
            m_avgFrameMs = 16.0f;
            m_perfWarnLevelMs = 0.0f;
            m_slowFrameRun = 0;
            m_hugeFrameRun = 0;
            m_wasAnimating = false;
        }
        m_frameClock.restart();
    }

    // ==========================================================
    // PARTE COMUNE (SEMPRE IN ESECUZIONE)
    // ==========================================================
    QRhiResourceUpdateBatch *resourceUpdates = rhi()->nextResourceUpdateBatch();

    // --- CALCOLO MATRICI E TELECAMERA ---
    QSize outputSize = renderTarget()->pixelSize();
    float aspect = (float)outputSize.width() / (float)(outputSize.height() > 0 ? outputSize.height() : 1);

    QMatrix4x4 mvp;
    QMatrix4x4 mv;

    if (m_isFlatView) {
        m_projection.setToIdentity();
        m_view.setToIdentity();
        m_model.setToIdentity();
        mvp = rhi()->clipSpaceCorrMatrix();
        mv.setToIdentity();
    } else {
        m_projection.setToIdentity();
        // Piani near/far scalati con la distanza della camera. Un far fisso a
        // 100 tagliava la parte lontana di oggetti grandi quando si rimpicciolisce
        // (la camera arretra oltre i 100). Scalando NEAR e FAR insieme il rapporto
        // far/near resta costante (~10000, come l'originale 0.01:100), quindi la
        // precisione del depth buffer non peggiora (niente z-fighting/appiattimento)
        // e a camDist=4 si riottengono esattamente i valori storici.
        float camDist = m_cameraPos.length();
        if (camDist < 0.1f) camDist = 4.0f;
        const float nearPlane = camDist * 0.0025f;
        const float farPlane  = camDist * 25.0f;
        if (projectionMode == Ortho4D) {
            float halfHeight = camDist * std::tan(m_cameraFov * 0.5f * M_PI / 180.0f);
            float halfWidth = halfHeight * aspect;
            // NEAR SIMMETRICO IN ORTOGONALE (-far invece di nearPlane).
            // In prospettiva la camera e' un punto e il cono si stringe verso di
            // lei: cio' che sta prima del near e' poco o fuori campo. In
            // ortogonale i raggi sono PARALLELI e il volume e' un box a sezione
            // costante: tutto cio' che sta prima del near viene tagliato in
            // pieno. Con una camera vicina alla superficie (i record di path la
            // salvano DENTRO l'oggetto: Klein3D Racing a distanza 1.06) meta'
            // figura ha z negativo e spariva, mentre lo stesso preset caricato
            // dal ramo Surface (camera a 4.0) si vedeva intero.
            // Estendendo il box all'indietro fino a -farPlane la camera si
            // comporta come una lastra: nessun taglio, da qualunque distanza.
            //
            // COSTO SUL DEPTH BUFFER: in ortogonale z_ndc e' LINEARE in z_eye,
            // quindi conta solo l'AMPIEZZA (far-near), non il rapporto far/near
            // che governa il caso prospettico citato qui sopra. Raddoppiando
            // l'ampiezza la risoluzione si dimezza: a camDist=4 si passa da
            // 6.0e-06 a 1.2e-05 unita' per livello su depth a 24 bit. Margine
            // ampio; il ramo prospettico resta invariato.
            m_projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -farPlane, farPlane);
        } else {
            m_projection.perspective(m_cameraFov, aspect, nearPlane, farPlane);
        }

        m_view.setToIdentity();
        if (m_isPathFollowing) {
            m_view.lookAt(m_cameraPos, m_pathTarget, m_pathUp);
            if (m_pathRoll != 0.0f) {
                QMatrix4x4 rollMat;
                rollMat.rotate(m_pathRoll, 0.0f, 0.0f, 1.0f);
                m_view = rollMat * m_view;
            }
        } else {
            float radYaw = m_cameraYaw * M_PI / 180.0f;
            float radPitch = m_cameraPitch * M_PI / 180.0f;
            QVector3D front(std::sin(radYaw)*std::cos(radPitch), std::sin(radPitch), -std::cos(radYaw)*std::cos(radPitch));
            QMatrix4x4 rollMat;
            rollMat.rotate(m_cameraRoll, 0.0f, 0.0f, 1.0f);
            QVector3D up = rollMat.map(QVector3D(0.0f, 1.0f, 0.0f));
            m_view.lookAt(m_cameraPos, m_cameraPos + front, up);
        }

        m_model.setToIdentity();
        m_model.rotate(m_rotationQuat);

        mvp = rhi()->clipSpaceCorrMatrix() * m_projection * m_view * m_model;
        mv = m_view * m_model;
    }

    // --- 3. COMPILAZIONE UNIFORM BUFFER (UBO) ---
    memcpy(m_uboData.mvpMatrix, mvp.constData(), 64);
    memcpy(m_uboData.mvMatrix, mv.constData(), 64);
    memcpy(m_uboData.mMatrix, m_model.constData(), 64);

    m_uboData.dummyZero = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    m_uboData.observerPos = m_observerPos;
    m_uboData.cameraPos4D = m_cameraPos4D;
    m_uboData.omega = omega;
    m_uboData.phi = phi;
    m_uboData.psi = psi;

    // 1. CALCOLO DEL DELTA TIME (dt) GLOBALE
    float currentRealTime = (float)m_surfaceTimer.elapsed() / 1000.0f;
    float dt = currentRealTime - m_lastRealTime;
    if (dt < 0) dt = 0; // Protezione al riavvio del timer
    m_lastRealTime = currentRealTime;

    // 2. OROLOGI: in uso normale avanzano qui, col dt reale. In registrazione
    // li avanza il loop del recorder (advanceClocksBy col dt del frame): il
    // frame i mostra cio' che lo schermo mostrerebbe al tempo equivalente, e
    // un modulo fermo resta fermo in entrambi i casi, senza congelamenti.
    // m_lastRealTime si aggiorna comunque (sopra): a fine REC il primo dt
    // live e' quello di un frame, non la durata della registrazione.
    if (!m_clocksDrivenByRecorder) advanceClocksBy(dt);

    // 3. INVIO DEI DATI ALLA GPU
    const ClockTimes clocks = clockTimes();
    m_uboData.time = clocks.geom;

    // Usiamo la coordinata X di dummyZero per inviare il tempo specifico della Texture
    m_uboData.dummyZero.setX(clocks.tex);

    // .y = flag "seconda superficie interna" (Inner:= nello script ray marching).
    // .x resta l'orologio texture; .y/.z/.w erano liberi (azzerati a inizio frame).
    m_uboData.dummyZero.setY(m_raymarchHasInner ? 1.0f : 0.0f);
    // .z = quota del piano di sezione (Cross Section). Si riusa dummyZero invece
    // di aggiungere un campo all'UBO: un campo nuovo andrebbe dichiarato in
    // ENTRAMBI gli shader o Adreno rifiuta il link (vedi CLAUDE.md), e qui basta
    // una componente gia' presente e inutilizzata.
    m_uboData.dummyZero.setZ(m_crossSectionP);
    m_uboData.projMode = projectionMode;
    m_uboData.renderMode = renderMode;
    m_uboData.lightingMode = is4DActive() ? m_lightingMode4D : 0;
    m_uboData.useSpecular = m_isSpecularEnabled ? 1 : 0;

    m_uboData.isFlat = m_isFlatView ? 1 : 0;
    // TRASFORMAZIONE 2D GLOBALE (non il buffer di lavoro della vista 2D): e' la
    // base di OGNI blocco parte, quindi ci va lo stato globale, o l'inquadratura
    // che il mouse sta dando alla fascia in editing si propagherebbe a tutte le
    // fasce che ereditano e all'ambito "All".
    // In vista 2D sulla SUPERFICIE fa eccezione cio' che si sta editando: li' il
    // fragment deve seguire il mouse.
    //  - con una FASCIA selezionata ci pensa il ramo editingThisPart del loop
    //    per-parte piu' sotto, che scrive il buffer nel blocco di quella parte;
    //  - con una mesh SOLA (nessuna parte dichiarata) quel loop non esiste;
    //  - in ambito "ALL" (nessuna parte attiva) nessuna parte e' "in editing",
    //    e il quad 2D disegna col blocco 0.
    // Negli ultimi due casi il buffer va messo QUI, o trascinare non muove nulla.
    const bool flatEditingGlobal = m_isFlatView && m_flatViewTarget == 0
                                   && (!engine || engine->getMeshPartCount() <= 1
                                       || m_activeMeshPart < 0);
    m_uboData.zoom     = flatEditingGlobal ? m_flatZoom     : m_globalTexZoom;
    m_uboData.center   = flatEditingGlobal ? m_flatPan      : m_globalTexPan;
    m_uboData.rotation = flatEditingGlobal ? m_flatRotation : m_globalTexRotation;

    if (engine) {
        m_uboData.hasExplicitW = (!engine->getActiveExplicitEquation().isEmpty()) ? 1 : 0;
    } else {
        m_uboData.hasExplicitW = 0;
    }

    m_uboData.u_raySteps = m_raySteps;

    if (m_textureEnabled && m_engineMode == ModeParametric) {
        m_uboData.color = QVector3D(1.0f, 1.0f, 1.0f);
    } else {
        m_uboData.color = QVector3D(red, green, blue);
    }

    // FALLBACK ANTI-SPARIZIONE: su un campo implicito mal condizionato (prodotto,
    // es. "Chain") il ramo trasparente aggancia crossing fantasma e la superficie
    // sparirebbe con alpha<1. Forziamo opaco: il cammino opaco (d=val/gradLen) e'
    // affidabile. La UI disabilita comunque lo slider, ma questo e' l'ultimo baluardo
    // indipendente da come alpha e' stato impostato (slider/preset/script).
    m_uboData.alpha = (m_engineMode == ModeImplicit && m_implicitIllConditioned) ? 1.0f : alpha;
    m_uboData.lightIntensity = m_lightIntensity;

    m_uboData.col1 = QVector3D(texRed1, texGreen1, texBlue1);
    m_uboData.col2 = QVector3D(texRed2, texGreen2, texBlue2);
    m_uboData.useTexture = m_textureEnabled ? 1 : 0;

    // NESSUNA IMMAGINE nello slot sampler: gli script che campionano iChannel0
    // (famiglia "Animated Images") cadono sulla scacchiera procedurale invece
    // che sui pixel della texture tappabuchi. m_surfaceTexture e' la texture
    // dell'immagine caricata: nullo = non c'e' immagine.
    // Conta l'immagine dell'UTENTE, non "esiste una texture": generateTexture()
    // carica la scacchiera di default per la stessa via, quindi m_surfaceTexture
    // resta popolata anche senza alcuna immagine e il fallback non sarebbe mai
    // scattato (era il motivo per cui gli slider colore sembravano inerti).
    m_uboData.u_noImage = m_surfaceImagePath.isEmpty() ? 1 : 0;

    // Luce di riempimento dall'osservatore: 0 (default) = termine nullo nello
    // shader, immagine identica a quella storica. Vedi u_fillLight in UboData.
    m_uboData.u_fillLight = m_fillLight;

    // Spessore del guscio (modalita' Shell): uniform, non entra nel sorgente
    // dello shader, quindi cambiarlo non richiede una ricompilazione.
    m_uboData.u_shellThickness = m_shellThickness;

    // Scelta del marcher (radio Ray Marching): come lo spessore, e' un uniform,
    // quindi il cambio e' immediato senza ricompilare lo shader.
    m_uboData.u_marcherMode = m_hybridMarcher ? 1.0f : 0.0f;

    // Raggio del tubo (sotto-tab Tubes): uniform anche lui, lo slider dello
    // spessore e' immediato.
    m_uboData.u_tubeRadius = m_tubeRadius;
    m_uboData.u_borderRadius = m_borderRadius;
    m_uboData.u_isBorder = 0;   // solo i blocchi del bordo lo accendono

    // ==========================================================
    // AGGIORNAMENTO BUFFER PRINCIPALE (multi-mesh)
    // ==========================================================
    // Scriviamo un blocco UBO per parte di mesh: tutto identico tranne i limiti
    // del dominio (u_min/u_max/v_min/v_max) e u_meshIndex, che sono per-parte.
    // Il blocco 0 resta quello "globale": lo usano lo sfondo in flat view e ogni
    // draw che non appartiene a una parte, quindi il caso a mesh singola scrive
    // esattamente cio' che scriveva prima.
    const std::vector<MeshPart> &uboParts = engine->getMeshParts();
    const int uboPartCount = (int)uboParts.size();

    // TUBO: direzione della sezione e spessore del tubo `mesh` (vedi
    // SurfaceEngine::TubeCopy e MeshPart::tubeThickness, fattore sullo
    // spessore del campo Thickness). Fuori dai tubi nessuno li legge.
    // Sezione trasportata: gli anelli della parte, e se la passata compute di
    // questo frame scrive la sua riga (entro i limiti della texture).
    const bool tubeFrames = tubeFramesActive();
    // Smorzamento della sezione: costante di tempo 0.1 s, indipendente dai fps.
    const float tubeBlend = 1.0f - std::exp(-std::max(0.0f, m_tubeFrameDt) / 0.1f);
    auto applyTubeFields = [this, tubeFrames, tubeBlend](UboData &u, const MeshPart *mp, int mesh) {
        const QVector3D r = engine->tubeReference(mesh);
        u.u_tubeRefX = r.x();
        u.u_tubeRefY = r.y();
        u.u_tubeRefZ = r.z();
        u.u_tubeRadius = m_tubeRadius * ((mp && mp->tubeThickness >= 0.0f) ? mp->tubeThickness : 1.0f);
        const int rings = mp ? mp->numU + 1 : 0;
        u.u_tubeRings = float(rings);
        u.u_tubeFramesOn = (tubeFrames && rings >= 2 && rings <= kTubeFrameMaxRings
                            && mesh >= 0 && mesh < kTubeFrameMaxParts) ? 1.0f : 0.0f;
        u.u_tubeFrameBlend = tubeBlend;
    };
    applyTubeFields(m_uboData, uboParts.empty() ? nullptr : &uboParts[0], 0);
    m_uboData.u_meshCount = (float)std::max(1, uboPartCount);
    // Col bordo acceso i blocchi raddoppiano: dopo quelli delle parti, uno di
    // bordo per parte (vedi sotto).
    const bool borderOn = (m_borderRadius > 0.0f && m_engineMode == ModeParametric);
    const int uboPartSlots = std::max(1, uboPartCount);

    // Cresce l'UBO se lo script ha dichiarato piu' parti della capienza attuale.
    // ensureUboCapacity riaggancia i binding in place (senza distruggerli),
    // quindi le pipeline restano valide e non serve ricostruirle.
    // Nel parametrico, dopo i blocchi del bordo, i cinque della passata dei
    // lati coperti per ogni istante della sonda: anche a bordo spento, che la
    // usa come sonda (borderAvailability).
    ensureUboCapacity(m_engineMode == ModeParametric ? uboPartSlots * 2 + 5 * kBorderProbeInstants : uboPartSlots);
    ensureDynamicBindings(m_surfaceTexture ? m_surfaceTexture : m_dummyTexture);
    // Immagini proprie delle fasce: prima dei blocchi, che ne dipendono (u_noImage).
    syncPartImages(resourceUpdates);

    if (uboPartCount <= 1) {
        // Mesh singola: un blocco solo, come da sempre.
        resourceUpdates->updateDynamicBuffer(m_ubo, 0, sizeof(UboData), &m_uboData);
    } else {
        for (int k = 0; k < uboPartCount; ++k) {
            const MeshPart &mp = uboParts[k];
            UboData partUbo = m_uboData;
            partUbo.u_min = mp.uMin;
            partUbo.u_max = mp.uMax;
            partUbo.v_min = mp.vMin;
            partUbo.v_max = mp.vMax;
            partUbo.u_meshIndex = (float)mp.meshIndex;
            applyTubeFields(partUbo, &mp, mp.meshIndex);
            partUbo.u_globalTexture = globalTextureActive() ? 1.0f : 0.0f;

            // ASPETTO PER-PARTE. Un valore negativo significa "eredita dallo
            // stato globale", che e' gia' in partUbo perche' copiato da
            // m_uboData: percio' una parte non configurata resta identica a
            // prima e nessun preset esistente cambia.
            // Il colore NON si applica con una texture attiva: li' il motore
            // forza deliberatamente ubuf.color a bianco per non sporcare le
            // texture che portano gia' il proprio colore (vedi il ramo
            // m_textureEnabled sopra), e scriverci sopra lo vanificherebbe.
            // AMBITO "ALL" (m_meshAppearanceUniform): l'aspetto proprio viene
            // IGNORATO e resta valido solo il globale, gia' in partUbo. La
            // superficie si disegna come una sola; i valori per-parte non
            // vengono toccati, quindi tornando su "Mesh" ricompaiono.
            if (!m_meshAppearanceUniform) {
                // Questa parte finira' texturizzata? Serve PRIMA del colore: una
                // parte SENZA texture deve riprendersi la propria tinta, mentre
                // una texturizzata vuole il bianco neutro (finalRGB = color * tex).
                //
                // IN WIREFRAME (o Bordi) LA TEXTURE NON SI APPLICA: il fragment
                // esce subito con `FragColor = vec4(ubuf.color, alpha)` (vedi la
                // guardia u_renderMode 2/5 in surface.frag), quindi ubuf.color
                // NON e' un fattore da neutralizzare ma il colore FINALE delle
                // linee. Trattare la parte come texturizzata la disegnava col
                // bianco neutro: mettendo il wireframe su una mesh con texture,
                // le linee diventavano bianche e il colore proprio spariva.
                // Stesso criterio del render (effectivePartRenderMode), cosi' le
                // due decisioni non possono divergere.
                const int partMode = effectivePartRenderMode(mp);
                const bool partFlatColored = (partMode == 2 || partMode == 5);
                const bool partTextured = mp.effectiveTextureEnabledMulti()
                                          && !partFlatColored;

                // COLORE DELLA PARTE. Il ramo globale poco sopra forza
                // m_uboData.color a BIANCO quando la texture globale e' accesa,
                // e ogni blocco parte da quella copia: una fascia senza texture
                // si ritrovava quindi bianca invece che del proprio colore
                // (o di quello globale). Il colore va soppresso solo se e'
                // QUESTA parte ad essere texturizzata, non perche' lo e' la
                // superficie.
                if (!partTextured) {
                    // Tinta piena: la propria se ce l'ha, altrimenti quella
                    // globale REALE (red/green/blue), non il bianco neutro che
                    // m_uboData porta quando la texture globale e' accesa.
                    if (mp.hasCustomColor())
                        partUbo.color = QVector3D(mp.colorR, mp.colorG, mp.colorB);
                    else if (m_textureEnabled && m_engineMode == ModeParametric)
                        partUbo.color = QVector3D(red, green, blue);
                }
                if (mp.alpha >= 0.0f)
                    partUbo.alpha = mp.alpha;
                // LUCE: NON per-parte. E' l'illuminazione della scena (come la
                // speculare) e vale per tutta la superficie, qualunque mesh sia
                // selezionata: partUbo porta gia' il valore globale.
                // Il campo lightIntensity resta in MeshPart per compatibilita'
                // con i preset che lo hanno salvato -- viene letto e riscritto
                // dalla persistenza -- ma non governa piu' il render.

                // TEXTURE PROPRIA DELLA PARTE. Il codice e' gia' compilato nello
                // shader come getCustomColor_<k> (vedi createFragmentShaderSource):
                // qui basta accendere l'interruttore per questa parte, perche' il
                // dispatcher sceglie la funzione da u_meshIndex.
                //
                // TEXTURE EFFICACE DELLA PARTE. Si scrive SEMPRE, anche quando
                // la parte non ne ha una propria: in multi-mesh una fascia mai
                // configurata deve restare SENZA texture, non ereditare quella
                // globale (vedi effectiveTextureEnabledMulti). Prima qui si
                // entrava solo con hasCustomTexture e le altre si tenevano lo
                // useTexture globale copiato da m_uboData: applicando una
                // texture in "All" le fasce lasciate apposta nude se la
                // prendevano addosso.
                partUbo.useTexture = partTextured ? 1 : 0;

                // IMMAGINE PROPRIA DELLA PARTE. Se il suo script nomina
                // un'immagine (tag //IMG:) e quella e' caricata, allo slot 1 di
                // QUESTA draw call c'e' la sua texture (bindingsForPart), non
                // quella della superficie: la leggono la funzione di default e
                // gli script che campionano iChannel0. u_noImage va quindi a 0
                // anche se la superficie non ha alcuna immagine.
                // Nominata ma non caricabile: resta tutto com'e' in m_uboData,
                // cioe' l'immagine della superficie se c'e', altrimenti la
                // scacchiera di ripiego.
                if (partImageFor(mp))
                    partUbo.u_noImage = 0;

                if (mp.hasCustomTexture) {

                    // COLORE BASE A BIANCO SULLA PARTE TEXTURIZZATA.
                    // Il fragment compone finalRGB = ubuf.color * texture: il
                    // colore va quindi neutralizzato, o la texture esce
                    // MOLTIPLICATA per la tinta della superficie (con un colore
                    // scuro il risultato e' nero, e con la scacchiera meta'
                    // celle si annullano -> la mesh sembra sparire).
                    // Il ramo globale lo fa gia' piu' sopra, ma solo quando e'
                    // m_textureEnabled GLOBALE ad essere acceso: una texture
                    // per-mesh non passa di li'.
                    // NB: si scrive DOPO il colore proprio della parte, cosi'
                    // vince su di esso; il colore resta in MeshPart e ricompare
                    // spegnendo la texture.
                    // Stessa condizione usata per useTexture e per il colore
                    // pieno qui sopra: il bianco neutro e la texture accesa
                    // devono essere la STESSA decisione, o si torna al caso
                    // "parte bianca senza texture".
                    if (partTextured)
                        partUbo.color = QVector3D(1.0f, 1.0f, 1.0f);

                    // COLORI u_col1/u_col2 DELLA PARTE. Vivono nel blocco UBO,
                    // quindi possono essere diversi per ogni mesh: senza questo
                    // le parti condividevano i due slot globali e l'ultima
                    // texture applicata riscriveva i colori di TUTTE le altre.
                    if (mp.hasCustomTexColors()) {
                        partUbo.col1 = QVector3D(mp.texCol1R, mp.texCol1G, mp.texCol1B);
                        partUbo.col2 = QVector3D(mp.texCol2R, mp.texCol2G, mp.texCol2B);
                    }

                }

                // ZOOM / PAN / ROTAZIONE 2D PROPRI DELLA PARTE. Stessa ragione
                // dei colori: sono campi del blocco UBO, quindi per-parte.
                // Senza, la trasformazione fatta col mouse in vista 2D su UNA
                // texture veniva applicata a tutte.
                // FUORI dal blocco hasCustomTexture: la trasformazione e' una
                // proprieta' della PARTE, non dello script. Due fasce con LA
                // STESSA texture devono poterla mostrare a scale diverse, e una
                // fascia che eredita la texture globale deve avere comunque la
                // propria inquadratura, altrimenti resta agganciata allo zoom
                // globale e si ridimensiona insieme alle altre.
                // In vista 2D (m_isFlatView) si edita a schermo intero la
                // texture della SOLA parte attiva: quella deve seguire i membri
                // globali che il mouse muove (o trascinare non muoverebbe
                // nulla), mentre tutte le ALTRE restano ferme sulla propria.
                const bool editingThisPart = m_isFlatView && m_flatViewTarget == 0
                                             && (k == m_activeMeshPart);
                if (editingThisPart) {
                    // BUFFER DI LAVORO, esplicito. Prima qui si lasciava stare,
                    // contando sul fatto che partUbo (copia di m_uboData) gia'
                    // portasse i membri che il mouse muove. Non e' piu' vero: in
                    // m_uboData ora c'e' la trasformazione GLOBALE persistente,
                    // che il mouse non tocca -- e la texture in editing restava
                    // immobile al trascinamento.
                    partUbo.zoom = m_flatZoom;
                    partUbo.center = m_flatPan;
                    partUbo.rotation = m_flatRotation;
                } else if (mp.hasCustomTexTransform()) {
                    partUbo.zoom = mp.texZoom;
                    partUbo.center = QVector2D(mp.texPanX, mp.texPanY);
                    partUbo.rotation = mp.texRotation;
                }

                // OROLOGIO DELLA PARTE (dummyZero.x = tempo letto da ogni
                // getCustomColor_<k>). Si scrive SEMPRE, mai per ereditarieta':
                // il tempo e' un valore che scorre e che il globale puo'
                // congelare, quindi una parte che copiasse m_timeTex si
                // congelerebbe insieme a lui e non ripartirebbe piu' da sola.
                // Vedi la nota su timeTex in MeshPart.
                partUbo.dummyZero.setX(kClockOrigin + mp.timeTex);
            }

            // MODALITA' PER-PARTE. Lo shader decide dal solo ubuf.u_renderMode
            // se disegnare a colore piatto (wireframe/bordi) o illuminato:
            // senza questa riga una mesh in wireframe con globale Phong veniva
            // illuminata, e una mesh solida con globale wireframe usciva piatta.
            partUbo.renderMode = effectivePartRenderMode(mp);

            resourceUpdates->updateDynamicBuffer(m_ubo, k * m_uboBlockStride,
                                                 sizeof(UboData), &partUbo);
        }
    }

    // BORDO: un blocco per parte, dopo quelli delle parti (indice
    // uboPartSlots + k). Del blocco della parte servono il dominio e
    // u_meshIndex -- gli script scelgono la geometria leggendo `mesh` -- il
    // resto e' del bordo: il suo colore, opaco, senza texture, e la sua
    // modalita' (Base, Phong o Wireframe), indipendente dalla superficie.
    if (borderOn) {
        for (int k = 0; k < uboPartSlots; ++k) {
            UboData borderUbo = m_uboData;
            if (k < uboPartCount) {
                const MeshPart &mp = uboParts[k];
                borderUbo.u_min = mp.uMin;
                borderUbo.u_max = mp.uMax;
                borderUbo.v_min = mp.vMin;
                borderUbo.v_max = mp.vMax;
                borderUbo.u_meshIndex = (float)mp.meshIndex;
                applyTubeFields(borderUbo, &mp, mp.meshIndex);
            }
            borderUbo.u_isBorder = 1;
            borderUbo.color = QVector3D(m_borderColor.redF(), m_borderColor.greenF(),
                                        m_borderColor.blueF());
            borderUbo.alpha = 1.0f;
            // TEXTURE DEL BORDO: colore base a bianco (il fragment compone
            // color * texture), i suoi due colori, la sua inquadratura, il suo
            // orologio e la sua immagine, se ne ha una (altrimenti campiona
            // quella della superficie, come una fascia senza immagine propria).
            const bool borderTex = borderTextureShown();
            borderUbo.useTexture = borderTex ? 1 : 0;
            if (borderTex) borderUbo.color = QVector3D(1.0f, 1.0f, 1.0f);
            borderUbo.col1 = QVector3D(m_borderTexCol1.redF(), m_borderTexCol1.greenF(), m_borderTexCol1.blueF());
            borderUbo.col2 = QVector3D(m_borderTexCol2.redF(), m_borderTexCol2.greenF(), m_borderTexCol2.blueF());
            borderUbo.zoom = m_borderTexZoom;
            borderUbo.center = m_borderTexPan;
            borderUbo.rotation = m_borderTexRotation;
            borderUbo.dummyZero.setX(kClockOrigin + m_borderTimeTex);
            if (borderBindings() != (m_bindingsDyn ? m_bindingsDyn : m_bindings))
                borderUbo.u_noImage = 0;
            // Base / Phong / Wireframe del BORDO, non della superficie: in
            // wireframe il fragment esce col colore pieno, in Phong accende la
            // speculare (che altrove e' un flag unico della figura).
            borderUbo.renderMode = m_borderRenderMode;
            borderUbo.useSpecular = (m_borderRenderMode == 1) ? 1 : 0;
            resourceUpdates->updateDynamicBuffer(m_ubo, (uboPartSlots + k) * m_uboBlockStride,
                                                 sizeof(UboData), &borderUbo);
        }
    }

    // =========================================================================
    // SCARICO TEXTURE (Superficie) — distruzione RHI nel contesto del frame.
    // Eseguito PRIMA dell'upload: se nello stesso giro arriva una nuova immagine,
    // il blocco di upload sotto ricrea comunque m_surfaceTexture.
    // =========================================================================
    if (m_surfaceTextureNeedsClear) {
        if (m_surfaceTexture) {
            m_surfaceTexture->destroy();
            delete m_surfaceTexture;
            m_surfaceTexture = nullptr;

            // Il binding allo slot 1 puntava a m_surfaceTexture appena distrutta:
            // ricolleghiamolo a m_dummyTexture (come all'init), altrimenti un frame
            // disegnato senza re-upload leggerebbe memoria liberata.
            if (m_bindings && m_dummyTexture) {
                m_bindings->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_dummyTexture, m_sampler)
                });
                m_bindings->create();
            }
            // Stesso problema sui binding con dynamic offset usati dalla
            // superficie: puntavano alla texture appena distrutta.
            ensureDynamicBindings(m_dummyTexture);
        }
        m_surfaceTextureNeedsClear = false;
    }

    // =========================================================================
    // GESTIONE COMUNE UPLOAD TEXTURE (Superficie)
    // =========================================================================
    if (m_surfaceTextureNeedsUpload && !m_pendingSurfaceImage.isNull()) {
        if (m_surfaceTexture) {
            m_surfaceTexture->destroy();
            delete m_surfaceTexture;
        }
        m_surfaceTexture = rhi()->newTexture(QRhiTexture::RGBA8, m_pendingSurfaceImage.size(), 1);
        m_surfaceTexture->create();

        QRhiTextureSubresourceUploadDescription subresDesc(m_pendingSurfaceImage.constBits(), m_pendingSurfaceImage.sizeInBytes());
        QRhiTextureUploadEntry entry(0, 0, subresDesc);
        QRhiTextureUploadDescription uploadDesc({ entry });
        resourceUpdates->uploadTexture(m_surfaceTexture, uploadDesc);

        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_surfaceTexture, m_sampler)
        });
        m_bindings->create();

        // La superficie disegna dai binding con dynamic offset: senza questo
        // aggiornamento la nuova texture non comparirebbe (resterebbe la dummy).
        ensureDynamicBindings(m_surfaceTexture);

        m_surfaceTextureNeedsUpload = false;
    }

    // =========================================================================
    // GESTIONE COMUNE SFONDO 2D (Texture Upload, UBO, VBO)
    // Questo serve sia alla Parametrica che al Ray Marching!
    // =========================================================================
    if (m_backgroundTextureNeedsUpload && !m_pendingBackgroundImage.isNull()) {
        if (!m_bgBindings) initBackgroundShader();

        if (m_backgroundTexture) {
            m_backgroundTexture->destroy();
            delete m_backgroundTexture;
        }

        m_backgroundTexture = rhi()->newTexture(QRhiTexture::RGBA8, m_pendingBackgroundImage.size(), 1);
        m_backgroundTexture->create();

        QRhiTextureSubresourceUploadDescription bgSubresDesc(m_pendingBackgroundImage.constBits(), m_pendingBackgroundImage.sizeInBytes());
        QRhiTextureUploadEntry bgEntry(0, 0, bgSubresDesc);
        QRhiTextureUploadDescription bgUploadDesc({ bgEntry });
        resourceUpdates->uploadTexture(m_backgroundTexture, bgUploadDesc);

        m_bgBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_bgUbo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_backgroundTexture, m_sampler)
        });
        m_bgBindings->create();

        m_backgroundTextureNeedsUpload = false;
    }

    if (m_useBackgroundTexture || m_isFlatView || m_engineMode == ModeImplicit) {
        if (!m_bgBindings) initBackgroundShader();

        if (m_bgUbo) {
            UboData bgUboData = m_uboData;
            bgUboData.time = clocks.bg;
            // Lo sfondo è sempre opaco: lo slider trasparenza del renderer agisce
            // sulla superficie (m_uboData.alpha), non deve sbiadire la texture di
            // background fondendola col clearColor (pipeline bg con blend SrcAlpha).
            bgUboData.alpha = 1.0f;

            if (m_bgTexColorsSet) {
                bgUboData.col1 = m_bgTexCol1;
                bgUboData.col2 = m_bgTexCol2;
            }

            bgUboData.zoom = m_bgZoom;
            bgUboData.center = m_bgPan;
            bgUboData.rotation = m_bgRotation;

            // SFONDO SOLIDALE: la matrice che porta un pixel dello schermo alla
            // DIREZIONE del mondo in cui guarda, letta da bgBaseUV() nello shader.
            //
            // Viaggia in u_mvpMatrix di QUESTA copia dell'UBO, che lo sfondo non
            // usava: il vertex shader dello sfondo non dichiara nemmeno il blocco
            // (rebuildBackgroundShader), e nessun ramo del fragment la legge. Un
            // campo NUOVO avrebbe invece toccato SceneUBO, che va tenuto identico
            // fra gli stadi (su Adreno una discordanza = schermo vuoto, vedi
            // CLAUDE.md). La copia e' dello sfondo soltanto: la superficie ha il
            // suo buffer e non ne risente.
            // Oltre al 3x3 della direzione, due celle della colonna 3 (fuori dal
            // mat3 che lo shader estrae):
            //   (3,3) = la MODALITA' (BgSkyMode: 0 fisso, 1 sfera, 2 cilindro,
            //           3 cubo) -- in GLSL u_mvpMatrix[3][3];
            //   (1,3) = le proporzioni dell'immagine, larghezza/altezza, che il
            //           cilindro usa per non deformarla -- in GLSL [3][1]. 1 per
            //           gli script: non hanno proporzioni proprie.
            // La matrice si scrive SEMPRE, anche a zero: la copia arriva con la
            // mvp della scena, le cui celle possono valere qualunque cosa.
            //
            // Solo la ROTAZIONE di m_view: niente modello (girare l'oggetto non
            // deve trascinare il cielo), niente traslazione (un cielo
            // all'infinito non ha parallasse). E un campo visivo "virtuale" preso
            // da m_cameraFov invece della proiezione vera: in ortogonale i raggi
            // sono paralleli, e ricavando la direzione dalla proiezione ogni pixel
            // guarderebbe nello stesso punto -- sfondo di un colore solo.
            // Vista piatta 2D: si edita la texture a schermo, quindi fisso.
            QMatrix4x4 skyRay;
            skyRay.fill(0.0f);
            if (m_bgSkyMode != BgFixed && !m_isFlatView) {
                QMatrix4x4 viewRotInv = m_view;
                viewRotInv.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
                viewRotInv = viewRotInv.transposed();   // rotazione: inversa = trasposta
                const float tanHalf = std::tan(m_cameraFov * 0.5f * float(M_PI) / 180.0f);
                QMatrix4x4 eyeFromNdc;                  // pixel (ndc.x, ndc.y, 1) -> raggio in camera
                eyeFromNdc.setToIdentity();
                eyeFromNdc(0, 0) = tanHalf * aspect;
                eyeFromNdc(1, 1) = tanHalf;
                eyeFromNdc(2, 2) = -1.0f;               // la camera guarda verso -Z
                skyRay = viewRotInv * eyeFromNdc;
                skyRay(3, 3) = float(m_bgSkyMode);
                const bool isImage = !m_bgIsScript && m_backgroundTexture
                                     && m_backgroundTexture->pixelSize().height() > 0;
                skyRay(1, 3) = isImage
                             ? float(m_backgroundTexture->pixelSize().width())
                               / float(m_backgroundTexture->pixelSize().height())
                             : 1.0f;
            }
            memcpy(bgUboData.mvpMatrix, skyRay.constData(), 64);

            resourceUpdates->updateDynamicBuffer(m_bgUbo, 0, sizeof(UboData), &bgUboData);
        }

        if (!m_bgVboUploaded && m_bgVbo) {
            Vertex quadVertices[6] = {
                { QVector4D(-1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 1.0f) },
                { QVector4D( 1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 1.0f) },
                { QVector4D(-1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 0.0f) },
                { QVector4D(-1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(0.0f, 0.0f) },
                { QVector4D( 1.0f, -1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 1.0f) },
                { QVector4D( 1.0f,  1.0f, 0.99f, 1.0f), QVector4D(), QVector2D(1.0f, 0.0f) }
            };
            resourceUpdates->updateDynamicBuffer(m_bgVbo, 0, sizeof(quadVertices), quadVertices);
            m_bgVboUploaded = true;
        }
    }


    if (m_engineMode == ModeParametric) {

        // ==========================================
        // 1. MODALITÀ PARAMETRICA
        // ==========================================
        if (!m_pipelineOpaque) {
            buildPipeline();
        }

        if (meshNeedsUpdate) {
            const auto& vertices = engine->getVertices();
            const auto& indices = engine->getIndices();

            if (vertices.empty() || indices.empty()) {
                // MESH VUOTA = NIENTE DA DISEGNARE. m_indexCount va azzerato:
                // senza, conserva il conteggio della mesh PRECEDENTE mentre
                // VBO/IBO contengono ancora i suoi dati, e il draw piu' sotto
                // (gate `m_indexCount > 0`) ridisegna la superficie vecchia.
                // Finora era latente perche' ogni percorso produceva subito
                // un'altra mesh; serve per la scena vuota, che deve poter
                // disegnare NIENTE di proposito.
                m_indexCount = 0;
            } else {
                int vSize = vertices.size() * sizeof(Vertex);
                int iSize = indices.size() * sizeof(unsigned int);

                if (m_vbo->size() < vSize) {
                    m_vbo->destroy(); delete m_vbo;
                    m_vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                    m_vbo->create();
                }

                if (m_ibo->size() < iSize) {
                    m_ibo->destroy(); delete m_ibo;
                    m_ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                    m_ibo->create();
                }

                resourceUpdates->updateDynamicBuffer(m_vbo, 0, vSize, vertices.data());
                resourceUpdates->updateDynamicBuffer(m_ibo, 0, iSize, indices.data());
                m_indexCount = indices.size();
            }
            meshNeedsUpdate = false;
            // Il bordo segue le parti appena rigenerate.
            m_borderUploaded = false;
        }

        // Lati della mesh custom per il bordo: solo la parte usata della texture.
        if (m_borderEdgeDirty && m_borderEdgeTex && m_borderEdgeWidth > 0) {
            QRhiTextureSubresourceUploadDescription edges(
                m_borderEdgeData.data(), quint32(m_borderEdgeData.size() * sizeof(float)));
            edges.setSourceSize(QSize(m_borderEdgeWidth, kBorderEdgeRows));
            resourceUpdates->uploadTexture(m_borderEdgeTex,
                                           QRhiTextureUploadDescription({ QRhiTextureUploadEntry(0, 0, edges) }));
            m_borderEdgeDirty = false;
        }

        // BORDO: i suoi buffer, solo quando si disegna (a raggio 0 non serve
        // caricarli; li ricarica il primo frame col bordo acceso).
        if (!m_borderUploaded && m_borderRadius > 0.0f && m_borderVbo && m_borderIbo) {
            const auto &bv = engine->getBorderVertices();
            const auto &bi = engine->getBorderIndices();
            if (!bv.empty() && !bi.empty()) {
                const int vSize = int(bv.size() * sizeof(Vertex));
                const int iSize = int(bi.size() * sizeof(unsigned int));
                if (m_borderVbo->size() < vSize) {
                    m_borderVbo->destroy(); delete m_borderVbo;
                    m_borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                    m_borderVbo->create();
                }
                if (m_borderIbo->size() < iSize) {
                    m_borderIbo->destroy(); delete m_borderIbo;
                    m_borderIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                    m_borderIbo->create();
                }
                resourceUpdates->updateDynamicBuffer(m_borderVbo, 0, vSize, bv.data());
                resourceUpdates->updateDynamicBuffer(m_borderIbo, 0, iSize, bi.data());

                // Linee del wireframe, con la densita' corrente.
                engine->buildBorderWire(m_borderWireAlong, m_borderWireAround);
                const auto &bw = engine->getBorderWireIndices();
                const int wSize = int(bw.size() * sizeof(unsigned int));
                if (wSize > 0 && m_borderWireIbo) {
                    if (m_borderWireIbo->size() < wSize) {
                        m_borderWireIbo->destroy(); delete m_borderWireIbo;
                        m_borderWireIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, wSize * 1.5);
                        m_borderWireIbo->create();
                    }
                    resourceUpdates->updateDynamicBuffer(m_borderWireIbo, 0, wSize, bw.data());
                }
            }
            m_borderUploaded = true;
        }

        // Stesso trattamento del solido: a indici vuoti il ramo di upload non
        // gira, quindi m_wireframeIndexCount/m_wireframeRanges resterebbero
        // quelli della mesh precedente e le sue LINEE sopravvivrebbero alla
        // scena vuota (il ramo wireframe ha un gate suo, non coperto da
        // m_indexCount).
        if (wireframeNeedsUpdate && m_wireframeIndices.empty()) {
            m_wireframeIndexCount = 0;
            m_wireframeRanges.clear();
            wireframeNeedsUpdate = false;
        }

        if (wireframeNeedsUpdate && !m_wireframeIndices.empty()) {
            int iSize = m_wireframeIndexCount * sizeof(unsigned int);
            if (m_wireframeIbo->size() < iSize) {
                m_wireframeIbo->destroy(); delete m_wireframeIbo;
                m_wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                m_wireframeIbo->create();
            }
            resourceUpdates->updateDynamicBuffer(m_wireframeIbo, 0, iSize, m_wireframeIndices.data());
            wireframeNeedsUpdate = false;
        }

        // SEZIONE TRASPORTATA DEI TUBI: prima del disegno, fuori dal render
        // pass. La passata consuma gli aggiornamenti in coda (i blocchi UBO di
        // questo frame), quindi il render pass parte senza.
        if (tubeFramesActive()) {
            // Tubi o anelli diversi: lo stato per anello non corrisponde piu'.
            quint64 layout = engine->getMeshParts().size();
            for (const MeshPart &mp : engine->getMeshParts()) layout = layout * 1000003u + quint64(mp.numU);
            if (layout != m_tubeFrameLayout) {
                m_tubeFrameLayout = layout;
                m_tubeFrameStateReset = true;
            }
            if (m_tubeFrameStateReset) {
                static const QByteArray zeros(kTubeFrameWorkVec4 * 16, '\0');
                resourceUpdates->uploadStaticBuffer(m_tubeFrameWork, 0, quint32(zeros.size()), zeros.constData());
                m_tubeFrameStateReset = false;
            }
            cb->beginComputePass(resourceUpdates);
            resourceUpdates = nullptr;
            cb->setComputePipeline(m_tubeFramePipeline);
            const std::vector<MeshPart> &frameParts = engine->getMeshParts();
            const int blocks = std::max(1, (int)frameParts.size());
            for (int k = 0; k < blocks; ++k) {
                // Stesso blocco dei draw: 0 per un tubo solo, k per le copie.
                const quint32 ofsBytes = (frameParts.size() <= 1) ? 0u : quint32(k) * m_uboBlockStride;
                const QRhiCommandBuffer::DynamicOffset ofs(0, ofsBytes);
                cb->setShaderResources(m_tubeFrameSrb, 1, &ofs);
                cb->dispatch(1, 1, 1);
            }
            cb->endComputePass();
        }

        // LATI DEL BORDO COPERTI (buildSharedEdgePipeline): prima del disegno,
        // col blocco 0 per costanti, tempo e vincoli; dominio e `mesh` di ogni
        // parte dalla tabella, ricaricata solo quando cambia. Le due passate
        // girano solo quando cambia la geometria (m_sharedEdgeKey): le
        // rotazioni non contano, il compute confronta punti non ruotati.
        // Si decide QUI, dopo buildPipeline, e qui si accende u_sharedEdgesOn
        // nei blocchi del bordo (scritti piu' in alto con 0): la passata nasce
        // dal vertex della pipeline, che dopo un rebuildShader si rifa' solo
        // in questo punto. Decidendo coi blocchi, nel frame in cui il bordo si
        // accende valeva ancora il vertex di prima, senza bordo: passata spenta
        // e tutti i lati disegnati, finche' qualcosa non ridisegnava la scena.
        // A BORDO SPENTO la passata gira come SONDA (borderAvailability): una
        // volta per geometria, quando resta ferma per m_borderProbeTimer, e se
        // ne rileggono i flag per sapere se il bordo, acceso, si vedrebbe. A
        // bordo acceso gira gia' per disegnare: la sonda ne rilegge i flag.
        const bool borderOn = m_borderRadius > 0.0f;
        const bool edgePass = sharedEdgesActive();
        if (!edgePass && !m_isCustomMesh && engine && !engine->isTubeModeActive())
            setBorderAvailability(BorderAvailability::Unknown);   // compute assente o non compilato
        if (edgePass) {
            const std::vector<MeshPart> &edgeParts = engine->getMeshParts();
            if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
            const float on = 1.0f;
            const int edgeSlots = std::max(1, (int)edgeParts.size());
            if (borderOn) {
                for (int k = 0; k < edgeSlots; ++k)
                    resourceUpdates->updateDynamicBuffer(
                        m_ubo, quint32((edgeSlots + k) * m_uboBlockStride + offsetof(UboData, u_sharedEdgesOn)),
                        sizeof(float), &on);
            }
            std::vector<QVector4D> table;
            table.reserve(1 + 2 * edgeParts.size());
            table.emplace_back(float(edgeParts.size()), 0.0f, 0.0f, 0.0f);
            for (const MeshPart &mp : edgeParts) {
                table.emplace_back(mp.uMin, mp.uMax, mp.vMin, mp.vMax);
                table.emplace_back(float(mp.meshIndex), 0.0f, 0.0f, 0.0f);
            }
            if (table != m_sharedEdgeTable) {
                resourceUpdates->uploadStaticBuffer(m_sharedEdgeParts, 0, quint32(table.size() * sizeof(QVector4D)),
                                                    table.data());
                m_sharedEdgeTable = table;
            }
            // Cio' da cui dipendono i punti dei lati, oltre allo shader (che
            // azzera la chiave quando si rifa'). La sonda guarda la forma senza
            // il tempo: su una superficie animata girerebbe a ogni frame.
            QByteArray shape(reinterpret_cast<const char *>(table.data()), int(table.size() * sizeof(QVector4D)));
            shape.append(reinterpret_cast<const char *>(&m_uboData.mathParams), sizeof(QVector4D));
            shape.append(reinterpret_cast<const char *>(&m_uboData.mathParams2), sizeof(QVector4D));
            shape.append(reinterpret_cast<const char *>(&m_uboData.hasExplicitW), sizeof(int));
            shape.append(reinterpret_cast<const char *>(&m_uboData.u_meshCount), sizeof(float));
            // Animata o ferma: ripartendo, la sonda riguarda piu' istanti.
            const int animating = m_surfaceAnimating ? 1 : 0;
            shape.append(reinterpret_cast<const char *>(&animating), sizeof(int));
            QByteArray key = shape;
            key.append(reinterpret_cast<const char *>(&m_uboData.time), sizeof(float));
            // La sonda segue la geometria anche a bordo acceso (un preset
            // salvato col bordo): li' rilegge i flag della passata che gira
            // comunque, cosi' spegnendo il bordo il dock sa gia' cosa fare.
            bool probe = false, watchProbe = false;
            if (shape != m_borderProbeKey) {
                if (shape != m_borderProbePending) {
                    // Geometria nuova: si aspetta che si fermi.
                    m_borderProbePending = shape;
                    m_borderProbeArmed = false;
                    m_borderProbeWatch = false;
                    m_borderProbeTimer->start();
                } else if (m_borderProbeArmed && !m_borderProbeReading) {
                    m_borderProbeArmed = false;
                    m_borderProbeKey = shape;
                    probe = true;
                }
            } else if (m_borderProbeWatch && !m_borderProbeReading
                       && m_borderProbeWatchClock.elapsed() >= kBorderProbeWatchMs) {
                // Si aspetta che la superficie animata si apra: l'istante corrente.
                m_borderProbeWatchClock.restart();
                probe = watchProbe = true;
            }
            // SU UNA SUPERFICIE ANIMATA la sonda guarda kBorderProbeInstants
            // istanti di t in un colpo solo, per sapere se si apre in qualche
            // momento (S3 Moving Plane) o resta sempre chiusa: vedi
            // finishBorderProbe. Ogni istante coi suoi cinque blocchi UBO: scritti
            // nello stesso frame negli stessi blocchi, varrebbe l'ultimo per
            // tutti. L'ultimo istante e' quello attuale: col bordo acceso restano
            // i suoi flag. La sorveglianza (watchProbe) guarda solo l'attuale.
            const int instants = (probe && !watchProbe && m_surfaceAnimating) ? kBorderProbeInstants : 1;
            const bool run = (borderOn && key != m_sharedEdgeKey) || probe;
            if (run) {
                m_sharedEdgeKey = key;
                // Cinque passate distinte, ciascuna legge cio' che la
                // precedente scrive: campioni (fase 10), sfere dei blocchi (11),
                // test dei campioni dei lati (12), pulizia dei tratti corti
                // (13), lati coperti per intero (14). La fase sta nel loro
                // blocco UBO, dopo quelli del bordo; il resto del blocco e'
                // quello globale (costanti, tempo, vincoli).
                const int count = int(edgeParts.size());
                const int work[5] = {
                    count * (4 * (kSharedEdgeSamples + 1) + (kSharedEdgeGrid + 1) * (kSharedEdgeGrid + 1)),
                    count * kSharedEdgeTiles,
                    count * 4 * (kSharedEdgeSamples + 1),
                    count * 4 * (kSharedEdgeSamples + 1),
                    count * 4 };
                if (probe) {
                    m_borderProbeMeshes.clear();
                    for (const MeshPart &mp : edgeParts) m_borderProbeMeshes.push_back(mp.meshIndex);
                    m_borderProbeReadSerial = m_borderProbeSerial;
                    m_borderProbeReading = true;
                    m_borderProbeInstantsRead = instants;
                    m_borderProbeArrived = 0;
                }
                for (int i = 0; i < instants; ++i) {
                    for (int phase = 0; phase < 5; ++phase) {
                        UboData block = m_uboData;
                        block.u_isBorder = 10 + phase;
                        block.time = m_uboData.time + float(instants - 1 - i) * kBorderProbeStep;
                        const quint32 offset = quint32(2 * edgeSlots + 5 * i + phase) * m_uboBlockStride;
                        if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
                        resourceUpdates->updateDynamicBuffer(m_ubo, offset, sizeof(UboData), &block);
                    }
                }
                for (int i = 0; i < instants; ++i) {
                    for (int phase = 0; phase < 5; ++phase) {
                        cb->beginComputePass(resourceUpdates);
                        resourceUpdates = nullptr;
                        cb->setComputePipeline(m_sharedEdgePipeline);
                        const QRhiCommandBuffer::DynamicOffset ofs(
                            0, quint32(2 * edgeSlots + 5 * i + phase) * m_uboBlockStride);
                        cb->setShaderResources(m_sharedEdgeSrb, 1, &ofs);
                        cb->dispatch((work[phase] + 63) / 64, 1, 1);
                        cb->endComputePass();
                    }
                    if (probe) {
                        // I flag di QUESTO istante; il risultato arriva dentro un
                        // frame successivo e si legge fuori da render, in coda.
                        QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
                        m_borderProbeResults[i] = {};
                        m_borderProbeResults[i].completed = [this] {
                            QMetaObject::invokeMethod(this, [this] { finishBorderProbe(); }, Qt::QueuedConnection);
                        };
                        rb->readBackTexture(QRhiReadbackDescription(m_sharedEdgeTex), &m_borderProbeResults[i]);
                        cb->resourceUpdate(rb);
                    }
                }
            }
            if (m_sharedEdgeReadbackRequested) {
                m_sharedEdgeReadbackRequested = false;
                QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
                m_sharedEdgeFlagsResult = {};
                m_sharedEdgeFlagsResult.completed = [this] { ++m_sharedEdgeReadbacks; };
                rb->readBackTexture(QRhiReadbackDescription(m_sharedEdgeTex), &m_sharedEdgeFlagsResult);
                m_sharedEdgeSamplesResult = {};
                m_sharedEdgeSamplesResult.completed = [this] { ++m_sharedEdgeReadbacks; };
                rb->readBackBuffer(m_sharedEdgeSamples, 0,
                                   quint32(edgeParts.size() * kSharedEdgePartVec4 * sizeof(QVector4D)),
                                   &m_sharedEdgeSamplesResult);
                cb->resourceUpdate(rb);
            }
        }
        // La lettura della sonda si chiude in un frame successivo: finche' non
        // arriva, se ne chiedono altri (una scena ferma non ne disegnerebbe).
        if (m_borderProbeReading) update();

        // --- RENDER PASS ---
        QColor clearColor = QColor::fromRgbF(m_bgColor.x(), m_bgColor.y(), m_bgColor.z());
        cb->beginPass(renderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

        if (m_isFlatView) {
            if (m_flatViewTarget == 1 && m_bgPipeline && m_bgVbo) {
                cb->setGraphicsPipeline(m_bgPipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bgBindings);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            } else if (m_flatViewTarget == 2 && m_borderRadius > 0.0f && m_pipelineOpaque && m_bgVbo) {
                // TEXTURE DEL BORDO in 2D: lo stesso quadrato, col blocco di
                // uniform del bordo (u_isBorder: il fragment sceglie la sua
                // texture; colori, inquadratura e orologio suoi) e la sua
                // immagine. Il blocco e' quello del bordo della prima parte.
                cb->setGraphicsPipeline(m_pipelineOpaque);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                const int partSlots = std::max(1, (int)engine->getMeshParts().size());
                const QRhiCommandBuffer::DynamicOffset dynOfs0(0, partSlots * m_uboBlockStride);
                cb->setShaderResources(borderBindings(), 1, &dynOfs0);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            } else if (m_pipelineOpaque && m_bgVbo) {
                cb->setGraphicsPipeline(m_pipelineOpaque);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                // m_pipelineOpaque e' costruita sul layout con dynamic offset:
                // anche questo draw (quad di anteprima, nessuna parte di mesh)
                // deve passare un offset.
                // MULTI-MESH: la vista 2D edita la texture della mesh SELEZIONATA,
                // quindi deve leggere il blocco UBO di QUELLA parte. Col blocco 0
                // fisso, u_meshIndex valeva sempre 0 e il dispatcher mostrava la
                // texture della mesh 1 mentre il dock script mostrava quella
                // selezionata: stesso indice, due texture diverse a schermo.
                // L'indice va CLAMPATO agli stessi limiti che usa
                // commitFlatTransformToActivePart, o si disegna un blocco mentre
                // il mouse ne scrive un altro e il trascinamento sembra non
                // funzionare (il "a tratti" storico: capita quando l'indice
                // attivo sopravvive a una rigenerazione con meno parti).
                // Nota: con uboPartCount <= 1 e' scritto SOLO il blocco 0, quindi
                // qualunque altro offset leggerebbe un blocco mai aggiornato.
                const std::vector<MeshPart> &flatParts = engine->getMeshParts();
                const int flatPart = (flatParts.size() > 1 && m_activeMeshPart >= 0
                                      && m_activeMeshPart < (int)flatParts.size())
                                     ? m_activeMeshPart : 0;
                const QRhiCommandBuffer::DynamicOffset dynOfs0(0, flatPart * m_uboBlockStride);
                // ...e con la SUA immagine, se ne ha una.
                QRhiShaderResourceBindings *flatSrb = m_bindingsDyn ? m_bindingsDyn : m_bindings;
                if (flatParts.size() > 1 && m_activeMeshPart >= 0
                    && m_activeMeshPart < (int)flatParts.size())
                    flatSrb = bindingsForPart(flatParts[flatPart]);
                cb->setShaderResources(flatSrb, 1, &dynOfs0);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        } else {
            if (m_useBackgroundTexture) {
                if (!m_bgPipeline) rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
                if (m_bgPipeline && m_bgVbo) {
                    cb->setGraphicsPipeline(m_bgPipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(m_bgBindings);
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
            }

            // MULTI-MESH: una draw call per parte, ognuna col proprio blocco UBO
            // (dominio + u_meshIndex) selezionato via dynamic offset. Con una
            // parte sola il loop fa un solo giro sul blocco 0: identico a prima.
            QRhiShaderResourceBindings *srb = m_bindingsDyn ? m_bindingsDyn : m_bindings;
            const std::vector<MeshPart> &parts = engine->getMeshParts();

            // Una parte e' in wireframe se la sua modalita' EFFICACE e' 2.
            // Con parti che ereditano tutte, questo coincide col vecchio test
            // globale renderMode == 2.
            auto partIsWireframe = [&](const MeshPart &p) {
                return effectivePartRenderMode(p) == 2;
            };
            bool anyWireframe = (renderMode == 2);
            bool anySolid     = (renderMode != 2);
            if (parts.size() > 1) {
                anyWireframe = false;
                anySolid     = false;
                for (const MeshPart &p : parts) {
                    if (partIsWireframe(p)) anyWireframe = true;
                    else                    anySolid = true;
                }
            }

            // BORDO, per primo: e' opaco e scrive la profondita', quindi le
            // parti trasparenti disegnate dopo lo lasciano vedere attraverso e
            // il wireframe ne resta occluso dove gli passa dietro. Ogni parte
            // col suo blocco di bordo (uboPartSlots + indice della parte).
            if (m_borderRadius > 0.0f && m_borderRenderMode != 2 && m_indexCount > 0
                && m_pipelineOpaque && m_borderVbo && m_borderIbo && m_borderUploaded) {
                const int partSlots = std::max(1, (int)parts.size());
                // La pipeline del bordo, col discard dei tratti coperti; senza,
                // quella della superficie (bordo intero).
                cb->setGraphicsPipeline(m_borderPipelineOpaque ? m_borderPipelineOpaque : m_pipelineOpaque);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                const QRhiCommandBuffer::VertexInput borderBinding(m_borderVbo, 0);
                // Con un'immagine propria lo slot 1 e' la SUA texture.
                QRhiShaderResourceBindings *borderSrb = borderBindings();
                for (const SurfaceEngine::BorderRange &r : engine->getBorderRanges()) {
                    if (r.indexCount <= 0) continue;
                    const int slot = partSlots + std::clamp(r.meshIndex, 0, partSlots - 1);
                    const QRhiCommandBuffer::DynamicOffset ofs(0, slot * m_uboBlockStride);
                    cb->setShaderResources(borderSrb, 1, &ofs);
                    cb->setVertexInput(0, 1, &borderBinding, m_borderIbo,
                                       r.indexOffset * sizeof(unsigned int),
                                       QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(r.indexCount, 1, 0, r.vertexOffset);
                }
            }

            // ORDINE: prima il SOLIDO, poi il WIREFRAME.
            // La pipeline wireframe ha depthWrite = true (le linee devono
            // occludersi fra loro), quindi disegnandola per PRIMA riempiva il
            // depth buffer e le mesh solide TRASPARENTI disegnate dopo venivano
            // scartate dal depth test: a mesh wireframe presente, una mesh
            // trasparente dietro spariva del tutto. Il solido opaco scrive
            // comunque depth, quindi il wireframe disegnato dopo resta
            // correttamente occluso da cio' che gli sta davanti.
            if (anySolid) {
                // Solido
                if (m_indexCount > 0 && m_vbo && m_ibo) {
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);

                    // Disegna una parte con la pipeline corrente. Gli indici sono
                    // LOCALI alla parte, quindi il rebase sui vertici passa da
                    // vertexOffset di drawIndexed; l'offset nell'index buffer e'
                    // in byte.
                    auto drawPart = [&](const MeshPart &p, quint32 ubOffset) {
                        if (p.indexCount <= 0) return;
                        const QRhiCommandBuffer::DynamicOffset ofs(0, ubOffset);
                        // I binding della parte: con un'immagine propria lo
                        // slot 1 e' la SUA texture.
                        cb->setShaderResources(bindingsForPart(p), 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_ibo,
                                           p.indexOffset * sizeof(unsigned int),
                                           QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(p.indexCount, 1, 0, p.vertexOffset);
                    };

                    // Mesh singola (o mesh custom senza parti): percorso storico,
                    // un solo drawIndexed su tutto il buffer col blocco UBO 0.
                    auto drawWhole = [&]() {
                        const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                        cb->setShaderResources(m_bindingsDyn ? m_bindingsDyn : m_bindings, 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_ibo, 0, QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(m_indexCount);
                    };

                    const bool multi = (parts.size() > 1);

                    // Alpha EFFICACE di una parte: la sua, se dichiarata,
                    // altrimenti quella globale. Decide se va nel gruppo
                    // trasparente o in quello opaco.
                    // In ambito "All" l'alpha per-parte e' sospeso: vale il
                    // globale per tutte, o una mesh con alpha proprio finirebbe
                    // nel gruppo sbagliato e la superficie non si comporterebbe
                    // come una sola.
                    auto partAlpha = [&](const MeshPart &p) {
                        if (m_meshAppearanceUniform) return alpha;
                        return (p.alpha >= 0.0f) ? p.alpha : alpha;
                    };
                    if (multi) {
                        // Le parti in wireframe sono gia' state disegnate dal
                        // ramo sopra: qui vanno saltate, o comparirebbero anche
                        // come solido (doppio disegno).
                        std::vector<const MeshPart*> solidParts;
                        solidParts.reserve(parts.size());
                        for (const MeshPart &p : parts)
                            if (!partIsWireframe(p)) solidParts.push_back(&p);

                        // ORDINE: prima tutte le parti OPACHE (scrivono depth),
                        // poi le trasparenti; e fra queste TUTTE le facce
                        // posteriori prima di QUALSIASI faccia anteriore. Fare
                        // back+front parte per parte fonderebbe la parte k+1
                        // sopra il fronte della parte k, invertendo la
                        // profondita' fra i rami (il fronte non scrive depth).
                        if (m_pipelineOpaque) {
                            cb->setGraphicsPipeline(m_pipelineOpaque);
                            for (const MeshPart *p : solidParts)
                                if (partAlpha(*p) >= 0.99f)
                                    drawPart(*p, p->meshIndex * m_uboBlockStride);
                        }
                        // TRASPARENTI ORDINATE PER PROFONDITA'.
                        // Il blending non e' commutativo e questo ramo non scrive
                        // depth: disegnandole nell'ordine delle PARTI vinceva
                        // l'ultima anche se stava dietro, e con tori annidati uno
                        // interno sembrava coprire quello esterno.
                        // La distanza si stima sul CENTROIDE dei vertici della
                        // parte, portato in spazio vista: e' un'approssimazione
                        // (due superfici compenetranti non hanno un ordine
                        // corretto) ma copre il caso dei rami annidati o
                        // affiancati, che e' quello dei preset multi-mesh.
                        std::vector<const MeshPart*> transp;
                        transp.reserve(solidParts.size());
                        for (const MeshPart *p : solidParts)
                            if (partAlpha(*p) < 0.99f) transp.push_back(p);

                        if (transp.size() > 1) {
                            const std::vector<Vertex> &verts = engine->getVertices();
                            const QMatrix4x4 mvNow = m_view * m_model;
                            auto depthOf = [&](const MeshPart *p) -> float {
                                if (p->vertexCount <= 0) return 0.0f;
                                QVector3D c(0.0f, 0.0f, 0.0f);
                                const int step = std::max(1, p->vertexCount / 64);
                                int n = 0;
                                for (int i = 0; i < p->vertexCount; i += step) {
                                    const int idx = p->vertexOffset + i;
                                    if (idx < 0 || idx >= (int)verts.size()) continue;
                                    c += verts[idx].position.toVector3D();
                                    ++n;
                                }
                                if (n == 0) return 0.0f;
                                c /= float(n);
                                // La camera guarda lungo -z: piu' NEGATIVO = piu' lontano.
                                return mvNow.map(c).z();
                            };
                            std::stable_sort(transp.begin(), transp.end(),
                                             [&](const MeshPart *a, const MeshPart *b) {
                                                 return depthOf(a) < depthOf(b);
                                             });
                        }

                        // Disegna solo se entrambe le pipeline di trasparenza sono
                        // valide: dopo un errore di compilazione una puo' essere
                        // nulla, e passarla a Metal causa EXC_BAD_ACCESS.
                        if (m_pipelineTranspBack && m_pipelineTranspFront) {
                            cb->setGraphicsPipeline(m_pipelineTranspBack);
                            for (const MeshPart *p : transp)
                                drawPart(*p, p->meshIndex * m_uboBlockStride);

                            cb->setGraphicsPipeline(m_pipelineTranspFront);
                            for (const MeshPart *p : transp)
                                drawPart(*p, p->meshIndex * m_uboBlockStride);
                        }
                    } else if (alpha < 0.99f) {
                        if (m_pipelineTranspBack && m_pipelineTranspFront) {
                            cb->setGraphicsPipeline(m_pipelineTranspBack);
                            drawWhole();
                            cb->setGraphicsPipeline(m_pipelineTranspFront);
                            drawWhole();
                        }
                    } else {
                        if (m_pipelineOpaque) {
                            cb->setGraphicsPipeline(m_pipelineOpaque);
                            drawWhole();
                        }
                    }
                }
            }

            // WIREFRAME, dopo il solido (vedi la nota sull'ordine sopra).
            if (anyWireframe) {
                if (m_wireframePipeline && m_wireframeIndexCount > 0) {
                    cb->setGraphicsPipeline(m_wireframePipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbo, 0);

                    if (m_wireframeRanges.size() <= 1) {
                        const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                        cb->setShaderResources(srb, 1, &ofs);
                        cb->setVertexInput(0, 1, &vbufBinding, m_wireframeIbo, 0, QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(m_wireframeIndexCount);
                    } else {
                        for (const WireframeRange &r : m_wireframeRanges) {
                            if (r.indexCount <= 0) continue;
                            // Salta le parti che NON sono in wireframe: i loro
                            // indici esistono comunque nel buffer (buildWireframe
                            // genera un range per ogni parte), ma vanno disegnate
                            // dal ramo solido.
                            if (r.meshIndex >= 0 && r.meshIndex < (int)parts.size()
                                && !partIsWireframe(parts[r.meshIndex])) continue;
                            const QRhiCommandBuffer::DynamicOffset ofs(0, r.meshIndex * m_uboBlockStride);
                            cb->setShaderResources(srb, 1, &ofs);
                            // Gli indici del wireframe sono ASSOLUTI: l'offset
                            // della parte si applica all'index buffer (in byte),
                            // non ai vertici.
                            cb->setVertexInput(0, 1, &vbufBinding, m_wireframeIbo,
                                               r.indexOffset * sizeof(unsigned int),
                                               QRhiCommandBuffer::IndexUInt32);
                            cb->drawIndexed(r.indexCount);
                        }
                    }
                }
            }

            // BORDO IN WIREFRAME: con la pipeline a linee, dopo il solido per
            // la stessa ragione del wireframe della superficie (scrive la
            // profondita' e non deve nascondere le parti trasparenti).
            if (m_borderRadius > 0.0f && m_borderRenderMode == 2 && m_indexCount > 0
                && m_wireframePipeline && m_borderVbo && m_borderWireIbo && m_borderUploaded) {
                const int partSlots = std::max(1, (int)parts.size());
                cb->setGraphicsPipeline(m_borderPipelineWire ? m_borderPipelineWire : m_wireframePipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                const QRhiCommandBuffer::VertexInput borderBinding(m_borderVbo, 0);
                for (const SurfaceEngine::BorderRange &r : engine->getBorderRanges()) {
                    if (r.wireIndexCount <= 0) continue;
                    const int slot = partSlots + std::clamp(r.meshIndex, 0, partSlots - 1);
                    const QRhiCommandBuffer::DynamicOffset ofs(0, slot * m_uboBlockStride);
                    cb->setShaderResources(srb, 1, &ofs);
                    cb->setVertexInput(0, 1, &borderBinding, m_borderWireIbo,
                                       r.wireIndexOffset * sizeof(unsigned int),
                                       QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(r.wireIndexCount, 1, 0, r.vertexOffset);
                }
            }

        }

        cb->endPass();

    } else if (m_engineMode == ModeImplicit) {
        // ==========================================
        // MODALITÀ IMPLICITA (RAY MARCHING)
        // ==========================================
        if (!m_pipelineImplicit) buildImplicitPipeline();

        QColor clearColor = QColor::fromRgbF(m_bgColor.x(), m_bgColor.y(), m_bgColor.z());
        cb->beginPass(renderTarget(), clearColor, { 1.0f, 0 }, resourceUpdates);

        if (m_isFlatView) {
            if (m_flatViewTarget == 1 && m_bgPipeline && m_bgVbo) {
                cb->setGraphicsPipeline(m_bgPipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bgBindings);
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        } else {
            if (m_useBackgroundTexture) {
                if (!m_bgPipeline) rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
                if (m_bgPipeline && m_bgVbo) {
                    cb->setGraphicsPipeline(m_bgPipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(m_bgBindings);
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
            }

            if (m_pipelineImplicit && m_bgVbo) {
                cb->setGraphicsPipeline(m_pipelineImplicit);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(m_bindings);

                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
        }

        cb->endPass();
    }
}

void GLWidget::releaseResources()
{
    releaseTubeFramePipeline();
    releaseSharedEdgePipeline();
    if (m_sharedEdgeTex) {
        m_sharedEdgeTex->destroy();
        delete m_sharedEdgeTex;
        m_sharedEdgeTex = nullptr;
    }
    if (m_sharedEdgeSampler) {
        m_sharedEdgeSampler->destroy();
        delete m_sharedEdgeSampler;
        m_sharedEdgeSampler = nullptr;
    }
    if (m_sharedEdgeParts) {
        m_sharedEdgeParts->destroy();
        delete m_sharedEdgeParts;
        m_sharedEdgeParts = nullptr;
    }
    if (m_sharedEdgeSamples) {
        m_sharedEdgeSamples->destroy();
        delete m_sharedEdgeSamples;
        m_sharedEdgeSamples = nullptr;
    }
    if (m_sharedEdgeRaw) {
        m_sharedEdgeRaw->destroy();
        delete m_sharedEdgeRaw;
        m_sharedEdgeRaw = nullptr;
    }
    if (m_sharedEdgeTiles) {
        m_sharedEdgeTiles->destroy();
        delete m_sharedEdgeTiles;
        m_sharedEdgeTiles = nullptr;
    }
    m_sharedEdgeTable.clear();
    m_sharedEdgeSourceDirty = !m_sharedEdgeSource.isEmpty();
    if (m_tubeFrameTex) {
        m_tubeFrameTex->destroy();
        delete m_tubeFrameTex;
        m_tubeFrameTex = nullptr;
    }
    if (m_tubeFrameSampler) {
        m_tubeFrameSampler->destroy();
        delete m_tubeFrameSampler;
        m_tubeFrameSampler = nullptr;
    }
    if (m_tubeFrameWork) {
        m_tubeFrameWork->destroy();
        delete m_tubeFrameWork;
        m_tubeFrameWork = nullptr;
    }
    if (m_borderEdgeTex) {
        m_borderEdgeTex->destroy();
        delete m_borderEdgeTex;
        m_borderEdgeTex = nullptr;
    }
    if (m_borderEdgeSampler) {
        m_borderEdgeSampler->destroy();
        delete m_borderEdgeSampler;
        m_borderEdgeSampler = nullptr;
    }
    if (m_dummyTexture) {
        m_dummyTexture->destroy();
        delete m_dummyTexture;
        m_dummyTexture = nullptr;
    }
    if (m_sampler) {
        m_sampler->destroy();
        delete m_sampler;
        m_sampler = nullptr;
    }
    if (m_bindings) {
        delete m_bindings;
        m_bindings = nullptr;
    }
    if (m_bindingsDyn) {
        delete m_bindingsDyn;
        m_bindingsDyn = nullptr;
        m_bindingsDynTexture = nullptr;
    }
    if (m_ubo) {
        delete m_ubo;
        m_ubo = nullptr;
        m_uboBlockCapacity = 0;
    }
    if (m_vbo) {
        delete m_vbo;
        m_vbo = nullptr;
    }
    if (m_ibo) {
        delete m_ibo;
        m_ibo = nullptr;
    }
    if (m_wireframeIbo) {
        delete m_wireframeIbo;
        m_wireframeIbo = nullptr;
    }
    if (m_borderVbo) {
        delete m_borderVbo;
        m_borderVbo = nullptr;
    }
    if (m_borderIbo) {
        delete m_borderIbo;
        m_borderIbo = nullptr;
    }
    if (m_borderWireIbo) {
        delete m_borderWireIbo;
        m_borderWireIbo = nullptr;
    }
    m_borderUploaded = false;
    if (m_wireframePipeline) {
        delete m_wireframePipeline;
        m_wireframePipeline = nullptr;
    }
    if (m_surfaceTexture) {
        m_surfaceTexture->destroy();
        delete m_surfaceTexture;
        m_surfaceTexture = nullptr;
    }
    releasePartImages();
    if (m_bgVbo) {
        delete m_bgVbo;
        m_bgVbo = nullptr;
    }
    if (m_bgPipeline) {
        delete m_bgPipeline;
        m_bgPipeline = nullptr;
    }
    if (m_bgBindings) {
        delete m_bgBindings;
        m_bgBindings = nullptr;
    }
    if (m_backgroundTexture) {
        m_backgroundTexture->destroy();
        delete m_backgroundTexture;
        m_backgroundTexture = nullptr;
    }
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }
    if (m_pipelineTranspBack) {
        delete m_pipelineTranspBack;
        m_pipelineTranspBack = nullptr;
    }
    if (m_pipelineTranspFront) {
        delete m_pipelineTranspFront;
        m_pipelineTranspFront = nullptr;
    }
    if (m_borderPipelineOpaque) {
        delete m_borderPipelineOpaque;
        m_borderPipelineOpaque = nullptr;
    }
    if (m_borderPipelineWire) {
        delete m_borderPipelineWire;
        m_borderPipelineWire = nullptr;
    }
    if (m_bgUbo) {
        delete m_bgUbo;
        m_bgUbo = nullptr;
    }
    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    if (m_bindingsImplicit) {
        delete m_bindingsImplicit;
        m_bindingsImplicit = nullptr;
    }
}

void GLWidget::mousePressEvent(QMouseEvent *event) {
    m_inputHandler->handleMousePress(event);
}

void GLWidget::mouseMoveEvent(QMouseEvent *event) {
    m_inputHandler->handleMouseMove(event);
}

void GLWidget::wheelEvent(QWheelEvent *event)
{
    const bool zoomed = m_inputHandler && m_inputHandler->handleWheel(event);

    event->accept();

    // Zoom col mouse: e' l'utente che cambia la vista. Solo se lo zoom e'
    // avvenuto davvero: prima il segnale partiva per QUALUNQUE evento di
    // rotellina, e il trackpad ne manda anche a delta verticale nullo (scroll
    // orizzontale, fine del gesto) -- passarci sopra col puntatore sporcava la
    // scena senza toccarla, e alla chiusura del preset usciva "vuoi salvare?".
    // Idem durante un path, dove lo zoom 3D e' bloccato.
    if (zoomed) emit userMovedView();

    update();
}

bool GLWidget::event(QEvent *e)
{
    // INIZIO DI UN TOCCO: lo si segnala PRIMA di handleTouch, che consuma
    // l'evento e ritorna true -- da li' in poi nessun evento di mouse viene
    // sintetizzato, quindi su iOS un eventFilter non vedrebbe mai il tocco.
    // Chi ascolta decide se il punto lo riguarda (vedi il messaggio in
    // sovrimpressione in MainWindow); il gesto prosegue comunque normalmente,
    // cosi' rotazione e pinch restano quelli di sempre.
    if (e->type() == QEvent::TouchBegin) {
        auto *te = static_cast<QTouchEvent *>(e);
        if (!te->points().isEmpty())
            emit scenePressedAt(te->points().first().position().toPoint());
    }

    if (m_inputHandler && m_inputHandler->handleTouch(e)) {
        // Touch: e' l'utente che cambia la vista, esattamente come la rotellina
        // e il trascinamento col mouse qui sopra. Il tocco non ha un "release"
        // da cui dedurlo (il pinch non ne ha affatto), quindi e' l'handler a
        // dire se la vista si e' davvero mossa -- il tap secco e il tocco sotto
        // la deadzone non contano. Senza questo, su mobile ne' la rotazione ne'
        // lo zoom sporcavano la scena: nessun avviso "vuoi salvare?".
        if (m_inputHandler->takeTouchMovedView()) emit userMovedView();
        return true;
    }

    // Usiamo QRhiWidget invece di QOpenGLWidget
    return QRhiWidget::event(e);
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(event);
    }

    // Trascinamento vero (non un clic secco, non un release orfano): l'utente
    // ha ruotato/spostato la vista a mano in un gesto iniziato qui.
    if (m_inputHandler && m_inputHandler->takeMouseMovedView()) emit userMovedView();

    // Usiamo QRhiWidget invece di QOpenGLWidget
    return QRhiWidget::mouseReleaseEvent(event);
}


//===========================================================
// PUBLIC SLOTS
//===========================================================

void GLWidget::rebuildBackgroundShader(bool isTextureMode, const QString &customCode) {
    if (!m_bgVbo) initBackgroundShader();

    if (m_bgPipeline) {
        delete m_bgPipeline;
        m_bgPipeline = nullptr;
    }

    m_bgPipeline = rhi()->newGraphicsPipeline();
    m_bgPipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    m_bgPipeline->setVertexInputLayout(inputLayout);

    QString header = "#version 450\n";

    QString vsSource = header +
                       "layout(location=0) in vec3 position;\n"
                       "layout(location=1) in vec4 normal;\n"
                       "layout(location=2) in vec2 texCoord;\n"
                       "layout(location=0) out vec2 v_texCoord;\n"
                       "void main() {\n"
                       "  v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);\n"
                       "  gl_Position = vec4(position.xy, 0.999, 1.0);\n"
                       "}\n";


    QString fsSource = createBackgroundFragmentShader(isTextureMode, customCode);

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    m_bgPipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_bgPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_bgPipeline->setSampleCount(renderTarget()->sampleCount());
    m_bgPipeline->setDepthTest(false);
    m_bgPipeline->setDepthWrite(false);
    m_bgPipeline->setCullMode(QRhiGraphicsPipeline::None);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_bgPipeline->setTargetBlends({ blend });
    m_bgPipeline->setShaderResourceBindings(m_bgBindings);
    m_bgPipeline->create();
}

// ==========================================================
// PRIVATE SLOTS
// ==========================================================

void GLWidget::updateRotation() {
    // Clock esterno attivo (registrazione): il tick live e' un no-op, ad
    // avanzare le rotazioni e' il loop del recorder (advanceRotationsBy col
    // dt virtuale del frame). Il timer resta attivo apposta: e' lo STATO.
    if (m_externalClockActive) return;

    advanceRotationsBy(kRotationTickMs / 1000.0f);

    // NB: la rotazione 4D NON cambia i dati CPU della mesh: la rotazione XW/YW/ZW
    // e la proiezione 4D->3D avvengono nel VERTEX SHADER leggendo omega/phi/psi
    // dall'UBO, che render() ricarica a OGNI frame (updateDynamicBuffer(m_ubo)).
    // Settare meshNeedsUpdate qui forzava un re-upload INTEGRALE di VBO+IBO a ogni
    // frame con dati IDENTICI: spreco puro che su iOS/Metal (memoria stretta +
    // watchdog jetsam) accumula buffer transitori e fa killare l'app dopo qualche
    // decina di secondi, specie su superfici dense (steps alti) in wireframe.
    // Basta l'update() qui sotto: ridisegna col nuovo omega senza toccare la mesh.

    emit rotationChanged();
    update();
}

void GLWidget::advanceRotationsBy(float dtSeconds) {
    // L'unita' di avanzamento e' il tick live di rotationTimer: le velocita'
    // dei dock sono tarate su quello. Convertire dtSeconds in tick
    // equivalenti garantisce che il recorder, passando il dt del suo frame
    // virtuale (1/fps), avanzi esattamente alla velocita' vista a schermo.
    float ticks = dtSeconds / (kRotationTickMs / 1000.0f);

    float speedMult3D = 2.0f * ticks;
    float speedMult4D = 0.05f * ticks;

    float dPrec = precessionSpeed * speedMult3D;
    float dNut  = nutationSpeed   * speedMult3D;
    float dSpin = spinSpeed       * speedMult3D;

    if (std::abs(dPrec) > 0.0001f || std::abs(dNut) > 0.0001f || std::abs(dSpin) > 0.0001f) {
        addObjectRotation(dPrec, dNut, dSpin);
    }

    // ------------- Rotazioni 4D -----------
    omega += omegaSpeed * speedMult4D;
    phi   += phiSpeed   * speedMult4D;
    psi   += psiSpeed   * speedMult4D;
}


// ==========================================================
// ENGINE MODES
// ==========================================================

void GLWidget::setEngineMode(EngineMode mode)
{
    if (m_engineMode == mode) return;

    // 1. SALVA lo stato del "Banco di Lavoro" nello slot che stiamo abbandonando
    int oldIndex = (m_engineMode == ModeParametric) ? 0 : 1;
    m_viewStates[oldIndex].cameraPos    = m_cameraPos;
    m_viewStates[oldIndex].cameraYaw    = m_cameraYaw;
    m_viewStates[oldIndex].cameraPitch  = m_cameraPitch;
    m_viewStates[oldIndex].cameraRoll   = m_cameraRoll;
    m_viewStates[oldIndex].rotationQuat = m_rotationQuat;
    m_viewStates[oldIndex].flatZoom     = m_flatZoom;
    m_viewStates[oldIndex].flatPan      = m_flatPan;
    m_viewStates[oldIndex].flatRotation = m_flatRotation;
    m_viewStates[oldIndex].globalTexZoom     = m_globalTexZoom;
    m_viewStates[oldIndex].globalTexPan      = m_globalTexPan;
    m_viewStates[oldIndex].globalTexRotation = m_globalTexRotation;

    // 2. Cambia la modalità attiva
    m_engineMode = mode;

    // 3. RIPRISTINA i dati nel "Banco di Lavoro" dallo slot in cui stiamo entrando
    int newIndex = (m_engineMode == ModeParametric) ? 0 : 1;
    m_cameraPos    = m_viewStates[newIndex].cameraPos;
    m_cameraYaw    = m_viewStates[newIndex].cameraYaw;
    m_cameraPitch  = m_viewStates[newIndex].cameraPitch;
    m_cameraRoll   = m_viewStates[newIndex].cameraRoll;
    m_rotationQuat = m_viewStates[newIndex].rotationQuat;
    m_flatZoom     = m_viewStates[newIndex].flatZoom;
    m_flatPan      = m_viewStates[newIndex].flatPan;
    m_flatRotation = m_viewStates[newIndex].flatRotation;
    m_globalTexZoom     = m_viewStates[newIndex].globalTexZoom;
    m_globalTexPan      = m_viewStates[newIndex].globalTexPan;
    m_globalTexRotation = m_viewStates[newIndex].globalTexRotation;

    update();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool GLWidget::setParametricEquations(const QString &xEq, const QString &yEq,
                                      const QString &zEq, const QString &wEq)
{
    m_isCustomMesh = false;
    QString oldX = m_eqX, oldY = m_eqY, oldZ = m_eqZ, oldW = m_eqW;

    // Le equazioni sostituiscono un eventuale tubo: la prova si fa senza.
    const bool oldTube = engine->isTubeModeActive();
    engine->setTubeMode(false);

    // 1. DRY RUN del vertex con le NUOVE equazioni prima di toccare lo stato
    QString vsSource = createVertexShaderSource(xEq, yEq, zEq, wEq);
    QShaderBaker baker;
    baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "VERTEX (parametric): " + baker.errorMessage();
        engine->setTubeMode(oldTube);
        return false;  // Non tocchiamo nulla, l'UI riceve false e mostra il popup
    }

    // 2. Solo ora applichiamo
    m_eqX = xEq; m_eqY = yEq; m_eqZ = zEq; m_eqW = wEq;
    engine->setEquations(xEq, yEq, zEq, wEq);
    rebuildShader();
    meshNeedsUpdate = true;
    update();
    return true;
}

// Gli offset di UboData devono combaciare col blocco SceneUBO degli shader
// (verificati con qsb --dump, vedi CLAUDE.md): un campo spostato qui non da'
// errori su desktop e su Adreno fa fallire tutte le pipeline.
static_assert(offsetof(UboData, u_min) == 372, "UboData: u_min fuori posto");
static_assert(offsetof(UboData, z_max) == 416, "UboData: z_max fuori posto");
static_assert(offsetof(UboData, u_meshIndex) == 420, "UboData: u_meshIndex fuori posto");
static_assert(offsetof(UboData, u_noImage) == 424, "UboData: u_noImage fuori posto");
static_assert(offsetof(UboData, u_marcherMode) == 436, "UboData: u_marcherMode fuori posto");
static_assert(offsetof(UboData, u_tubeRadius) == 440, "UboData: u_tubeRadius fuori posto");
static_assert(offsetof(UboData, u_borderRadius) == 444, "UboData: u_borderRadius fuori posto");
static_assert(offsetof(UboData, u_isBorder) == 448, "UboData: u_isBorder fuori posto");
static_assert(offsetof(UboData, u_tubeRefX) == 452, "UboData: u_tubeRefX fuori posto");
static_assert(offsetof(UboData, u_tubeRefZ) == 460, "UboData: u_tubeRefZ fuori posto");
static_assert(offsetof(UboData, u_meshCount) == 464, "UboData: u_meshCount fuori posto");
static_assert(offsetof(UboData, u_tubeRings) == 468, "UboData: u_tubeRings fuori posto");
static_assert(offsetof(UboData, u_tubeFramesOn) == 472, "UboData: u_tubeFramesOn fuori posto");
static_assert(offsetof(UboData, u_tubeFrameBlend) == 476, "UboData: u_tubeFrameBlend fuori posto");
static_assert(offsetof(UboData, u_sharedEdgesOn) == 480, "UboData: u_sharedEdgesOn fuori posto");
static_assert(offsetof(UboData, u_globalTexture) == 484, "UboData: u_globalTexture fuori posto");
static_assert(sizeof(UboData) == 496, "UboData: dopo u_globalTexture (e _pad4) il blocco e' di 496 byte");
static_assert(sizeof(UboData) % 16 == 0, "UboData: dimensione non multipla di 16 (std140)");

bool GLWidget::setTubeCurve(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq,
                            const std::vector<SurfaceEngine::TubeCopy> &copies)
{
    return applyTubeSource([&] { engine->setTubeCurve(xEq, yEq, zEq, wEq); }, copies);
}

bool GLWidget::setTubeScript(const QString &glslBody, const std::vector<SurfaceEngine::TubeCopy> &copies)
{
    return applyTubeSource([&] { engine->setTubeScriptBody(glslBody); }, copies);
}

bool GLWidget::applyTubeSource(const std::function<void()> &setCurve,
                               const std::vector<SurfaceEngine::TubeCopy> &copies)
{
    m_isCustomMesh = false;
    // Stato di prima, per tornarci se la prova fallisce.
    const bool oldTube = engine->isTubeModeActive();
    const bool oldScript = engine->isScriptModeActive();
    const QString oldX = engine->tubeX(), oldY = engine->tubeY(), oldZ = engine->tubeZ(), oldP = engine->tubeP();
    const QString oldBody = engine->tubeScriptBody();
    const std::vector<SurfaceEngine::TubeCopy> oldCopies = engine->tubeCopies();

    setCurve();
    engine->setTubeCopies(copies);
    engine->setTubeMode(true);
    engine->setScriptMode(false);

    // DRY RUN del vertex, come setParametricEquations.
    QString vsSource = createVertexShaderSource("0", "0", "0", "0");
    QShaderBaker baker;
    baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "VERTEX (tube): " + baker.errorMessage();
        if (!oldBody.isEmpty()) engine->setTubeScriptBody(oldBody);
        else engine->setTubeCurve(oldX, oldY, oldZ, oldP);
        engine->setTubeCopies(oldCopies);
        engine->setTubeMode(oldTube);
        engine->setScriptMode(oldScript);
        return false;
    }

    rebuildShader();
    meshNeedsUpdate = true;
    update();
    return true;
}

void GLWidget::setTubeCopies(const std::vector<SurfaceEngine::TubeCopy> &copies)
{
    engine->setTubeCopies(copies);
    // La chiusura in u e' delle parti: si rigenerano.
    meshNeedsUpdate = true;
    update();
}

QString GLWidget::tubeCurveFunction(const QString &xEq, const QString &yEq, const QString &zEq,
                                    const QString &wEq, const QString &scriptBody)
{
    QString body;
    if (!scriptBody.trimmed().isEmpty()) {
        // SCRIPT: come quelli della superficie, interi in float e variabili di
        // supporto (costanti, t, mesh) iniettate se lo script non le dichiara.
        QString code = scriptBody;
        static const QRegularExpression regInt("(?<=[^a-zA-Z0-9_.]|^)([0-9]+)(?=[^a-zA-Z0-9_.]|$)");
        code.replace(regInt, "\\1.0");
        body = generateGlslHelperVars(code) + code;
    } else {
        // EQUAZIONI: come quelle della superficie (traduzione, vuoto = 0).
        auto eq = [](const QString &s) {
            const QString translated = GlslTranslator::translateEquation(s);
            if (translated.trimmed().isEmpty()) return QString("0.0");
            return "float(" + translated + ")";
        };
        body = "    float A = ubuf.u_mathParams.x;\n"
               "    float B = ubuf.u_mathParams.y;\n"
               "    float C = ubuf.u_mathParams.z;\n"
               "    float D = ubuf.u_mathParams2.x;\n"
               "    float E = ubuf.u_mathParams2.y;\n"
               "    float F = ubuf.u_mathParams2.z;\n"
               "    float s = ubuf.u_mathParams.w;\n"
               "    float t = ubuf.u_time;\n"
               "    float mesh = ubuf.u_meshIndex;\n"
               "    float meshes = ubuf.u_meshCount;\n"
               "    return vec4(" + eq(xEq) + ", " + eq(yEq) + ", " + eq(zEq) + ", " + eq(wEq) + ");\n";
    }
    return "vec4 tubeCurve(float u, float v, float w) {\n" + body + "\n}\n";
}

void GLWidget::useImplicit3DBranch()
{
    if (!m_implicitUsesCrossSection) return;
    m_implicitUsesCrossSection = false;
    // Lo shader implicito va rifatto: includeva la rotazione della sezione.
    if (m_engineMode == ModeImplicit) rebuildShader();
}

void GLWidget::setImplicitEquation(const QString &eqF)
{
    // L'equazione qui e' quella del sotto-tab 3D (3 variabili): impostarla
    // significa che il ramo attivo torna a essere il 3D. Senza questa riga
    // m_implicitUsesCrossSection restava APPICCICATO a true dopo una visita al
    // Cross Section, e createImplicitFragmentShader continuava a leggere
    // m_eqCrossSectionF: ogni superficie RM caricata da qui veniva disegnata con
    // l'equazione 4D della sessione precedente -- "quasi tutte rotte" finche' non
    // si riavviava l'app (che e' l'unica cosa che azzerava il flag).
    // NB: sta PRIMA del confronto qui sotto. Se l'equazione coincide con quella
    // 3D gia' in memoria la funzione esce subito, ma il ramo da riportare al 3D
    // va corretto lo stesso -- anzi, e' proprio il caso in cui serve di piu':
    // caricare due volte la stessa superficie 3D dopo il Cross Section.
    const bool branchChanged = m_implicitUsesCrossSection;
    m_implicitUsesCrossSection = false;

    if (m_eqImplicitF == eqF && !branchChanged) return;

    m_eqImplicitF = eqF;
    detectImplicitConditioning(eqF);

    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

// Rileva EMPIRICAMENTE se il campo implicito e' "mal condizionato" (prodotto di
// piu' fattori, es. preset "Chain"). Non e' un'euristica sulla stringa: valuta il
// campo su una griglia CPU (exprtk) e misura il RANGE DINAMICO *SULLA SUPERFICIE*.
//
// La chiave e' misurare |f| SOLO vicino a f=0 (celle adiacenti a un cambio di segno),
// NON su tutto il box. Un polinomio a espressione singola di grado alto (es. "Filament
// Cube", grado ~12) esplode nel vuoto lontano dalle sue radici (~1e12) pur restando
// SANO sulla superficie (~1): misurando il max globale sarebbe un FALSO POSITIVO. Un
// prodotto di piu' fattori (es. "Chain", 6 tori) invece resta enorme ANCHE vicino ai
// suoi crossing (~1e8), perche' ogni crossing porta con se' il prodotto degli altri
// fattori grandi. La banda near-surface separa i due con ~6 ordini di margine.
//
// Se max|f| near-surface sfonda la soglia -> il ramo trasparente aggancerebbe crossing
// FANTASMA (cambi di segno per parita' del prodotto, con salti di scala enormi) e la
// superficie sparirebbe con alpha<1: settiamo m_implicitIllConditioned = true cosi' il
// render forza alpha=1.0 e la UI disabilita lo slider. Vedi m_implicitIllConditioned in
// glwidget.h per il razionale completo.
void GLWidget::detectImplicitConditioning(const QString &eqF)
{
    m_implicitIllConditioned = false;
    m_implicitTransparencyWarn = false;   // SOLO Android; ricalcolato sotto
    if (eqF.trimmed().isEmpty()) return;

    // I chiamanti passano l'equazione in due forme: gia' sottratta "(LHS) - (RHS)"
    // oppure grezza "LHS = RHS". exprtk NON compila l'uguaglianza come espressione di
    // valore, quindi normalizziamo "A = B" -> "(A) - (B)" (altrimenti compile fallisce
    // e usciremmo con un falso negativo proprio sul "Chain").
    QString expr = eqF.trimmed();
    int eqPos = expr.indexOf('=');
    if (eqPos >= 0) {
        QString lhs = expr.left(eqPos).trimmed();
        QString rhs = expr.mid(eqPos + 1).trimmed();
        if (rhs.isEmpty()) rhs = "0.0";
        expr = QString("(%1) - (%2)").arg(lhs, rhs);
    }

    // Coordinate e costanti collegate per riferimento (exprtk richiede lvalue vivi).
    double x = 0.0, y = 0.0, z = 0.0, tVar = 0.0;
    double A = m_uboData.mathParams.x(), B = m_uboData.mathParams.y();
    double C = m_uboData.mathParams.z(), S = m_uboData.mathParams.w();
    double D = m_uboData.mathParams2.x(), E = m_uboData.mathParams2.y();
    double F = m_uboData.mathParams2.z();

    ExpressionParser parser;
    // Il campo implicito e' in x,y,z (+ eventuale t e costanti A..F,S). Aggiungiamo
    // solo questi simboli: setupConstants collega A..F,S e crea la symbol_table.
    parser.setupConstants(A, B, C, D, E, F, S);
    parser.addCustomVariable("x", x);
    parser.addCustomVariable("y", y);
    parser.addCustomVariable("z", z);
    parser.addCustomVariable("t", tVar);
    if (!parser.compile(expr)) {
        // se non compila lato CPU non blocchiamo nulla: lo shader la gestisce a modo suo
        return;
    }

    // Campioniamo dentro un box RAGIONEVOLE dove vive la geometria: intersezione tra
    // il bounding box UI e [-10,10]^3 (i clip box di default sono +/-1000, campionarli
    // tutti misurerebbe solo il vuoto lontano).
    const float clampR = 10.0f;
    float bxMin = std::max(m_uboData.x_min, -clampR), bxMax = std::min(m_uboData.x_max, clampR);
    float byMin = std::max(m_uboData.y_min, -clampR), byMax = std::min(m_uboData.y_max, clampR);
    float bzMin = std::max(m_uboData.z_min, -clampR), bzMax = std::min(m_uboData.z_max, clampR);
    if (bxMax <= bxMin) { bxMin = -clampR; bxMax = clampR; }
    if (byMax <= byMin) { byMin = -clampR; byMax = clampR; }
    if (bzMax <= bzMin) { bzMin = -clampR; bzMax = clampR; }

    // Griglia FITTA (40^3): i tubi dei tori sono sottili (raggio ~sqrt(0.4)~0.63);
    // una griglia grossa (12^3) li mancherebbe e non troverebbe i crossing. 40^3 = 64k
    // valutazioni exprtk: eseguito UNA volta per cambio-equazione, costo trascurabile.
    // Materializziamo l'intero campo in un buffer per poter confrontare i VICINI.
    const int N = 40;
    std::vector<double> vals(static_cast<size_t>(N) * N * N);
    auto idx = [N](int i, int j, int k) {
        return (static_cast<size_t>(i) * N + j) * N + k;
    };
    for (int i = 0; i < N; ++i) {
        x = bxMin + (bxMax - bxMin) * (i + 0.5) / N;
        for (int j = 0; j < N; ++j) {
            y = byMin + (byMax - byMin) * (j + 0.5) / N;
            for (int k = 0; k < N; ++k) {
                z = bzMin + (bzMax - bzMin) * (k + 0.5) / N;
                double v = parser.value();
                vals[idx(i, j, k)] = std::isfinite(v) ? v : std::numeric_limits<double>::quiet_NaN();
            }
        }
    }

    // Misuriamo max|f| SOLO nella banda near-surface: celle che hanno almeno un vicino
    // (6-connesso) di segno opposto. E' qui che il ramo trasparente aggancia i crossing;
    // e' qui che un campo a prodotto tradisce la sua scala enorme, mentre un polinomio
    // grado-alto sano (che esplode solo nel vuoto lontano) resta O(1).
    double maxNear = 0.0;
    auto opposite = [](double a, double b) {
        return std::isfinite(a) && std::isfinite(b) && ((a < 0.0) != (b < 0.0)) && a != 0.0 && b != 0.0;
    };
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k) {
                double v = vals[idx(i, j, k)];
                if (!std::isfinite(v)) continue;
                bool onSurface =
                    (i + 1 < N && opposite(v, vals[idx(i + 1, j, k)])) ||
                    (i - 1 >= 0 && opposite(v, vals[idx(i - 1, j, k)])) ||
                    (j + 1 < N && opposite(v, vals[idx(i, j + 1, k)])) ||
                    (j - 1 >= 0 && opposite(v, vals[idx(i, j - 1, k)])) ||
                    (k + 1 < N && opposite(v, vals[idx(i, j, k + 1)])) ||
                    (k - 1 >= 0 && opposite(v, vals[idx(i, j, k - 1)]));
                if (onSurface) {
                    double av = std::abs(v);
                    if (av > maxNear) maxNear = av;
                }
            }

    // Soglia empirica sul RANGE DINAMICO del campo NEAR-SURFACE. Verificato valutando
    // TUTTI i preset impliciti Ray Marching su griglia 40^3: il "Chain" (prodotto di 6
    // tori) resta a ~1e8 anche sui suoi crossing, mentre OGNI superficie a espressione
    // singola sta sotto ~20 (peggiori: KleinIso ~21, DuplinCyclides ~14) — incluso il
    // "Filament Cube" grado ~12 che vale ~1 sulla superficie pur esplodendo a ~1e12 nel
    // vuoto lontano (era il falso positivo della vecchia soglia globale 1e10). La soglia
    // 1e5 lascia ~3.7 ordini di margine su ENTRAMBI i lati (sopra il peggior legittimo,
    // sotto il "Chain"): separazione netta e robusta.
    const double ILL_THRESHOLD = 1.0e5;
    m_implicitIllConditioned = (maxNear > ILL_THRESHOLD);

    if (m_implicitIllConditioned) {
        qDebug() << "[implicit] campo mal condizionato (prodotto): max|f| near-surface ="
                 << maxNear << "-> trasparenza disabilitata (fallback opaco)";
    }

#if defined(Q_OS_ANDROID)
    // TROPPE FACCE PER RAGGIO — SOLO ANDROID (desktop/iOS: questo blocco non esiste
    // nemmeno, #if compilato via). Il ramo trasparente RM compone al massimo MAX_FACES
    // gusci per pixel; su Android MAX_FACES/MAX_LAYER_STEPS sono ridotti (4/600 vs
    // 8/2000 desktop) come rete anti-timeout-GPU. Le superfici triplamente periodiche
    // (Gyroid, Lidinoid, Holes...) attraversano MOLTE facce lungo un raggio e sforano
    // quel budget: con alpha<1 su Android la superficie si TAGLIA (facce oltre MAX_FACES
    // non composte). Non potendo alzare i limiti senza reintrodurre il fault GPU,
    // AVVISIAMO l'utente: alziamo m_implicitTransparencyWarn -> lo slider resta USABILE
    // (nessun blocco, nessun fallback opaco) e la UI mostra un popup di avviso al primo
    // tocco dell'alpha. Diverso dal "Chain" (m_implicitIllConditioned), che invece
    // sparisce del tutto e resta bloccato+opaco su tutte le piattaforme.
    //
    // STIMA DELLE FACCE: contiamo i cambi di segno del campo lungo 7 raggi che
    // attraversano il CENTRO del box (3 assi + 4 diagonali dello spazio), a passo fine
    // 0.05 come il marcher. La griglia 40^3 sopra e' troppo grossa (celle ~0.5) per
    // contare le facce di un campo con periodo ~2*PI, e i raggi assiali sottostimano
    // (i raggi reali della camera lontana tagliano in diagonale): i raggi centrali
    // colgono il caso peggiore. Taratura su tutti i preset impliciti RM: le periodiche
    // problematiche danno 13-26 crossing, tutte le legittime <=6 (Chmutov 6, Blobs 4,
    // sfere/tori/quadriche <=4). Soglia 8 (= 2 * MAX_FACES Android) nel mezzo, con
    // margine su entrambi i lati. Solo per superfici a EQUAZIONE (parser CPU); gli
    // script RM sono avvisati preventivamente altrove (non valutabili su CPU).
    if (!m_implicitIllConditioned) {
        const int    ANDROID_MAX_FACES = 4;                 // deve combaciare col %MAX_FACES% Android
        const int    CROSSINGS_LIMIT   = 2 * ANDROID_MAX_FACES;
        const double rayStep = 0.05;
        const double cx = 0.5 * (bxMin + bxMax);
        const double cy = 0.5 * (byMin + byMax);
        const double cz = 0.5 * (bzMin + bzMax);
        const double hx = 0.5 * (bxMax - bxMin);
        const double hy = 0.5 * (byMax - byMin);
        const double hz = 0.5 * (bzMax - bzMin);
        const double dirs[7][3] = {
            {1,0,0}, {0,1,0}, {0,0,1},
            {1,1,1}, {1,-1,1}, {1,1,-1}, {-1,1,1}
        };
        int maxCrossings = 0;
        for (int r = 0; r < 7 && maxCrossings <= CROSSINGS_LIMIT; ++r) {
            double dx = dirs[r][0], dy = dirs[r][1], dz = dirs[r][2];
            double dlen = std::sqrt(dx*dx + dy*dy + dz*dz);
            dx /= dlen; dy /= dlen; dz /= dlen;
            // Estensione del raggio: mezza-diagonale del box, in entrambi i versi.
            double L = std::sqrt(hx*hx + hy*hy + hz*hz);
            int crossings = 0;
            double prev = 0.0; bool havePrev = false;
            for (double s = -L; s <= L; s += rayStep) {
                x = cx + dx * s; y = cy + dy * s; z = cz + dz * s;
                if (std::abs(x - cx) > hx || std::abs(y - cy) > hy || std::abs(z - cz) > hz)
                    continue; // fuori dal box di taglio
                double v = parser.value();
                if (!std::isfinite(v)) { havePrev = false; continue; }
                if (havePrev && ((prev < 0.0) != (v < 0.0)) && prev != 0.0 && v != 0.0)
                    ++crossings;
                prev = v; havePrev = true;
            }
            if (crossings > maxCrossings) maxCrossings = crossings;
        }
        if (maxCrossings > CROSSINGS_LIMIT) {
            m_implicitTransparencyWarn = true;
            qDebug() << "[implicit] Android: troppe facce per raggio (crossings ="
                     << maxCrossings << "> " << CROSSINGS_LIMIT
                     << ") -> avviso trasparenza (slider resta usabile)";
        }
    }
#endif
}

void GLWidget::setEquationConstants(float a, float b, float c, float d, float e, float f, float s) {
    engine->setConstants(a, b, c, d, e, f, s);
    // Salviamo nel pacchetto UBO!
    m_uboData.mathParams = QVector4D(a, b, c, s);
    m_uboData.mathParams2 = QVector4D(d, e, f, 0.0f);
    meshNeedsUpdate = true;

    m_constants.clear();
    m_constants["A"] = a;
    m_constants["B"] = b;
    m_constants["C"] = c;
    m_constants["D"] = d;
    m_constants["E"] = e;
    m_constants["F"] = f;
    m_constants["S"] = s;

    update();
}

void GLWidget::setRangeU(float min, float max) {
    if (engine) engine->setRangeU(min, max);
    m_uboData.u_min = min;
    m_uboData.u_max = max;
    meshNeedsUpdate = true;
}

void GLWidget::setRangeV(float min, float max) {
    if (engine) engine->setRangeV(min, max);
    m_uboData.v_min = min;
    m_uboData.v_max = max;
    meshNeedsUpdate = true;
}

void GLWidget::setRangeW(float min, float max)
{
    if (engine) engine->setRangeW(min, max);
    meshNeedsUpdate = true;
}

void GLWidget::setRangeX(float min, float max) {
    m_uboData.x_min = min;
    m_uboData.x_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRangeY(float min, float max) {
    m_uboData.y_min = min;
    m_uboData.y_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setRangeZ(float min, float max) {
    m_uboData.z_min = min;
    m_uboData.z_max = max;
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setResolution(int n) {

    engine->setResolution(n, n);
    meshNeedsUpdate = true;
}

void GLWidget::setRaySteps(int steps) {
    m_raySteps = std::clamp(steps, 1, kRayStepsMax);   // il tetto del ciclo nello shader
    update();           // Richiede un nuovo fotogramma
}

bool GLWidget::setCustomMesh(const QVector<QVector<QVector4D>>& grid, bool tolerateTruncated,
                             float uMin, float uMax, float vMin, float vMax)
{
    bool wasCustom = m_isCustomMesh; // Salviamo lo stato precedente
    m_isCustomMesh = true;

    if (grid.isEmpty() || grid[0].isEmpty()) return false;

    int numU = grid.size() - 1;
    int numV = grid[0].size() - 1;

    // =====================================================================
    // 1. CONTROLLO MATEMATICO GENERALE (Anticipazione Errori e Artefatti)
    // =====================================================================
    int deadTrajectories = 0;

    // Soglia coerente con safeLim in GeodesicCalculator, scalata in base alla scena.
    const float limitThreshold = 999.0f * m_surfaceScale / 2.0f;

    // NUOVO: Limite di salto. Se un singolo step schizza oltre 20 unità,
    // l'equazione differenziale sta esplodendo generando artefatti.
    const float jumpThreshold = 20.0f;

    for (int i = 0; i <= numU; ++i) {
        bool isDead = false;

        // A. Punto "nato morto" (il calcolatore ha intercettato un NaN al primo step)
        if (grid[i][0].lengthSquared() < 1e-8f && grid[i][1].lengthSquared() < 1e-8f) {
            isDead = true;
        } else {
            for (int j = 0; j <= numV; ++j) {
                // B. Controllo sul limite spaziale assoluto (Anticipato a 250)
                if (std::abs(grid[i][j].x()) > limitThreshold ||
                    std::abs(grid[i][j].y()) > limitThreshold ||
                    std::abs(grid[i][j].z()) > limitThreshold) {
                    isDead = true;
                    break;
                }

                // C. NUOVO: Controllo sull'esplosione della derivata (Jump Check)
                if (j > 0) {
                    float distSq = (grid[i][j] - grid[i][j-1]).lengthSquared();
                    if (distSq > (jumpThreshold * jumpThreshold)) {
                        isDead = true;
                        break;
                    }
                }
            }
        }
        if (isDead) deadTrajectories++;
    }

    // Blocca la mesh se più del 5% delle traiettorie sono collassate.
    // Con la metrica da script (tolerateTruncated) i troncamenti sono
    // fisiologici (singolarità, blow-up di coordinate): si rigetta solo se
    // sono collassate tutte le traiettorie, come nel GeodesicCalculator.
    if (tolerateTruncated ? (deadTrajectories > numU)
                          : (deadTrajectories > (numU * 0.05f))) {
        return false;
    }

    // =====================================================================
    // 1.5 MARCATURA DEI VERTICI "CONGELATI" (tail di truncate del calculator)
    // =====================================================================
    // Il GeodesicCalculator congela una traiettoria sull'ultimo punto valido
    // quando incontra un NaN. Quei punti hanno (grid[i][j] == grid[i][j-1])
    // esatto. Vanno esclusi dal rendering per non generare quad degeneri.
    //
    // Inoltre il calculator usa QVector4D(0,0,0,0) ESATTO come sentinella per un
    // punto "nato morto" (traiettoria che crasha al primo step, prima di avere un
    // ultimo-punto-buono a cui ripiegare: vedi scanDirection in
    // geodesiccalculator.cpp). Su alcune GPU (Mali) una singola traiettoria può
    // crashare così mentre su altre (Adreno) no: il vertice resta a (0,0,0,0),
    // NON è uguale al suo vicino (quindi sfugge al test di "congelato" sopra) e
    // viene triangolato verso i vicini sani, generando la scheggia che attraversa
    // la superficie (confermato: triangolo grid[0][0]=(0,0,0) -> grid[1][0],
    // grid[0][1], lato 2.17 vs mediana 0.04). Un vertice geodetico legittimo non
    // cade mai esattamente su tutte e 4 le coordinate nulle, quindi possiamo
    // trattare (0,0,0,0) esatto come la sentinella che è ed escluderlo dai quad.
    std::vector<bool> isFrozen((numU + 1) * (numV + 1), false);
    std::vector<bool> isDeadOrigin((numU + 1) * (numV + 1), false);
    for (int i = 0; i <= numU; ++i) {
        for (int j = 0; j <= numV; ++j) {
            const int idx = i * (numV + 1) + j;
            if (grid[i][j] == QVector4D(0.0f, 0.0f, 0.0f, 0.0f)) {
                isDeadOrigin[idx] = true;        // sentinella punto-morto: SEMPRE da escludere
                continue;
            }
            if (j >= 1 && (grid[i][j] - grid[i][j-1]).lengthSquared() < 1e-12f) {
                isFrozen[idx] = true;            // coda congelata di un troncamento
            }
        }
    }

#if GEO_DIAG_TRIANGLE
    // Quante sentinelle punto-morto sono state intercettate. Il fenomeno è
    // INTERMITTENTE (la stessa traiettoria su Mali a volte crasha a volte no, anche
    // a parità di binario): se questo numero oscilla tra 0 e >0 tra un caricamento
    // e l'altro è la conferma del non-determinismo a monte — e che la rete a valle
    // scatta quando serve. A regime (assenza di artefatto) il triangolo NON dipende
    // più da questo valore: con sentinella i quad sono esclusi, senza non ci sono.
    {
        int deadCount = 0;
        for (bool b : isDeadOrigin) if (b) deadCount++;
        qWarning("[GEO_TRI] dead-origin sentinels intercepted=%d", deadCount);
    }
#endif

    // =====================================================================
    // 2. COSTRUZIONE GEOMETRIA
    // =====================================================================
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Il chiamante ha passato un dominio utilizzabile? (NaN = non passato, e un
    // intervallo degenere non darebbe una scala sensata.)
    const bool domainUV = !std::isnan(uMin) && !std::isnan(uMax)
                       && !std::isnan(vMin) && !std::isnan(vMax)
                       && (uMax - uMin) != 0.0f && (vMax - vMin) != 0.0f;

    // Primo calcolo dopo un load/reset: e' questo intervallo a definire la
    // scala della texture, e da qui in poi resta fermo. Cosi' il preset si apre
    // con l'aspetto con cui e' stato salvato (texCoord 0..1 esatte, come prima
    // di questa modifica) e sono le MODIFICHE SUCCESSIVE dell'intervallo a
    // tagliare la texture invece di comprimerla.
    if (domainUV && !m_uvRefValid) {
        m_uvRefUMin  = uMin;
        m_uvRefUSpan = uMax - uMin;
        m_uvRefVMin  = vMin;
        m_uvRefVSpan = vMax - vMin;
        m_uvRefValid = true;
    }

    // Costruzione Vertici e Normali
    for (int i = 0; i <= numU; ++i) {
        for (int j = 0; j <= numV; ++j) {
            Vertex v;
            v.position = grid[i][j];

            auto p = [&](int x, int y) { return grid[x][y].toVector3D(); };
            QVector3D du, dv;

            if (i > 0 && i < numU) {
                du = grid[i+1][j].toVector3D() - grid[i-1][j].toVector3D();
            } else {
                if ((grid[numU][j] - grid[0][j]).lengthSquared() < 1e-5f) {
                    du = grid[1][j].toVector3D() - grid[numU-1][j].toVector3D();
                } else {
                    if (i == 0) du = grid[1][j].toVector3D() - grid[0][j].toVector3D();
                    else        du = grid[numU][j].toVector3D() - grid[numU-1][j].toVector3D();
                }
            }

            if (j > 0 && j < numV) {
                dv = grid[i][j+1].toVector3D() - grid[i][j-1].toVector3D();
            } else {
                if ((grid[i][numV] - grid[i][0]).lengthSquared() < 1e-5f) {
                    dv = grid[i][1].toVector3D() - grid[i][numV-1].toVector3D();
                } else {
                    if (j == 0) dv = grid[i][1].toVector3D() - grid[i][0].toVector3D();
                    else        dv = grid[i][numV].toVector3D() - grid[i][numV-1].toVector3D();
                }
            }

            // Normalizza i vettori in modo sicuro prima del prodotto vettoriale
            QVector3D safeDu = du.normalized();
            QVector3D safeDv = dv.normalized();

            // Calcola la normale (la sua lunghezza ora dipenderà solo dall'angolo tra du e dv)
            QVector3D normal = QVector3D::crossProduct(safeDu, safeDv);

            // Threshold per vertici degeneri / singolarità matematiche
            if (normal.lengthSquared() < 1e-5f) {
                if (j > 0) {
                    normal = vertices.back().normal.toVector3D();
                } else {
                    normal = QVector3D(0.0f, 0.0f, 1.0f);
                }
            } else {
                normal.normalize();
            }

            if (j > 0) {
                QVector3D prevNormal = vertices.back().normal.toVector3D();
                if (QVector3D::dotProduct(normal, prevNormal) < 0.0f) {
                    normal = -normal;
                }
            }

            v.normal = QVector4D(normal, 0.0f);

            // COORDINATE TEXTURE ANCORATE AL DOMINIO, non agli indici di
            // griglia. Con (i/numU, j/numV) la texture si ri-STIRA ogni volta
            // che cambia l'intervallo: numU/numV vengono dallo slider Steps e
            // restano gli stessi, cosi' la stessa immagine finiva spalmata su
            // un arco piu' corto, a frequenza spaziale piu' alta. Su una
            // texture molto dettagliata la compressione porta il pattern sotto
            // il passo di campionamento dei pixel e la superficie collassa
            // verso il nero (aliasing) -- il "dimezzo l'intervallo e cambia
            // l'aspetto della texture".
            //
            // Ancorandole al dominio, l'unita' di texture corrisponde a
            // un'unita' del parametro: restringendo l'intervallo la texture
            // resta alla stessa scala e si vede la porzione corrispondente,
            // cioe' viene TAGLIATA invece che compressa.
            //
            // NB per il flusso geodetico: v non e' una coordinata della
            // superficie ma il parametro affine lungo la geodetica, e vMin/vMax
            // sono la lunghezza d'arco percorsa. Ancorare li' e' proprio cio'
            // che rende la texture solidale alla geometria mentre l'arco si
            // accorcia.
            //
            // Senza limiti validi (chiamante che non li passa) si ricade sulle
            // UV normalizzate storiche: nessun preset cambia aspetto.
            if (domainUV && m_uvRefValid) {
                // Frazione del dominio di RIFERIMENTO, non dell'intervallo
                // corrente: la texture resta ancorata alla geometria e
                // restringendo l'intervallo se ne vede la porzione
                // corrispondente. La convenzione "in_uv sta in [0,1]" e'
                // preservata sul dominio di riferimento -- rotazione, zoom e
                // pan della trasformazione 2D continuano a girare attorno a
                // 0.5 come sempre, e il cutout (che ricostruisce u,v con
                // mix(u_min,u_max,texCoord)) resta coerente.
                v.texCoord = QVector2D((uMin + (float)i / numU * (uMax - uMin) - m_uvRefUMin) / m_uvRefUSpan,
                                       (vMin + (float)j / numV * (vMax - vMin) - m_uvRefVMin) / m_uvRefVSpan);
            } else {
                v.texCoord = QVector2D((float)i / numU, (float)j / numV);
            }
            vertices.push_back(v);
        }
    }

    // Costruzione Indici (Triangoli). drawnQuad: i quad disegnati, per il bordo.
    std::vector<char> drawnQuad(size_t(numU) * numV, 0);
    for (int i = 0; i < numU; ++i) {
        for (int j = 0; j < numV; ++j) {
            int p0 = i * (numV + 1) + j;
            int p1 = (i + 1) * (numV + 1) + j;
            int p2 = i * (numV + 1) + (j + 1);
            int p3 = (i + 1) * (numV + 1) + (j + 1);

            // Sentinella punto-morto (0,0,0,0): basta UN vertice del quad per
            // saltarlo. È un punto inventato (traiettoria crashata al primo step),
            // non un vertice reale: triangolarlo verso i vicini sani produce la
            // scheggia che attraversa la superficie. Soglia >=1 (a differenza dei
            // frozen) perché qui NON c'è il rischio di bucare la mesh su un
            // artefatto di arrotondamento: o il punto è la sentinella esatta, o no.
            if (isDeadOrigin[p0] || isDeadOrigin[p1] || isDeadOrigin[p2] || isDeadOrigin[p3])
                continue;

            // Salta il quad se almeno 2 dei 4 vertici sono "frozen".
            // Tollerare 1 frozen evita di bucare la mesh per artefatti
            // di arrotondamento isolati; saltare a >=2 elimina i quad
            // dentro la coda congelata e sul confine con essa.
            int frozenCount = (isFrozen[p0] ? 1 : 0) +
                              (isFrozen[p1] ? 1 : 0) +
                              (isFrozen[p2] ? 1 : 0) +
                              (isFrozen[p3] ? 1 : 0);
            if (frozenCount >= 2) continue;

            drawnQuad[size_t(i) * numV + j] = 1;
            indices.push_back(p0);
            indices.push_back(p1);
            indices.push_back(p2);

            indices.push_back(p2);
            indices.push_back(p1);
            indices.push_back(p3);
        }
    }

    // RILEVAMENTO TOPOLOGICO DELLA CHIUSURA (Cilindro o Möbius)
    bool isUClosed = true;
    bool isVClosed = true;
    float threshold = 1e-4f; // Tolleranza per l'errore del calcolo geodetico numerico

    // Controllo Chiusura su U
    for (int j = 0; j <= numV; ++j) {
        float distNormal = (grid[0][j] - grid[numU][j]).lengthSquared();
        float distTwisted = (grid[0][j] - grid[numU][numV - j]).lengthSquared();

        if (distNormal > threshold && distTwisted > threshold) {
            isUClosed = false;
            break;
        }
    }

    // Controllo Chiusura su V
    for (int i = 0; i <= numU; ++i) {
        float distNormal = (grid[i][0] - grid[i][numV]).lengthSquared();
        float distTwisted = (grid[i][0] - grid[numU - i][numV]).lengthSquared();

        if (distNormal > threshold && distTwisted > threshold) {
            isVClosed = false;
            break;
        }
    }

#if GEO_DIAG_TRIANGLE
    // --- Diagnostica triangolo-artefatto sulla MESH FINALE --------------------
    // Esamina i triangoli realmente inviati alla GPU. Un artefatto = un triangolo
    // con un lato enormemente più lungo della mediana di tutti i lati (una
    // scheggia che attraversa la superficie). Logghiamo i peggiori con indici e
    // posizioni 3D dei 3 vertici, così si risale al vertice colpevole e al perché
    // è finito nella mesh. Su Android → logcat (qWarning, priorità W).
    {
        auto vpos = [&](unsigned int idx) {
            return vertices[idx].position.toVector3D();
        };
        QVector<float> edges;
        edges.reserve(indices.size());
        auto triLongestEdge = [&](unsigned int a, unsigned int b, unsigned int c) {
            const QVector3D pa = vpos(a), pb = vpos(b), pc = vpos(c);
            return std::max({ (pa-pb).length(), (pb-pc).length(), (pc-pa).length() });
        };
        for (size_t k = 0; k + 2 < indices.size(); k += 3)
            edges.push_back(triLongestEdge(indices[k], indices[k+1], indices[k+2]));

        if (!edges.isEmpty()) {
            QVector<float> s = edges; std::sort(s.begin(), s.end());
            const float median = s[s.size()/2];
            const float maxEdge = s.last();
            const float thr = std::max(median * 20.0f, 1e-4f);
            qWarning("[GEO_TRI] tris=%lld median edge=%.4f  maxEdge=%.4f  thr=%.4f",
                     static_cast<long long>(edges.size()), median, maxEdge, thr);
            int reported = 0;
            for (size_t k = 0; k + 2 < indices.size() && reported < 12; k += 3) {
                const unsigned int a=indices[k], b=indices[k+1], c=indices[k+2];
                if (triLongestEdge(a,b,c) > thr) {
                    const QVector3D pa=vpos(a), pb=vpos(b), pc=vpos(c);
                    qWarning("[GEO_TRI] BIG tri longest=%.3f  idx(%u,%u,%u) -> (%d,%d)(%d,%d)(%d,%d)  A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f) C=(%.3f,%.3f,%.3f)",
                             triLongestEdge(a,b,c), a,b,c,
                             a/(numV+1), a%(numV+1), b/(numV+1), b%(numV+1), c/(numV+1), c%(numV+1),
                             pa.x(),pa.y(),pa.z(), pb.x(),pb.y(),pb.z(), pc.x(),pc.y(),pc.z());
                    reported++;
                }
            }
            if (reported == 0)
                qWarning("[GEO_TRI] no oversized triangle (all edges <= %.4f)", thr);
        }
    }
#endif

    // La mesh custom sostituisce un eventuale tubo, come equazioni e script.
    // Il load di un record geodetico dopo un tubo non passava di li': le sue
    // equazioni (x = U, ...) non compilano come parametriche, quindi
    // setParametricEquations falliva e RIMETTEVA il tubo. Rimasto acceso,
    // il motore leggeva la curva al posto delle equazioni (costanti in uso)
    // e lo shader disegnava la curva al posto della superficie, che spariva.
    const bool wasTube = engine->isTubeModeActive();
    engine->setTubeMode(false);

    // Lati della mesh disegnata per il bordo (lo shader non sa ricalcolarli).
    setBorderEdges(grid, drawnQuad);

    // 3. Invia i dati al SurfaceEngine
    engine->setCustomMesh(vertices, indices, isUClosed, isVClosed);

    buildWireframeGeometry();

    meshNeedsUpdate = true;

    if (!wasCustom || wasTube) {
        rebuildShader();
    }

    // LE PARTI SONO CAMBIATE: la mesh custom ne ha una sola. Il segnale nasce in
    // updateSurfaceData, da cui la mesh custom non passa: senza, dopo un
    // multi-mesh l'ambito restava "Mesh" sul flusso geodetico e l'ambito del
    // preset restava in sospeso. Solo al passaggio, non a ogni frame
    // dell'animazione geodetica.
    if (!wasCustom) emit meshPartsChanged();

    update();
    return true; // Costruzione completata con successo!
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void GLWidget::updateSurfaceData()
{
    // 1. Calcola i nuovi vertici sulla CPU usando il tuo Engine
    engine->computeMesh();

    if (m_isFlatView) {
        meshNeedsUpdate = false;
        return;
    }

    buildWireframeGeometry();

    // 2. Diciamo al Render Pass che i dati sono pronti per essere spediti alla GPU
    meshNeedsUpdate = true;

    // Le parti possono essere cambiate di numero o essere state rigenerate:
    // avvisa la UI (selettore di mesh + aspetto per-mesh in sospeso).
    emit meshPartsChanged();

    // 3. Forza il ridisegno
    update();
}

void GLWidget::resetVisuals()
{
    engine->clear();
    m_lightingMode4D = 0;

    m_wireframeIndexCount = 0;

    meshNeedsUpdate = true; // Diciamo a RHI di svuotare i buffer al prossimo render()

    if (m_animTimer->isActive()) {
        m_animTimer->stop();
    }

    QImage defaultImg(":/background.png");
    if (!defaultImg.isNull()) {

        m_useBackgroundTexture = false; // La manteniamo disattivata

        // Se lo sfondo precedente usava uno Script, forziamo il ripristino
        if (m_bgIsScript) {
            rebuildBackgroundShader(true, "");
        }
        m_bgIsScript = false;

        resetTextureFraming();
    }
}

void GLWidget::resetTextureFraming()
{
    // Azzera i parametri dinamici dello sfondo
    m_bgZoom = 1.0f;
    m_bgPan = QVector2D(0.0f, 0.0f);
    m_bgRotation = 0.0f;

    // Azzera i parametri della superficie: sia il buffer di lavoro sia la
    // trasformazione GLOBALE, o quest'ultima sopravviverebbe allo
    // spegnimento della texture e tornerebbe applicata alla successiva.
    m_flatZoom = 1.0f;
    m_flatPan = QVector2D(0.0f, 0.0f);
    m_flatRotation = 0.0f;
    m_globalTexZoom = 1.0f;
    m_globalTexPan = QVector2D(0.0f, 0.0f);
    m_globalTexRotation = 0.0f;

    update();
}

void GLWidget::setProjectionMode(int mode) {
    projectionMode = mode;           // Serve ancora alla CPU per projectPoint4Dto3D
    m_uboData.projMode = mode;       // Sincronizza con la GPU
    meshNeedsUpdate = true;
    update();
}

// ==========================================================
// ASPETTO PER-MESH
// ==========================================================
// m_activeMeshPart e' l'indice scelto con lo spinbox del dock renderer:
// -1 (voce "All") = i controlli agiscono sullo stato GLOBALE, come da sempre;
// >= 0 = agiscono solo su quella parte, che smette di ereditare.
// Le superfici a mesh singola non ne risentono: lo spinbox resta su "All" e
// nessuna parte dichiara un aspetto proprio, quindi il percorso e' invariato.
void GLWidget::setActiveMeshPart(int index) {
    const int n = engine ? engine->getMeshPartCount() : 0;
    const int next = (index >= 0 && index < n) ? index : -1;
    if (next == m_activeMeshPart) { update(); return; }

    // CAMBIO DI MESH MENTRE SI EDITA IN VISTA 2D: zoom/pan/rotazione correnti
    // appartengono ancora alla parte che si sta lasciando. Vanno fissati PRIMA
    // di spostare l'indice, o resterebbero nei membri globali e verrebbero
    // riscritti sulla parte NUOVA (il ridimensionamento della mesh 1 si
    // ritrovava sulla mesh 2, e da li' su tutte).
    if (m_isFlatView) commitFlatTransformToActivePart();

    m_activeMeshPart = next;

    // ...e la vista 2D deve ora mostrare l'inquadratura della parte entrante.
    if (m_isFlatView) loadFlatTransformFromActivePart();

    update();
}

// Applica una modifica di aspetto: alla parte attiva se ce n'e' una, altrimenti
// allo stato globale. Ritorna true se ha scritto su una parte, cosi' i setter
// sanno se devono anche aggiornare il proprio membro globale.
bool GLWidget::applyToActiveMeshPart(const std::function<void(MeshPart&)> &fn) {
    // Bypass: il chiamante sta eseguendo un reset automatico (non un comando
    // dell'utente), quindi deve finire sullo stato globale anche se una mesh e'
    // selezionata. Senza questo, entrando in wireframe il reset di alpha/luce
    // veniva scritto sulla parte attiva, che smetteva di ereditare.
    if (m_meshAppearanceBypass) return false;
    if (m_activeMeshPart < 0 || !engine) return false;
    MeshPart *p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return false;
    fn(*p);
    // L'aspetto va ricopiato sulle parti DICHIARATE, o la prossima
    // rigenerazione della griglia lo riporterebbe ai valori dello script.
    engine->syncPartAppearance();
    update();
    return true;
}

// SEPARAZIONE DISPLAY / COMANDO (il punto che rende stabile il wireframe
// per-mesh). setGlobalRenderMode e' la via da cui passa updateRenderState, che gira a
// ogni cambio tab, cambio proiezione, load e a ogni toggled dei radio: qui NON
// si scrive mai su una parte, si scrive SOLO lo stato globale. I radio restano
// un DISPLAY della mesh selezionata; la sorgente di una modalita' per-parte e'
// soltanto setActiveMeshRenderMode, chiamata dal gestore dei radio quando e'
// l'utente a muoverli.
//
// Il tentativo precedente falliva proprio qui: facendo scrivere questa funzione
// sulla parte attiva, ogni updateRenderState riapplicava il valore a un
// destinatario che dipendeva dallo stato del momento, e il wireframe si
// propagava a tutte le parti che ereditano.
void GLWidget::setGlobalRenderMode(int mode) {
    this->renderMode = mode;
    update();
}

void GLWidget::setMeshAppearanceUniform(bool on) {
    if (m_meshAppearanceUniform == on) return;
    m_meshAppearanceUniform = on;

    // L'engine deve conoscere l'ambito: il DOMINIO, a differenza di colore e
    // texture, non e' un uniform che il render puo' sostituire al volo -- genera
    // i VERTICI sulla CPU. resolveMeshParts legge questo flag per decidere se
    // applicare il dominio di "All" o quello delle singole parti, e la griglia
    // va quindi rigenerata a ogni passaggio fra i due ambiti.
    // La rigenerazione si paga solo quando serve davvero: updateSurfaceData e'
    // chiamata piu' sotto, dopo che il flag e' stato propagato.
    if (engine) engine->setMeshScopeAll(on);
    // Le densita' wireframe per-parte sono INDICI, non uniform: sospenderle
    // richiede di ricostruire la geometria delle linee.
    buildWireframeGeometry();
    // Le texture per-mesh vivono nel CODICE del fragment shader: in "All" il
    // dispatcher non deve esistere affatto, in "Mesh" si', quindi il passaggio
    // fra i due ambiti e' un cambio di sorgente e va ricompilato.
    rebuildShader();

    // GRIGLIA RIGENERATA: se un dominio di "All" e' in gioco -- o lo era e si
    // sta tornando a "Mesh" -- i vertici delle parti cambiano, e nessun altro
    // lo farebbe (buildWireframeGeometry e rebuildShader non toccano la mesh).
    // Si rigenera solo quando c'e' davvero un dominio di All da applicare o da
    // sospendere: senza, il cambio di ambito resta a costo zero com'era.
    if (engine && engine->hasAllDomain() && engine->getMeshPartCount() > 1)
        updateSurfaceData();

    update();
}

// DOMINIO DELL'AMBITO "ALL". Gemello di setActiveMeshDomain, ma scrive nel
// dominio di All invece che nella parte selezionata: da qui passa il taglio
// fatto coi campi u/v quando l'ambito e' "All", e vale per tutte le mesh
// insieme senza cancellare i tagli delle singole (vedi SurfaceEngine::setAllDomain).
bool GLWidget::setAllMeshDomain(float uMin, float uMax, float vMin, float vMax) {
    if (!engine) return false;
    if (!(uMin < uMax) || !(vMin < vMax)) return false;
    engine->setAllDomain(uMin, uMax, vMin, vMax);
    updateSurfaceData();
    return true;
}

// Dominio da MOSTRARE nei campi in ambito "All": quello di All se c'e', altrimenti
// quello della prima parte -- cioe' cio' che la figura sta disegnando adesso.
// false se non c'e' nessuna parte (scena vuota): i campi restano vuoti.
bool GLWidget::allMeshDomain(float &uMin, float &uMax, float &vMin, float &vMax) const {
    if (!engine) return false;
    if (engine->hasAllDomain()) {
        engine->allDomain(uMin, uMax, vMin, vMax);
        return true;
    }
    const auto &parts = engine->getMeshParts();
    if (parts.empty()) return false;
    uMin = parts[0].uMin; uMax = parts[0].uMax;
    vMin = parts[0].vMin; vMax = parts[0].vMax;
    return true;
}

// TEXTURE PROCEDURALE DELLA PARTE ATTIVA. Come setActiveMeshRenderMode, e' la
// via di COMANDO: ci passa solo l'utente che preme Run con una mesh selezionata.
// In "All" (nessuna parte attiva) ricade sul comportamento di sempre, cioe'
// texture globale della superficie: lo decide il chiamante, qui torniamo false.
// rebuildShader e' necessario perche' il codice della texture vive DENTRO il
// fragment shader (getCustomColor_<k>), non in una risorsa: cambiare lo script
// di una mesh significa ricompilare, esattamente come per la texture globale.
bool GLWidget::setActiveMeshTexture(const QString &code, bool enabled) {
    const bool onPart = applyToActiveMeshPart([&](MeshPart &p){
        p.textureCode = code;
        p.textureEnabled = enabled;
        p.hasCustomTexture = true;
        // I colori correnti diventano PROPRI della parte: altrimenti restano
        // condivisi con le altre mesh e la texture applicata dopo riscrive i
        // colori di quelle di prima.
        // Solo se la parte non ne ha GIA' di propri: chi la configura puo'
        // averli appena scritti con setActiveMeshTexColors, e quelli devono
        // vincere sui due slot globali (che appartengono alla superficie).
        if (!p.hasCustomTexColors()) {
            p.texCol1R = texRed1; p.texCol1G = texGreen1; p.texCol1B = texBlue1;
            p.texCol2R = texRed2; p.texCol2G = texGreen2; p.texCol2B = texBlue2;
        }
    });
    if (onPart) rebuildShader();
    return onPart;
}

// CODICE della texture di una fascia scelta per INDICE, non la parte attiva: lo
// usa "Sync Focused Texture", che aggiorna TUTTE le fasce del record rimaste
// indietro rispetto alla loro voce di libreria. Cambia il solo codice (e
// l'orologio, che segue il codice): acceso/spento, colori, trasformazione 2D e
// ancora restano quelli della parte, come nel Sync della texture globale.
// Niente rebuildShader: il chiamante ne fa uno solo dopo l'ultima fascia.
bool GLWidget::setMeshPartTextureCode(int index, const QString &code, bool animating) {
    if (!engine) return false;
    MeshPart *p = engine->mutableMeshPart(index);
    if (!p || !p->hasCustomTexture) return false;
    p->textureCode = code;
    p->texAnimating = animating;
    engine->syncPartAppearance();   // o la prossima rigenerazione lo riporterebbe indietro
    if (animating) ensureTextureClockRunning();
    return true;
}

// OROLOGIO DELLA TEXTURE DELLA SOLA PARTE ATTIVA (Stop/Run per-mesh).
// Il tempo NON si azzera: fermando e riavviando, l'animazione riprende da dove
// era, come fa lo Stop globale.
// Nessun rebuildShader: si muove solo un flag letto dal tick.
// Il flag da solo NON basta: se m_animTimer e' fermo (dopo un master Stop, che
// spegne tutti i clock) nessun tick avanza mp.timeTex e la fascia resta
// immobile pur risultando "in moto". Lo stesso riavvio che fanno i tre setter
// globali -- setSurface/Background/SurfaceTextureAnimating -- vale quindi anche
// per le parti: e' l'unico modo perche' il Run per-mesh del dock Script e il
// riclic sulla texture della fascia nella Library rimettano in moto la sola
// mesh selezionata con la scena per il resto ferma.
bool GLWidget::setActiveMeshTextureAnimating(bool animating) {
    const bool onPart = applyToActiveMeshPart([&](MeshPart &p){
        p.texAnimating = animating;
    });
    if (onPart && animating) ensureTextureClockRunning();
    if (onPart) update();
    return onPart;
}

// Vedi la nota nell'header: gli orologi per-mesh non passano dai setter globali,
// quindi il timer dei tick va riacceso esplicitamente.
void GLWidget::ensureTextureClockRunning() {
    if (m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();
    }
}

// AZZERA l'orologio della SOLA parte attiva. E' il gemello per-mesh di
// resetTextureTime: il riclic sulla texture di UNA fascia la fa ripartire da
// capo senza toccare il clock globale ne' quelli delle altre fasce.
bool GLWidget::resetActiveMeshTextureTime() {
    const bool onPart = applyToActiveMeshPart([](MeshPart &p){
        p.timeTex = 0.0f;
    });
    if (onPart) update();
    return onPart;
}

// OROLOGIO DI TUTTE LE PARTI (ambito "All", e master Start/Stop).
// ADOZIONE ESPLICITA, non ereditarieta': lo stato si SCRIVE su ogni parte invece
// di lasciare che le parti copino il clock globale. E' la differenza che fa
// funzionare lo stop per-mesh -- una parte che ereditasse il tempo globale
// erediterebbe anche il suo freeze, e dopo uno Stop in "All" non ripartirebbe
// piu' da sola (e' l'errore per cui il primo tentativo fu revertato).
void GLWidget::setAllMeshTexturesAnimating(bool animating) {
    if (!engine) return;
    for (MeshPart &mp : engine->mutableMeshParts()) mp.texAnimating = animating;
    engine->syncPartAppearance();
    if (animating) ensureTextureClockRunning();
    update();
}

// Colori u_col1/u_col2 PROPRI della parte attiva. Non tocca i due slot globali
// (texRed1..texBlue2), che appartengono alla texture di SUPERFICIE.
// Nessun rebuildShader: i colori vivono nel blocco UBO, non nel codice.
bool GLWidget::setActiveMeshTexColors(const QColor &c1, const QColor &c2) {
    return applyToActiveMeshPart([&](MeshPart &p){
        p.texCol1R = c1.redF();   p.texCol1G = c1.greenF();   p.texCol1B = c1.blueF();
        p.texCol2R = c2.redF();   p.texCol2G = c2.greenF();   p.texCol2B = c2.blueF();
    });
}

// SPEGNE la texture propria della parte attiva CONSERVANDO lo script:
// hasCustomTexture resta true e textureCode intatto, quindi riaccendendo il
// checkbox si ritrova la propria texture invece di doverla ricaricare.
// NON usare setActiveMeshTexture per spegnere: e' la via di COMANDO e riscrive
// textureCode forzando hasCustomTexture=true, cioe' riafferma la texture proprio
// mentre la si toglie (l'editor continuava a mostrarne lo script).
// Serve al wireframe, che la texture non la disegna
// (il fragment esce prima, vedi la guardia u_renderMode 2/5 in surface.frag):
// senza spegnerla, togliendo il wireframe la texture ricompariva da sola con
// il checkbox che diceva il contrario.
// Ritorna true se ha davvero cambiato qualcosa, cosi' il chiamante sa se
// ricompilare e se aggiornare l'UI.
bool GLWidget::setActiveMeshTextureEnabled(bool on) {
    bool changed = false;
    const bool onPart = applyToActiveMeshPart([&](MeshPart &p){
        if (!p.hasCustomTexture || p.textureEnabled == on) return;
        p.textureEnabled = on;
        // Spegnendo si ferma anche il suo orologio: un clock acceso su una
        // texture che nessuno disegna terrebbe il master su "STOP" a vuoto.
        if (!on) p.texAnimating = false;
        changed = true;
    });
    if (onPart && changed) { rebuildShader(); update(); }
    return onPart && changed;
}

// Codice della texture della parte attiva, per il DISPLAY nell'editor.
// Vuoto se non ne ha una propria o se siamo in "All".
QString GLWidget::activeMeshTextureCode() const {
    if (m_activeMeshPart < 0 || !engine) return QString();
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return QString();
    const MeshPart &p = parts[m_activeMeshPart];
    return p.hasCustomTexture ? p.textureCode : QString();
}

// Codice della texture EFFICACE della parte attiva: quello che la mesh sta
// davvero disegnando. Vuoto se la texture e' SPENTA, anche se lo script esiste
// ancora (lo si conserva apposta: wireframe o checkbox tolto).
// E' cio' che l'EDITOR deve mostrare: activeMeshTextureCode() guarda il solo
// hasCustomTexture, quindi rimetteva in vista lo script di una texture spenta --
// e un Run l'avrebbe riapplicata di soppiatto.
QString GLWidget::activeMeshEffectiveTextureCode() const {
    if (m_activeMeshPart < 0 || !engine) return QString();
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return QString();
    const MeshPart &p = parts[m_activeMeshPart];
    return p.effectiveTextureEnabledMulti() ? p.textureCode : QString();
}

// Orologio della parte attiva: e' cio' che i tasti Run/Stop devono MOSTRARE
// quando l'ambito e' "Mesh", allo stesso modo in cui slider e radio mostrano
// l'aspetto della parte. In "All" (nessuna parte attiva) torna false e decide
// il chiamante, che li' guarda il clock globale.
bool GLWidget::isActiveMeshTextureAnimating() const {
    if (m_activeMeshPart < 0 || !engine) return false;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return false;
    return parts[m_activeMeshPart].texAnimating;
}

// C'e' almeno una parte con la texture in movimento? Serve al master button:
// una texture per-mesh in moto e' attivita' a tutti gli effetti, e il master
// deve poterla fermare e farla ripartire.
bool GLWidget::anyMeshTextureAnimating() const {
    if (!engine) return false;
    for (const MeshPart &mp : engine->getMeshParts()) {
        if (mp.texAnimating && mp.hasCustomTexture && mp.textureEnabled
            && !mp.textureCode.isEmpty())
            return true;
    }
    return false;
}

// Texture EFFICACE della parte attiva: propria E accesa. E' cio' che il render
// disegna davvero (vedi effectiveTextureEnabledMulti), quindi e' la condizione
// giusta ovunque serva sapere se la fascia e' texturizzata: se gli slider colore
// editano u_col1/u_col2 o la tinta, se il tasto 2D ha qualcosa da mostrare.
bool GLWidget::activeMeshTextureActive() const {
    if (m_activeMeshPart < 0 || !engine) return false;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return false;
    return parts[m_activeMeshPart].effectiveTextureEnabledMulti();
}

void GLWidget::setActiveMeshRenderMode(int mode) {
    // Nessuna parte selezionata ("All") o bypass attivo: e' una scelta globale.
    if (!applyToActiveMeshPart([mode](MeshPart &p){
            p.renderMode = mode;
            p.hasCustomRenderMode = true;
        })) {
        this->renderMode = mode;
        update();
    }
    // La densita' wireframe dipende da quali parti sono in wireframe e con che
    // stride: la geometria va ricostruita.
    buildWireframeGeometry();
    update();
}

void GLWidget::clearActiveMeshRenderMode() {
    applyToActiveMeshPart([](MeshPart &p){
        p.hasCustomRenderMode = false;
        p.renderMode = 0;
    });
    buildWireframeGeometry();
    update();
}

// Rende PROPRIA di ogni parte la modalita' che sta ereditando dal globale. Non
// cambia nulla di visibile: a ogni parte si scrive esattamente il valore che
// effectiveRenderMode gia' restituiva per lei.
// A cosa serve: dall'ambito "All" un click sui radio cambia il GLOBALE, e tutte
// le parti senza modalita' propria lo seguono. Il preset "Hopf Tori Mesh Colors"
// ha globale = Phong e solo due mesh con wireframe proprio: mettendo tutto in
// wireframe da "All" le altre ereditavano il 2 e, tornando su "Mesh", la
// superficie era interamente wireframe. I dati per-mesh non erano stati persi
// (le due mesh avevano ancora il loro 2): era la BASE ereditata a essersi
// spostata sotto di loro. Congelandola prima, l'aspetto misto sopravvive.
void GLWidget::pinInheritedRenderModes() {
    if (!engine) return;
    const int n = engine->getMeshPartCount();
    if (n <= 1) return;              // mesh singola: nessun aspetto per-parte in gioco
    for (int k = 0; k < n; ++k) {
        MeshPart *p = engine->mutableMeshPart(k);
        if (!p || p->hasCustomRenderMode) continue;   // chi ha gia' la sua non si tocca
        p->renderMode = renderMode;                   // = quello che stava ereditando
        p->hasCustomRenderMode = true;
    }
    // Come in applyToActiveMeshPart: senza questo la prossima rigenerazione
    // della griglia riporterebbe le parti ai valori dello script.
    engine->syncPartAppearance();
}

// Stesso congelamento per trasparenza e colore. Il commit che ha introdotto
// pinInheritedRenderModes riteneva che questi due non ne soffrissero, perche'
// in "All" l'aspetto proprio e' sospeso: vero per le parti che CE L'HANNO, ma
// quelle senza (alpha/colore negativi = eredita) disegnano il globale anche in
// "Mesh". Caso segnalato: multi-mesh opaca, trasparenza da "All", ritorno su
// "Mesh" -> tutte le mesh trasparenti.
void GLWidget::pinInheritedAlphas() {
    if (!engine) return;
    const int n = engine->getMeshPartCount();
    if (n <= 1) return;
    for (int k = 0; k < n; ++k) {
        MeshPart *p = engine->mutableMeshPart(k);
        if (!p || p->alpha >= 0.0f) continue;
        p->alpha = alpha;
    }
    engine->syncPartAppearance();
}

void GLWidget::pinInheritedColors() {
    if (!engine) return;
    const int n = engine->getMeshPartCount();
    if (n <= 1) return;
    for (int k = 0; k < n; ++k) {
        MeshPart *p = engine->mutableMeshPart(k);
        if (!p || p->hasCustomColor()) continue;
        // red/green/blue, non m_uboData.color: e' la tinta che una parte senza
        // colore proprio disegna anche con la texture globale accesa.
        p->colorR = red;
        p->colorG = green;
        p->colorB = blue;
    }
    engine->syncPartAppearance();
}

int GLWidget::activeMeshEffectiveRenderMode() const {
    if (m_activeMeshPart < 0 || !engine) return renderMode;
    const auto &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return renderMode;
    return parts[m_activeMeshPart].effectiveRenderMode(renderMode);
}

void GLWidget::setActiveMeshWireframeDensity(int uStep, int vStep) {
    // 0 = eredita; altrimenti clamp come i tasti +/- globali.
    const int u = (uStep <= 0) ? 0 : std::clamp(uStep, STEP_MIN, STEP_MAX);
    const int v = (vStep <= 0) ? 0 : std::clamp(vStep, STEP_MIN, STEP_MAX);
    if (!applyToActiveMeshPart([u,v](MeshPart &p){ p.wfStepU = u; p.wfStepV = v; })) {
        // "All": e' la densita' globale, percorso storico.
        setWireframeDensity(u > 0 ? u : wfStepU, v > 0 ? v : wfStepV);
        return;
    }
    buildWireframeGeometry();
    update();
}

void GLWidget::activeMeshWireframeDensity(int &uStep, int &vStep) const {
    uStep = 0; vStep = 0;
    if (m_activeMeshPart < 0 || !engine) return;
    const auto &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return;
    uStep = parts[m_activeMeshPart].wfStepU;
    vStep = parts[m_activeMeshPart].wfStepV;
}

// DOMINIO DELLA PARTE ATTIVA. Vedi la nota estesa in glwidget.h: qui il punto
// da tenere a mente e' che NON si passa da applyToActiveMeshPart, per due
// ragioni distinte.
//  1. Quella funzione chiama syncPartAppearance(), che ricopia l'aspetto nelle
//     parti DICHIARATE. Il dominio non e' in quella lista ed e' voluto (lo
//     script resta l'autorita'), quindi passare di li' non servirebbe a nulla.
//  2. Il dominio genera i VERTICI: cambiarlo richiede computeMesh(), non il
//     solo re-upload dell'UBO. Gli altri setter per-mesh scrivono uniform e si
//     accontentano di update().
bool GLWidget::setActiveMeshDomain(float uMin, float uMax, float vMin, float vMax) {
    if (m_activeMeshPart < 0 || !engine) return false;
    MeshPart *p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return false;

    // Intervallo impossibile: non si applica nulla. Stessa regola di
    // updateULimits/updateVLimits per i limiti globali, che rifiutano min>=max
    // senza toccare il dominio in vigore.
    if (!(uMin < uMax) || !(vMin < vMax)) return false;

    p->uMin = uMin; p->uMax = uMax;
    p->vMin = vMin; p->vMax = vMax;

    // ...E ANCHE NELLA PARTE DICHIARATA, o la riga qui sopra non sopravvive.
    // generateParametricGrid riparte sempre da resolveMeshParts(), che
    // ricostruisce m_meshParts da m_declaredParts: scrivendo nella sola lista
    // generata, updateSurfaceData() qui sotto -- chiamato proprio per rendere
    // visibile la modifica -- la cancellerebbe prima di generare un vertice, e
    // a schermo non cambierebbe nulla.
    // Le due liste restano comunque distinte su tutto il resto: setMeshParts
    // (cioe' il Run dello script) non preserva il dominio, quindi le sezioni
    // //MESH_BEGIN continuano a essere l'autorita' e riscrivono queste
    // modifiche al primo Run.
    engine->setDeclaredPartDomain(m_activeMeshPart, uMin, uMax, vMin, vMax);

    // Rigenerazione della griglia: updateSurfaceData() rifa' i vertici di TUTTE
    // le parti (computeMesh) e riemette meshPartsChanged, che e' cio' che
    // riallinea il selettore e i campi. La chiusura per-parte viene ricalcolata
    // li' dentro: un dominio ridotto puo' aprire una cucitura prima saldata, ed
    // e' corretto che accada -- la parte non e' piu' chiusa.
    updateSurfaceData();
    return true;
}

bool GLWidget::activeMeshDomain(float &uMin, float &uMax, float &vMin, float &vMax) const {
    if (m_activeMeshPart < 0 || !engine) return false;
    const auto &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return false;
    const MeshPart &p = parts[m_activeMeshPart];
    uMin = p.uMin; uMax = p.uMax;
    vMin = p.vMin; vMax = p.vMax;
    return true;
}

void GLWidget::setColor(float r, float g, float b) {
    if (applyToActiveMeshPart([r,g,b](MeshPart &p){ p.colorR=r; p.colorG=g; p.colorB=b; })) return;
    // Il globale e' la BASE delle parti che ereditano: spostandolo da "All" le
    // si congela prima (vedi pinInheritedColors). Sotto bypass no: load e reset
    // automatici devono muovere il globale senza rendere "propri" dei colori.
    if (!m_meshAppearanceBypass && (r != red || g != green || b != blue))
        pinInheritedColors();
    this->red = r;
    this->green = g;
    this->blue = b;
    // Rimuovi l'assegnazione diretta a m_uboData qui, lo fa già il render()
    update();
}

void GLWidget::setAlpha(float a) {
    if (applyToActiveMeshPart([a](MeshPart &p){ p.alpha = a; })) return;
    // Come in setColor (vedi pinInheritedAlphas).
    if (!m_meshAppearanceBypass && a != alpha) pinInheritedAlphas();
    this->alpha = a;
    update();
}

void GLWidget::setSpecularEnabled(bool enabled) {
    m_isSpecularEnabled = enabled;
    update();
}

// INTENSITA' LUMINOSA: GLOBALE anche in ambito "Mesh".
// E' una proprieta' dell'ILLUMINAZIONE della scena, non dell'aspetto di una
// fascia: come la luce speculare, deve valere per tutta la superficie qualunque
// mesh sia selezionata. Colore e trasparenza restano invece per-mesh (li' la
// differenza fra le fasce e' il senso della feature).
// I valori lightIntensity gia' presenti nelle MeshPart non vengono cancellati:
// restano nei preset che li hanno salvati e il render continua a rispettarli;
// semplicemente lo slider non ne scrive di nuovi.
void GLWidget::setLightIntensity(float intensity) {
    this->m_lightIntensity = intensity;
    update();
}

void GLWidget::setFillLight(float v) {
    m_fillLight = v;
    update();
}

void GLWidget::setShellThickness(float v) {
    m_shellThickness = v;
    update();
}

void GLWidget::setBorderRadius(float r) {
    const float next = std::max(0.0f, r);
    const bool wasOn = (m_borderRadius > 0.0f);
    m_borderRadius = next;
    // Il codice del bordo e' nello shader solo con HAS_BORDER: acceso o spento
    // si ricompila, altrimenti basta l'uniform.
    if (wasOn != (next > 0.0f)) {
        rebuildShader();
        emit borderAvailabilityChanged();   // col bordo acceso il radio resta cliccabile
    }
    update();
}

void GLWidget::setBorderRenderMode(int mode) {
    m_borderRenderMode = std::clamp(mode, 0, 2);
    update();
}

void GLWidget::adjustBorderWireStep(bool along, int delta) {
    if (along) m_borderWireAlong = std::clamp(m_borderWireAlong + delta, 1, kBorderWireAlongMax);
    else       m_borderWireAround = std::clamp(m_borderWireAround + delta, 1, kBorderWireAroundMax);
    m_borderUploaded = false;   // le linee si ricostruiscono al prossimo frame
    update();
}

bool GLWidget::globalTextureActive() const
{
    return m_meshAppearanceUniform && engine && engine->getMeshParts().size() > 1;
}

GLWidget::BorderStyle GLWidget::borderStyle() const {
    BorderStyle s;
    s.radius = m_borderRadius;
    s.color = m_borderColor;
    s.mode = m_borderRenderMode;
    s.wireAlong = m_borderWireAlong;
    s.wireAround = m_borderWireAround;
    s.texCode = m_borderTexCode;
    s.texEnabled = m_borderTexEnabled;
    s.texCol1 = m_borderTexCol1;
    s.texCol2 = m_borderTexCol2;
    s.texZoom = m_borderTexZoom;
    s.texPan = m_borderTexPan;
    s.texRotation = m_borderTexRotation;
    s.texLibName = m_borderTexLibName;
    s.texHint = m_borderTexHint;
    s.texHintSeconds = m_borderTexHintSeconds;
    return s;
}

void GLWidget::setBorderStyle(const BorderStyle &s) {
    m_borderColor = s.color;
    m_borderRenderMode = std::clamp(s.mode, 0, 2);
    m_borderWireAlong = std::clamp(s.wireAlong, 1, kBorderWireAlongMax);
    m_borderWireAround = std::clamp(s.wireAround, 1, kBorderWireAroundMax);
    m_borderTexCol1 = s.texCol1;
    m_borderTexCol2 = s.texCol2;
    m_borderTexZoom = s.texZoom;
    m_borderTexPan = s.texPan;
    m_borderTexRotation = s.texRotation;
    m_borderTexLibName = s.texLibName;
    m_borderTexHint = s.texHint.trimmed();
    m_borderTexHintSeconds = s.texHintSeconds;
    m_borderTimeTex = 0.0f;      // una scena nuova parte dall'inizio
    m_borderUploaded = false;
    setBorderTexture(s.texCode, s.texEnabled);
    setBorderRadius(s.radius);   // ricompila solo se il bordo si accende o si spegne
}

void GLWidget::setBorderTexture(const QString &code, bool enabled) {
    m_borderTexEnabled = enabled;
    if (code != m_borderTexCode) {
        m_borderTexCode = code;
        // Il codice e' nel fragment solo col bordo acceso.
        if (m_borderRadius > 0.0f) rebuildShader();
    }
    update();
}

void GLWidget::setBorderTextureAnimating(bool on) {
    m_borderTexAnimating = on;
    if (on) ensureTextureClockRunning();
}

bool GLWidget::borderSamplesImage() const {
    const QString path = imagePathInTextureCode(m_borderTexCode);
    if (!path.isEmpty() && path != m_surfaceImagePath) {
        const auto it = m_partImages.constFind(path);
        // Non ancora caricata (lo fa il prossimo frame) conta come presente.
        if (it == m_partImages.constEnd() || !it->failed) return true;
    }
    return !m_surfaceImagePath.isEmpty();
}

QRhiShaderResourceBindings *GLWidget::borderBindings() const {
    const QString path = imagePathInTextureCode(m_borderTexCode);
    if (!path.isEmpty()) {
        const auto it = m_partImages.constFind(path);
        if (it != m_partImages.constEnd() && it->tex && it->srb) return it->srb;
    }
    return m_bindingsDyn ? m_bindingsDyn : m_bindings;
}

void GLWidget::setHybridMarcher(bool on) {
    m_hybridMarcher = on;
    update();
}

// I tasti +/- della densita' agiscono sulla mesh SELEZIONATA se ce n'e' una,
// altrimenti sullo stato globale (voce "All"): stesso schema di colore/alpha.
// La parte parte dal valore che stava gia' usando (il proprio se dichiarato,
// altrimenti il globale), cosi' il primo click non fa un salto.
// NB: lo scambio U<->V e' storico e va conservato (il tasto "U" muove wfStepV).
bool GLWidget::adjustActiveWireframeStep(bool isU, int delta) {
    MeshPart *p = nullptr;
    if (!m_meshAppearanceBypass && m_activeMeshPart >= 0 && engine)
        p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return false;

    int &partStep = isU ? p->wfStepV : p->wfStepU;
    const int fallback = isU ? wfStepV : wfStepU;
    const int cur = (partStep > 0) ? partStep : fallback;
    partStep = std::clamp(cur + delta, STEP_MIN, STEP_MAX);

    engine->syncPartAppearance();
    buildWireframeGeometry();
    update();
    return true;
}

void GLWidget::increaseWireframeUDensity() {
    if (adjustActiveWireframeStep(true, -1)) return;
    if (wfStepV > STEP_MIN) wfStepV--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeUDensity() {
    if (adjustActiveWireframeStep(true, +1)) return;
    if (wfStepV < STEP_MAX) wfStepV++;
    buildWireframeGeometry();
    update();
}

void GLWidget::increaseWireframeVDensity() {
    if (adjustActiveWireframeStep(false, -1)) return;
    if (wfStepU > STEP_MIN) wfStepU--;
    buildWireframeGeometry();
    update();
}

void GLWidget::decreaseWireframeVDensity() {
    if (adjustActiveWireframeStep(false, +1)) return;
    if (wfStepU < STEP_MAX) wfStepU++;
    buildWireframeGeometry();
    update();
}

void GLWidget::setWireframeDensity(int uStep, int vStep) {
    // Clamp ai limiti dei tasti +/- (STEP_MIN..STEP_MAX): un preset con valori fuori
    // range o corrotto non deve produrre una geometria degenere.
    wfStepU = std::clamp(uStep, STEP_MIN, STEP_MAX);
    wfStepV = std::clamp(vStep, STEP_MIN, STEP_MAX);
    // Ricostruiamo solo se c'è un engine valido: al load la mesh potrebbe non essere
    // ancora pronta, nel qual caso basta aver impostato wfStepU/V (la geometria sarà
    // costruita con questi valori quando la superficie viene caricata). Vedi resetWireframeDensity.
    if (engine) {
        buildWireframeGeometry();
        update();
    }
}

void GLWidget::resetWireframeDensity() {
    if (wfStepU == STEP_DEF && wfStepV == STEP_DEF) return; // già al default
    wfStepU = STEP_DEF;
    wfStepV = STEP_DEF;
    // Ricostruiamo la geometria wireframe solo se c'è un engine valido: chiamato anche
    // su cambio superficie/tab, quando la nuova mesh potrebbe non essere ancora pronta.
    // In quel caso basta aver riportato wfStepU/V al default: la geometria sarà costruita
    // con questi valori quando la superficie viene caricata.
    if (engine) {
        buildWireframeGeometry();
        update();
    }
}

void GLWidget::rebuildShader()
{
    if (m_pipelineOpaque) {
        delete m_pipelineOpaque;
        m_pipelineOpaque = nullptr;
    }

    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }

    meshNeedsUpdate = true;

    // Riarmo del watchdog di performance: rebuildShader() e' l'imbuto di OGNI
    // cambio che altera il peso GPU della scena (nuovo preset/superficie/record,
    // Run script, toggle trasparenza/RM, parametri pesanti come steps). "Un
    // avviso per scena, finche' non cambiano le impostazioni": qui le
    // impostazioni sono cambiate, quindi un futuro rallentamento potra'
    // riavvisare. NON riarmato dallo stop/riavvio dei moti (vedi ramo !animating
    // in render(), da cui il reset del flag e' stato tolto apposta).
    m_perfWarnDismissed = false;
    m_perfWarnLevelMs = 0.0f;
    // Misura FRESCA per la nuova scena: senza questo azzeramento l'EMA della
    // scena PRECEDENTE sopravviveva al cambio di preset e faceva scattare a
    // vuoto la conferma trasparenza (renderingUnderHeavyLoad) al primo tocco
    // dell'alpha su un preset appena caricato. Se la scena nuova e' davvero
    // pesante l'EMA risale in 2-3 frame (peso 0.35 in salita).
    m_avgFrameMs = 16.0f;
    m_slowAccumMs = 0.0f;
    m_slowFrameRun = 0;
    m_hugeFrameRun = 0;

    update();
}

void GLWidget::setRecordingActive(bool on)
{
    m_isRecording = on;
    if (!on) {
        // Ritorno dall'export: il transitorio di ripristino (endHiResCapture,
        // resize dell'FBO alla dimensione a schermo, eventuale ricompilazione)
        // produce alcuni frame lenti che NON sono carico GPU reale. Su desktop
        // sono trascurabili, su mobile bastano a sforare il dwell e a far
        // ricomparire il popup SUBITO DOPO la registrazione (fuori tempo massimo).
        // Riarmiamo lo stato di MISURA e apriamo una finestra di grazia: entro
        // kPerfGraceMs il watchdog scarta ogni misura, cosi' l'INTERO transitorio
        // di ripristino (di durata variabile su mobile: puo' durare piu' frame,
        // non solo il primo) non viene scambiato per un rallentamento reale. NON
        // tocchiamo m_perfWarnDismissed: se avevamo gia' avvisato per questa scena
        // PRIMA di registrare, resta zitto.
        m_avgFrameMs = 16.0f;
        m_slowAccumMs = 0.0f;
        m_perfWarnLevelMs = 0.0f;
        m_slowFrameRun = 0;
        m_hugeFrameRun = 0;
        m_wasAnimating = false;      // -> justResumed=true: salta comunque il 1o frame
        m_perfGraceClock.start();    // apre la finestra di grazia post-export
    }
}


// ==========================================================
// TEXTURES, SCRIPTS & BACKGROUND
// ==========================================================

void GLWidget::loadTextureFromFile(const QString &f) {
    QImage img(f);
    if (!img.isNull()) {
        loadTextureFromImage(img);
        // Immagine VERA dell'utente (da file). Distinta dalla scacchiera che
        // MainWindow::generateTexture() disegna su CPU e carica dalla stessa
        // via: quella e' un default procedurale, non un'immagine, e non deve
        // spegnere il fallback di iChannel0 (vedi m_surfaceImagePath).
        m_surfaceImagePath = f;
    } else {
        // Prima qui non c'era nulla: si usciva in silenzio e a schermo restava
        // la texture PRECEDENTE, che l'utente leggeva come "il preset ha la
        // texture di default". Il percorso e' gia' stato validato da chi
        // chiama (isReadableFile + Smart Path Resolver), quindi arrivare qui
        // significa che il file si apre ma non contiene un'immagine valida:
        // formato non supportato, o file corrotto.
        qWarning() << "GLWidget: immagine non decodificabile:" << f;
        emit textureImageLoadFailed(f);
    }
}

void GLWidget::loadTextureFromImage(const QImage &img) {
    if (img.isNull()) return;

    // Caricamento da QImage: e' la via della scacchiera generata. Chi carica
    // un'immagine dell'utente passa da loadTextureFromFile, che ne scrive il
    // percorso subito dopo. Azzerarlo qui evita che una scacchiera generata DOPO
    // un'immagine continui a spacciarsi per quella.
    m_surfaceImagePath.clear();

    // 1. RHI richiede formati precisi (RGBA8888)
    m_pendingSurfaceImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));

    // 2. Alziamo la bandierina per il Render Loop
    m_surfaceTextureNeedsUpload = true;

    rebuildShader();
    update();
}

void GLWidget::setGlobalTextureEnabled(bool enable) {
    if (m_textureEnabled != enable) {
        m_textureEnabled = enable;

        if (m_engineMode == ModeImplicit && m_pipelineImplicit) {
            m_pipelineImplicit->destroy();
            delete m_pipelineImplicit;
            m_pipelineImplicit = nullptr;
        }

        update();
    }
}

void GLWidget::setGlobalTextureColors(const QColor& c1, const QColor& c2)
{
    texRed1 = c1.redF();
    texGreen1 = c1.greenF();
    texBlue1 = c1.blueF();

    texRed2 = c2.redF();
    texGreen2 = c2.greenF();
    texBlue2 = c2.blueF();

    update();
}

void GLWidget::clearTexture() {
    // Svuota l'immagine in attesa
    m_pendingSurfaceImage = QImage();
    m_surfaceTextureNeedsUpload = false;
    // Non c'e' piu' un'immagine dell'utente: gli script che campionano
    // iChannel0 devono tornare alla scacchiera procedurale.
    m_surfaceImagePath.clear();

    // Distruzione effettiva dell'oggetto RHI rinviata al render loop: svuotare solo
    // m_pendingSurfaceImage NON bastava (m_surfaceTexture restava residente sulla GPU
    // e riaffiorava cambiando superficie / riaccendendo la texture). Il flag forza lo
    // scarico al prossimo frame, nel contesto RHI corretto.
    m_surfaceTextureNeedsClear = true;

    // Al prossimo frame tornerà tutto nero/colore base se disattiviamo m_textureEnabled
    setGlobalTextureEnabled(false);
    update();
}

void GLWidget::setScriptCheck(bool enabled) {
    engine->setScriptMode(enabled);
    if (enabled) engine->setTubeMode(false);   // script e tubo si escludono
    meshNeedsUpdate = true;
}

bool GLWidget::loadCustomShader(const QString &customCode)
{
    if (!validateAndApplyParametricShader(customCode)) {
        qWarning() << "loadCustomShader: codice non valido, non applicato:"
                   << m_lastCompilationError;
        return false;
    }
    return true;
}

void GLWidget::setCameraFov(float deg) {
    // Clamp: sotto ~20° l'immagine è un teleobiettivo inutilizzabile negli
    // interni, sopra ~110° la prospettiva rettilinea degenera ai bordi.
    m_cameraFov = qBound(20.0f, deg, 110.0f);
    update();
}

void GLWidget::setBackgroundColor(const QColor &c) {
    m_bgColor = QVector3D(c.redF(), c.greenF(), c.blueF());
    update();
}


void GLWidget::setBackgroundTexture(const QString &path) {
    setBackgroundImage(path);
    m_bgIsScript = false;
    rebuildBackgroundShader(true, "");

    update();
}

void GLWidget::setBackgroundImage(const QString &path) {
    QImage img;

    // 1. Tenta il percorso letterale esatto fornito (es. "background.png")
    img.load(path);

    // 2. Tenta la risorsa interna Qt (utile se in futuro lo metti nel .qrc)
    if (img.isNull() && !path.startsWith(":/")) {
        img.load(":/" + path);
    }

    // 3. Tenta la cartella in cui si trova l'eseguibile (.exe o AppBundle)
    if (img.isNull()) {
        img.load(QCoreApplication::applicationDirPath() + "/" + path);
    }

    // 4. SALVAVITA: Se l'immagine è persa, generiamo un gradiente visibile!
    if (img.isNull()) {
        qWarning() << "Warning: Failed to load background image:" << path << "- Generating fallback.";
        img = QImage(512, 512, QImage::Format_RGBA8888);
        QPainter p(&img);
        QLinearGradient grad(0, 0, 0, 512);
        grad.setColorAt(0, QColor("#1e1e1e")); // Grigio scuro in alto
        grad.setColorAt(1, QColor("#4a4a4a")); // Grigio più chiaro in basso
        p.fillRect(0, 0, 512, 512, grad);
        p.end();
    }

    // A questo punto 'img' ha SEMPRE dei pixel validi da mostrare a Vulkan
    m_pendingBackgroundImage = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Orientations(Qt::Vertical));

    m_backgroundTextureNeedsUpload = true;
    m_bgImagePath = path;

    update();
}

void GLWidget::setBackgroundTextureEnabled(bool enabled) {
    m_useBackgroundTexture = enabled;

    if (enabled) {
        // 1. Se dall'avvio del programma non abbiamo NESSUNA immagine in memoria,
        // carichiamo quella di default (nella working directory).
        if (m_pendingBackgroundImage.isNull() && !m_backgroundTexture) {
            setBackgroundTexture("background.png");
        }
        // 2. Altrimenti, l'immagine c'è già (es. l'ultima usata). Dobbiamo solo
        // assicurarci che la pipeline Vulkan sia pronta per disegnarla.
        else {
            if (!m_bgPipeline) {
                // Ricostruisce lo shader in base allo stato in cui l'avevamo lasciato
                rebuildBackgroundShader(!m_bgIsScript, m_bgScriptCode);
            }
            update();
        }
    } else {
        // Se stiamo spegnendo lo sfondo, basta aggiornare lo schermo
        update();
    }
}

void GLWidget::loadBackgroundScript(const QString &scriptCode) {
    m_bgScriptCode = scriptCode; // Memorizza il codice
    m_bgIsScript = true;         // Alziamo la bandierina dello script

    m_useBackgroundTexture = true;

    // Ricostruiamo la pipeline Vulkan
    rebuildBackgroundShader(false, m_bgScriptCode);

    // Diciamo allo schermo di aggiornarsi
    update();
}

void GLWidget::setTextureCode(const QString& code) {
    if (m_textureCode == code) return;

    QString oldTex = m_textureCode;
    m_textureCode = code;

    QString fsSource = createImplicitFragmentShader();
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        // Stessa regola di setDisplacementCode: TOGLIERE la texture non puo'
        // essere la causa dell'errore, e' il resto dello shader RM a non
        // compilare in questo istante (load di un record parametrico, equazione
        // implicita gia' svuotata). Col rollback il motore si teneva la texture
        // del record RM aperto prima mentre il campo diceva vuoto (trovato dal
        // test degli scenari). Il vuoto si registra sempre.
        if (code.trimmed().isEmpty()) {
            if (m_engineMode != ModeImplicit && m_pipelineImplicit) {
                m_pipelineImplicit->destroy();
                delete m_pipelineImplicit;
                m_pipelineImplicit = nullptr;
            }
            return;
        }
        m_textureCode = oldTex;
        return;
    }

    if (m_pipelineImplicit) {
        m_pipelineImplicit->destroy();
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

void GLWidget::setDisplacementCode(const QString& code) {
    if (m_displacementCode == code) return;

    QString oldDisp = m_displacementCode;
    m_displacementCode = code;

    QString fsSource = createImplicitFragmentShader();
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        // TOGLIERE il rilievo non puo' essere la causa dell'errore: e' il resto
        // dello shader RM a non compilare in questo istante -- tipicamente al
        // load di un record PARAMETRICO, con l'equazione implicita gia'
        // svuotata. Col rollback il motore si teneva il rilievo del record RM
        // aperto prima mentre il campo diceva vuoto (trovato dal test degli
        // scenari). Il vuoto si registra sempre; la pipeline RM si scarta solo
        // fuori dal Ray Marching (li' non si disegna, e al rientro si ricostruisce
        // da capo): in RM resta quella in uso fino al prossimo apply valido.
        if (code.trimmed().isEmpty()) {
            if (m_engineMode != ModeImplicit && m_pipelineImplicit) {
                m_pipelineImplicit->destroy();
                delete m_pipelineImplicit;
                m_pipelineImplicit = nullptr;
            }
            return;
        }
        m_displacementCode = oldDisp;
        return;
    }

    if (m_pipelineImplicit) {
        m_pipelineImplicit->destroy();
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }
    update();
}

// ==========================================================
// 2D FLAT VIEW
// ==========================================================

// Fissa sul destinatario corrente zoom/pan/rotazione 2D.
// Va chiamata a OGNI modifica, non solo all'uscita dalla vista 2D: finche' la
// trasformazione vive solo nel buffer di lavoro (m_flatZoom/m_flatPan/
// m_flatRotation) appartiene alla vista, non a cio' che si sta editando.
// Scrivendola subito nella parte, hasCustomTexTransform() diventa vero e il ramo
// per-parte del loop UBO la rimette al suo posto.
// Il destinatario e' la parte attiva, o lo stato GLOBALE in ambito "All": sono i
// due casi che il render sa disegnare (vedi flatEditingGlobal e il clamp del
// quad 2D), e vanno tenuti allineati o il mouse scrive dove nessuno legge.
// NB: NON si richiede hasCustomTexture. Due mesh possono avere LA STESSA texture
// e volerla a scale diverse: la trasformazione e' una proprieta' della parte, non
// dello script. Con quel requisito una fascia senza texture propria restava
// agganciata allo zoom globale e si ridimensionava insieme alle altre.
// Solo per la texture di SUPERFICIE (m_flatViewTarget 0): il target 1 e' lo
// sfondo, che ha i propri m_bgZoom/m_bgPan/m_bgRotation ed e' unico.
void GLWidget::commitFlatTransformToActivePart() {
    if (m_flatViewTarget != 0) return;

    // AMBITO "ALL" (nessuna parte selezionata): si sta regolando la texture
    // della SUPERFICIE, quindi la trasformazione appartiene allo stato globale.
    // Senza questo ramo, in "All" il mouse muoveva solo il buffer di lavoro e
    // l'inquadratura si perdeva uscendo dalla vista 2D.
    if (m_activeMeshPart < 0) {
        m_globalTexZoom = m_flatZoom;
        m_globalTexPan = m_flatPan;
        m_globalTexRotation = m_flatRotation;
        return;
    }

    if (!engine) return;
    MeshPart *p = engine->mutableMeshPart(m_activeMeshPart);
    if (!p) return;
    p->texZoom = m_flatZoom;
    p->texPanX = m_flatPan.x();
    p->texPanY = m_flatPan.y();
    p->texRotation = m_flatRotation;
    engine->syncPartAppearance();
}

// Trasformazione 2D della parte attiva, impostata da FUORI la vista 2D: serve a
// chi APPLICA una texture per darle l'inquadratura salvata nel suo preset
// (zoom/pan/rotation del JSON), come il ramo globale fa con setFlatZoom & C.
// Senza, la fascia si teneva la manipolazione della texture PRECEDENTE e ogni
// texture nuova nasceva gia' zoomata/ruotata.
// Aggiorna anche il buffer di lavoro se la parte e' quella in editing 2D, o la
// vista continuerebbe a mostrare la vecchia inquadratura.
bool GLWidget::setActiveMeshTexTransform(float zoom, const QVector2D &pan, float rotation) {
    const bool onPart = applyToActiveMeshPart([&](MeshPart &p){
        p.texZoom = zoom;
        p.texPanX = pan.x();
        p.texPanY = pan.y();
        p.texRotation = rotation;
    });
    if (!onPart) return false;
    if (m_isFlatView && m_flatViewTarget == 0) {
        m_flatZoom = zoom;
        m_flatPan = pan;
        m_flatRotation = rotation;
    }
    update();
    return true;
}

bool GLWidget::setActiveMeshTextureLibName(const QString &name) {
    return applyToActiveMeshPart([&](MeshPart &p){ p.textureLibName = name.trimmed(); });
}

// Carica nei membri globali la trasformazione della parte attiva, cosi' la vista
// 2D parte dall'inquadratura di QUELLA mesh e non da quella dell'ultima toccata.
// Una parte che non ne ha ancora una eredita quella corrente (non si azzera:
// sarebbe un salto visivo a ogni cambio di fascia).
void GLWidget::loadFlatTransformFromActivePart() {
    if (m_flatViewTarget != 0) return;

    // AMBITO "ALL": il buffer di lavoro parte dalla trasformazione GLOBALE, che
    // e' quella che si sta per regolare.
    if (m_activeMeshPart < 0) {
        m_flatZoom = m_globalTexZoom;
        m_flatPan = m_globalTexPan;
        m_flatRotation = m_globalTexRotation;
        return;
    }

    if (!engine) return;
    const std::vector<MeshPart> &parts = engine->getMeshParts();
    if (m_activeMeshPart >= (int)parts.size()) return;
    const MeshPart &p = parts[m_activeMeshPart];
    // Una parte senza trasformazione propria parte da quella GLOBALE, che e'
    // esattamente cio' che sta ereditando e disegnando: cosi' il primo
    // trascinamento non fa un salto.
    if (p.hasCustomTexTransform()) {
        m_flatZoom = p.texZoom;
        m_flatPan = QVector2D(p.texPanX, p.texPanY);
        m_flatRotation = p.texRotation;
    } else {
        m_flatZoom = m_globalTexZoom;
        m_flatPan = m_globalTexPan;
        m_flatRotation = m_globalTexRotation;
    }
}

void GLWidget::setFlatView(bool active) {
    const bool wasFlat = m_isFlatView;
    m_isFlatView = active;

    if (!m_isFlatView) {
        // USCITA DALLA VISTA 2D: fissa sulla parte quanto regolato col mouse.
        if (wasFlat) commitFlatTransformToActivePart();
        buildWireframeGeometry();
    }
    else {
        // INGRESSO in vista 2D: si edita la texture della parte selezionata,
        // quindi si riparte dalla SUA trasformazione, non da quella globale
        // (altrimenti la texture apparirebbe con lo zoom di un'altra mesh).
        loadFlatTransformFromActivePart();
    }

    meshNeedsUpdate = true;
    update();
}

// NB: i getter espongono il BUFFER DI LAVORO, non lo stato globale. Li usa
// inputhandler per il trascinamento incrementale (legge, somma, riscrive) e
// devono quindi riflettere cio' che il mouse sta muovendo sul destinatario
// corrente. Per la persistenza esistono i globalTex*() (vedi glwidget.h), che
// salvano l'inquadratura della texture di SUPERFICIE.
// BERSAGLIO 2 = TEXTURE DEL BORDO: come lo sfondo, i valori sono direttamente
// quelli della texture del bordo (m_borderTex*), senza buffer di lavoro -- il
// bordo ha un'inquadratura sola, e il blocco di uniform del bordo la legge da
// li' (vedi la scrittura dei blocchi del bordo in render).
float GLWidget::getFlatZoom() const {
    if (m_flatViewTarget == 1) return backgroundZoom();
    if (m_flatViewTarget == 2) return m_borderTexZoom;
    return m_flatZoom;
}

void GLWidget::setFlatZoom(float z) {
    if (m_flatViewTarget == 1) { // 1 = Sfondo
        m_bgZoom = std::clamp(z, 0.001f, 1000.0f);
    } else if (m_flatViewTarget == 2) { // 2 = Bordo
        m_borderTexZoom = std::clamp(z, 0.001f, 1000.0f);
    } else { // 0 = Superficie
        m_flatZoom = std::clamp(z, 0.001f, 1000.0f);
        commitFlatTransformToActivePart();
    }
    update();
}

float GLWidget::getFlatRotation() const {
    if (m_flatViewTarget == 1) return backgroundRotation();
    if (m_flatViewTarget == 2) return m_borderTexRotation;
    return m_flatRotation;
}

void GLWidget::setFlatRotation(float angle) {
    if (m_flatViewTarget == 1) {
        m_bgRotation = angle;
    } else if (m_flatViewTarget == 2) {
        m_borderTexRotation = angle;
    } else {
        m_flatRotation = angle;
        commitFlatTransformToActivePart();
    }
    update();
}

void GLWidget::addFlatRotation(float angle) {
    setFlatRotation(getFlatRotation() + angle);
}

void GLWidget::rotateFlat90() {
    if (m_flatViewTarget == 1) { // Sfondo
        m_bgRotation += 90.0f;
    } else if (m_flatViewTarget == 2) { // Bordo
        m_borderTexRotation += 90.0f;
    } else { // Superficie
        m_flatRotation += 90.0f;
        commitFlatTransformToActivePart();
    }

    // Rotazione voluta dall'utente (clic secco o tap in 2D), e la rotazione e'
    // contenuto del file texture. Il segnale va emesso QUI e non dai chiamanti:
    // il clic secco non passa da mouseReleaseEvent (che emette solo dopo un
    // trascinamento) ne' dal ramo touch (takeTouchMovedView esclude il tap),
    // quindi restava l'unica trasformazione 2D che non marcava nulla.
    emit userMovedView();

    update();
}

QVector2D GLWidget::getFlatPan() const {
    if (m_flatViewTarget == 1) return backgroundPan();
    if (m_flatViewTarget == 2) return m_borderTexPan;
    return m_flatPan;
}

void GLWidget::setFlatPan(float x, float y) {
    if (m_flatViewTarget == 1) {
        m_bgPan = QVector2D(x, y);
    } else if (m_flatViewTarget == 2) {
        m_borderTexPan = QVector2D(x, y);
    } else {
        m_flatPan = QVector2D(x, y);
        commitFlatTransformToActivePart();
    }
    update();
}


// ==========================================================
// CAMERA 3D & 4D STATE
// ==========================================================

void GLWidget::set4DLighting(bool enable) {

    engine->set4DLighting(enable);
    meshNeedsUpdate = true;
    update();
}

void GLWidget::setLightingMode4D(int mode) {

    if (mode == 0) {
        m_lightingMode4D = 1; // Radial
    } else if (mode == 1) {
        m_lightingMode4D = 2; // Observer
    } else {
        m_lightingMode4D = 3; // Slice
    }

    meshNeedsUpdate = true;
    update();
}

bool GLWidget::is4DActive() const {
    return engine && engine->is4DLightingEnabled();
}

void GLWidget::setRotation4D(float o, float p, float ps) {
    omega = o; phi = p; psi = ps;
    update();
}

void GLWidget::beginPathHandoff()
{
    m_handoffPos = m_cameraPos;
    if (m_isPathFollowing) {
        m_handoffTarget = m_pathTarget;
        m_handoffUp     = m_pathUp;
        m_handoffRoll   = m_pathRoll;
    } else {
        // Camera libera: ricostruiamo target/up con lo stesso modello del render()
        float radYaw = m_cameraYaw * M_PI / 180.0f;
        float radPitch = m_cameraPitch * M_PI / 180.0f;
        QVector3D front(std::sin(radYaw) * std::cos(radPitch),
                        std::sin(radPitch),
                        -std::cos(radYaw) * std::cos(radPitch));
        QMatrix4x4 rollMat;
        rollMat.rotate(m_cameraRoll, 0.0f, 0.0f, 1.0f);
        m_handoffTarget = m_cameraPos + front;
        m_handoffUp     = rollMat.map(QVector3D(0.0f, 1.0f, 0.0f));
        m_handoffRoll   = 0.0f;   // il roll libero e' gia' dentro l'up
    }
    m_handoffObserver = m_observerPos;
    m_handoffCam4D    = m_cameraPos4D;
    m_pathHandoffK = 0.0f;
    m_pathHandoffActive = true;
}

float GLWidget::advancePathHandoff()
{
    m_pathHandoffK += 0.025f;          // ~1.2 s a un tick ogni 30 ms
    if (m_pathHandoffK >= 1.0f) {
        m_pathHandoffK = 1.0f;
        m_pathHandoffActive = false;
    }
    float k = m_pathHandoffK;
    return k * k * (3.0f - 2.0f * k);  // smoothstep
}

void GLWidget::setCameraPosAndDirection3D(const QVector3D& pos, const QVector3D& targetPoint, float roll)
{
    QVector3D finalPos = pos;
    QVector3D finalTarget = targetPoint;
    // 1. RIPRISTINA L'UP VECTOR ORIGINALE
    QVector3D finalUp(0.0f, 0.0f, 1.0f);
    // 2. IL VERO FIX PER RHI: INVERTI IL ROLLIO
    float rollDeg = qRadiansToDegrees(roll);

    // Handoff dal path 4D: nei primi tick la camera scivola dalla vista
    // catturata al click a quella del path, invece di teletrasportarsi.
    if (m_pathHandoffActive) {
        float k = advancePathHandoff();
        finalPos    = m_handoffPos    * (1.0f - k) + finalPos    * k;
        finalTarget = m_handoffTarget * (1.0f - k) + finalTarget * k;
        finalUp     = m_handoffUp     * (1.0f - k) + finalUp     * k;
        if (finalUp.lengthSquared() > 1e-6f) finalUp.normalize();
        else finalUp = QVector3D(0.0f, 0.0f, 1.0f);
        rollDeg     = m_handoffRoll * (1.0f - k) + rollDeg * k;
    }

    m_cameraPos = finalPos;
    m_pathTarget = finalTarget;
    m_isPathFollowing = true;
    m_pathUp = finalUp;
    m_pathRoll = rollDeg;

    update();
}

void GLWidget::setCameraFrom4DVectors(const QVector4D &pos4D, const QVector4D &target4D, const QVector4D &up4D)
{
    // 1. GESTIONE OSSERVATORE
    QVector4D safeObserverPos = pos4D;
    safeObserverPos.setW(pos4D.w() + 5.0f);

    // (assegnati in fondo: durante l'handoff dal path 3D anche osservatore e
    // camera 4D vengono fusi, altrimenti la proiezione 4D->3D della superficie
    // scatterebbe pur con la camera in blend)

    // 2. PROIEZIONE DELLA POSIZIONE
    QVector3D pos3D = projectPoint4Dto3D(pos4D);
    QVector3D target3D = projectPoint4Dto3D(target4D);

    // 3. CALCOLO VETTORE UP "VISIVO"
    QVector4D upTip4D = pos4D + up4D;
    QVector3D upTip3D = projectPoint4Dto3D(upTip4D);

    // Il vettore UP 3D è la differenza tra punta e base
    QVector3D finalUp3D = (upTip3D - pos3D);

    // 4. GESTIONE SINGOLARITÀ
    if (finalUp3D.lengthSquared() < 0.0001f) {
        finalUp3D = m_pathUp;
    } else {
        finalUp3D.normalize();
    }

    // 5. ANTI-FLIP VISIVO
    if (m_isFirstPathRun) {
        m_lastValidUp = finalUp3D;
        m_isFirstPathRun = false;
    } else {
        if (QVector3D::dotProduct(finalUp3D, m_lastValidUp) < 0.0f) {
            finalUp3D = -finalUp3D;
        }
        m_lastValidUp = finalUp3D;
    }

    // 5b. HANDOFF dal path 3D: scivolata dalla vista catturata al click a
    // quella del path (l'anti-flip sopra resta sul vettore puro del path).
    QVector3D dispUp = finalUp3D;
    float rollDeg = 0.0f;
    QVector4D newObserver = safeObserverPos;
    QVector4D newCam4D = pos4D;
    if (m_pathHandoffActive) {
        float k = advancePathHandoff();
        pos3D    = m_handoffPos    * (1.0f - k) + pos3D    * k;
        target3D = m_handoffTarget * (1.0f - k) + target3D * k;
        dispUp   = m_handoffUp     * (1.0f - k) + dispUp   * k;
        if (dispUp.lengthSquared() > 1e-6f) dispUp.normalize();
        else dispUp = finalUp3D;
        rollDeg     = m_handoffRoll * (1.0f - k);   // il 4D lavora a roll 0
        newObserver = m_handoffObserver * (1.0f - k) + safeObserverPos * k;
        newCam4D    = m_handoffCam4D    * (1.0f - k) + pos4D           * k;
    }

    m_pathUp = dispUp;
    m_observerPos = newObserver;
    m_cameraPos4D = newCam4D;

    // 6. APPLICAZIONE
    m_cameraPos = pos3D;
    m_pathTarget = target3D;
    m_isPathFollowing = true;
    m_pathRoll = rollDeg;

    m_view.setToIdentity();
    m_view.lookAt(pos3D, target3D, dispUp);

    update();
}

void GLWidget::zoomCamera(float delta) {
    // Calcoliamo il vettore "Avanti" completo
    float radYaw = m_cameraYaw * M_PI / 180.0f;
    float radPitch = m_cameraPitch * M_PI / 180.0f;

    // Vettore direzione sguardo
    QVector3D front;
    front.setX(std::sin(radYaw) * std::cos(radPitch));
    front.setY(std::sin(radPitch));
    front.setZ(-std::cos(radYaw) * std::cos(radPitch));

    // Spostiamo la camera lungo questo vettore
    m_cameraPos += front * delta;

    update();
}

// YAW/PITCH EFFETTIVI: vedi il commento esteso in glwidget.h.
//
// Fuori dalla modalita' path i campi grezzi sono gia' la verita'. Dentro, la
// direzione di vista e' (m_pathTarget - m_cameraPos) e va convertita INVERTENDO
// esattamente la formula con cui paintGL costruisce "front":
//     front = ( sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch) )
// da cui  pitch = asin(front.y)  e  yaw = atan2(front.x, -front.z).
// Le due formule vanno tenute allineate: se cambia "front" in paintGL, cambia
// anche qui.
static void effectiveYawPitch(bool pathMode, const QVector3D &camPos,
                              const QVector3D &pathTarget,
                              float rawYaw, float rawPitch,
                              float *outYaw, float *outPitch)
{
    if (!pathMode) { *outYaw = rawYaw; *outPitch = rawPitch; return; }

    const QVector3D d = pathTarget - camPos;
    // Target coincidente con la camera: direzione indefinita, si tengono i
    // valori grezzi invece di produrre NaN.
    if (d.lengthSquared() < 1e-12f) { *outYaw = rawYaw; *outPitch = rawPitch; return; }

    const QVector3D f = d.normalized();
    *outPitch = qRadiansToDegrees(std::asin(qBound(-1.0f, f.y(), 1.0f)));
    *outYaw   = qRadiansToDegrees(std::atan2(f.x(), -f.z()));
}

float GLWidget::getEffectiveCameraYaw() const
{
    float y, p;
    effectiveYawPitch(m_isPathFollowing, m_cameraPos, m_pathTarget,
                      m_cameraYaw, m_cameraPitch, &y, &p);
    return y;
}

float GLWidget::getEffectiveCameraPitch() const
{
    float y, p;
    effectiveYawPitch(m_isPathFollowing, m_cameraPos, m_pathTarget,
                      m_cameraYaw, m_cameraPitch, &y, &p);
    return p;
}

void GLWidget::addCameraRotation(float dYaw, float dPitch) {
    m_cameraYaw += dYaw;
    m_cameraPitch += dPitch;

    if (m_cameraPitch > 89.0f) m_cameraPitch = 89.0f;
    if (m_cameraPitch < -89.0f) m_cameraPitch = -89.0f;

  //  update();
}

void GLWidget::resetTransformations()
{
    // =======================================================
    // FIX 1: SALVAVITA ZOOM 3D (Risolve l'effetto "Gigante")
    // =======================================================
    float current3DZoom = m_cameraPos.length();
    // Se la telecamera era dentro la figura (distanza < 2.5), la tiriamo indietro a 4.0
    if (std::isnan(current3DZoom) || std::isinf(current3DZoom) || current3DZoom < 2.5f) {
        current3DZoom = 4.0f;
    }

    // =======================================================
    // FIX 2: SALVAVITA PROIEZIONE 4D
    // =======================================================
    // Leggiamo la distanza dall'Osservatore fisso, NON dalla telecamera mobile!
    float current4DZoom = m_observerPos.w();
    if (std::isnan(current4DZoom) || std::isinf(current4DZoom) || std::abs(current4DZoom) < 1.5f) {
        current4DZoom = 4.0f;
    }

    Framing f;
    f.cameraPos = QVector3D(0.0f, 0.0f, current3DZoom);
    f.observerPos = QVector4D(0.0f, 0.0f, 0.0f, current4DZoom);
    // Quota del piano di sezione: azzerata davvero (a differenza di w, che e'
    // una DISTANZA e ha il salvavita zoom qui sopra). p=0 e' la sezione centrale,
    // quella da cui ogni superficie Cross Section deve ripartire.
    f.crossSectionP = 0.0f;
    resetFraming(f);

    // Orientamento neutro (angoli 4D al valore di Orientation, vedi glwidget.h).
    // Il marchio "ruotato a mano" non si tocca: qui non si rimette una posa
    // fotografata, si azzera.
    Orientation o;
    o.userRotated = m_userRotatedManually;
    resetOrientation(o);

    // FOV al default: il reset della vista annulla anche lo "zoom" del FOV di
    // un path appena abbandonato (senza questo, dopo un path a FOV alto la
    // superficie resettata apparirebbe rimpicciolita). Se un path e' ANCORA in
    // corsa, il suo primo tick riapplica subito il proprio FOV (stesso schema
    // della posa): il default vale solo per la vista libera.
    m_cameraFov = 45.0f;
}

GLWidget::Framing GLWidget::framing() const
{
    Framing f;
    f.cameraPos = m_cameraPos;
    f.yaw = m_cameraYaw;
    f.pitch = m_cameraPitch;
    f.roll = m_cameraRoll;
    f.observerPos = m_observerPos;
    f.crossSectionP = m_crossSectionP;
    return f;
}

GLWidget::Orientation GLWidget::orientation() const
{
    Orientation o;
    o.rotation = m_rotationQuat;
    o.omega = omega;
    o.phi = phi;
    o.psi = psi;
    o.userRotated = m_userRotatedManually;
    return o;
}

void GLWidget::resetFraming(const Framing &f)
{
    // Vista libera: la modalita' path si spegne (un path in corsa la riaccende
    // al suo prossimo tick, insieme alla propria camera).
    m_isPathFollowing = false;

    m_cameraPos = f.cameraPos;
    m_cameraYaw = f.yaw;
    m_cameraPitch = f.pitch;
    m_cameraRoll = f.roll;

    // Osservatore 4D e camera 4D coincidono fuori dal path 4D.
    m_observerPos = f.observerPos;
    m_cameraPos4D = f.observerPos;
    m_crossSectionP = f.crossSectionP;

    // Reset dei vettori di mira
    m_pathTarget = QVector3D(0.0f, 0.0f, 0.0f);
    m_pathUp = QVector3D(0.0f, 1.0f, 0.0f);
    m_lastValidUp = QVector3D(0.0f, 1.0f, 0.0f);
    m_isFirstPathRun = true;

    m_flatPan = QVector2D(0.0f, 0.0f);
    m_bgPan = QVector2D(0.0f, 0.0f);

    // Azzera le memorie di backup
    m_viewStates[0] = ViewState();
    m_viewStates[1] = ViewState();

    meshNeedsUpdate = true;
    update();
}

void GLWidget::resetOrientation(const Orientation &o)
{
    precession = 0.0f;
    nutation = 0.0f;
    spin = 0.0f;

    m_rotationQuat = o.rotation;
    omega = o.omega;
    phi = o.phi;
    psi = o.psi;
    m_userRotatedManually = o.userRotated;

    meshNeedsUpdate = true;
    update();
}

void GLWidget::virtualMove(MoveDir dir, float speed3D, float speed4D)
{
    // speed3D e speed4D arrivano dagli slider e vanno da 0.001 a 0.1.
    // Li moltiplichiamo per dei "moltiplicatori base" per avere una buona sensibilità
    float speed     = speed3D * 5.0f;   // Spostamento 3D standard (Avanti, Indietro...)
    float rollSpeed = speed3D * 10.0f;  // Rollio della telecamera 3D
    float rotSpeed  = speed4D * 2.0f;   // Rotazioni 4D (Omega, Phi, Psi)
    float obsSpeed  = speed4D * 5.0f;   // Spostamento 4D lineare (X+, P-, ecc.)

    // QUOTA DEL PIANO DI SEZIONE (Cross Section): passo 10 volte piu' fine dello
    // spostamento 4D parametrico. Il moltiplicatore 5 di obsSpeed e' tarato sulla
    // scala della SCENA, dove si attraversano unita' intere; qui si muove la
    // quota di un taglio la cui estensione utile e' piccola -- sul T^3 di default
    // circa +-0.2 -- e con 5 anche il minimo dello slider (0.005/tick) copre il
    // 2.5% dell'intervallo a ogni scatto: si salta l'oggetto invece di esplorarlo.
    // Con 0.5: minimo 0.12% a scatto (controllo fine), default 1.25%, massimo
    // 12.5% (attraversa in 8 scatti, veloce ma senza saltare).
    const float kSectionPMul = 0.5f;
    float sectionSpeed = speed4D * kSectionPMul;

    bool updateNeeded = true;

    float radYaw = m_cameraYaw * M_PI / 180.0f;
    QVector3D front(sin(radYaw), 0, -cos(radYaw));
    QVector3D right(cos(radYaw), 0, sin(radYaw));

    switch (dir) {
    case MoveForward: m_cameraPos += front * speed; break;
    case MoveBack:    m_cameraPos -= front * speed; break;
    case MoveLeft:    m_cameraPos -= right * speed; break;
    case MoveRight:   m_cameraPos += right * speed; break;
    case MoveUp:      m_cameraPos.setY(m_cameraPos.y() + speed); break;
    case MoveDown:    m_cameraPos.setY(m_cameraPos.y() - speed); break;
    case RollLeft:    m_cameraRoll += rollSpeed; break;
    case RollRight:   m_cameraRoll -= rollSpeed; break;

    case ObsMoveXPos:
        m_cameraPos4D.setX(m_cameraPos4D.x() + obsSpeed);
        m_observerPos.setX(m_observerPos.x() + obsSpeed);
        break;
    case ObsMoveXNeg:
        m_cameraPos4D.setX(m_cameraPos4D.x() - obsSpeed);
        m_observerPos.setX(m_observerPos.x() - obsSpeed);
        break;

    case ObsMoveYPos:
        m_cameraPos4D.setY(m_cameraPos4D.y() + obsSpeed);
        m_observerPos.setY(m_observerPos.y() + obsSpeed);
        break;
    case ObsMoveYNeg:
        m_cameraPos4D.setY(m_cameraPos4D.y() - obsSpeed);
        m_observerPos.setY(m_observerPos.y() - obsSpeed);
        break;

    case ObsMoveZPos:
        m_cameraPos4D.setZ(m_cameraPos4D.z() + obsSpeed);
        m_observerPos.setZ(m_observerPos.z() + obsSpeed);
        break;
    case ObsMoveZNeg:
        m_cameraPos4D.setZ(m_cameraPos4D.z() - obsSpeed);
        m_observerPos.setZ(m_observerPos.z() - obsSpeed);
        break;

    case ObsMovePPos:
        // In RAY MARCHING i tasti P muovono la QUOTA DEL PIANO DI SEZIONE, non
        // l'osservatore 4D: li' u_observerPos non e' letta dal template, mentre
        // la quota decide quale fetta dell'ipersuperficie si vede. In
        // parametrico restano quelli storici.
        if (m_engineMode == ModeImplicit) { m_crossSectionP += sectionSpeed; break; }
        m_cameraPos4D.setW(m_cameraPos4D.w() + obsSpeed);
        m_observerPos.setW(m_observerPos.w() + obsSpeed);
        break;
    case ObsMovePNeg:
        if (m_engineMode == ModeImplicit) { m_crossSectionP -= sectionSpeed; break; }
        m_cameraPos4D.setW(m_cameraPos4D.w() - obsSpeed);
        m_observerPos.setW(m_observerPos.w() - obsSpeed);
        break;

    case RotOmegaPos:
        omega += rotSpeed;
        break;
    case RotOmegaNeg:
        omega -= rotSpeed;
        break;

    case RotPhiPos:
        phi += rotSpeed;
        break;
    case RotPhiNeg:
        phi -= rotSpeed;
        break;

    case RotPsiPos:
        psi += rotSpeed;
        break;
    case RotPsiNeg:
        psi -= rotSpeed;
        break;

    default:
        updateNeeded = false;
        break;
    }

    update();
}


// ==========================================================
// ANIMATION & MOTION CONTROL
// ==========================================================

void GLWidget::addObjectRotation(float dPrecession, float dNutation, float dSpin)
{
    // --- ROTAZIONE BASATA SUGLI ASSI DELLO SCHERMO (Trackball Style) ---

    // 1. Mouse Destra/Sinistra (dPrecession) -> Ruota attorno all'asse Y verticale
    QQuaternion yRot = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, dPrecession);

    // 2. Mouse Su/Giù (dNutation) -> Ruota attorno all'asse X orizzontale
    QQuaternion xRot = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, dNutation);

    // 3. Spin (dSpin) -> Ruota attorno all'asse Z (profondità/vista)
    QQuaternion zRot = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, dSpin);

    // 4. Applica le rotazioni: Ordine: Y * X * Z * VecchiaRotazione
    // Questo garantisce che la rotazione avvenga rispetto al punto di vista dell'osservatore
    m_rotationQuat = yRot * xRot * zRot * m_rotationQuat;

    // Normalizza per evitare errori di arrotondamento nel tempo
    m_rotationQuat.normalize();

    emit rotationChanged();
    update();
}

void GLWidget::pauseMotion() {
    if (rotationTimer) rotationTimer->stop();
}

void GLWidget::resumeMotion() {
    if (rotationTimer) rotationTimer->start();
}

void GLWidget::startAnimationTimer() {
    if (m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();
    }
}

// Ferma solo i TICK: gli orologi restano dove sono. Qui si scriveva
// m_manualTime = elapsed reale, una base sommata a TUTTI gli orologi: ogni
// stop/start (il Save Record, il menu della libreria) faceva saltare avanti le
// animazioni dei secondi trascorsi dall'ultimo start -- misurato: geometria da
// 3.97 a 8.10, texture FERMA da 0 a 4.09.
void GLWidget::stopAnimationTimer() {
    if (m_animTimer && m_animTimer->isActive()) m_animTimer->stop();
}

void GLWidget::stopAllTimers() {
    // Ferma la rotazione fisica
    if (rotationTimer->isActive()) rotationTimer->stop();

    // Ferma l'animazione shader (Shadertoy)
    if (m_animTimer->isActive()) m_animTimer->stop();
}

// Riporta all'origine i tre orologi globali (le fasce hanno i propri reset).
// Nessuna base da azzerare a parte: il tempo mostrato E' l'orologio, quindi
// non puo' piu' "resuscitare" da un timer esterno (il bug della sfera RM che
// nasceva a t=43 veniva da li').
void GLWidget::resetTime() {
    m_surfaceTimer.restart();
    m_lastRealTime = 0.0f;
    m_timeGeom = 0.0f;
    m_timeTex = 0.0f;
    m_timeBg = 0.0f;
    update();
}

// Riavvia dall'inizio il SOLO orologio indicato. Per la texture di SUPERFICIE
// colore e displacement leggono lo stesso clock (dummyZero.x), quindi basta
// azzerare m_timeTex; lo SFONDO ha il suo (m_timeBg).
// Non si passa da resetTime(): quella azzera anche m_timeGeom,
// cioe' la geometria, che un click sulla texture non riguarda -- fermerebbe la
// superficie animata insieme alla texture.
void GLWidget::resetTextureTime(bool background) {
    if (background) {
        m_timeBg = 0.0f;
    } else {
        m_timeTex = 0.0f;
        // Anche gli orologi PER-MESH: appartengono allo stesso modulo texture e
        // il riclic lo riavvia tutto (il chiamante rilancia gia' le loro
        // animazioni con restartAnimatedMeshTextures). Lasciarli avanti
        // ripartirebbe la globale da zero e le fasce da meta' ciclo.
        if (engine) {
            for (MeshPart &mp : engine->mutableMeshParts())
                mp.timeTex = 0.0f;
        }
    }
    // NIENTE m_surfaceTimer.restart() qui: quel timer produce il dt CONDIVISO da
    // tutti e tre gli orologi (geometria, texture, sfondo), e ribasarlo farebbe
    // perdere a geometria e sfondo la frazione di tempo gia' trascorsa dal loro
    // ultimo tick. Il reset cade fra due tick, quindi il primo dt dopo di esso e'
    // quello normale di un frame: la texture riparte da ~0 senza scatti.
    update();
}

bool GLWidget::anyClockAdvanced() const
{
    if (m_timeGeom != 0.0f || m_timeTex != 0.0f || m_timeBg != 0.0f || m_borderTimeTex != 0.0f)
        return true;
    if (engine)
        for (const MeshPart &mp : engine->getMeshParts())
            if (mp.timeTex != 0.0f) return true;
    return false;
}

void GLWidget::resetAllClocks() {
    if (engine) {
        for (MeshPart &mp : engine->mutableMeshParts())
            mp.timeTex = 0.0f;
    }
    m_borderTimeTex = 0.0f;
    resetTime();
}

void GLWidget::setSurfaceAnimating(bool animating) {
    m_surfaceAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();
    }
}

void GLWidget::setBackgroundTextureAnimating(bool animating)
{
    m_bgAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();
        m_animTimer->start();
    }
}

void GLWidget::setSurfaceTextureAnimating(bool animating) {
    m_texAnimating = animating;
    if (animating && m_animTimer && !m_animTimer->isActive()) {
        m_surfaceTimer.restart();   // azzera la base del dt: niente salti
        m_animTimer->start();        // <-- avvia anche se il flag era già true
    }
}

// Unica implementazione dell'avanzamento degli orologi (vedi glwidget.h):
// la chiama il render col dt reale e il loop del recorder col dt del frame.
// Ogni orologio avanza SOLO col proprio flag: e' questo che tiene fermo un
// modulo fermato dall'utente, dal vivo come nel video. Se corrono insieme,
// avanzano dello stesso identico dt e restano in sincrono.
void GLWidget::advanceClocksBy(float dt) {
    // Il passo del frame serve anche alla sezione dei tubi (smorzamento degli
    // scatti): preso qui, che e' il punto comune a render live e REC.
    m_tubeFrameDt = dt;
    if (m_surfaceAnimating) m_timeGeom += dt;
    if (m_texAnimating)     m_timeTex  += dt;
    if (m_bgAnimating)      m_timeBg   += dt;

    // OROLOGI PER-MESH DELLA TEXTURE: stesso dt dei globali, ogni parte col
    // proprio flag -- e' cio' che permette di fermarne una sola.
    if (engine) {
        for (MeshPart &mp : engine->mutableMeshParts())
            if (mp.texAnimating) mp.timeTex += dt;
    }

    // OROLOGIO DELLA TEXTURE DEL BORDO: stesso dt, proprio flag.
    if (m_borderTexAnimating) m_borderTimeTex += dt;
}

GLWidget::ClockTimes GLWidget::clockTimes() const {
    return { kClockOrigin + m_timeGeom, kClockOrigin + m_timeTex, kClockOrigin + m_timeBg };
}


// ==========================================================
// UTILITIES
// ==========================================================

void GLWidget::beginHiResCapture(int w, int h) {
    if (w <= 0 || h <= 0) return;
    m_hiResCapture = true;
    // Fissa il color buffer offscreen di QRhiWidget alla risoluzione di export:
    // d'ora in poi render() disegna NATIVAMENTE a w x h (renderTarget()->pixelSize()
    // diventa questa) e grabFramebuffer() restituisce pixel nitidi a quella misura.
    setFixedColorBufferSize(QSize(w, h));
    // La riconfigurazione del render target avviene al prossimo grab/render:
    // grabFramebuffer() in getFrameForVideo fa un render sincrono e raccoglie la
    // nuova dimensione. Niente processEvents (ignorato su iOS).
}

void GLWidget::endHiResCapture() {
    if (!m_hiResCapture) return;
    m_hiResCapture = false;
    // Torna a seguire la dimensione a schermo del widget (QSize() = dinamica).
    setFixedColorBufferSize(QSize());
    this->repaint();
}

QImage GLWidget::getFrameForVideo(int targetW, int targetH, bool useFbo) {
    // Mesh CUSTOM (flusso geodetico): i vertici correnti del motore sono la
    // griglia appena caricata da setCustomMesh (che alza meshNeedsUpdate per
    // il re-upload, fatto da render() durante il grab). computeMesh() qui la
    // SOVRASCRIVEREBBE con la valutazione parametrica delle equazioni — per
    // gli script metrici la display map, cioe' una lamina piatta nel video.
    // Il ricalcolo CPU pre-grab serve solo alla mesh parametrica.
    if (meshNeedsUpdate && !m_isCustomMesh) updateSurfaceData();

    // 1. Catturiamo il frame. grabFramebuffer() (QRhiWidget) esegue da sé un render
    //    SINCRONO e restituisce i PIXEL REALI del color buffer: non serve repaint()
    //    né processEvents (quest'ultimo con ExcludeUserInputEvents è IGNORATO su iOS
    //    e spammava il log, oltre a non forzare nulla). In modalità hi-res capture
    //    (FBO) il buffer è già fissato alla risoluzione di export -> nessun upscale.
    QImage img = this->grabFramebuffer();

    // Fallback anti-crash: se la cattura RHI fallisce, prova il vecchio grab().
    if (img.isNull()) {
        img = this->grab().toImage();
    }
    if (img.isNull()) {
        img = QImage(targetW > 0 ? targetW : 1920,
                     targetH > 0 ? targetH : 1080,
                     QImage::Format_RGBA8888);
        img.fill(Qt::black);
        return img;
    }

    // 3. In hi-res capture il buffer è GIÀ alla risoluzione di export: NON riscalare
    //    (eviterebbe la nitidezza appena guadagnata). Negli altri casi (motore
    //    Screen Upscale con risoluzione fissa) la vista entra TUTTA nel formato
    //    del video, alla scala dello schermo, e le fasce che avanzano restano
    //    nere: ai lati se la vista e' piu' stretta del video, sopra e sotto se
    //    piu' larga. Stirandola (IgnoreAspectRatio) usciva deformata;
    //    ritagliandola cambiava la scala (con la Library aperta, vista quasi
    //    quadrata, il toro usciva ingrandito e tagliato). A riempire il formato
    //    di scena vera e' il motore FBO, che ridisegna con l'aspetto del video.
    //    Il video conta i PIXEL: grabFramebuffer porta il devicePixelRatio dello
    //    schermo (2 su Retina) e drawImage disegnava la vista a meta' misura, in
    //    un quarto del fotogramma.
    if (!m_hiResCapture && targetW > 0 && targetH > 0 &&
            (img.width() != targetW || img.height() != targetH)) {
        QImage fitted = img.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        fitted.setDevicePixelRatio(1.0);
        QImage framed(targetW, targetH, fitted.format());
        framed.fill(Qt::black);
        QPainter(&framed).drawImage((targetW - fitted.width()) / 2, (targetH - fitted.height()) / 2, fitted);
        img = framed;
    }

    return img;
}

bool GLWidget::validateAndApplyParametricShader(const QString &customLogic)
{
    // TOGLIERE il codice custom non puo' essere la causa di un errore: si
    // registra sempre, senza dry-run (un rifiuto lascerebbe nel motore la
    // texture di prima a texture ormai spenta). Stessa regola di
    // setTextureCode("") e setDisplacementCode("") per il Ray Marching.
    if (customLogic.trimmed().isEmpty()) {
        m_customFragmentCode.clear();
        rebuildShader();
        return true;
    }

    // 1. DRY RUN FRAGMENT
    QString fsSource = createFragmentShaderSource(customLogic);
    {
        QShaderBaker baker;
        baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });
        QShader shader = baker.bake();
        if (!shader.isValid()) {
            m_lastCompilationError = "FRAGMENT: " + baker.errorMessage();
            return false;
        }
    }

    // NIENTE prova del VERTEX: il codice della texture li' non c'e', e il
    // vertex si costruisce dalle equazioni del momento, gia' provate al loro
    // commit (setParametricEquations). A meta' load potevano non combaciare
    // ancora col vincolo (Oloid, vincolo in W: "'W' undeclared") e la texture
    // veniva rifiutata per un errore non suo. rebuildShader e' differito: la
    // pipeline si ricostruisce al frame dopo, con lo stato del load completo.
    // (In Ray Marching poi quel vertex non disegna nulla, e con uno script
    // implicito falliva sempre: la SDF su 'p' dentro getRawPosition.)

    // 2. APPLICA
    m_customFragmentCode = customLogic;
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyImplicitShader(const QString &eqF, const QString &texCode, const QString &dispCode,
                                              bool useCrossSection)
{
    // Salvataggio di emergenza dei vecchi parametri funzionanti
    QString oldEq = useCrossSection ? m_eqCrossSectionF : m_eqImplicitF;
    QString oldTex = m_textureCode;
    QString oldDisp = m_displacementCode;
    bool oldUsesCrossSection = m_implicitUsesCrossSection;

    // Prepariamo i nuovi parametri per generare lo shader
    if (useCrossSection) {
        m_eqCrossSectionF = eqF;
    } else {
        m_eqImplicitF = eqF;
    }
    m_implicitUsesCrossSection = useCrossSection;
    m_textureCode = texCode;
    m_displacementCode = dispCode;

    QString fsSource = createImplicitFragmentShader();

    // DRY RUN
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();

        // RIPRISTINO: Se la compilazione fallisce, rimettiamo tutto a posto!
        if (useCrossSection) {
            m_eqCrossSectionF = oldEq;
        } else {
            m_eqImplicitF = oldEq;
        }
        m_implicitUsesCrossSection = oldUsesCrossSection;
        m_textureCode = oldTex;
        m_displacementCode = oldDisp;
        return false;
    }

    // Se compila, la pipeline prosegue sicura. Questo percorso setta m_eqImplicitF
    // direttamente (bypassa setImplicitEquation), quindi rileviamo qui il
    // condizionamento del campo per il fallback trasparenza (vedi m_implicitIllConditioned).
    detectImplicitConditioning(eqF);
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyTextureDisplacement(const QString &texCode, const QString &dispCode)
{
    // Salvataggio di emergenza
    QString oldTex = m_textureCode;
    QString oldDisp = m_displacementCode;

    // Prepariamo i nuovi parametri per generare lo shader. m_eqImplicitF
    // resta invariato — è già valido (per i record script-mode è settato da
    // validateAndApplyImplicitScript).
    m_textureCode = texCode;
    m_displacementCode = dispCode;

    QString fsSource = createImplicitFragmentShader();

    // DRY RUN
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();
        // RIPRISTINO
        m_textureCode = oldTex;
        m_displacementCode = oldDisp;
        return false;
    }

    // Se compila, la pipeline prosegue
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyBackgroundShader(const QString &scriptCode)
{
    // Genera la stessa stringa fsSource che produrrebbe rebuildBackgroundShader
    QString fsSource = createBackgroundFragmentShader(false, scriptCode);

    // DRY RUN con un baker locale: a differenza di bakeShader (la funzione del
    // widget che genera multi-target SPIR-V/GLSL/HLSL/MSL), qui generiamo solo
    // SPIR-V. Questo rende isValid() affidabile: se SPIR-V fallisce, sappiamo
    // che lo shader è rotto e blocchiamo prima di toccare la pipeline.
    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = baker.errorMessage();
        return false;
    }

    // OK: applichiamo davvero
    m_bgScriptCode = scriptCode;
    m_bgIsScript = true;
    m_useBackgroundTexture = true;
    rebuildBackgroundShader(false, scriptCode);
    update();
    return true;
}

bool GLWidget::validateAndApplyParametricScript(const QString &scriptCodeGLSL)
{
    // Salva lo stato attuale dello script per ripristinarlo se fallisce
    QString oldScript = engine->getScriptCodeGLSL();
    QString oldCutout = engine->getCutoutCodeGLSL();
    bool oldScriptMode = engine->isScriptModeActive();
    // Lo script sostituisce un eventuale tubo.
    const bool oldTube = engine->isTubeModeActive();

    // Applica temporaneamente per testare
    engine->setScriptCodeGLSL(scriptCodeGLSL);
    engine->setScriptMode(true);
    engine->setTubeMode(false);

    // Genera il vertex shader con lo script applicato
    QString vsSource = createVertexShaderSource("0", "0", "0", "0");

    // DRY RUN: solo SPIR-V con baker locale (vedi pattern background)
    QShaderBaker baker;
    baker.setSourceString(vsSource.toUtf8(), QShader::VertexStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "VERTEX (script): " + baker.errorMessage();
        // RIPRISTINO: rimettiamo lo stato di prima
        engine->setScriptCodeGLSL(oldScript);
        engine->setCutoutCodeGLSL(oldCutout);
        engine->setScriptMode(oldScriptMode);
        engine->setTubeMode(oldTube);
        return false;
    }

    // DRY RUN FRAGMENT: valida anche l'eventuale sezione CUTOUT (già impostata
    // nell'engine da onRunScriptClicked prima di chiamare questa funzione).
    QString fsSource = createFragmentShaderSource(m_customFragmentCode);
    QShaderBaker fragBaker;
    fragBaker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    fragBaker.setGeneratedShaderVariants({QShader::StandardShader});
    fragBaker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader fragShader = fragBaker.bake();
    if (!fragShader.isValid()) {
        m_lastCompilationError = "FRAGMENT (cutout): " + fragBaker.errorMessage();
        engine->setScriptCodeGLSL(oldScript);
        engine->setCutoutCodeGLSL(oldCutout);
        engine->setScriptMode(oldScriptMode);
        engine->setTubeMode(oldTube);
        return false;
    }

    // OK: lo script è valido. Lo lasciamo applicato e schedula il rebuild.
    rebuildShader();
    return true;
}

bool GLWidget::validateAndApplyImplicitScript(const QString &scriptCodeGLSL)
{
    QString oldScript     = engine->getScriptCodeGLSL();
    bool    oldScriptMode = engine->isScriptModeActive();

    engine->setScriptCodeGLSL(scriptCodeGLSL);
    engine->setScriptMode(true);

    // createImplicitFragmentShader() inietta lo scriptCodeGLSL appena impostato
    QString fsSource = createImplicitFragmentShader();

    QShaderBaker baker;
    baker.setSourceString(fsSource.toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({ {QShader::SpirvShader, QShaderVersion(100)} });

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        m_lastCompilationError = "FRAGMENT (script): " + baker.errorMessage();
        engine->setScriptCodeGLSL(oldScript);   // rollback
        engine->setScriptMode(oldScriptMode);
        return false;
    }

    // Le superfici da script usano GLSL grezzo (non un campo algebrico x,y,z che il
    // parser CPU possa valutare): la rilevazione "campo a prodotto" non si applica.
    // Azzeriamo il flag cosi' non resta stantio da un preset "Chain" caricato prima
    // (l'autore dello script e' responsabile della propria trasparenza).
    m_implicitIllConditioned = false;
    rebuildShader();
    return true;
}


// ==========================================================
// RISORSE QRHI IMPLICIT (RAY MARCHING)
// ==========================================================

void GLWidget::buildImplicitPipeline()
{
    if (m_pipelineImplicit) {
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
    }

    // 1. Assicuriamoci che il VBO dello schermo intero esista (sfruttiamo quello del background!)
    if (!m_bgVbo) {
        initBackgroundShader();
    }

    m_pipelineImplicit = rhi()->newGraphicsPipeline();
    m_pipelineImplicit->setTopology(QRhiGraphicsPipeline::Triangles);

    // 2. Diciamo alla GPU come leggere i vertici del quad
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    m_pipelineImplicit->setVertexInputLayout(inputLayout);

    // 3. VERTEX SHADER (Adattato da raymarch.vert per QRhi)
    QString vsSource = "#version 450\n"
                       "layout(location=0) in vec3 position;\n"
                       "layout(location=1) in vec4 normal;\n"
                       "layout(location=2) in vec2 texCoord;\n"
                       "layout(location=0) out vec2 fragCoord;\n"
                       "void main() {\n"
                       "  // Passiamo le coordinate al Fragment Shader (da 0 a 1)\n"
                       "  fragCoord = texCoord;\n"
                       "  // Disegniamo il quadrato che copre tutto lo schermo\n"
                       "  gl_Position = vec4(position.xy, 0.999, 1.0);\n"
                       "}\n";

    // 4. FRAGMENT SHADER: Generiamo lo shader di Ray Marching con l'equazione corrente!
   QString fsSource = createImplicitFragmentShader();

    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    if (!vs.isValid() || !fs.isValid()) {
        m_lastCompilationError = "Implicit pipeline: invalid shader stage.";
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
        return;
    }

    m_pipelineImplicit->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineImplicit->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    m_pipelineImplicit->setSampleCount(renderTarget()->sampleCount());

    // Per il Ray Marching, la profondità si calcola via matematica, non via Z-Buffer nativo
    m_pipelineImplicit->setDepthTest(false);
    m_pipelineImplicit->setDepthWrite(false);
    m_pipelineImplicit->setCullMode(QRhiGraphicsPipeline::None);

    // --- TRASPARENZA ---
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_pipelineImplicit->setTargetBlends({ blend });

    // 5. Usiamo temporaneamente i bindings standard (che contengono il tuo UBO con la telecamera)
    m_pipelineImplicit->setShaderResourceBindings(m_bindings);

    if (!m_pipelineImplicit->create()) {
        m_lastCompilationError = "Implicit pipeline: create() failed.";
        delete m_pipelineImplicit;
        m_pipelineImplicit = nullptr;
        return;
    }
}


// ==========================================================
// IMPLICIT EQUATIONS STATE
// ==========================================================

QString GLWidget::createImplicitFragmentShader()
{
    const QString &activeEqF = m_implicitUsesCrossSection ? m_eqCrossSectionF : m_eqImplicitF;
    QString safeEqF = GlslTranslator::translateEquation(activeEqF);

    // Per garantire la compatibilità con script stile Shadertoy:
    safeEqF.replace(QRegularExpression("\\biTime\\b"), "t");

    // Fallback di sicurezza se la stringa e' vuota: un campo COSTANTE POSITIVO,
    // che il marcher non interseca mai -> scena vuota.
    // Era "x*x + y*y + z*z - 1.0", cioe' una SFERA: "niente" diventava una
    // superficie che nessuno aveva chiesto. In Cross Section si vedeva bene,
    // perche' quella sfera passa anche da %CROSS_SECTION_P% e veniva mostrata
    // come sezione ruotata in 4D -- una forma irriconoscibile comparsa dal
    // nulla. Il fallback deve essere NEUTRO: se non c'e' equazione non c'e'
    // superficie. Stesso valore che resetScene usa per la scena vuota
    // (MainWindow::kEmptyImplicitField), tenuto in sincrono a mano perche'
    // GLWidget non include mainwindow.h.
    if (safeEqF.trimmed().isEmpty()) safeEqF = "1.0";

    QString templateSource = loadShaderSource(":/shaders/raymarch_template.txt");
    templateSource.remove(QRegularExpression("^\\s*#version\\s+450\\s*\n?"));

    // MAX_FACES del ramo trasparente (raymarch_template.txt): ogni faccia costa 3
    // marchNextLayer (loop di marcia completi) per pixel. Su Android (GPU Adreno) a 8
    // facce, con un campo SDF pesante e alpha<1, il frame supera il timeout di rendering
    // -> GPU fault -> l'app viene terminata (verificato su Galaxy Tab S9 con N-Tours
    // trasparente). Riduciamo a 3 SOLO su mobile: la trasparenza resta (entrata+uscita+
    // un secondo guscio per le concavita'), ma il costo cala ~60%. Desktop invariato (8).
    // RETE DI SICUREZZA TRASPARENZA — MOBILE (desktop INVARIATO).
    // Il ramo trasparente costa MAX_FACES x 3 chiamate a marchNextLayer per pixel, e
    // ogni marchNextLayer e' un loop fino a MAX_LAYER_STEPS con 4 map() a passo. Su
    // mobile (anche superfici fisse RM trasparenti) questo supera il timeout GPU su
    // pannelli ad alta risoluzione (es. Galaxy Tab S9 Adreno) -> fault -> app killed.
    // Riduciamo ENTRAMBI i moltiplicatori; il ramo OPACO (una sola marchField, cappata
    // a u_raySteps) resta a piena qualita'. Android e' il caso peggiore osservato
    // (schermo grande + watchdog severo) -> piu' aggressivo; iOS un gradino sopra.
    // Desktop usa i valori pieni (8 facce, 2000 passi).
#if defined(Q_OS_ANDROID)
    templateSource.replace("%MAX_FACES%", "4");          // Android: 4 facce (era 8)
    templateSource.replace("%MAX_LAYER_STEPS%", "600");  // Android: tetto passi/strato (era 2000)
    templateSource.replace("%BISECT_ITERS%", "24");      // Android: bisezione piena
#elif defined(Q_OS_IOS)
    templateSource.replace("%MAX_FACES%", "4");           // iOS: 4 facce (era 8)
    // iOS: tetto passi/strato a 2000 (TEST). Ridurlo a 1000 NON ha eliminato il
    // glitch residuo in casi estremi su iPhone (non iPad), quindi quel tetto non
    // era il collo di bottiglia: i raggi del caso estremo escono prima di 1000.
    // Riportato a 2000 per isolare la vera causa altrove (probabilmente MAX_FACES
    // o il costo del campo map() con texture 3D + animazione insieme).
    templateSource.replace("%MAX_LAYER_STEPS%", "2000");
    templateSource.replace("%BISECT_ITERS%", "24");       // iOS: bisezione invariata
#else
    templateSource.replace("%MAX_FACES%", "8");
    templateSource.replace("%MAX_LAYER_STEPS%", "2000");
    templateSource.replace("%BISECT_ITERS%", "24");
#endif
    // Tetto del ciclo dei Ray Steps (marchField): il ciclo esce a u_raySteps,
    // e questo e' solo il massimo che il campo Steps accetta. Era 1000 fisso:
    // oltre, il valore scritto non avrebbe cambiato nulla.
    templateSource.replace("%MAX_RAY_STEPS%", QString::number(kRayStepsMax));

    // safePow (pow sicura per basi negative) vive in common.glsl, iniettato qui sotto.
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    // Libreria solver (solveKerrR, kerr*SDF, ...): serve agli script ray marching
    // che usano la direttiva Inner:= (es. ergosfera di Kerr). Le sue funzioni sono
    // self-contained (prendono M/a come parametri, non leggono ubuf), quindi
    // possono precedere la dichiarazione dell'UBO nel template senza problemi.
    QString implicitLib = loadShaderSource(":/shaders/implicit.glsl");

    QString finalSource = "#version 450\n\n" + commonCode + "\n"
                          + implicitLib + "\n" + templateSource;

    // Variabili iniettate, condivise tra superficie esterna e interna.
    QString injectedVars = "    float t = ubuf.u_time;\n"
                           "    float iTime = ubuf.u_time;\n"
                           "    float A = ubuf.u_mathParams.x;\n"
                           "    float B = ubuf.u_mathParams.y;\n"
                           "    float C = ubuf.u_mathParams.z;\n"
                           "    float s = ubuf.u_mathParams.w;\n"
                           "    float S = ubuf.u_mathParams.w;\n"
                           "    float D = ubuf.u_mathParams2.x;\n"
                           "    float E = ubuf.u_mathParams2.y;\n"
                           "    float F = ubuf.u_mathParams2.z;\n"
                           "    float x = pos.x; float y = pos.y; float z = pos.z;\n";

    // Default: nessuna seconda superficie. Lo stub ritorna 1e9 (mai colpito) e
    // il flag resta false, così il main() segue il cammino storico opaco.
    m_raymarchHasInner = false;
    QString innerExpr = "1e9";

    // --- INIZIO NUOVA LOGICA SCRIPT MULTI-RIGA ---
    if (engine->isScriptModeActive()) {
        QString customCode = engine->getScriptCodeGLSL();

        // --- DIRETTIVA //INNER: (seconda superficie opaca interna) ---
        // Espressione SDF del campo interno; il resto dello script è la
        // superficie esterna. È una DIRETTIVA-COMMENTO (inizia con //): cruciale
        // perché il filtro direttive di mainwindow SCARTA ogni riga con ":=",
        // quindi una "Inner:=" sparirebbe prima di arrivare qui. Con "//INNER:"
        // (niente :=) la riga sopravvive intatta nel glslCode e la estraiamo noi.
        // ANCORATA A INIZIO RIGA (^ + MultilineOption).
        QRegularExpression innerRe("^[ \\t]*//[ \\t]*INNER[ \\t]*:[ \\t]*([^\\n;]+);?",
                                   QRegularExpression::CaseInsensitiveOption
                                   | QRegularExpression::MultilineOption);
        QRegularExpressionMatch innerMatch = innerRe.match(customCode);
        if (innerMatch.hasMatch()) {
            innerExpr = innerMatch.captured(1).trimmed();
            customCode.remove(innerMatch.capturedStart(), innerMatch.capturedLength());
            m_raymarchHasInner = true;
        }

        // Creiamo una funzione indipendente con il codice custom (esterna).
        //
        // ALIAS 'p' = PUNTO 3D. Gli script ray marching esistenti usano 'p' per il
        // punto (length(p), p*scale, ...): era il NOME DEL PARAMETRO di questa
        // funzione, "float getCustomImplicit(vec3 p)", finche' il sotto-tab Cross
        // Section non l'ha rinominato in 'pos' per liberare 'p' come quarta
        // variabile scalare. Il rename ha lasciato gli script senza 'p':
        // "'p' : undeclared identifier", lo shader NON compila, resta in piedi
        // quello precedente e la superficie SPARISCE senza alcun errore visibile
        // (regressione su Blistered Gyroid, Breathing Sphere e ogni altro record
        // RM da script che nomina 'p').
        //
        // L'alias vive SOLO qui dentro, dove 'p' ha sempre significato "punto 3D".
        // Non collide con la 'p' scalare del Cross Section, che e' una locale di
        // rawField(): funzioni diverse, scope diversi.
        QString newFunction = "float getCustomImplicit(vec3 pos) {\n"
                              "    vec3 p = pos;\n" + injectedVars + customCode + "\n}\n\n";

        // 1. Dichiariamo la nostra funzione custom ESATTAMENTE prima di rawField,
        // che e' dove vive %IMPLICIT_EQ%. L'ancora era "float map(vec3 pos) {" finche'
        // il placeholder stava in map(); ora rawField() e' stata estratta SOPRA map()
        // (serve a map() per campionare il gradiente del campo nel guscio, e GLSL non
        // ammette ricorsione). Ancorarsi ancora a map() metterebbe la definizione DOPO
        // il suo uso -> "getCustomImplicit : no matching function", shader non compilato.
        finalSource.replace("float rawField(vec3 pos) {", newFunction + "float rawField(vec3 pos) {");

        // 2. Sostituiamo il placeholder in rawField() delegando il calcolo alla nostra funzione
        finalSource.replace("%IMPLICIT_EQ%", "getCustomImplicit(pos)");

    } else {
        // Modalità classica: UI LineEdit (Singola Riga)
        finalSource.replace("%IMPLICIT_EQ%", safeEqF);
    }
    // --- FINE NUOVA LOGICA ---

    // Iniettiamo l'espressione del campo interno (o lo stub 1e9). Le variabili
    // x,y,z,A,B,... sono già dichiarate nel corpo di mapInner() nel template.
    finalSource.replace("%INNER_MAP%", innerExpr);

    // 4a variabile (x,y,z,p) del sotto-tab Cross Section: dichiarata SOLO
    // quando l'equazione attiva e' quella a 4 variabili, cosi' il tab 3D non
    // vede mai 'p' nello scope (equazione a 3 variabili, come da contratto).
    //
    // ROTAZIONI 4D. La telecamera vive SEMPRE nel piano p=0 del riferimento
    // MONDO: ogni punto che il ray marcher visita e' (x, y, z, 0). La superficie
    // ha un suo riferimento (O, x', y', z', p'), inizialmente coincidente con
    // quello della camera; ruotarla in 4D significa che il piano di sezione,
    // LETTO NEL RIFERIMENTO DELLA SUPERFICIE, non e' piu' p'=0 ma un piano
    // obliquo -- ed e' per questo che la sezione cambia forma.
    //
    // Quindi qui si applica la rotazione INVERSA: si porta il punto del mondo
    // dentro il riferimento della superficie, e la si valuta li'. E' l'opposto
    // del ramo parametrico (surface.vert ~222), dove la superficie e' GENERATA
    // in 4D e i suoi punti si trasformano in avanti; la stessa rotazione, letta
    // dal lato opposto. Con omega=phi=psi=0 le due terne coincidono e p' resta
    // 0: il comportamento storico e' esattamente il caso particolare.
    //
    // L'inversa di R = RzW(psi)*RyW(phi)*RxW(omega) (l'ordine in cui il vertex
    // shader le applica al punto) e' RxW(-omega)*RyW(-phi)*RzW(-psi): ordine
    // rovesciato e angoli negati. Le matrici sono ortogonali, quindi l'inversa
    // e' anche la trasposta -- verificato numericamente.
    //
    // TRASLAZIONE LUNGO p (la quota del piano di sezione). Il punto che il
    // marcher visita e' (x, y, z, p0), dove p0 NON e' piu' fisso a 0 ma la
    // quarta coordinata dell'osservatore -- quella che i tasti P+/P- del dock 4D
    // muovono (ObsMovePPos/Neg -> m_observerPos.w, gia' caricata nell'UBO).
    //
    // E' la traslazione che da' senso alla feature: ruotando si cambia
    // l'INCLINAZIONE del piano di sezione, spostando p si cambia la sua QUOTA,
    // cioe' si scorre l'ipersuperficie da un capo all'altro vedendo la sezione
    // trasformarsi. Senza, si vede sempre e solo la fetta centrale.
    //
    // La traslazione si applica PRIMA della rotazione inversa: p0 e' misurato
    // nel riferimento MONDO (e' dove sta l'osservatore), e va portato dentro il
    // riferimento della superficie insieme a x, y, z.
    //
    // x, y, z dell'osservatore NON entrano qui: in 3D la posizione della
    // telecamera e' gia' governata dalla matrice di vista (m_cameraPos, il
    // rayPos del marcher), e sommarli una seconda volta sposterebbe la scena due
    // volte. La quarta coordinata e' l'unica che la matrice di vista non
    // rappresenta, ed e' l'unica che serve qui.
    //
    // La rotazione resta attorno all'ORIGINE: le due terne condividono O.
    static const QString kCrossSectionRot = QStringLiteral(
        "    vec4 q4 = vec4(pos, ubuf.u_dummyZero.z);\n"
        "    {\n"
        "        float co, si;\n"
        "        co = cos(-ubuf.u_psi);   si = sin(-ubuf.u_psi);\n"
        "        q4 = vec4(q4.x, q4.y, q4.z*co + q4.w*si, -q4.z*si + q4.w*co);\n"
        "        co = cos(-ubuf.u_phi);   si = sin(-ubuf.u_phi);\n"
        "        q4 = vec4(q4.x, q4.y*co + q4.w*si, q4.z, -q4.y*si + q4.w*co);\n"
        "        co = cos(-ubuf.u_omega); si = sin(-ubuf.u_omega);\n"
        "        q4 = vec4(q4.x*co + q4.w*si, q4.y, q4.z, -q4.x*si + q4.w*co);\n"
        "    }\n"
        "    x = q4.x; y = q4.y; z = q4.z;\n"
        "    float p = q4.w;\n");
    finalSource.replace("%CROSS_SECTION_P%",
                        m_implicitUsesCrossSection ? kCrossSectionRot : QString());

    if (m_textureEnabled) {
        QString texCodeRemapped = m_textureCode;
        texCodeRemapped.replace("ubuf.u_time", "ubuf.u_dummyZero.x");
        // COSTANTI A..F/S anche nel COLORE. Il template le dichiara come LOCALI
        // di map() (~31) e mapInner() (~75), ma questo codice finisce in
        // shadeSurface(), un'altra funzione: da li' non erano visibili e uno
        // script colore che citava 'A' NON COMPILAVA ("'A' : undeclared
        // identifier"), lasciando in piedi lo shader precedente. Il
        // DISPLACEMENT non aveva il problema perche' viene iniettato dentro
        // map(), dove le locali ci sono gia' -- l'asimmetria fra i due campi
        // del modulo texture RM era solo un effetto del punto di iniezione.
        // Si dichiarano QUI, nello scope del wrapper, come si fa gia' per t e
        // iTime: nessun simbolo globale, nessuna #define, e uno script che
        // dichiarasse una propria 'float A' la ombreggerebbe in uno scope
        // interno senza conflitto (oggi quello script non compila affatto).
        QString texWrap = "{\n  float t = ubuf.u_dummyZero.x;\n  float iTime = ubuf.u_dummyZero.x;\n"
                          "  float A = ubuf.u_mathParams.x;\n"
                          "  float B = ubuf.u_mathParams.y;\n"
                          "  float C = ubuf.u_mathParams.z;\n"
                          "  float S = ubuf.u_mathParams.w;\n"
                          "  float s = ubuf.u_mathParams.w;\n"
                          "  float D = ubuf.u_mathParams2.x;\n"
                          "  float E = ubuf.u_mathParams2.y;\n"
                          "  float F = ubuf.u_mathParams2.z;\n"
                          + texCodeRemapped + "\n}\n";
        finalSource.replace("%TEXTURE_CODE%", texWrap);

        // Il displacement appartiene al MODULO TEXTURE: il suo 't' deve leggere
        // l'orologio texture (dummyZero.x), non quello geometria (u_time del map()).
        // Lo avvolgiamo in uno scope che fa SHADOW del 't' esterno, come il colore
        // texture qui sopra. Senza questo, un 't' nudo nel displacement leggeva il
        // 'float t = ubuf.u_time' del template (orologio geometria), creando
        // l'interazione incrociata col dock Equations. Il blocco { } ridichiara
        // solo 't'/'iTime' (shadow): d_surf, pModel, p restano quelli esterni e
        // le assegnazioni del displacement (d_surf -= ...) li modificano davvero.
        QString dispRemapped = m_displacementCode;
        dispRemapped.replace("ubuf.u_time", "ubuf.u_dummyZero.x");   // STESSO orologio della texture
        QString dispWrap = "{\n  float t = ubuf.u_dummyZero.x;\n  float iTime = ubuf.u_dummyZero.x;\n"
                           + dispRemapped + "\n}\n";
        finalSource.replace("%DISPLACEMENT_CODE%", dispWrap);
    } else {
        finalSource.replace("%TEXTURE_CODE%", "");
        finalSource.replace("%DISPLACEMENT_CODE%", "");
    }

    return finalSource;
}

QString GLWidget::createBackgroundFragmentShader(bool isTextureMode, const QString &customCode)
{
    QString fsSource;
    QString header = "#version 450\n";

    QString uboBlock = "layout(std140, binding=0) uniform SceneUBO {\n"
                       "    mat4 u_mvpMatrix; mat4 u_mvMatrix; mat4 u_mMatrix;\n"
                       "    vec4 u_dummyZero; vec4 u_observerPos; vec4 u_cameraPos4D; vec4 u_mathParams; vec4 u_mathParams2;\n"
                       "    vec3 color; float alpha;\n"
                       "    vec3 u_col1; float u_lightIntensity;\n"
                       "    vec3 u_col2; float u_zoom;\n"
                       "    vec2 u_center; float u_rotation; float u_omega;\n"
                       "    float u_phi; float u_psi; float u_time; int u_projMode;\n"
                       "    int u_lightingMode; int u_renderMode; int u_isFlat; int useTexture;\n"
                       "    int useSpecular; float u_min; float u_max; float v_min;\n"
                       "    float v_max; int u_hasExplicitW; float x_min; float x_max;\n"
                       "    float y_min; float y_max; float z_min; float z_max;\n"
                       "} ubuf;\n";

    // COORDINATA DI BASE DELLO SFONDO, unica per i rami che la usano: tutti
    // partivano da v_texCoord (posizione sullo SCHERMO), e questa funzione la
    // sostituisce con la DIREZIONE di vista quando lo sfondo e' solidale con la
    // scena. Zoom/pan/rotazione 2D si applicano DOPO, come sempre: agiscono
    // sull'immagine, che poi viene avvolta sul cielo.
    //
    // La matrice e' in u_mvpMatrix di questa copia dell'UBO (vedi render()): il
    // 3x3 porta il pixel (ndc.x, ndc.y, 1) alla direzione nel mondo, [3][3] e' la
    // modalita' (GLWidget::BgSkyMode) e [3][1] le proporzioni dell'immagine.
    // La forma decide solo come le DIREZIONI diventano coordinate d'immagine:
    //   SFERA    -- equirettangolare: longitudine su u, latitudine su v. Il
    //               formato dei panorami 360 (2:1); stirata ai poli.
    //   CILINDRO -- stessa longitudine su u; su v l'altezza su un cilindro di
    //               raggio 1 (tan della latitudine), scalata con le proporzioni
    //               dell'immagine: l'intera larghezza copre il giro (2 pi), quindi
    //               l'altezza vale 2 pi / (L/A) e l'immagine non si deforma.
    //               Aperto sopra e sotto: oltre la fascia il campionatore
    //               (Repeat) ripete l'immagine verso il punto di fuga.
    //   CUBO     -- la faccia e' l'asse dominante della direzione, e sulla faccia
    //               la proiezione e' piatta (gnomonica): nessuna deformazione. La
    //               stessa immagine su tutte e sei, DRITTA sulle quattro laterali
    //               (u segue la destra dello schermo guardando quella faccia, v
    //               l'alto). Sopra e sotto u resta +X e v segue il verso in cui
    //               l'alto dello schermo punta alzando/abbassando lo sguardo dal
    //               fronte: attraversato lo spigolo l'immagine continua come una
    //               piastrella, invece di ribaltarsi.
    // VERSO: il quad ha v_texCoord.y = 1 in ALTO (e' li' che l'immagine fissa
    // appare dritta), quindi l'alto dello schermo e l'alto del cielo (+Y del
    // mondo) si mappano entrambi su v = 1. Ricavato da cio' che si vede, non dal
    // backend: se lo sfondo fisso e' dritto, lo e' anche il cielo.
    const QString bgBaseUVFunc =
        "vec2 bgBaseUV() {\n"
        "    float _mode = ubuf.u_mvpMatrix[3][3];\n"
        "    if (_mode < 0.5) return v_texCoord;\n"
        "    vec2 _ndc = v_texCoord * 2.0 - 1.0;\n"
        "    vec3 _d = normalize(mat3(ubuf.u_mvpMatrix) * vec3(_ndc, 1.0));\n"
        "    if (_mode > 2.5) {\n"
        "        vec3 _a = abs(_d);\n"
        "        vec2 _f;\n"
        "        if (_a.z >= _a.x && _a.z >= _a.y) _f = vec2(_d.z < 0.0 ? _d.x : -_d.x, _d.y) / _a.z;\n"
        "        else if (_a.x >= _a.y)            _f = vec2(_d.x > 0.0 ? _d.z : -_d.z, _d.y) / _a.x;\n"
        "        else                              _f = vec2(_d.x, _d.y > 0.0 ? _d.z : -_d.z) / _a.y;\n"
        "        return _f * 0.5 + 0.5;\n"
        "    }\n"
        "    float _u = atan(_d.x, -_d.z) * 0.15915494 + 0.5;\n"
        "    if (_mode > 1.5) {\n"
        "        float _h = _d.y / max(length(_d.xz), 1e-4);\n"
        "        return vec2(_u, _h * ubuf.u_mvpMatrix[3][1] * 0.15915494 + 0.5);\n"
        "    }\n"
        "    return vec2(_u, asin(clamp(_d.y, -1.0, 1.0)) * 0.31830989 + 0.5);\n"
        "}\n";

    if (!customCode.isEmpty() && customCode.contains("#version")) {
        fsSource = customCode;
    }
    else if (isTextureMode) {
        fsSource = header +
                   "layout(binding=1) uniform sampler2D tex;\n"
                   + uboBlock +
                   "layout(location=0) in vec2 v_texCoord;\n"
                   "layout(location=0) out vec4 fragColor;\n"
                   + bgBaseUVFunc +
                   "void main() {\n"
                   "  float rad = radians(ubuf.u_rotation);\n"
                   "  float c = cos(rad); float s = sin(rad);\n"
                   "  vec2 centered = bgBaseUV() - 0.5;\n"
                   "  vec2 rot = vec2(centered.x * c - centered.y * s, centered.x * s + centered.y * c) + 0.5;\n"
                   "  float scale = 1.0 / ubuf.u_zoom;\n"
                   "  vec2 shift = ubuf.u_center * 0.5;\n"
                   "  vec2 uv = (rot - 0.5) * scale + 0.5 + shift;\n"
                   "  fragColor = texture(tex, uv);\n"
                   "}\n";
    } else {
        QString commonCode = loadShaderSource(":/shaders/common.glsl");

        QString safeCode = customCode;
        safeCode.remove(QRegularExpression("#ifdef GL_ES[\\s\\S]*?#endif"));
        safeCode.remove(QRegularExpression("precision\\s+(highp|mediump|lowp)\\s+float\\s*;"));
        safeCode.remove(QRegularExpression("//SOUND_BEGIN.*?//SOUND_END", QRegularExpression::DotMatchesEverythingOption));

        // FIX: Rinominiamo gl_FragColor per non collidere con i parametri di Shadertoy!
        safeCode.replace("gl_FragColor", "out_FragColor");

        QString helpers = "    float _rad = radians(ubuf.u_rotation);\n"
                          "    float _c = cos(_rad); float _s = sin(_rad);\n"
                          "    vec2 _centered = in_uv - 0.5;\n"
                          "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                          "    float _scale = 1.0 / ubuf.u_zoom;\n"
                          "    vec2 _shift = ubuf.u_center * 0.5;\n"
                          "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n"
                          "    bool u_isFlat = (ubuf.u_isFlat != 0);\n";

        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+t\\b"))) helpers += "    float t = ubuf.u_time;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+A\\b"))) helpers += "    float A = ubuf.u_mathParams.x;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+B\\b"))) helpers += "    float B = ubuf.u_mathParams.y;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+C\\b"))) helpers += "    float C = ubuf.u_mathParams.z;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+S\\b"))) helpers += "    float S = ubuf.u_mathParams.w;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+s\\b"))) helpers += "    float s = ubuf.u_mathParams.w;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+D\\b"))) helpers += "    float D = ubuf.u_mathParams2.x;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+E\\b"))) helpers += "    float E = ubuf.u_mathParams2.y;\n";
        if (!safeCode.contains(QRegularExpression("\\bfloat\\s+F\\b"))) helpers += "    float F = ubuf.u_mathParams2.z;\n";
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
        if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

        QString dynamicBody;

        if (safeCode.contains("mainImage")) {
            // Molti shader Shadertoy ignorano il parametro 'fragCoord' di mainImage e
            // leggono direttamente gl_FragCoord (posizione del pixel sullo schermo).
            // gl_FragCoord scavalca la nostra trasformazione (zoom/pan/rotazione di
            // u_center/u_zoom/u_rotation), così la texture di SFONDO non rispondeva ai
            // comandi (a differenza della stessa texture su superficie, dove il fix
            // c'e' gia' in createFragmentShaderSource). Lo rimappiamo su una globale
            // _st_fragCoord impostata al fragCoord TRASFORMATO prima di mainImage.
            safeCode.replace(QRegularExpression("\\bgl_FragCoord\\b"), "_st_fragCoord");

            // FIX: Vere variabili globali invece di macro!
            QString stHelpers = "vec3 iResolution;\n"
                                "float iTime;\n"
                                "float iTimeDelta;\n"
                                "int iFrame;\n"
                                "vec4 iMouse;\n"
                                "vec4 iDate;\n"
                                "vec4 _st_fragCoord;\n"
                                "#define iChannel0 tex\n"
                                "#define iChannel1 tex\n"
                                "#define iChannel2 tex\n"
                                "#define iChannel3 tex\n";

            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) stHelpers += "vec3 u_col1;\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) stHelpers += "vec3 u_col2;\n";

            // Le inizializziamo dinamicamente all'interno della funzione
            QString initVars = "    iResolution = vec3(1024.0, 1024.0, 1.0);\n"
                               "    iTime = ubuf.u_time;\n"
                               "    iTimeDelta = 0.016;\n"
                               "    iFrame = int(ubuf.u_time * 60.0);\n"
                               "    iMouse = vec4(0.0);\n"
                               "    iDate = vec4(0.0);\n";

            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) initVars += "    u_col1 = ubuf.u_col1;\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) initVars += "    u_col2 = ubuf.u_col2;\n";

            // Le helpers locali ridichiarano u_col1/u_col2: rimuovile per non fare
            // shadowing delle globali assegnate in initVars (vedi stesso accorgimento
            // in createFragmentShaderSource), altrimenti gli slider colore non agiscono.
            QString stLocalHelpers = helpers;
            stLocalHelpers.remove("    vec3 u_col1 = ubuf.u_col1;\n");
            stLocalHelpers.remove("    vec3 u_col2 = ubuf.u_col2;\n");

            dynamicBody = stHelpers + safeCode + "\n"
                                                 "vec3 getCustomColor(vec2 in_uv) {\n"
                          + stLocalHelpers + initVars +
                          "    vec2 _st_coord = uv * iResolution.xy;\n"
                          "    _st_fragCoord = vec4(_st_coord, 0.0, 1.0);\n"
                          "    vec4 fragColor_out;\n"
                          "    mainImage(fragColor_out, _st_coord);\n"
                          "    return fragColor_out.rgb;\n"
                          "}\n"
                          "void main() {\n"
                          "  out_FragColor = vec4(getCustomColor(bgBaseUV()), ubuf.alpha);\n"
                          "}\n";
        }
        else if (safeCode.contains("void main()")) {
            QString extHelpers = "#define iResolution vec3(1.0, 1.0, 1.0)\n#define iTime ubuf.u_time\n";
            extHelpers += "#define A ubuf.u_mathParams.x\n#define B ubuf.u_mathParams.y\n#define C ubuf.u_mathParams.z\n#define S ubuf.u_mathParams.w\n";
            extHelpers += "#define D ubuf.u_mathParams2.x\n#define E ubuf.u_mathParams2.y\n#define F ubuf.u_mathParams2.z\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
            dynamicBody = extHelpers + safeCode + "\n";
        }
        else if (safeCode.contains("getCustomColor")) {
            // Lo snippet definisce GIA' la propria getCustomColor(vec2 in_uv) — e' il
            // formato dei preset texture parametrica/superficie. Senza questo ramo
            // ricadeva nel fallback che lo ri-avvolgeva in un'altra getCustomColor,
            // producendo una funzione annidata (illegale in GLSL) -> "unexpected
            // LEFT_BRACE". Lo iniettiamo cosi' com'e' (come fa createFragmentShaderSource),
            // mappando iTime/u_col1/u_col2, e gli forniamo noi la void main() perche'
            // nel background non preesiste.
            QString extHelpers = "#define iResolution vec3(1.0, 1.0, 1.0)\n#define iTime ubuf.u_time\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
            if (!safeCode.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";

            // TRASFORMAZIONE 2D anche qui. Iniettando lo script tale e quale la
            // funzione riceveva v_texCoord GREZZO: zoom, pan e rotazione del
            // mouse non toccavano la texture di SFONDO (caso "Animated
            // Gradient"). E' lo stesso difetto gia' corretto per la texture di
            // SUPERFICIE in buildTextureFunction, dove il rimedio e' identico:
            // si rinomina la funzione dell'utente e le si mette davanti un
            // wrapper col nome atteso, che trasforma le coordinate e la chiama.
            // Il corpo del codice utente non si tocca.
            //
            // Le due trappole di quel fix valgono anche qui: NON passare
            // `helpers` intero al wrapper (dichiara le locali u_col1/u_col2, che
            // in questo ramo sono #define -> "unexpected DOT"), e ordine
            // obbligato -- funzione utente PRIMA, wrapper dopo, perche' in GLSL
            // non c'e' hoisting.
            safeCode.replace(QRegularExpression("\\bgetCustomColor\\b"), "getCustomColor_user");

            const QString uvTransform =
                "    float _rad = radians(ubuf.u_rotation);\n"
                "    float _c = cos(_rad); float _s = sin(_rad);\n"
                "    vec2 _centered = in_uv - 0.5;\n"
                "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                "    float _scale = 1.0 / ubuf.u_zoom;\n"
                "    vec2 _shift = ubuf.u_center * 0.5;\n"
                "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n";

            dynamicBody = extHelpers + safeCode + "\n"
                          "vec3 getCustomColor(vec2 in_uv) {\n"
                          + uvTransform +
                          "    return getCustomColor_user(uv);\n"
                          "}\n"
                          "void main() {\n"
                          "  out_FragColor = vec4(getCustomColor(bgBaseUV()), ubuf.alpha);\n"
                          "}\n";
        }
        else {
            if (!safeCode.contains("return")) {
                safeCode += "\n    return vec3(u, v, 0.2);\n";
            }
            dynamicBody = "vec3 getCustomColor(vec2 in_uv) {\n"
                          + helpers +
                          "    float u = uv.x;\n"
                          "    float v = uv.y;\n"
                          + safeCode + "\n"
                                       "}\n"
                                       "void main() {\n"
                                       "  out_FragColor = vec4(getCustomColor(bgBaseUV()), ubuf.alpha);\n"
                                       "}\n";
        }

        // OUTPUT GLOBALE RINOMINATO A "out_FragColor"
        // bgBaseUV subito dopo l'UBO (le serve ubuf) e prima del codice utente.
        // Il ramo con la "void main()" dell'utente non la chiama: quello script
        // legge v_texCoord da se', e resta fisso anche in modalita' solidale.
        fsSource = "#version 450\n"
                   "layout(location=0) in vec2 v_texCoord;\n"
                   "layout(location=0) out vec4 out_FragColor;\n"
                   "layout(binding=1) uniform sampler2D tex;\n"
                   + uboBlock + "\n" + bgBaseUVFunc + "\n" + commonCode + "\n" + dynamicBody;
    }

    return fsSource;
}


// ==========================================================
// PRIVATE HELPER METHODS
// ==========================================================

// --- Geometry & Mesh Builders ---

void GLWidget::buildWireframeGeometry() {
    // Zero indici è legittimo quando l'engine non ha ancora vertici (reset/init/
    // flat-view): la mesh arriva subito dopo e il wireframe si rigenera. Nessun
    // warning: era una diagnostica residua di un vecchio bug, ormai solo rumore.
    m_wireframeIndices = GeometryBuilder::buildWireframe(engine.get(), wfStepU, wfStepV,
                                                         &m_wireframeRanges,
                                                         m_meshAppearanceUniform);
    m_wireframeIndexCount = m_wireframeIndices.size();
    wireframeNeedsUpdate = true;
}

 // --- Shader Generation & Compilation ---

// La riga che accende il codice del bordo nel vertex (createVertexShaderSource)
// e che buildPipeline toglie per ricavarne il sorgente del compute dei lati
// coperti. Lo stesso testo nei due punti, a capo iniziale compreso: togliendo
// solo "#define HAS_BORDER\n" restava un a capo in piu', il compute risultava
// diverso da quello a bordo spento e si ricompilava a ogni accensione (con la
// sonda del bordo ripartita da capo).
static const QLatin1String kBorderDefineLine("\n#define HAS_BORDER\n");

QString GLWidget::createVertexShaderSource(const QString &xEq, const QString &yEq, const QString &zEq, const QString &wEq)
{
    // 1. Carica il template
    QString vertexTemplate = loadShaderSource(":/shaders/surface.vert");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");
    // Libreria di solver per variabili implicite (Kerr, Kruskal, ...): la stessa
    // iniettata nel flusso geodetico. Resa disponibile anche alle superfici
    // PARAMETRICHE così i campi x/y/z possono chiamare kerrRadius/kerrEmbedZ ecc.
    // (es. imbuto equatoriale di Kerr come superficie di rivoluzione).
    QString implicitCode = loadShaderSource(":/shaders/implicit.glsl");

    // 2. RIMUOVE QUALSIASI INTESTAZIONE ESISTENTE
    QRegularExpression headerCleanup("^\\s*(#version|precision).*\n", QRegularExpression::MultilineOption);
    vertexTemplate.remove(headerCleanup);
    vertexTemplate.remove(QRegularExpression("^\\s*#ifdef GL_ES[\\s\\S]*?#endif", QRegularExpression::MultilineOption));

    QString header = "#version 450\n";

    // safePow (pow sicura per basi negative) è definita in common.glsl, iniettato qui
    // sotto prima del template: il traduttore riscrive pow(->safePow( nelle equazioni.
    QString source = header + "\n" + commonCode + "\n" + implicitCode + "\n" + vertexTemplate;

    auto sanitizeEq = [](const QString &s) {
        // 1. Traduce potenze e costanti (pi, e)
        QString translated = GlslTranslator::translateEquation(s);

        // 2. Wrap di sicurezza finale (Niente più "replace" manuali del tempo!)
        if (translated.trimmed().isEmpty()) return QString("0.0");
        return "float(" + translated + ")";
    };

    // --- SOSTITUZIONE LOGICA ---
    // Mai sulla mesh custom (stessa guardia di IS_TUBE piu' sotto): li'
    // getRawPosition restituisce il vertice del buffer, e sostituirla con la
    // curva faceva sparire la superficie.
    if (engine->isTubeModeActive() && !engine->isScriptModeActive() && !m_isCustomMesh) {
        // TUBO attorno alla curva C(u) = (X, Y, Z, P), sotto-tab Tubes: dalle
        // equazioni o da uno script (tubeCurveFunction). Qui getRawPosition
        // restituisce il solo punto della CURVA; il tubo lo costruisce il
        // template (emitTubeVertex, con IS_TUBE) nello spazio 3D finale, dopo
        // rotazioni 4D e proiezione, cosi' la sezione resta rotonda anche
        // quando la curva ruota in 4D. Una sola chiamata a tubeCurve nel
        // sorgente: gli shader parametrici enormi fanno cadere il compilatore
        // Mali.
        const QString tubeFunction =
            tubeCurveFunction(engine->tubeX(), engine->tubeY(), engine->tubeZ(), engine->tubeP(),
                              engine->tubeScriptBody()) +
            "vec4 getRawPosition(float u, float v, float w) {\n"
            "    return sanitizePos(tubeCurve(u, 0.0, 0.0));\n"
            "}";
        QRegularExpression regEx("vec4 getRawPosition\\(float u, float v, float w\\)\\s*\\{[\\s\\S]*?\\}");
        // Sostituzione per posizione, non con replace(regex, testo): il testo
        // della curva viene da uno script e un "\\1" li' sarebbe preso per un
        // rimando alla regex.
        const QRegularExpressionMatch rawMatch = regEx.match(source);
        if (rawMatch.hasMatch())
            source.replace(rawMatch.capturedStart(), rawMatch.capturedLength(), tubeFunction);
        source.replace("%X_EQ%", "0.0");
        source.replace("%Y_EQ%", "0.0");
        source.replace("%Z_EQ%", "0.0");
        source.replace("%W_EQ%", "0.0");

    } else if (engine->isScriptModeActive()) {
        QString customCode = engine->getScriptCodeGLSL();
        if (customCode.trimmed().isEmpty()) customCode = "return vec4(0.0, 0.0, 0.0, 0.0);";

        // Pulizia interi anche nello script custom
        QRegularExpression regInt("(?<=[^a-zA-Z0-9_.]|^)([0-9]+)(?=[^a-zA-Z0-9_.]|$)");
        customCode.replace(regInt, "\\1.0");

        QRegularExpression regEx("vec4 getRawPosition\\(float u, float v, float w\\)\\s*\\{[\\s\\S]*?\\}");

        // INIEZIONE VARIABILI DI SUPPORTO
        QString injectedVars = generateGlslHelperVars(customCode);

        QString newFunction = "vec4 getRawPosition(float u, float v, float w) {\n" +
                              injectedVars +
                              customCode +
                              "\n}";

        source.replace(regEx, newFunction);

        // Pulisci placeholder non usati
        source.replace("%X_EQ%", "0.0");
        source.replace("%Y_EQ%", "0.0");
        source.replace("%Z_EQ%", "0.0");
        source.replace("%W_EQ%", "0.0");

    } else {
        // Parametric Mode: Applica sanitizzazione a ogni equazione
        source.replace("%X_EQ%", sanitizeEq(xEq));
        source.replace("%Y_EQ%", sanitizeEq(yEq));
        source.replace("%Z_EQ%", sanitizeEq(zEq));
        source.replace("%W_EQ%", sanitizeEq(wEq));
    }

    // --- GESTIONE VINCOLI ---
    SurfaceEngine::ConstraintMode mode = engine->getConstraintMode();
    QString explicitEq = engine->getActiveExplicitEquation();
    if (explicitEq.trimmed().isEmpty()) explicitEq = "0.0";

    // Applica sanitize anche al vincolo
    QString explicitEqSafe = sanitizeEq(explicitEq);

    // PARAMETRI SOLO MAIUSCOLI (a e b minuscoli sotto rimangono perché sono le coordinate U,V della funzione)
    QString params = "float A=ubuf.u_mathParams.x; "
                     "float B=ubuf.u_mathParams.y; "
                     "float C=ubuf.u_mathParams.z; "
                     "float S=ubuf.u_mathParams.w; float s=S; "
                     "float D=ubuf.u_mathParams2.x; "
                     "float E=ubuf.u_mathParams2.y; "
                     "float F=ubuf.u_mathParams2.z; "
                     "float t=ubuf.u_time; ";

    QString explicitBody;
    if (mode == SurfaceEngine::ConstraintU) {
        explicitBody = params + "float v=a; float w=b; return (" + explicitEqSafe + ");";
    } else if (mode == SurfaceEngine::ConstraintV) {
        explicitBody = params + "float u=a; float w=b; return (" + explicitEqSafe + ");";
    } else {
        explicitBody = params + "float u=a; float v=b; return (" + explicitEqSafe + ");";
    }

    source.replace("%EXPLICIT_BODY%", explicitBody);
    source.replace("%CONSTRAINT_MODE%", QString::number((int)mode));

    if (m_isCustomMesh) {
        source.replace("#version 450", "#version 450\n#define CUSTOM_MESH\n");
    }

    // Bordo (Border Thickness > 0): il ramo che costruisce i tubi sui lati del
    // dominio (emitBorderVertex) esiste solo con HAS_BORDER, come il CUTOUT:
    // una superficie senza bordo compila lo shader di sempre. Sulla mesh
    // custom i vertici sono posizioni, non parametri: i punti dei lati li
    // legge dalla texture dei lati (borderSupported).
    if (m_borderRadius > 0.0f && borderSupported()) {
        source.replace(QStringLiteral("#version 450"), QStringLiteral("#version 450") + kBorderDefineLine);
    }
    // Tubo: il template lo costruisce nella scena (emitTubeVertex).
    if (engine->isTubeModeActive() && !engine->isScriptModeActive() && !m_isCustomMesh) {
        source.replace("#version 450", "#version 450\n#define IS_TUBE\n");
    }

    return source;
}

// Corpo di UNA funzione texture procedurale, col nome che le viene dato.
// Estratta da createFragmentShaderSource per poterla generare piu' volte nello
// stesso shader (una per mesh con texture propria, vedi il dispatcher li'):
// duplicare i tre rami di iniezione (Shadertoy / getCustomColor esterno / corpo
// semplice) avrebbe creato due copie destinate a divergere, che in questo
// progetto e' la famiglia di bug piu' ricorrente.
// funcName e' il nome da generare; con "getCustomColor" il risultato e'
// byte-identico a quello che la funzione produceva prima di questa estrazione.
QString GLWidget::buildTextureFunction(const QString &customLogic, const QString &funcName)
{
    QString safeLogic = customLogic;
    safeLogic.remove(QRegularExpression("//SOUND_BEGIN.*?//SOUND_END", QRegularExpression::DotMatchesEverythingOption));
    safeLogic.remove(QRegularExpression("#ifdef GL_ES[\\s\\S]*?#endif"));
    safeLogic.remove(QRegularExpression("precision\\s+(highp|mediump|lowp)\\s+float\\s*;"));
    safeLogic.replace("gl_FragColor", "fragColor");

    QString helpers = "    float _rad = radians(ubuf.u_rotation);\n"
                      "    float _c = cos(_rad); float _s = sin(_rad);\n"
                      "    vec2 _centered = in_uv - 0.5;\n"
                      "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
                      "    float _scale = 1.0 / ubuf.u_zoom;\n"
                      "    vec2 _shift = ubuf.u_center * 0.5;\n"
                      "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n"
                      "    bool u_isFlat = (ubuf.u_isFlat != 0);\n";

    helpers += generateGlslHelperVars(safeLogic);
    helpers.replace("ubuf.u_time", "ubuf.u_dummyZero.x");

    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) helpers += "    vec3 u_col1 = ubuf.u_col1;\n";
    if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) helpers += "    vec3 u_col2 = ubuf.u_col2;\n";

    QString codeToInject;

    // NB: la dichiarazione del sampler NON sta qui. E' una uniform di modulo e
    // va emessa UNA volta sola: generandola per funzione, uno shader con piu'
    // texture per-mesh la ridichiarerebbe N volte ("redefinition"). La emette
    // createFragmentShaderSource prima di tutte le funzioni.
    if (customLogic.isEmpty()) {
        // _st_sampleTex e non texture(tex, uv): senza immagine caricata questo
        // ramo campionava la texture tappabuchi, cioe' un colore piatto. Ora
        // cade sulla scacchiera procedurale mix(u_col1,u_col2), coerente col
        // ramo Shadertoy e con gli slider Color 1/2 che la governano.
        codeToInject = "vec3 " + funcName + "(vec2 in_uv) {\n" +
                       helpers +
                       "\n    return _st_sampleTex(uv).rgb;\n}";
    }
    else if (safeLogic.contains("mainImage")) {
        // Supporto Shadertoy

        // Molti shader Shadertoy ignorano il parametro 'fragCoord' di mainImage e
        // leggono direttamente gl_FragCoord (la posizione del pixel sullo schermo).
        // gl_FragCoord scavalca la nostra trasformazione flat (zoom/pan/rotazione),
        // così la texture in vista 2D non rispondeva al mouse. La rimappiamo su una
        // variabile globale che impostiamo al fragCoord trasformato prima di mainImage.
        safeLogic.replace(QRegularExpression("\\bgl_FragCoord\\b"), "_st_fragCoord");

        // FALLBACK "NESSUNA IMMAGINE": le chiamate texture(iChannelN, uv[, bias])
        // passano da _st_sampleTex, che senza immagine caricata restituisce una
        // scacchiera mix(u_col1,u_col2) invece dei pixel della texture tappabuchi
        // (che darebbero una mesh piatta e monocroma). Si riscrive la CHIAMATA,
        // come qui sopra per gl_FragCoord, e non si tocca il #define iChannelN:
        // una "#define texture(...)" di modulo verrebbe raccolta dal rinominatore
        // per-mesh piu' sotto (reDef legge OGNI #define) e finirebbe rinominata,
        // rompendo lo shader. Il terzo argomento (bias di mip, usato da "Squished
        // Coordinates") sopravvive alla sostituzione, quindi _st_sampleTex e'
        // OVERLOADED anche a 2 parametri: senza, quella chiamata non compilerebbe.
        safeLogic.replace(
            QRegularExpression(R"(\btexture\s*\(\s*iChannel[0-3]\s*,)"),
            "_st_sampleTex(");

        QString stHelpers = "vec3 iResolution;\n"
                            "float iTime;\n"
                            "float iTimeDelta;\n"
                            "int iFrame;\n"
                            "vec4 iMouse;\n"
                            "vec4 iDate;\n"
                            "vec4 _st_fragCoord;\n"
                            "#define iChannel0 tex\n"
                            "#define iChannel1 tex\n"
                            "#define iChannel2 tex\n"
                            "#define iChannel3 tex\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) stHelpers += "vec3 u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) stHelpers += "vec3 u_col2;\n";

        QString initVars = "    iResolution = vec3(1024.0, 1024.0, 1.0);\n"
                           "    iTime = ubuf.u_dummyZero.x;\n"               // <- SOLO l'orologio della texture!
                           "    iTimeDelta = 0.016;\n"
                           "    iFrame = int(ubuf.u_dummyZero.x * 60.0);\n"  // <- Anche iFrame usa l'orologio texture
                           "    iMouse = vec4(0.0);\n"
                           "    iDate = vec4(0.0);\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) initVars += "    u_col1 = ubuf.u_col1;\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) initVars += "    u_col2 = ubuf.u_col2;\n";

        // Nel ramo Shadertoy u_col1/u_col2 sono GLOBALI (lette da mainImage) e
        // vengono assegnate in initVars. Le dichiarazioni LOCALI aggiunte da
        // `helpers` farebbero shadowing: mainImage leggerebbe le globali rimaste
        // a vec3(0) e gli slider colore non avrebbero effetto. Le rimuoviamo qui.
        QString stLocalHelpers = helpers;
        stLocalHelpers.remove("    vec3 u_col1 = ubuf.u_col1;\n");
        stLocalHelpers.remove("    vec3 u_col2 = ubuf.u_col2;\n");

        QString stBody = stHelpers + safeLogic + "\n"
                         "vec3 " + funcName + "(vec2 in_uv) {\n"
                         + stLocalHelpers + initVars +
                         "    vec2 _st_coord = uv * iResolution.xy;\n"
                         "    _st_fragCoord = vec4(_st_coord, 0.0, 1.0);\n"
                         "    vec4 fragColor_out;\n"
                         "    mainImage(fragColor_out, _st_coord);\n"
                         "    return fragColor_out.rgb;\n"
                         "}\n";

        // NOMI DI MODULO RESI UNICI PER FUNZIONE.
        // Questo ramo dichiara a livello GLOBALE iResolution, iTime, iFrame,
        // mainImage, _st_fragCoord...: con due mesh Shadertoy nello stesso
        // shader quei simboli collidono ("redefinition") e la compilazione
        // fallisce -> resta in piedi lo shader precedente, cioe' la texture
        // nuova non compare e la vecchia sembra "congelarsi".
        // Con funcName di default (texture globale) NON si rinomina nulla: il
        // sorgente resta byte-identico a prima.
        if (funcName != QLatin1String("getCustomColor")) {
            // funcName e' "getCustomColor_<k>": il suffisso e' gia' "_<k>".
            // NB: non anteporre un altro '_', o si otterrebbe "__<k>": i doppi
            // underscore sono RISERVATI in GLSL e il compilatore puo' rifiutarli.
            const QString sfx = QString(funcName).remove("getCustomColor");
            static const char *kSyms[] = {
                "iResolution", "iTime", "iTimeDelta", "iFrame", "iMouse",
                "iDate", "_st_fragCoord", "mainImage", "u_col1", "u_col2"
            };
            for (const char *s : kSyms) {
                // (?<!\.) esclude i CAMPI DELL'UBO: "ubuf.u_col1" e' un membro
                // del blocco uniform e rinominarlo dà "no such field in
                // structure 'ubuf'". Va rinominata solo la GLOBALE omonima che
                // questo ramo dichiara, non l'accesso qualificato che la
                // inizializza.
                stBody.replace(QRegularExpression(QString("(?<!\\.)\\b%1\\b").arg(s)),
                               QString(s) + sfx);
            }
            // Le #define iChannelN puntano al sampler `tex`, che e' unico e NON
            // va rinominato: il replace sopra le ha gia' lasciate intatte
            // (iChannel0..3 non sono nell'elenco), ma i loro nomi vanno resi
            // unici come gli altri, o la seconda definizione collide.
            for (int c = 0; c < 4; ++c) {
                const QString ch = QString("iChannel%1").arg(c);
                stBody.replace(QRegularExpression("\\b" + ch + "\\b"), ch + sfx);
            }

            // SIMBOLI DICHIARATI DALLO SCRIPT (#define e funzioni).
            // Finora si rinominavano solo i simboli NOTI (iTime, mainImage...),
            // ma uno script Shadertoy dichiara anche i propri: due mesh con lo
            // STESSO script davano "'#define' : Macro redefined" o la
            // ridefinizione di una funzione -> lo shader non compilava,
            // rebuildShader lasciava in piedi il precedente e **la superficie
            // spariva**. Stesso effetto fra script DIVERSI che condividono un
            // nome, e fra i preset in libreria ce ne sono gia' parecchi
            // (map, palette, rot, noise, wave, calcNormal...).
            // Si raccolgono i nomi DICHIARATI e si rinominano ovunque: una
            // sostituzione cieca su nomi corti (N, R, g...) colpirebbe anche
            // parole altrui, quindi si parte sempre dalla DICHIARAZIONE.
            QSet<QString> declared;
            // #define NOME  /  #define NOME(args)
            QRegularExpression reDef(R"(^[ \t]*#define[ \t]+(\w+))",
                                     QRegularExpression::MultilineOption);
            auto itD = reDef.globalMatch(stBody);
            while (itD.hasNext()) declared.insert(itD.next().captured(1));
            // tipo NOME(  a inizio riga: definizione o prototipo di funzione
            QRegularExpression reFn(
                R"(^[ \t]*(?:const[ \t]+)?(?:float|vec2|vec3|vec4|int|uint|void|mat2|mat3|mat4|bool)[ \t]+(\w+)[ \t]*\()",
                QRegularExpression::MultilineOption);
            auto itF = reFn.globalMatch(stBody);
            while (itF.hasNext()) declared.insert(itF.next().captured(1));
            // VARIABILI GLOBALI: "float NOME = ...;" / "const int NOME = ...;"
            // a livello di modulo. Diverse dalle funzioni per il '=' al posto
            // della parentesi. Diversi preset le usano come pannello di
            // impostazioni in testa allo script (ZGESize, MAX_STEPS, scale...).
            // I CAMPI DI STRUCT hanno la stessa forma ("float distance;") ma NON
            // vanno rinominati: sono nomi qualificati (v.distance) e toccarli da'
            // "no such field in structure". Si cerca percio' sul testo ripulito
            // dai corpi delle struct.
            QString bodyNoStructs = stBody;
            bodyNoStructs.replace(
                QRegularExpression(R"(struct[ \t]+\w+[ \t]*\{[^}]*\})",
                                   QRegularExpression::DotMatchesEverythingOption),
                QString());
            QRegularExpression reVar(
                R"(^[ \t]*(?:const[ \t]+)?(?:float|vec2|vec3|vec4|int|uint|mat2|mat3|mat4|bool)[ \t]+(\w+)[ \t]*(?:=|;))",
                QRegularExpression::MultilineOption);
            auto itV = reVar.globalMatch(bodyNoStructs);
            while (itV.hasNext()) declared.insert(itV.next().captured(1));
            // STRUCT: il nome del tipo collide come qualunque altro simbolo.
            QRegularExpression reSt(R"(^[ \t]*struct[ \t]+(\w+))",
                                    QRegularExpression::MultilineOption);
            auto itS = reSt.globalMatch(stBody);
            QSet<QString> userTypes;
            while (itS.hasNext()) {
                const QString ty = itS.next().captured(1);
                declared.insert(ty);
                userTypes.insert(ty);
            }
            // FUNZIONI CHE RITORNANO UN TIPO STRUCT ("sVoronoi voronoi(...)"):
            // reFn cerca solo i tipi base, quindi queste sfuggivano e restavano
            // col nome originale -> "overloaded functions must have the same
            // return type" (il tipo di ritorno era stato rinominato, il nome no).
            for (const QString &ty : userTypes) {
                QRegularExpression reFnU(
                    QString(R"(^[ \t]*%1[ \t]+(\w+)[ \t]*\()").arg(QRegularExpression::escape(ty)),
                    QRegularExpression::MultilineOption);
                auto itU = reFnU.globalMatch(stBody);
                while (itU.hasNext()) declared.insert(itU.next().captured(1));
            }

            for (const QString &name : declared) {
                // GIA' RINOMINATI DAI DUE PASSAGGI SOPRA: la raccolta avviene
                // DOPO, quindi qui "mainImage" si presenta gia' come
                // "mainImage_<k>". Senza questo controllo verrebbe suffissato una
                // seconda volta ("mainImage_1_1") e la chiamata dal wrapper non
                // troverebbe piu' la funzione: "no matching overloaded function".
                if (name.endsWith(sfx)) continue;
                if (name.startsWith("iChannel")) continue;
                bool known = false;
                for (const char *s : kSyms)
                    if (name == QLatin1String(s)) { known = true; break; }
                if (known) continue;
                // main non esiste in questo ramo (il corpo e' mainImage), ma
                // se comparisse rinominarlo romperebbe l'entry point.
                if (name == QLatin1String("main")) continue;
                // (?<!\.) come sopra: mai toccare un accesso qualificato
                // (ubuf.qualcosa, o un campo di struct omonimo).
                stBody.replace(QRegularExpression(
                                   QString("(?<!\\.)\\b%1\\b").arg(QRegularExpression::escape(name))),
                               name + sfx);
            }
        }
        codeToInject = stBody;
    }
    else if (safeLogic.contains("getCustomColor")) {
        QString extHelpers;

        extHelpers += "#define iResolution vec3(1.0, 1.0, 1.0)\n"
                      "#define iTime ubuf.u_dummyZero.x\n";

        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col1\\b"))) extHelpers += "#define u_col1 ubuf.u_col1\n";
        if (!safeLogic.contains(QRegularExpression("\\bvec3\\s+u_col2\\b"))) extHelpers += "#define u_col2 ubuf.u_col2\n";
        // TRASFORMAZIONE 2D (zoom / pan / rotazione del mouse in vista flat).
        // Questo ramo iniettava lo script dell'utente TALE E QUALE: gli helpers
        // che calcolano 'uv' non venivano emessi, quindi la funzione riceveva
        // in_uv GREZZO e la texture non rispondeva al mouse in 2D (caso
        // "Animated Gradient", che dichiara getCustomColor(vec2 in_uv) invece di
        // essere uno snippet su u/v -- il ramo snippet gli helpers ce li ha).
        // Non si tocca il CORPO della funzione dell'utente: la si rinomina e le
        // si mette davanti un wrapper col nome atteso, che trasforma le
        // coordinate e la chiama. Cosi' vale per qualunque script di questa
        // forma, presenti e futuri, e il codice utente resta intatto.
        const QString innerName = funcName + "_user";
        safeLogic.replace(QRegularExpression("\\bgetCustomColor\\b"), innerName);

        // SOLO le righe della trasformazione, NON `helpers` intero: quello
        // dichiara anche le LOCALI u_col1/u_col2, che qui sono #define -> si
        // espanderebbero in `vec3 ubuf.u_col1 = ...` ("unexpected DOT") e lo
        // shader non compilerebbe, facendo sparire la superficie. E' la trappola
        // gia' nota del leak delle #define.
        const QString uvTransform =
            "    float _rad = radians(ubuf.u_rotation);\n"
            "    float _c = cos(_rad); float _s = sin(_rad);\n"
            "    vec2 _centered = in_uv - 0.5;\n"
            "    vec2 _rot = vec2(_centered.x * _c - _centered.y * _s, _centered.x * _s + _centered.y * _c) + 0.5;\n"
            "    float _scale = 1.0 / ubuf.u_zoom;\n"
            "    vec2 _shift = ubuf.u_center * 0.5;\n"
            "    vec2 uv = (_rot - 0.5) * _scale + 0.5 + _shift;\n";

        QString wrapper = "vec3 " + funcName + "(vec2 in_uv) {\n"
                        + uvTransform +
                          "    return " + innerName + "(uv);\n"
                          "}\n";
        // CHIUSURA DELLO SCOPE DELLE MACRO. Le #define qui sopra NON hanno
        // scope di funzione: valgono fino a fine file, quindi si espandono
        // dentro le funzioni generate DOPO questa. Il caso rotto: texture
        // globale su questo ramo (che definisce u_col1) + texture per-mesh sul
        // ramo "corpo" (che dichiara `vec3 u_col1 = ubuf.u_col1;` come locale)
        // -> la macro riscriveva la locale in `vec3 ubuf.u_col1 = ...`, cioe'
        // "syntax error, unexpected DOT": lo shader non compilava e la
        // superficie spariva TUTTA. Le rimuoviamo appena il codice che le usa
        // e' stato emesso: dentro safeLogic hanno gia' fatto il loro lavoro.
        QString undefs = "\n#undef iResolution\n#undef iTime\n";
        if (extHelpers.contains("#define u_col1")) undefs += "#undef u_col1\n";
        if (extHelpers.contains("#define u_col2")) undefs += "#undef u_col2\n";
        // Ordine obbligato: la funzione dell'utente (rinominata) PRIMA, poi il
        // wrapper che la chiama -- in GLSL non c'e' hoisting, una funzione va
        // dichiarata prima dell'uso. Gli #undef restano ULTIMI, o le macro
        // sparirebbero prima che il wrapper le abbia usate.
        codeToInject = extHelpers + safeLogic + "\n" + wrapper + undefs;
    }
    else {
        if (!safeLogic.contains("return")) {
            safeLogic += "\n    return vec3(u, v, 0.2); // Fallback\n";
        }
        codeToInject = "vec3 " + funcName + "(vec2 in_uv) {\n"
                       + helpers +
                       "    float u = uv.x;\n"
                       "    float v = uv.y;\n"
                       + safeLogic + "\n"
                                     "}\n";
    }

    return codeToInject;
}

QString GLWidget::createFragmentShaderSource(const QString &customLogic, bool forBorder)
{
    QString fragmentTemplate = loadShaderSource(":/shaders/surface.frag");
    QString commonCode = loadShaderSource(":/shaders/common.glsl");

    QRegularExpression versionRegex("^\\s*#version\\s+[0-9]+(\\s+es|\\s+core)?\\s*\n?");
    fragmentTemplate.remove(versionRegex);

    QString header = "#version 450\n";

    // safePow (pow sicura per basi negative) è in common.glsl, iniettato qui sotto.
    QString fullSource = header + "\n" + commonCode + "\n" + fragmentTemplate;

    // DICHIARAZIONE FONDAMENTALE DELLA TEXTURE PER RHI. Emessa QUI, una volta
    // sola, prima di tutte le funzioni texture: e' una uniform di modulo e
    // ridichiararla per funzione sarebbe un errore di compilazione.
    QString codeToInject = "layout(binding=1) uniform sampler2D tex;\n";

    // FALLBACK "NESSUNA IMMAGINE" PER iChannel0.
    // Gli script che campionano iChannel0 (famiglia "Animated Images": rimostrano
    // l'immagine caricata deformandola) senza alcuna immagine leggevano la texture
    // tappabuchi. Ora, quando u_noImage vale 1, restituiamo una SCACCHIERA
    // PROCEDURALE costruita su u_col1/u_col2: identica per aspetto e per contratto
    // alla scacchiera di default (TextureCode::defaultMeshCode), quindi i
    // picker Color1/Color2 agiscono davvero invece di essere decorativi.
    // Si intercetta la CHIAMATA texture(), non il sampler: i preset la usano sia a
    // 2 argomenti sia a 3 (Squished Coordinates passa il bias di mip), e una macro
    // variadica copre entrambe le arita' senza toccarne il corpo.
    // Il define e' emesso una sola volta a livello di modulo, come il sampler.
    codeToInject +=
        "vec4 _st_sampleTex(vec2 uv) {\n"
        "    if (ubuf.u_noImage != 0) {\n"
        "        vec2 g = floor(uv * 8.0);\n"
        "        float c = mod(g.x + g.y, 2.0);\n"
        "        return vec4(mix(ubuf.u_col1, ubuf.u_col2, c), 1.0);\n"
        "    }\n"
        "    return texture(tex, uv);\n"
        "}\n"
        // Overload col bias di mip: alcuni preset Shadertoy chiamano
        // texture(iChannel0, uv, bias) e la riscrittura conserva il terzo
        // argomento. Sul fallback procedurale il bias non ha senso e si ignora.
        "vec4 _st_sampleTex(vec2 uv, float bias) {\n"
        "    if (ubuf.u_noImage != 0) return _st_sampleTex(uv);\n"
        "    return texture(tex, uv, bias);\n"
        "}\n";

    // Texture GLOBALE della superficie: la funzione che il frag chiama sempre,
    // identica a prima di questa modifica.
    //
    // ECCEZIONE, con texture per-mesh in gioco: se la superficie non ha MAI
    // avuto una texture globale, customLogic e' vuoto e il ramo di default di
    // buildTextureFunction genera `texture(tex, uv)`, cioe' campiona il
    // sampler. Con nessuna immagine caricata li' c'e' m_dummyTexture, che e'
    // 1x1 e non viene mai riempita -> NERO. Le mesh senza texture propria
    // cadono su questa funzione e uscivano nere.
    // In quel caso la neutralizziamo a bianco: bianco e' l'elemento neutro
    // della composizione (finalRGB = ubuf.color * texture), quindi la parte si
    // disegna col proprio colore, che e' esattamente "nessuna texture".
    // AMBITO "ALL": la superficie si comporta come UNA SOLA, quindi vale solo
    // la texture GLOBALE e le texture per-mesh sono SOSPESE (non cancellate:
    // restano nelle MeshPart e tornano passando a "Mesh", come gia' avviene per
    // colore/alpha/luce/wireframe). Percio' in All non si genera alcun
    // dispatcher e ogni parte usa getCustomColor.
    const bool perMeshTextures = !m_meshAppearanceUniform;

    QString globalLogic = customLogic;
    if (globalLogic.trimmed().isEmpty() && engine && perMeshTextures) {
        for (const MeshPart &mp : engine->getMeshParts()) {
            if (!mp.textureCode.trimmed().isEmpty()) {
                globalLogic = QStringLiteral("return vec3(1.0);");
                break;
            }
        }
    }
    codeToInject += buildTextureFunction(globalLogic, "getCustomColor");

    // ==========================================================
    // TEXTURE PER-MESH (procedurali)
    // ==========================================================
    // Ogni parte con codice proprio ottiene la sua getCustomColor_<k> nello
    // STESSO shader, e un dispatcher sceglie su u_meshIndex. Non servono ne'
    // binding ne' pipeline aggiuntive: le mesh gia' si distinguono per blocco
    // UBO (dynamic offset), e u_meshIndex e' costante su tutto il draw di una
    // parte, quindi il branch e' uniforme e non divergente.
    // Le parti SENZA codice proprio cadono sul default, cioe' la texture
    // globale: una superficie che non usa la feature genera esattamente lo
    // shader di prima, dispatcher incluso ma con un solo ramo.
    QString dispatch;
    if (engine && perMeshTextures) {
        const std::vector<MeshPart> &tparts = engine->getMeshParts();
        static const QRegularExpression imgTagRe(R"(^\s*//IMG:.*$\n?)",
                                                 QRegularExpression::MultilineOption);
        for (size_t k = 0; k < tparts.size(); ++k) {
            const QString code = tparts[k].textureCode.trimmed();
            if (code.isEmpty()) continue;
            const QString fn = QString("getCustomColor_%1").arg(k);
            // SOLA IMMAGINE (lo script e' il solo tag //IMG:): la funzione di
            // default, che campiona lo slot 1 -- dove questa fascia ha la sua
            // immagine. Passando il tag come codice, buildTextureFunction ne
            // farebbe una funzione senza return (ripiego a colori u/v).
            QString logic = code;
            logic.remove(imgTagRe);
            const bool imageOnly = logic.trimmed().isEmpty();
            codeToInject += "\n" + buildTextureFunction(imageOnly ? QString() : tparts[k].textureCode, fn);
            // Confronto su float: u_meshIndex e' un indice piccolo esatto in
            // float32, ma restiamo sulla soglia 0.5 come il resto del progetto.
            dispatch += QString("    if (abs(ubuf.u_meshIndex - %1.0) < 0.5) return %2(in_uv);\n")
                            .arg(k).arg(fn);
        }
    }

    if (!dispatch.isEmpty()) {
        codeToInject += "\nvec3 getMeshColor(vec2 in_uv) {\n"
                        + dispatch +
                        "    return getCustomColor(in_uv);\n"
                        "}\n";
    }

    // Il frag chiama getMeshColor SOLO se esiste almeno una texture per-mesh;
    // altrimenti resta la chiamata diretta di sempre.
    QString entryPoint = dispatch.isEmpty() ? QStringLiteral("getCustomColor")
                                            : QStringLiteral("getMeshColor");

    // TEXTURE DEL BORDO: getCustomColor_border, scelta dal flag dei blocchi del
    // bordo (u_isBorder) davanti a tutto il resto. Solo col bordo acceso e una
    // texture: senza, lo shader e' quello di prima. Il suffisso "_border" fa
    // rinominare a buildTextureFunction i simboli di modulo degli script
    // Shadertoy, come per le fasce: la stessa texture su superficie e bordo
    // non collide.
    if (m_borderRadius > 0.0f && !m_borderTexCode.trimmed().isEmpty() && borderSupported()) {
        static const QRegularExpression imgTagRe(R"(^\s*//IMG:.*$\n?)",
                                                 QRegularExpression::MultilineOption);
        QString logic = m_borderTexCode;
        logic.remove(imgTagRe);
        // Sola immagine: la funzione di default, che campiona lo slot 1, dove
        // il bordo ha la sua immagine.
        const bool imageOnly = logic.trimmed().isEmpty();
        codeToInject += "\n" + buildTextureFunction(imageOnly ? QString() : m_borderTexCode,
                                                     QStringLiteral("getCustomColor_border"));
        codeToInject += "\nvec3 getSceneColor(vec2 in_uv) {\n"
                        "    if (ubuf.u_isBorder != 0) return getCustomColor_border(in_uv);\n"
                        "    return " + entryPoint + "(in_uv);\n"
                        "}\n";
        entryPoint = QStringLiteral("getSceneColor");
    }

    fullSource.replace("%CUSTOM_CODE%", codeToInject);

    // PUNTO D'INGRESSO DELLA TEXTURE NEL TEMPLATE.
    // Il template chiama getCustomColor() direttamente (le vecchie
    // `texture(textureSampler, ...)` non esistono piu' nel .frag): quando ci
    // sono texture per-mesh dobbiamo dirottare quelle chiamate sul dispatcher.
    // Si sostituiscono SOLO le chiamate del template (vista 2D in main(),
    // superficie in surfaceTextureColor()), non la definizione delle funzioni
    // appena generate, che vivono in codeToInject ed e' gia' stato inserito: il
    // replace mirato sulle due forme letterali presenti nel template lascia
    // intatte le definizioni.
    if (entryPoint != QLatin1String("getCustomColor")) {
        fullSource.replace("vec3 texColor = getCustomColor(v_texCoord);",
                           "vec3 texColor = " + entryPoint + "(v_texCoord);");
        fullSource.replace("c += wk * getCustomColor(uv);",
                           "c += wk * " + entryPoint + "(uv);");
    }

    // Taglio pareti interne (sezione //CUTOUT_BEGIN..//CUTOUT_END dello script
    // dock Script): vuoto = nessun taglio. HAS_CUTOUT è iniettato SOLO quando
    // serve davvero, così il branch discard in surface.frag (sotto #ifdef) non
    // esiste nemmeno per le superfici senza cutout: costo zero, non solo "sempre
    // false" (misurato: senza questo #ifdef erano comunque 4 istruzioni scalari
    // extra per pixel su OGNI superficie parametrica, anche senza cutout).
    QString cutoutBody = engine ? engine->getCutoutCodeGLSL() : QString();
    QString cutoutCode;
    bool hasCutout = !cutoutBody.trimmed().isEmpty();
    if (!hasCutout) {
        cutoutCode = "bool cutHere(float u, float v) { return false; }";
    } else {
        QString cutoutHelpers = generateGlslHelperVars(cutoutBody);
        cutoutCode = "bool cutHere(float u, float v) {\n" + cutoutHelpers + cutoutBody + "\n}";
    }
    fullSource.replace("%CUTOUT_CODE%", cutoutCode);

    if (hasCutout) {
        fullSource.replace("#version 450", "#version 450\n#define HAS_CUTOUT\n");
    }
    // Bordo acceso: il fragment DEL BORDO scarta i tratti coperti dei suoi lati
    // (borderFragmentCovered), con la stessa condizione del vertex. Solo il
    // suo: in quello della superficie il discard spegnerebbe lo scarto
    // anticipato dei frammenti nascosti (vedi m_borderPipelineOpaque).
    if (forBorder && m_borderRadius > 0.0f && borderSupported()) {
        fullSource.replace("#version 450", "#version 450\n#define HAS_BORDER\n");
    }

    if (m_isCustomMesh) {
        fullSource.replace("#version 450", "#version 450\n#define CUSTOM_MESH\n");
    }

    return fullSource;
}

QString GLWidget::generateGlslHelperVars(const QString& sourceCode) {
    QString vars;
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+t\\b"))) vars += "    float t = ubuf.u_time;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+A\\b"))) vars += "    float A = ubuf.u_mathParams.x;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+B\\b"))) vars += "    float B = ubuf.u_mathParams.y;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+C\\b"))) vars += "    float C = ubuf.u_mathParams.z;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+S\\b"))) vars += "    float S = ubuf.u_mathParams.w;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+s\\b"))) vars += "    float s = ubuf.u_mathParams.w;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+D\\b"))) vars += "    float D = ubuf.u_mathParams2.x;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+E\\b"))) vars += "    float E = ubuf.u_mathParams2.y;\n";
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+F\\b"))) vars += "    float F = ubuf.u_mathParams2.z;\n";
    // MULTI-MESH: indice della parte in corso di disegno, per distinguere il ramo
    // (0 se la superficie e' a mesh singola). Nome esposto allo script: mesh.
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+mesh\\b"))) vars += "    float mesh = ubuf.u_meshIndex;\n";
    // Quante copie (sezioni //MESH_BEGIN o campo Meshes): nome esposto, meshes.
    if (!sourceCode.contains(QRegularExpression("\\bfloat\\s+meshes\\b"))) vars += "    float meshes = ubuf.u_meshCount;\n";
    return vars;
}

QShader GLWidget::bakeShader(const QByteArray &source, QShader::Stage stage)
{
    // Gia' compilato (m_bakedShaders): solo gli shader validi, un errore si
    // ricalcola per averne il messaggio (m_lastCompilationError).
    QByteArray cacheKey = QCryptographicHash::hash(source, QCryptographicHash::Sha1);
    cacheKey.append(char(stage));
    if (const auto it = m_bakedShaders.constFind(cacheKey); it != m_bakedShaders.constEnd())
        return *it;

    QShaderBaker baker;
    baker.setSourceString(source, stage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});

    QList<QShaderBaker::GeneratedShader> targets;

    // Target base per Mobile e Desktop (Vulkan e OpenGL ES)
    targets.append({QShader::SpirvShader, QShaderVersion(100)});
    targets.append({QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs)});
    targets.append({QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)});
    targets.append({QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)});

    // Compila DirectX e OpenGL Desktop SOLO se NON siamo su smartphone
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    targets.append({QShader::GlslShader, QShaderVersion(410)});
    targets.append({QShader::GlslShader, QShaderVersion(460)});
    targets.append({QShader::HlslShader, QShaderVersion(50)});
#endif

    // Compila Metal SOLO su sistemi Apple (macOS/iOS)
#if defined(Q_OS_APPLE)
    targets.append({QShader::MslShader,  QShaderVersion(12)});
#endif

    // Solo quelli del backend in uso: gli altri erano traduzioni che nessuno
    // legge (su Mac 8 per shader, di cui serve l'MSL).
    baker.setGeneratedShaders(TubeCurveSampler::targetsForBackend(rhi(), targets));

    QShader shader = baker.bake();
    if (!shader.isValid()) {
        qWarning() << "SHADER ERROR (" << (stage == QShader::VertexStage ? "VERT" : "FRAG") << "):" << baker.errorMessage();
        // L'errore va CONSERVATO, non solo loggato: buildPipeline lo usa per
        // avvisare l'utente. Senza, un fallimento qui faceva sparire la
        // superficie in silenzio (le pipeline vengono azzerate) e l'unica
        // traccia era una riga sulla console, che l'utente non vede.
        m_lastCompilationError = baker.errorMessage();

        // RISORSA MANCANTE, non errore dell'utente. Se un loadShaderSource e'
        // fallito, il sorgente passato al baker e' vuoto o monco: il fallimento
        // NON viene dallo script, e l'avviso standard ("una texture dichiara un
        // simbolo gia' usato") sarebbe una spiegazione falsa. Qui si sostituisce
        // con la causa vera, che e' un guasto di BUILD e non qualcosa che
        // l'utente possa correggere dall'interfaccia.
        // Il segno distintivo e' proprio errorMessage() VUOTO: il compilatore non
        // ha nulla da dire perche' non ha ricevuto codice.
        if (!g_lastMissingShaderResource.isEmpty()) {
            m_lastCompilationError =
                QStringLiteral("Internal error: the built-in shader resource \"%1\" is "
                               "missing from the application bundle, so no shader code "
                               "could be compiled.\n"
                               "This is a packaging problem, not a problem with your "
                               "surface or texture: reinstalling the application should "
                               "fix it.")
                    .arg(g_lastMissingShaderResource);
        }
    }
    if (shader.isValid()) {
        if (m_bakedShaders.size() >= kBakedShadersMax) m_bakedShaders.clear();
        m_bakedShaders.insert(cacheKey, shader);
    }
    return shader;
}

// --- Pipeline & Resource Initialization ---

void GLWidget::buildPipeline() {
    // NB: le pipeline VECCHIE non si distruggono qui. Prima si compilano gli
    // shader: se non sono validi si esce lasciandole in piedi, e a schermo resta
    // l'ultima immagine buona invece del vuoto (vedi il controllo di validita').
    // La distruzione avviene solo quando c'e' un rimpiazzo pronto.

    // --- Layout, shader e blend (variabili locali, usate da tutte le pipeline) ---
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float4, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal)   },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });

    // Si riparte da zero: la traccia vale per QUESTO giro di build, altrimenti
    // una risorsa mancante letta una volta continuerebbe a spiegare come
    // "bundle rotto" ogni errore successivo, compresi quelli veri dell'utente.
    // Va azzerata PRIMA di generare i sorgenti, che e' cio' che rilegge le
    // risorse dal .qrc.
    g_lastMissingShaderResource.clear();

    QString vsSource = createVertexShaderSource(m_eqX, m_eqY, m_eqZ, m_eqW);
    QString fsSource = createFragmentShaderSource(m_customFragmentCode);
    QShader vs = bakeShader(vsSource.toUtf8(), QShader::VertexStage);
    QShader fs = bakeShader(fsSource.toUtf8(), QShader::FragmentStage);

    if (!vs.isValid() || !fs.isValid()) {
        qWarning() << "buildPipeline: shader non valido, pipeline non costruite.";

        // SI TENGONO LE PIPELINE PRECEDENTI: a schermo resta l'ULTIMA IMMAGINE
        // VALIDA invece del vuoto. Prima qui si distruggevano, e una texture che
        // non compila faceva sparire tutta la superficie -- perdendo anche il
        // lavoro fatto fino a quel momento. Lo shader nuovo semplicemente non
        // entra in vigore: e' il comportamento meno distruttivo, ed e' anche
        // quello che il resto del codice gia' assume altrove ("rebuildShader
        // lascia in piedi il precedente").
        // Se non esistono pipeline precedenti (fallimento al primo build) non
        // c'e' nulla da salvare e restano nulle, come prima.

        // UN SOLO AVVISO per errore. Il popup desktop e' MODALE (box.exec() gira
        // un event loop annidato): i segnali emessi nel frattempo verrebbero
        // consegnati DENTRO quel loop, ognuno aprendo un altro popup sopra il
        // precedente -- la raffica che rendeva l'app inutilizzabile. La guardia
        // lato ricevente non basta proprio per questo: va zittita la SORGENTE.
        // Il flag si riarma da solo appena uno shader torna valido (piu' sotto),
        // cosi' un errore successivo viene di nuovo segnalato.
        if (!m_shaderErrorReported) {
            m_shaderErrorReported = true;
            emit shaderCompilationFailed(m_lastCompilationError);
        }
        return;
    }

    // Shader valido: si riarma l'avviso, cosi' il prossimo errore (una texture
    // diversa, applicata piu' tardi) viene segnalato di nuovo.
    m_shaderErrorReported = false;

    // Il fragment del bordo: lo stesso con HAS_BORDER (vedi
    // m_borderPipelineOpaque). Differisce solo per il codice fisso dei tratti
    // coperti; se non compilasse lo stesso, il bordo usa le pipeline della
    // superficie e resta intero.
    QShader fsBorder;
    if (m_borderRadius > 0.0f && borderSupported())
        fsBorder = bakeShader(createFragmentShaderSource(m_customFragmentCode, true).toUtf8(), QShader::FragmentStage);

    // ORA si liberano le pipeline precedenti: il rimpiazzo e' garantito, perche'
    // gli shader hanno compilato. Farlo in cima alla funzione (com'era) le
    // distruggeva anche quando il build falliva, ed e' cio' che faceva sparire
    // la superficie.
    // Tutte e quattro: prima le tre trasparenti/wireframe venivano riassegnate
    // senza delete, contando sulla pulizia in cima. Spostata quella, vanno
    // liberate qui o si perderebbero (leak a ogni rebuild).
    auto freePipe = [](QRhiGraphicsPipeline *&p){ if (p) { delete p; p = nullptr; } };
    freePipe(m_pipelineOpaque);
    freePipe(m_pipelineTranspBack);
    freePipe(m_pipelineTranspFront);
    freePipe(m_wireframePipeline);
    freePipe(m_borderPipelineOpaque);
    freePipe(m_borderPipelineWire);
    // La passata dei tubi nasce dallo stesso vertex: va rifatta con lui.
    buildTubeFramePipeline(vsSource);
    // Anche quella dei lati del bordo coperti, ma solo al primo frame che la
    // usa (sharedEdgesActive): qui si sa solo da quale vertex nascera'. Dal
    // vertex SENZA il codice del bordo: il compute usa solo la superficie, e
    // cosi' e' la stessa a bordo acceso e spento -- spento gira come sonda
    // (borderAvailability) -- e non si ricompila accendendo il bordo o
    // cambiando texture, solo quando cambia la geometria.
    QString edgeSource;
    if (!vsSource.contains(QLatin1String("#define IS_TUBE")) && !vsSource.contains(QLatin1String("#define CUSTOM_MESH"))) {
        edgeSource = vsSource;
        edgeSource.remove(kBorderDefineLine);
    }
    if (edgeSource != m_sharedEdgeSource) {
        releaseSharedEdgePipeline();
        m_sharedEdgeSource = edgeSource;
        m_sharedEdgeSourceDirty = !edgeSource.isEmpty();
        // Superficie nuova: nuova sonda, e la lettura in corso non vale piu'.
        m_borderProbeKey.clear();
        m_borderProbePending.clear();
        m_borderProbeArmed = false;
        m_borderProbeWatch = false;
        ++m_borderProbeSerial;
    }

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    // MULTI-MESH: le pipeline della superficie devono essere costruite sul
    // layout con DYNAMIC OFFSET, perche' e' quello che ricevono al draw (un
    // blocco UBO per parte di mesh). Il layout dichiarato alla pipeline e quello
    // passato a setShaderResources devono corrispondere, altrimenti il
    // comportamento dipende dall'API grafica.
    // Nota: le pipeline dello sfondo (m_bgPipeline) e del ray marching restano
    // sul binding statico m_bindings, che non ha parti.
    ensureDynamicBindings(m_surfaceTexture ? m_surfaceTexture : m_dummyTexture);
    QRhiShaderResourceBindings *surfaceBindings = m_bindingsDyn ? m_bindingsDyn : m_bindings;

    // --- 1. PIPELINE OPACO (Klein perfetta, Bordi nascosti) ---
    m_pipelineOpaque = rhi()->newGraphicsPipeline();
    m_pipelineOpaque->setVertexInputLayout(inputLayout);
    m_pipelineOpaque->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineOpaque->setSampleCount(renderTarget()->sampleCount());
    m_pipelineOpaque->setDepthTest(true);
    m_pipelineOpaque->setDepthWrite(true); // SCRITTURA ATTIVA
    m_pipelineOpaque->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineOpaque->setCullMode(QRhiGraphicsPipeline::None); // DISEGNA TUTTO INSIEME
    m_pipelineOpaque->setTargetBlends({ blend });
    m_pipelineOpaque->setShaderResourceBindings(surfaceBindings);
    m_pipelineOpaque->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineOpaque->create()) {
        qWarning() << "buildPipeline: create() opaque fallita.";
        delete m_pipelineOpaque; m_pipelineOpaque = nullptr;
    }

    // --- 2. PIPELINE TRASPARENTE: FACCE POSTERIORI (Passata 1) ---
    m_pipelineTranspBack = rhi()->newGraphicsPipeline();
    m_pipelineTranspBack->setVertexInputLayout(inputLayout);
    m_pipelineTranspBack->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineTranspBack->setSampleCount(renderTarget()->sampleCount());
    m_pipelineTranspBack->setDepthTest(true);
    m_pipelineTranspBack->setDepthWrite(false); // SCRITTURA SPENTA
    m_pipelineTranspBack->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineTranspBack->setCullMode(QRhiGraphicsPipeline::Front); // NASCONDE IL FRONTE
    m_pipelineTranspBack->setTargetBlends({ blend });
    m_pipelineTranspBack->setShaderResourceBindings(surfaceBindings);
    m_pipelineTranspBack->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineTranspBack->create()) {
        qWarning() << "buildPipeline: create() transpBack fallita.";
        delete m_pipelineTranspBack; m_pipelineTranspBack = nullptr;
    }

    // --- 3. PIPELINE TRASPARENTE: FACCE ANTERIORI (Passata 2) ---
    m_pipelineTranspFront = rhi()->newGraphicsPipeline();
    m_pipelineTranspFront->setVertexInputLayout(inputLayout);
    m_pipelineTranspFront->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_pipelineTranspFront->setSampleCount(renderTarget()->sampleCount());
    m_pipelineTranspFront->setDepthTest(true);
    m_pipelineTranspFront->setDepthWrite(false); // SCRITTURA SPENTA
    m_pipelineTranspFront->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipelineTranspFront->setCullMode(QRhiGraphicsPipeline::Back); // NASCONDE IL RETRO
    m_pipelineTranspFront->setTargetBlends({ blend });
    m_pipelineTranspFront->setShaderResourceBindings(surfaceBindings);
    m_pipelineTranspFront->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipelineTranspFront->create()) {
        qWarning() << "buildPipeline: create() transpFront fallita.";
        delete m_pipelineTranspFront; m_pipelineTranspFront = nullptr;
    }

    // --- PIPELINE WIREFRAME ---
    if (m_wireframePipeline) {
        delete m_wireframePipeline;
        m_wireframePipeline = nullptr;
    }
    m_wireframePipeline = rhi()->newGraphicsPipeline();
    m_wireframePipeline->setTopology(QRhiGraphicsPipeline::Lines);

    // Assegna gli stessi identici layout e shader della mesh principale
    m_wireframePipeline->setVertexInputLayout(inputLayout);
    m_wireframePipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
    m_wireframePipeline->setSampleCount(renderTarget()->sampleCount());
    m_wireframePipeline->setDepthTest(true);
    m_wireframePipeline->setDepthWrite(true);
    m_wireframePipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_wireframePipeline->setTargetBlends({ blend });
    m_wireframePipeline->setShaderResourceBindings(surfaceBindings);
    m_wireframePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_wireframePipeline->create()) {
        qWarning() << "buildPipeline: create() wireframe fallita.";
        delete m_wireframePipeline; m_wireframePipeline = nullptr;
    }

    // --- PIPELINE DEL BORDO: lo stato dell'opaca e della wireframe, col
    // fragment del bordo ---
    if (fsBorder.isValid()) {
        auto borderPipe = [&](bool lines, const char *what) -> QRhiGraphicsPipeline * {
            QRhiGraphicsPipeline *p = rhi()->newGraphicsPipeline();
            if (lines) p->setTopology(QRhiGraphicsPipeline::Lines);
            p->setVertexInputLayout(inputLayout);
            p->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fsBorder } });
            p->setSampleCount(renderTarget()->sampleCount());
            p->setDepthTest(true);
            p->setDepthWrite(true);
            p->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            p->setCullMode(QRhiGraphicsPipeline::None);
            p->setTargetBlends({ blend });
            p->setShaderResourceBindings(surfaceBindings);
            p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!p->create()) {
                qWarning() << "buildPipeline: create()" << what << "del bordo fallita.";
                delete p;
                p = nullptr;
            }
            return p;
        };
        m_borderPipelineOpaque = borderPipe(false, "opaca");
        m_borderPipelineWire = borderPipe(true, "wireframe");
    }
}

void GLWidget::initBackgroundShader() {
    if (m_bgVbo) return;

    m_bgVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 6 * sizeof(Vertex));
    m_bgVbo->create();

    if (!m_bgUbo) {
        m_bgUbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UboData));
        m_bgUbo->create();
    }

    m_bgBindings = rhi()->newShaderResourceBindings();
    m_bgBindings->setBindings({
        // CORREZIONE: Usa m_bgUbo invece di m_ubo!
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_bgUbo),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_dummyTexture, m_sampler)
    });
    m_bgBindings->create();
}

// --- Texture Utilities ---

void GLWidget::createDummyTexture(QRhiCommandBuffer *cb) {
    if (!rhi()) return;

    // Texture "tappabuchi" per lo slot sampler quando nessuna immagine e'
    // caricata. Era 1x1 e non veniva MAI riempita: il contenuto restava
    // indefinito (in pratica zeri = NERO OPACO). Finche' serviva solo a
    // occupare il binding non dava fastidio, ma gli script che campionano
    // iChannel0 davvero -- quelli di "Animated Images", che rimostrano
    // l'immagine caricata -- leggevano quel nero e la mesh diventava nera,
    // senza alcun errore.
    // Ora e' una SCACCHIERA vera: e' il fallback che l'utente si aspetta e
    // dice a colpo d'occhio "nessuna immagine caricata".
    // Nota: NON e' il rimedio del caso "mesh senza texture propria" (vedi
    // createFragmentShaderSource), che si risolve nel codice generato con
    // `return vec3(1.0)` e non dipende dal contenuto di questa texture.
    // Colori: il VERDE di default del progetto (0.20, 0.80, 0.20 -> 51,204,51,
    // lo stesso del colore di default della superficie e del Colore 1 della
    // scacchiera di default) e il nero del Colore 2. Cosi' il fallback ha
    // l'aspetto di casa invece del bianco/nero generico.
    const int kDummySize = 64;   // 8 celle da 8 px
    const int kDummyCell = 8;
    QImage checker(kDummySize, kDummySize, QImage::Format_RGBA8888);
    for (int y = 0; y < kDummySize; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(checker.scanLine(y));
        for (int x = 0; x < kDummySize; ++x) {
            const bool light = ((x / kDummyCell) + (y / kDummyCell)) % 2 == 0;
            row[x] = light ? qRgba(51, 204, 51, 255) : qRgba(0, 0, 0, 255);
        }
    }

    m_dummyTexture = rhi()->newTexture(QRhiTexture::RGBA8, checker.size(), 1);
    m_dummyTexture->create();

    // L'upload va accodato in un batch e sottomesso: create() alloca soltanto.
    // Senza questo la scacchiera resterebbe scritta solo lato CPU e la texture
    // continuerebbe a leggere nero, cioe' il bug di partenza.
    // Il command buffer arriva da initialize(cb): QRhiWidget non espone alcun
    // commandBuffer(), l'unico modo di sottomettere e' passarlo qui.
    if (cb) {
        QRhiResourceUpdateBatch *batch = rhi()->nextResourceUpdateBatch();
        // Variante QImage (non puntatore+size): QImage e' implicitly shared e
        // viene tenuta per valore dalla descrizione, quindi i pixel restano
        // validi anche se 'checker' e' locale a questa funzione.
        batch->uploadTexture(m_dummyTexture,
                             QRhiTextureUploadDescription(
                                 QRhiTextureUploadEntry(0, 0,
                                     QRhiTextureSubresourceUploadDescription(checker))));
        cb->resourceUpdate(batch);
    }

    // Crea le regole di lettura per l'immagine
    m_sampler = rhi()->newSampler(QRhiSampler::Linear,
                                  QRhiSampler::Linear,
                                  QRhiSampler::None,
                                  QRhiSampler::Repeat,
                                  QRhiSampler::Repeat);
    m_sampler->create();
}

// --- Math & Projections ---

QVector3D GLWidget::projectPoint4Dto3D(const QVector4D& p) {
    // 1. Ortogonale Pura
    if (projectionMode == 0) {
        return p.toVector3D();
    }
    // 2. Stereografica Pura (Polo fissato sull'asse W)
    else if (projectionMode == 2) {
        float r = p.length();
        if (r < 0.0001f) return QVector3D(0, 0, 0);

        // 1. Curviamo lo spazio normalizzando il punto sulla sfera 4D
        QVector4D pNorm = p / r;

        // 2. Proiettiamo dal "Polo Nord" (W = 1.0)
        float denom = 1.0f - pNorm.w();
        if (denom < 0.05f) denom = 0.05f; // Evita esplosioni matematiche al polo

        // 3. Ripristiniamo la scala (r) ammorbidita (* 0.5) per il 3D
        return pNorm.toVector3D() * (r / denom);
    }

    // 3. Prospettiva Centrale (Camera Pinhole 4D)
    float distW = m_observerPos.w() - p.w();
    if (std::abs(distW) < 0.01f) {
        return p.toVector3D();
    }

    float wFactor = m_observerPos.w() / distW;
    float x = m_observerPos.x() + (p.x() - m_observerPos.x()) * wFactor;
    float y = m_observerPos.y() + (p.y() - m_observerPos.y()) * wFactor;
    float z = m_observerPos.z() + (p.z() - m_observerPos.z()) * wFactor;

    return QVector3D(x, y, z);
}
