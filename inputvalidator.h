#ifndef INPUTVALIDATOR_H
#define INPUTVALIDATOR_H

#include <QString>
#include <QWidget>
#include <QLineEdit>
#include <QVector>
#include <functional>
#include <QMessageBox>

class InputValidator
{
public:
    struct LimitField {
        QLineEdit* edit;
        bool active;
    };

    struct NamedField {
        QString name;
        QString text;
    };

    using ParseFn = std::function<float(const QString&, bool*)>;

    // Controlli per la modalità Ray Marching (Implicita)
    // allowP: sotto-tab Cross Section (equazione a 4 variabili x,y,z,p).
    // Default false: il tab 3D continua a vietare 'p' come sempre.
    static bool validateImplicitEquation(QWidget* parent, const QString& rawEq, bool allowP = false);
    // Campo equazione vuoto al Run/Invio: avvisa invece di uscire in silenzio
    // lasciando a schermo la superficie precedente. Per svuotare c'e' NEW.
    // sceneEmpty: la scena e' gia' vuota (dopo NEW), non c'e' nessuna
    // superficie precedente a schermo di cui parlare.
    static void notifyEmptyImplicitEquation(QWidget* parent, bool crossSection, bool sceneEmpty);
    // Scena vuota di NEW: START, RESET e Projection non hanno niente su cui
    // agire e lo dicono (nothingTo = "Nothing to start", ...). Information:
    // non e' un errore, e non conta per l'esito del Run.
    static void showEmptySceneNotice(QWidget* parent, const QString& nothingTo);
    static bool validateImplicitScriptContext(QWidget* parent, const QString& texCode);

    // Controlli per la modalità Parametrica
    static bool validateParametricVariables(QWidget* parent, const QString& mainEqs, const QString& allEqsToTest, bool hasConstraint);
    static bool validateCompositionFields(QWidget* parent, const QString& mainEqs, const QString& cU, const QString& cV, const QString& cW, bool geoHasText, const QString& composedTest);
    static bool validateLimits(QWidget* parent, float uMin, float uMax, bool uActive, float vMin, float vMax, bool vActive, float wMin, float wMax, bool wActive);
    static bool validateWUsage(QWidget* parent, const QString& allEqs, bool hasExplicit);
    static void resetGeodesicWarning();
    static bool validateGeodesicConformalFactor(QWidget* parent, const QString& lineX, const QString& lineY, const QString& lineZ, const QString& lineP, const QString& lambdaStr, bool showWarning);
    static void showGeodesicSingularityError(QWidget* parent);
    static void showEquationSyntaxError(QWidget* parent);
    static void showMathematicalCollapseError(QWidget* parent);
    static void showAnimatedGeodesicSingularityError(QWidget* parent);
    static void showInvalidConformalConstantError(QWidget* parent);

    // Gestione avvisi file e shader
    static void showImageNotFoundError(QWidget* parent, const QString& imgPath);
    static void showShaderCompilationError(QWidget* parent, const QString& title, const QString& errorLog);
    static void showInvalidConstantError(QWidget* parent, const QString& name, const QString& text);
    static void showInvalidStepsError(QWidget* parent, const QString& text);
    // Spessore del tubo (sotto-tab Tubes) vuoto o illeggibile.
    static void showInvalidThicknessError(QWidget* parent, const QString& text);
    static void showInvalidMeshCountError(QWidget* parent, const QString& text);
    // emptyMeansNoLimit distingue i due contesti, che hanno regole OPPOSTE sul
    // campo vuoto: nei limiti spaziali X/Y/Z del Ray Marching vuoto = nessun
    // taglio (lecito), nei limiti u/v/w del parametrico il dominio DEVE essere
    // dichiarato. Lo stesso messaggio li serviva entrambi e consigliava di
    // lasciare il campo vuoto anche dove il vuoto e' proprio l'errore.
    static void showInvalidLimitError(QWidget* parent, const QString& name, const QString& text,
                                      bool emptyMeansNoLimit = true);
    // Limiti leggibili ma incoerenti (min >= max) su un singolo asse: caso
    // diverso dal numero illeggibile, e va detto all'utente in modo diverso.
    static void showLimitOrderError(QWidget* parent, QChar axis);
    // Meno di 3 campi fra X/Y/Z/P compilati al Run/START parametrico: non c'e'
    // una superficie da costruire. Il Run del dock si limita a restare spento,
    // ma il master START non si disabilita mai e deve spiegare perche' non fa
    // nulla, invece di disegnare una forma degenere.
    static void showIncompleteEquationsError(QWidget* parent);

    // Validazione specifica per il dock script (Parametrico)
    static bool validateParametricScriptContext(QWidget* parent, const QString& texCode);
    static bool validateScriptModeContext(QWidget* parent, int currentScriptMode, const QString& currentText);
    static bool validateParametricScriptReturn(QWidget* parent, const QString& cleanCode);
    static bool validateImplicitScriptReturn(QWidget* parent, const QString& cleanCode);
    static void showMetricMissingConditionsInfo(QWidget* parent);
    static void showMetricAmbiguousConstantWarning(QWidget* parent, const QStringList& names);
    // Stessa famiglia del precedente, per l'altro caso di costante contesa: una
    // texture che rivendica una lettera gia' usata dalla SUPERFICIE. Non e' un
    // errore -- si puo' volere che un solo slider muova entrambe -- quindi
    // l'avviso e' informativo e non blocca il caricamento: dice quali lettere
    // sono in conflitto e come liberarle. Ritorna false se l'utente sceglie di
    // annullare l'applicazione della texture.
    static bool showTextureConstantClashWarning(QWidget* parent, const QStringList& names,
                                                const QStringList& freeLetters);
    // Gemello INFORMATIVO, per il caricamento di un record: la combinazione e'
    // gia' salvata, quindi non si chiede se procedere -- si dice soltanto quali
    // lettere sono condivise e fra quali parti. lines: una riga per lettera
    // ("F is used by both the texture and the background").
    // Ritorna true se l'utente ha spuntato "Don't show again for this record".
    static bool showSharedConstantsNotice(QWidget* parent, const QStringList& lines,
                                          const QStringList& freeLetters);
    static bool validateParentheses(QWidget* parent, const QString& cleanCode);

    // Validatore generico di sintassi.
    // outWarned (opzionale): posto a true se e' stato mostrato un WARNING non
    // bloccante (es. operatori consecutivi). Il chiamante puo' usarlo per NON
    // mostrare un secondo popup (es. errore di compilazione) sullo stesso errore.
    static bool validateExpressionSyntax(QWidget* parent, const QString& expr, const QString& fieldName, bool* outWarned = nullptr);
    static bool validateAndParseLimits(QWidget* parent, const QVector<LimitField>& fields, ParseFn parse, QVector<float>& outValues);
    static bool validateFieldList(QWidget* parent, const QVector<NamedField>& fields, bool* outWarned = nullptr);
    static bool validateConstants(QWidget* parent, const QVector<NamedField>& fields, ParseFn parse);
    static bool validateIdentifiers(QWidget* parent, const QString& expr, const QString& fieldName);
    static void showNegativeConstantError(QWidget* parent, const QString& name);


    // Quanti errori (Critical/Warning) sono stati mostrati all'utente da sempre.
    // Serve a capire se un'azione e' fallita SENZA dover marcare a mano ogni
    // singolo validatore: si legge il contatore prima e dopo, e se e' cresciuto
    // l'utente ha visto almeno un errore. Copre anche i validatori aggiunti in
    // futuro, che passano tutti da notify().
    static quint64 errorCount() { return s_errorCount; }

private:
    static void notify(QWidget* parent, QMessageBox::Icon icon,
                       const QString& title, const QString& text);
    static bool s_boxActive;
    static quint64 s_errorCount;
};

#endif // INPUTVALIDATOR_H
