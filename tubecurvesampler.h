// tubecurvesampler.h - Campiona sulla GPU la curva asse di un tubo.
//
// Al Run il motore dei tubi sceglie, per ogni tubo, la direzione che orienta
// la sezione (lontana da tutte le tangenti) e se la curva si chiude: per farlo
// gli servono i punti della curva. Le equazioni del sotto-tab Tubes si
// valutano sulla CPU (exprtk); uno SCRIPT e' GLSL e la CPU non lo sa valutare.
// Qui lo si valuta sulla GPU con un compute shader che contiene la STESSA
// funzione della curva del vertex (GLWidget::tubeCurveFunction), e i punti
// tornano alla CPU. Stesso schema del flusso geodetico (GeodesicCalculator):
// frame offscreen, dispatch, lettura sincrona.
#ifndef TUBECURVESAMPLER_H
#define TUBECURVESAMPLER_H

#include <QString>
#include <QVector>
#include <QVector4D>
#include <rhi/qshaderbaker.h>

class QRhi;
class QShader;

class TubeCurveSampler
{
public:
    // Per ogni campione (u, mesh, t, meshes) di `params` restituisce in `out` il
    // punto (x, y, z, p) della curva. `constants` = A, B, C, D, E, F, s.
    // false = niente GPU, shader che non compila o lettura fallita (`error`).
    static bool sample(QRhi *rhi, const QString &curveFunction,
                       const QVector<QVector4D> &params, const float constants[7],
                       QVector<QVector4D> *out, QString *error = nullptr);

    // Compila un compute shader per il backend di `rhi` (tutti i backend, gli
    // stessi target del flusso geodetico, se `rhi` e' nullo), con la
    // precisione alta forzata nelle varianti GLSL ES. Lo usano anche il
    // trasporto della sezione dei tubi e i lati coperti del bordo (GLWidget).
    // Gli shader gia' compilati si riprendono dalla memoria.
    static bool bakeCompute(QRhi *rhi, const QString &source, QShader *shader, QString *error = nullptr);

    // Dei target di `all` tiene solo quelli che il backend di `rhi` legge:
    // MSL su Metal, SPIR-V su Vulkan, GLSL su OpenGL, HLSL su Direct3D. Ogni
    // target in piu' e' una traduzione di SPIRV-Cross che nessuno usa (fino a
    // 8 per shader). `rhi` nullo o backend sconosciuto: tutti, come prima.
    static QList<QShaderBaker::GeneratedShader> targetsForBackend(QRhi *rhi,
                                                                  const QList<QShaderBaker::GeneratedShader> &all);
};

#endif // TUBECURVESAMPLER_H
