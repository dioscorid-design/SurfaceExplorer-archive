#include "tubecurvesampler.h"

#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <memory>

static QString loadShaderLibrary(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QTextStream(&f).readAll();
}

QList<QShaderBaker::GeneratedShader> TubeCurveSampler::targetsForBackend(QRhi *rhi,
                                                                         const QList<QShaderBaker::GeneratedShader> &all)
{
    if (!rhi) return all;
    QShader::Source wanted;
    switch (rhi->backend()) {
    case QRhi::Metal:     wanted = QShader::MslShader;   break;
    case QRhi::Vulkan:    wanted = QShader::SpirvShader; break;
    case QRhi::OpenGLES2: wanted = QShader::GlslShader;  break;
    case QRhi::D3D11:
    case QRhi::D3D12:     wanted = QShader::HlslShader;  break;
    default:              return all;
    }
    QList<QShaderBaker::GeneratedShader> kept;
    for (const QShaderBaker::GeneratedShader &t : all)
        if (t.first == wanted) kept.append(t);
    return kept.isEmpty() ? all : kept;
}

// Compute tenuti in memoria (bakeCompute): oltre, si riparte da vuoto.
static constexpr int kBakedComputeMax = 32;

bool TubeCurveSampler::bakeCompute(QRhi *rhi, const QString &source, QShader *shader, QString *error)
{
    // GIA' COMPILATI, per backend e sorgente: il trasporto dei tubi e i lati
    // coperti del bordo si rifanno a ogni ricompilazione della superficie, e
    // la loro sorgente spesso non cambia (accendere il bordo, cambiare
    // texture). Solo quelli validi: un errore si ricalcola per averne il
    // messaggio. Tutto sul thread della GUI (render di QRhiWidget e Run).
    static QHash<QByteArray, QShader> baked;
    const QByteArray utf8 = source.toUtf8();
    QByteArray cacheKey = QCryptographicHash::hash(utf8, QCryptographicHash::Sha1);
    cacheKey.append(char(rhi ? int(rhi->backend()) : -1));
    if (const auto it = baked.constFind(cacheKey); it != baked.constEnd()) {
        *shader = *it;
        return true;
    }

    QShaderBaker baker;
    baker.setSourceString(utf8, QShader::ComputeStage);
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders(targetsForBackend(rhi, {
        {QShader::SpirvShader, QShaderVersion(100)},
        {QShader::SpirvShader, QShaderVersion(130)},
        {QShader::GlslShader,  QShaderVersion(430)},
        {QShader::GlslShader,  QShaderVersion(460)},
        {QShader::GlslShader,  QShaderVersion(310, QShaderVersion::GlslEs)},
        {QShader::GlslShader,  QShaderVersion(320, QShaderVersion::GlslEs)},
        {QShader::MslShader,   QShaderVersion(20)},
        {QShader::HlslShader,  QShaderVersion(50)},
        {QShader::HlslShader,  QShaderVersion(60)}
    }));
    *shader = baker.bake();
    if (!shader->isValid()) {
        if (error) *error = baker.errorMessage();
        return false;
    }
    // GLSL ES: i compute non hanno una precisione di default (Mali ripiega su
    // mediump) e la riga nel sorgente la toglie SPIRV-Cross: si inserisce
    // nelle varianti, come in GeodesicCalculator.
    for (const QShaderKey &key : shader->availableShaders()) {
        if (key.source() != QShader::GlslShader) continue;
        if (!key.sourceVersion().flags().testFlag(QShaderVersion::GlslEs)) continue;
        QByteArray code = shader->shader(key).shader();
        const int versionPos = code.indexOf("#version");
        const int eol = versionPos < 0 ? -1 : code.indexOf('\n', versionPos);
        if (eol < 0) continue;
        code.insert(eol + 1, "\nprecision highp float;\nprecision highp int;\n");
        QShaderCode patched = shader->shader(key);
        patched.setShader(code);
        shader->setShader(key, patched);
    }
    if (baked.size() >= kBakedComputeMax) baked.clear();
    baked.insert(cacheKey, *shader);
    return true;
}

bool TubeCurveSampler::sample(QRhi *rhi, const QString &curveFunction,
                              const QVector<QVector4D> &params, const float constants[7],
                              QVector<QVector4D> *out, QString *error)
{
    if (out) out->clear();
    if (params.isEmpty()) return true;
    if (!rhi || !rhi->isFeatureSupported(QRhi::Compute)) {
        if (error) *error = QStringLiteral("Compute shaders not available.");
        return false;
    }

    // --- 1. Sorgente: la curva del vertex dentro un compute. Gli script
    // leggono costanti, t e mesh da `ubuf` (GLWidget::generateGlslHelperVars):
    // qui `ubuf` e' una variabile con quei soli campi, scritta per ogni
    // campione prima di valutare la curva.
    const QString source =
        "#version 450\n"
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 0) buffer Samples { vec4 s[]; } samples;\n"
        "layout(std140, binding = 1) uniform Params { vec4 mathParams; vec4 mathParams2; int count; } params;\n"
        "struct TubeCurveScene { vec4 u_mathParams; vec4 u_mathParams2; float u_time; float u_meshIndex; float u_meshCount; };\n"
        "TubeCurveScene ubuf;\n"
        + loadShaderLibrary(QStringLiteral(":/shaders/common.glsl")) + "\n"
        + loadShaderLibrary(QStringLiteral(":/shaders/implicit.glsl")) + "\n"
        + curveFunction + "\n"
        "void main() {\n"
        "    int i = int(gl_GlobalInvocationID.x);\n"
        "    if (i >= params.count) return;\n"
        "    vec4 q = samples.s[i];\n"
        "    ubuf.u_mathParams = params.mathParams;\n"
        "    ubuf.u_mathParams2 = params.mathParams2;\n"
        "    ubuf.u_time = q.z;\n"
        "    ubuf.u_meshIndex = q.y;\n"
        "    ubuf.u_meshCount = q.w;\n"
        "    samples.s[i] = tubeCurve(q.x, 0.0, 0.0);\n"
        "}\n";

    // --- 2. Bake, stessi target del flusso geodetico.
    QShader shader;
    if (!bakeCompute(rhi, source, &shader, error)) return false;

    // --- 3. Risorse: i campioni in un SSBO (entrano come parametri, escono
    // come punti), costanti e conteggio in un UBO std140.
    const int count = params.size();
    const quint32 bufferSize = quint32(count) * sizeof(QVector4D);
    std::unique_ptr<QRhiBuffer> ssbo(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, bufferSize));
    std::unique_ptr<QRhiBuffer> ubo(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 48));
    if (!ssbo->create() || !ubo->create()) {
        if (error) *error = QStringLiteral("Cannot create the sampling buffers.");
        return false;
    }
    std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
    srb->setBindings({
        QRhiShaderResourceBinding::bufferLoadStore(0, QRhiShaderResourceBinding::ComputeStage, ssbo.get()),
        QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::ComputeStage, ubo.get())
    });
    std::unique_ptr<QRhiComputePipeline> pipeline(rhi->newComputePipeline());
    pipeline->setShaderResourceBindings(srb.get());
    pipeline->setShaderStage(QRhiShaderStage(QRhiShaderStage::Compute, shader));
    if (!srb->create() || !pipeline->create()) {
        if (error) *error = QStringLiteral("Cannot create the sampling pipeline.");
        return false;
    }

    // --- 4. Dispatch e lettura sincrona.
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        if (error) *error = QStringLiteral("Cannot start the sampling frame.");
        return false;
    }
    QRhiResourceUpdateBatch *rub = rhi->nextResourceUpdateBatch();
    rub->uploadStaticBuffer(ssbo.get(), params.constData());
    const float ubufData[12] = { constants[0], constants[1], constants[2], constants[6],
                                 constants[3], constants[4], constants[5], 0.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f };
    rub->updateDynamicBuffer(ubo.get(), 0, 32, ubufData);
    const qint32 countValue = count;
    rub->updateDynamicBuffer(ubo.get(), 32, sizeof(qint32), &countValue);

    cb->beginComputePass(rub);
    cb->setComputePipeline(pipeline.get());
    cb->setShaderResources();
    cb->dispatch((count + 63) / 64, 1, 1);
    QRhiResourceUpdateBatch *readbackRub = rhi->nextResourceUpdateBatch();
    QRhiReadbackResult result;
    readbackRub->readBackBuffer(ssbo.get(), 0, bufferSize, &result);
    cb->endComputePass(readbackRub);
    rhi->endOffscreenFrame();
    rhi->finish();

    if (result.data.size() < int(bufferSize)) {
        if (error) *error = QStringLiteral("Cannot read the sampled curve back.");
        return false;
    }
    if (out) {
        out->resize(count);
        memcpy(out->data(), result.data.constData(), bufferSize);
    }
    return true;
}
