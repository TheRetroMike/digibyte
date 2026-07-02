// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/digibyte-config.h>
#endif

#include <qt/digidollarcoincontroldialog.h>
#include <qt/forms/ui_digidollarcoincontroldialog.h>

#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/ddcoincontrol.h>
#include <wallet/digidollarwallet.h>

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QTreeWidget>

using wallet::DDCoinControl;

namespace {

bool UseDarkDigiDollarCoinControlTheme(const WalletModel* model, const QWidget* widget)
{
    if (model && model->getOptionsModel()) {
        const QString theme = model->getOptionsModel()->data(
            model->getOptionsModel()->index(OptionsModel::Theme), Qt::EditRole).toString();
        return theme.isEmpty() || theme == QLatin1String("dark");
    }
    const QPalette palette = widget ? widget->palette() : qApp->palette();
    return palette.color(QPalette::Window).lightness() < 128;
}

QString DigiDollarCoinControlDialogStyleSheet(bool dark_theme)
{
    if (dark_theme) {
        return QStringLiteral(
            "QDialog#DigiDollarCoinControlDialog {"
            "  background-color: #0b2419;"
            "  color: #ffffff;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QLabel,"
            "QDialog#DigiDollarCoinControlDialog QCheckBox,"
            "QDialog#DigiDollarCoinControlDialog QRadioButton {"
            "  color: #ffffff;"
            "  background-color: transparent;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QFrame#frame {"
            "  background-color: #113a29;"
            "  border: 1px solid #42d884;"
            "  border-radius: 4px;"
            "}"
            "QDialog#DigiDollarCoinControlDialog CoinControlTreeWidget,"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget {"
            "  background-color: #113a29;"
            "  alternate-background-color: #164532;"
            "  color: #ffffff;"
            "  selection-background-color: #16804f;"
            "  selection-color: #ffffff;"
            "  border: 1px solid #42d884;"
            "  gridline-color: #42d884;"
            "  font-size: 11pt;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::item {"
            "  color: #ffffff;"
            "  padding: 8px;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:alternate {"
            "  background-color: #164532;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:selected {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:hover {"
            "  background-color: #1b5b3f;"
            "  color: #ffffff;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QHeaderView::section {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "  border: none;"
            "  padding: 8px;"
            "  font-weight: bold;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QCheckBox::indicator,"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::indicator {"
            "  width: 16px;"
            "  height: 16px;"
            "  background-color: #ffffff;"
            "  border: 2px solid #42d884;"
            "  border-radius: 3px;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QCheckBox::indicator:checked,"
            "QDialog#DigiDollarCoinControlDialog QTreeWidget::indicator:checked {"
            "  background-color: #16804f;"
            "  border: 2px solid #42d884;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QPushButton {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "  border: 2px solid #16804f;"
            "  border-radius: 4px;"
            "  padding: 6px 16px;"
            "  font-weight: bold;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QPushButton:hover {"
            "  background-color: #21a866;"
            "  border-color: #21a866;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QPushButton:pressed {"
            "  background-color: #0f633c;"
            "  border-color: #0f633c;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QScrollBar:vertical,"
            "QDialog#DigiDollarCoinControlDialog QScrollBar:horizontal {"
            "  background-color: #0b2419;"
            "  border: 1px solid #42d884;"
            "}"
            "QDialog#DigiDollarCoinControlDialog QScrollBar::handle:vertical,"
            "QDialog#DigiDollarCoinControlDialog QScrollBar::handle:horizontal {"
            "  background-color: #16804f;"
            "  border-radius: 3px;"
            "}");
    }

    return QStringLiteral(
        "QDialog#DigiDollarCoinControlDialog {"
        "  background-color: #eef9f2;"
        "  color: #123f2b;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QLabel,"
        "QDialog#DigiDollarCoinControlDialog QCheckBox,"
        "QDialog#DigiDollarCoinControlDialog QRadioButton {"
        "  color: #123f2b;"
        "  background-color: transparent;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QFrame#frame {"
        "  background-color: #ffffff;"
        "  border: 1px solid #1f9d57;"
        "  border-radius: 4px;"
        "}"
        "QDialog#DigiDollarCoinControlDialog CoinControlTreeWidget,"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget {"
        "  background-color: #ffffff;"
        "  alternate-background-color: #e6f7ec;"
        "  color: #123f2b;"
        "  selection-background-color: #1f9d57;"
        "  selection-color: #ffffff;"
        "  border: 1px solid #1f9d57;"
        "  gridline-color: #c8ead6;"
        "  font-size: 11pt;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::item {"
        "  color: #123f2b;"
        "  padding: 8px;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:alternate {"
        "  background-color: #e6f7ec;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:selected {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::item:hover {"
        "  background-color: #d7f2e1;"
        "  color: #123f2b;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QHeaderView::section {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "  border: none;"
        "  padding: 8px;"
        "  font-weight: bold;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QCheckBox::indicator,"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::indicator {"
        "  width: 16px;"
        "  height: 16px;"
        "  background-color: #ffffff;"
        "  border: 2px solid #1f9d57;"
        "  border-radius: 3px;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QCheckBox::indicator:checked,"
        "QDialog#DigiDollarCoinControlDialog QTreeWidget::indicator:checked {"
        "  background-color: #1f9d57;"
        "  border: 2px solid #1f9d57;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QPushButton {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "  border: 2px solid #1f9d57;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-weight: bold;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QPushButton:hover {"
        "  background-color: #26b96a;"
        "  border-color: #26b96a;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QPushButton:pressed {"
        "  background-color: #147a42;"
        "  border-color: #147a42;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QScrollBar:vertical,"
        "QDialog#DigiDollarCoinControlDialog QScrollBar:horizontal {"
        "  background-color: #eef9f2;"
        "  border: 1px solid #1f9d57;"
        "}"
        "QDialog#DigiDollarCoinControlDialog QScrollBar::handle:vertical,"
        "QDialog#DigiDollarCoinControlDialog QScrollBar::handle:horizontal {"
        "  background-color: #1f9d57;"
        "  border-radius: 3px;"
        "}");
}

} // namespace

QList<CAmount> DigiDollarCoinControlDialog::payAmounts;

bool DigiDollarCoinControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    int column = treeWidget()->sortColumn();
    if (column == DigiDollarCoinControlDialog::COLUMN_AMOUNT ||
        column == DigiDollarCoinControlDialog::COLUMN_DATE ||
        column == DigiDollarCoinControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}

DigiDollarCoinControlDialog::DigiDollarCoinControlDialog(DDCoinControl& coin_control, WalletModel* _model, const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::DigiDollarCoinControlDialog),
    m_coin_control(coin_control),
    model(_model),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);
    ui->treeWidget->setAlternatingRowColors(true);
    setStyleSheet(DigiDollarCoinControlDialogStyleSheet(
        UseDarkDigiDollarCoinControlTheme(model, this)));

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(tr("&Copy address"), this, &DigiDollarCoinControlDialog::copyAddress);
    contextMenu->addAction(tr("Copy &label"), this, &DigiDollarCoinControlDialog::copyLabel);
    contextMenu->addAction(tr("Copy &amount"), this, &DigiDollarCoinControlDialog::copyAmount);
    m_copy_transaction_outpoint_action = contextMenu->addAction(tr("Copy transaction &ID and output index"), this, &DigiDollarCoinControlDialog::copyTransactionOutpoint);
    connect(ui->treeWidget, &QWidget::customContextMenuRequested, this, &DigiDollarCoinControlDialog::showMenu);

    // clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);

    connect(clipboardQuantityAction, &QAction::triggered, this, &DigiDollarCoinControlDialog::clipboardQuantity);
    connect(clipboardAmountAction, &QAction::triggered, this, &DigiDollarCoinControlDialog::clipboardAmount);

    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);

    // toggle tree/list mode
    connect(ui->radioTreeMode, &QRadioButton::toggled, this, &DigiDollarCoinControlDialog::radioTreeMode);
    connect(ui->radioListMode, &QRadioButton::toggled, this, &DigiDollarCoinControlDialog::radioListMode);

    // click on checkbox
    connect(ui->treeWidget, &QTreeWidget::itemChanged, this, &DigiDollarCoinControlDialog::viewItemChanged);

    // click on header
    ui->treeWidget->header()->setSectionsClickable(true);
    connect(ui->treeWidget->header(), &QHeaderView::sectionClicked, this, &DigiDollarCoinControlDialog::headerSectionClicked);

    // ok button
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &DigiDollarCoinControlDialog::buttonBoxClicked);

    // (un)select all
    connect(ui->pushButtonSelectAll, &QPushButton::clicked, this, &DigiDollarCoinControlDialog::buttonSelectAllClicked);

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 150);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 190);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 320);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 130);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 110);
    ui->treeWidget->setColumnWidth(COLUMN_TXID_VOUT, 180);

    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT, Qt::DescendingOrder);

    // restore list mode and sortorder as a convenience feature
    QSettings settings;
    if (settings.contains("nDDCoinControlMode") && !settings.value("nDDCoinControlMode").toBool())
        ui->radioTreeMode->click();
    if (settings.contains("nDDCoinControlSortColumn") && settings.contains("nDDCoinControlSortOrder"))
        sortView(settings.value("nDDCoinControlSortColumn").toInt(), (static_cast<Qt::SortOrder>(settings.value("nDDCoinControlSortOrder").toInt())));

    GUIUtil::handleCloseWindowShortcut(this);

    if(_model)
    {
        updateView();
        DigiDollarCoinControlDialog::updateLabels(m_coin_control, _model, this);
    }
}

DigiDollarCoinControlDialog::~DigiDollarCoinControlDialog()
{
    QSettings settings;
    settings.setValue("nDDCoinControlMode", ui->radioListMode->isChecked());
    settings.setValue("nDDCoinControlSortColumn", sortColumn);
    settings.setValue("nDDCoinControlSortOrder", (int)sortOrder);

    delete ui;
}

// ok button
void DigiDollarCoinControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void DigiDollarCoinControlDialog::buttonSelectAllClicked()
{
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
    {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked)
        {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
                ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    if (state == Qt::Unchecked)
        m_coin_control.UnSelectAll(); // just to be sure
    DigiDollarCoinControlDialog::updateLabels(m_coin_control, model, this);
}

// context menu
void DigiDollarCoinControlDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID) for tree roots in context menu
        if (item->data(COLUMN_TXID_VOUT, TxHashRole).toString().length() == 64) // transaction hash is 64 characters (this means it is a child node)
        {
            m_copy_transaction_outpoint_action->setEnabled(true);
        }
        else // this means click on parent node in tree mode -> disable
        {
            m_copy_transaction_outpoint_action->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void DigiDollarCoinControlDialog::copyAmount()
{
    GUIUtil::setClipboard(contextMenuItem->text(COLUMN_AMOUNT));
}

// context menu action: copy label
void DigiDollarCoinControlDialog::copyLabel()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void DigiDollarCoinControlDialog::copyAddress()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id and vout index
void DigiDollarCoinControlDialog::copyTransactionOutpoint()
{
    const QString address = contextMenuItem->data(COLUMN_TXID_VOUT, TxHashRole).toString();
    const QString vout = contextMenuItem->data(COLUMN_TXID_VOUT, VOutRole).toString();
    const QString outpoint = QString("%1:%2").arg(address).arg(vout);

    GUIUtil::setClipboard(outpoint);
}

// copy label "Quantity" to clipboard
void DigiDollarCoinControlDialog::clipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// copy label "Amount" to clipboard
void DigiDollarCoinControlDialog::clipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// treeview: sort
void DigiDollarCoinControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void DigiDollarCoinControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }
    else
    {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void DigiDollarCoinControlDialog::radioTreeMode(bool checked)
{
    if (checked && model)
        updateView();
}

// toggle list mode
void DigiDollarCoinControlDialog::radioListMode(bool checked)
{
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void DigiDollarCoinControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    if (column == COLUMN_CHECKBOX && item->data(COLUMN_TXID_VOUT, TxHashRole).toString().length() == 64) // transaction hash is 64 characters (child node)
    {
        COutPoint outpt(uint256S(item->data(COLUMN_TXID_VOUT, TxHashRole).toString().toStdString()), item->data(COLUMN_TXID_VOUT, VOutRole).toUInt());

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            m_coin_control.UnSelect(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else
            m_coin_control.Select(outpt);

        // selection changed -> update labels
        if (ui->treeWidget->isEnabled()) // do not update on every click for (un)select all
            DigiDollarCoinControlDialog::updateLabels(m_coin_control, model, this);
    }
}

void DigiDollarCoinControlDialog::updateLabels(DDCoinControl& m_coin_control, WalletModel *model, QDialog* dialog)
{
    if (!model)
        return;

    // nPayAmount
    CAmount nPayAmount = 0;
    for (const CAmount &amount : DigiDollarCoinControlDialog::payAmounts) {
        nPayAmount += amount;
    }

    CAmount nAmount = 0;
    unsigned int nQuantity = 0;

    auto vCoinControl{m_coin_control.ListSelected()};

    // Get DD wallet from model
    DigiDollarWallet* ddWallet = model->getDigiDollarWallet();
    if (!ddWallet) {
        return;
    }

    // Iterate through selected DD UTXOs
    for (const auto& outpt : vCoinControl) {
        // Get DD amount from UTXO
        CAmount dd_amount = ddWallet->GetDDFromUTXO(outpt);
        if (dd_amount > 0) {
            nQuantity++;
            nAmount += dd_amount;
        }
    }

    // actually update labels
    QLabel *l1 = dialog->findChild<QLabel *>("labelCoinControlQuantity");
    QLabel *l2 = dialog->findChild<QLabel *>("labelCoinControlAmount");

    // stats
    l1->setText(QString::number(nQuantity));                                           // Quantity
    // DD amounts are stored in cents, divide by 100 to get dollars
    l2->setText(QString("%1 $DD").arg(nAmount / 100.0, 0, 'f', 2));                    // Amount in DD dollars
}

void DigiDollarCoinControlDialog::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        updateView();
    }

    QDialog::changeEvent(e);
}

void DigiDollarCoinControlDialog::updateView()
{
    if (!model)
        return;

    bool treeMode = ui->radioTreeMode->isChecked();

    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors(!treeMode);
    QFlags<Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    QFlags<Qt::ItemFlag> flgTristate = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate;

    // Get DD wallet from model
    DigiDollarWallet* ddWallet = model->getDigiDollarWallet();
    if (!ddWallet) {
        ui->treeWidget->setEnabled(true);
        return;
    }

    // Get all DD UTXOs
    std::vector<DDUtxo> dd_utxos = ddWallet->GetDDUTXOs();

    // Group UTXOs by address (for tree mode)
    std::map<QString, std::vector<DDUtxo>> utxos_by_address;

    for (const auto& dd_utxo : dd_utxos) {
        // For DD UTXOs, we don't have traditional addresses
        // Group them as "DigiDollar UTXOs"
        QString groupLabel = tr("DigiDollar UTXOs");
        utxos_by_address[groupLabel].push_back(dd_utxo);
    }

    // Build tree/list
    for (const auto& addr_group : utxos_by_address) {
        DigiDollarCoinControlWidgetItem* itemWalletAddress{nullptr};
        QString sGroupLabel = addr_group.first;

        if (treeMode)
        {
            // Create parent node
            itemWalletAddress = new DigiDollarCoinControlWidgetItem(ui->treeWidget);
            itemWalletAddress->setFlags(flgTristate);
            itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
            itemWalletAddress->setText(COLUMN_LABEL, sGroupLabel);
        }

        CAmount nSum = 0;
        int nChildren = 0;

        for (const auto& dd_utxo : addr_group.second) {
            nSum += dd_utxo.dd_amount;
            nChildren++;

            DigiDollarCoinControlWidgetItem *itemOutput;
            if (treeMode)    itemOutput = new DigiDollarCoinControlWidgetItem(itemWalletAddress);
            else             itemOutput = new DigiDollarCoinControlWidgetItem(ui->treeWidget);
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Unchecked);

            // amount (display in DD dollars, dd_amount is in cents)
            double dd_display = dd_utxo.dd_amount / 100.0;
            itemOutput->setText(COLUMN_AMOUNT, QString("%1 $DD").arg(dd_display, 0, 'f', 2));
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)dd_utxo.dd_amount));

            // label
            itemOutput->setText(COLUMN_LABEL, tr("$DD UTXO"));

            // address - show txid:vout
            QString outpoint_str = QString::fromStdString(dd_utxo.outpoint.hash.GetHex()).left(16) + "...:" + QString::number(dd_utxo.outpoint.n);
            itemOutput->setText(COLUMN_ADDRESS, outpoint_str);

            // Full txid and vout for COLUMN_TXID_VOUT
            itemOutput->setText(COLUMN_TXID_VOUT, QString::fromStdString(dd_utxo.outpoint.hash.GetHex()) + ":" + QString::number(dd_utxo.outpoint.n));

            // date - we don't have this info readily available, leave blank for now
            itemOutput->setText(COLUMN_DATE, "");
            itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong)0));

            // confirmations - we don't have this info readily available, leave blank for now
            itemOutput->setText(COLUMN_CONFIRMATIONS, "");
            itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong)0));

            // transaction hash
            itemOutput->setData(COLUMN_TXID_VOUT, TxHashRole, QString::fromStdString(dd_utxo.outpoint.hash.GetHex()));

            // vout index
            itemOutput->setData(COLUMN_TXID_VOUT, VOutRole, dd_utxo.outpoint.n);

            // set checkbox if already selected
            if (m_coin_control.IsSelected(dd_utxo.outpoint))
                itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        }

        // amount for parent
        if (treeMode)
        {
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            double dd_display_sum = nSum / 100.0;
            itemWalletAddress->setText(COLUMN_AMOUNT, QString("%1 $DD").arg(dd_display_sum, 0, 'f', 2));
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)nSum));
        }
    }

    // expand all partially selected
    if (treeMode)
    {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}
