#version 440

// --- INPUT DAL VERTEX SHADER ---
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec4 v_normal;
layout(location = 2) in vec2 v_texCoord;
layout(location = 3) in float v_light4D;
layout(location = 4) in float v_spec4D;
// Lato del bordo (0-3), -1 per tutto il resto (vedi surface.vert).
layout(location = 5) in float v_borderSide;
layout(location = 6) in float v_borderReach;
// Punto e normale nella scena prima della rotazione col mouse (texture
// globale, surfaceTextureColor).
layout(location = 7) in vec3 v_objPos;
layout(location = 8) in vec3 v_objNormal;
#ifdef HAS_WIRE_QUAD
// Distanza in pixel dal centro della linea e meta' spessore (vedi surface.vert).
layout(location = 9) in vec2 v_wireDist;
#endif

// --- OUTPUT ---
layout(location = 0) out vec4 FragColor;

layout(std140, binding = 0) uniform SceneUBO {
    mat4 u_mvpMatrix;
    mat4 u_mvMatrix;
    mat4 u_mMatrix;
    vec4 u_dummyZero;
    vec4 u_observerPos;
    vec4 u_cameraPos4D;
    vec4 u_mathParams;
    vec4 u_mathParams2;
    vec3 color;
    float alpha;
    vec3 u_col1;
    float u_lightIntensity;
    vec3 u_col2;
    float u_zoom;
    vec2 u_center;
    float u_rotation;
    float u_omega;
    float u_phi;
    float u_psi;
    float u_time;
    int u_projMode;
    int u_lightingMode;
    int u_renderMode;
    int u_isFlat;
    int useTexture;
    int useSpecular;
    float u_min;
    float u_max;
    float v_min;
    float v_max;
    int u_hasExplicitW;
    // Coda del blocco allineata a UboData (glwidget.h): std140 consente di
    // dichiarare un prefisso, non di saltare campi intermedi.
    int u_raySteps;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
    // MULTI-MESH: indice della parte in corso di disegno (0 se mesh singola).
    float u_meshIndex;
    // 1 = nessuna immagine caricata: vedi _st_sampleChannel0 piu' sotto.
    int u_noImage;
    // Luce di riempimento dall'osservatore, 0 = spenta. Dichiarata qui per far
    // combaciare il blocco col vertex (regola Adreno): il ramo PARAMETRICO usa
    // gia' abs(dot(N,L)), quindi non ha il problema delle facce non illuminate
    // che questa luce risolve nel ray marching, e non la somma.
    float u_fillLight;
    // Spessore del guscio: usato solo dal ray marching, dichiarato qui per
    // tenere i due blocchi identici campo per campo.
    float u_shellThickness;
    // Scelta del marcher (0 = sphere tracing storico, 1 = ibrido): usata solo
    // dal ray marching, dichiarata qui per tenere i due blocchi identici campo
    // per campo (regola Adreno, vedi CLAUDE.md).
    float u_marcherMode;
    // Raggio del tubo: NON USATO QUI (lo usa il vertex), dichiarato per tenere
    // i due blocchi identici campo per campo (regola Adreno, vedi CLAUDE.md).
    float u_tubeRadius;
    // Raggio del bordo: NON USATO QUI (il tubo lo costruisce il vertex),
    // dichiarato per tenere i due blocchi identici (regola Adreno, CLAUDE.md).
    float u_borderRadius;
    // 1 nei blocchi del bordo: texture del bordo (getSceneColor) e niente
    // cutout.
    int u_isBorder;
    // Direzione della sezione del tubo: NON USATA QUI (la usa il vertex),
    // dichiarata per tenere i due blocchi identici campo per campo (regola
    // Adreno, vedi CLAUDE.md).
    float u_tubeRefX;
    float u_tubeRefY;
    float u_tubeRefZ;
    // Numero di copie (campo Meshes): NON USATO QUI, dichiarato per tenere i
    // due blocchi identici (regola Adreno).
    float u_meshCount;
    // Sezione trasportata dei tubi: NON USATI QUI (li leggono vertex e
    // compute), dichiarati per la regola Adreno. Ultimi campi di UboData.
    float u_tubeRings;
    float u_tubeFramesOn;
    float u_tubeFrameBlend;
    // Lati del bordo in comune con un'altra mesh: NON USATO QUI (lo legge il
    // vertex), dichiarato per la regola Adreno.
    float u_sharedEdgesOn;
    // 1 nei blocchi delle parti di una multimesh in ambito All: la texture e'
    // una sola su tutta la figura (surfaceTextureColor). Ultimo campo di
    // UboData.
    float u_globalTexture;
    // NON USATI QUI (il vertex li usa per le strisce del wireframe), ma il blocco
    // deve combaciare col vertex campo per campo (regola Adreno). Ultimi campi di
    // UboData.
    float u_wireHalfWidth;
    float u_wireViewW;
    float u_wireViewH;
} ubuf;

// --- PLACEHOLDER PER CODICE TEXTURE CUSTOM ---
%CUSTOM_CODE%

// --- PLACEHOLDER PER TAGLIO PARETI INTERNE (script dock, sezione CUTOUT) ---
// Fallback: nessun taglio (retrocompatibile con ogni preset che non la definisce).
%CUTOUT_CODE%

// COLORE DELLA TEXTURE nel punto. Di solito la texture sulle coordinate (u, v)
// della parte, 0..1 su ciascuna. TEXTURE GLOBALE (u_globalTexture: multimesh in
// ambito All): una sola su tutta la figura, proiettata dalla scena sui tre
// piani coordinati e mescolata secondo la normale (triplanare), alla scala del
// triplanare delle immagini in Ray Marching (0.5: si ripete ogni 2 unita'; lo
// zoom della vista 2D la regola). Il bordo resta sulle sue coordinate. Una sola
// chiamata alla texture, dentro il ciclo: tre chiamate scritte triplicherebbero
// il codice delle texture pesanti (Mali lo ricopia per ogni punto di chiamata).
// Il peso alla quarta potenza stringe le transizioni: dove una proiezione pesa
// quasi zero non si calcola.
vec3 surfaceTextureColor() {
    bool tri = ubuf.u_globalTexture > 0.5 && ubuf.u_isBorder == 0;
    vec3 w = vec3(1.0, 0.0, 0.0);
    if (tri) {
        vec3 a = abs(v_objNormal);
        a = a * a;
        a = a * a;
        float s = a.x + a.y + a.z;
        w = (s > 1.0e-12) ? a / s : vec3(0.0, 0.0, 1.0);
    }
    vec3 c = vec3(0.0);
    float sum = 0.0;
    for (int k = 0; k < 3; ++k) {
        float wk = (k == 0) ? w.x : ((k == 1) ? w.y : w.z);
        if (wk < 0.02) continue;
        vec2 uv = !tri ? v_texCoord
                : 0.5 * ((k == 0) ? v_objPos.yz : ((k == 1) ? v_objPos.xz : v_objPos.xy));
        c += wk * getCustomColor(uv);
        sum += wk;
    }
    return c / max(sum, 1.0e-12);
}

#ifdef HAS_BORDER
// TRATTI COPERTI DEL BORDO: dove l'orlo passa dentro un'altra mesh (le fasce
// dell'Octahedron Bands) il tubo non si disegna. Un texel per campione del lato
// (passata compute dei lati coperti, GLWidget::buildSharedEdgePipeline), letto
// alla posizione del frammento lungo il lato (v_texCoord.x). Un campione
// scoperto fra due coperti conta come coperto, come nel vertex.
// Il tubo prosegue per tre quarti del raggio dentro il tratto coperto
// (v_borderReach): dove due orli formano un angolo del contorno, un tubo
// tagliato proprio sull'asse dell'altro lasciava vuoto il cuneo esterno.
layout(binding = 4) uniform sampler2D u_sharedEdges;
const int SHARED_EDGE_SAMPLES = 256;   // = GLWidget::kSharedEdgeSamples

bool borderCoveredSample(int row, float s) {
    int m = clamp(int(floor(s * float(SHARED_EDGE_SAMPLES) + 0.5)), 0, SHARED_EDGE_SAMPLES);
    if (texelFetch(u_sharedEdges, ivec2(m, row), 0).x > 0.5) return true;
    return texelFetch(u_sharedEdges, ivec2(max(m - 1, 0), row), 0).x > 0.5
        && texelFetch(u_sharedEdges, ivec2(min(m + 1, SHARED_EDGE_SAMPLES), row), 0).x > 0.5;
}

bool borderFragmentCovered() {
    int side = int(floor(v_borderSide + 0.5));
    if (side < 0 || ubuf.u_sharedEdgesOn < 0.5) return false;
    int row = int(ubuf.u_meshIndex + 0.5) * 4 + side;
    float s = v_texCoord.x;
    return borderCoveredSample(row, s) && borderCoveredSample(row, s - v_borderReach)
        && borderCoveredSample(row, s + v_borderReach);
}
#endif

// --- MAIN (LOGICA DI ILLUMINAZIONE CALIBRATA) ---
void main() {
#ifdef HAS_BORDER
    if (ubuf.u_isBorder != 0 && borderFragmentCovered()) discard;
#endif

    if (ubuf.u_isFlat != 0) {
        vec3 texColor = getCustomColor(v_texCoord);
        FragColor = vec4(texColor, 1.0);
        return;
    }

#ifdef HAS_CUTOUT
    // Taglio pareti interne: stesso (u,v) usato da getRawPosition nel vertex,
    // ricostruito qui dal texCoord normalizzato (v_texCoord.y è invertito nel
    // vertex shader, vedi surface.vert). Iniettato SOLO se il preset definisce
    // una sezione //CUTOUT_BEGIN (vedi createFragmentShaderSource): costo zero
    // per ogni altra superficie, che non passa mai da questo branch.
    // Il bordo no: il suo v_texCoord sono le coordinate della sua texture
    // (lungo il lato, attorno al tubo), non i parametri della superficie.
    float _cutU = mix(ubuf.u_min, ubuf.u_max, v_texCoord.x);
    float _cutV = mix(ubuf.v_min, ubuf.v_max, 1.0 - v_texCoord.y);
    if (ubuf.u_isBorder == 0 && cutHere(_cutU, _cutV)) {
        discard;
    }
#endif

    // Scompattiamo i parametri matematici
    // Scompattiamo i parametri matematici
    float A = ubuf.u_mathParams.x;
    float B = ubuf.u_mathParams.y;
    float C = ubuf.u_mathParams.z;
    float D = ubuf.u_mathParams2.x;
    float E = ubuf.u_mathParams2.y;
    float F = ubuf.u_mathParams2.z;
    float s = ubuf.u_mathParams.w;

    // Se siamo in modalità Wireframe (2) o Bordi (5), usa colore piatto senza luci
    if (ubuf.u_renderMode == 2 || ubuf.u_renderMode == 5) {
#ifdef HAS_WIRE_QUAD
        // Striscia del wireframe: copertura del pixel dalla distanza dal centro, che
        // da' il bordo sfumato (antialiasing della linea).
        const float coverage = clamp(v_wireDist.y - abs(v_wireDist.x) + 0.5, 0.0, 1.0);
        FragColor = vec4(ubuf.color, ubuf.alpha * coverage);
#else
        FragColor = vec4(ubuf.color, ubuf.alpha);
#endif
        return;
    }

    // --- 1. SETUP VETTORI CON SAFETY CHECKS ---
    vec3 N;

    // Usiamo sempre le normali interpolate dal Vertex Shader (Smooth Shading nativo)
    float lenN = length(v_normal.xyz);
    if (lenN > 0.0001) {
        N = normalize(v_normal.xyz);
    } else {
        N = vec3(0.0, 0.0, 1.0);
    }

#ifdef CUSTOM_MESH
    // Manteniamo il fix di orientamento visivo per le mesh custom
    if (dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) {
        N = -N;
    }
#endif

    vec3 V;
    float lenPos = length(v_pos);
    if (lenPos > 0.0001) {
        V = normalize(-v_pos);     // Vista
    } else {
        V = vec3(0.0, 0.0, 1.0);
    }

    vec3 L = vec3(0.0, 0.0, 1.0);   // Luce frontale
    vec3 H = normalize(L + V);      // Vettore mediano

    // --- 2. ILLUMINAZIONE DIFFUSA ---
    float diff = abs(dot(N, L));
    diff = 0.3 + 0.7 * diff;

    // --- 3. ILLUMINAZIONE SPECULARE (PHONG) CALIBRATA ---
    float spec = 0.0;
    if (ubuf.useSpecular != 0) {
        float specAngle = abs(dot(N, H));
        specAngle = clamp(specAngle, 0.0, 1.0);
        spec = pow(specAngle, 64.0);
        spec *= 0.3;
    }

    // --- 4. MIX ILLUMINAZIONE 4D E SPECULARE ----
    // Applica l'intensità globale alla luce diffusa base
    float lightingMix = diff * ubuf.u_lightIntensity;

    if (ubuf.useSpecular != 0) {
        // Applica l'intensità globale anche alla componente speculare
        spec *= ubuf.u_lightIntensity;
    }

    if (ubuf.u_lightingMode > 0) {
        lightingMix *= v_light4D;

        spec *= 0.2;
        // Mantiene un lieve riflesso 3D per percepire il volume globale

        // Aggiunge la luce speculare nativa del 4D se il tasto Phong è premuto
        if (ubuf.useSpecular != 0) {
            // Anche la speculare 4D è soggetta allo slider della luce!
            spec += v_spec4D * 0.8 * ubuf.u_lightIntensity;
        }
    }

    // --- 5. COMPOSIZIONE COLORE ---
    vec3 finalRGB = ubuf.color;
    if (ubuf.useTexture != 0) {
        vec4 tex = vec4(surfaceTextureColor(), 1.0);
        finalRGB *= tex.rgb;
    }

    finalRGB *= lightingMix;
    finalRGB += vec3(spec);

    // ---> FIX: TONE MAPPING SOLO PER LUCI 4D <---
    // Applichiamo la protezione anti-bruciatura solo se siamo in Radial, Observer o Slice (u_lightingMode > 0)
    // così la modalità 3D base torna alla sua luminosità originale brillante!
    if (ubuf.u_lightingMode > 0) {
        finalRGB = vec3(1.0) - exp(-finalRGB * 1.5); // Portato a 1.5 per mantenere un po' più di luminosità
    }

    FragColor = vec4(finalRGB, ubuf.alpha);
}
