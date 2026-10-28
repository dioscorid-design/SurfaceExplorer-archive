# SurfaceExplorer — regole per il codice

## Recorder video (videorecorder.cpp)
- Contratto del loop di registrazione: **il frame i mostra ciò che mostrerebbe lo schermo al tempo equivalente**. Mai duplicare logica live nel recorder: le copie divergono ed è la famiglia di bug più ricorrente del progetto (moduli stoppati che ripartivano nel video, vista path sbagliata, base 4D ignorata).
- La camera dei path passa SOLO da `CameraPaths::applyCameraAt(path, t)` (l'avanzamento di un frame da `CameraPaths::advance(path, tick)`, lo stesso del tick live); le rotazioni (oggetto + 4D) SOLO da `GLWidget::advanceRotationsBy(dt)`; gli orologi di animazione (t di geometria, texture, sfondo, fasce) SOLO da `GLWidget::advanceClocksBy(dt)` — uniche implementazioni condivise tra tick live e loop di registrazione (il recorder passa tempo/dt virtuali del frame). Ogni nuovo comportamento di camera/moto va estratto allo stesso modo: funzione parametrizzata sul tempo, chiamata da entrambi i punti.
- Ogni orologio È il tempo che il suo modulo mostra: niente basi comuni sommate a più orologi (la vecchia `m_manualTime` dava video a velocità doppia e salti a ogni stop/start). Verifica: `./ExC/Mac/Test/clock-test.sh` (lancia `SurfaceExplorer --clock-test <radice preset> <uscita>`; i test interni esistono solo nella build Debug desktop e in `build/Tests`, compilata da `./ExC/Mac/Test/build-tests.sh`, che e' quella usata dagli script).
- Nei diff che toccano videorecorder.cpp, cercare stato live ricostruito a mano (velocità, modalità di vista, clock): è il segnale d'allarme.

## Blocco uniforme SceneUBO (shaders/surface.vert + surface.frag)
- Il blocco `SceneUBO` è dichiarato in **due** file e deve combaciare **campo per campo**: stessi tipi, stesso ordine, stessa lunghezza. Un campo aggiunto a `UboData` (glwidget.h) va dichiarato in **entrambi** gli shader, anche se lo usa uno solo — con un commento che dica che lì non serve.
- Perché non basta il desktop per accorgersene: std140 permette a uno stage di dichiarare solo un **prefisso** del blocco, e macOS/Metal lo accetta. **Adreno no**: rifiuta il link con `Uniform ubuf type mismatch with other stage`, *tutte* le pipeline falliscono (opaque, transpBack, transpFront, wireframe) e su Android si vede **schermo vuoto** — senza alcun errore nell'app. Il sintomo non punta allo shader: sembra un preset rotto.
- Il campo nuovo va in **coda** (l'ultimo di `UboData`), mai in mezzo: std140 non consente di saltare campi e ogni inserimento intermedio sposta tutti gli offset successivi.
- Verifica rapida degli offset: estrarre il solo blocco in uno shader minimo (i due file veri hanno segnaposto `%X_EQ%` che `qsb` non compila) e `qsb --dump`. Riferimento noto: `u_min=372`, `z_max=416`, `u_meshIndex=420`, `u_noImage=424`.

## Nomi trappola
- `m_scene.pathViewMode4D` = vista del path 4D (pushView); `m_scene.pathViewMode3D` = vista del path 3D (pushView3D). Non abbreviarli né confonderli: il bug storico nasceva dai vecchi nomi quasi identici (`m_pathMode`/`m_pathMode3D`).
- Le chiavi JSON di persistenza restano `"pathMode"` / `"pathMode3D"` (compatibilità coi record esistenti).
