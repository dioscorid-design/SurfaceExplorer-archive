// mainwindow_motion.cpp - MainWindow: moti della camera -- path 4D e 3D, rotazioni,
// navigazione, proiezione e FOV.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"

// Dock 3D e 4D: rotazioni, path, navigazione. Parte del costruttore, nell'ordine in cui la chiama.
void MainWindow::setupMotionDocks()
{
    // =========================================================================
    // 8. MOTION, PATHS & NAVIGATION
    // =========================================================================
    float omega = 0.0f, phi = 0.0f, psi = 0.0f;
    refreshRotationSpeedLabels();

    ui->glWidget->setRotation4D(omega, phi, psi);
    ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    ui->glWidget->setNutationSpeed(0.0f); ui->glWidget->setPrecessionSpeed(0.0f); ui->glWidget->setSpinSpeed(0.0f);
    ui->glWidget->setOmegaSpeed(0.0f); ui->glWidget->setPhiSpeed(0.0f); ui->glWidget->setPsiSpeed(0.0f);

    ui->btnStart_2->setText("GO");

    m_scene.lightingMode4D = 0;
    ui->glWidget->setLightingMode4D(0);
    ui->btnLightMode->setText("Directional Lighting");

    connect(ui->btnLightMode, &QPushButton::clicked, this, [this](){
        QString xEq = m_scene.eq.x.trimmed(); QString yEq = m_scene.eq.y.trimmed();
        QString zEq = m_scene.eq.z.trimmed(); QString pEq = m_scene.eq.p.trimmed();

        auto isNullCoord = [](const QString &s) { return s.isEmpty() || s == "0" || s == "0.0"; };

        // Sui TUBI Slice non c'e' (si basa sulla sezione 3D di una superficie):
        // restano Directional e Observer, vedi tubeLight4D in surface.vert.
        bool isDegenerate4D = tubesShown() || isNullCoord(xEq) || isNullCoord(yEq)
                              || isNullCoord(zEq) || isNullCoord(pEq);
        int numModes = (!isDegenerate4D) ? 3 : 2;
        m_scene.lightingMode4D = (m_scene.lightingMode4D + 1) % numModes;

        switch (m_scene.lightingMode4D) {
        case 0: ui->glWidget->setLightingMode4D(0); ui->btnLightMode->setText("Directional Lighting"); break;
        case 1: ui->glWidget->setLightingMode4D(1); ui->btnLightMode->setText("Observer Lighting"); break;
        case 2: ui->glWidget->setLightingMode4D(2); ui->btnLightMode->setText("Slice Lighting"); break;
        }
    });

    navTimer = new QTimer(this); navTimer->setInterval(30);
    connect(navTimer, &QTimer::timeout, this, &MainWindow::onNavTimerTick);

    // I path della camera: orologi, tempi, posa e camera (camerapaths.h).
    // Fermi durante il REC: li avanza il recorder, col dt del suo frame.
    m_paths = new CameraPaths(ui->glWidget, m_scene, [this] { return m_isRecording; }, this);

    ui->btnDeparture->setEnabled(false); connect(ui->btnDeparture, &QPushButton::clicked, this, &MainWindow::onDepartureClicked);
    ui->btnDeparture3D->setEnabled(false); connect(ui->btnDeparture3D, &QPushButton::clicked, this, &MainWindow::onDeparture3DClicked);

    m_scene.pathViewMode4D = ModeTangential;
    m_scene.pathViewMode3D = ModeTangential;
    // Enabled iniziale ai View: deciso da updateViewButtonsEnabled (campi path
    // vuoti all'avvio -> spenti, come il Departure; si accendono compilandoli).
    ui->pushView->setText("Tangent View"); ui->pushView->setEnabled(false); connect(ui->pushView, &QPushButton::clicked, this, &MainWindow::onToggleViewClicked);
    ui->pushView3D->setText("Tangent View"); ui->pushView3D->setEnabled(false); connect(ui->pushView3D, &QPushButton::clicked, this, &MainWindow::onToggleView3DClicked);

    connect(ui->lineX_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineY_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineZ_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineP_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);

    // Alpha/Beta/Gamma NON sono connessi: non accendono il Departure (solo le
    // COORDINATE lo fanno, vedi hasPath4DInput), quindi il loro textChanged
    // ricalcolerebbe sempre lo stesso esito. Collegarli direbbe il contrario.

    connect(ui->lineX_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineY_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineZ_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    // lineR_P3D (rollio) idem: fuori dal gate, vedi hasPath3DInput.

    // Le espressioni dei path possono usare le costanti A..F/S: scrivere una
    // costante in un campo path deve sbloccarne l'edit come nelle equazioni.
    for (QLineEdit* pathEdit : { ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
                                 ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P,
                                 ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D }) {
        connect(pathEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(pathEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);
        // Un path scritto (o svuotato) cambia cio' che il master avrebbe da
        // accendere: il suo testo va rifatto (vedi masterActivity).
        connect(pathEdit, &QLineEdit::textChanged, this, &MainWindow::updateMasterButtonState);
    }

    // (L'Invio sui campi path passa dai filtri tastiera desktop/mobile, che
    // consumano il Return e chiamano commitPathFieldOnEnter: connettere qui
    // returnPressed non servirebbe, il segnale non viene mai emesso.)

    // Stessa ragione per i limiti U/V/W, che ammettono A..F/S: scrivere "2*A"
    // in uMax sblocca subito slider e casella di A. Qui SOLO lo stato dell'UI
    // delle costanti: il limite si applica alla conferma del campo (Invio o
    // uscita), mai su textChanged -- altrimenti la mesh si rigenererebbe a ogni
    // carattere digitato.
    for (QLineEdit* limitEdit : { ui->uMinEdit, ui->uMaxEdit,
                                  ui->vMinEdit, ui->vMaxEdit,
                                  ui->wMinEdit, ui->wMaxEdit }) {
        connect(limitEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(limitEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);

        // Il dominio fa parte della definizione della superficie: toccarlo
        // riaccende il Run one-shot, esattamente come toccare X/Y/Z/P
        // (markUserEdit). Senza questo, cambiando i SOLI limiti il tasto
        // restava spento e non c'era piu' alcun modo di applicarli, ora che
        // non si applicano piu' da soli all'Invio.
        connect(limitEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            QWidget* w = qobject_cast<QWidget*>(sender());
            noteSceneEdited(w);
            m_parametricApplied = false;
            // Modifica DELL'UTENTE in attesa di conferma. textEdited (non
            // textChanged) scatta solo per la digitazione: i setText di preset,
            // reset e cambio tab non lo emettono, e non devono far validare
            // nulla all'uscita dal campo.
            if (w) setUserEditPending(w, true);
            updateMasterButtonState();
        });

        // Conferma del campo al CAMBIO DI FOCUS, non solo con l'Invio: valida
        // il numero (popup se illeggibile o min>=max) e registra il dominio --
        // che entra a schermo subito se la superficie e' in moto, al Run se e'
        // ferma. L'Invio arriva qui dai filtri tastiera, che consumano il
        // Return prima che QLineEdit emetta returnPressed; editingFinished
        // copre chi si limita a cliccare altrove.
        connect(limitEdit, &QLineEdit::editingFinished, this, [this, limitEdit]() {
            if (!m_uiReady) return;
            // Solo se c'e' una digitazione da confermare. Qt emette
            // editingFinished a ogni perdita di focus -- anche entrando e
            // uscendo da un campo senza toccarlo, e anche subito dopo il
            // clearFocus() del filtro tastiera sull'Invio: senza questa
            // guardia il campo verrebbe validato (e il popup mostrato) due
            // volte, o per un testo che l'utente non ha mai scritto.
            // Il controllo-e-consuma del flag sta dentro commitLimitFieldOnEnter,
            // sede unica: farlo anche qui lo consumerebbe prima, e la chiamata
            // del filtro tastiera che segue il clearFocus() tornerebbe a
            // rivalidare (due popup per un solo Invio).
            commitLimitFieldOnEnter(limitEdit->objectName());
        });
    }

    // LIMITI PER-MESH (pannello Multi Mesh). Stesso cablaggio dei limiti
    // globali qui sopra -- textEdited per registrare la digitazione in attesa,
    // editingFinished per confermarla -- ma con due assenze deliberate:
    //  - updateConstantsUIState: questi campi accettano gli stessi A..F/S, ma
    //    lo sblocco delle costanti lo decidono gia' i campi globali e le
    //    equazioni. Agganciarli qui non aggiungerebbe nulla e farebbe girare
    //    il ricalcolo a ogni carattere.
    //  - m_parametricApplied / updateMasterButtonState: il Run dello script
    //    RISCRIVE questi domini, quindi accendere il tasto Run come se ci
    //    fosse qualcosa "da applicare" direbbe il contrario del vero. Il
    //    dominio per-mesh si applica da se' alla conferma del campo.
    for (QLineEdit* meshLimitEdit : { ui->meshUMinEdit, ui->meshUMaxEdit,
                                      ui->meshVMinEdit, ui->meshVMaxEdit }) {
        if (!meshLimitEdit) continue;

        connect(meshLimitEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            QWidget* w = qobject_cast<QWidget*>(sender());
            // La scena e' cambiata come per ogni altro comando di aspetto
            // per-mesh: il dominio di una fascia fa parte di cio' che il
            // record salva.
            noteSceneEdited(w);
            if (w) setUserEditPending(w, true);
        });

        connect(meshLimitEdit, &QLineEdit::editingFinished, this, [this, meshLimitEdit]() {
            if (!m_uiReady) return;
            commitMeshLimitFieldOnEnter(meshLimitEdit->objectName());
        });
    }

    connectNavButton(ui->btnForward, GLWidget::MoveForward); connectNavButton(ui->btnBackward, GLWidget::MoveBack);
    connectNavButton(ui->btnLeft, GLWidget::MoveLeft); connectNavButton(ui->btnRight, GLWidget::MoveRight);
    connectNavButton(ui->btnDown, GLWidget::MoveDown); connectNavButton(ui->btnUp, GLWidget::MoveUp);
    connectNavButton(ui->btnRollLeft, GLWidget::RollLeft); connectNavButton(ui->btnRollRight, GLWidget::RollRight);
    connectNavButton(ui->btnXPlus,  GLWidget::ObsMoveXPos); connectNavButton(ui->btnXMinus, GLWidget::ObsMoveXNeg);
    connectNavButton(ui->btnYPlus,  GLWidget::ObsMoveYPos); connectNavButton(ui->btnYMinus, GLWidget::ObsMoveYNeg);
    connectNavButton(ui->btnZPlus,  GLWidget::ObsMoveZPos); connectNavButton(ui->btnZMinus, GLWidget::ObsMoveZNeg);
    connectNavButton(ui->btnPPlus,  GLWidget::ObsMovePPos); connectNavButton(ui->btnPMinus, GLWidget::ObsMovePNeg);
    connectNavButton(ui->btnOmegaAhead, GLWidget::RotOmegaPos); connectNavButton(ui->btnOmegaRear,  GLWidget::RotOmegaNeg);
    connectNavButton(ui->btnPhiRear, GLWidget::RotPhiNeg); connectNavButton(ui->btnPhiAhead,  GLWidget::RotPhiPos);
    connectNavButton(ui->btnPsiAhead,   GLWidget::RotPsiPos); connectNavButton(ui->btnPsiRear,    GLWidget::RotPsiNeg);
}


void MainWindow::switchTo3DMode()
{   updateLayoutForMode(1);
    ui->glWidget->set4DLighting(false);
    updateProjectionButtonText();
}

void MainWindow::switchTo4DMode() {
    updateLayoutForMode(2);
    ui->glWidget->set4DLighting(true);
    updateProjectionButtonText();
}

void MainWindow::update4DButtonState()
{
    // 1. Controllo Equazione P
    QString pText = m_scene.eq.p.trimmed();

    // Gestione Virgola/Punto
    QString sanitizedP = pText;
    sanitizedP.replace(",", ".");

    bool isNumber;
    float val = sanitizedP.toFloat(&isNumber);

    bool isEquation3D = true;

    if (!pText.isEmpty()) {
        if (isNumber) {
            if (std::abs(val) > 0.001f) {
                isEquation3D = false;
            }
        } else {
            isEquation3D = false;
        }
    }

    // 2. Controllo Rotazione 4D (Angoli Omega, Phi, Psi)
    bool has4DRotation = false;
    if (ui->glWidget) {
        float eps = 0.01f;
        bool angleNonZero = (std::abs(ui->glWidget->getOmega()) > eps ||
                             std::abs(ui->glWidget->getPhi())   > eps ||
                             std::abs(ui->glWidget->getPsi())   > eps);
        has4DRotation = angleNonZero;
    }

    // 3. Logica: ABILITA SE (P è valido e != 0) OPPURE (C'è rotazione 4D)
    bool enable4D = (!isEquation3D) || has4DRotation;

    // 4. --- CERCA IL PULSANTE NELLA STATUS BAR E ABILITALO/DISABILITALO ---
    QPushButton* btn4D = ui->statusbar->findChild<QPushButton*>("btnDock4D");
    if (btn4D) {
        btn4D->setEnabled(true);
    }

    // 5. Uscita forzata se disabilitato mentre attivo.
    // MA non mentre un path anima: all'avvio di un Departure questa funzione viene
    // richiamata prima che il primo tick imposti la rotazione 4D, quindi enable4D
    // potrebbe risultare falso e chiuderebbe il dock 4D da cui si e' avviato il
    // path. I dock si chiudono solo a mano (o quando P viene svuotato a path fermo).
    bool pathActive = pathRunning(CameraPaths::Path4D) ||
                      pathRunning(CameraPaths::Path3D);
    if (!enable4D && ui->dock4D->isVisible() && !pathActive) {
        ui->dock4D->close();
    }
}

void MainWindow::stopPathAnimations()
{
    // Ferma entrambi i percorsi camera (4D + 3D) riportandoli allo stato "fermo".
    // Rispecchia il ramo STOP di onDepartureClicked / onDeparture3DClicked.
    bool changed = false;
    if (pathRunning(CameraPaths::Path4D)) {
        m_paths->stop(CameraPaths::Path4D);
        ui->btnDeparture->setText("DEPARTURE");
        checkPathFields();
        changed = true;
    }
    if (pathRunning(CameraPaths::Path3D)) {
        m_paths->stop(CameraPaths::Path3D);
        ui->btnDeparture3D->setText("DEPARTURE");
        checkPath3DFields();
        changed = true;
    }
    if (changed) {
        if (ui->glWidget) ui->glWidget->setPathAnimating(false);
        updateViewButtonsEnabled();
    }
}

void MainWindow::stopRotationMotion()
{
    // Ferma il moto GO (rotazioni superficie/4D). Rispecchia il ramo "STOP" di
    // onStopClicked, ma non tocca i timer dei path.
    if (ui->glWidget && ui->glWidget->isAnimating()) {
        ui->glWidget->pauseMotion();
        if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    }
}

void MainWindow::onResetClicked()
{
    // SCENA VUOTA DI NEW: niente da rimettere, e lo si dice come START e
    // Projection (prima non succedeva nulla, senza una parola). Resta il
    // RESET di uno sfondo che si muove: animato (Motions ne azzera
    // l'orologio) o a cielo (View ne rimette la direzione).
    if (isWholeSceneEmpty()
        && !masterActivity().bgAvailable
        && !(ui->glWidget->isBackgroundTextureEnabled()
             && ui->glWidget->backgroundSkyMode() != GLWidget::BgFixed)) {
        InputValidator::showEmptySceneNotice(this, "Nothing to reset");
        return;
    }
    // Il path che ha la camera si guarda PRIMA di resettare la vista, che da
    // fermo gliela toglie: e' quello che i moti rimettono al punto di partenza.
    const std::optional<CameraPaths::Path> camPath = cameraPath();

    // LA CAMERA E' DI UN PATH, in corsa o fermo: il RESET e' quello dei moti,
    // senza domande. Camera al punto di partenza del path, t = 0. View la
    // porterebbe alla vista libera e alla ripartenza il path la riprenderebbe di
    // colpo dal tempo a cui era arrivato; a path in corsa era spento, e il popup
    // offriva una scelta sola.
    if (camPath) {
        resetMotions(camPath);
        return;
    }

    // Se niente si muove -- ne' rotazioni, ne' path, ne' animazioni in t -- la
    // domanda non ha senso: si rimette tutto (anche l'orientamento girato col
    // mouse). Che cosa si muove lo dice la stessa sede del master (masterActivity).
    const MasterActivity act = masterActivity();
    if (!act.eqAvailable && !act.eqRunning && !act.texAvailable && !act.bgAvailable
        && !act.borderAvailable && !act.cameraAvailable) {
        resetView();
        resetMotions(camPath);
        return;
    }

    // Se una delle due parti non ha niente da rimettere la scelta e' una sola:
    // la camera gia' all'inquadratura d'apertura (un record di rotazioni: gira la
    // superficie, non la camera) -> i moti; i moti gia' all'inizio (dopo un
    // Motions a moti fermi) -> la vista. Il popup solo se tutte e due hanno
    // qualcosa da rimettere.
    const bool viewChanges = viewResetChangesSomething();
    const bool motionsChange = motionsResetChangesSomething();
    if (!viewChanges || !motionsChange) {
        if (viewChanges) resetView();
        if (motionsChange || !viewChanges) resetMotions(camPath);
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle("Reset");
    box.setText("What do you want to reset?");
    box.setInformativeText(QString("View: camera, zoom and field of view.\n"
                                   "Motions: rotations, camera paths and everything that moves "
                                   "with t go back to the start."));
    QPushButton *viewBtn   = box.addButton("View",    QMessageBox::ActionRole);
    QPushButton *motionBtn = box.addButton("Motions", QMessageBox::ActionRole);
    QPushButton *bothBtn   = box.addButton("Both",    QMessageBox::AcceptRole);
    box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(bothBtn);
    box.exec();

    const QAbstractButton *choice = box.clickedButton();
    if (choice == viewBtn   || choice == bothBtn) resetView();
    if (choice == motionBtn || choice == bothBtn) resetMotions(camPath);
}

bool MainWindow::viewResetChangesSomething() const
{
    const GLWidget::Framing target = m_startFraming;
    const GLWidget::Framing now = ui->glWidget->framing();
    auto near = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
    const bool same = (now.cameraPos - target.cameraPos).length() < 1e-3f
                      && near(now.yaw, target.yaw) && near(now.pitch, target.pitch)
                      && near(now.roll, target.roll)
                      && (now.observerPos - target.observerPos).length() < 1e-3f
                      && near(now.crossSectionP, target.crossSectionP)
                      && near(m_scene.fov, m_startFov);
    return !same;
}

bool MainWindow::motionsResetChangesSomething() const
{
    GLWidget *gl = ui->glWidget;
    if (m_paths->time(CameraPaths::Path4D) != 0.0f || m_paths->time(CameraPaths::Path3D) != 0.0f)
        return true;
    const GLWidget::Orientation a = gl->orientation();
    const GLWidget::Orientation &b = m_startOrientation;
    if (std::abs(QQuaternion::dotProduct(a.rotation, b.rotation)) < 0.999999f
        || std::abs(a.omega - b.omega) > 1e-4f || std::abs(a.phi - b.phi) > 1e-4f
        || std::abs(a.psi - b.psi) > 1e-4f)
        return true;
    if (gl->anyClockAdvanced()) return true;
    return m_geoTime != 0.0;
}

void MainWindow::captureStartPose()
{
    if (!ui->glWidget) return;
    m_startFraming     = ui->glWidget->framing();
    m_startOrientation = ui->glWidget->orientation();
    m_startFov         = m_scene.fov;
}

std::optional<CameraPaths::Path> MainWindow::cameraPath() const
{
    if (pathRunning(CameraPaths::Path4D)) return CameraPaths::Path4D;
    if (pathRunning(CameraPaths::Path3D)) return CameraPaths::Path3D;
    if (!ui->glWidget || !ui->glWidget->isPathFollowing()) return std::nullopt;
    if (m_scene.lastCameraMotion == QLatin1String("path4D")) return CameraPaths::Path4D;
    if (m_scene.lastCameraMotion == QLatin1String("path3D")) return CameraPaths::Path3D;
    return std::nullopt;
}

void MainWindow::resetView()
{
    // Inquadratura d'apertura. Le velocita' e i moti non si toccano. Un path
    // FERMO lascia la camera: si torna alla vista libera (resetFraming spegne
    // la modalita' path).
    ui->glWidget->resetFraming(m_startFraming);

    // Un path IN CORSA la camera la tiene: gliela si rimette subito al suo
    // tempo, invece di mostrare la vista libera per un fotogramma fino al
    // prossimo tick. Il resto dell'inquadratura (FOV, e osservatore 4D per il
    // path 3D) torna comunque all'apertura.
    for (CameraPaths::Path p : { CameraPaths::Path4D, CameraPaths::Path3D })
        if (pathRunning(p)) m_paths->applyCameraAt(p, m_paths->time(p));

    // Il FOV della scena, slider compreso: anche lui e' uno zoom.
    applyCameraFov(m_startFov);

    // Aggiorna in sicurezza la mesh. useAppliedEquations: il ripristino della
    // vista non e' un Run e non deve applicare equazioni in sospeso.
    checkAndTriggerMeshUpdate(/*useAppliedEquations=*/true);
    updateMasterButtonState();

    // Lo stato 4D appena rimesso diventa il nuovo zero dei campi del dock 4D.
    resetNav4DBaseline();
}

void MainWindow::resetMotions(std::optional<CameraPaths::Path> camPath)
{
    // PATH: t = 0. Uno in corsa riparte da capo e resta su "STOP"; uno fermo
    // resta fermo, e al Departure ripartira' dall'inizio invece che da dove si
    // era fermato (da solo il Departure riprende dal tempo raggiunto).
    m_paths->resetTimes();
    if (!pathRunning(CameraPaths::Path4D)) {
        ui->btnDeparture->setText("DEPARTURE");
        checkPathFields();
    }
    if (!pathRunning(CameraPaths::Path3D)) {
        ui->btnDeparture3D->setText("DEPARTURE");
        checkPath3DFields();
    }

    // ROTAZIONI: posa d'apertura. Le velocita' restano: un GO in corso
    // continua a girare da li'.
    ui->glWidget->resetOrientation(m_startOrientation);

    // La camera del path torna al punto di partenza, SUBITO: da fermo
    // resterebbe dov'era fino al Departure (il reset sembrava non fare nulla),
    // in corsa ci arriverebbe al tick dopo. Dopo l'orientamento: il path 4D
    // scrive i propri angoli, come al tick.
    if (camPath) m_paths->applyCameraAt(*camPath, 0.0f);

    // ANIMAZIONI IN t: orologi all'origine (fasce comprese) e flusso geodetico
    // a geoTime 0. I flag di moto non cambiano, quindi master e tasti dei dock
    // restano come sono.
    ui->glWidget->resetAllClocks();
    m_geoTime = 0.0;

    // Le animazioni in t leggono l'orologio nello shader al prossimo frame; il
    // flusso geodetico invece e' una mesh calcolata sulla CPU al suo geoTime e,
    // da fermo, resterebbe sul fotogramma di prima. useAppliedEquations: non e'
    // un Run, non applica equazioni in sospeso e non avvia un flusso fermo.
    checkAndTriggerMeshUpdate(/*useAppliedEquations=*/true);
    updateMasterButtonState();

    // Gli angoli 4D sono cambiati: nuovo zero dei campi del dock 4D.
    resetNav4DBaseline();
}

void MainWindow::onNavTimerTick()
{
    if (activeNavActions.isEmpty()) return;

    // Stato 4D PRIMA degli scatti di questo tick. La differenza col dopo e'
    // quanto hanno prodotto i tasti -- e solo loro, perche' fra queste due
    // righe non gira nient'altro: le rotazioni automatiche avanzano nel tick di
    // rotationTimer, il path nel suo. E' il motivo per cui la misura si prende
    // qui e non in updateNav4DReadout, dove i due contributi sarebbero gia'
    // sommati e indistinguibili.
    const QVector4D obsBefore = ui->glWidget->observerPos();
    const float csPBefore   = ui->glWidget->crossSectionP();
    const float omegaBefore = ui->glWidget->getOmega();
    const float phiBefore   = ui->glWidget->getPhi();
    const float psiBefore   = ui->glWidget->getPsi();

    for (int action : activeNavActions) {
        ui->glWidget->virtualMove(static_cast<GLWidget::MoveDir>(action), pathSpeed3D(), pathSpeed4D());
    }

    m_nav4DDeltaObs   += ui->glWidget->observerPos() - obsBefore;
    m_nav4DDeltaCsP   += ui->glWidget->crossSectionP() - csPBefore;
    m_nav4DDeltaOmega += ui->glWidget->getOmega() - omegaBefore;
    m_nav4DDeltaPhi   += ui->glWidget->getPhi()   - phiBefore;
    m_nav4DDeltaPsi   += ui->glWidget->getPsi()   - psiBefore;

    updateNav4DReadout();

    // useAppliedEquations: navigare non e' un Run.
    checkAndTriggerMeshUpdate(/*useAppliedEquations=*/true);
}

// Readout numerico dei tasti a scatto del dock 4D: i tasti non davano alcun
// riscontro, quindi dopo qualche click non c'era modo di sapere il valore
// corrente ne' di tornare a una posizione nota.
//
// IL DOCK SERVE DUE MODALITA' CON SEMANTICHE DIVERSE, e le label devono seguire
// quella attiva, non una sola delle due:
//  - PARAMETRICO: X/Y/Z/P muovono l'OSSERVATORE 4D (m_observerPos, che il ramo
//    parametrico legge davvero). Nota che la sua W parte da 4.0, non da 0: e'
//    la distanza di default della camera 4D, e mostrare "0.00" li' sarebbe una
//    bugia.
//  - RAY MARCHING: X/Y/Z sono spenti (scriverebbero uno stato che il template
//    del marcher non legge, vedi updateRenderState) e P muove invece la QUOTA
//    DEL PIANO DI SEZIONE (m_crossSectionP). Le label X/Y/Z mostrano percio'
//    un trattino: il valore esiste nel motore ma non descrive nulla di cio' che
//    si vede, e un numero che non cambia mai premendo il tasto confonde.
// Omega/Phi/Psi sono gli stessi angoli nei due modi, quindi non si ramificano.
void MainWindow::updateNav4DReadout()
{
    if (!ui->glWidget) return;

    const bool isImplicitMode = (implicitMode());

    // Il segno si mostra esplicitamente ('+' incluso) perche' un delta senza
    // segno si confonde con una posizione.
    auto fmt = [](float v) {
        // -0.00 e' matematicamente corretto ma si legge come un errore.
        if (qAbs(v) < 0.005f) v = 0.0f;
        return QString::asprintf("%+.2f", v);
    };

    if (ui->lblXVal4D) ui->lblXVal4D->setText(fmt(m_nav4DDeltaObs.x()));
    if (ui->lblYVal4D) ui->lblYVal4D->setText(fmt(m_nav4DDeltaObs.y()));
    if (ui->lblZVal4D) ui->lblZVal4D->setText(fmt(m_nav4DDeltaObs.z()));

    // P: quota della sezione in Ray Marching, quarta coordinata (distanza) della
    // camera 4D in parametrico. Due grandezze distinte, due accumulatori: lo
    // stesso tasto muove l'una o l'altra secondo il modo, e passando da un modo
    // all'altro il campo deve mostrare gli scatti dati IN QUEL modo.
    if (ui->lblPVal4D)
        ui->lblPVal4D->setText(fmt(isImplicitMode ? m_nav4DDeltaCsP
                                                  : m_nav4DDeltaObs.w()));

    if (ui->lblOmegaVal4D) ui->lblOmegaVal4D->setText(fmt(m_nav4DDeltaOmega));
    if (ui->lblPhiVal4D)   ui->lblPhiVal4D->setText(fmt(m_nav4DDeltaPhi));
    if (ui->lblPsiVal4D)   ui->lblPsiVal4D->setText(fmt(m_nav4DDeltaPsi));
}

// Azzera il conto degli scatti: il dock torna a sette "+0.00" qualunque sia lo
// stato reale del motore. E' il comportamento voluto anche sui preset, dove
// l'inquadratura salvata e' il punto da cui l'utente inizia a muoversi.
// (Il nome parla ancora di "baseline" per continuita' coi ~4 punti che la
// chiamano -- cambio scena, load di superfici e record, tasto RESET.)
void MainWindow::resetNav4DBaseline()
{
    if (!ui->glWidget) return;
    m_nav4DDeltaObs   = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    m_nav4DDeltaCsP   = 0.0f;
    m_nav4DDeltaOmega = 0.0f;
    m_nav4DDeltaPhi   = 0.0f;
    m_nav4DDeltaPsi   = 0.0f;
    updateNav4DReadout();
}

void MainWindow::commitPathFieldOnEnter(const QString& fieldName)
{
    // Invio su un campo path a moto ATTIVO: ricompila le equazioni al volo,
    // cosi' una costante (o qualunque modifica all'espressione) entra subito
    // senza fermare e far ripartire il path. I VALORI delle costanti sono gia' live
    // (m_pathSymbolTable le lega per riferimento, vedi SurfaceEngine).
    // NB: chiamata dai filtri tastiera (desktop e mobile), che consumano il
    // Return prima che i QLineEdit possano emettere returnPressed.
    //
    // CAMPI SVUOTATI: l'Invio FERMA il path invece di ricompilarlo. Cancellare
    // tutti i campi e confermare e' il modo naturale di dire "basta": senza
    // questo ramo il path continuava a correre sull'ultima compilazione valida,
    // con il tasto su STOP e il master su Stop, su una definizione che a schermo
    // non esisteva piu'. Si passa da onDeparture*Clicked a timer ATTIVO, cioe'
    // dal suo ramo di arresto: ferma il timer, riporta il tasto a DEPARTURE e
    // riallinea Departure (che ora si spegne, campi vuoti) e master (che torna
    // su Start). Rifare quei passaggi a mano qui significherebbe due copie che
    // divergono al primo cambiamento.
    // PATH FERMO e campi validi: l'Invio AVVIA, come il tasto. Scritta
    // un'equazione, confermarla e vederla partire e' il gesto naturale, e ora
    // che basta una coordinata sola il giro "scrivo, poi vado col mouse sul
    // tasto" e' quasi sempre superfluo.
    // Si passa da onDeparture*Clicked a timer INATTIVO, cioe' dal suo ramo di
    // avvio: compila (e in caso d'errore non parte, col suo popup), applica la
    // mutua esclusivita' con l'altro path e col moto GO, fa l'handoff di camera
    // 3D<->4D, porta il tasto su STOP e il master su Stop. Tutte cose che
    // rifatte qui a mano sarebbero una seconda copia destinata a divergere.
    //
    // Riassunto dei tre casi, per lato:
    //   campi vuoti + path in corsa -> ferma      (ramo STOP)
    //   campi validi + path in corsa -> ricompila al volo
    //   campi validi + path fermo    -> avvia     (ramo DEPARTURE)
    //   campi vuoti  + path fermo    -> niente
    if (fieldName.endsWith("_P3D")) {
        if (!m_paths) return;
        if (pathRunning(CameraPaths::Path3D)) {
            if (!hasPath3DInput()) onDeparture3DClicked();
            else                   compilePath3DFromFields();
        } else if (hasPath3DInput()) {
            onDeparture3DClicked();
        }
    } else {
        if (!m_paths) return;
        if (pathRunning(CameraPaths::Path4D)) {
            if (!hasPath4DInput()) onDepartureClicked();
            else                   compilePath4DFromFields();
        } else if (hasPath4DInput()) {
            onDepartureClicked();
        }
    }
}

bool MainWindow::compilePath4DFromFields()
{
    // Pulizia input (campo vuoto = "0", virgola decimale tollerata)
    auto getSafeEq = [this](QLineEdit* line) {
        QString t = lineText(line).trimmed();
        if (t.isEmpty()) return QString("0");
        return t.replace(",", ".");
    };

    QString eqX = getSafeEq(ui->lineX_P);
    QString eqY = getSafeEq(ui->lineY_P);
    QString eqZ = getSafeEq(ui->lineZ_P);
    QString eqP = getSafeEq(ui->lineP_P);

    // Opzionali
    QString eqAlpha = getSafeEq(ui->lineAlpha_P);
    QString eqBeta  = getSafeEq(ui->lineBeta_P);
    QString eqGamma = getSafeEq(ui->lineGamma_P);

    bool syntaxWarned = false;
    if (!InputValidator::validateFieldList(this, {
        {"X(t)", eqX},
        {"Y(t)", eqY},
        {"Z(t)", eqZ},
        {"W(t)", eqP},
        {"Alpha(t)", eqAlpha},
        {"Beta(t)", eqBeta},
        {"Gamma(t)", eqGamma}
    }, &syntaxWarned)) {
        return false;
    }

    bool ok = ui->glWidget->getEngine()->compilePathEquations(eqX, eqY, eqZ, eqP, eqAlpha, eqBeta, eqGamma);
    if (!ok) {
        // Niente secondo popup se un warning di sintassi e' gia' comparso (stesso errore).
        if (!syntaxWarned)
            QMessageBox::warning(this, "Error", "Path 4D compilation error .\nCheck the syntax.");
        return false;
    }
    // Il path compilato e' a schermo: le costanti che usa si sbloccano ora
    // (con modifiche in sospeso lo slider aspetta che sia applicato).
    updateConstantsUIState();
    return true;
}

bool MainWindow::compilePath3DFromFields()
{
    auto getSafeEq = [this](QLineEdit* line) {
        QString t = lineText(line).trimmed();
        if (t.isEmpty()) return QString("0");
        return t.replace(",", ".");
    };

    QString eqX = getSafeEq(ui->lineX_P3D);
    QString eqY = getSafeEq(ui->lineY_P3D);
    QString eqZ = getSafeEq(ui->lineZ_P3D);
    QString eqR = getSafeEq(ui->lineR_P3D);

    bool syntaxWarned = false;
    if (!InputValidator::validateFieldList(this, {
        {"X(t)", eqX},
        {"Y(t)", eqY},
        {"Z(t)", eqZ},
        {"Roll(t)", eqR}
    }, &syntaxWarned)) {
        return false;
    }

    bool ok = ui->glWidget->getEngine()->compilePath3DEquations(eqX, eqY, eqZ, eqR);
    if (!ok) {
        // Niente secondo popup se l'utente e' gia' stato avvisato da un warning di
        // sintassi (es. operatori consecutivi): sarebbe lo stesso errore due volte.
        if (!syntaxWarned)
            QMessageBox::warning(this, "Error", "3D path compilation error.\nCheck the syntax.");
        return false;
    }
    // Come per il path 4D: le sue costanti si sbloccano a path applicato.
    updateConstantsUIState();
    return true;
}

void MainWindow::onDepartureClicked()
{
    // NB: funziona anche DURANTE il REC — i timer restano attivi (tick no-op)
    // e il loop legge lo stato vivo a ogni frame (vedi CameraPaths::tick).

    // CASO 1: VOGLIAMO FERMARE
    if (pathRunning(CameraPaths::Path4D)) {
        m_paths->stop(CameraPaths::Path4D);
        m_userStoppedCameraMotion = true;
        if (ui->glWidget) ui->glWidget->setPathAnimating(false);
        updateViewButtonsEnabled();
        ui->btnDeparture->setText("DEPARTURE");
        checkPathFields();
        updateMasterButtonState();
        return;
    }

    // CASO 2: VOGLIAMO PARTIRE
    // Mutua esclusivita': fermiamo il percorso 3D e il moto GO se attivi.
    // Se stiamo SUBENTRANDO al path 3D, la camera fa l'handoff (scivolata
    // continua invece del teletrasporto): vedi GLWidget::beginPathHandoff.
    const bool handoffFrom3D = pathRunning(CameraPaths::Path3D);
    if (pathRunning(CameraPaths::Path3D)) {
        m_paths->stop(CameraPaths::Path3D);
        ui->btnDeparture3D->setText("DEPARTURE");
    }
    stopRotationMotion();

    if (!compilePath4DFromFields()) {
        return; // Errore matematico o di compilazione: niente avvio
    }

    // Base 4D per le compensazioni del tick (vedi CameraPaths::captureBase4D):
    // con le equazioni APPENA compilate qui sopra.
    m_paths->captureBase4D();

    if (handoffFrom3D && ui->glWidget) ui->glWidget->beginPathHandoff();

    m_paths->start(CameraPaths::Path4D);
    m_scene.lastCameraMotion = "path4D";
    m_userStoppedCameraMotion = false;
    if (ui->glWidget) {
        ui->glWidget->setPathAnimating(true);
        // Solo il PRIMO Departure della sessione azzera la rotazione di default
        // (se non ruotata a mano); dai successivi si conserva l'orientamento
        // accumulato (es. dal moto GO), senza reset nel cambio di modalita'.
        if (m_paths->takeFirstStart()) ui->glWidget->neutralizeDefaultRotationForPath();
    }
    updateViewButtonsEnabled();
    ui->btnDeparture->setText("STOP");

    updateMasterButtonState();
    update4DButtonState();   // riallinea i controlli 4D dopo l'eventuale stop del moto GO
}

// UNA COORDINATA BASTA, MA DEVE ESSERE UNA COORDINATA.
// Un path con la sola X e' legittimo: gli altri assi valgono 0 (campo vuoto ->
// "0", vedi compilePath4DFromFields) e il moto corre lungo un asse solo, che e'
// il caso piu' semplice da scrivere a mano. La vecchia soglia >=2 lo negava
// senza dire perche'.
//
// Gli ANGOLI (Alpha/Beta/Gamma) non contano ai fini dell'accensione. Da soli
// lasciano X=Y=Z=P=0: la camera resta ferma nell'ORIGINE, che e' anche il punto
// guardato -- in Center View il target e' (0,0,0), in Tangent View e' la
// posizione a t+delta, cioe' ancora l'origine. Direzione di vista nulla, matrice
// di vista degenere: a schermo la superficie sembra sparita, mentre in realta'
// si e' dentro di essa senza una direzione in cui guardare. Un orientamento non
// definisce un percorso; serve qualcosa che muova il PUNTO.
bool MainWindow::hasPath4DInput() const
{
    int filled = 0;
    if (!m_scene.path.x.trimmed().isEmpty()) filled++;
    if (!m_scene.path.y.trimmed().isEmpty()) filled++;
    if (!m_scene.path.z.trimmed().isEmpty()) filled++;
    if (!m_scene.path.p.trimmed().isEmpty()) filled++;

    return filled >= 1;
}

void MainWindow::checkPathFields()
{
    // I timer nascono a META' del costruttore (~3986), ma updateRenderState --
    // che ora chiama questa funzione -- e' raggiungibile gia' da prima: senza
    // questa guardia l'app crashava all'avvio sul primo isActive().
    // updateViewButtonsEnabled si protegge gia' da se'.
    if (!m_paths || !ui->btnDeparture) return;

    if (pathRunning(CameraPaths::Path4D)) {
        ui->btnDeparture->setEnabled(true);
    } else {
        ui->btnDeparture->setEnabled(hasPath4DInput());
    }
    updateViewButtonsEnabled();
}

void MainWindow::onDeparture3DClicked()
{
    // NB: funziona anche DURANTE il REC — i timer restano attivi (tick no-op)
    // e il loop legge lo stato vivo a ogni frame (vedi CameraPaths::tick).

    // CASO 1: STOP
    if (pathRunning(CameraPaths::Path3D)) {
        m_paths->stop(CameraPaths::Path3D);
        m_userStoppedCameraMotion = true;
        if (ui->glWidget) ui->glWidget->setPathAnimating(false);
        updateViewButtonsEnabled();
        ui->btnDeparture3D->setText("DEPARTURE");
        checkPath3DFields();
        updateMasterButtonState();
        return;
    }

    // CASO 2: START
    // Mutua esclusivita': fermiamo il percorso 4D e il moto GO se attivi.
    // Se stiamo SUBENTRANDO al path 4D, la camera fa l'handoff (scivolata
    // continua invece del teletrasporto): vedi GLWidget::beginPathHandoff.
    const bool handoffFrom4D = pathRunning(CameraPaths::Path4D);
    if (pathRunning(CameraPaths::Path4D)) {
        m_paths->stop(CameraPaths::Path4D);
        ui->btnDeparture->setText("DEPARTURE");
    }
    stopRotationMotion();

    if (!compilePath3DFromFields()) {
        return; // Errore matematico o di compilazione: niente avvio
    }

    if (handoffFrom4D && ui->glWidget) ui->glWidget->beginPathHandoff();

    m_paths->start(CameraPaths::Path3D);
    m_scene.lastCameraMotion = "path3D";
    m_userStoppedCameraMotion = false;
    if (ui->glWidget) {
        ui->glWidget->setPathAnimating(true);
        // Solo il PRIMO Departure della sessione azzera la rotazione di default
        // (se non ruotata a mano); dai successivi si conserva l'orientamento
        // accumulato (es. dal moto GO), senza reset nel cambio di modalita'.
        if (m_paths->takeFirstStart()) ui->glWidget->neutralizeDefaultRotationForPath();
    }
    updateViewButtonsEnabled();
    ui->btnDeparture3D->setText("STOP");

    updateMasterButtonState();
    update4DButtonState();   // riallinea i controlli 4D dopo l'eventuale stop del moto GO
}

// Una coordinata basta, e R(t) non e' una coordinata: e' il ROLLIO, che ruota
// attorno alla direzione di vista. Col solo R la camera resta nell'origine a
// guardare l'origine, e il rollio non ha nemmeno un asse su cui agire -- vedi
// la nota estesa su hasPath4DInput.
bool MainWindow::hasPath3DInput() const
{
    int filled = 0;
    if (!m_scene.path.x3D.trimmed().isEmpty()) filled++;
    if (!m_scene.path.y3D.trimmed().isEmpty()) filled++;
    if (!m_scene.path.z3D.trimmed().isEmpty()) filled++;

    return filled >= 1;
}

void MainWindow::checkPath3DFields()
{
    // Stessa guardia di checkPathFields: timer non ancora costruiti all'avvio.
    if (!m_paths || !ui->btnDeparture3D) return;

    if (pathRunning(CameraPaths::Path3D)) {
        ui->btnDeparture3D->setEnabled(true);
    } else {
        ui->btnDeparture3D->setEnabled(hasPath3DInput());
    }
    updateViewButtonsEnabled();
}

void MainWindow::updateViewButtonsEnabled()
{
    // I tasti Tangent/Center View rispecchiano i rispettivi Departure: attivi
    // se il proprio path e' in corsa O i suoi campi sono compilati. Anche a
    // path ALTRUI in corsa il View resta attivo coi campi compilati: per il
    // subentro 3D<->4D (handoff) si deve poter pre-selezionare la vista con
    // cui partira' l'altro path (m_scene.pathViewMode4D/m_scene.pathViewMode3D sono letti nei tick).
    bool path4D = pathRunning(CameraPaths::Path4D);
    bool path3D = pathRunning(CameraPaths::Path3D);
    ui->pushView->setEnabled(path4D || hasPath4DInput());
    ui->pushView3D->setEnabled(path3D || hasPath3DInput());

    // Gli slider FOV seguono lo stesso ciclo di vita dei path (questa funzione
    // e' chiamata ovunque un path parta o si fermi); il fattore proiezione e'
    // coperto dalla chiamata gemella in updateProjectionButtonText().
    // NB: lo STOP di un path NON tocca il FOV: l'immagine si congela
    // sull'ultimo fotogramma (posa E prospettiva). Il ritorno al default 45
    // avviene in GLWidget::resetTransformations, cioe' solo quando la vista
    // viene davvero resettata (Reset view, cambio tab, load).

    // Mentre un path qualsiasi controlla la telecamera, i tasti di spostamento a
    // click dei dock 3D/4D sono disabilitati. Chiamato ovunque si avvii/fermi un
    // path (in coppia con setPathAnimating), quindi resta sempre sincronizzato.
    // I comandi mouse 3D (rotazione/zoom) sono bloccati a parte in InputHandler
    // via GLWidget::isPathAnimating().
    setNavControlsEnabled(!(path4D || path3D));
}

void MainWindow::setNavControlsEnabled(bool enabled)
{
    // Tasti di spostamento a click dei dock 3D/4D (X±, Y±, left/right, roll, ...).
    for (QPushButton* btn : m_navButtons) {
        if (btn) btn->setEnabled(enabled);
    }

    // RIACCENSIONE NON INCONDIZIONATA. Questa funzione spegne i tasti mentre un
    // path guida la telecamera e li riaccende quando finisce, ma 'enabled=true'
    // significa "il path non comanda piu'", NON "tutti questi tasti hanno
    // senso adesso": in Ray Marching gli spostamenti X/Y/Z dell'osservatore
    // restano spenti in ogni caso (scrivono u_observerPos, che il template del
    // marcher non legge, e sarebbero un doppio spostamento rispetto alla
    // matrice di vista -- vedi updateRenderState).
    // Senza questo ripristino bastava DIGITARE in un campo path per riaccenderli:
    // textChanged -> checkPathFields -> updateViewButtonsEnabled ->
    // setNavControlsEnabled(true), e i tasti tornavano attivi a sproposito.
    // I vincoli di modalita' li decide updateRenderState, con gli STESSI due
    // gate usati la': gli spostamenti dell'osservatore sono sempre spenti in
    // Ray Marching, mentre P+/P- e le rotazioni 4D dipendono da rot4DUsable
    // (accesi nel solo sotto-tab Cross Section, dove governano quota e
    // inclinazione del piano di sezione).
    if (enabled && implicitMode()) {
        for (QPushButton *b : { ui->btnXPlus, ui->btnXMinus,
                                ui->btnYPlus, ui->btnYMinus,
                                ui->btnZPlus, ui->btnZMinus,
                                ui->btnLightMode }) {
            if (b) b->setEnabled(false);
        }
        const bool rot4DUsable = crossSectionTab();
        for (QPushButton *b : { ui->btnPPlus,      ui->btnPMinus,
                                ui->btnOmegaAhead, ui->btnOmegaRear,
                                ui->btnPhiAhead,   ui->btnPhiRear,
                                ui->btnPsiAhead,   ui->btnPsiRear }) {
            if (b) b->setEnabled(rot4DUsable);
        }
    }
}

float MainWindow::pathSpeed3D() const { return m_scene.pathSpeed3D / 1000.0f; }
float MainWindow::pathSpeed4D() const { return m_scene.pathSpeed4D / 1000.0f; }

// Lo slider taglia da se' i valori fuori dal suo intervallo: lo stato fa lo
// stesso, o il Save scriverebbe una velocita' che lo slider non mostra.
void MainWindow::setPathSpeed3D(int speed)
{
    m_scene.pathSpeed3D = qBound(ui->speed3DSlider->minimum(), speed, ui->speed3DSlider->maximum());
    const QSignalBlocker b(ui->speed3DSlider);
    ui->speed3DSlider->setValue(m_scene.pathSpeed3D);
}

void MainWindow::setPathSpeed4D(int speed)
{
    m_scene.pathSpeed4D = qBound(ui->speed4DSlider->minimum(), speed, ui->speed4DSlider->maximum());
    const QSignalBlocker b(ui->speed4DSlider);
    ui->speed4DSlider->setValue(m_scene.pathSpeed4D);
}

void MainWindow::setPathViewModes(CameraPathMode mode4D, CameraPathMode mode3D)
{
    m_scene.pathViewMode4D = mode4D;
    m_scene.pathViewMode3D = mode3D;
    ui->pushView->setText(m_scene.pathViewMode4D == ModeTangential ? "Tangent View" : "Center View");
    ui->pushView3D->setText(m_scene.pathViewMode3D == ModeTangential ? "Tangent View" : "Center View");
}

void MainWindow::resetMotionControls()
{
    // La scena nuova non eredita i comandi dei path di quella di prima. Li
    // riscrivevano solo i record: dopo NEW, un cambio di modalita' o il load di
    // una superficie, un path scritto da capo partiva con la vista (Center) e
    // la velocita' del record appena lasciato, e il Save scriveva come moto
    // attivo quello del record di prima.
    setPathViewModes(ModeTangential, ModeTangential);
    setPathSpeed3D(10);
    setPathSpeed4D(10);
    m_scene.lastCameraMotion.clear();
    if (ui->glWidget && ui->glWidget->getEngine())
        ui->glWidget->getEngine()->clearPathEquations();
}

void MainWindow::onToggleViewClicked()  // path 4D (pushView)
{
    setPathViewModes(m_scene.pathViewMode4D == ModeTangential ? ModeCentered : ModeTangential,
                     m_scene.pathViewMode3D);
}

void MainWindow::onToggleView3DClicked()  // path 3D (pushView3D)
{
    setPathViewModes(m_scene.pathViewMode4D,
                     m_scene.pathViewMode3D == ModeTangential ? ModeCentered : ModeTangential);
}

void MainWindow::refreshRotationSpeedLabels()
{
    if (!ui->glWidget) return;
    // Un decimale, come il passo dei tasti +/- (0.1); lo zero senza segno.
    auto show = [](QLabel *label, float v) {
        v = std::round(v * 10.0f) / 10.0f;
        if (std::abs(v) < 0.01f) v = 0.0f;
        label->setText(QString::number(v, 'f', 1));
    };
    show(ui->lblNutVal,   ui->glWidget->getNutationSpeed());
    show(ui->lblPrecVal,  ui->glWidget->getPrecessionSpeed());
    show(ui->lblSpinVal,  ui->glWidget->getSpinSpeed());
    show(ui->lblOmegaVal, ui->glWidget->getOmegaSpeed());
    show(ui->lblPhiVal,   ui->glWidget->getPhiSpeed());
    show(ui->lblPsiVal,   ui->glWidget->getPsiSpeed());
}

void MainWindow::setupSpeedControl(QPushButton* btnPlus, QPushButton* btnMinus,
                                   std::function<float()> getter, std::function<void(float)> setter) {

    // 1. Pulizia totale delle connessioni per evitare comandi fantasma
    disconnect(btnPlus, &QPushButton::clicked, nullptr, nullptr);
    disconnect(btnMinus, &QPushButton::clicked, nullptr, nullptr);

    auto changeVal = [this, getter, setter](int direction) {
        // Usiamo un passo di 0.1
        float step = 0.1f;

        // 2. Il valore corrente e' quello del MOTORE. Prima si rileggeva
        // l'etichetta: due copie della stessa velocita', e bastava che una
        // scrittura programmatica toccasse una sola delle due perche' il tasto
        // ripartisse da un valore che il motore non aveva.
        float currentVal = getter();

        // 3. Calcolo del nuovo valore
        // Moltiplichiamo la direzione (1 o -1) per lo step
        float newVal = currentVal + (static_cast<float>(direction) * step);

        // 4. ARROTONDAMENTO CRITICO:
        // Arrotondiamo a 1 decimale PRIMA di ogni altra operazione
        newVal = std::round(newVal * 10.0f) / 10.0f;

        // 5. Gestione dello zero assoluto (Zero-Snap)
        // Se siamo molto vicini allo zero, forziamolo a 0.0 per resettare il segno
        if (std::abs(newVal) < 0.01f) {
            newVal = 0.0f;
        }

        // 6. Invio al motore, poi l'etichetta dal motore
        setter(newVal);
        refreshRotationSpeedLabels();

        // 8. Se tutto è fermo, riporta il tasto a START
        if (ui->glWidget) {
            bool anyMotion = std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
                    std::abs(ui->glWidget->getPsiSpeed()) > 0.001f;

            if (!anyMotion) {
                    // Se si azzera tutto, fermiamo l'animazione per sicurezza
                    ui->glWidget->pauseMotion();
                    if (ui->btnStart_2) ui->btnStart_2->setText("GO");
                } else {
                    // Aggiorniamo il tasto solo in base allo stato REALE dell'animazione.
                    if (ui->btnStart_2) {
                        ui->btnStart_2->setText(ui->glWidget->isAnimating() ? "STOP" : "GO");
                    }
                }

                updateMasterButtonState();
                ui->glWidget->update();
        }
    };

    // Usiamo il contesto 'this' per garantire che la connessione sia stabile
    connect(btnPlus, &QPushButton::clicked, this, [changeVal](){ changeVal(1); });
    connect(btnMinus, &QPushButton::clicked, this, [changeVal](){ changeVal(-1); });
}

void MainWindow::updateProjectionButtonText()
{
    int mode = (int)ui->glWidget->projectionMode;
    QString txt;

    if (mode == 0) {
        txt = "Orthogonal";
    } else if (mode == 1) {
        txt = "Perspective";
    } else if (mode == 2) {
        txt = "Stereographic";
    }

    // Aggiorna il NUOVO tasto sulla status bar
    if (m_btnProjection) { m_btnProjection->setText(txt); }

    // Chiamata a ogni cambio di proiezione (toggle, load preset/record, init):
    // in Ortho gli slider FOV si spengono.
}


void MainWindow::applyCameraFov(float deg)
{
    const float v = qBound(20.0f, deg, 110.0f);

    m_scene.fov = v;

    if (ui->lblValFov)
        ui->lblValFov->setText(QString::number(qRound(v)) + QString::fromUtf8("°"));

    if (ui->fovSliderMain) {
        bool old = ui->fovSliderMain->blockSignals(true);
        ui->fovSliderMain->setValue(qRound(v));
        ui->fovSliderMain->blockSignals(old);
    }

    if (ui->glWidget)
        ui->glWidget->setCameraFov(v);
}

void MainWindow::toggleProjection()
{
    // SCENA VUOTA DI NEW: la proiezione non ha niente da mostrare (lo sfondo
    // non ne dipende: il suo campo visivo e' virtuale). Prima cambiava lo
    // stesso, e al preset successivo la scena risultava da salvare.
    if (isWholeSceneEmpty()) {
        InputValidator::showEmptySceneNotice(this, "Nothing to project");
        return;
    }

    // Leggi modo attuale forzandolo a intero (0=Ortho, 1=Persp, 2=Wide)
    int current = (int)ui->glWidget->projectionMode;

    // Calcola il prossimo (aggiunge 1 e torna a 0 quando arriva a 3)
    int nextMode = (current + 1) % 3;

    // Applica
    ui->glWidget->setProjectionMode(nextMode);

    // Aggiorna il testo e forza il repaint
    updateProjectionButtonText();
    ui->glWidget->update();
}
