// Copyright (c) 2014-2026 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qt/transactionoverviewwidget.h>

#include <qt/guiutil.h>
#include <qt/transactiontablemodel.h>

#include <QEvent>
#include <QHelpEvent>
#include <QListView>
#include <QSize>
#include <QSizePolicy>
#include <QToolTip>

TransactionOverviewWidget::TransactionOverviewWidget(QWidget* parent)
    : QListView(parent) {
    setAlternatingRowColors(true);
}

QSize TransactionOverviewWidget::sizeHint() const
{
    return {sizeHintForColumn(TransactionTableModel::ToAddress), QListView::sizeHint().height()};
}

void TransactionOverviewWidget::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Minimum);
    setSizePolicy(sp);
}

bool TransactionOverviewWidget::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(event);
        QModelIndex index = indexAt(helpEvent->pos());
        if (index.isValid()) {
            QString tooltipText = index.data(Qt::ToolTipRole).toString();
            if (!tooltipText.isEmpty()) {
                // Show tooltip with explicit HTML styling to ensure black text
                QString styledTooltip = QString(
                    "<div style='color: #000000; background-color: #ffffdc; padding: 4px;'>"
                    "%1"
                    "</div>"
                ).arg(GUIUtil::TooltipToHtml(tooltipText));
                QToolTip::showText(helpEvent->globalPos(), styledTooltip, this);
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QListView::viewportEvent(event);
}
