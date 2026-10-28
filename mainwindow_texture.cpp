// mainwindow_texture.cpp - MainWindow: texture di superficie e di sfondo -- scelta dalla
// Library, codice applicato, immagini, colori, fasce, checkbox.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


void MainWindow::syncTextureTreeSelection()
{
    // SINCRONIZZA L'ALBERO TEXTURE AL CAMBIO MODALITÀ
    ui->treeTextures->clearSelection();
    // Anche la voce CORRENTE: senza, quando la texture del bersaglio non ha
    // una voce nella Library (scritta nel dock Script, scacchiera accesa dal
    // checkbox) l'indicatore di focus restava sulla voce di prima -- passando
    // da Surface a Border sembrava evidenziata la texture della superficie.
    // A segnali bloccati: e' la vista che si riallinea, non un clic.
    {
        const QSignalBlocker blocker(ui->treeTextures);
        ui->treeTextures->setCurrentItem(nullptr);
    }

    // Texture SPENTA: a schermo non c'e' alcuna texture, quindi l'albero non
    // deve indicarne una. Il codice resta in memoria (per poterla riaccendere),
    // ma non e' piu' "cio' che si vede": senza questo controllo il dock Library
    // restava puntato sulla texture appena tolta -- segnalato dall'utente, su
    // superficie e su sfondo allo stesso modo.
    //
    // LO STATO SI LEGGE DAL MODELLO, NON DA chkBoxTexture. Prima c'era qui un
    // "if (!ui->chkBoxTexture->isChecked()) return;" unico per tutti i rami, e
    // il checkbox e' un DISPLAY: in ambito "Mesh" lo scrive
    // syncAppearanceControlsToActiveMesh, che lo fa DOPO aver chiamato questa
    // funzione. Cambiando mesh il gate leggeva quindi lo stato della fascia
    // PRECEDENTE: passando da una mesh in wireframe (che lascia il checkbox
    // spento, perche' il wireframe SPEGNE la texture conservando lo script) a
    // una texturizzata, l'albero veniva pulito e si usciva dal return prima di
    // ricercare -- la texture subito dopo un wireframe perdeva il focus.
    // E' la firma del "giro di ritardo": si riparava al primo evento successivo
    // che ripassava di qui. Leggendo la texture EFFICACE dal modello la
    // funzione non dipende piu' dall'ordine delle chiamate.
    // Regola generale gia' fissata per l'editor: ogni punto che mostra una
    // texture legge quella EFFICACE, mai quella "dichiarata" ne' il widget.
    //
    // clearSelection() e' gia' stato fatto qui sopra: uscendo ora l'albero
    // resta pulito.
    QString activeCode;
    // Nome di libreria della texture cercata, ciascuna con la SUA ancora: la
    // superficie globale m_scene.textureLibName, lo sfondo
    // m_scene.bgTextureLibName, una fascia MeshPart::textureLibName
    // (vedi LibraryTreeFocus::selectTexture).
    QString libName;
    if (editingBorder()) {
        // BORDO: la sua texture e la sua ancora, se si vede.
        if (!ui->glWidget || !ui->glWidget->borderTextureShown()) return;
        activeCode = ui->glWidget->borderTextureCode();
        libName = ui->glWidget->borderTextureLibName();
    } else if (editingBackground()) {
        // SFONDO: il suo stato e' nel motore (isBackgroundTextureEnabled).
        if (!targetTextureOn()) return;
        activeCode = m_scene.bgTextureCode;
        libName = m_scene.bgTextureLibName;
    } else {
        if (implicitMode()) {
            // RAY MARCHING: nessuna fascia, la texture e' quella di superficie.
            if (!surfaceTextureShown()) return;
            activeCode = m_scene.rm.texture;
            libName = m_scene.textureLibName;
        } else {
            // MULTI-MESH: con una fascia selezionata l'albero deve evidenziare
            // la texture di QUELLA, non quella globale -- stessa regola con cui
            // l'editor mostra p.textureCode (vedi
            // syncAppearanceControlsToActiveMesh). Senza, il dock Library
            // restava puntato sulla texture della superficie mentre editor,
            // checkbox e render parlavano della fascia: il focus indicava una
            // texture diversa da quella su cui si stava lavorando.
            // Una fascia SENZA texture propria non eredita quella globale (vedi
            // effectiveTextureEnabledMulti), quindi lascia il codice VUOTO e
            // l'albero si limita a deselezionare: e' la stessa cosa che fa
            // l'editor, che in quel caso si svuota.
            // AMBITO "ALL": stato GLOBALE. In WIREFRAME globale la texture non
            // si disegna (surface.frag esce a colore piatto), quindi l'albero
            // non deve indicarne nessuna. Il vecchio gate lo otteneva di
            // rimbalzo -- il ramo wireframe del radio forza il checkbox a false
            // (~2098) senza toccare m_scene.surfaceTextureState -- e leggendo il
            // modello quella condizione va riscritta ESPLICITA, o passando in
            // wireframe da "All" l'albero resterebbe puntato sulla texture.
            // La modalita' si legge dal motore, non dai radio, per la stessa
            // ragione: i radio sono display e in ambito "Mesh" mostrano la
            // fascia, non il globale.
            const bool wireframeAll =
                ui->glWidget && ui->glWidget->globalRenderMode() == 2;
            bool on = m_scene.surfaceTextureState && !wireframeAll;
            activeCode = m_scene.surfaceTextureCode;
            libName = m_scene.textureLibName;
            if (ui->glWidget && ui->glWidget->getEngine()) {
                const int idx = ui->glWidget->activeMeshPart();
                const auto &parts = ui->glWidget->getEngine()->getMeshParts();
                if (idx >= 0 && idx < (int)parts.size()) {
                    // Stessa coppia di regole del display del checkbox: in
                    // multi-mesh una fascia mai configurata NON eredita la
                    // texture globale; a mesh singola vale l'eredita' di sempre.
                    // Qui NON serve escludere il wireframe a mano: applicandolo
                    // a una fascia la sua texture viene SPENTA (textureEnabled =
                    // false, script conservato), quindi lo stato efficace e' gia'
                    // falso -- ed e' esattamente il caso che questo fix risolve.
                    const MeshPart &p = parts[idx];
                    const bool multi = ui->glWidget->meshPartCount() > 1;
                    on = multi ? p.effectiveTextureEnabledMulti()
                               : p.effectiveTextureEnabled(m_scene.surfaceTextureState
                                                           && !wireframeAll);
                    activeCode = p.hasCustomTexture ? p.textureCode : QString();
                    // Texture della FASCIA: il nome del record e' quello della
                    // texture globale, non di questa. La fascia ha la SUA ancora
                    // (MeshPart::textureLibName, vuota nei record salvati prima
                    // che esistesse: li' la ricerca resta per solo codice).
                    libName = p.hasCustomTexture ? p.textureLibName : QString();
                }
            }
            if (!on) return;
        }
    }

    LibraryTreeFocus::selectTexture(ui->treeTextures, m_libraryManager, activeCode, libName);
}

// Sceglie e seleziona nell'albero la voce che corrisponde alla texture attiva.
// UNICA SEDE della scansione: la usano sia syncTextureTreeSelection sia la
// sincronizzazione al load di un record, che prima avevano due copie dello
// stesso ciclo -- destinate a divergere alla prima correzione (e infatti la
// correzione qui sotto andava fatta due volte).
//
// DUE PASSATE, e l'ordine conta: prima il CODICE su tutto l'albero, poi il
// NOME. Fermarsi alla prima voce che risponde rendeva l'esito casuale quando
// DUE voci rivendicano lo stesso record per criteri diversi -- una col codice
// identico, una col nome salvato ma codice cambiato: vinceva quella incontrata
// prima, cioe' l'ordine alfabetico ("Stripes" batte "Stripes v2", e si finiva
// sulla texture sbagliata).
// Vince il CODICE perche' e' quello che il record DISEGNA: evidenziare altro
// significherebbe indicare una voce che, se cliccata, darebbe un'altra resa.
// Il NOME serve al caso per cui e' stato introdotto -- codice modificato in
// libreria -- dove nessun codice combacia piu'.
// "Sync Focused Texture": riporta nel record il codice AGGIORNATO della sua
// texture di libreria, senza toccare nient'altro.
//
// PERCHE' NON BASTA RICARICARE LA TEXTURE DAL DOCK. Quel percorso
// (handleTextureSelection) applica il preset INTERO: sovrascrive i colori
// (~8508) e rimette le costanti A-F del preset (~9052, quelle non usate dalla
// superficie). Chi aveva scelto i propri colori o tarato uno slider li perdeva,
// e l'unica alternativa era copiare lo script a mano nell'editor.
// Qui si aggiorna il SOLO codice -- piu' il displacement, che e' parte della
// stessa texture -- e restano: col1/col2, costanti, zoom, pan, rotation.
//
// NON SALVA. La scena risulta modificata e l'avviso di lavoro non salvato fa il
// suo corso: vedere il risultato prima di scrivere sul file e' il punto, visto
// che una texture che ha ACQUISITO uno slider usera' il valore che quella
// costante ha NEL RECORD, non quello con cui la texture e' stata pensata.
// SONDA: stato COMPLETO della texture in una riga sola (vedi SE_TEX_PROBE).
//
// Perche' una riga sola e perche' tutti i campi insieme: il guasto di questa
// famiglia non e' un valore sbagliato, e' DUE valori che dovrebbero coincidere e
// non coincidono (l'editor dice una cosa, lo schermo ne mostra un'altra). Con
// una stampa per campo, sparsa, quella divergenza non si vede; con la stessa
// riga ripetuta nei punti della sequenza si legge in colonna e salta all'occhio
// quale passo ha spostato cosa.
//
// Del codice si stampa la sola DENSITY e una firma breve, non lo script: negli
// script Wireframe la densita' e' esattamente cio' che distingue un preset
// dall'altro, e uno script intero per riga renderebbe il log illeggibile.
void MainWindow::dumpTextureState(const char *tag) const
{
#if SE_TEX_PROBE
    // "DENSITY = <numero>": il coefficiente scritto nel codice. E' il dato che
    // distingue le due texture di prova (6.0 contro 12.0), e da solo non basta
    // -- va letto INSIEME a F, che lo moltiplica.
    auto densityOf = [](const QString &code) -> QString {
        static const QRegularExpression re(
            R"(DENSITY\s*=\s*([0-9]*\.?[0-9]+))");
        const QRegularExpressionMatch m = re.match(code);
        return m.hasMatch() ? m.captured(1) : QStringLiteral("-");
    };
    // RILIEVO: il moltiplicatore dentro il displacement (es. "pModel.x * 60.0*F"
    // -> 60.0). E' cio' che distingue le due versioni del displacement nei
    // preset di prova, come DENSITY fa per il colore.
    auto reliefOf = [](const QString &code) -> QString {
        static const QRegularExpression re(
            R"(pModel\.[xyz]\s*\*\s*([0-9]*\.?[0-9]+))");
        const QRegularExpressionMatch m = re.match(code);
        return m.hasMatch() ? m.captured(1) : QStringLiteral("-");
    };
    // Firma: lunghezza + hash. Due codici con la stessa DENSITY ma diversi per
    // altro (colori, displacement) restano distinguibili senza stampare nulla
    // di lungo.
    auto sigOf = [](const QString &code) -> QString {
        if (code.isEmpty()) return QStringLiteral("vuoto");
        return QString("%1/%2").arg(code.length())
               .arg(qHash(code) & 0xffff, 4, 16, QLatin1Char('0'));
    };

    const GLWidget *g = ui->glWidget;
    const bool isImplicit = (implicitMode());
    const int meshIdx = g ? g->activeMeshPart() : -1;

    // Codice della FASCIA attiva, quando l'ambito e' "Mesh": e' il terzo canale,
    // quello che ne' m_textureCode ne' m_customFragmentCode rappresentano.
    QString meshCode;
    if (g && g->getEngine() && meshIdx >= 0) {
        const auto &parts = g->getEngine()->getMeshParts();
        if (meshIdx < (int)parts.size() && parts[meshIdx].hasCustomTexture)
            meshCode = parts[meshIdx].textureCode;
    }

    const QString editorText = (m_currentScriptMode == ScriptModeTexture)
                             ? scriptText(shownScriptSlot()) : QString();
    const QString lineTexText = ui->lineTexture ? m_scene.rm.texture : QString();
    const QString dispFieldText = ui->lineVariations ? m_scene.rm.displacement : QString();
    const QString dispEngine = g ? g->currentDisplacementCode() : QString();

    qDebug().noquote() << QString(
        "TEXP %1 | mode=%2 mesh=%3/%4 | F=%5 | "
        "RM(m_textureCode) D=%6 %7 | PARAM(m_customFragmentCode) D=%8 %9 | "
        "MESH D=%10 %11 | lineTexture D=%12 %13 | editor D=%14 %15 | "
        "surfCode D=%16 %17 | texEnabled=%18 chk=%19 libName='%20' | "
        "COL picker1=%21 picker2=%22 gpu1=%23 gpu2=%24 | "
        "GATE focused=%25 recPath='%26' | "
        "DISP campo R=%27 %28 | motore R=%29 %30")
        .arg(QString::fromUtf8(tag), -28)
        .arg(isImplicit ? "RM" : "PAR")
        .arg(meshIdx).arg(g ? g->meshPartCount() : 0)
        .arg(m_scene.constants.f)
        .arg(densityOf(g ? g->currentTextureCode() : QString()))
        .arg(sigOf(g ? g->currentTextureCode() : QString()))
        .arg(densityOf(g ? g->currentParametricTextureCode() : QString()))
        .arg(sigOf(g ? g->currentParametricTextureCode() : QString()))
        .arg(densityOf(meshCode)).arg(sigOf(meshCode))
        .arg(densityOf(lineTexText)).arg(sigOf(lineTexText))
        .arg(densityOf(editorText)).arg(sigOf(editorText))
        .arg(densityOf(m_scene.surfaceTextureCode)).arg(sigOf(m_scene.surfaceTextureCode))
        .arg(g && g->isTextureEnabled() ? 1 : 0)
        .arg(ui->chkBoxTexture && ui->chkBoxTexture->isChecked() ? 1 : 0)
        .arg(m_scene.textureLibName)
        // COLORI. picker1/2 sono quelli del bersaglio (la fascia selezionata
        // se ne ha di propri), gpu1/gpu2 i due slot globali che il Save del
        // record scrive. Fuori dall'ambito Mesh coincidono per costruzione.
        .arg(surfaceTexColor(1).name(), surfaceTexColor(2).name())
        .arg(ui->glWidget ? ui->glWidget->globalTexColor1().name() : QString("?"),
             ui->glWidget ? ui->glWidget->globalTexColor2().name() : QString("?"))
        // GATE della voce di menu: esattamente cio' che il menu contestuale
        // valuta per decidere se abilitare "Sync Focused Texture". Stamparlo
        // qui evita di dedurlo: se e' 0 quando il disallineamento c'e', il
        // guasto e' dentro focusedTextureLibraryItem; se e' 1 ma la voce resta
        // grigia, il guasto e' nell'altra condizione (isLoaded/recPath).
        .arg(focusedTextureLibraryItem() ? 1 : 0)
        .arg(QFileInfo(m_currentRecordPath).fileName())
        // DISPLACEMENT, due livelli come i colori: il CAMPO (lineVariations, cio'
        // che l'utente legge) e il MOTORE (m_displacementCode, cio' che si
        // disegna). Se divergono, il guasto e' fra i due; se il campo mostra un
        // rilievo e il preset ne ha un altro, il guasto e' in chi lo applica.
        .arg(reliefOf(dispFieldText), sigOf(dispFieldText))
        .arg(reliefOf(dispEngine), sigOf(dispEngine));
#else
    Q_UNUSED(tag);
#endif
}

bool MainWindow::syncFocusedTextureFromLibrary()
{
    SE_TEXP("sync:PRIMA");

    // Le due texture del record, ognuna con la sua ancora: si aggiornano quelle
    // che ne hanno bisogno, anche entrambe, in un solo comando. Prima esisteva la
    // sola superficie, e uno sfondo rimasto indietro rispetto alla libreria non
    // aveva modo di essere riallineato (ne' di ritrovare il focus in Library).
    // LE FASCE, ognuna con la sua ancora (MeshPart::textureLibName), tutte in un
    // colpo: prima il comando guardava la sola texture GLOBALE -- ancora e codice
    // -- e in ambito "Mesh" ne scriveva il codice nella fascia selezionata, che
    // poteva venire da tutt'altra voce. E aggiornava una fascia per volta: i
    // labirinti di Clifford, cinque fasce con la stessa texture, chiedevano
    // cinque comandi.
    // Le tre liste si calcolano PRIMA di applicare: ogni sync cambia lo stato
    // che le altre confrontano.
    const LibraryItem *surfLib = focusedTextureLibraryItem();
    const LibraryItem *bgLib   = focusedBgTextureLibraryItem();
    const QVector<MeshTextureSync> meshSyncs = focusedMeshTextureLibraryItems();
    bool changed = false;
    if (surfLib) changed = syncSurfaceTextureFrom(surfLib) || changed;
    if (bgLib)   changed = syncBackgroundTextureFrom(bgLib) || changed;
    if (!meshSyncs.isEmpty()) changed = syncMeshTexturesFrom(meshSyncs) || changed;
    if (!changed) return false;

    // DISPLAY DELLA FASCIA SELEZIONATA per ultimo: editor, checkbox e colori
    // devono mostrare la texture della parte attiva, che il sync delle fasce puo'
    // aver appena cambiato -- e che il sync della globale non deve coprire.
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0)
        syncAppearanceControlsToActiveMesh();

    // GLI SLIDER SEGUONO IL CODICE, come l'orologio nei due rami. Una volta sola,
    // qualunque texture sia cambiata: updateConstantsUIState legge tutti i codici.
    //
    // E' IL CASO PER CUI IL COMANDO ESISTE: una texture aggiornata in libreria
    // che ha ACQUISITO uno slider (o che ne ha perso uno). Il codice nuovo e'
    // stato scritto nei campi a blockSignals -- e deve esserlo, o textChanged
    // azzererebbe m_scene.textureLibName -- ma quel blocco ferma anche
    // updateConstantsUIState, che e' l'unico punto che decide quali costanti
    // sono "usate" e quindi quali slider sono accesi.
    // Senza questa chiamata il Sync portava a schermo una texture che usa F
    // lasciando lo slider F SPENTO e inerte: il comando sembrava non aver
    // funzionato, e la densita' restava quella di prima perche' nessuno
    // spingeva il valore nel motore.
    //
    // Va DOPO la scrittura dei campi: updateConstantsUIState giudica leggendoli
    // (in RM lineTexture + lineVariations, ~7464), quindi chiamarla prima la
    // farebbe decidere sul codice VECCHIO -- e' lo stesso errore d'ordine del
    // caricamento record.
    // Le costanti appena sbloccate vanno anche SPINTE nel motore: sono uniform,
    // non serve ricompilare, ma la GPU ha ancora i valori di prima
    // (refreshConstants: ricalcolo + valori dei campi nel motore).
    refreshConstants();

    // Messaggi delle texture aggiornate, una volta sola per entrambe: si
    // mostrano i SOLI messaggi delle texture, non quello della SCENA -- gia'
    // visto al caricamento del record. Qui non e' cambiata la scena.
    showTextureHintsOnly(std::max(m_currentTextureHintSeconds, m_currentBgTextureHintSeconds));

    // Lavoro non salvato: il record sul disco ha ancora il codice vecchio (lo
    // dice il confronto dello stato, textureModuleDirty).
    updateMasterButtonState();
    syncTextureTreeSelection();

    SE_TEXP("sync:DOPO");
    return true;
}

// SUPERFICIE: il corpo storico del comando. Ritorna false se non ha toccato
// nulla (costante contesa annullata, shader che non compila).
bool MainWindow::syncSurfaceTextureFrom(const LibraryItem *lib)
{
    const QString newCode = lib->textureCode.isEmpty() ? lib->scriptCode : lib->textureCode;
    if (newCode.trimmed().isEmpty()) return false;

    // Costante contesa: stessa domanda del caricamento normale. Il codice nuovo
    // puo' rivendicare una lettera che la superficie sta gia' usando, e quello
    // slider ne muoverebbe due insieme. Annullando non si tocca nulla.
    // forBackground = false SEMPRE: qui arriva la texture di SUPERFICIE. Prima
    // si passava lo stato del radio Surface/Background, e col Renderer su
    // Background il confronto avveniva dalla parte sbagliata.
    if (!confirmTextureConstantClash(newCode, lib->displacementCode, /*forBackground=*/false))
        return false;

    const bool isImplicit = (implicitMode());

    // blockSignals: questo codice VIENE dalla libreria, non e' una digitazione.
    // Senza, textChanged azzererebbe m_scene.textureLibName (~2315) e il record
    // perderebbe proprio l'ancora che ha permesso di trovare la texture.
    // (In parametrico il codice va nello slot della texture di superficie, qui
    // sotto: l'editor lo mostra solo se il dock e' su quello slot -- in ambito
    // "Mesh" mostra la FASCIA selezionata.)
    if (isImplicit && ui->lineTexture) {
        setRmText(&ImplicitTexts::texture, newCode);
    }

    // Lo SLOT dello script (l'intenzione, da cui l'editor si ricostruisce
    // cambiando scheda) subito, in qualunque ambito: qui arriva sempre la
    // texture GLOBALE (le fasce hanno la loro via, syncMeshTexturesFrom). La
    // copia APPLICATA m_scene.surfaceTextureCode la scrive piu' sotto il ramo
    // parametrico, solo se il codice compila: prima veniva scritta qui, e un
    // codice di libreria che non reggeva risultava applicato.
    // Solo in PARAMETRICO: in Ray Marching la texture vive nel campo lineTexture
    // (e' quello che la Library confronta per riconoscere la texture attiva), e
    // i due membri parametrici restano vuoti.
    if (!isImplicit) setScriptText(SlotSurfaceTexture, newCode);

    // m_currentTexturePresetPath NON si tocca, ed e' una scelta.
    // Quel campo dice "la scena mostra QUEL preset per intero", ed e' una delle
    // condizioni con cui handleTextureSelection decide che una texture cliccata
    // e' gia' quella attiva (isMatch). Dopo un Sync pero' la scena ha il CODICE
    // del preset e i COLORI del record: non e' lo stesso stato. Allineandolo,
    // isMatch diventava vero e cliccare quella voce in libreria non applicava
    // piu' nulla -- restavano i colori del record, con il preset che sembrava
    // caricato a meta'.
    // Lasciandolo com'e', il click resta un caricamento vero e porta il preset
    // completo, colori compresi.

    if (ui->lineVariations) {
        setRmText(&ImplicitTexts::displacement, lib->displacementCode);
    }

    // APPLICAZIONE AL MOTORE: due vie diverse, e sbagliarle NON da' errori --
    // da' una scena che continua a disegnare il codice VECCHIO mentre l'editor
    // mostra quello nuovo.
    //
    // setTextureCode() scrive m_textureCode, che finisce SOLO in
    // createImplicitFragmentShader: e' la via del Ray Marching. Il fragment
    // PARAMETRICO non lo legge mai -- li' il codice della texture globale sta
    // in m_customFragmentCode (validateAndApplyParametricShader) e quello delle
    // fasce in MeshPart::textureCode (setActiveMeshTexture). Chiamando
    // setTextureCode su una superficie parametrica si aggiornava un campo che
    // nessuno di quei due shader guarda: rebuildShader ricompilava con
    // m_customFragmentCode invariato e la figura restava identica.
    // E' il sintomo riferito: l'editor diceva "density 12.0", lo schermo
    // continuava a disegnare la 6.0 del record.
    if (ui->glWidget) {
        ui->glWidget->setDisplacementCode(lib->displacementCode);

        if (isImplicit) {
            ui->glWidget->setTextureCode(newCode);
            ui->glWidget->rebuildShader();
        } else {
            // PARAMETRICO, in qualunque ambito (la texture e' la globale):
            // la via del Run/della Library, cioe'
            // la compilazione vera del fragment parametrico. Se lo shader non
            // compila NON si tocca nulla -- resta in piedi il precedente --, si
            // dice perche', e si esce senza sporcare la scena: il codice puo'
            // essere stato modificato in libreria in un modo che qui non regge
            // (una costante contesa risolta in altro modo, un'altra direttiva).
            if (!commitSurfaceTextureCode(newCode)) {
                showShaderError("Syntax Error (Parametric Texture)",
                                ui->glWidget->getShaderError());
                return false;
            }
        }

        // L'OROLOGIO SEGUE IL CODICE. Il codice aggiornato puo' aver ACQUISITO
        // o PERSO la variabile t: lasciando il clock com'era, una texture
        // diventata animata restava ferma (e una diventata statica teneva
        // acceso un orologio che non muove piu' nulla, col master su "STOP" a
        // vuoto). Nessuna guardia sul master, come nei due percorsi gemelli:
        // il Sync e' un comando esplicito sul modulo texture.
        m_userStoppedTexClock = false;
        if (isImplicit) {
            // In RM il codice della texture sta in lineTexture/lineVariations,
            // NON in allSurfaceTextureCode() (che raccoglie la globale
            // parametrica e le per-mesh): leggendo quella, una texture RM
            // animata risultava sempre statica e il Sync la lasciava ferma.
            // Stessa distinzione che fa il riclic della Library (~13254).
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(ui->lineTexture ? m_scene.rm.texture : QString())
                || hasTimeVariable(ui->lineVariations ? m_scene.rm.displacement : QString()));
        } else {
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(allSurfaceTextureCode()));
        }

        ui->glWidget->update();
    }

    // Il messaggio della texture puo' essere cambiato insieme al codice: se ora
    // nomina uno slider, e' l'informazione che serve subito dopo.
    // Solo lo STATO qui: lo mostra syncFocusedTextureFromLibrary, una volta sola
    // per le due texture (vedi showTextureHintsOnly). Anche vuoto: la versione
    // aggiornata puo' aver tolto il messaggio.
    m_currentTextureHintText    = lib->hintText.trimmed();
    m_currentTextureHintSeconds = lib->hintSeconds > 0 ? lib->hintSeconds : m_currentHintSeconds;

    return true;
}

void MainWindow::forgetBackgroundTexture()
{
    m_scene.bgTextureCode.clear();
    setScriptText(SlotBackgroundTexture, QString());
    m_scene.bgTextureLibName.clear();
    m_currentBgTextureHintText.clear();
    // Ricarica la default e spegne lo script di sfondo (m_bgIsScript): chi
    // riaccende lo sfondo riparte da li', non dall'ultima immagine usata.
    if (ui->glWidget) ui->glWidget->setBackgroundTexture("background.png");
}

// SFONDO. Stesso contratto della superficie -- arriva il CODICE della voce di
// libreria, restano i colori (m_scene.bgTexColor1/2), le costanti e l'inquadratura 2D
// del record -- ma per una via propria:
//  - NON si passa dal ramo A di onApplyTextureScriptClicked: sceglie fra sfondo e
//    superficie guardando il radio del Renderer (il Sync parte dal menu della
//    libreria, col radio dove capita) e azzera zoom/pan/rotazione, che qui vanno
//    conservati;
//  - un tag //IMG: gia' presente si CONSERVA davanti al codice nuovo: e' lo
//    sfondo misto immagine+script, come nel ramo sfondo di handleTextureSelection.
//    La voce di libreria porta solo lo script;
//  - il messaggio va in m_currentBgTextureHintText, il suo: quello della texture
//    di SUPERFICIE lo salva il record, e usarlo per lo sfondo lo sovrascriverebbe.
bool MainWindow::syncBackgroundTextureFrom(const LibraryItem *lib)
{
    QString newCode = lib->textureCode.isEmpty() ? lib->scriptCode : lib->textureCode;
    if (newCode.trimmed().isEmpty()) return false;

    // Costante contesa dal punto di vista dello SFONDO: contro la superficie e
    // la sua texture. Annullando non si tocca nulla.
    if (!confirmTextureConstantClash(newCode, QString(), /*forBackground=*/true))
        return false;

    static const QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)",
                                          QRegularExpression::MultilineOption);
    const QRegularExpressionMatch imgMatch = imgRe.match(m_scene.bgTextureCode);
    if (imgMatch.hasMatch() && !newCode.contains("//IMG:"))
        newCode = "//IMG:" + imgMatch.captured(1).trimmed() + "\n" + newCode;

    // Dry-run PRIMA di toccare lo stato: se non compila resta lo sfondo di prima.
    if (ui->glWidget && !ui->glWidget->validateAndApplyBackgroundShader(newCode)) {
        showShaderError("Background Shader Error", ui->glWidget->getShaderError());
        return false;
    }

    m_scene.bgTextureCode = newCode;
    // Lo slot, e con lui l'editor se sta mostrando lo sfondo: e' la libreria,
    // non una digitazione.
    setScriptText(SlotBackgroundTexture, newCode);
    // Messaggio dello sfondo: e' cio' che dice a cosa serve uno slider appena
    // acquisito (il caso per cui il comando esiste). Solo lo stato: lo mostra il
    // chiamante, una volta per le due texture. Anche vuoto, come la superficie.
    m_currentBgTextureHintText    = lib->hintText.trimmed();
    m_currentBgTextureHintSeconds = lib->hintSeconds > 0 ? lib->hintSeconds : m_currentHintSeconds;

    // L'orologio segue il codice, come per la superficie: il codice nuovo puo'
    // aver acquisito o perso la variabile t. Comando esplicito: riarma lo stop
    // manuale dello sfondo, senza guardia sul master.
    m_userStoppedBgClock = false;
    if (ui->glWidget) {
        ui->glWidget->setBackgroundTextureAnimating(hasTimeVariable(newCode));
        ui->glWidget->update();
    }
    return true;
}

// Voce di libreria da cui viene la texture DI SUPERFICIE, e SOLO se c'e'
// davvero qualcosa da aggiornare: serve sia al comando sia al gate della voce di
// menu, che deve restare spenta quando non farebbe nulla.
// nullptr se: la texture non viene da libreria (nessun libName -- scritta a
// mano, o record salvato prima che il campo esistesse), la voce non esiste piu'
// (rinominata o cancellata), oppure il codice e' gia' identico.
const LibraryItem *MainWindow::focusedTextureLibraryItem() const
{
    const LibraryItem *item = LibraryTreeFocus::textureNamed(ui->treeTextures, m_libraryManager, m_scene.textureLibName);
    if (!item) return nullptr;

    const bool isImplicit = (implicitMode());
    const QString activeCode = isImplicit && ui->lineTexture
                             ? m_scene.rm.texture
                             : m_scene.surfaceTextureCode;
    // Il DISPLACEMENT fa parte della texture quanto il colore, e va confrontato
    // anche lui: una texture il cui solo rilievo e' cambiato ha eccome qualcosa
    // da sincronizzare, ma guardando il solo codice colore la voce sarebbe
    // rimasta spenta -- e il comando, che il displacement lo aggiorna gia',
    // sarebbe risultato irraggiungibile proprio nel caso che lo richiede.
    const QString activeDisp = ui->lineVariations ? m_scene.rm.displacement : QString();

    const QString libCode = item->textureCode.isEmpty() ? item->scriptCode : item->textureCode;
    // Gia' allineati (codice E rilievo): niente da sincronizzare.
    if (TextureCode::cleanForComparison(libCode) == TextureCode::cleanForComparison(activeCode)
        && TextureCode::cleanForComparison(item->displacementCode) == TextureCode::cleanForComparison(activeDisp))
        return nullptr;
    return item;
}

// Gemella per la texture DI SFONDO, con la sua ancora (m_scene.bgTextureLibName).
// Differenze dalla superficie:
//  - niente displacement: lo sfondo non ha rilievo;
//  - le voci IMMAGINE si escludono: il loro legame e' il nome del file nel tag
//    //IMG:, non il codice, e non c'e' codice da portare. TextureCode::cleanForComparison
//    toglie il tag, quindi uno sfondo misto immagine+script si confronta sul
//    solo script, che e' cio' che la voce di libreria contiene;
//  - vale anche con lo sfondo SPENTO: il codice resta nel record, e aggiornarlo
//    e' comunque cio' che il comando promette.
const LibraryItem *MainWindow::focusedBgTextureLibraryItem() const
{
    if (m_scene.bgTextureCode.trimmed().isEmpty()) return nullptr;
    const LibraryItem *item = LibraryTreeFocus::textureNamed(ui->treeTextures, m_libraryManager, m_scene.bgTextureLibName);
    if (!item || item->isImage) return nullptr;

    const QString libCode = item->textureCode.isEmpty() ? item->scriptCode : item->textureCode;
    if (TextureCode::cleanForComparison(libCode) == TextureCode::cleanForComparison(m_scene.bgTextureCode))
        return nullptr;
    return item;
}

// Gemella per le FASCE: tutte quelle con un'ancora (MeshPart::textureLibName) la
// cui voce esiste e ha un codice diverso da quello che la fascia disegna. Stesso
// criterio della superficie, ma confrontando il codice della PARTE -- prima il
// gate guardava la sola texture globale anche con una fascia selezionata.
// Il tag //IMG: non conta (TextureCode::cleanForComparison lo toglie): una fascia
// immagine+script si confronta sul solo script, che e' cio' che la voce contiene.
// Anche una fascia SPENTA: il codice resta nel record e aggiornarlo e' cio' che
// il comando promette, come per lo sfondo.
// Solo parametrico: in Ray Marching le fasce non esistono.
QVector<MainWindow::MeshTextureSync> MainWindow::focusedMeshTextureLibraryItems() const
{
    QVector<MeshTextureSync> out;
    if (!ui->glWidget || !ui->glWidget->getEngine()) return out;
    if (implicitMode()) return out;
    const auto &parts = ui->glWidget->getEngine()->getMeshParts();
    for (int k = 0; k < (int)parts.size(); ++k) {
        const MeshPart &p = parts[k];
        if (!p.hasCustomTexture || p.textureLibName.isEmpty()) continue;
        const LibraryItem *item = LibraryTreeFocus::textureNamed(ui->treeTextures, m_libraryManager, p.textureLibName);
        if (!item || item->isImage) continue;
        const QString libCode = item->textureCode.isEmpty() ? item->scriptCode : item->textureCode;
        if (TextureCode::cleanForComparison(libCode) == TextureCode::cleanForComparison(p.textureCode)) continue;
        out.append({k, item});
    }
    return out;
}

// FASCE. Stesso contratto della superficie: arriva il CODICE della voce, restano
// colori, trasformazione 2D, acceso/spento e ancora della parte. Un tag //IMG:
// gia' presente si conserva davanti al codice nuovo (fascia immagine+script,
// come nel ramo sfondo): la voce porta solo lo script.
// La domanda sulla costante contesa si fa UNA volta per voce, non per fascia:
// cinque fasce con la stessa texture farebbero cinque popup identici. Annullando,
// le fasce di quella voce vengono saltate e le altre proseguono.
// Una sola ricompilazione, dopo l'ultima fascia.
bool MainWindow::syncMeshTexturesFrom(const QVector<MeshTextureSync> &items)
{
    if (items.isEmpty() || !ui->glWidget || !ui->glWidget->getEngine()) return false;
    static const QRegularExpression imgRe(R"(^\s*//IMG:.*$)", QRegularExpression::MultilineOption);

    QHash<const LibraryItem *, bool> accepted;
    bool changed = false;
    for (const MeshTextureSync &s : items) {
        QString newCode = s.lib->textureCode.isEmpty() ? s.lib->scriptCode : s.lib->textureCode;
        if (newCode.trimmed().isEmpty()) continue;
        if (!accepted.contains(s.lib))
            accepted.insert(s.lib, confirmTextureConstantClash(newCode, QString(),
                                                               /*forBackground=*/false));
        if (!accepted.value(s.lib)) continue;

        const auto &parts = ui->glWidget->getEngine()->getMeshParts();
        if (s.part < 0 || s.part >= (int)parts.size()) continue;
        const QRegularExpressionMatch img = imgRe.match(parts[s.part].textureCode);
        if (img.hasMatch() && !imgRe.match(newCode).hasMatch())
            newCode = img.captured(0).trimmed() + "\n" + newCode;

        // L'orologio segue il codice, come per la superficie: nessuna guardia sul
        // master, il Sync e' un comando esplicito sul modulo texture.
        if (!ui->glWidget->setMeshPartTextureCode(s.part, newCode, hasTimeVariable(newCode)))
            continue;
        changed = true;
        // Messaggio della voce aggiornata, come per la superficie -- ma solo se
        // ne ha uno: lo slot e' unico per tutte le fasce (vedi il ramo per-mesh
        // di handleTextureSelection). Lo mostra syncFocusedTextureFromLibrary.
        if (!s.lib->hintText.trimmed().isEmpty()) {
            m_currentTextureHintText    = s.lib->hintText.trimmed();
            m_currentTextureHintSeconds = s.lib->hintSeconds > 0 ? s.lib->hintSeconds
                                                                 : m_currentHintSeconds;
        }
    }
    if (!changed) return false;

    ui->glWidget->rebuildShader();
    m_userStoppedMeshTexClock = false;
    ui->glWidget->update();
    return true;
}

void MainWindow::uncheckInExclusiveGroup(QAbstractButton *btn)
{
    if (!btn || !btn->isChecked()) return;
    QButtonGroup *grp = btn->group();
    bool ob = btn->blockSignals(true);
    if (grp && grp->exclusive()) {
        // setChecked(false) sull'unico acceso di un gruppo esclusivo è un no-op:
        // togliamo l'esclusività il tempo di deselezionare, poi la ripristiniamo.
        grp->setExclusive(false);
        btn->setChecked(false);
        grp->setExclusive(true);
    } else {
        btn->setChecked(false);
    }
    btn->blockSignals(ob);
}

void MainWindow::selectSurfaceColorTarget()
{
    // Seleziona Surface nella tripla (m_bgTargetGroup). Color1/2 sono un gruppo
    // INDIPENDENTE e NON vengono toccati: Surface + Color1 possono coesistere (sei sulla
    // superficie ed editi la sua tinta 1). A segnali bloccati: i chiamanti riallineano
    // già gli slider (di solito un onColorTargetChanged() subito dopo).
    showSurfaceTarget();
}

void MainWindow::onColorTargetChanged()
{
    // Il gruppo Background Controls segue il bersaglio Surface/Background come
    // gli slider colore qui sotto: vedi updateBackgroundControlsGate.
    updateBackgroundControlsGate();

    // Blocchiamo i segnali per evitare loop infiniti
    ui->sliderR->blockSignals(true);
    ui->sliderG->blockSignals(true);
    ui->sliderB->blockSignals(true);

    QColor target;
    bool slidersEnabled = true;

    // Stessa priorità di handleColorChange (gruppi indipendenti): la terna decide DOVE,
    // Color1/Color2 QUALE tinta. Qui calcoliamo il colore da MOSTRARE e se gli slider
    // hanno un effetto (altrimenti li disattiviamo).
    if (editingBorder()) {
        // Come la superficie: con una texture colorata gli slider mostrano il
        // suo Color 1/2; con una texture senza colori (immagine) sono inerti;
        // senza texture mostrano il colore del bordo.
        GLWidget *gl = ui->glWidget;
        const bool texOn = gl && gl->borderTextureShown();
        if (texOn && activeTextureUsesColors()) {
            target = ui->radioTexColor2->isChecked() ? gl->borderTexColor2() : gl->borderTexColor1();
        } else if (texOn) {
            target = Qt::black;
            slidersEnabled = false;
        } else {
            target = gl ? gl->borderColor() : QColor(Qt::white);
        }
    }
    else if (editingBackground()) {
        if (targetTextureOn() && activeTextureUsesColors()) {
            target = ui->radioTexColor2->isChecked() ? m_scene.bgTexColor2 : m_scene.bgTexColor1;
        } else if (targetTextureOn()) {
            // Texture di sfondo SENZA colori (immagine): copre lo sfondo, gli slider
            // non hanno effetto -> disattivati.
            target = Qt::black;
            slidersEnabled = false;
        } else {
            target = m_scene.bgColor;
        }
    }
    else { // target = Surface
        bool wireframeMode = (shownRenderMode() == 2);
        // Stessa condizione di handleColorChange: il DISPLAY deve dire cio' che
        // gli slider scriveranno. Con una fascia selezionata conta la texture
        // di QUELLA, non lo stato della globale.
        const bool texActiveHere =
            (ui->glWidget && ui->glWidget->activeMeshPart() >= 0)
                ? ui->glWidget->activeMeshTextureActive()
                : m_scene.surfaceTextureState;
        if (!wireframeMode && texActiveHere && activeTextureUsesColors()) {
            target = surfaceTexColor(ui->radioTexColor2->isChecked() ? 2 : 1);
        } else if (!wireframeMode && texActiveHere) {
            // Texture di superficie SENZA colori: copre la superficie, slider inerti.
            target = Qt::black;
            slidersEnabled = false;
        } else {
            // Nessuna texture colorata (o wireframe): si edita il colore
            // superficie/linee.
            //
            // Il colore da MOSTRARE e' quello dell'ambito corrente, letto dal
            // motore. (Una copia in MainWindow, m_currentSurfaceColor, non era
            // affidabile: handleColorChange ci scriveva anche mentre si colorava
            // una MESH -- il setColor instrada poi il valore nella parte --, e
            // restava contaminata dal colore
            // dell'ultima fascia toccata.
            // Effetto senza questo ramo: colorata una mesh e tornati ad "All",
            // la superficie restava verde ma gli slider mostravano il colore
            // della mesh -- e al primo spostamento "All" saltava a quel colore.
            // Lo stesso passando da "All" a "Mesh": showAppearance aveva gia'
            // mostrato il colore giusto della parte, e questa funzione -- che
            // gira per ultima -- lo sovrascriveva col globale.
            const int mi = ui->glWidget ? ui->glWidget->activeMeshPart() : -1;
            bool shown = false;
            if (mi >= 0 && ui->glWidget->getEngine()) {
                const auto &mparts = ui->glWidget->getEngine()->getMeshParts();
                if (mi < (int)mparts.size()) {
                    const MeshPart &mp = mparts[mi];
                    // Una parte senza colore proprio EREDITA il globale: si
                    // mostra quello, come fa syncAppearanceControlsToActiveMesh.
                    if (mp.hasCustomColor()) {
                        target = QColor::fromRgbF(mp.colorR, mp.colorG, mp.colorB);
                        shown = true;
                    }
                }
            }
            if (!shown) {
                // Ambito "All", o parte che eredita: il colore e' quello
                // GLOBALE del motore.
                if (ui->glWidget) {
                    float gr, gg, gb;
                    ui->glWidget->globalColor(gr, gg, gb);
                    target = QColor::fromRgbF(gr, gg, gb);
                }
            }
        }
    }

    // SCENA VUOTA (tasto NEW): non c'e' superficie da colorare. Lo SFONDO si',
    // quindi il blocco vale solo quando il target e' la superficie. Il controllo
    // sta qui, che e' la sede unica dello stato di questi slider: metterlo solo
    // in applyEmptySceneGating non bastava, perche' questa funzione gira DOPO
    // (la chiamano il reset stesso, i cambi di target, updateMasterButtonState)
    // e li riaccendeva.
    if (!editingBackground() && isSceneEmpty()) slidersEnabled = false;

    ui->sliderR->setEnabled(slidersEnabled);
    ui->sliderG->setEnabled(slidersEnabled);
    ui->sliderB->setEnabled(slidersEnabled);

    // Imposta gli slider al valore del colore selezionato
    ui->sliderR->setValue(target.red());
    ui->sliderG->setValue(target.green());
    ui->sliderB->setValue(target.blue());

    // Aggiorna anche le etichette numeriche
    ui->valR->setNum(target.red());
    ui->valG->setNum(target.green());
    ui->valB->setNum(target.blue());

    // Riattiva i segnali
    ui->sliderR->blockSignals(false);
    ui->sliderG->blockSignals(false);
    ui->sliderB->blockSignals(false);

    // Checkbox Texture: la sua vista, con la regola in un posto solo (spento
    // in wireframe sulla superficie o sulla fascia, e a scena vuota; sempre
    // acceso col bersaglio Background).
    refreshTextureCheckbox();
}

void MainWindow::scheduleTextureGeneration()
{
    // Controllo di sicurezza
    if (!ui->glWidget) return;

    // Evitiamo di generare l'immagine se il programma sta caricando un file
    if (m_blockTextureGen) return;

    // Genera la nuova scacchiera con i colori aggiornati
    generateTexture();
}

void MainWindow::handleTextureSelection(int index)
{
    SE_TEXP("libTex:ENTRATA");

    // 1. Recupera i dati
    const LibraryItem &data = m_libraryManager.getTexture(index);

    // COSTANTE CONTESA: si chiede PRIMA di toccare qualunque stato. La texture
    // in arrivo puo' rivendicare una lettera A..F gia' usata dalla superficie, e
    // quello slider ne muoverebbe due insieme. Non e' un errore -- puo' essere
    // voluto -- quindi e' una domanda, non un rifiuto; annullando si esce senza
    // aver modificato nulla, che e' la ragione per cui il controllo sta qui in
    // cima e non a meta' funzione.
    // Vale anche per lo SFONDO: legge lo stesso mathParams della superficie e
    // della texture, quindi una lettera contesa muove due cose insieme
    // esattamente come sul modulo di superficie.
    // Non se la texture fa cambiare modalita': la superficie in scena sta per
    // essere sostituita da quella di default, che non usa costanti, e la
    // domanda si sommerebbe al popup del cambio.
    // TEXTURE INCOMPATIBILE col bersaglio: un avviso, e niente cambia. Prima
    // di qualunque altra domanda: non si chiede di una costante contesa per
    // una texture che non verra' applicata.
    if (refuseIncompatibleTexture(data)) return;
    {
        const QString incomingTex = data.textureCode.isEmpty() ? data.scriptCode
                                                               : data.textureCode;
        if (!confirmTextureConstantClash(incomingTex, data.displacementCode,
                                         editingBackground()))
            return;
    }

    // Ricorda il file della texture caricata: serve come nome di default in salvataggio
    // (stessa logica dei record). Vuoto solo se non è stata caricata nessuna texture.
    m_currentTexturePresetPath = data.filePath;

    // A schermo ci sara' una texture di libreria, non lavoro dell'utente: per
    // il MODULO texture non c'e' piu' niente da proteggere (la conferma per la
    // texture PRECEDENTE l'ha gia' chiesta il chiamante, onExampleItemClicked).
    // La SCENA invece e' cambiata rispetto al suo file: il record la salva, e
    // l'avviso di scena la difende. All'uscita, quando la texture e' applicata.
    struct TexturePickedGuard {
        MainWindow *w;
        ~TexturePickedGuard() { w->markTexturePicked(); }
    } texturePickedGuard{this};

    static int lastTextureIndex = -1;
    static bool lastWasBg = false;
    bool isBg = editingBackground();

    // Se l'utente clicca di nuovo lo stesso script procedurale, scarta l'immagine!
    // Quella della SUPERFICIE, e solo in ambito All: prima valeva anche col
    // bersaglio Background, dove toglieva dal Save l'immagine della superficie
    // lasciandola a schermo (trovato dal test degli scenari). Con una fascia
    // selezionata la texture va alla fascia, e l'immagine della superficie non
    // e' cosa sua.
    const bool meshTarget = ui->glWidget && ui->glWidget->activeMeshPart() >= 0;
    if (index == lastTextureIndex && isBg == lastWasBg && !data.isImage
        && !isBg && !meshTarget && !editingBorder()) {
        if (ui->glWidget) {
            ui->glWidget->clearTexture();
            // clearTexture spegne anche la texture nel motore: la si riallinea
            // all'intenzione, che il riclic non cambia.
            applySurfaceTextureToEngine();
        }

        // Rimuove il tag //IMG: dallo script della texture di superficie
        QString currentText = m_scene.surfaceTextureScriptText;
        currentText.remove(QRegularExpression(R"(^\s*//IMG:\s*(.*)$\n?)", QRegularExpression::MultilineOption));
        setScriptText(SlotSurfaceTexture, currentText);
    }
    lastTextureIndex = index;
    lastWasBg = isBg;

    // 2. CONTROLLO MODALITÀ SFONDO: la texture va sullo sfondo, e qui finisce.
    if (editingBackground()) {
        applyLibraryTextureToBackground(data);
        return;
    }
    // ...e col bersaglio Border va sul bordo.
    if (editingBorder()) {
        applyLibraryTextureToBorder(data);
        return;
    }

    // =====================================================================
    // 2.5 MODALITA' DELLA TEXTURE: quella in scena. Una texture di modalita'
    // opposta e' gia' stata rifiutata in testa (refuseIncompatibleTexture), e
    // un'immagine prende la modalita' corrente.
    // =====================================================================
    const bool texIsImplicit = implicitMode();

    // =====================================================================
    // 3. APPLICAZIONE DATI TEXTURE (Separati per Parametrico / Implicito)
    // =====================================================================
    m_blockTextureGen = true;

    // I COLORI GLOBALI SI TOCCANO SOLO IN AMBITO "ALL". Con una fascia
    // selezionata la texture andra' su QUELLA (vedi il ramo per-mesh piu'
    // sotto), e i due slot globali sono quelli della texture di SUPERFICIE:
    // sovrascriverli faceva uscire la texture globale coi colori di quella
    // appena messa sulla fascia. I colori della fascia li cattura
    // setActiveMeshTexture nella MeshPart.
    const bool texGoesToMesh = ui->glWidget && ui->glWidget->activeMeshPart() >= 0
                               && !editingBackground();
    if (!texGoesToMesh && ui->glWidget) {
        if (data.hasCustomColors)
            ui->glWidget->setGlobalTextureColors(QColor(data.color1), QColor(data.color2));
        else
            ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
    }
    // Qui i colori del PRESET sono appena stati scritti. Se piu' avanti nella
    // sequenza risultano diversi, qualcuno li ha sovrascritti DOPO.
    SE_TEXP("libTex:colori-del-preset");

    if (ui->radioTexColor1->isChecked() || ui->radioTexColor2->isChecked()) {
        onColorTargetChanged();
    }
    SE_TEXP("libTex:post-onColorTargetChanged");

    // Path dell'IMMAGINE da caricare: per un file immagine diretto e' filePath;
    // per una texture-immagine salvata come JSON e' imagePath (estratto dal tag
    // //IMG:), perche' filePath punta al .json e non alla PNG.
    QString imgSrc = data.imagePath.isEmpty() ? data.filePath : data.imagePath;

    // DIVIDIAMO IL FLUSSO IN BASE ALLA NATURA DELLA TEXTURE
    // Un "false" vuol dire che il lavoro e' finito li' (texture o immagine
    // messa su una fascia, texture Ray Marching che non compila).
    const bool applied = texIsImplicit ? applyLibraryTextureRM(data, imgSrc)
                                       : applyLibraryTextureParametric(data, imgSrc, texGoesToMesh);
    if (!applied) return;

    m_blockTextureGen = false;
    updateFlatPreviewButton();
    restartLibraryTextureClocks();
    finishLibraryTexturePick(data);
}

// LA TEXTURE DI LIBRERIA SUL BORDO (handleTextureSelection, bersaglio Border).
// Come per una fascia: codice, colori, inquadratura e nome della voce vanno nel
// motore, e il bordo ha il suo orologio. Solo texture PARAMETRICHE (procedurali
// e immagini): quelle Ray Marching sono funzioni di un punto 3D, il bordo ha
// coordinate di superficie (lungo il lato, attorno al tubo), e le rifiuta
// refuseIncompatibleTexture prima di arrivare qui.
void MainWindow::applyLibraryTextureToBorder(const LibraryItem &data)
{
    GLWidget *gl = ui->glWidget;
    if (!gl) return;
    // (Le Ray Marching le ha gia' rifiutate refuseIncompatibleTexture.)

    QString code;
    if (data.isImage) {
        code = "//IMG:" + (data.imagePath.isEmpty() ? data.filePath : data.imagePath);
    } else {
        code = data.scriptCode;
        // Lo script che campiona un'immagine trova quella che il bordo ha gia'
        // (il gesto "prima l'immagine, poi la sua animazione"), come le fasce.
        const QString ownImage = GLWidget::imagePathInTextureCode(gl->borderTextureCode());
        if (!ownImage.isEmpty() && TextureCode::samplesImage(code))
            code = "//IMG:" + ownImage + "\n" + code;
    }

    gl->setBorderTexColors(data.hasCustomColors ? QColor(data.color1) : QColor::fromRgbF(0.20f, 0.80f, 0.20f),
                           data.hasCustomColors ? QColor(data.color2) : QColor(Qt::black));
    gl->setBorderTexTransform(data.zoom, QVector2D(data.panX, data.panY), data.rotation);
    gl->setBorderTexture(code, true);
    gl->setBorderTextureLibName(data.name);
    // Il messaggio della voce, come per lo sfondo: anche vuoto, perche' la
    // texture di prima puo' averne lasciato uno che ora va tolto da schermo.
    gl->setBorderTextureHint(data.hintText, data.hintSeconds);
    showLoadedTextureHint(QString(), QString(), data.hintText, data.hintSeconds);
    // Applicare una texture e' un comando esplicito: un'animata parte subito,
    // anche a master fermo, come sulle fasce.
    m_userStoppedBorderTexClock = false;
    gl->setBorderTextureAnimating(hasTimeVariable(code));

    m_blockTextureGen = false;
    updateTextureUIState(true, true);
    refreshTextureCheckbox();
    updateMasterButtonState();   // ...e il dock Script mostra la texture nuova
    syncTextureTreeSelection();
    refreshConstants();   // la texture puo' usare A..F
    gl->update();
}

// LA TEXTURE DI LIBRERIA COME SFONDO (handleTextureSelection, bersaglio
// Background). Il resto di handleTextureSelection non vale per lo sfondo.
void MainWindow::applyLibraryTextureToBackground(const LibraryItem &data)
{
    // ANCORA E MESSAGGIO DELLO SFONDO: da quale voce di libreria viene, e
    // cosa dice dei suoi slider. Qui, in testa al ramo, per immagini e
    // procedurali: sono i gemelli di m_scene.textureLibName e
    // m_currentTextureHintText, che questo ramo non tocca perche' sono della
    // superficie. Il blocco dell'hint in coda alla funzione NON vale per lo
    // sfondo: questo ramo esce prima. Anche a hint vuoto: lo sfondo di prima
    // puo' averne lasciato uno, che ora va tolto da schermo.
    m_scene.bgTextureLibName     = data.name.trimmed();
    m_currentBgTextureHintText    = data.hintText.trimmed();
    m_currentBgTextureHintSeconds = data.hintSeconds;
    showLoadedTextureHint(QString(), m_currentBgTextureHintText, QString(), data.hintSeconds);

    if (data.hasCustomColors) {
        m_scene.bgTexColor1 = QColor(data.color1);
        m_scene.bgTexColor2 = QColor(data.color2);
    } else {
        m_scene.bgTexColor1 = QColor::fromRgbF(0.20f, 0.80f, 0.20f); // Verde default
        m_scene.bgTexColor2 = Qt::black;
    }

    if (data.isImage) {
        // Come per la texture di superficie: se e' un JSON-immagine, la PNG
        // sta in imagePath (filePath e' il .json).
        QString bgImgSrc = data.imagePath.isEmpty() ? data.filePath : data.imagePath;
        ui->glWidget->setBackgroundTexture(bgImgSrc);
        m_scene.bgTextureCode = "//IMG:" + bgImgSrc;
        // Il percorso dell'immagine sta nel motore (backgroundImagePath),
        // come per la superficie: e' da li' che il salvataggio ricostruisce
        // il tag, che dentro m_scene.bgTextureCode viene riscritto da piu' punti.

        // L'immagine SOSTITUISCE la procedurale: va azzerato anche lo slot
        // dello script, non solo il codice attivo. Senza questo il vecchio
        // script sopravviveva in m_scene.bgTextureScriptText e i punti che lo
        // rileggono -- onRunScriptClicked (~8051), il ramo A di
        // onApplyTextureScriptClicked (~9706) e applyBackgroundTextureIfNeeded
        // -- lo riapplicavano al primo Run / Stop-Start, facendo tornare la
        // procedurale mentre in Library restava evidenziata l'immagine.
        // Nessuno di quei tre puo' accorgersene da solo: il residuo NON
        // contiene il tag //IMG:, e' lo script vecchio puro, quindi ogni
        // guardia basata sul testo lo scambia per codice da applicare.
        // Stesso trattamento del ramo "texture implicita incompatibile" piu'
        // sotto, che azzera gia' questa coppia.
        // Lo slot prende il TAG (come per la superficie): e' lo script di
        // questo sfondo, senza codice da riapplicare -- quei tre punti
        // cercano return/vec3/vec4/mainImage e sul solo tag non fanno nulla.
        // Svuotato, col dock Script su un altro modulo l'editor tornava
        // vuoto sullo sfondo (trovato dal test degli scenari).
        setScriptText(SlotBackgroundTexture, m_scene.bgTextureCode);

        if (ui->glWidget) {
            ui->glWidget->setBackgroundFraming(data.zoom, QVector2D(data.panX, data.panY), data.rotation);
        }
    }
    else {
        // (Le texture Ray Marching le ha gia' rifiutate refuseIncompatibleTexture,
        // in testa a handleTextureSelection: lo sfondo non si tocca piu'.)

        // CARICAMENTO TEXTURE PROCEDURALE 2D
        QString newCode = data.scriptCode;

        // 1. Controlliamo se c'era già un'immagine di sfondo attiva.
        // Questo permette di mixare immagini e codice procedurale al primo clic.
        // FONTE PRIMARIA il motore (backgroundImagePath: l'immagine attiva
        // e' quella caricata li'), lo script dello sfondo solo come ripiego
        // per l'unico caso che il motore non vede: un tag incollato a mano
        // dall'utente e non ancora eseguito.
        QString keepImage = backgroundImagePath();
        if (keepImage.isEmpty()) {
            QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
            const QRegularExpressionMatch imgMatch = imgRe.match(m_scene.bgTextureScriptText);
            if (imgMatch.hasMatch()) keepImage = imgMatch.captured(1).trimmed();
        }

        if (!keepImage.isEmpty() && !newCode.contains("//IMG:")) {
            newCode = "//IMG:" + keepImage + "\n" + newCode;
        }
        // Se dopo il mix il tag NON c'e', il Run qui sotto toglie l'immagine
        // dal motore (onApplyTextureScriptClicked, ramo A).

        // Come per la superficie: lo slot subito, la copia applicata solo
        // se compila (onApplyTextureScriptClicked, ramo A).
        setScriptText(SlotBackgroundTexture, newCode);

        // 2-3. Il metodo centralizzato legge lo slot dello sfondo (fa
        // parsing, carica immagini, compila e fa l'Update GPU). L'editor
        // non fa da tramite: mostra lo sfondo solo se il dock e' li'.
        onApplyTextureScriptClicked();

        // 4. Applica proprietà aggiuntive di trasformazione
        if (ui->glWidget) {
            ui->glWidget->setBackgroundFraming(data.zoom, QVector2D(data.panX, data.panY), data.rotation);
        }

        // Riallinea gli slider colore al nuovo script di sfondo: se la texture
        // procedurale usa u_col1/u_col2 devono riattivarsi (m_scene.bgTextureCode è già
        // aggiornato qui sopra). Senza questa chiamata, dopo una texture di sfondo
        // SENZA colori (es. immagine di default) gli slider restavano disattivati
        // a zero anche caricando poi una texture procedurale CON colori, perché
        // questo ramo procedurale terminava senza rinfrescare onColorTargetChanged.
        onColorTargetChanged();

        // Stesso discorso per le COSTANTI (vedi la coda del ramo, piu' sotto).
        refreshConstants();

        return;
    }

    ui->glWidget->setBackgroundTextureEnabled(true);
    refreshTextureCheckbox();

    // Picker Colore solo se lo sfondo usa quel colore (un'immagine no; indipendenti).
    bool bgCol1 = m_scene.bgTextureCode.contains("u_col1");
    bool bgCol2 = m_scene.bgTextureCode.contains("u_col2");
    ui->radioTexColor1->setEnabled(bgCol1);
    ui->radioTexColor2->setEnabled(bgCol2);
    if (bgCol1 || bgCol2) {
        QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
        bool oldRad = target->blockSignals(true);
        target->setChecked(true);
        target->blockSignals(oldRad);
    }

    onColorTargetChanged();
    updateFlatPreviewButton();
    // COSTANTI: lo sfondo le usa come la superficie (stesso mathParams).
    // Questo ramo usciva senza ricalcolarle: scelta come sfondo una texture
    // che usa F, lo slider F restava spento fino al gesto successivo
    // (trovato dal test degli scenari).
    refreshConstants();
}

bool MainWindow::textureIncompatible(const LibraryItem &data) const
{
    if (data.isImage) return false;
    if (editingBackground() || editingBorder()) return data.isImplicitMode;
    return data.isImplicitMode != implicitMode();
}

// TEXTURE CHE IL BERSAGLIO NON PUO' USARE: lo stesso gesto per superficie,
// fascia, sfondo e bordo -- un avviso, e la scena resta com'e'. Prima la
// superficie cambiava MODALITA' (chiedendo del lavoro non salvato e mettendo la
// superficie di default) e lo sfondo tornava alla DEFAULT: in entrambi i casi
// compariva qualcosa che l'utente non aveva chiesto, mentre il bordo si
// limitava ad avvisare. Uniformati a quest'ultimo, deciso con l'utente il
// 2026-10-08 (superava la regola del 2026-09-24 sul cambio di modalita').
bool MainWindow::refuseIncompatibleTexture(const LibraryItem &data)
{
    if (!textureIncompatible(data)) return false;
    QString text;
    if (editingBorder())          text = QStringLiteral("Ray marching textures can't go on the border.");
    else if (editingBackground()) text = QStringLiteral("Ray marching textures can't go on the background.");
    else if (implicitMode())      text = QStringLiteral("Parametric textures can't go on a ray marching surface.");
    else                          text = QStringLiteral("Ray marching textures can't go on a parametric surface.");
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Incompatible Texture"));
    box.setText(text);
    box.setInformativeText(QStringLiteral("Pick a texture made for this kind of surface, or an image."));
    box.exec();
    // Il clic ha selezionato la voce rifiutata: il focus torna su quella in vigore.
    syncTextureTreeSelection();
    return true;
}

// TEXTURE PARAMETRICA: immagine o procedurale, sulla superficie o sulla
// fascia selezionata. false = finito qui (fascia), non proseguire.
bool MainWindow::applyLibraryTextureParametric(const LibraryItem &data, const QString &imgSrc,
                                               bool texGoesToMesh)
{

    // --- LOGICA PARAMETRICA ---
    if (data.isImage) {
        // IMMAGINE CON UNA FASCIA SELEZIONATA: va su QUELLA fascia, ferma,
        // e la superficie (la sua immagine, la sua texture) non si tocca.
        // Ogni fascia e' una draw call a se', e il renderer le lega allo
        // slot 1 la SUA immagine: quella nominata dal tag //IMG: nel suo
        // script, come per la superficie (vedi GLWidget::syncPartImages).
        // Prima l'immagine andava comunque alla superficie intera, con un
        // avviso, e sulla fascia la portava solo uno script di "Animated
        // Images" -- lo stesso file per tutte le fasce.
        if (texGoesToMesh) {
            // Inquadratura 2D della voce, come il ramo delle procedurali.
            ui->glWidget->setActiveMeshTexTransform(
                data.zoom, QVector2D(data.panX, data.panY), data.rotation);
            // Lo script della fascia e' il solo tag: l'immagine SOSTITUISCE
            // la procedurale che c'era, come sulla superficie.
            ui->glWidget->setActiveMeshTexture("//IMG:" + imgSrc, true);
            ui->glWidget->setActiveMeshTextureLibName(data.name);
            refreshTextureCheckbox();   // la fascia ha ora la sua texture

            // Un'immagine e' ferma: l'orologio della fascia si spegne, e
            // quello del modulo solo se non serve piu' a nessuno. Non si
            // AVVIA nulla: una texture fermata dall'utente resta ferma.
            ui->glWidget->setActiveMeshTextureAnimating(false);
            if (!hasTimeVariable(allSurfaceTextureCode()))
                ui->glWidget->setSurfaceTextureAnimating(false);
            updateMasterButtonState();

            updateTextureUIState(true, true);
            updateFlatPreviewButton();
            updateScriptButtonText();
            // COSTANTI: la procedurale sostituita poteva usarne una.
            refreshConstants();

            m_blockTextureGen = false;
            ui->glWidget->update();
            return false;
        }
        if (ui->glWidget) {
            // Sola immagine: il motore torna allo shader standard e la
            // copia applicata e' il tag. Prima dell'aggiornamento UI:
            // updateTextureUIState legge questo codice per decidere se
            // accendere i picker Colore (un'immagine "//IMG:" non usa
            // u_col1/u_col2 -> picker spenti).
            commitSurfaceTextureCode("//IMG:" + imgSrc);
            ui->glWidget->loadTextureFromFile(imgSrc);
            m_scene.surfaceTextureState = true;
            applySurfaceTextureToEngine();
            ui->glWidget->rebuildShader();

            // Come per lo sfondo: l'immagine sostituisce la procedurale,
            // quindi lo slot dello script non deve tenerla. Qui il residuo
            // era LATENTE -- non si vedeva subito, si manifestava al primo
            // Run del dock texture (ramo B di onApplyTextureScriptClicked,
            // ~9794), che riapplicava la vecchia procedurale lasciando
            // l'immagine evidenziata in Library.
            // Lo slot prende il TAG, come l'editor qui sotto: e' lo script
            // di questa texture (il Run senza tag toglierebbe l'immagine).
            // Svuotato, col dock Script su un altro modulo l'editor tornava
            // vuoto sulla texture, e il Save dipendeva da copie di riserva
            // per non perdere l'immagine. Trovato dal test degli scenari.
            setScriptText(SlotSurfaceTexture, m_scene.surfaceTextureCode);

            // Il checkbox mostra l'intenzione (gia' accesa qui sopra, PRIMA
            // di updateTextureUIState: onColorTargetChanged() vi si appoggia
            // per decidere se disattivare gli slider colore). In wireframe
            // resta spento e grigio: prima lo si spuntava comunque.
            // (Con una fascia selezionata qui non si arriva piu': l'immagine
            // va alla fascia, nel ramo qui sopra.)
            const bool wasChecked = ui->chkBoxTexture->isChecked();
            refreshTextureCheckbox();
            if (!wasChecked) updateTextureUIState(true);

            ui->glWidget->setFlatZoom(data.zoom);
            ui->glWidget->setFlatPan(data.panX, data.panY);
            ui->glWidget->setFlatRotation(data.rotation);

            updateRenderState();
            ui->glWidget->update();
        }

    } else if (!data.scriptCode.isEmpty()) {
        QString newCode = data.scriptCode;

        // L'IMMAGINE CHE LO SCRIPT TROVA SOTTO. Superficie: quella gia'
        // caricata nel motore resta, e il tag la accompagna.
        // Fascia: resta la SUA immagine (il tag del suo script), e solo se
        // lo script nuovo la campiona -- e' il gesto "prima l'immagine, poi
        // la sua animazione" (famiglia Animated Images). Una procedurale
        // che non campiona nulla la toglie: e' il modo di levare l'immagine
        // da una fascia. Una fascia SENZA immagine propria non prende piu'
        // il tag della superficie: campiona quella della superficie finche'
        // non ne ha una sua, e la segue se cambia.
        if (texGoesToMesh) {
            const QString ownImage =
                GLWidget::imagePathInTextureCode(ui->glWidget->activeMeshTextureCode());
            if (!ownImage.isEmpty() && TextureCode::samplesImage(newCode))
                newCode = "//IMG:" + ownImage + "\n" + newCode;
        } else if (surfaceHasImage()) {
            newCode = "//IMG:" + surfaceImagePath() + "\n" + newCode;
        }

        // AMBITO "MESH": la texture va sulla FASCIA selezionata, non sulla
        // superficie. Questo ramo (applicazione dalla Library) non aveva la
        // diramazione per-mesh che ha invece il Run dello script
        // (onApplyTextureScriptClicked): scriveva sempre gli stati GLOBALI
        // -- m_scene.surfaceTextureCode e i colori (setGlobalTextureColors) -- e
        // quindi applicare una texture a una fascia cancellava quella della
        // superficie. Tornando su "All" si trovava il codice della fascia al
        // posto del proprio (clock fermo, perche' allSurfaceTextureCode()
        // parte da m_scene.surfaceTextureCode) e i suoi colori addosso.
        if (texGoesToMesh) {
            // I colori di QUESTA texture si scrivono direttamente nella
            // parte: i due slot globali appartengono alla texture di
            // SUPERFICIE e non vanno toccati (il blocco che li scrive piu'
            // sopra e' saltato proprio per questo).
            // Prima di setActiveMeshTexture, che li cattura dai globali solo
            // se la parte non ne ha gia' di propri.
            // Una texture SENZA colori propri riparte dai default, come fa il
            // ramo globale: senza azzerarli, la fascia si terrebbe addosso i
            // colori della texture che aveva prima.
            const QColor meshTexC1 = data.hasCustomColors
                    ? QColor(data.color1) : QColor::fromRgbF(0.20f, 0.80f, 0.20f);
            const QColor meshTexC2 = data.hasCustomColors
                    ? QColor(data.color2) : QColor(Qt::black);
            // Gli slider li mostrano da se': surfaceTexColor legge la parte.
            ui->glWidget->setActiveMeshTexColors(meshTexC1, meshTexC2);

            // INQUADRATURA 2D DELLA TEXTURE APPENA APPLICATA (zoom/pan/
            // rotazione salvati nel suo preset). Stesso criterio dei colori
            // qui sopra, e stessa cosa che il ramo GLOBALE fa poco piu' sotto
            // con setFlatZoom/setFlatPan/setFlatRotation: era l'unico dato
            // della texture che il ramo per-mesh non applicava, e la fascia
            // si teneva addosso la manipolazione 2D della texture PRECEDENTE
            // -- ogni texture nuova nasceva gia' zoomata e ruotata.
            ui->glWidget->setActiveMeshTexTransform(
                data.zoom, QVector2D(data.panX, data.panY), data.rotation);

            ui->glWidget->setActiveMeshTexture(newCode, true);
            // ANCORA DEL FOCUS della fascia: la voce da cui la texture viene,
            // come m_scene.textureLibName fa per la texture globale (che
            // questo ramo, uscendo prima, non scrive).
            ui->glWidget->setActiveMeshTextureLibName(data.name);

            refreshTextureCheckbox();   // la fascia ha ora la sua texture

            // Il clock guarda globale + parti accese: una texture animata
            // messa su una fascia deve farlo ripartire, e una statica non
            // deve fermare quella della superficie.
            m_userStoppedTexClock = false;
            // NESSUNA guardia sul master STOP, come la riga gemella del ramo
            // globale (~8002) e come l'orologio della parte qui sotto:
            // applicare una texture e' un comando esplicito sul modulo, e il
            // modulo parte -- anche a scena ferma. Con la guardia il clock
            // globale restava spento e una texture animata applicata mentre
            // il master era fermo nasceva FERMA: serviva un secondo click.
            // Il master torna a "STOP" perche' c'e' davvero qualcosa in moto.
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(allSurfaceTextureCode()));
            // OROLOGIO DELLA PARTE. Va acceso QUI: e' un campo suo, e senza
            // questa riga una texture animata appena applicata alla fascia
            // nascerebbe FERMA (il clock globale non la muove piu': ogni
            // parte ha il proprio). Solo se il codice usa il tempo, cosi'
            // una texture statica non lascia acceso un orologio inutile.
            // NESSUNA guardia sul master STOP. Applicare una texture animata
            // a una fascia e' un comando esplicito su quella fascia: parte
            // subito, come gia' fanno il riclic sulla texture attiva e il Run
            // per-mesh del dock Script (~10200). Con la guardia, a master
            // fermo la texture nasceva FERMA e serviva un SECONDO click --
            // quello che entrava nel ramo riclic -- per vederla partire.
            // La scena resta ferma: si muove solo la fascia, e il master
            // torna a dire STOP perche' qualcosa e' davvero in moto.
            ui->glWidget->setActiveMeshTextureAnimating(hasTimeVariable(newCode));
            // Applicare una texture e' un comando esplicito sulla fascia:
            // riarma il gate, come m_userStoppedTexClock qui sopra per il
            // globale. Senza, dopo uno Stop per-mesh ogni texture applicata
            // dopo sarebbe nata ferma.
            m_userStoppedMeshTexClock = false;
            updateMasterButtonState();

            updateTextureUIState(true, true);
            updateFlatPreviewButton();
            updateScriptButtonText();

            // MESSAGGIO DELLA TEXTURE: il ramo globale lo scrive piu' sotto
            // (~9850), ma questo ramo esce prima e lo saltava -- una texture
            // messa sulle sole fasce non mostrava mai il suo hint, e il record
            // non lo salvava (Clifford Labyrinth 3-Tubes Fibers: nessun
            // "F: density of the grid").
            // A differenza del ramo globale, un hint VUOTO non cancella quello
            // in scena: lo slot e' uno per tutte le fasce, e una texture senza
            // messaggio su una mesh non rende inutile quello di un'altra. A
            // schermo invece, come ovunque, il solo messaggio della texture
            // presa (vuoto = via quello di prima).
            if (!data.hintText.trimmed().isEmpty()) {
                m_currentTextureHintText    = data.hintText.trimmed();
                m_currentTextureHintSeconds = data.hintSeconds;
            }
            showLoadedTextureHint(data.hintText.trimmed(), QString(), QString(), data.hintSeconds);

            // SLIDER DELLE COSTANTI sul codice appena applicato, come fa la
            // coda del ramo globale: la texture della fascia puo' usare una
            // lettera che finora nessuno usava, e senza ricalcolo lo slider
            // restava spento. updateConstantsUIState conta le fasce (vedi
            // meshTextureCodesForConstants); i valori vanno poi spinti nel
            // motore, perche' il ricalcolo scrive a segnali bloccati.
            // Nessun reset al default delle costanti "libere" qui: e' la
            // stessa scena delle altre fasce, e una lettera usata da un'altra
            // mesh cambierebbe sotto gli occhi.
            refreshConstants();

            m_blockTextureGen = false;
            ui->glWidget->update();
            return false;
        }

        // Lo slot (lo script, cioe' l'intenzione) subito; la copia applicata
        // la scrive onApplyTextureScriptClicked, se il codice compila.
        setScriptText(SlotSurfaceTexture, newCode);

        onApplyTextureScriptClicked();

        if (ui->glWidget) {
            ui->glWidget->setFlatZoom(data.zoom);
            ui->glWidget->setFlatPan(data.panX, data.panY);
            ui->glWidget->setFlatRotation(data.rotation);
        }
    }
    return true;
}

// TEXTURE RAY MARCHING: colore e rilievo nel marcher. false = non compila,
// texture spenta, non proseguire.
bool MainWindow::applyLibraryTextureRM(const LibraryItem &data, const QString &imgSrc)
{
    // --- LOGICA RAY MARCHING (IMPLICIT) ---
    // 1. Caricamento fisico dell'immagine nella GPU
    if (data.isImage) {
        if (ui->glWidget) {
            ui->glWidget->loadTextureFromFile(imgSrc);
        }
    } else {
        // Texture procedurale RM (non immagine). I picker Colore in Ray
        // Marching si decidono sul campo lineTexture
        // (activeTextureUsesColorToken), non su un flag.
        // In Ray Marching l'immagine esiste solo insieme al suo triplanar,
        // che la procedurale sostituisce (il campo non avra' il tag
        // //IMG:): via anche dalla GPU. Restava caricata, e uno script
        // che campiona `tex` avrebbe mostrato un'immagine che il Save non
        // scrive. Trovato dal test degli scenari.
        if (ui->glWidget) ui->glWidget->clearTexture();
    }

    // Displacement applicato PRIMA di questa texture: catturato QUI, prima di
    // setDisplacementCode qui sotto, altrimenti currentDisplacementCode()
    // restituirebbe gia' il valore NUOVO e la guardia lo vedrebbe "invariato"
    // (era il bug: il popup non compariva e restava il watchdog tardivo).
    const QString prevDispApplied =
        ui->glWidget ? ui->glWidget->currentDisplacementCode() : QString();

    SE_TEXP("libTex:RM-pre-displacement");
#if SE_TEX_PROBE
    // Cosa dice il PRESET, che nessun altro campo della sonda mostra: se qui
    // il rilievo e' gia' quello vecchio, il guasto e' a monte (parsing,
    // indice della libreria, voce cliccata), non in chi lo applica.
    qDebug().noquote() << QString("TEXP   preset.displacementCode = %1 (len %2) | preset='%3'")
                          .arg(data.displacementCode.left(60).replace('\n', ' '))
                          .arg(data.displacementCode.length())
                          .arg(QFileInfo(data.filePath).fileName());
#endif
    setRmText(&ImplicitTexts::displacement, data.displacementCode);
    if (ui->glWidget) ui->glWidget->setDisplacementCode(data.displacementCode);
    SE_TEXP("libTex:RM-post-displacement");

    // Codice della texture in ARRIVO (scriptCode e' il fallback dei vecchi
    // preset, che non avevano textureCode). Serve al solo ramo PROCEDURALE:
    // il ramo immagine qui sotto lo sovrascrive per intero col triplanar.
    QString rmTexCode = data.textureCode.isEmpty() ? data.scriptCode : data.textureCode;

    // 2. Iniezione automatica del Triplanar Mapping per le immagini pure!
    if (data.isImage) {
        // L'IMMAGINE SOSTITUISCE LA PROCEDURALE, non ci si somma. rmTexCode
        // qui descrive la texture in ARRIVO (per una PNG di libreria e'
        // vuoto), mai quella gia' in lineTexture: senza questa riga il campo
        // conservava lo script RM PRECEDENTE e il tag //IMG: gli finiva
        // semplicemente davanti. Il risultato lo si vedeva nel dock
        // Equations -- immagine in cima, vecchio script morto a seguire --
        // e finiva tale e quale dentro i record salvati, perche' il ramo
        // implicito del serializzatore dumpa lineTexture cosi' com'e'.
        // Stesso trattamento che i rami parametrico (~6875) e sfondo (~6455)
        // riservano gia' ai loro slot script.
        //
        // Il Triplanar Mapping non e' un default "se manca altro": e' LO
        // script che campiona l'immagine, senza il quale non si vedrebbe
        // nulla. Va quindi imposto, non messo in un ramo condizionale che
        // ora non potrebbe mai essere falso.
        rmTexCode = "vec3 blend = abs(n_model);\n"
                    "blend /= max(blend.x + blend.y + blend.z, 0.00001);\n"
                    "float scale = 0.5;\n"
                    "vec3 cX = texture(tex, pModel.yz * scale).rgb;\n"
                    "vec3 cY = texture(tex, pModel.xz * scale).rgb;\n"
                    "vec3 cZ = texture(tex, pModel.xy * scale).rgb;\n"
                    "textureCol = cX * blend.x + cY * blend.y + cZ * blend.z;";
        // Tag in cima: dice al motore quale file caricare. imgSrc e non
        // data.filePath, come nel caricamento GPU poche righe sopra: per una
        // texture-immagine salvata come JSON filePath e' il .json, e il tag
        // avrebbe puntato a quello invece che alla PNG.
        rmTexCode = "//IMG:" + imgSrc + "\n" + rmTexCode;
    }

    setRmText(&ImplicitTexts::texture, rmTexCode);

    if (ui->glWidget) {
        // 1. Preparazione dell'equazione (per darla in pasto al validatore).
        // La sorgente dipende dal sotto-tab implicito ATTIVO, come nel Run
        // (onStartClicked): "3D" ha lineEquation, "Cross Section" ha
        // lineEquationCrossSection. Leggendo sempre lineEquation, applicare
        // una texture RM mentre si e' in Cross Section ricompilava lo shader
        // con la SFERA del sotto-tab 3D -- la superficie 4D spariva e la
        // texture finiva su una sfera che l'utente non aveva chiesto.
        // Il flag va passato anche a validateAndApplyImplicitShader piu'
        // sotto: senza, il suo default false toglie 'p' dallo scope e
        // l'equazione a 4 variabili non compilerebbe nemmeno.
        const bool crossSectionActive = crossSectionTab();
        QString rawEq = (crossSectionActive ? m_scene.rm.crossSection : m_scene.rm.equation).trimmed();
        QString implicitEqF;
        if (rawEq.contains("=")) {
            QStringList parts = rawEq.split("=");
            implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
        } else {
            implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
        }

        // 2. Stato UI: il checkbox segue l'intenzione, accesa qui sotto.

        // ---> LE RIGHE CRITICHE RIPRISTINATE: Sincronizziamo la memoria! <---
        ui->glWidget->setTextureCode(rmTexCode);
        m_scene.surfaceTextureState = true;
        applySurfaceTextureToEngine();
        refreshTextureCheckbox();

        // La checkbox è stata attivata con blockSignals: il suo handler non
        // scatta, quindi sincronizziamo a mano lo stato UI. updateTextureUIState
        // deduce da solo se i picker Colore servono (lineTexture contiene rmTexCode).
        // resetColorTargetToFirst: caricando il preset il focus torna a Colore 1.
        updateTextureUIState(true, true);

        // 3. Validazione reale
        bool success = ui->glWidget->validateAndApplyImplicitShader(implicitEqF, rmTexCode,
                                                                    data.displacementCode,
                                                                    crossSectionActive);

        if (!success) {
            performMasterStop();
            showShaderError("Preset Shader Error", ui->glWidget->getShaderError());
            ui->glWidget->setTextureCode("");
            // Texture non applicata: anche l'intenzione (e il checkbox che
            // la mostra) tornano spenti. Prima restava il checkbox acceso
            // col motore spento, due copie in disaccordo.
            m_scene.surfaceTextureState = false;
            applySurfaceTextureToEngine();
            refreshTextureCheckbox();
            ui->glWidget->rebuildShader();
            // Come le uscite per-mesh del ramo parametrico: il blocco alzato da
            // handleTextureSelection va tolto anche qui, o fino al load/reset
            // seguente slider dei colori e riaccensione del checkbox Texture non
            // rigenerano piu' nulla.
            m_blockTextureGen = false;
            return false; // Esce in sicurezza senza crashare
        }

        SE_TEXP("libTex:RM-pre-rebuild");
        // ---> COMPILAZIONE FINALE RIPRISTINATA <---
        ui->glWidget->rebuildShader();

        ui->glWidget->updateSurfaceData();

        // Mobile: displacement nuovo + trasparenza attiva -> alpha a 1
        // (vedi guardTransparencyOnDisplacementApply).
        guardTransparencyOnDisplacementApply(prevDispApplied);

        // TUTTE le piattaforme: displacement calato su una scena trasparente
        // -> stop del moto + opaco + popup. E' il percorso del caso
        // segnalato: texture di libreria CON RILIEVO sul Cross Section di
        // default, che e' gia' ad alpha 0.75 + Ray Steps 600 senza che
        // nessuna guardia l'abbia visto. Il displacement viaggia insieme alla
        // texture (data.displacementCode, passato all'apply qui sopra) ed e'
        // lui il moltiplicatore: vedi guardTransparencyOnHeavyTextureApply.
        //
        // PRIMA della update() qui sotto, non dopo: e' il punto dell'ordine
        // che conta. La guardia deve poter portare l'alpha a 1 mentre il
        // primo frame trasparente+texturizzato non e' ancora partito -- ed e'
        // proprio quel frame che puo' far collassare la GPU. update() e'
        // asincrona (accoda un paintGL), ma il box modale della guardia fa
        // girare l'event loop: accodare prima significherebbe disegnarlo.
        // L'equazione implicita e' committata: risincronizza lo slider trasparenza
        // (campi a prodotto -> disabilitato + popup). Vedi syncImplicitAlphaSlider.
        //
        // PRIMA della guardia qui sotto, non dopo. Due motivi, entrambi
        // necessari: (1) con newSurface=true questa chiamata RIARMA i flag
        // "gia' chiesto" (compreso quello della guardia), quindi eseguirla
        // dopo cancellava la memoria appena scritta e il commit successivo
        // riapriva il box -- la raffica senza fine; (2) e' lei a portare lo
        // slider allo stato definitivo della nuova superficie, ed e' quello
        // stato che la guardia deve valutare per decidere se chiedere.
        syncImplicitAlphaSlider(true, true);

        guardTransparencyOnHeavyTextureApply(data.displacementCode);

        ui->glWidget->update();
    }
    return true;
}

void MainWindow::restartLibraryTextureClocks()
{
    // ==========================================
    // ANIMAZIONE: selezionare una texture di SUPERFICIE avvia solo il proprio
    // orologio. NON tocca né la geometria (SDF) né lo sfondo né la camera.
    // ==========================================
    const QRegularExpression& timeRegex = kReTimeVar;
    bool isRM = (implicitMode());
    // STATO DELLA TEXTURE DI SUPERFICIE, non del checkbox: in Background quel
    // widget e' il display dello SFONDO, e leggerlo qui faceva spegnere il clock
    // della superficie appena si applicava una texture di sfondo statica (o la
    // si disattivava). Il modulo superficie ha il proprio flag,
    // m_scene.surfaceTextureState, che il ramo Background non tocca.
    const bool editingBg = editingBackground();
    // MODULO TEXTURE DI SUPERFICIE ATTIVO: conta la texture GLOBALE **o** quella
    // di una qualunque fascia. Non basta m_scene.surfaceTextureState (ne' il checkbox,
    // che in Background e' il display dello sfondo): con la texture messa solo
    // sulla fascia 1 quel flag e' false, il ramo veniva saltato e il clock della
    // superficie spento -- l'animazione si fermava andando in Background.
    bool isSurfTexActive = surfaceTextureModuleActive();

    // MODULO TEXTURE: sia il COLORE che il DISPLACEMENT appartengono a questo
    // modulo e nello shader leggono lo STESSO orologio texture (dummyZero.x).
    // Quindi il loro 't' accende/spegne SOLO setSurfaceTextureAnimating. Il clock
    // GEOMETRIA (setSurfaceAnimating) è dell'SDF/equazioni e non va MAI toccato da
    // un caricamento di texture: così sparisce l'interazione incrociata per cui il
    // Run texture faceva partire la "superficie" e i tasti restavano bloccati.
    bool texColorAnim = false;   // 't' nel colore texture  -> orologio TEXTURE
    bool dispAnim     = false;   // 't' nel displacement     -> orologio TEXTURE
    if (isSurfTexActive) {
        if (isRM) {
            texColorAnim = m_scene.rm.texture.contains(timeRegex);
            dispAnim     = m_scene.rm.displacement.contains(timeRegex);
        } else {
            texColorAnim = allSurfaceTextureCode().contains(timeRegex);
        }
    }
    bool texAnim = texColorAnim || dispAnim;
    // NB: NON azzeriamo m_masterStopped: resusciterebbe la GEOMETRIA ferma dopo
    // un master STOP (vedi nota in onTreeItemClicked, sezione texture) e, dopo
    // uno stop del watchdog, disfarebbe il "Keep it stopped" appena scelto.
    // Il clock della TEXTURE invece si accende: vedi la nota sotto, dove viene
    // impostato. Sono due cose diverse -- lo stop globale e l'orologio di un
    // singolo modulo -- e vanno tenute separate.

    if (ui->glWidget) {
        // Caricare una texture e' un avvio esplicito del modulo: riarma un
        // eventuale stop manuale del suo clock.
        // Solo se si sta agendo sulla SUPERFICIE: una texture di sfondo non e'
        // un comando su questo modulo e non deve resuscitarne il clock fermato
        // a mano.
        if (!editingBg) m_userStoppedTexClock = false;
        // Unico orologio del modulo: colore + displacement insieme.
        // NESSUNA guardia sul master STOP. Caricare una texture animata e' un
        // comando ESPLICITO su questo modulo: parte subito anche a scena ferma,
        // esattamente come il ramo per-mesh (~7770), il riclic sulla texture
        // gia' attiva (~11660) e il Run per-mesh del dock Script (~10200). Con
        // la guardia la texture nasceva FERMA e serviva un SECONDO click --
        // quello che entrava nel ramo riclic -- per vederla partire.
        // La scena resta ferma: geometria, rotazioni e path non li riaccende
        // nessuno. Il master torna a dire "STOP" perche' qualcosa e' DAVVERO in
        // moto, quindi il primo click ha di nuovo qualcosa da fermare: lo stato
        // e' coerente, ed e' il caso che la vecchia guardia voleva evitare.
        // Vale anche dopo un "Keep it stopped" del watchdog: sostituire la
        // texture pesante con una animata piu' leggera e' proprio il gesto che
        // l'utente sta chiedendo. Se anche la nuova e' troppo pesante il
        // watchdog riscatta -- rebuildShader() lo riarma (glwidget ~389) e il
        // caricamento di una texture ci passa.
        ui->glWidget->setSurfaceTextureAnimating(texAnim);
        // L'SDF/geometria resta invariato: un caricamento di texture non lo accende
        // né lo spegne (lo governano il dock Equations e il master).
    }

    // Caricare una texture (preset/record) la applica e la renderizza subito: in
    // RM senza animazione non c'è nulla da rieseguire -> il Run texture nasce
    // disabilitato finché lo script non viene modificato. La scrittura dei campi
    // qui sopra è a segnali bloccati, quindi non passa da markRmTextureEdited:
    // allineiamo il flag qui. Con animazione resta Run/Stop.
    if (isRM) {
        // Statica: applicata subito (Run one-shot disabilitato finché non si edita).
        // Animata: NON è un one-shot già consumato -> azzeriamo il flag, altrimenti
        // resta ereditato true da una texture statica caricata prima e, allo Stop del
        // clock (isTexVisuallyMoving=false), il tasto Run nasce disabilitato a torto.
        m_rmTextureApplied = !texAnim;
    }
    updateMasterButtonState();
}

// La coda della scelta di una texture di superficie: tasti, colori, ancora,
// messaggio e costanti.
void MainWindow::finishLibraryTexturePick(const LibraryItem &data)
{
    if (m_currentScriptMode != ScriptModeSound) {
        ui->btnRunCurrentScript->setEnabled(false);
    }

    SE_TEXP("libTex:pre-onColorTargetChanged-finale");
    onColorTargetChanged();
    SE_TEXP("libTex:post-onColorTargetChanged-finale");

    // SUGGERIMENTO IN SOVRIMPRESSIONE della texture, in coda come per superfici
    // (applySurfaceExample) e record (applyMotionExample). Serve soprattutto
    // alle texture che usano le costanti A..F/S: gli slider si sbloccano da
    // soli -- updateConstantsUIState le trova nel codice -- ma niente dice a
    // COSA servano in questa texture, e l'utente si trova uno slider acceso
    // senza sapere cosa muove.
    // Qui e non nei singoli rami: vale per parametrica e ray marching. NON per lo
    // SFONDO, che esce prima e ha il suo messaggio (m_currentBgTextureHintText,
    // scritto in testa al suo ramo): il commento che lo dava per coperto era
    // sbagliato, e l'hint di uno sfondo caricato dalla libreria non compariva.
    // Il messaggio della texture e' un'AGGIUNTA, non un sostituto: si somma a
    // quello della scena in composedHintText(). Non si passa il testo della
    // texture a showSceneHint -- quella scrive m_currentHintText, cioe' il
    // messaggio della SCENA, che PresetSerializer riscrive nel preset di
    // superficie e nel record: il testo della texture ci finirebbe dentro e al
    // primo Save diventerebbe l'hint della superficie.
    // Si aggiorna quindi la sola variabile della texture, e a schermo va il
    // suo solo messaggio (showLoadedTextureHint): non quello della scena, gia'
    // visto al caricamento. Anche a hint VUOTO: la texture precedente puo'
    // averne lasciato uno, che ora va tolto da schermo.
    m_currentTextureHintText    = data.hintText.trimmed();
    m_currentTextureHintSeconds = data.hintSeconds;

    // Nome della texture da cui veniamo: finisce nel record e lo riaggancia a
    // QUESTA voce di libreria anche dopo che il suo codice e' stato modificato
    // (vedi m_scene.textureLibName). Si scrive qui, dove si scrive l'hint:
    // stesso ciclo di vita, stessa provenienza.
    m_scene.textureLibName = data.name.trimmed();
    showLoadedTextureHint(m_currentTextureHintText, QString(), QString(), data.hintSeconds);

    // VALORI DELLE COSTANTI: una texture caricata deve apparire com'e' stata
    // salvata, non com'e' rimasta quella precedente. I preset di texture NON
    // salvano i valori delle costanti (le chiavi sono code/color/zoom/pan/
    // rotation/hint), quindi non c'e' un valore "del preset" da ripristinare:
    // si torna al DEFAULT (A..F = 1, S = 0).
    // Senza, caricando Psychedelic Vortex, muovendo A e B, e poi caricando
    // Reactive Holography -- che usa le stesse due lettere -- la texture nuova
    // partiva gia' alterata dai valori della vecchia: updateConstantsUIState
    // riporta a 1 le sole costanti CADUTE IN DISUSO, e quelle usate da entrambe
    // non lo sono.
    // SOLO le costanti che la SUPERFICIE non usa: quelle condivise appartengono
    // anche a lei, e resettarle le cambierebbe la forma sotto gli occhi --
    // effetto peggiore del bug che si sta correggendo. Stesso spirito del reset
    // di zoom/pan/rotazione sul riclic: si azzera cio' che e' della texture.
    {
        const QSet<QString> freeConsts = constantsNotUsedBySurface();
        for (ConstField f : constantFields()) {
            const QString letter = constantName(f);
            if (!freeConsts.contains(letter)) continue;
            // S in Ray Marching e' lo Step Relax del ray marcher, non una
            // costante libera: resettarlo congela i raggi (vedi la guardia in
            // updateConstantsUIState). Si tocca solo in parametrica.
            if (f == &ConstantTexts::s) {
                if (!implicitMode()) setConstText(f, QStringLiteral("0"));
            } else {
                setConstText(f, QStringLiteral("1"));
            }
        }
        refreshConstantSliders();
    }

    // SLIDER DELLE COSTANTI: vanno ricalcolati sul codice APPENA caricato.
    // In Ray Marching updateConstantsUIState legge lineTexture/lineVariations,
    // che qui sopra sono stati riempiti con blockSignals(true) -- necessario per
    // non sporcare la scena e non far scattare i watchdog -- e quindi SENZA
    // emettere il textChanged a cui e' agganciato il ricalcolo.
    // Il risultato era che gli slider restavano quelli della texture PRECEDENTE:
    // caricando su un record con rilievo su E una texture che usa F, restava
    // abilitato E (che non compare piu' in nessun codice, quindi muoverlo non
    // faceva nulla) mentre F, la costante realmente usata, era bloccato.
    // L'hint diceva gia' la cosa giusta -- viene dal file della texture nuova --
    // e questo rendeva l'incoerenza ancora piu' evidente.
    // updateConstantsUIState riporta a 1 le costanti non piu' usate, ma lo fa
    // con blockSignals: il valore nuovo non arriverebbe alla GPU. refreshConstants
    // spinge quindi le costanti come le legge ora la UI, altrimenti la vecchia
    // (es. E, lasciata a 4.17 dal record) resterebbe in vigore nell'UBO pur
    // essendo sparita dallo shader.
    refreshConstants();

    m_textureModified = false;

    SE_TEXP("libTex:USCITA");
}

void MainWindow::onRunRaymarchTextureClicked()
{
    if (!ui->glWidget) return;

    const QRegularExpression& timeRegex = kReTimeVar;
    bool texColorHasTime = m_scene.rm.texture.contains(timeRegex);
    bool dispHasTime     = m_scene.rm.displacement.contains(timeRegex);

    // Il MODULO TEXTURE possiede UN solo orologio: colore e displacement leggono
    // entrambi dummyZero.x nello shader. Quindi Run/Stop texture agisce SOLO su
    // setSurfaceTextureAnimating e NON tocca mai la geometria/SDF (setSurfaceAnimating):
    // è proprio l'aver toccato il clock geometria che faceva "partire la superficie"
    // e bloccava i tasti. Regola: azione su un modulo = solo quel modulo.
    if (ui->btnTextureCode->text() == "Stop") {
        // Stop esplicito del clock texture: il flag lo tiene fermo anche
        // attraverso i ricalcoli globali (vedi applyAnimationState).
        m_userStoppedTexClock = true;
        ui->glWidget->setSurfaceTextureAnimating(false);
        updateMasterButtonState();
        return;
    }

    // RUN del modulo TEXTURE: applica le equazioni (un commit di servizio non
    // avvia la geometria) e avvia il proprio unico orologio.
    runScene(RunOrigin::ServiceCommit);

    // La texture di SUPERFICIE (il checkbox, col bersaglio su Background,
    // mostra lo sfondo: a sfondo spento il Run lasciava ferma la texture).
    bool active = surfaceTextureShown();
    bool texAnim = active && (texColorHasTime || dispHasTime);
    // NB: NON azzeriamo m_masterStopped. setSurfaceTextureAnimating avvia il clock
    // texture da solo (non gated dallo stop globale), quindi la texture si anima
    // comunque; azzerare il flag sbloccherebbe la GEOMETRIA e dopo un master STOP
    // un toggle della texture potrebbe far ripartire la superficie (t in defU/V/W).

    // Run esplicito del modulo texture: riarma un eventuale stop manuale.
    m_userStoppedTexClock = false;
    ui->glWidget->setSurfaceTextureAnimating(texAnim);

    // Run "one-shot": se gli script texture NON sono animati, la modifica è
    // applicata e il tasto si disabilita finché lineTexture/lineVariations non
    // cambiano. Con animazione resta Run/Stop (gestito da updateMasterButtonState).
    if (!texColorHasTime && !dispHasTime) {
        m_rmTextureApplied = true;
    }

    updateMasterButtonState();
}

// ACCENSIONE DELLA TEXTURE DI SUPERFICIE NEL MOTORE, derivata dall'intenzione.
// m_scene.surfaceTextureState e' l'unica copia che dice se l'utente VUOLE la texture
// (cambia col checkbox della superficie, coi load e coi reset); il motore ne e'
// una conseguenza: accesa se l'utente la vuole e la superficie non e' in
// wireframe. Il wireframe di UNA fascia (ambito Mesh su una multi-mesh) non la
// spegne: e' una proprieta' della parte, e il fragment non texturizza gia' da
// se' una parte in wireframe (surface.frag, u_renderMode == 2 per-parte).
// Prima 14 punti scrivevano setGlobalTextureEnabled, ciascuno con la sua regola
// (alcuni ignoravano il wireframe, lasciando a updateRenderState il compito di
// rimediare): ora la regola e' qui, ed e' quella di updateRenderState.
// Tappa 3 dello "stato unico della scena"; il test degli scenari la verifica.
void MainWindow::applySurfaceTextureToEngine()
{
    if (!ui->glWidget) return;
    const bool editingOneMesh = ui->glWidget->activeMeshPart() >= 0
                                && ui->glWidget->meshPartCount() > 1;
    const bool surfaceWireframe = (m_scene.renderMode == 2) && !editingOneMesh;
    ui->glWidget->setGlobalTextureEnabled(m_scene.surfaceTextureState && !surfaceWireframe);
}

// IL CHECKBOX "Texture" COME VISTA dell'intenzione m_scene.surfaceTextureState,
// quando il dock edita la superficie in ambito All: spuntato = intenzione, e
// in WIREFRAME spento e disabilitato (la texture non si disegna). Con lo sfondo
// o una fascia in editing il checkbox mostra quelli (syncAppearanceControls-
// ToActiveMesh per la fascia): qui non si tocca.
// E' la regola che il test degli scenari verifica dopo ogni gesto; chi accende
// o spegne la texture scrive l'intenzione e chiama questa, invece di spuntare
// il checkbox a mano (scegliere un'immagine in wireframe lo spuntava).
bool MainWindow::surfaceTextureShown() const
{
    if (textureTargetInWireframe()) return false;

    // Ambito Mesh: lo stato EFFICACE della fascia (il proprio se dichiarato,
    // altrimenti cio' che eredita). In MULTI-mesh una parte mai configurata NON
    // eredita la texture globale (resta in tinta unita): il display dice la
    // stessa cosa del render. Era la regola di
    // syncAppearanceControlsToActiveMesh; i cambi di modalita' la scavalcavano
    // scrivendo l'intenzione GLOBALE, e una fascia senza texture risultava
    // "accesa" (trovato dal test degli scenari).
    const int part = ui->glWidget ? ui->glWidget->activeMeshPart() : -1;
    if (part >= 0 && !implicitMode() && ui->glWidget->getEngine()) {
        const auto &parts = ui->glWidget->getEngine()->getMeshParts();
        if (part < (int)parts.size()) {
            const bool multi = ui->glWidget->meshPartCount() > 1;
            return multi ? parts[part].effectiveTextureEnabledMulti()
                         : parts[part].effectiveTextureEnabled(ui->glWidget->isTextureEnabled());
        }
    }
    return m_scene.surfaceTextureState;
}

bool MainWindow::targetTextureOn() const
{
    if (editingBorder())
        return ui->glWidget && ui->glWidget->borderTextureEnabled()
               && !ui->glWidget->borderTextureCode().trimmed().isEmpty();
    if (editingBackground())
        return ui->glWidget && ui->glWidget->isBackgroundTextureEnabled();
    return surfaceTextureShown();
}

bool MainWindow::surfaceTextureModuleActive() const
{
    return surfaceTextureShown() || anyMeshTextureActive();
}

QString MainWindow::surfaceTextureModuleCode() const
{
    if (implicitMode())
        return m_scene.rm.texture + QLatin1Char('\n') + m_scene.rm.displacement;
    return allSurfaceTextureCode();
}

void MainWindow::refreshTextureCheckbox()
{
    const bool onBackground = editingBackground();
    const QSignalBlocker blocker(ui->chkBoxTexture);
    ui->chkBoxTexture->setText(onBackground ? QStringLiteral("Background Texture")
                               : editingBorder() ? QStringLiteral("Border Texture")
                                                 : QStringLiteral("Texture"));
    // Lo sfondo esiste anche senza superficie e in wireframe; la texture di
    // superficie no. Quella del bordo segue la modalita' del bordo.
    const bool wire = editingBorder()
                      ? (ui->glWidget && ui->glWidget->borderRenderMode() == 2)
                      : textureTargetInWireframe();
    ui->chkBoxTexture->setEnabled(onBackground || (!wire && !isSceneEmpty()));
    ui->chkBoxTexture->setChecked(targetTextureOn());
}

bool MainWindow::textureTargetInWireframe() const
{
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0
        && ui->glWidget->meshPartCount() > 1 && !implicitMode())
        return ui->glWidget->activeMeshEffectiveRenderMode() == 2;
    return m_scene.renderMode == 2;
}




// Ritorna true se la texture attualmente attiva (superficie/ray-marching o
// background, a seconda della modalità) referenzia davvero u_col1/u_col2.
// Le texture che non li usano (immagini, triplanar, pattern a colori fissi)
// devono lasciare i picker Colore spenti per non illudere l'utente.
bool MainWindow::activeTextureUsesColors() const
{
    // OR delle due granulari: la texture usa ALMENO uno dei due colori.
    return activeTextureUsesColorToken("u_col1") || activeTextureUsesColorToken("u_col2");
}

QColor MainWindow::surfaceTexColor(int slot) const
{
    GLWidget *g = ui->glWidget;
    if (!g) return slot == 2 ? QColor(Qt::black) : QColor(Qt::white);
    // Stessa regola del render (GLWidget, blocco UBO della parte): una fascia
    // disegna coi colori propri se li ha, altrimenti coi due slot globali.
    const int part = g->activeMeshPart();
    if (part >= 0 && g->getEngine()) {
        const auto &parts = g->getEngine()->getMeshParts();
        if (part < (int)parts.size() && parts[part].hasCustomTexColors()) {
            const MeshPart &p = parts[part];
            return slot == 2 ? QColor::fromRgbF(p.texCol2R, p.texCol2G, p.texCol2B)
                             : QColor::fromRgbF(p.texCol1R, p.texCol1G, p.texCol1B);
        }
    }
    return slot == 2 ? g->globalTexColor2() : g->globalTexColor1();
}

QString MainWindow::surfaceImagePath() const
{
    return ui->glWidget ? ui->glWidget->surfaceImagePath() : QString();
}




bool MainWindow::commitSurfaceTextureCode(const QString &code)
{
    // La logica si cerca fuori dalla riga del tag: il percorso di un'immagine
    // non e' codice, qualunque parola contenga.
    static const QRegularExpression imgTagRe(R"(^\s*//IMG:.*$\n?)",
                                             QRegularExpression::MultilineOption);
    QString logic = code;
    logic.remove(imgTagRe);
    const QString engineCode = TextureCode::hasLogic(logic) ? code : QString();
    if (ui->glWidget && !ui->glWidget->validateAndApplyParametricShader(engineCode))
        return false;
    m_scene.surfaceTextureCode = code;
    return true;
}

QString MainWindow::backgroundImagePath() const
{
    return ui->glWidget ? ui->glWidget->backgroundUserImagePath() : QString();
}

bool MainWindow::activeTextureUsesColorToken(const QString &token) const
{
    // BORDO: il suo codice; senza immagine da campionare gli script che
    // leggono iChannel mostrano la scacchiera, fatta dei due colori.
    if (editingBorder()) {
        const QString code = ui->glWidget ? ui->glWidget->borderTextureCode() : QString();
        return code.contains(token)
            || (code.contains("iChannel") && !ui->glWidget->borderSamplesImage());
    }
    if (editingBackground()) {
        return m_scene.bgTextureCode.contains(token);
    }

    // In Ray Marching la texture vive SEMPRE nel campo dedicato (lineTexture) e i
    // "codice custom" e immagine sono un concetto del solo modo parametrico:
    // al load di un record RM restano entrambi false (il texCode parametrico viene
    // svuotato, la texture va in lineTexture), quindi la scorciatoia "scacchiera"
    // più sotto accendeva i picker a torto su texture RM senza u_col1/u_col2. In RM
    // la verità è solo nel codice del campo texture.
    if (implicitMode()) {
        return m_scene.rm.texture.contains(token);
    }

    // AMBITO "MESH": se la parte selezionata ha una texture PROPRIA, la verità
    // è nel SUO codice, non in m_scene.surfaceTextureCode (che è la texture globale
    // della superficie). Leggere il globale qui era il motivo per cui i picker
    // Color1/Color2 venivano abilitati/spenti in base allo script sbagliato:
    // selezionando una mesh senza texture propria restavano quelli della mesh
    // precedente, e da lì i colori della texture finivano riscritti sulla parte
    // sbagliata al primo tocco del checkbox.
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0) {
        const QString partCode = ui->glWidget->activeMeshTextureCode();
        // L'immagine che la fascia campiona e' la SUA, o in mancanza quella
        // della superficie: senza nessuna delle due c'e' la scacchiera di
        // ripiego, fatta dei due colori.
        if (!partCode.trimmed().isEmpty())
            return partCode.contains(token)
                || (partCode.contains("iChannel") && !ui->glWidget->activeMeshSamplesImage());
        // Nessuna texture propria: la parte EREDITA la globale, quindi si
        // prosegue col ragionamento globale qui sotto.
    }

    // Parametrico: la scacchiera procedurale di default (applyDefaultCheckerShader)
    // non è uno script utente ma usa comunque ENTRAMBI u_col1/u_col2 -> entrambi
    // i picker servono, quindi true per qualunque token.
    if (!surfaceTextureIsCustom() && !surfaceHasImage()) {
        return true;
    }

    return m_scene.surfaceTextureCode.contains(token) || samplesImageWithoutOne(m_scene.surfaceTextureCode);
}

// Uno script che campiona iChannel0 (famiglia "Animated Images": rimostra
// l'immagine caricata deformandola) NON nomina u_col1/u_col2, quindi i picker
// colore restano spenti -- ed e' giusto, una fotografia non ha due tinte da
// editare. Ma SENZA immagine caricata quegli script mostrano la scacchiera
// procedurale di fallback (vedi _st_sampleTex in createFragmentShaderSource),
// che su u_col1/u_col2 e' costruita: in quel caso i picker servono davvero e
// muoverli cambia la scacchiera. Appena si carica un'immagine il fallback
// sparisce e i picker tornano spenti da soli.
bool MainWindow::samplesImageWithoutOne(const QString &code) const
{
    if (!code.contains("iChannel")) return false;
    // L'immagine caricata e' quella nel motore.
    return !surfaceHasImage();
}

bool MainWindow::hasSavableTexture() const
{
    // BORDO: il suo codice (o la sua immagine), come lo mostra il dock Script o
    // com'e' nel motore. Prima della regola sull'immagine della SUPERFICIE, che
    // qui non c'entra.
    if (editingBorder()) {
        const QString code = (m_currentScriptMode == ScriptModeTexture)
                             ? scriptText(shownScriptSlot())
                             : (ui->glWidget ? ui->glWidget->borderTextureCode() : QString());
        return !code.trimmed().isEmpty();
    }

    const bool isBg = editingBackground();
    const bool isImplicit = (implicitMode());

    // Un'immagine caricata è salvabile anche con i box di codice vuoti: quella
    // del bersaglio (prima si guardava la superficie anche salvando lo sfondo).
    if (isBg ? !backgroundImagePath().isEmpty() : surfaceHasImage())
        return true;

    // Ray Marching (non sfondo): contenuto = colore (lineTexture) + displacement
    // (lineVariations), gli stessi campi salvati da saveTexture().
    if (isImplicit && !isBg) {
        return !m_scene.rm.texture.trimmed().isEmpty()
               || !m_scene.rm.displacement.trimmed().isEmpty();
    }

    // Parametrico/sfondo: in modalità script texture la verità è lo script
    // che il dock mostra, altrimenti il codice in memoria del target attivo.
    if (m_currentScriptMode == ScriptModeTexture)
        return !scriptText(shownScriptSlot()).trimmed().isEmpty();

    const QString &code = isBg ? m_scene.bgTextureCode : m_scene.surfaceTextureCode;
    return !code.trimmed().isEmpty();
}

void MainWindow::updateTextureUIState(bool isTextureOn, bool resetColorTargetToFirst)
{
    // 2. I controlli Colore Texture sono abilitati SOLO se la texture è ACCESA
    //    E lo script referenzia davvero u_col1/u_col2: lo deduciamo dalla
    //    texture attiva, così ogni chiamante è automaticamente corretto.
    //    Eccezione: in WIREFRAME sulla SUPERFICIE la texture è nascosta e gli slider
    //    editano il colore uniforme delle linee, quindi Color1/2 non hanno senso e
    //    vanno spenti (lo sfondo invece può mostrare la sua texture anche in wireframe).
    bool surfaceWireframe = shownRenderMode() == 2 && !editingBackground();
    bool baseActive = isTextureOn && !surfaceWireframe;
    // Ogni picker abilitato solo se la texture referenzia il SUO colore: una texture
    // che usa solo u_col1 (es. "Xor") lascia spento il picker di col2, che sarebbe
    // inerte e fuorviante. colorsActive (almeno un colore) governa il "pallino".
    bool col1Active = baseActive && activeTextureUsesColorToken("u_col1");
    bool col2Active = baseActive && activeTextureUsesColorToken("u_col2");
    bool colorsActive = col1Active || col2Active;
    ui->radioTexColor1->setEnabled(col1Active);
    ui->radioTexColor2->setEnabled(col2Active);

    // 1. "Surface" resta SEMPRE abilitato: è l'indicatore del target (il pallino deve
    //    restare visibile sulla superficie). Anche con una texture SENZA colori
    //    (immagine), dove non c'è alcun colore editabile, NON lo disabilitiamo: sarebbe
    //    ambiguo (pallino spento, tripla vuota). A comunicare che non si può editare ci
    //    pensano gli slider, disattivati da onColorTargetChanged quando la texture copre
    //    la superficie senza colori editabili.
    ui->radioSurface->setEnabled(true);

    // 3. GESTIONE DEL "PALLINO" (due gruppi INDIPENDENTI)
    //    - Coppia Surface/Background: dove operi. La lasciamo dov'è, salvo
    //      garantire che un target esista (default Surface).
    //    - Coppia Color1/Color2: quale tinta texture editi. Ha senso solo con una
    //      texture colorata attiva; in quel caso almeno un Color dev'essere acceso
    //      (default Color 1); altrimenti vanno entrambi spenti (sono pure disabilitati).
    if (colorsActive) {
        // Texture colorata: assicuriamo un Color acceso. Sempre su nuova texture
        // (resetColorTargetToFirst) o se nessuno dei due Color era selezionato.
        // Accendiamo il picker ABILITATO: col1 se la texture lo usa, altrimenti col2
        // (una texture che usa solo u_col2 non deve selezionare un col1 disabilitato).
        if (resetColorTargetToFirst ||
            (!ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked())) {
            QRadioButton *target = col1Active ? ui->radioTexColor1 : ui->radioTexColor2;
            bool ob1 = target->blockSignals(true);
            target->setChecked(true);
            target->blockSignals(ob1);
        }
    } else {
        // Nessuna texture colorata: spegniamo i color slot (gruppo esclusivo: serve
        // uncheckInExclusiveGroup, un setChecked(false) diretto sull'unico acceso è no-op).
        uncheckInExclusiveGroup(ui->radioTexColor1);
        uncheckInExclusiveGroup(ui->radioTexColor2);
    }

    onColorTargetChanged();

    updateFlatPreviewButton();
}

void MainWindow::updateFlatPreviewButton() {
    bool isTextureScriptMode = (m_currentScriptMode == ScriptModeTexture);
    bool isParametric = (!implicitMode());
    bool bgMode = editingBackground();

    // 1. Gestione del Testo
    //
    // IL TASTO E' UN TOGGLE E IL TESTO DESCRIVE DOVE SI VA, NON DOVE SI E':
    // in vista 3D dice "2D ..." (dove porta), in vista 2D dice "3D Shape" (per
    // tornare). Quindi con la vista 2D ATTIVA il testo non si tocca: e' il
    // gestore toggled a scriverlo (~3621) ed e' l'unico che sa dove si sta
    // andando. Senza questa guardia ogni chiamata a questa funzione mentre si
    // e' in 2D riportava l'etichetta a "2D ...", pur restando in vista 2D:
    // succedeva fermando l'animazione della texture, perche' quel ramo chiama
    // updateScriptButtonText (~7496) che finisce qui (~12286). Il tasto diceva
    // "2D" mentre la texture era gia' in 2D, e per uscirne serviva premerlo due
    // volte. Stesso giro per il checkbox texture e per i cambi di ambito.
    if (!ui->btnFlatPreview->isChecked()) {
        if (bgMode) {
            ui->btnFlatPreview->setText("2D Background");
        } else if (editingBorder()) {
            ui->btnFlatPreview->setText("2D Border");
        } else {
            ui->btnFlatPreview->setText("2D Texture");
        }
    }

    // 2. Controllo attivazione fisica (Checkbox / Motore)
    bool isTexActive = bgMode ? targetTextureOn() : m_scene.surfaceTextureState;

    // AMBITO "MESH": la texture puo' essere accesa sulla SOLA fascia
    // selezionata, e in quel caso m_scene.surfaceTextureState (che e' il flag GLOBALE
    // della superficie) resta spento -- il ramo per-mesh del checkbox e del Run
    // esce prima di scriverlo. Il tasto 2D risultava quindi disabilitato con una
    // texture visibilissima a schermo: e' il "texture attiva ma 2D deselezionato".
    // Qui conta la texture del destinatario corrente, cioe' quella della fascia:
    // propria E accesa (una fascia con la texture spenta non ha nulla da
    // mostrare in 2D). E' esattamente activeMeshTextureActive(), che legge lo
    // stato REALE della parte; prima si ricostruiva la stessa condizione a mano
    // combinando activeMeshHasOwnTexture() col checkbox, che di quello stato e'
    // solo il display e puo' divergerne.
    if (!bgMode && ui->glWidget && ui->glWidget->activeMeshPart() >= 0) {
        isTexActive = ui->glWidget->activeMeshTextureActive();
    }
    // BORDO: la sua texture, se si vede.
    if (editingBorder()) isTexActive = ui->glWidget && ui->glWidget->borderTextureShown();

    // 3. Regola di abilitazione:
    // Deve essere aperta la tab Texture (isTextureScriptMode)
    // La texture deve essere attiva (isTexActive)
    // Se è Background va bene tutto, se è Superficie DEVE essere Parametrica (isParametric)
    bool canBeEnabled = isTextureScriptMode && isTexActive && (bgMode || isParametric);

    ui->btnFlatPreview->setEnabled(canBeEnabled);

    // 4. Sicurezza: Se disabilitiamo il bottone, togliamo la spunta per evitare bug visivi
    if (!canBeEnabled && ui->btnFlatPreview->isChecked()) {
        ui->btnFlatPreview->setChecked(false);
    }
}

void MainWindow::generateTexture()
{
    // 1. Crea un'immagine 512x512
    int size = 512;
    QImage img(size, size, QImage::Format_RGBA8888);
    QPainter p(&img);

    // 2. Sfondo (Colore 1)
    p.fillRect(0, 0, size, size, surfaceTexColor(1));

    // 3. Scacchi (Colore 2)
    p.setBrush(surfaceTexColor(2));
    p.setPen(Qt::NoPen);
    int step = 64; // Dimensione quadretti

    for (int y=0; y<size; y+=step) {
        for (int x=0; x<size; x+=step) {
            // Disegna a scacchiera
            if (((x/step) + (y/step)) % 2 == 1) {
                p.drawRect(x, y, step, step);
            }
        }
    }
    p.end();

    // 4. Invia al GLWidget
    if (ui->glWidget) {
        ui->glWidget->loadTextureFromImage(img);
    }
}

// Scacchiera default PROCEDURALE (stessa resa del preset "Checkboard"), come
// gia' fatto per il default Ray Marching: l'immagine di generateTexture()
// campionata col filtro bilineare mostrava una riga di colore misto sulla
// chiusura UV delle superfici chiuse (i due bordi opposti della bitmap si
// mescolano), che il calcolo per-fragment non ha. L'immagine resta caricata
// nel sampler solo come feed `tex` per gli script che lo campionano.
// u_col1/u_col2 arrivano dall'UBO: i picker agiscono live, senza rigenerare.
// UNICO PUNTO: la usano sia la texture globale (applyDefaultCheckerShader) sia
// quella per-mesh (checkbox con una parte selezionata). Due copie sarebbero
// destinate a divergere.
// Tutto il codice texture di SUPERFICIE che concorre all'animazione: quello
// globale piu' quello delle singole mesh. Con le texture per-mesh il solo
// m_scene.surfaceTextureCode non basta piu': una parte con 't' nel proprio script
// anima davvero (il suo codice e' compilato nel fragment), ma i punti che
// decidono setSurfaceTextureAnimating guardavano solo il globale e trovavano
// una stringa senza 't' -> clock spento e texture ferma.
// UNICO PUNTO: sono cinque i posti che valutano l'animazione della texture;
// correggerli uno per uno li avrebbe fatti divergere.
// C'e' almeno una FASCIA con una texture propria e accesa?
// Il modulo "texture di superficie" e' attivo anche in questo caso, e nessuno
// dei flag globali lo dice: m_scene.surfaceTextureState riguarda la sola texture
// della superficie, e chkBoxTexture in ambito Mesh e' il display della parte
// (in Background, dello sfondo). Punto unico: la stessa domanda serve al clock
// e a chi decide se il modulo va considerato in moto.
bool MainWindow::anyMeshTextureActive() const
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return false;
    for (const MeshPart &mp : ui->glWidget->getEngine()->getMeshParts()) {
        if (mp.hasCustomTexture && mp.textureEnabled && !mp.textureCode.isEmpty())
            return true;
    }
    return false;
}

// C'e' almeno una FASCIA la cui texture, oltre a essere accesa, usa il tempo?
// Distinta da allSurfaceTextureCode(), che in ambito "All" esclude apposta le
// texture per-mesh (li' lo shader non le compila): al master serve invece
// sapere se una parte ha qualcosa in movimento, qualunque sia l'ambito.
bool MainWindow::anyMeshTextureCodeAnimated() const
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return false;
    auto hasTime = [this](const QString &c){ return hasTimeVariable(c); };
    for (const MeshPart &mp : ui->glWidget->getEngine()->getMeshParts()) {
        if (meshTextureAnimated(mp, hasTime)) return true;
    }
    return false;
}

// Riaccende l'orologio di OGNI fascia che ha una texture propria, accesa e
// ANIMATA; spegne quello delle altre. E' il gesto simmetrico dello Stop in
// ambito "All" (setAllMeshTexturesAnimating(false)): senza, le texture per-mesh
// restavano ferme per sempre, perche' il Run globale riaccendeva solo il clock
// della superficie.
// Si decide parte per parte invece di scrivere "true" a tutte: una fascia con
// texture statica non deve restare con l'orologio acceso a vuoto.
void MainWindow::restartAnimatedMeshTextures()
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return;
    auto hasTime = [this](const QString &c){ return hasTimeVariable(c); };
    bool anyOn = false;
    for (MeshPart &mp : ui->glWidget->getEngine()->mutableMeshParts()) {
        mp.texAnimating = meshTextureAnimated(mp, hasTime);
        if (mp.texAnimating) anyOn = true;
    }
    ui->glWidget->getEngine()->syncPartAppearance();
    // Il timer dei tick lo riavviano da se' solo i tre setter GLOBALI, e solo
    // quando accendono qualcosa: con la texture animata nelle sole FASCE (la
    // superficie statica, o senza texture propria) setSurfaceTextureAnimating
    // riceve false e il timer resta fermo. I flag qui sopra dicevano allora "in
    // moto" senza che nulla si muovesse: al master Start ripartiva tutto tranne
    // le texture.
    if (anyOn) ui->glWidget->ensureTextureClockRunning();
    ui->glWidget->update();
}

QString MainWindow::allSurfaceTextureCode() const
{
    QString all = m_scene.surfaceTextureCode;
    // AMBITO "ALL": le texture per-mesh sono SOSPESE (lo shader non le compila
    // nemmeno), quindi non devono far girare il clock: conta solo la globale.
    if (ui->glWidget && ui->glWidget->getEngine()
        && !ui->glWidget->meshAppearanceUniform()) {
        for (const MeshPart &mp : ui->glWidget->getEngine()->getMeshParts()) {
            // Solo le parti che la texture ce l'hanno DAVVERO accesa: una parte
            // che la tiene spenta non deve far girare il clock.
            if (mp.hasCustomTexture && mp.textureEnabled && !mp.textureCode.isEmpty())
                all += "\n" + mp.textureCode;
        }
    }
    return all;
}


void MainWindow::applyDefaultCheckerShader()
{
    if (ui->glWidget) ui->glWidget->loadCustomShader(TextureCode::defaultMeshCode());
}

// I quattro radio della forma dello sfondo, NELL'ORDINE di GLWidget::BgSkyMode:
// l'indice nella lista E' la modalita'. Unica tabella per clic, load, reset e
// tooltip -- aggiungere un radio altrove farebbe divergere le quattro sedi.
QList<QRadioButton*> MainWindow::bgSkyRadios() const
{
    return { ui->radioBgFixed, ui->radioBgSphere, ui->radioBgCylinder, ui->radioBgCube };
}

// Forma dello sfondo: DISPLAY dei radio e stato del motore insieme, cosi' non
// possono divergere. A segnali bloccati su TUTTI i radio, per la stessa ragione
// di refreshRenderRadios: accendendone uno quello acceso prima si spegne ed
// emette toggled(false). Il motore si scrive a parte, esplicitamente: con i
// segnali bloccati il gestore del clic non gira.
void MainWindow::applyBackgroundSkyMode(int mode)
{
    const QList<QRadioButton*> radios = bgSkyRadios();
    if (mode < 0 || mode >= radios.size()) mode = GLWidget::BgFixed;

    QList<bool> old;
    for (QRadioButton *r : radios) old << (r ? r->blockSignals(true) : false);
    if (radios[mode]) radios[mode]->setChecked(true);
    for (int i = 0; i < radios.size(); ++i)
        if (radios[i]) radios[i]->blockSignals(old[i]);

    if (ui->glWidget) ui->glWidget->setBackgroundSkyMode(mode);
}

// Forma dello sfondo (panelBgLock) attiva SOLO col bersaglio Background e la
// texture di sfondo accesa (come il resto dei controlli che agiscono sullo
// sfondo; la forma avvolge la texture, una tinta unita non ne ha bisogno). Si spegne il contenitore dei
// quattro radio e non il riquadro: in testa al riquadro c'e' il radio
// Background stesso, che deve restare cliccabile.
// Spento non vuol dire azzerato: la scelta resta in vigore sullo sfondo a
// schermo, semplicemente non la si cambia editando la superficie.
//
// PERCHE' DUE CHIAMANTI (updateRenderState e onColorTargetChanged). Il radio
// Surface/Background viene riportato su Surface anche A SEGNALI BLOCCATI -- dal
// reset di scena, dal load di un preset, da selectSurfaceColorTarget -- e li' il
// toggled non scatta: agganciarsi al solo toggled lasciava il gruppo acceso con
// Surface selezionato. Ognuno di quei percorsi passa pero' da una delle due
// funzioni, che sono le sedi del gating del Renderer.
//
// I TOOLTIP stanno qui e non nel .ui: da spenti devono dire PERCHE' (regola del
// progetto per i controlli inerti), da accesi cosa fanno. Una sede sola, o i
// testi divergono. Qt mostra il tooltip anche sui widget disabilitati.
void MainWindow::updateBackgroundControlsGate()
{
    const QList<QRadioButton*> radios = bgSkyRadios();
    if (!ui->panelBgLock || radios.contains(nullptr)) return;

    // La forma riguarda la TEXTURE di sfondo (come la si avvolge attorno alla
    // scena): con lo sfondo a tinta unita non c'e' niente da avvolgere, quindi
    // i radio si accendono solo col bersaglio Background E la texture accesa.
    const bool onBackground = editingBackground();
    const bool bgTexture = ui->glWidget && ui->glWidget->isBackgroundTextureEnabled();
    const bool usable = onBackground && bgTexture;
    ui->panelBgLock->setEnabled(usable);

    // Stesso ordine di bgSkyRadios / GLWidget::BgSkyMode.
    const QStringList what = {
        tr("The background stays still on the screen."),
        tr("The background is a sky around the scene."),
        tr("The background is a cylinder around the scene."),
        tr("The background is a cube around the scene.")
    };
    const QString why = !onBackground ? tr("Select Background to change it.")
                                      : tr("Turn on the Background Texture to choose its shape.");
    for (int i = 0; i < radios.size(); ++i)
        radios[i]->setToolTip(usable ? what.value(i) : why);
}

bool MainWindow::applyBackgroundTextureIfNeeded() {
    if (!ui->glWidget->isBackgroundTextureEnabled()) return true;

    QString bgSrc = m_scene.bgTextureScriptText;
    bool bgHasLogic = bgSrc.contains("return") || bgSrc.contains("vec3")
            || bgSrc.contains("vec4") || bgSrc.contains("mainImage");
    if (bgHasLogic) {
        if (!ui->glWidget->validateAndApplyBackgroundShader(bgSrc)) {
            showShaderError("Syntax Error (Background Texture)", ui->glWidget->getShaderError());
            return false;
        }
        m_scene.bgTextureCode = bgSrc; // applicata e valida: committa
    }
    return true;
}
