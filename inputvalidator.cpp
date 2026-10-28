#include "inputvalidator.h"
#include <QRegularExpression>
#include <QMessageBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSet>

static bool geodesicWarningDisabledForCurrentLoad = false;
bool InputValidator::s_boxActive = false;
quint64 InputValidator::s_errorCount = 0;

void InputValidator::notifyEmptyImplicitEquation(QWidget* parent, bool crossSection, bool sceneEmpty)
{
    QString text = crossSection ? "Type an equation in x, y, z and p before running."
                                : "Type an equation in x, y and z before running.";
    if (!sceneEmpty)
        text += "\n\nThe surface on screen is the previous one: use NEW to clear the scene.";
    notify(parent, QMessageBox::Warning, "No Equation", text);
}

void InputValidator::showEmptySceneNotice(QWidget* parent, const QString& nothingTo)
{
    notify(parent, QMessageBox::Information, "Empty Scene",
           nothingTo + ": the scene is empty.\n\n"
           "Load a preset from the Library, or type an equation and press Run.");
}

bool InputValidator::validateImplicitEquation(QWidget* parent, const QString& rawEq, bool allowP)
{
    if (rawEq.count("=") > 1) {
        notify (parent, QMessageBox::Warning, "Syntax Error", "Use only one '=' sign (e.g., x^2 + y^2 = 1)");
        return false;
    }

    // Costruiamo una stringa di test per controllare le variabili vietate
    QString testEq = rawEq;
    if (!testEq.contains("=")) {
        testEq += " = 0.0"; // Simuliamo la correzione automatica per il controllo
    }

    const QString forbidden = allowP ? "\\bu\\b|\\bv\\b|\\bw\\b" : "\\bu\\b|\\bv\\b|\\bw\\b|\\bp\\b";
    if (testEq.contains(QRegularExpression(forbidden))) {
        notify(parent, QMessageBox::Critical, "Invalid Variables",
              allowP ? "Use only x, y, z, and p in Ray Marching Cross Section."
                     : "Use only x, y, and z in Ray Marching.");
        return false;
    }

    // ALMENO DUE fra x, y, z. Un campo che ne usa meno non definisce una
    // superficie nello spazio: "1 = 0" non ha soluzioni (schermo vuoto, nessun
    // errore), e anche "x = 0" o "x^2 = 1" descrivono piani infiniti che il
    // marcher mostra come fondali piatti dentro il box dei limiti -- tutti casi
    // che l'utente scambia per un preset rotto invece che per un'equazione
    // degenere.
    // La soglia e' DUE, non tre: superfici legittime di rivoluzione o estruse
    // (p.es. il cilindro x^2 + y^2 = 1) usano due sole variabili e devono
    // passare.
    // Si conta sul testo SENZA commenti: un "// x y z" in coda non deve valere
    // come uso delle variabili. La 'p' del Cross Section non entra nel conto --
    // e' la coordinata della sezione, non una delle tre dello spazio: con la sola
    // p l'equazione non definisce comunque una superficie visibile.
    QString mathOnly = rawEq;
    mathOnly.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
    mathOnly.remove(QRegularExpression("//[^\n]*"));
    int spatialVars = 0;
    for (const char *v : { "x", "y", "z" }) {
        if (mathOnly.contains(QRegularExpression(QString("\\b%1\\b").arg(v))))
            ++spatialVars;
    }
    if (spatialVars < 2) {
        notify(parent, QMessageBox::Critical, "Not a Surface",
               allowP ? "The equation must use at least two of x, y and z.\n\n"
                        "With fewer it does not define a surface to intersect: "
                        "the section comes out empty or flat."
                      : "The equation must use at least two of x, y and z.\n\n"
                        "With fewer it does not define a surface to intersect: "
                        "nothing appears, or you get an infinite plane.");
        return false;
    }

    return true;
}

bool InputValidator::validateImplicitScriptContext(QWidget* parent, const QString& texCode)
{
    QString cleanTexCode = texCode;
    // Rimuoviamo i commenti dalla stringa solo per il check
    cleanTexCode.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
    cleanTexCode.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));

    bool isParametricTexture = cleanTexCode.contains(QRegularExpression(R"(\breturn\b)")) || cleanTexCode.contains("fragColor");

    if (isParametricTexture) {
        notify(parent, QMessageBox::Warning, "Wrong Panel",
                             "You entered Parametric texture code in the Ray Marching tab.\n\n"
                             "Parametric procedural textures must be written using the 'Script' side panel.\n"
                             "Only Ray Marching compatible code (e.g., calculating and assigning 'textureCol') is allowed in this tab, and it must not contain 'return' statements.");
        return false;
    }
    return true;
}

bool InputValidator::validateParametricVariables(QWidget* parent, const QString& mainEqs, const QString& allEqsToTest, bool hasConstraint)
{
    QRegularExpression singleLetterRegex("\\b[A-Za-z]\\b");
    QRegularExpressionMatchIterator it = singleLetterRegex.globalMatch(allEqsToTest);

    QStringList allowedSingleLetters = {
        "u", "v", "w", "U", "V", "W", "t", "T",
        "a", "b", "c", "d", "e", "f", "s",
        "A", "B", "C", "D", "E", "F",
        "x", "y", "z", "P", "p"
    };

    QString invalidLetter = "";
    while (it.hasNext()) {
        QString letter = it.next().captured(0);
        if (!allowedSingleLetters.contains(letter)) {
            invalidLetter = letter;
            break;
        }
    }

    if (!invalidLetter.isEmpty()) {
        notify(parent, QMessageBox::Critical, "Unrecognized Variable",
                              QString("You typed the letter '%1', which is not a recognized variable.\n\n"
                                      "Allowed variables: u, v, w (spatial), U, V, W (composite), "
                                      "t (time), or A, B, C, D, E, F, s (parameters).").arg(invalidLetter));
        return false;
    }

    bool has_u = mainEqs.contains(QRegularExpression("\\bu\\b"));
    bool has_v = mainEqs.contains(QRegularExpression("\\bv\\b"));
    bool has_w = mainEqs.contains(QRegularExpression("\\bw\\b"));
    int lowerCount = (has_u ? 1 : 0) + (has_v ? 1 : 0) + (has_w ? 1 : 0);

    bool has_U = mainEqs.contains(QRegularExpression("\\bU\\b"));
    bool has_V = mainEqs.contains(QRegularExpression("\\bV\\b"));
    bool has_W = mainEqs.contains(QRegularExpression("\\bW\\b"));
    int upperCount = (has_U ? 1 : 0) + (has_V ? 1 : 0) + (has_W ? 1 : 0);

    if (lowerCount > 0 && upperCount > 0) {
        notify(parent, QMessageBox::Critical, "Syntax Error",
                              "You cannot mix lowercase (u, v, w) and uppercase (U, V, W) spatial variables in the main equations!\n"
                              "Please choose only one coordinate system.");
        return false;
    }

    int totalVars = lowerCount + upperCount;
    if (totalVars != 2 && totalVars != 3) {
        notify(parent, QMessageBox::Critical, "Spatial Dimension Error",
                              "You must use exactly 2 or 3 valid spatial variables.\n"
                              "For example: 'u, v' (2D) or 'U, V, W' (3D composite).\n"
                              "You have entered an invalid number of variables or unrecognized characters.");
        return false;
    }

    if (totalVars == 3 && !hasConstraint && upperCount == 0) {
        notify(parent, QMessageBox::Warning, "Missing Constraint Equation",
                             "You have used parameters 'u', 'v', and 'w' in your equations, "
                             "but no explicit constraint has been defined.\n\n"
                             "Please provide a constraint in the Constraints tab (e.g., w = h(u,v)) "
                             "to compute the surface correctly.");
        return false;
    }

    return true;
}

bool InputValidator::validateWUsage(QWidget* parent, const QString& allEqs, bool hasExplicit)
{
    bool hasU = allEqs.contains(QRegularExpression("\\bu\\b"));
    bool hasV = allEqs.contains(QRegularExpression("\\bv\\b"));
    bool hasW = allEqs.contains(QRegularExpression("\\bw\\b"));

    if (hasW && (!hasU || !hasV) && !hasExplicit) {
        notify(parent, QMessageBox::Warning, "Parameter Error",
                             "You used the variable 'w' instead of 'u' or 'v' without setting a 3D/4D constraint.\n\n"
                             "In standard 2D surfaces, the two base parametric variables must be 'u' and 'v'.\n"
                             "Please replace 'w' with the missing variable in your equations, or add a constraint in the Constraints panel to draw the figure.");
        return false;
    }
    return true;
}

bool InputValidator::validateLimits(QWidget* parent,
                                    float uMin, float uMax, bool uActive,
                                    float vMin, float vMax, bool vActive,
                                    float wMin, float wMax, bool wActive)
{
    QStringList errors;

    // Controlla il range SOLO se la rispettiva casella è abilitata/attiva
    if (uActive && uMin >= uMax) errors << "u";
    if (vActive && vMin >= vMax) errors << "v";
    if (wActive && wMin >= wMax) errors << "w";

    if (!errors.isEmpty()) {
        QString vars = errors.join(", ");
        notify(parent, QMessageBox::Critical, "Invalid Limits",
                              QString("Minimum values must be strictly less than Maximum values!\nCheck limits for: %1").arg(vars));
        return false;
    }

    return true;
}

bool InputValidator::validateCompositionFields(QWidget* parent, const QString& mainEqs, const QString& cU, const QString& cV, const QString& cW, bool geoHasText, const QString& composedTest)
{
    // Ricalcoliamo la presenza delle variabili per rendere la funzione indipendente
    bool has_U = mainEqs.contains(QRegularExpression("\\bU\\b"));
    bool has_V = mainEqs.contains(QRegularExpression("\\bV\\b"));
    bool has_W = mainEqs.contains(QRegularExpression("\\bW\\b"));
    int upperCount = (has_U ? 1 : 0) + (has_V ? 1 : 0) + (has_W ? 1 : 0);

    bool isCompositionActive = ((upperCount > 0) || !cU.isEmpty() || !cV.isEmpty() || !cW.isEmpty()) && !geoHasText;

    if (isCompositionActive) {
        if ((has_U && cU.isEmpty()) || (has_V && cV.isEmpty()) || (has_W && cW.isEmpty())) {
            notify(parent, QMessageBox::Warning, "Missing Composition Fields",
                                 "You have activated the Composition or Geodesic Flow mode, but one of the used fields (U, V, or W) is empty.\n\n"
                                 "Please explicitly fill the corresponding composition fields.");
            return false;
        }

        bool finalU = composedTest.contains(QRegularExpression("\\bu\\b"));
        bool finalV = composedTest.contains(QRegularExpression("\\bv\\b"));

        if (!finalU || !finalV) {
        notify(parent, QMessageBox::Critical, "Dimensional Collapse",
                                  "To draw a valid surface, the final composed equations must depend on both 'u' and 'v'.\n\n"
                                  "Currently, one of the two parameters is missing from the Composition panel, "
                                  "which would collapse the 2D surface into a 1D line.\n\n"
                                  "Please ensure both 'u' and 'v' are used.");
            return false;
        }
    }
    return true;
}

void InputValidator::resetGeodesicWarning()
{
    geodesicWarningDisabledForCurrentLoad = false;
}

bool InputValidator::validateGeodesicConformalFactor(QWidget* parent, const QString& lineX, const QString& lineY, const QString& lineZ, const QString& lineP, const QString& lambdaStr, bool showWarning)
{
    // Se l'avviso è stato disattivato per QUESTO caricamento, o showWarning è false, usciamo subito
    if (geodesicWarningDisabledForCurrentLoad || !showWarning) return true;

    // Regex strettamente limitata ai parametri dello spazio 3D (Maiuscole)
    QRegularExpression spatialVars("\\b[UVW]\\b");

    // Il fattore conforme è una costante spaziale (piatto) se NON contiene U, V o W
    bool isLambdaFlat = !lambdaStr.contains(spatialVars);

    // Stessa logica per le coordinate spaziali
    auto isConstantCoord = [&](const QString& eq) {
        QString trimmed = eq.trimmed();
        if (trimmed.isEmpty() || trimmed == "0" || trimmed == "0.0") return true;
        return !trimmed.contains(spatialVars);
    };

    bool hasConstant = isConstantCoord(lineX) || isConstantCoord(lineY) || isConstantCoord(lineZ) || isConstantCoord(lineP);

    if (hasConstant && isLambdaFlat) {
        const QString msg =
            "One or more of your spatial coordinates are constant, and the Conformal Factor is set to a constant value.\n\n"
            "This means the induced metric remains perfectly flat (a scaled Euclidean geometry), so the geodesic flow will just produce straight lines.\n\n"
            "If you want to simulate curved geometries (like the Poincaré disk or ball) using flat coordinates, please modify the Conformal Factor to be a spatially varying expression.\n\n"
            "IMPORTANT: The Conformal Factor must ALWAYS evaluate to a strictly positive value (> 0). A value of zero or negative will cause mathematical singularities and break the calculation.\n\n"
            "Do you want to disable this warning for the current preset/session?";

#if defined(Q_OS_ANDROID)
        if (!s_boxActive) {
            s_boxActive = true;
            auto* box = new QMessageBox(QMessageBox::Information, "Euclidean Geometry Warning",
                                        msg, QMessageBox::Yes | QMessageBox::No, nullptr);
            box->setDefaultButton(QMessageBox::No);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(true);
            QObject::connect(box, &QMessageBox::finished, box, [](int r){
                if (r == QMessageBox::Yes) geodesicWarningDisabledForCurrentLoad = true;
                s_boxActive = false;
            });
            box->open();   // non bloccante: niente event loop annidato
        }
#else
        QMessageBox::StandardButton reply = QMessageBox::information(
            parent, "Euclidean Geometry Warning", msg,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            geodesicWarningDisabledForCurrentLoad = true;
        }
#endif
    }
    return true;
}

void InputValidator::showGeodesicSingularityError(QWidget* parent)
{
    notify(parent, QMessageBox::Critical, "Mathematical Collapse (Geodesic Flow)",
                          "The Geodesic Flow generated impossible or infinite values and has been blocked to prevent graphical artifacts.\n\n"
                          "This usually happens when:\n"
                          "• The curve escapes the valid domain (e.g., hitting the boundary of the Poincaré ball).\n"
                          "• The Conformal Factor (\u03BB) evaluates to zero or a negative number.\n"
                          "• The integration step is too large, causing the calculation to overshoot.\n\n"
                          "Please adjust your starting parameters or the conformal equation.");
}

void InputValidator::showEquationSyntaxError(QWidget* parent)
{
    notify(parent, QMessageBox::Critical, "Equation Error",
                          "Syntax error in the parametric equations or constraints.\n\n"
                          "Please check your math, brackets, and try again.");
}

void InputValidator::showMathematicalCollapseError(QWidget* parent)
{
    notify(parent, QMessageBox::Critical, "Mathematical Collapse",
                          "The entered limits or equations generate impossible or infinite values (e.g., division by zero).\n\n"
                          "Execution has been blocked to prevent graphical artifacts or a blank screen.");
}

void InputValidator::showAnimatedGeodesicSingularityError(QWidget* parent)
{
    notify(parent, QMessageBox::Critical, "Mathematical Singularity",
                          "The animated Geodesic Flow encountered infinite values and was stopped.\n\n"
                          "The curve likely escaped the valid domain of the Conformal Factor over time.");
}

void InputValidator::showInvalidConformalConstantError(QWidget* parent) {
    notify(parent, QMessageBox::Critical, "Conformal Factor Error",
                          "The conformal factor (\u03BB) cannot assume negative values or be equal to zero.\n\n"
                          "Please enter a strictly positive value or expression.");
}

void InputValidator::showImageNotFoundError(QWidget* parent, const QString& imgPath)
{
    notify(parent, QMessageBox::Warning, "Image Not Found",
                         "Warning: the original image linked to this preset was not found:\n\n" +
                             imgPath + "\n\nIt might have been moved or renamed. It will be ignored to prevent graphical errors.");
}

void InputValidator::showShaderCompilationError(QWidget* parent, const QString& title, const QString& errorLog)
{
    notify(parent, QMessageBox::Critical, title,
                          "A compilation error was found:\n\n" + errorLog);
}

void InputValidator::showInvalidConstantError(QWidget* parent, const QString& name, const QString& text)
{
    notify(parent, QMessageBox::Critical, "Invalid Constant",
                          QString("The constant '%1' is not a valid number or expression:\n\n"
                                  "    \"%2\"\n\n"
                                  "Please enter a number (e.g. 2, 0.5) or a valid expression "
                                  "(e.g. pi/2, A+1).\nAllowed parameters: A, B, C, D, E, F, S.")
                              .arg(name, text.trimmed()));
}

void InputValidator::showInvalidThicknessError(QWidget* parent, const QString& text)
{
    const QString what = text.trimmed().isEmpty()
        ? QString("The tube thickness is empty.")
        : QString("The tube thickness is not a valid number:\n\n    \"%1\"").arg(text.trimmed());
    notify(parent, QMessageBox::Critical, "Invalid Thickness",
           what + "\n\nPlease enter a number (e.g. 1, 0.5).");
}

void InputValidator::showInvalidMeshCountError(QWidget* parent, const QString& text)
{
    notify(parent, QMessageBox::Critical, "Invalid Number of Meshes",
           QString("The number of meshes is not valid:\n\n    \"%1\"\n\n"
                   "Please enter a whole number (e.g. 3) or an expression with the "
                   "constants (e.g. A*A).").arg(text.trimmed()));
}

void InputValidator::showInvalidStepsError(QWidget* parent, const QString& text)
{
    notify(parent, QMessageBox::Critical, "Invalid Steps",
                          QString("The 'Steps' value is not a valid integer:\n\n    \"%1\"\n\n"
                                  "Please enter a whole number (e.g. 100).").arg(text.trimmed()));
}

void InputValidator::showInvalidLimitError(QWidget* parent, const QString& name, const QString& text,
                                           bool emptyMeansNoLimit)
{
    const QString trimmed = text.trimmed();

    // Campo VUOTO nel parametrico: non e' un "numero non valido" ma un dominio
    // non dichiarato. Mostrare la stringa vuota fra virgolette ("") e suggerire
    // di lasciarla vuota era la coppia peggiore: descriveva come errore di
    // sintassi cio' che e' un dato mancante, e consigliava di rifare l'errore.
    if (trimmed.isEmpty() && !emptyMeansNoLimit) {
        notify(parent, QMessageBox::Critical, "Missing Limit",
                              QString("The %1 limit is empty.\n\n"
                                      "Parametric surfaces need an explicit range: enter a number "
                                      "(e.g. 0, 6.28318) or an expression using the constants.")
                                  .arg(name));
        return;
    }

    const QString tail = emptyMeansNoLimit
                       ? QStringLiteral("Please enter a number (e.g. -10, 2.5) or leave it empty for no limit.")
                       : QStringLiteral("Please enter a number (e.g. 0, 6.28318) or an expression using the constants.");

    notify(parent, QMessageBox::Critical, "Invalid Limit",
                          QString("The %1 limit is not a valid number:\n\n    \"%2\"\n\n%3")
                              .arg(name, trimmed, tail));
}

void InputValidator::showLimitOrderError(QWidget* parent, QChar axis)
{
    notify(parent, QMessageBox::Critical, "Invalid Limits",
                          QString("The %1 minimum must be strictly less than the %1 maximum.\n\n"
                                  "Check the %1 min and %1 max fields.").arg(axis));
}

void InputValidator::showIncompleteEquationsError(QWidget* parent)
{
    notify(parent, QMessageBox::Warning, "Incomplete Equations",
                          "A parametric surface needs at least three of the four "
                          "equation fields (X, Y, Z, P).\n\n"
                          "Fill in the missing ones, then press Run. Leaving P empty "
                          "is fine for purely 3D surfaces.");
}

bool InputValidator::validateParametricScriptContext(QWidget* parent, const QString& texCode)
{
    QString cleanCode = texCode;
    cleanCode.remove(QRegularExpression(R"(//.*$)", QRegularExpression::MultilineOption));
    cleanCode.remove(QRegularExpression(R"(/\*.*?\*/)", QRegularExpression::DotMatchesEverythingOption));

    // 0. CONTROLLO DNA TEXTURE: Previene codice Ray Marching nel Dock Script
    bool isImplicitTexture = cleanCode.contains("textureCol") || cleanCode.contains("pModel") || cleanCode.contains("n_model");
    if (isImplicitTexture) {
        notify(parent, QMessageBox::Warning, "Wrong Panel",
                             "You entered Ray Marching (Implicit) texture code in the Script Dock.\n\n"
                             "The Script Dock is strictly dedicated to procedural textures for Parametric surfaces.\n"
                             "Please move this code to the 'Texture' field inside the 'Ray Marching' tab.");
        return false;
    }
    return true;
}

bool InputValidator::validateScriptModeContext(QWidget* parent, int currentScriptMode, const QString& currentText)
{
    // Identifichiamo il "dna" del codice tramite le parole chiave tipiche
    bool isTextureCode = currentText.contains("mainImage", Qt::CaseInsensitive) ||
                         currentText.contains("fragColor", Qt::CaseInsensitive) ||
                         currentText.contains("//IMG:", Qt::CaseInsensitive);

    bool isSoundCode   = currentText.contains("mainSound", Qt::CaseInsensitive) ||
                       currentText.contains("//SOUND_BEGIN", Qt::CaseInsensitive) ||
                       currentText.contains("//MUSIC:", Qt::CaseInsensitive);

    bool isSurfaceCode = currentText.contains("u_min", Qt::CaseInsensitive) ||
                         currentText.contains("v_min", Qt::CaseInsensitive) ||
                         currentText.contains("w_min", Qt::CaseInsensitive) ||
                         currentText.contains("steps:=", Qt::CaseInsensitive);

    if (currentScriptMode == 0) { // ScriptModeSurface
        if (isTextureCode || isSoundCode) {
            notify(parent, QMessageBox::Critical, "Wrong Script Mode",
                                  "You are trying to run Texture or Audio code while in the 'Surface' tab.\n\n"
                                  "Please select the correct mode using the 'Script Mode' button before running this code.");
            return false;
        }
    } else if (currentScriptMode == 1) { // ScriptModeTexture
        if (isSoundCode || isSurfaceCode) {
            notify(parent, QMessageBox::Critical, "Wrong Script Mode",
                                  "You are trying to run Audio or Surface code while in the 'Texture' tab.\n\n"
                                  "Please select the correct mode before running this code.");
            return false;
        }
    } else if (currentScriptMode == 2) { // ScriptModeSound
        if (isTextureCode || isSurfaceCode) {
            notify(parent, QMessageBox::Critical, "Wrong Script Mode",
                                  "You are trying to run Texture or Surface code while in the 'Sound' tab.\n\n"
                                  "Please select the correct mode before running this code.");
            return false;
        }
    }
    return true;
}

bool InputValidator::validateParametricScriptReturn(QWidget* parent, const QString& cleanCode)
{
    // 1. Controllo rigido: deve esserci un return di un vettore 3D o 4D esplicito
    // Questo impedisce crash nel GlslTranslator causati da sintassi "aliena"
    QRegularExpression returnRegex(R"(\breturn\s+vec[34]\s*\()");
    if (!returnRegex.match(cleanCode).hasMatch()) {
        notify(parent, QMessageBox::Critical, "Script Error",
                              "Invalid script format.\nParametric scripts must explicitly return a 3D or 4D vector.\n\n"
                              "Example:\nreturn vec4(u, v, sin(u*v), 1.0);");
        return false;
    }

    // 2. Prevenzione Crash del Parser: controllo bilanciamento parentesi
    int openP = cleanCode.count('(');
    int closeP = cleanCode.count(')');
    if (openP != closeP) {
        notify(parent, QMessageBox::Critical, "Syntax Error",
                              "Unmatched parentheses '()' detected in the script.\nPlease check your syntax before running.");
        return false;
    }

    return true;
}

void InputValidator::showMetricMissingConditionsInfo(QWidget* parent)
{
    notify(parent, QMessageBox::Information, "Metric Script",
           "Metric tensor script loaded.\n\n"
           "To start the geodesic flow, set the initial conditions in the "
           "Equations dock (U, V, W and dU, dV, dW as functions of u), "
           "then run the script again.\n\n"
           "Tip: you can declare them in the script itself, e.g.\n"
           "B := 0.1;\n"
           "U := 4; V := pi/2; W := 0;\n"
           "dU := -1; dV := B*cos(u); dW := B*sin(u);");
}

void InputValidator::showMetricAmbiguousConstantWarning(QWidget* parent, const QStringList& names)
{
    const QString list = names.join(", ");
    const QString plural = names.size() > 1 ? "s" : "";
    notify(parent, QMessageBox::Warning, "Metric Script",
           QString("The constant%1 %2 appear%3 both in the metric tensor and in the "
                   "initial conditions.\n\n"
                   "Each constant A..F is a single global slider: moving %2 will change "
                   "the metric and the initial conditions together, which is usually not "
                   "what you want (e.g. the black-hole mass and the beam aperture at once).\n\n"
                   "Use a different free constant for the initial conditions "
                   "(B..F are unused if the metric only uses A).")
           .arg(plural, list, names.size() > 1 ? "" : "s"));
}

bool InputValidator::showTextureConstantClashWarning(QWidget* parent, const QStringList& names,
                                                     const QStringList& freeLetters)
{
    if (names.isEmpty()) return true;

    const QString list   = names.join(", ");
    const bool    many   = names.size() > 1;
    const QString isAre  = many ? "are" : "is";
    const QString sPl    = many ? "s"   : "";
    const QString them   = many ? "them" : "it";

    QString free = freeLetters.isEmpty()
            ? QStringLiteral("none are free: every constant is already in use")
            : freeLetters.join(", ") + QStringLiteral(" are still free");

    // NON e' un errore: unire una superficie e una texture che si contendono la
    // stessa lettera puo' essere voluto (un solo slider che muove entrambe).
    // Per questo si CHIEDE invece di rifiutare, e il pulsante di default e'
    // quello che applica: chi non legge ottiene il comportamento di prima.
    // Il testo dice quali lettere sono libere, cosi' la via d'uscita e' concreta
    // e non un consiglio generico.
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Shared Constant");
    box.setText(QString("The constant%1 %2 %3 already in use elsewhere in this scene.")
                .arg(sPl, list, isAre));
    box.setInformativeText(
        QString("Each constant A-F is a single global slider shared by the surface, its "
                "texture and the background, so moving %1 will change two of them at once.\n\n"
                "Apply anyway if that is what you want. Otherwise edit the script to use a "
                "free letter (%2), then load it again.")
        .arg(them, free));

    QPushButton* apply  = box.addButton("Apply anyway", QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(apply);
    Q_UNUSED(cancel);

    // Conta come avviso mostrato, come ogni altro popup di questa classe.
    ++s_errorCount;
    box.exec();
    return box.clickedButton() == apply;
}

bool InputValidator::showSharedConstantsNotice(QWidget* parent, const QStringList& lines,
                                               const QStringList& freeLetters)
{
    if (lines.isEmpty()) return false;

    const QString free = freeLetters.isEmpty()
            ? QStringLiteral("none are free: every constant is already in use")
            : freeLetters.join(", ") + QStringLiteral(" are still free");

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Shared Constant");
    box.setText(lines.join("\n"));
    box.setInformativeText(
        QString("Each constant A-F is a single global slider, so one slider moves "
                "both at once.\n\nThis may be intended. Otherwise edit one of the "
                "scripts to use a free letter (%1).").arg(free));
    QCheckBox* dontShow = new QCheckBox("Don't show again for this record", &box);
    box.setCheckBox(dontShow);
    box.addButton(QMessageBox::Ok);

    // NON conta in s_errorCount: e' un'informazione su un file caricato, non un
    // errore dell'utente, e RunOutcomeGuard leggerebbe quel contatore come
    // "Run fallito".
    box.exec();
    return dontShow->isChecked();
}

bool InputValidator::validateImplicitScriptReturn(QWidget* parent, const QString& cleanCode)
{
    // 1. Controllo presenza return o fragColor
    if (!cleanCode.contains(QRegularExpression(R"(\breturn\b)")) && !cleanCode.contains("fragColor")) {
        notify(parent, QMessageBox::Critical, "Script Error",
                              "Invalid script.\nImplicit scripts must return a value (e.g., 'return length(p) - 1.0;') or assign to 'fragColor'.");
        return false;
    }

    // 2. Prevenzione Crash del Parser: controllo bilanciamento parentesi
    int openP = cleanCode.count('(');
    int closeP = cleanCode.count(')');
    if (openP != closeP) {
        notify(parent, QMessageBox::Critical, "Syntax Error",
                              "Unmatched parentheses '()' detected in the script.\nPlease check your syntax before running.");
        return false;
    }

    return true;
}

bool InputValidator::validateParentheses(QWidget* parent, const QString& cleanCode) {
    if (cleanCode.count('(') != cleanCode.count(')')) {
        notify(parent, QMessageBox::Critical, "Syntax Error", "Unmatched parentheses '()' detected.");
        return false;
    }
    return true;
}

bool InputValidator::validateExpressionSyntax(QWidget* parent, const QString& expr, const QString& fieldName, bool* outWarned)
{
    if (expr.trimmed().isEmpty()) return true; // vuoto = valido

    // 1. Parentesi tonde
    if (expr.count('(') != expr.count(')')) {
        notify(parent, QMessageBox::Critical, "Syntax Error",
                              QString("Unmatched parentheses in field '%1'.").arg(fieldName));
        return false;
    }
    // 2. Parentesi quadre e graffe (per future estensioni)
    if (expr.count('[') != expr.count(']') ||
        expr.count('{') != expr.count('}')) {
        notify(parent, QMessageBox::Critical, "Syntax Error",
                              QString("Unmatched brackets in field '%1'.").arg(fieldName));
        return false;
    }
    // 3. Operatori binari "stranded" alla fine (es. "u+", "v*", "1/")
    QString trimmed = expr.trimmed();
    if (!trimmed.isEmpty()) {
        QChar last = trimmed.back();
        if (QString("+-*/^").contains(last)) {
            notify(parent, QMessageBox::Critical, "Syntax Error",
                                  QString("Field '%1' ends with a dangling operator '%2'.")
                                      .arg(fieldName).arg(last));
            return false;
        }
    }
    // 4. Doppi operatori consecutivi tipici di errore di battitura
    if (expr.contains(QRegularExpression(R"([\+\-\*/\^]{2,})"))
        // Permetto '--' e '+-' come negazioni? Più sicuro vietarli e chiedere parentesi.
        ) {
        notify(parent, QMessageBox::Warning, "Possible Syntax Error",
                             QString("Field '%1' contains consecutive operators. Use parentheses "
                                     "to clarify (e.g. 'a*(-b)' instead of 'a*-b').").arg(fieldName));
        // warning non blocca: solo se vuoi rigore, return false. Segnaliamo pero'
        // al chiamante che un warning e' stato mostrato, cosi' non aggiunge un
        // secondo popup (es. errore di compilazione) per lo stesso input.
        if (outWarned) *outWarned = true;
    }
    // 5. Virgola decimale "sospetta" non separata da operatori
    // (opzionale; molti utenti scrivono "1,5" pensando italiano)
    if (expr.contains(QRegularExpression(R"(\d,\d)"))) {
        notify(parent, QMessageBox::Warning, "Decimal Format",
                             QString("Field '%1' seems to use a comma as decimal separator. "
                                     "Use a dot (e.g. '1.5').").arg(fieldName));
        if (outWarned) *outWarned = true;
    }
    return true;
}

bool InputValidator::validateAndParseLimits(QWidget* parent, const QVector<LimitField>& fields, ParseFn parse, QVector<float>& outValues)
{
    outValues.clear();
    outValues.reserve(fields.size());

    bool parseOk = true;
    // Campo attivo ma VUOTO: va segnalato a parte. parseLimitField lo accetta
    // (stringa vuota -> ok=true, valore 0.0), quindi arriverebbe a
    // validateLimits come 0 e finirebbe nel ramo "min >= max" -- l'utente si
    // vedeva un errore sul confronto dei limiti mentre il problema e' che non
    // li ha ancora scritti. Sono due situazioni diverse e meritano due messaggi.
    bool anyEmpty = false;
    for (const auto& f : fields) {
        if (!f.active) {
            outValues.append(0.0f);  // valore segnaposto, non verrà usato
            continue;
        }
        if (f.edit->text().trimmed().isEmpty()) anyEmpty = true;
        bool ok = false;
        float v = parse(f.edit->text(), &ok);
        outValues.append(v);
        if (!ok) parseOk = false;
    }

    if (anyEmpty) {
        notify(parent, QMessageBox::Critical, "Missing Limits",
                              "The parameter range is not set: one or more min/max fields are empty.\n\n"
                              "Fill in the limits of the parameters used by your equations "
                              "(for example u from 0 to 6.28318) and run again.");
        // NB: nel testo le variabili vanno MINUSCOLE (u, v, w): le maiuscole
        // U/V/W sono le variabili di composizione, un'altra cosa.
        return false;
    }

    if (!parseOk) {
        notify(parent, QMessageBox::Critical, "Invalid Limit",
                              "One of the active min/max fields could not be parsed as a number "
                              "or valid expression. Please check for typos.");
        return false;
    }
    return true;
}

bool InputValidator::validateFieldList(QWidget* parent, const QVector<NamedField>& fields, bool* outWarned)
{
    for (const auto& f : fields) {
        if (!validateExpressionSyntax(parent, f.text, f.name, outWarned))
            return false;
        if (!validateIdentifiers(parent, f.text, f.name))
            return false;
    }
    return true;
}

bool InputValidator::validateConstants(QWidget* parent, const QVector<NamedField>& fields, ParseFn parse)
{
    for (const auto& f : fields) {
        if (f.text.trimmed().isEmpty()) continue;   // campo vuoto = 0, lecito

        bool ok = false;
        parse(f.text, &ok);                          // tenta di valutare la costante
        if (!ok) {
            showInvalidConstantError(parent, f.name, f.text);
            return false;
        }
    }
    return true;
}

bool InputValidator::validateIdentifiers(QWidget* parent, const QString& expr,
                                         const QString& fieldName)
{
    if (expr.trimmed().isEmpty()) return true;

    // Identificatori MULTI-lettera ammessi.
    // I single-letter (u,v,w,t,A..F,s,x,y,z,p,...) sono già gestiti da
    // validateParametricVariables, quindi qui li IGNORIAMO per non duplicare.
    static const QSet<QString> kAllowedMultiLetter = {
        // costanti
        "pi", "PI", "Pi", "tau", "TAU", "Tau",
        "epsilon", "inf", "NaN",
        // exprtk gestisce 'e' come singolo carattere -> sotto

        // trigonometriche
        "sin", "cos", "tan",
        "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh",
        "asinh", "acosh", "atanh",
        "sec", "csc", "cot",
        "deg2rad", "rad2deg", "deg2grad", "grad2deg",

        // esponenziali / logaritmi
        "exp", "expm1", "log", "log2", "log10", "log1p", "ln", "logn",

        // potenze e radici
        "pow", "sqrt", "cbrt", "root", "hypot",

        // valore assoluto, segno, arrotondamenti
        "abs", "fabs", "sgn", "sign", "floor", "ceil", "round", "trunc",
        "frac", "fract",

        // min / max / clamp
        "min", "max", "clamp", "iclamp", "inrange", "avg", "sum", "mul",

        // modulo / resto
        "mod", "fmod", "fdim",

        // GLSL vector ops (per gli script che scrivono codice GLSL)
        "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
        "length", "normalize", "dot", "cross", "distance",
        "reflect", "refract", "mix", "step", "smoothstep",

        // time / shader globals
        "iTime", "u_time", "fragCoord", "fragColor", "textureCol",
        // multi-mesh: quale copia e quante (campo Meshes, sezioni //MESH_BEGIN)
        "mesh", "meshes",

        // utility comuni nelle texture/ray-marching
        "texture", "texture2D",

        // funzioni di common.glsl (progetto)
        "sys_hash", "sys_noise", "NoiseW",

        // solver per variabili implicite (shaders/implicit.glsl): chiamabili
        // dal corpo metrico e dalla mappa di visualizzazione x/y/z.
        "solveKerrR", "solveQuadraticPos", "clampPositive",
        "kerrGrr", "kerrGpp", "kerrRadius", "kerrRprime", "kerrEmbedZ", "kerrUmin",
        "solveKruskalR",
        "kruskalR_U", "kruskalGxx", "kruskalRprime", "kruskalEmbedZ",

        // altre funzioni GLSL standard talvolta utili
        "inverse", "transpose", "determinant",
        "radians", "degrees",
        "any", "all", "not",
        "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual",
        "equal", "notEqual",
    };

    QRegularExpression identRe(R"(\b[A-Za-z_][A-Za-z0-9_]+\b)");
    auto it = identRe.globalMatch(expr);
    while (it.hasNext()) {
        QString token = it.next().captured(0);
        if (!kAllowedMultiLetter.contains(token)) {
            notify(parent, QMessageBox::Critical, "Unknown Identifier",
                                  QString("Unknown function or variable name '%1' in field '%2'.\n\n"
                                          "Check for typos (e.g. 'lengt' vs 'length', 'pwr' vs 'pow').")
                                      .arg(token, fieldName));
            return false;
        }
    }
    return true;
}

void InputValidator::showNegativeConstantError(QWidget* parent, const QString& name)
{
    notify(parent, QMessageBox::Warning, "Negative Constant",
           QString("The constant '%1' cannot be negative.\n\n"
                   "Negative values are not allowed for the uppercase parameters "
                   "(A...F). The previous value has been restored.").arg(name));
}

void InputValidator::notify(QWidget* parent, QMessageBox::Icon icon,
                            const QString& title, const QString& text)
{
    // Solo gli errori veri: un Information e' un avviso, non un fallimento.
    if (icon == QMessageBox::Critical || icon == QMessageBox::Warning)
        ++s_errorCount;

#if defined(Q_OS_ANDROID)
    // Evita di impilare due modali (è proprio questo che fa crashare Android)
    if (s_boxActive) return;
    s_boxActive = true;

    // NIENTE parent sulla finestra OpenGL, niente exec(): box asincrono.
    auto* box = new QMessageBox(icon, title, text, QMessageBox::Ok, nullptr);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(true);
    QObject::connect(box, &QDialog::finished, box, [](int){ s_boxActive = false; });
    box->open();   // ritorna subito: nessun event loop annidato
    Q_UNUSED(parent);
#else
    QMessageBox box(icon, title, text, QMessageBox::Ok, parent);
    box.exec();
#endif
}
