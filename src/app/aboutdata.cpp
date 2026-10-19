#include "aboutdata.h"

#include <KLocalizedString>

KAboutData penelopeAboutData()
{
    KAboutData aboutData(
        QStringLiteral("penelope"),
        i18n("Penelope"),
        QStringLiteral(PENELOPE_VERSION),
        i18n("A beautiful paginated markdown reader"),
        KAboutLicense::GPL_V3,
        i18n("(c) 2025-2026"));

    // Set by name: the constructor's trailing arguments are otherText, then the
    // homepage, then the bug address, and getting that order wrong leaves the
    // bug address at its KDE default.
    aboutData.setHomepage(QStringLiteral("https://vibekoder.ca"));
    aboutData.setBugAddress("https://github.com/clintonthegeek/Penelope/issues");

    // Name, task, email, web address: the website belongs in the last one.
    aboutData.addAuthor(i18n("Clinton Ignatov"), i18n("Developer"), QString(),
                        QStringLiteral("https://vibekoder.ca"));
    aboutData.setOtherText(
        i18n("Penelope is developed at <a href=\"https://github.com/clintonthegeek/Penelope\">"
             "github.com/clintonthegeek/Penelope</a>, where bugs and feature requests belong. "
             "It is built with the KDE Frameworks libraries."));
    aboutData.setOrganizationDomain("penelope.org");
    aboutData.setDesktopFileName(QStringLiteral("org.penelope.Penelope"));
    return aboutData;
}
