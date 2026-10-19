#ifndef PENELOPE_ABOUTDATA_H
#define PENELOPE_ABOUTDATA_H

#include <KAboutData>

// The application's About data, in one place: the About dialog, the Help menu's
// report-bug entry and the command line all read it, and the regression suite
// checks it. Penelope is not a KDE project, so help and bug reports point at the
// project's own pages; KDE appears only as a library it is built on.
KAboutData penelopeAboutData();

#endif // PENELOPE_ABOUTDATA_H
