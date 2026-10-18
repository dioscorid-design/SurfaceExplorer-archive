#version 440

// INPUT
layout(location = 0) in vec4 vertex;
layout(location = 1) in vec4 normal;
layout(location = 2) in vec2 texCoord;

// OUTPUT
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec4 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out float v_light4D;
layout(location = 4) out float v_spec4D;
// Lato del bordo a cui appartiene il vertice (0-3), -1 per tutto il resto: il
// fragment scarta i tratti coperti del lato (borderFragmentCovered).
layout(location = 5) out float v_borderSide;
// Quanto il tubo prosegue oltre l'inizio di un tratto coperto, in frazione del
// lato: tre quarti del raggio (vedi borderFragmentCovered).
layout(location = 6) out float v_borderReach;
// Punto e normale nella scena prima della rotazione col mouse: la texture
// GLOBALE (multimesh in ambito All, u_globalTexture) li proietta sui tre piani
// (surfaceTextureColor nel fragment), cosi' resta ferma sulla figura girandola.
layout(location = 7) out vec3 v_objPos;
layout(location = 8) out vec3 v_objNormal;

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
    // Da qui in poi il blocco deve rispecchiare la coda di UboData (glwidget.h):
    // std140 permette di dichiarare solo un PREFISSO del blocco, ma non di
    // saltare campi in mezzo. u_meshIndex sta dopo i limiti x/y/z, quindi questi
    // vanno dichiarati anche se il vertex shader non li usa.
    int u_raySteps;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    float z_min;
    float z_max;
    // MULTI-MESH: indice della parte in corso di disegno (0 se mesh singola).
    // Permette allo script di distinguere il ramo.
    float u_meshIndex;
    // NON USATO QUI, ma il blocco deve combaciare campo per campo con quello del
    // fragment: Adreno rifiuta il link con "Uniform ubuf type mismatch with other
    // stage" se i due stage dichiarano lo stesso binding con code diverse, e
    // fallivano TUTTE le pipeline (schermo vuoto su Android, desktop ok perche'
    // li' il prefisso std140 e' tollerato). Ultimo campo di UboData: aggiungerlo
    // non sposta nessun offset.
    int u_noImage;
    // NON USATO QUI (la luce di riempimento e' un termine del fragment), ma il
    // blocco deve combaciare campo per campo con quello del fragment: vedi la
    // nota su u_noImage qui sopra. Ultimo campo di UboData.
    float u_fillLight;
    // NON USATA QUI (il guscio e' solo del ray marching), ma il blocco deve
    // combaciare col fragment. Ultimo campo di UboData.
    float u_shellThickness;
    // NON USATA QUI (la scelta del marcher e' solo del ray marching), ma il
    // blocco deve combaciare col fragment.
    float u_marcherMode;
    // Raggio del tubo (sotto-tab Tubes): lo legge il getRawPosition generato
    // per i tubi (GLWidget::createVertexShaderSource).
    float u_tubeRadius;
    // Raggio del bordo (Border Thickness): lo usa solo il ramo HAS_BORDER qui
    // sotto.
    float u_borderRadius;
    // NON USATO QUI (lo legge il fragment per la texture del bordo), ma il
    // blocco deve combaciare campo per campo col fragment.
    int u_isBorder;
    // Direzione di riferimento della sezione del tubo, per parte: la legge il
    // ramo dei tubi (tubeSceneFrame).
    float u_tubeRefX;
    float u_tubeRefY;
    float u_tubeRefZ;
    // Numero di copie (campo Meshes): `meshes` nelle equazioni e negli script.
    float u_meshCount;
    // Sezione trasportata dei tubi: anelli della parte e 1 se anelli e
    // sezione sono in u_tubeFrames (tubeTransportedRing). Ultimi campi di
    // UboData.
    float u_tubeRings;
    float u_tubeFramesOn;
    // Smorzamento della sezione dei tubi: lo legge solo il compute.
    float u_tubeFrameBlend;
    // 1 se u_sharedEdges e' valida in questo frame (borderEdgeShared).
    // Ultimo campo di UboData.
    float u_sharedEdgesOn;
    // Texture globale (multimesh in ambito All): NON USATO QUI (lo legge il
    // fragment), dichiarato per la regola Adreno. Ultimo campo di UboData.
    float u_globalTexture;
} ubuf;

float sq(float x) { return x*x; }

// NB: safePow(base, expo) — pow() sicura per basi negative — è definita e iniettata
// da GLWidget (createVertexShaderSource) PRIMA di questo template, ed è quella che il
// traduttore usa al posto di pow(. Qui non va ridefinita.

// Bonifica NaN/Inf da una posizione calcolata: equazioni numericamente delicate
// (sqrt/acos/pow con argomenti al limite, es. il preset "Fresnel") possono uscire
// dal dominio valido su GPU con precisione diversa (tipico Android Mali/Adreno vs
// desktop/Metal) e produrre NaN/Inf -> i vertici schizzano all'infinito e la mesh
// "esplode" in triangoli. Sostituiamo i componenti non finiti con 0, così un vertice
// degenere collassa in un punto finito invece di sparare via.
// NB: usiamo il test `!(abs <= 1e6)` invece di isnan()/isinf(): è FALSO per ogni
// confronto con NaN (quindi !false=true -> scartato) e cattura anche Inf/valori
// enormi, restando affidabile anche sui driver Android con fast-math che ignorano
// isnan()/isinf().
vec4 sanitizePos(vec4 p) {
    bvec4 ok = lessThanEqual(abs(p), vec4(1.0e6));
    return mix(vec4(0.0), p, vec4(ok));
}

// --- MATRICI DI ROTAZIONE 4D ---
vec4 rotateXW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x*c + p.w*s, p.y, p.z, -p.x*s + p.w*c);
}
vec4 rotateYW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x, p.y*c + p.w*s, p.z, -p.y*s + p.w*c);
}
vec4 rotateZW(vec4 p, float t) {
    float c = cos(t); float s = sin(t);
    return vec4(p.x, p.y, p.z*c + p.w*s, -p.z*s + p.w*c);
}

// --- STEREOGRAFICA SU IPERSFERA (S^3) ---
// Unica implementazione: la usano sia project4D() (normali e derivate) sia il
// calcolo della posizione finale in main(). Erano due copie identiche della
// stessa formula, e una divergenza fra loro sfalserebbe le normali rispetto
// alla geometria.
//
// Il raggio proiettato e' sqrt((1+w)/(1-w)) e deve CRESCERE all'infinito verso
// l'antipode della camera. Il clamp storico agiva sul denominatore
// (denom < 0.05 -> 0.05), quindi oltre w = 0.95 (18.2 gradi dall'antipode) il
// raggio DECRESCEVA fino a 0: la calotta antipodale veniva ripiegata verso la
// camera con le facce invertite, e il punto di fuga dei tunnel restava
// scoperto (buco tondo sullo sfondo volando dentro una superficie che
// circonda l'antipode). Qui il clamp e' MONOTONO e agisce sul risultato:
// sotto w = 0.95 la formula e' algebricamente identica a quella storica,
// quindi nessuna figura cambia aspetto; cambia solo cio' che prima era
// ripiegato.
//
// Il tetto 12 non e' arbitrario: il far plane vale 25 * distanza della camera
// (glwidget.cpp), e fra i preset in stereografica la camera piu' vicina e'
// quella di Clifford Torus a 0.5, cioe' far plane 12.5. Con 12 nessun preset
// della libreria puo' vedere la calotta antipodale sparire oltre il far plane,
// che sarebbe l'unica regressione possibile rispetto al vecchio clamp (il
// quale, ripiegando, teneva tutto entro 6.24 e quindi non tagliava mai).
vec3 stereoS3(vec4 p) {
    float r = length(p);
    if (r < 0.0001) return vec3(0.0);

    vec4 pNorm = p / r;
    float sxy = length(pNorm.xyz);
    float rad = min(sqrt((1.0 + pNorm.w) / max(1.0 - pNorm.w, 1.0e-6)), 12.0);
    vec3 dir = (sxy > 1.0e-6) ? pNorm.xyz / sxy : vec3(0.0, 0.0, 1.0);

    return dir * (r * rad);
}

// --- PROIEZIONE 4D -> 3D CON OSSERVATORE ---
vec3 project4D(vec4 p) {
    if (ubuf.u_projMode == 0) {
        return p.xyz;
    }
    else if (ubuf.u_projMode == 2) {
        return stereoS3(p);
    }

    vec4 obs = ubuf.u_observerPos;
    float denom = obs.w - p.w;
    if (abs(denom) < 0.0001) {
        return p.xyz * 10.0;
    }

    float wFactor = obs.w / denom;
    float x = (p.x - obs.x) * wFactor + obs.x;
    float y = (p.y - obs.y) * wFactor + obs.y;
    float z = (p.z - obs.z) * wFactor + obs.z;
    return vec3(x, y, z);
}

// --- FUNZIONE UTENTE ---
vec4 getRawPosition(float u, float v, float w) {
#ifdef CUSTOM_MESH
    // SE SIAMO IN MODALITÀ GEODESIC FLOW, PRENDIAMO IL VERTICE DIRETTAMENTE DAL BUFFER
    return vertex;
#else
    // ALTRIMENTI FACCIAMO I CALCOLI CLASSICI
    float A = ubuf.u_mathParams.x;
    float B = ubuf.u_mathParams.y;
    float C = ubuf.u_mathParams.z;
    float D = ubuf.u_mathParams2.x;
    float E = ubuf.u_mathParams2.y;
    float F = ubuf.u_mathParams2.z;
    float s = ubuf.u_mathParams.w;
    float t = ubuf.u_time;
    // Multi-mesh dal campo Meshes: quale copia (0..meshes-1) e quante.
    float mesh = ubuf.u_meshIndex;
    float meshes = ubuf.u_meshCount;

    float x = %X_EQ%;
    float y = %Y_EQ%;
    float z = %Z_EQ%;
    float p = %W_EQ%;
    return sanitizePos(vec4(x, y, z, p));
#endif
}

// --- FUNZIONE INIETTATA DA C++ ---
float getExplicitVal(float a, float b) {
    // Qui il C++ inietterà: "float u=a; float v=b; return (sin(u)*cos(v));" o simile
    %EXPLICIT_BODY%
}

vec4 getRawPositionAuto(float r1, float r2) {
    // 0=W(u,v), 1=U(v,w), 2=V(u,w)
    int mode = %CONSTRAINT_MODE%;

    float u, v, w;

    if (mode == 1) { // Constraint U
        v = r1;
        w = r2;
        u = getExplicitVal(v, w); // Calcola U
    }
    else if (mode == 2) { // Constraint V
        u = r1;
        w = r2;
        v = getExplicitVal(u, w); // Calcola V
    }
    else { // Constraint W (Default)
            u = r1;
            v = r2;
            // Se c'è un'equazione esplicita W, la calcola, altrimenti 0
            w = (ubuf.u_hasExplicitW != 0) ? getExplicitVal(u, v) : 0.0;
        }

    return getRawPosition(u, v, w);
}

// Le rotazioni 4D dell'oggetto. Unica implementazione: la usano i punti della
// superficie e, nei tubi, la direzione che orienta la sezione.
vec4 rotate4D(vec4 p) {
    if (ubuf.u_omega != 0.0) p = rotateXW(p, ubuf.u_omega);
    if (ubuf.u_phi   != 0.0) p = rotateYW(p, ubuf.u_phi);
    if (ubuf.u_psi   != 0.0) p = rotateZW(p, ubuf.u_psi);
    return p;
}

// Funzione completa (Solo Object Rotation)
vec4 getRotatedPoint4D(float u, float v) {
    return rotate4D(getRawPositionAuto(u, v));
}

// Estrae i due vettori ortonormali che formano il piano tangente in 4D
void getTangentPlane(float u, float v, out vec4 Tu, out vec4 Tv) {
    float eps = 0.0005;
    vec4 Tu_raw = getRotatedPoint4D(u + eps, v) - getRotatedPoint4D(u - eps, v);
    vec4 Tv_raw = getRotatedPoint4D(u, v + eps) - getRotatedPoint4D(u, v - eps);

    float lenU = length(Tu_raw);
    float lenV = length(Tv_raw);

    // FIX UNIVERSALE: Limite matematico (Soglia molto stretta, agisce solo sul vertice esatto)
    if (lenU < 1.0e-6 || lenV < 1.0e-6) {
        float tiny = 0.001;

        // Esploriamo la pendenza spostandoci microscopicamente su V (preserva la simmetria)
        Tu_raw = getRotatedPoint4D(u + eps, v + tiny) - getRotatedPoint4D(u - eps, v + tiny);
        Tv_raw = getRotatedPoint4D(u, v + tiny + eps) - getRotatedPoint4D(u, v + tiny - eps);

        lenU = length(Tu_raw);
        lenV = length(Tv_raw);

        // Fallback estremo sull'altro asse se la singolarità era invertita
        if (lenU < 1.0e-6 || lenV < 1.0e-6) {
            Tu_raw = getRotatedPoint4D(u + tiny + eps, v) - getRotatedPoint4D(u + tiny - eps, v);
            Tv_raw = getRotatedPoint4D(u + tiny, v + eps) - getRotatedPoint4D(u + tiny, v - eps);
            lenU = length(Tu_raw);
        }
    }

    Tu = (lenU > 1.0e-8) ? Tu_raw / lenU : vec4(1.0, 0.0, 0.0, 0.0);

    vec4 Tv_ortho = Tv_raw - Tu * dot(Tv_raw, Tu);
    float lenV_ortho = length(Tv_ortho);
    Tv = (lenV_ortho > 1.0e-8) ? Tv_ortho / lenV_ortho : vec4(0.0, 1.0, 0.0, 0.0);
}

vec4 calculate4DNormal_Slice(float u, float v) {
    float eps = 0.0005;
    vec4 P = getRotatedPoint4D(u, v);

    vec4 dPu = getRotatedPoint4D(u + eps, v) - getRotatedPoint4D(u - eps, v);
    vec4 dPv = getRotatedPoint4D(u, v + eps) - getRotatedPoint4D(u, v - eps);

    float lenU = length(dPu);
    float lenV = length(dPv);

    if (lenU < 1.0e-6 || lenV < 1.0e-6) {
        float tiny = 0.001;
        dPu = getRotatedPoint4D(u + eps, v + tiny) - getRotatedPoint4D(u - eps, v + tiny);
        dPv = getRotatedPoint4D(u, v + tiny + eps) - getRotatedPoint4D(u, v + tiny - eps);

        if (length(dPu) < 1.0e-6 || length(dPv) < 1.0e-6) {
            dPu = getRotatedPoint4D(u + tiny + eps, v) - getRotatedPoint4D(u + tiny - eps, v);
            dPv = getRotatedPoint4D(u + tiny, v + eps) - getRotatedPoint4D(u + tiny, v - eps);
        }
    }

    vec3 dU = length(dPu.xyz) > 1.0e-8 ? normalize(dPu.xyz) : vec3(1.0, 0.0, 0.0);
    vec3 dV = length(dPv.xyz) > 1.0e-8 ? normalize(dPv.xyz) : vec3(0.0, 1.0, 0.0);

    vec3 raw_N = cross(dU, dV);
    vec3 N_visual;
    if (dot(raw_N, raw_N) < 1.0e-8) {
        N_visual = vec3(0.0, 0.0, 1.0);
    } else {
        N_visual = normalize(raw_N);
    }

    float sliceFactor = 0.0;

    if (ubuf.u_hasExplicitW != 0) {
        // CASO 1: Volume esplicito (usa la casella w=h(u,v))
        float w_base = getExplicitVal(u, v);
        vec4 P_w = getRawPosition(u, v, w_base + eps);

        if (ubuf.u_omega != 0.0) P_w = rotateXW(P_w, ubuf.u_omega);
        if (ubuf.u_phi   != 0.0) P_w = rotateYW(P_w, ubuf.u_phi);
        if (ubuf.u_psi   != 0.0) P_w = rotateZW(P_w, ubuf.u_psi);

        vec4 dPw = P_w - P;
        vec3 dW = normalize(dPw.xyz);
        sliceFactor = dot(N_visual, dW);
    } else {
            // CASO 2: Superficie 4D pura
            float wTilt = length(vec2(dPu.w, dPv.w));
            float totalLength = length(dPu) + length(dPv) + 0.0001;

            sliceFactor = wTilt / totalLength;
            sliceFactor = smoothstep(0.0, 0.4, sliceFactor);
        }

    return vec4(N_visual, abs(sliceFactor));
}

vec3 calculateFinalNormal(float u, float v) {
    float eps = 0.001;
    vec3 pU_plus  = project4D(getRotatedPoint4D(u + eps, v));
    vec3 pU_minus = project4D(getRotatedPoint4D(u - eps, v));
    // ---> FIX: Moltiplichiamo per 500 per evitare l'underflow nel cross product!
    vec3 tangentU = (pU_plus - pU_minus) * 500.0;

    vec3 pV_plus  = project4D(getRotatedPoint4D(u, v + eps));
    vec3 pV_minus = project4D(getRotatedPoint4D(u, v - eps));
    vec3 tangentV = (pV_plus - pV_minus) * 500.0;

    vec3 N = cross(tangentU, tangentV);

    // Se la normale collassa (siamo sul polo / singolarità)
    if (dot(N, N) < 1.0e-8) {
        // Strategia: spostiamoci LONTANO dal polo (offset grande, ASIMMETRICO),
        // poi facciamo differenze in avanti standard nel nuovo punto.
        // Proviamo le 4 direzioni cardinali: la prima che produce una
        // normale non degenere vince.
        float pole_off = 0.05;
        float eps2     = 0.005;

        vec3 p0, pU, pV;

        // Tentativo 1: offset in +u
        p0 = project4D(getRotatedPoint4D(u + pole_off, v));
        pU = project4D(getRotatedPoint4D(u + pole_off + eps2, v));
        pV = project4D(getRotatedPoint4D(u + pole_off, v + eps2));
        N  = cross(pU - p0, pV - p0);

        // Tentativo 2: offset in -u
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u - pole_off, v));
            pU = project4D(getRotatedPoint4D(u - pole_off + eps2, v));
            pV = project4D(getRotatedPoint4D(u - pole_off, v + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        // Tentativo 3: offset in +v
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u, v + pole_off));
            pU = project4D(getRotatedPoint4D(u + eps2, v + pole_off));
            pV = project4D(getRotatedPoint4D(u, v + pole_off + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        // Tentativo 4: offset in -v
        if (dot(N, N) < 1.0e-8) {
            p0 = project4D(getRotatedPoint4D(u, v - pole_off));
            pU = project4D(getRotatedPoint4D(u + eps2, v - pole_off));
            pV = project4D(getRotatedPoint4D(u, v - pole_off + eps2));
            N  = cross(pU - p0, pV - p0);
        }

        if (dot(N, N) < 1.0e-8) return vec3(0.0, 0.0, 1.0);
    }

    return -normalize(N);
}

// Posizione nella scena 3D di un punto 4D gia' ruotato. Unica implementazione
// della proiezione del vertice: la usa main() per la superficie e il bordo, che
// deve cadere esattamente sui lati della superficie disegnata (due copie
// potrebbero divergere, come gia' visto per la stereografica).
vec3 projectToScene(vec4 pRot) {
    if (ubuf.u_projMode == 0) return pRot.xyz;
    // --- STEREOGRAFICA SU IPERSFERA (S^3) ---
    if (ubuf.u_projMode == 2) return stereoS3(pRot);
    // --- PROSPETTIVA CENTRALE ---
    vec4 obs = ubuf.u_observerPos;
    if (abs(obs.w - pRot.w) < 0.1) {
        obs.w += 0.1;
    }
    float denom = obs.w - pRot.w;
    if (abs(denom) < 0.15) {
        denom = (denom >= 0.0) ? 0.15 : -0.15;
    }
    float wFactor = obs.w / denom;
    return obs.xyz + (pRot.xyz - obs.xyz) * wFactor;
}

#ifdef IS_TUBE
// TUBO ATTORNO A UNA CURVA (sotto-tab Tubes). Qui getRawPosition, generata da
// GLWidget::createVertexShaderSource, restituisce il punto della CURVA C(u)
// (v non conta); il tubo si costruisce nello spazio 3D FINALE, come il bordo:
// la curva passa per le rotazioni 4D e la proiezione, e il cerchio di raggio
// u_tubeRadius si disegna attorno alla curva PROIETTATA, nel piano normale
// alla sua tangente. Cosi' la sezione resta rotonda con qualunque rotazione
// 4D: costruito in 4D prima della proiezione, il cerchio si vedeva di taglio
// ai gomiti e si schiacciava. Per una curva 3D non cambia nulla.
// La direzione che orienta la sezione (u_tubeRef, scelta al Run lontana da
// tutte le tangenti della curva) subisce la STESSA trasformazione della
// curva: la sezione gira con la figura, e texture e wireframe restano solidali
// al tubo. Se la direzione trasformata diventa parallela alla tangente, si
// ripiega su un asse.
// SEZIONE TRASPORTATA. Con la rotazione 4D e la proiezione gli angoli non si
// conservano: in qualche punto, in qualche istante, la direzione trasformata
// finiva quasi parallela alla tangente proiettata, la sezione girava di quasi
// mezzo giro in pochi anelli e il tubo si strozzava (pizzicature). Ogni
// orientamento calcolato punto per punto ha punti cosi'. Percio', quando la
// passata compute (GLWidget::buildTubeFramePipeline) e' disponibile, la
// direzione viene dal TRASPORTO PARALLELO lungo la curva proiettata, rifatto a
// ogni frame: la sezione passa da un anello al successivo senza ruotare, e
// sulle curve chiuse lo scarto si ripartisce lungo il giro. Quella locale qui
// sotto resta il riferimento della partenza (primo anello) e la riserva.
// ANELLI DOVE SERVONO. La stessa passata sceglie anche DOVE stanno gli
// anelli: uniformi in u dove la curva e' tranquilla, fitti dove la tangente
// gira in fretta (alle cuspidi la punta si chiudeva piatta, con una tacca).
// L'anello i della griglia si disegna al parametro e con la tangente che la
// passata gli ha dato; la texture segue quel parametro.
#ifndef TUBE_FRAME_COMPUTE
layout(binding = 2) uniform sampler2D u_tubeFrames;

// Anello in u: una riga per tubo (u_meshIndex), due texel per anello (sezione
// + parametro dell'anello, tangente), interpolati fra i due anelli vicini (il
// bordo cade anche fra due).
void tubeTransportedRing(float u, out float uRing, out vec3 F, out vec3 T) {
    float last = max(ubuf.u_tubeRings - 1.0, 1.0);
    float span = ubuf.u_max - ubuf.u_min;
    float f = (abs(span) > 1.0e-12) ? clamp((u - ubuf.u_min) / span * last, 0.0, last) : 0.0;
    int i0 = int(floor(f));
    int i1 = min(i0 + 1, int(last));
    int row = int(ubuf.u_meshIndex + 0.5);
    vec4 a = texelFetch(u_tubeFrames, ivec2(2 * i0, row), 0);
    vec4 b = texelFetch(u_tubeFrames, ivec2(2 * i1, row), 0);
    vec3 ta = texelFetch(u_tubeFrames, ivec2(2 * i0 + 1, row), 0).xyz;
    vec3 tb = texelFetch(u_tubeFrames, ivec2(2 * i1 + 1, row), 0).xyz;
    float k = f - float(i0);
    uRing = mix(a.w, b.w, k);
    F = mix(a.xyz, b.xyz, k);
    T = mix(ta, tb, k);
}
#endif

// Tangente della curva proiettata in u (e direzione 4D T4 della curva
// ruotata): unica sede, la usano l'orientamento locale e i campioni del
// compute che distribuiscono gli anelli.
vec3 tubeProjectedTangent(float u, out vec4 T4) {
    const float e = 0.001;
    vec4 Cp = getRotatedPoint4D(u + e, 0.0);
    vec4 Cm = getRotatedPoint4D(u - e, 0.0);
    T4 = Cp - Cm;
    T4 = (dot(T4, T4) > 1.0e-20) ? normalize(T4) : vec4(1.0, 0.0, 0.0, 0.0);
    vec3 T = projectToScene(Cp) - projectToScene(Cm);
    float lenT = length(T);
    return (lenT > 1.0e-10) ? T / lenT : vec3(1.0, 0.0, 0.0);
}

// d riportata sul piano normale a T (unitaria); se e' quasi parallela a T, un
// asse.
vec3 tubeNormalTo(vec3 d, vec3 T) {
    vec3 q = d - dot(d, T) * T;
    if (dot(q, q) < 1.0e-8) {
        vec3 alt = (abs(T.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        q = alt - dot(alt, T) * T;
    }
    return normalize(q);
}

// Orientamento LOCALE in u: punto, tangente proiettata Tn e direzione E1 della
// sezione dalla direzione di riferimento. `trust` dice quanto E1 e' affidabile
// (seno al quadrato dell'angolo fra riferimento e tangente): vicino a 0 la
// direzione gira in fretta, e col ripiego sull'asse vale 0. Lo usa il compute
// per non farsi trascinare dalla partenza quando non e' affidabile.
void tubeLocalFrame(float u, out vec3 C, out vec3 Tn, out vec3 E1, out vec4 C4, out vec4 T4,
                    out float trust) {
    const float e = 0.001;
    vec4 raw = getRawPositionAuto(u, 0.0);
    C4 = rotate4D(raw);
    C = projectToScene(C4);
    // T4: direzione 4D della curva ruotata, la usa la luce 4D (tubeLight4D).
    Tn = tubeProjectedTangent(u, T4);
    vec4 R4 = vec4(ubuf.u_tubeRefX, ubuf.u_tubeRefY, ubuf.u_tubeRefZ, 0.0);
    vec3 R = projectToScene(rotate4D(raw + e * R4)) - C;
    E1 = R - dot(R, Tn) * Tn;
    float rr = dot(R, R);
    trust = (rr > 1.0e-16) ? clamp(dot(E1, E1) / rr, 0.0, 1.0) : 0.0;
    if (dot(E1, E1) < 1.0e-4 * rr || rr < 1.0e-16) {
        vec3 alt = (abs(Tn.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        E1 = alt - dot(alt, Tn) * Tn;
        trust = 0.0;
    }
    E1 = normalize(E1);
}

// Anello del tubo per il vertice di parametro u della griglia: centro C,
// sezione E1/E2 e il parametro uAt della curva dove l'anello sta davvero
// (diverso da u solo con gli anelli distribuiti dalla passata compute).
void tubeSceneFrame(float u, out vec3 C, out vec3 E1, out vec3 E2, out vec4 C4, out vec4 T4,
                    out float uAt) {
    vec3 Tn;
    float trust;
    uAt = u;
#ifndef TUBE_FRAME_COMPUTE
    if (ubuf.u_tubeFramesOn > 0.5) {
        vec3 F, T;
        tubeTransportedRing(u, uAt, F, T);
        tubeLocalFrame(uAt, C, Tn, E1, C4, T4, trust);
        if (dot(T, T) > 1.0e-8) Tn = normalize(T);
        E1 = tubeNormalTo((dot(F, F) > 1.0e-8) ? F : E1, Tn);
        E2 = cross(Tn, E1);
        return;
    }
#endif
    tubeLocalFrame(u, C, Tn, E1, C4, T4, trust);
    E2 = cross(Tn, E1);
}

// Il punto (u, v) del tubo nella scena: lo usa il bordo, che sui tubi deve
// cadere sulla superficie del tubo, non sulla curva.
vec3 tubeScenePoint(float u, float v) {
    vec3 C, E1, E2;
    vec4 C4, T4;
    float uAt;
    tubeSceneFrame(u, C, E1, E2, C4, T4, uAt);
    return C + ubuf.u_tubeRadius * (cos(v) * E1 + sin(v) * E2);
}

// LUCE 4D DEL TUBO (dock 4D: Directional, Observer). Stesse formule delle
// superfici in main(): la luce e' la parte della direzione 4D della luce che
// esce dal piano tangente. Il piano tangente del tubo e' quello della
// direzione 4D della curva (T4) e della direzione attorno al tubo, portata in
// 4D come direzione della scena (w = 0: il cerchio vive nella scena). Cosi'
// la luminosita' segue dove la curva va in w e cambia ruotando in 4D. Slice
// si basa sulla sezione 3D di una superficie e per una curva non ha un
// equivalente: sui tubi vale come Directional.
void tubeLight4D(vec4 C4, vec4 T4, vec3 around) {
    v_light4D = 1.0;
    v_spec4D = 0.0;
    if (ubuf.u_lightingMode <= 0) return;
    vec4 S4 = vec4(around, 0.0);
    S4 -= T4 * dot(S4, T4);
    S4 = (dot(S4, S4) > 1.0e-12) ? normalize(S4) : vec4(0.0);
    vec4 lightDir4D = normalize(vec4(1.0, 1.0, 1.0, 0.5));
    float attenuation = 1.0;
    if (ubuf.u_lightingMode == 2) {   // OBSERVER
        vec4 toObserver = ubuf.u_observerPos - C4;
        float dist = length(toObserver);
        if (dist < 0.001) return;
        lightDir4D = toObserver / dist;
        attenuation = 2.0 / (1.0 + 0.2 * dist + 0.02 * dist * dist);
    }
    vec4 viewDir4D = normalize(ubuf.u_cameraPos4D - C4);
    vec4 L_norm = lightDir4D - T4 * dot(lightDir4D, T4) - S4 * dot(lightDir4D, S4);
    v_light4D = clamp(0.2 + 0.8 * length(L_norm) * attenuation, 0.1, 1.5);
    vec4 H = normalize(lightDir4D + viewDir4D);
    vec4 H_norm = H - T4 * dot(H, T4) - S4 * dot(H, S4);
    v_spec4D = pow(length(H_norm), 32.0) * 0.5 * attenuation;
}

void emitTubeVertex() {
    float u = vertex.x;
    float v = vertex.y;
    vec3 C, E1, E2;
    vec4 C4, T4;
    float uAt;
    tubeSceneFrame(u, C, E1, E2, C4, T4, uAt);
    vec3 n = cos(v) * E1 + sin(v) * E2;
    vec3 pos = C + ubuf.u_tubeRadius * n;

    v_pos = vec3(ubuf.u_mvMatrix * vec4(pos, 1.0));
    v_normal = ubuf.u_mvMatrix * vec4(n, 0.0);
    v_objPos = pos;
    v_objNormal = n;
    // Texture (e taglio, che rilegge u da qui) sul parametro dove l'anello sta.
    float span = ubuf.u_max - ubuf.u_min;
    float s = texCoord.x + ((abs(span) > 1.0e-12) ? (uAt - u) / span : 0.0);
    v_texCoord = vec2(s, 1.0 - texCoord.y);
    tubeLight4D(C4, T4, -sin(v) * E1 + cos(v) * E2);

    vec4 clip = ubuf.u_mvpMatrix * vec4(pos, 1.0);
    // Stesso spareggio di profondita' 4D di main().
    clip.z -= clamp(C4.w, -1.0, 1.0) * 0.0005 * clip.w;
    clip += ubuf.u_dummyZero * 0.000001;
    gl_Position = clip;
}
#endif

#if defined(CUSTOM_MESH) && defined(HAS_BORDER)
// LATI DELLA MESH CUSTOM (flusso geodetico). I suoi vertici sono posizioni, e
// il punto in (u, v) qui non si sa calcolare. GLWidget::setBorderEdges carica
// i quattro lati della griglia, ciascuno con la riga accanto verso l'interno
// (righe 0-7, nell'ultima numU e numV): il punto si legge dal lato piu' vicino
// in passi di griglia, interpolato lungo il lato e verso la riga accanto. Il
// bordo chiede solo punti sui lati o a un soffio dentro (tangente, direzione
// verso l'interno, test delle cuciture sul lato opposto).
layout(binding = 3) uniform sampler2D u_borderEdges;

vec4 borderEdgeSample(int row, float s, float n) {
    float x = clamp(s, 0.0, 1.0) * n;
    int i = clamp(int(floor(x)), 0, int(n) - 1);
    return mix(texelFetch(u_borderEdges, ivec2(i, row), 0),
               texelFetch(u_borderEdges, ivec2(i + 1, row), 0), x - float(i));
}

vec4 borderRawPoint(float u, float v) {
    vec4 info = texelFetch(u_borderEdges, ivec2(0, 8), 0);
    float nU = max(info.x, 1.0);
    float nV = max(info.y, 1.0);
    float spanU = ubuf.u_max - ubuf.u_min;
    float spanV = ubuf.v_max - ubuf.v_min;
    float su = (abs(spanU) > 1.0e-12) ? (u - ubuf.u_min) / spanU : 0.0;
    float sv = (abs(spanV) > 1.0e-12) ? (v - ubuf.v_min) / spanV : 0.0;
    float dV0 = sv * nV;
    float dV1 = (1.0 - sv) * nV;
    float dU0 = su * nU;
    float dU1 = (1.0 - su) * nU;
    float d = min(min(dV0, dV1), min(dU0, dU1));
    int row = 6;
    float s = sv;
    float n = nV;
    if (d == dV0)      { row = 0; s = su; n = nU; }
    else if (d == dV1) { row = 2; s = su; n = nU; }
    else if (d == dU0) { row = 4; }
    return mix(borderEdgeSample(row, s, n), borderEdgeSample(row + 1, s, n), clamp(d, 0.0, 1.0));
}
#endif

// Punto (u, v) della figura nello spazio 4D ruotato, per il bordo.
vec4 borderRotatedPoint(float u, float v) {
#if defined(CUSTOM_MESH) && defined(HAS_BORDER)
    return rotate4D(borderRawPoint(u, v));
#else
    return getRotatedPoint4D(u, v);
#endif
}

// Punto (u, v) della figura per il bordo: nello spazio 4D ruotato per le
// superfici (le rotazioni conservano le distanze, il test delle cuciture le
// confronta), nella scena per i tubi, che esistono solo li'.
vec4 borderFigurePoint(float u, float v) {
#ifdef IS_TUBE
    return vec4(tubeScenePoint(u, v), 0.0);
#else
    return borderRotatedPoint(u, v);
#endif
}
vec3 borderScenePoint(float u, float v) {
#ifdef IS_TUBE
    return tubeScenePoint(u, v);
#else
    return projectToScene(borderRotatedPoint(u, v));
#endif
}
// La w 4D della figura in (u, v): la usa lo spareggio di profondita' 4D, che
// il bordo deve fare uguale alla figura a cui sta attaccato (main() per le
// superfici, emitTubeVertex per i tubi).
float borderFigureW(float u, float v) {
#ifdef IS_TUBE
    vec3 C, E1, E2;
    vec4 C4, T4;
    float uAt;
    tubeSceneFrame(u, C, E1, E2, C4, T4, uAt);
    return C4.w;
#else
    return borderRotatedPoint(u, v).w;
#endif
}

#ifdef HAS_BORDER
// BORDO DELLA SUPERFICIE: un tubo di raggio u_borderRadius attorno a un lato
// del dominio. I vertici li genera SurfaceEngine::generateBorder:
//   vertex   = (u, v, angolo attorno al tubo, 1)
//   normal   = (lungo u, lungo v, verso l'interno, 1)
//   texCoord = (coordinata del lato opposto, somma degli estremi del lato)
// Il tubo si costruisce nello spazio 3D FINALE, dopo rotazioni 4D e
// proiezione: lo spessore e' lo stesso ovunque, comunque la superficie sia
// deformata dalla proiezione.
// Non ogni lato del dominio e' un bordo, e quelli che non lo sono non si
// disegnano affatto:
//  - le CUCITURE, dove il lato coincide col lato opposto: diritte (toro,
//    cilindro) o ritorte, col verso invertito (nastro di Moebius);
//  - i lati che collassano in un punto (i poli di una sfera).
// Il test e' qui e non sulla CPU: per gli script la CPU non sa valutare la
// superficie, e una cucitura puo' aprirsi e chiudersi col tempo.
// E' una decisione PER LATO (borderEdgeHidden), non per punto: vedi sotto.

// Il lato e' una cucitura o un polo PER INTERO? Si prova in tre punti fissi
// del lato, gli stessi per tutti i suoi vertici: la decisione e' identica per
// ognuno, quindi un lato o si disegna tutto o non si disegna. Punto per punto
// si sbagliava dove due lati DIVERSI si toccano in un punto solo -- sul nastro
// di Moebius la fine di v = -1 coincide, attraverso la cucitura, con l'inizio
// di v = +1 -- e il tubo si strozzava proprio li'.
// Le rotazioni 4D conservano le distanze: il confronto vale fra punti ruotati.
bool borderEdgeHidden(vec2 along, float fixedCoord, float opposite, float alongSum,
                      float a0, float a1) {
    const float tol = 1.0e-3;
    bool straight = true;
    bool twisted = true;
    float len = 0.0;
    vec4 prev = vec4(0.0);
    for (int i = 0; i < 3; ++i) {
        float a = mix(a0, a1, (float(i) + 0.5) / 3.0);
        vec2 p  = (along.x > 0.5) ? vec2(a, fixedCoord) : vec2(fixedCoord, a);
        vec2 q  = (along.x > 0.5) ? vec2(a, opposite)   : vec2(opposite, a);
        vec2 qt = (along.x > 0.5) ? vec2(alongSum - a, opposite) : vec2(opposite, alongSum - a);
        vec4 P = borderFigurePoint(p.x, p.y);
        if (length(borderFigurePoint(q.x, q.y) - P) > tol)   straight = false;
        if (length(borderFigurePoint(qt.x, qt.y) - P) > tol) twisted = false;
        if (i > 0) len += length(P - prev);
        prev = P;
    }
    return straight || twisted || len < tol;
}

// LATI COPERTI. Un tratto di lato su cui passa un'altra parte della
// superficie, che prosegue in un'altra direzione, non e' un bordo: le
// cuciture, le giunture fra le mesh (le patch del Calabi-Yau), i lati che si
// ripiegano su se' stessi (Oloid) o avvolgono piu' volte la stessa curva
// (Wound Sphere), i poli, gli orli che passano dentro un'altra mesh (le fasce
// dell'Octahedron Bands). Lo decide la passata compute dei lati coperti
// (GLWidget::buildSharedEdgePipeline), un texel per campione del lato
// (SHARED_EDGE_SAMPLES intervalli): qui si nasconde il lato coperto per
// intero (colonna del lato), i tratti li scarta il fragment. Un campione scoperto
// fra due coperti conta come coperto: un punto singolare isolato (il centro
// del Calabi-Yau) non lascia un pezzetto di tubo. Sulla mesh custom il test,
// per lato intero, lo fa la CPU sulla mesh disegnata (GLWidget::setBorderEdges),
// riga 8 texel 1 dei lati.
// Lati: 0 v = v_min, 1 v = v_max (lungo u), 2 u = u_min, 3 u = u_max (lungo v).
const int SHARED_EDGE_SAMPLES = 256;   // = GLWidget::kSharedEdgeSamples
#if defined(TUBE_FRAME_COMPUTE) || defined(SHARED_EDGE_COMPUTE)
bool borderEdgeShared(int side) { return false; }
bool borderCoveredAt(int side, int m) { return false; }
#elif defined(CUSTOM_MESH)
bool borderEdgeShared(int side) {
    return texelFetch(u_borderEdges, ivec2(1, 8), 0)[side] > 0.5;
}
bool borderCoveredAt(int side, int m) { return borderEdgeShared(side); }
#else
layout(binding = 4) uniform sampler2D u_sharedEdges;

bool borderCoveredAt(int side, int m) {
    if (ubuf.u_sharedEdgesOn < 0.5) return false;
    int row = int(ubuf.u_meshIndex + 0.5) * 4 + side;
    if (texelFetch(u_sharedEdges, ivec2(m, row), 0).x > 0.5) return true;
    return texelFetch(u_sharedEdges, ivec2(max(m - 1, 0), row), 0).x > 0.5
        && texelFetch(u_sharedEdges, ivec2(min(m + 1, SHARED_EDGE_SAMPLES), row), 0).x > 0.5;
}

// Il lato coperto per intero: l'ultima colonna della texture, che la quinta
// passata scrive guardando tutti i campioni fuori dai capi. Tre campioni fissi
// non bastavano: un lato scoperto per un tratto spariva intero.
bool borderEdgeShared(int side) {
    if (ubuf.u_sharedEdgesOn < 0.5) return false;
    int row = int(ubuf.u_meshIndex + 0.5) * 4 + side;
    return texelFetch(u_sharedEdges, ivec2(SHARED_EDGE_SAMPLES + 1, row), 0).x > 0.5;
}
#endif

// Il lato non si disegna. Con la passata dei lati coperti attiva decide solo
// lei, che vede anche le cuciture: il test del vertex (nove punti della
// superficie per vertice) resta per quando manca, e sulla mesh custom.
bool borderSideHidden(int side, vec2 along, float fixedCoord, float opposite, float alongSum,
                      float a0, float a1) {
#if !defined(CUSTOM_MESH)
    if (ubuf.u_sharedEdgesOn > 0.5) return borderEdgeShared(side);
#endif
    return borderEdgeShared(side) || borderEdgeHidden(along, fixedCoord, opposite, alongSum, a0, a1);
}

// Campioni fissi lungo un lato per il trasporto della sezione (emitBorderVertex).
const int BORDER_FRAME_SAMPLES = 32;

// Punto del lato: a e' la coordinata lungo il lato, fixedCoord l'altra.
vec3 borderEdgePoint(vec2 along, float fixedCoord, float a) {
    vec2 p = (along.x > 0.5) ? vec2(a, fixedCoord) : vec2(fixedCoord, a);
    return borderScenePoint(p.x, p.y);
}

// Una direzione qualunque perpendicolare a t (unitario).
vec3 borderAnyNormal(vec3 t) {
    vec3 alt = (abs(t.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    return normalize(alt - t * dot(alt, t));
}

// f, perpendicolare a t0, ruotato della rotazione minima che porta t0 su t1.
// Con t0 e t1 quasi opposti (cuspide) la rotazione non e' definita: si
// proietta sul piano normale a t1.
vec3 borderCarry(vec3 f, vec3 t0, vec3 t1) {
    float c = dot(t0, t1);
    vec3 r = (c > -0.99) ? f - dot(f, t1) / (1.0 + c) * (t0 + t1)
                         : f - t1 * dot(f, t1);
    return (dot(r, r) > 1.0e-12) ? normalize(r) : borderAnyNormal(t1);
}

// Vertice fuori dal volume di clip: triangoli e linee si scartano interi.
void emitBorderHidden() {
    v_pos = vec3(0.0);
    v_normal = vec4(0.0, 0.0, 1.0, 0.0);
    v_texCoord = vec2(0.0);
    v_light4D = 1.0;
    v_spec4D = 0.0;
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}

// Posizione finale di un vertice del bordo, col tie-break di profondita' 4D
// della figura in (u, v). Senza, su una figura con w diverso da zero (l'Enneper
// in H3 ha w = 0.5) la superficie veniva spostata in profondita' e il bordo no:
// il foglio, che dentro il tubo arriva fino all'asse, ne usciva attraverso la
// parete e lo tagliava a tratti.
vec4 borderClip(vec3 pos, vec2 uv) {
    vec4 clip = ubuf.u_mvpMatrix * vec4(pos, 1.0);
    clip.z -= clamp(borderFigureW(uv.x, uv.y), -1.0, 1.0) * 0.0005 * clip.w;
    clip += ubuf.u_dummyZero * 0.000001;
    return clip;
}

// GIUNTO D'ANGOLO (vedi SurfaceEngine::generateBorder): una sfera
// nell'angolo (u, v) del dominio, vertex.z = longitudine, normal.x =
// latitudine. Si vede se almeno uno dei due lati che vi si incontrano si vede.
// Il raggio e' un po' minore di quello del tubo: il tubo e' un poligono di 24
// lati (SurfaceEngine::kBorderRing) e le sue facce stanno a r cos(7.5 gradi) =
// 0.991 r dall'asse. A raggio pieno i vertici della sfera bucavano le facce
// dove il tubo PROSEGUE attraverso l'angolo -- ogni lato che si richiude su se'
// stesso, come l'orlo dei mezzi tori di Hopf o di un cilindro -- e a ogni
// giunzione la sfera affiorava dal tubo. A 0.98 r resta dentro; dove i lati
// fanno un angolo riempie ancora il cuneo. Con meno lati va ridotto.
const float BORDER_JOINT_SCALE = 0.98;

void emitBorderCorner() {
    vec2 uv = vertex.xy;
    bool atUMin = abs(uv.x - ubuf.u_min) <= abs(uv.x - ubuf.u_max);
    bool atVMin = abs(uv.y - ubuf.v_min) <= abs(uv.y - ubuf.v_max);
    // Un lato coperto anche solo vicino all'angolo (orlo che entra in
    // un'altra mesh) non chiede il giunto.
    bool hideAlongU = borderSideHidden(atVMin ? 0 : 1, vec2(1.0, 0.0), uv.y, atVMin ? ubuf.v_max : ubuf.v_min,
                                       ubuf.u_min + ubuf.u_max, ubuf.u_min, ubuf.u_max)
                      || borderCoveredAt(atVMin ? 0 : 1, atUMin ? 0 : SHARED_EDGE_SAMPLES);
    bool hideAlongV = borderSideHidden(atUMin ? 2 : 3, vec2(0.0, 1.0), uv.x, atUMin ? ubuf.u_max : ubuf.u_min,
                                       ubuf.v_min + ubuf.v_max, ubuf.v_min, ubuf.v_max)
                      || borderCoveredAt(atUMin ? 2 : 3, atVMin ? 0 : SHARED_EDGE_SAMPLES);
    if (hideAlongU && hideAlongV) {
        emitBorderHidden();
        return;
    }
    // Se si vede un lato solo e PROSEGUE liscio attraverso l'angolo -- nel suo
    // altro capo (un lato che si richiude: l'orlo dei mezzi tori di Hopf) o,
    // ritorto, nel capo del lato opposto (l'orlo del nastro di Moebius) -- il
    // tubo e' continuo e la sfera non serve. Anzi: dentro il tubo, a 0.98 r
    // contro facce a 0.991 r, da lontano la profondita' non la separava piu'
    // dalle facce e affiorava come una mezzaluna scura. Punti: 0 l'angolo,
    // 2 l'altro capo, 4 l'altro capo del lato opposto, ciascuno col vicino
    // verso l'interno del lato (1, 3, 5). Una sola chiamata alla superficie
    // nel ciclo (Mali la ricopia per ogni punto di chiamata).
    bool oneSide = hideAlongU != hideAlongV;
    bool alongU = !hideAlongU;
    float aHere = alongU ? uv.x : uv.y;
    float aThere = alongU ? (atUMin ? ubuf.u_max : ubuf.u_min) : (atVMin ? ubuf.v_max : ubuf.v_min);
    float fixedHere = alongU ? uv.y : uv.x;
    float fixedOpp = alongU ? (atVMin ? ubuf.v_max : ubuf.v_min) : (atUMin ? ubuf.u_max : ubuf.u_min);
    float stepIn = 0.001 * (aThere - aHere);   // da qui verso l'altro capo
    vec3 pts[6];
    int count = oneSide ? 6 : 1;
    for (int i = 0; i < count; ++i) {
        float a = (i < 2) ? aHere + float(i) * stepIn : aThere - float(i % 2) * stepIn;
        float c = (i < 4) ? fixedHere : fixedOpp;
        vec2 q = alongU ? vec2(a, c) : vec2(c, a);
        pts[i] = borderScenePoint(q.x, q.y);
    }
    if (oneSide) {
        // Direzioni nel verso del lato da qui all'altro capo: arrivando
        // all'altro capo il tubo prosegue da qui nella stessa direzione.
        float tol = 1.0e-3 * max(1.0, length(pts[0]));
        vec3 tHere = pts[1] - pts[0];
        vec3 tThere = pts[2] - pts[3];
        vec3 tOpp = pts[4] - pts[5];
        bool straight = length(pts[2] - pts[0]) < tol && dot(tHere, tThere) > 0.99 * length(tHere) * length(tThere);
        bool twisted = length(pts[4] - pts[0]) < tol && dot(tHere, tOpp) > 0.99 * length(tHere) * length(tOpp);
        if (straight || twisted) {
            emitBorderHidden();
            return;
        }
    }
    float lon = vertex.z;
    float lat = normal.x;
    vec3 n = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
    vec3 pos = pts[0] + BORDER_JOINT_SCALE * ubuf.u_borderRadius * n;
    v_pos = vec3(ubuf.u_mvMatrix * vec4(pos, 1.0));
    v_normal = ubuf.u_mvMatrix * vec4(n, 0.0);
    v_texCoord = vec2(lon / 6.28318531, 0.5 - lat / 3.14159265);
    v_light4D = 1.0;
    v_spec4D = 0.0;
    gl_Position = borderClip(pos, uv);
}

void emitBorderVertex() {
    if (normal.w > 1.5) {
        emitBorderCorner();
        return;
    }
    vec2 uv = vertex.xy;
    float ang = vertex.z;
    vec2 along = normal.xy;
    float e = 0.0005;
    float a0 = (along.x > 0.5) ? ubuf.u_min : ubuf.v_min;
    float a1 = (along.x > 0.5) ? ubuf.u_max : ubuf.v_max;

    // LATO NASCOSTO: tutti i suoi vertici fuori dal volume di clip, cosi'
    // triangoli e linee si scartano interi. Un raggio zero non bastava: i
    // triangoli spariscono, ma in wireframe le linee lungo il tubo collassano
    // sulla cucitura e si vedono (un tratteggio sul toro). Vale per ogni lato
    // coperto (borderSideHidden); normal.z e' il verso dell'interno, +1 sui
    // lati al minimo del dominio.
    int side = (along.x > 0.5) ? ((normal.z > 0.0) ? 0 : 1) : ((normal.z > 0.0) ? 2 : 3);
    if (borderSideHidden(side, along, (along.x > 0.5) ? uv.y : uv.x, texCoord.x, texCoord.y, a0, a1)) {
        emitBorderHidden();
        return;
    }

    vec3 P = borderScenePoint(uv.x, uv.y);
    vec3 T = borderScenePoint(uv.x + along.x * e, uv.y + along.y * e)
           - borderScenePoint(uv.x - along.x * e, uv.y - along.y * e);
    float lenT = length(T);
    float r = ubuf.u_borderRadius;
    vec3 Tn = (lenT > 1.0e-9) ? T / lenT : vec3(1.0, 0.0, 0.0);

    // Cerchio nel piano normale al lato. Il suo primo asse NON segue la
    // superficie punto per punto: la direzione verso l'interno puo' girare di
    // 180 gradi in un tratto piu' corto di un passo della griglia (Hyperbolic
    // Saddle a u = 8: tanh satura e la direzione e' quasi nulla dove
    // cos(2v) = 0), e due anelli consecutivi ruotati di tanto strozzavano il
    // tubo. Si TRASPORTA invece lungo il lato con la rotazione minima, come la
    // sezione dei tubi: dal campione di mezzo del lato, dove vale la direzione
    // verso l'interno, fino ai due campioni fissi attorno al vertice, poi
    // interpolato e portato sulla sua tangente. I campioni sono gli stessi per
    // tutti i vertici del lato, quindi anelli vicini combaciano.
    float fixedCoord = (along.x > 0.5) ? uv.y : uv.x;
    float a = (along.x > 0.5) ? uv.x : uv.y;
    float H = (a1 - a0) / float(BORDER_FRAME_SAMPLES);
    float x = (abs(H) > 1.0e-12) ? (a - a0) / H : float(BORDER_FRAME_SAMPLES / 2);
    int k = clamp(int(floor(x)), 0, BORDER_FRAME_SAMPLES - 1);
    float f = clamp(x - float(k), 0.0, 1.0);
    int m = BORDER_FRAME_SAMPLES / 2;
    int s = (k >= m) ? 1 : -1;
    int walk = abs(((s > 0) ? k + 1 : k) - m);
    vec3 inwardPoint = borderEdgePoint(along, fixedCoord + normal.z * e, a0 + float(m) * H);
    vec3 Pa = vec3(0.0), Pb = vec3(0.0), Pc = vec3(0.0);
    vec3 Tprev = vec3(1.0, 0.0, 0.0);
    vec3 F = vec3(0.0, 1.0, 0.0);
    vec3 Tk = Tn, Tk1 = Tn, Fk = F, Fk1 = F;
    for (int i = 0; i <= walk + 2; ++i) {
        // Finestra di tre campioni (dietro, centro, avanti nel verso del
        // cammino), un solo punto nuovo per passo.
        int j = clamp(m - s + i * s, 0, BORDER_FRAME_SAMPLES);
        Pa = Pb;
        Pb = Pc;
        Pc = borderEdgePoint(along, fixedCoord, a0 + float(j) * H);
        if (i < 2) continue;
        int jc = m + (i - 2) * s;
        vec3 Tj = (Pc - Pa) * float(s);
        Tj = (dot(Tj, Tj) > 1.0e-24) ? normalize(Tj) : Tprev;
        if (i == 2) {
            vec3 Q = inwardPoint - Pb;
            F = Q - Tj * dot(Q, Tj);
            if (dot(F, F) < 1.0e-12 * max(1.0, dot(Pb, Pb))) F = borderAnyNormal(Tj);
            F = normalize(F);
        } else {
            F = borderCarry(F, Tprev, Tj);
        }
        Tprev = Tj;
        if (jc == k)     { Tk = Tj;  Fk = F; }
        if (jc == k + 1) { Tk1 = Tj; Fk1 = F; }
    }
    vec3 tRef = mix(Tk, Tk1, f);
    tRef = (dot(tRef, tRef) > 1.0e-12) ? normalize(tRef) : ((f < 0.5) ? Tk : Tk1);
    vec3 fRef = mix(Fk, Fk1, f);
    fRef -= tRef * dot(fRef, tRef);
    fRef = (dot(fRef, fRef) > 1.0e-12) ? normalize(fRef) : borderAnyNormal(tRef);
    vec3 E1 = borderCarry(fRef, tRef, Tn);
    vec3 E2 = cross(Tn, E1);
    vec3 n = cos(ang) * E1 + sin(ang) * E2;
    vec3 pos = P + r * n;

    v_pos = vec3(ubuf.u_mvMatrix * vec4(pos, 1.0));
    v_normal = ubuf.u_mvMatrix * vec4(n, 0.0);
    // Coordinate della texture del bordo: lungo il lato (0..1 su ciascun
    // lato della parte) e attorno al tubo (0..1), nella stessa forma della
    // superficie (seconda coordinata rovesciata, come v_texCoord di main).
    float sAlong = (abs(a1 - a0) > 1.0e-9) ? (a - a0) / (a1 - a0) : 0.0;
    v_texCoord = vec2(sAlong, 1.0 - ang / 6.28318531);
    v_borderSide = float(side);
    v_borderReach = (lenT > 1.0e-12 && abs(a1 - a0) > 1.0e-12)
                  ? clamp(0.75 * r * 2.0 * e / (lenT * abs(a1 - a0)), 0.0, 0.25) : 0.0;
    v_light4D = 1.0;
    v_spec4D = 0.0;
    gl_Position = borderClip(pos, uv);
}
#endif

void main() {
    v_borderSide = -1.0;
    v_borderReach = 0.0;
    v_objPos = vec3(0.0);
    v_objNormal = vec3(0.0, 0.0, 1.0);
    // --- BYPASS PER FLAT VIEW (TEXTURE PREVIEW) ---
    if (ubuf.u_isFlat != 0) {
        gl_Position = ubuf.u_mvpMatrix * vec4(vertex.xyz, 1.0);
        v_pos = vertex.xyz;
        v_normal = vec4(0.0, 0.0, 1.0, 0.0);
        v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);
        v_light4D = 1.0;
        return;
    }

#ifdef HAS_BORDER
    // Vertice del bordo (normal.w = 1; la superficie ha normal.w = 0).
    if (normal.w > 0.5) {
        emitBorderVertex();
        return;
    }
#endif
#ifdef IS_TUBE
    // Vertice del tubo: costruito nella scena (emitTubeVertex).
    emitTubeVertex();
    return;
#endif

    float u = vertex.x;
    float v = vertex.y;

    // 1. Calcolo Posizione 4D Reale
    vec4 pRot = getRotatedPoint4D(u, v);

    // 2. Proiezione 4D -> 3D
    vec3 finalPos3D = projectToScene(pRot);

#ifdef CUSTOM_MESH
    // SE SIAMO NEL GEODESIC FLOW, PRENDIAMO LA NORMALE DAL BUFFER
    vec3 finalNormal3D = normal.xyz;
#else
    // ALTRIMENTI LA CALCOLIAMO CON LA MATEMATICA
    vec3 finalNormal3D = calculateFinalNormal(u, v);
#endif

    // 3. CALCOLO ILLUMINAZIONE 4D E SPECULARE
    v_spec4D = 0.0; // Valore di default

    if (ubuf.u_lightingMode == 1) { // DIRECTIONAL LIGHT (Sostituisce la vecchia Radial)
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        // Direzione fissa della luce all'infinito (Il nostro "Sole 4D")
        vec4 lightDir4D = normalize(vec4(1.0, 1.0, 1.0, 0.5));
        vec4 viewDir4D = normalize(ubuf.u_cameraPos4D - P);

        // Diffuse 4D
        vec4 L_norm = lightDir4D - Tu * dot(lightDir4D, Tu) - Tv * dot(lightDir4D, Tv);
        float intensity = length(L_norm);

        // Illuminazione omogenea, niente attenuazione
        v_light4D = clamp(0.2 + 0.8 * intensity, 0.1, 1.5);

        // Specular 4D
        vec4 H = normalize(lightDir4D + viewDir4D);
        vec4 H_norm = H - Tu * dot(H, Tu) - Tv * dot(H, Tv);
        float specAngle = length(H_norm);

        // Riflesso
        v_spec4D = pow(specAngle, 32.0) * 0.5;

    } else if (ubuf.u_lightingMode == 2) { // OBSERVER
        vec4 P = pRot;
        vec4 Tu, Tv;
        getTangentPlane(u, v, Tu, Tv);

        vec4 toObserver = ubuf.u_observerPos - P;
        float dist = length(toObserver); // ECCO LA VARIABILE CHE MANCAVA!

        if (dist < 0.001) {
            v_light4D = 1.0;
            v_spec4D = 0.0;
        } else {
            vec4 lightDir4D = toObserver / dist;
            vec4 viewDir4D = normalize(ubuf.u_cameraPos4D - P);

            // Diffuse 4D
            vec4 L_norm = lightDir4D - Tu * dot(lightDir4D, Tu) - Tv * dot(lightDir4D, Tv);
            float intensity = length(L_norm);

            // 1. Attenuazione più morbida (Evita di sparare la luce al 300% quando vicino)
            float attenuation = 2.0 / (1.0 + 0.2 * dist + 0.02 * dist * dist);

            // 2. Clamp di sicurezza sulla luce diffusa
            v_light4D = clamp(0.2 + 0.8 * intensity * attenuation, 0.1, 1.5);

            // Specular 4D
            vec4 H = normalize(lightDir4D + viewDir4D);
            vec4 H_norm = H - Tu * dot(H, Tu) - Tv * dot(H, Tv);
            float specAngle = length(H_norm);

            // 3. Riduciamo l'esponente da 64 a 32
            v_spec4D = pow(specAngle, 32.0) * attenuation * 0.5;
        }

    } else if (ubuf.u_lightingMode == 3) { // SLICE
            vec4 P = pRot;
            vec4 sliceData = calculate4DNormal_Slice(u, v);
            vec3 N_local = sliceData.xyz;
            float sliceIntensity = sliceData.w;

            vec3 lightPos = ubuf.u_observerPos.xyz;
            vec3 L = normalize(lightPos - pRot.xyz);

            float halfLambert = dot(N_local, L) * 0.5 + 0.5;
            float diff = halfLambert * halfLambert;

            // Sfumiamo dolcemente anche l'alone luminoso 4D
            float rim = smoothstep(0.0, 1.0, 1.0 - sliceIntensity);
            rim = pow(rim, 2.0);

            // Mix finale molto più bilanciato e morbido
            v_light4D = 0.2 + 0.7 * diff + rim * 0.7;

            // Specular locale più elegante
            vec3 viewDir3D = normalize(ubuf.u_cameraPos4D.xyz - pRot.xyz);
            vec3 H = normalize(L + viewDir3D);
            float specAngle = max(dot(N_local, H), 0.0);
            v_spec4D = pow(specAngle, 48.0) * 0.4;
    } else {
        v_light4D = 1.0;
        v_spec4D = 0.0;
    }

    v_pos = vec3(ubuf.u_mvMatrix * vec4(finalPos3D, 1.0));
    v_normal = ubuf.u_mvMatrix * vec4(finalNormal3D, 0.0);
    v_texCoord = vec2(texCoord.x, 1.0 - texCoord.y);
    v_objPos = finalPos3D;
    v_objNormal = finalNormal3D;

    vec4 pos = ubuf.u_mvpMatrix * vec4(finalPos3D, 1.0);

    // >>> FIX SFARFALLIO (Z-Fighting 4D Tie-Breaker) <<<
    pos.z -= clamp(pRot.w, -1.0, 1.0) * 0.0005 * pos.w;

    pos += ubuf.u_dummyZero * 0.000001;
    gl_Position = pos;
}
