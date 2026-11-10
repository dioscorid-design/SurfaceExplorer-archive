// camerapaths.cpp - I path della camera. Vedi camerapaths.h.
#include "camerapaths.h"

#include "glwidget.h"
#include "scenestate.h"
#include "surfaceengine.h"

#include <QTimer>
#include <QVector3D>
#include <QVector4D>
#include <cmath>

namespace {
float det3x3(float a1, float a2, float a3,
             float b1, float b2, float b3,
             float c1, float c2, float c3)
{
    return a1 * (b2 * c3 - b3 * c2) -
           a2 * (b1 * c3 - b3 * c1) +
           a3 * (b1 * c2 - b2 * c1);
}
}

CameraPaths::CameraPaths(GLWidget *view, const SceneState &scene,
                         std::function<bool()> recording, QObject *parent)
    : QObject(parent), m_view(view), m_scene(scene), m_recording(std::move(recording))
{
    m_timer4D = new QTimer(this);
    m_timer4D->setInterval(30);
    connect(m_timer4D, &QTimer::timeout, this, [this] { tick(Path4D); });
    m_timer3D = new QTimer(this);
    m_timer3D->setInterval(30);
    connect(m_timer3D, &QTimer::timeout, this, [this] { tick(Path3D); });
}

bool CameraPaths::isRunning(Path p) const { return timer(p)->isActive(); }
void CameraPaths::start(Path p) { timer(p)->start(); }
void CameraPaths::stop(Path p) { timer(p)->stop(); }
int CameraPaths::interval(Path p) const { return timer(p)->interval(); }

CameraPaths::Paused CameraPaths::pause()
{
    Paused paused{ isRunning(Path4D), isRunning(Path3D) };
    if (paused.path4D) stop(Path4D);
    if (paused.path3D) stop(Path3D);
    return paused;
}

void CameraPaths::resume(const Paused &paused)
{
    if (paused.path4D) start(Path4D);
    if (paused.path3D) start(Path3D);
}

float CameraPaths::time(Path p) const { return p == Path4D ? m_t4D : m_t3D; }

void CameraPaths::resetTimes()
{
    m_t4D = 0.0f;
    m_t3D = 0.0f;
}

void CameraPaths::resetSession()
{
    resetTimes();
    m_started4DOnce = false;
    m_anyStartedOnce = false;
}

void CameraPaths::captureBase4D()
{
    if (!m_started4DOnce) {
        m_started4DOnce = true;
        m_baseOmega = m_basePhi = m_basePsi = 0.0f;
        return;
    }
    // Dai successivi si conserva l'orientamento corrente (es. quello accumulato
    // dal moto GO), senza reset nel passaggio di modalita'.
    //
    // "Conservare" vuol dire che il PRIMO TICK ridia l'orientamento corrente, e
    // il tick scrive phi = base - gamma(t), psi = base - beta(t) (vedi
    // applyCamera4DAt). Quindi la base e' l'orientamento corrente PIU' la
    // compensazione al tempo da cui si riparte: leggere solo getPhi/getPsi,
    // dopo uno Stop di questo stesso path, contava due volte il -gamma/-beta gia'
    // applicato, e alla ripartenza la sezione saltava di gamma(T)/beta(T) --
    // con alpha=beta=gamma=t (Morphing 3-Torus) tanto piu' quanto piu' a lungo
    // il path aveva girato. Vale anche venendo dal moto GO o dal path 3D: gli
    // angoli correnti includono gia' quel che e' successo nel frattempo.
    // La formula usa le equazioni appena compilate dal chiamante: se i campi
    // sono cambiati durante lo Stop resta continua la rotazione (la posizione
    // segue il nuovo path, com'e' giusto).
    SurfaceEngine *engine = m_view->getEngine();
    m_baseOmega = m_view->getOmega();
    m_basePhi   = m_view->getPhi() + engine->evaluatePathGamma(m_t4D);
    m_basePsi   = m_view->getPsi() + engine->evaluatePathBeta(m_t4D);
}

bool CameraPaths::takeFirstStart()
{
    if (m_anyStartedOnce) return false;
    m_anyStartedOnce = true;
    return true;
}

void CameraPaths::tick(Path p)
{
    if (!isRunning(p)) return;
    if (m_recording && m_recording()) return;
    advance(p, 1.0f);
}

void CameraPaths::advance(Path p, float ticks)
{
    // Velocita': unita' dello slider (1..100), un millesimo per tick.
    if (p == Path4D) {
        m_t4D += m_scene.pathSpeed4D / 1000.0f * ticks;
        applyCamera4DAt(m_t4D);
    } else {
        m_t3D += m_scene.pathSpeed3D / 1000.0f * ticks;
        applyCamera3DAt(m_t3D);
    }
}

void CameraPaths::applyCameraAt(Path p, float t)
{
    if (p == Path4D) applyCamera4DAt(t);
    else             applyCamera3DAt(t);
}

void CameraPaths::applyCamera4DAt(float t)
{
    // NB: il FOV NON si applica qui. Con lo slider unico del dock renderer il
    // campo visivo e' gia' impostato su GLWidget e vale per tutto (path,
    // rotazioni, superfici ferme); riapplicarlo a ogni tick sovrascriverebbe una
    // regolazione fatta MENTRE il path e' in corsa. Il recorder non ne risente:
    // legge lo stesso m_cameraFov vivo del tick live.

    // 1. SETUP BASE
    float dt = 0.01f;

    SurfaceEngine* engine = m_view->getEngine();

    // 2. VALUTAZIONE POSIZIONE (la tangente serve solo in vista Tangent,
    // quindi p_prev si valuta dentro quel ramo: una eval exprtk in meno
    // per frame in vista Center)
    QVector4D p_curr = engine->evaluatePathPosition(t);
    QVector4D p_next = engine->evaluatePathPosition(t + dt);

    QVector4D V;

    // 3. RECUPERO ANGOLI (Alpha, Beta, Gamma)
    float alpha = engine->evaluatePathAlpha(t);
    float beta  = engine->evaluatePathBeta(t);
    float gamma = engine->evaluatePathGamma(t);

    // 4. CALCOLO BASE ORTONORMALE LOCALE (N1, N2, N3)
    QVector4D N1, N2, N3;
    QVector4D finalPos4D, finalTarget4D, finalUp4D;

    if (m_scene.pathViewMode4D == ModeTangential) {
        QVector4D velocity = p_next - engine->evaluatePathPosition(t - dt);
        V = (velocity.lengthSquared() > 1e-8f) ? velocity.normalized() : QVector4D(0, 1, 0, 0);

        QVector4D K(0.0f, 0.0f, 1.0f, 0.0f);
        N1 = K - V * QVector4D::dotProduct(K, V);
        if (N1.lengthSquared() > 1e-6f) N1.normalize();
        else {
            QVector4D Y(0.0f, 1.0f, 0.0f, 0.0f);
            N1 = (Y - V * QVector4D::dotProduct(Y, V)).normalized();
        }

        QVector3D v3 = V.toVector3D();
        QVector3D n13 = N1.toVector3D();
        QVector3D side3 = QVector3D::crossProduct(v3, n13);

        if (side3.lengthSquared() > 1e-6f) {
            N2 = QVector4D(side3, 0.0f).normalized();
            N2 = N2 - V * QVector4D::dotProduct(N2, V) - N1 * QVector4D::dotProduct(N2, N1);
            N2.normalize();
        } else {
            QVector4D I(1.0f, 0.0f, 0.0f, 0.0f);
            N2 = I - V * QVector4D::dotProduct(I, V) - N1 * QVector4D::dotProduct(I, N1);
            N2.normalize();
        }
        finalPos4D = p_curr - V * 0.2f;
        finalTarget4D = p_next;
    } else {
        finalPos4D = p_curr;
        finalTarget4D = QVector4D(0,0,0,0);
        QVector4D viewDir = (finalTarget4D - finalPos4D);
        V = (viewDir.lengthSquared() > 1e-8f) ? viewDir.normalized() : QVector4D(0,0,-1,0);
        N1 = QVector4D(0,0,1,0);

        QVector4D globalX(1,0,0,0);
        N2 = globalX - V * QVector4D::dotProduct(globalX, V) - N1 * QVector4D::dotProduct(globalX, N1);
        N2.normalize();
    }

    // Calcolo N3 (Ana)
    float dx =  det3x3(V.y(), V.z(), V.w(),  N1.y(), N1.z(), N1.w(),  N2.y(), N2.z(), N2.w());
    float dy = -det3x3(V.x(), V.z(), V.w(),  N1.x(), N1.z(), N1.w(),  N2.x(), N2.z(), N2.w());
    float dz =  det3x3(V.x(), V.y(), V.w(),  N1.x(), N1.y(), N1.w(),  N2.x(), N2.y(), N2.w());
    float dw = -det3x3(V.x(), V.y(), V.z(),  N1.x(), N1.y(), N1.z(),  N2.x(), N2.y(), N2.z());
    N3 = QVector4D(dx, dy, dz, dw).normalized();

    // Composizione Orientamento Locale
    float ca = std::cos(alpha), sa = std::sin(alpha);
    float cb = std::cos(beta),  sb = std::sin(beta);
    float cg = std::cos(gamma), sg = std::sin(gamma);

    float c1 = ca * cb;
    float c2 = sa * cg - ca * sb * sg;
    float c3 = sa * sg + ca * sb * cg;

    finalUp4D = N1 * c1 + N2 * c2 + N3 * c3;
    finalUp4D.normalize();

    // =========================================================================
    // >>> SINCRONIZZATO BETA + GAMMA <<<
    // =========================================================================

    // 1. Definiamo le rotazioni globali per compensare, RELATIVE alla base
    // catturata all'avvio del path (orientamento 4D preesistente, es. dal moto GO)
    float rotOmega = m_baseOmega;          // X-W (base)
    float rotPhi   = m_basePhi - gamma;    // Y-W (base + fix per Gamma)
    float rotPsi   = m_basePsi - beta;     // Z-W (base + fix per Beta)

    // 2. Aggiorniamo la GPU (Shader)
    m_view->setRotation4D(rotOmega, rotPhi, rotPsi);

    // 3. Funzione helper per ruotare la CPU Camera
    // NB: stesso ordine dello shader (surface.vert): XW -> YW -> ZW
    auto transformCPU = [&](QVector4D v) {
        // A. Rotazione XW (Omega base)
        if (std::abs(rotOmega) > 1e-6f) {
            float c = std::cos(rotOmega);
            float s = std::sin(rotOmega);
            float x = v.x();
            float w = v.w();
            v.setX( x * c + w * s);
            v.setW(-x * s + w * c);
        }
        // B. Rotazione YW (Phi / Gamma Fix)
        if (std::abs(rotPhi) > 1e-6f) {
            float c = std::cos(rotPhi);
            float s = std::sin(rotPhi);
            float y = v.y();
            float w = v.w();
            v.setY( y * c + w * s);
            v.setW(-y * s + w * c);
        }
        // C. Rotazione ZW (Psi / Beta Fix)
        if (std::abs(rotPsi) > 1e-6f) {
            float c = std::cos(rotPsi);
            float s = std::sin(rotPsi);
            float z = v.z();
            float w = v.w();
            v.setZ( z * c + w * s);
            v.setW(-z * s + w * c);
        }
        return v;
    };

    // 4. Applichiamo la trasformazione ai vettori camera
    QVector4D rotPos    = transformCPU(finalPos4D);
    QVector4D rotTarget = transformCPU(finalTarget4D);
    QVector4D rotUp     = transformCPU(finalUp4D);

    // 5. Invio finale
    m_view->setCameraFrom4DVectors(rotPos, rotTarget, rotUp);

    // NB: qui NON si aggiorna il readout del dock 4D. I campi misurano quanto
    // l'utente ha mosso con i TASTI a scatto: il path (e le rotazioni) muovono
    // la stessa camera, e riscrivere i campi mentre corrono faceva ballare sette
    // numeri che l'utente non stava toccando, perdendo per giunta il conto degli
    // scatti dati. L'unico aggiornamento sta in MainWindow::stepNavActions,
    // lo scatto dei tasti premuti.
}

void CameraPaths::applyCamera3DAt(float t)
{
    // NB: il FOV NON si applica qui: vedi la nota in applyCamera4DAt.
    // Lo slider unico lo imposta una volta e vale per tutto.

    QVector4D rawData = m_view->getEngine()->evaluatePath3DPosition(t);

    // Scala la posizione (XYZ) ma NON il rollio (W)
    QVector3D currentPos = rawData.toVector3D();
    float currentRoll = rawData.w();

    QVector3D target;

    if (m_scene.pathViewMode3D == ModeTangential) {
        float delta = 0.1f;
        QVector4D futureData = m_view->getEngine()->evaluatePath3DPosition(t + delta);
        target = futureData.toVector3D();
    } else {
        target = QVector3D(0, 0, 0);
    }

    m_view->setCameraPosAndDirection3D(currentPos, target, currentRoll);
}
