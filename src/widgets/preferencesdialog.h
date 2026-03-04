#ifndef PENELOPE_PREFERENCESDIALOG_H
#define PENELOPE_PREFERENCESDIALOG_H

#include <KConfigDialog>

class PenelopeConfigDialog : public KConfigDialog
{
    Q_OBJECT

public:
    explicit PenelopeConfigDialog(QWidget *parent);
};

#endif // PENELOPE_PREFERENCESDIALOG_H
