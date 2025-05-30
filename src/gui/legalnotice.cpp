/*
 * SPDX-FileCopyrightText: 2018 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "legalnotice.h"
#include "ui_legalnotice.h"
#include "theme.h"

namespace OCC {


LegalNotice::LegalNotice(QDialog *parent)
    : QDialog(parent)
    , _ui(new Ui::LegalNotice)
{
    _ui->setupUi(this);

    connect(_ui->closeButton, &QPushButton::clicked, this, &LegalNotice::accept);

    customizeStyle();
}

LegalNotice::~LegalNotice()
{
    delete _ui;
}

void LegalNotice::changeEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        customizeStyle();
        break;
    default:
        break;
    }

    QDialog::changeEvent(e);
}

void LegalNotice::customizeStyle()
{
    QString notice = tr("<p>Copyright 2017-2025 Nextcloud GmbH<br />"
                        "Copyright 2012-2023 ownCloud GmbH</p>");

    notice += tr("<p>Licensed under the GNU General Public License (GPL) Version 2.0 or any later version.</p>");

    notice += "<p>&nbsp;</p>";
    notice += Theme::instance()->aboutDetails();

    Theme::replaceLinkColorStringBackgroundAware(notice);

    _ui->notice->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextBrowserInteraction);
    _ui->notice->setText(notice);
    _ui->notice->setWordWrap(true);
    _ui->notice->setOpenExternalLinks(true);
}

} // namespace OCC
