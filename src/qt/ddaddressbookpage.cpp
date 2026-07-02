// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/ddaddressbookpage.h>
#include <qt/walletmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <base58.h>
#include <key_io.h>
#include <wallet/types.h>

#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QHeaderView>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QFile>
#include <QFileDialog>
#include <QPalette>
#include <QTextStream>

namespace {

bool UseDarkDigiDollarAddressBookTheme(const WalletModel* model, const QWidget* widget)
{
    if (model && model->getOptionsModel()) {
        const QString theme = model->getOptionsModel()->data(
            model->getOptionsModel()->index(OptionsModel::Theme), Qt::EditRole).toString();
        return theme.isEmpty() || theme == QLatin1String("dark");
    }
    const QPalette palette = widget ? widget->palette() : qApp->palette();
    return palette.color(QPalette::Window).lightness() < 128;
}

QString DigiDollarAddressBookStyleSheet(bool dark_theme)
{
    if (dark_theme) {
        return QStringLiteral(
            "QDialog#DDAddressBookPage {"
            "  background-color: #0b2419;"
            "  color: #ffffff;"
            "}"
            "QDialog#DDAddressBookPage QLabel {"
            "  color: #ffffff;"
            "  font-weight: bold;"
            "}"
            "QDialog#DDAddressBookPage QTableWidget {"
            "  background-color: #113a29;"
            "  alternate-background-color: #164532;"
            "  color: #ffffff;"
            "  selection-background-color: #16804f;"
            "  selection-color: #ffffff;"
            "  border: 1px solid #42d884;"
            "  gridline-color: #42d884;"
            "  font-size: 11pt;"
            "}"
            "QDialog#DDAddressBookPage QTableWidget::item {"
            "  color: #ffffff;"
            "  padding: 8px;"
            "}"
            "QDialog#DDAddressBookPage QTableWidget::item:alternate {"
            "  background-color: #164532;"
            "}"
            "QDialog#DDAddressBookPage QTableWidget::item:selected {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "}"
            "QDialog#DDAddressBookPage QTableWidget::item:hover {"
            "  background-color: #1b5b3f;"
            "  color: #ffffff;"
            "}"
            "QDialog#DDAddressBookPage QHeaderView::section {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "  border: none;"
            "  padding: 8px;"
            "  font-weight: bold;"
            "}"
            "QDialog#DDAddressBookPage QLineEdit {"
            "  background-color: #113a29;"
            "  color: #ffffff;"
            "  border: 2px solid #42d884;"
            "  border-radius: 4px;"
            "  padding: 6px;"
            "}"
            "QDialog#DDAddressBookPage QLineEdit:focus {"
            "  border-color: #21a866;"
            "  background-color: #164532;"
            "}"
            "QDialog#DDAddressBookPage QPushButton {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "  border: 2px solid #16804f;"
            "  border-radius: 4px;"
            "  padding: 8px 16px;"
            "  font-weight: bold;"
            "  min-height: 28px;"
            "}"
            "QDialog#DDAddressBookPage QPushButton:hover {"
            "  background-color: #21a866;"
            "  border-color: #21a866;"
            "}"
            "QDialog#DDAddressBookPage QPushButton:pressed {"
            "  background-color: #0f633c;"
            "  border-color: #0f633c;"
            "}"
            "QDialog#DDAddressBookPage QMenu {"
            "  background-color: #113a29;"
            "  color: #ffffff;"
            "  border: 1px solid #42d884;"
            "}"
            "QDialog#DDAddressBookPage QMenu::item:selected {"
            "  background-color: #16804f;"
            "  color: #ffffff;"
            "}");
    }

    return QStringLiteral(
        "QDialog#DDAddressBookPage {"
        "  background-color: #eef9f2;"
        "  color: #123f2b;"
        "}"
        "QDialog#DDAddressBookPage QLabel {"
        "  color: #123f2b;"
        "  font-weight: bold;"
        "}"
        "QDialog#DDAddressBookPage QTableWidget {"
        "  background-color: #ffffff;"
        "  alternate-background-color: #e6f7ec;"
        "  color: #123f2b;"
        "  selection-background-color: #1f9d57;"
        "  selection-color: #ffffff;"
        "  border: 1px solid #1f9d57;"
        "  gridline-color: #c8ead6;"
        "  font-size: 11pt;"
        "}"
        "QDialog#DDAddressBookPage QTableWidget::item {"
        "  color: #123f2b;"
        "  padding: 8px;"
        "}"
        "QDialog#DDAddressBookPage QTableWidget::item:alternate {"
        "  background-color: #e6f7ec;"
        "}"
        "QDialog#DDAddressBookPage QTableWidget::item:selected {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "}"
        "QDialog#DDAddressBookPage QTableWidget::item:hover {"
        "  background-color: #d7f2e1;"
        "  color: #123f2b;"
        "}"
        "QDialog#DDAddressBookPage QHeaderView::section {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "  border: none;"
        "  padding: 8px;"
        "  font-weight: bold;"
        "}"
        "QDialog#DDAddressBookPage QLineEdit {"
        "  background-color: #ffffff;"
        "  color: #123f2b;"
        "  border: 2px solid #1f9d57;"
        "  border-radius: 4px;"
        "  padding: 6px;"
        "}"
        "QDialog#DDAddressBookPage QLineEdit:focus {"
        "  border-color: #26b96a;"
        "  background-color: #f6fff9;"
        "}"
        "QDialog#DDAddressBookPage QPushButton {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "  border: 2px solid #1f9d57;"
        "  border-radius: 4px;"
        "  padding: 8px 16px;"
        "  font-weight: bold;"
        "  min-height: 28px;"
        "}"
        "QDialog#DDAddressBookPage QPushButton:hover {"
        "  background-color: #26b96a;"
        "  border-color: #26b96a;"
        "}"
        "QDialog#DDAddressBookPage QPushButton:pressed {"
        "  background-color: #147a42;"
        "  border-color: #147a42;"
        "}"
        "QDialog#DDAddressBookPage QMenu {"
        "  background-color: #ffffff;"
        "  color: #123f2b;"
        "  border: 1px solid #1f9d57;"
        "}"
        "QDialog#DDAddressBookPage QMenu::item:selected {"
        "  background-color: #1f9d57;"
        "  color: #ffffff;"
        "}");
}

} // namespace

DDAddressBookPage::DDAddressBookPage(const PlatformStyle *platformStyle, Mode mode, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags)
    , m_mode(mode)
    , m_walletModel(nullptr)
    , m_platformStyle(platformStyle)
    , m_mainLayout(nullptr)
    , m_explanationLabel(nullptr)
    , m_searchEdit(nullptr)
    , m_table(nullptr)
    , m_buttonLayout(nullptr)
    , m_newButton(nullptr)
    , m_copyButton(nullptr)
    , m_deleteButton(nullptr)
    , m_exportButton(nullptr)
    , m_closeButton(nullptr)
    , m_contextMenu(nullptr)
{
    setObjectName("DDAddressBookPage");

    if (m_mode == ForSelection) {
        setWindowTitle(tr("Choose the address to send coins to"));
    } else {
        setWindowTitle(tr("DigiDollar Address Book"));
    }
    setMinimumSize(760, 380);
    setupUI();
    applyTheme();
    GUIUtil::handleCloseWindowShortcut(this);
}

DDAddressBookPage::~DDAddressBookPage()
{
}

void DDAddressBookPage::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);

    m_explanationLabel = new QLabel(this);
    m_explanationLabel->setWordWrap(true);
    m_explanationLabel->setText(tr("These are your DigiDollar addresses for sending payments. Always check the amount and the receiving address before sending coins."));
    m_mainLayout->addWidget(m_explanationLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Enter address or label to search"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DDAddressBookPage::onSearchTextChanged);
    m_mainLayout->addWidget(m_searchEdit);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(Column::ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Label"), tr("Address")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(Column::Label, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(Column::Address, QHeaderView::ResizeToContents);
    m_table->setToolTip(tr("Right-click to edit address or label"));

    connect(m_table, &QTableWidget::customContextMenuRequested, this, &DDAddressBookPage::showContextMenu);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DDAddressBookPage::selectionChanged);
    connect(m_table, &QTableWidget::doubleClicked, this, &DDAddressBookPage::accept);

    m_mainLayout->addWidget(m_table);

    m_buttonLayout = new QHBoxLayout();

    m_newButton = new QPushButton(tr("&New"), this);
    m_newButton->setToolTip(tr("Create a new DigiDollar address"));
    if (m_platformStyle && m_platformStyle->getImagesOnButtons()) {
        m_newButton->setIcon(m_platformStyle->SingleColorIcon(":/icons/add"));
    }
    connect(m_newButton, &QPushButton::clicked, this, &DDAddressBookPage::onNewAddress);

    m_copyButton = new QPushButton(tr("&Copy"), this);
    m_copyButton->setToolTip(tr("Copy the selected address to clipboard"));
    m_copyButton->setEnabled(false);
    if (m_platformStyle && m_platformStyle->getImagesOnButtons()) {
        m_copyButton->setIcon(m_platformStyle->SingleColorIcon(":/icons/editcopy"));
    }
    connect(m_copyButton, &QPushButton::clicked, this, &DDAddressBookPage::onCopyAddress);

    m_deleteButton = new QPushButton(tr("&Delete"), this);
    m_deleteButton->setToolTip(tr("Delete the selected address"));
    m_deleteButton->setEnabled(false);
    if (m_platformStyle && m_platformStyle->getImagesOnButtons()) {
        m_deleteButton->setIcon(m_platformStyle->SingleColorIcon(":/icons/remove"));
    }
    connect(m_deleteButton, &QPushButton::clicked, this, &DDAddressBookPage::onDeleteAddress);

    m_exportButton = new QPushButton(tr("&Export"), this);
    m_exportButton->setToolTip(tr("Export address list to CSV"));
    if (m_platformStyle && m_platformStyle->getImagesOnButtons()) {
        m_exportButton->setIcon(m_platformStyle->SingleColorIcon(":/icons/export"));
    }
    connect(m_exportButton, &QPushButton::clicked, this, &DDAddressBookPage::onExport);

    m_closeButton = new QPushButton(m_mode == ForSelection ? tr("C&hoose") : tr("C&lose"), this);
    connect(m_closeButton, &QPushButton::clicked, this, &DDAddressBookPage::accept);

    m_buttonLayout->addWidget(m_newButton);
    m_buttonLayout->addWidget(m_copyButton);
    m_buttonLayout->addWidget(m_deleteButton);
    m_buttonLayout->addStretch();

    if (m_mode == ForSelection) {
        m_exportButton->hide();
    } else {
        m_buttonLayout->addWidget(m_exportButton);
    }
    m_buttonLayout->addWidget(m_closeButton);

    m_mainLayout->addLayout(m_buttonLayout);

    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction(tr("&Copy Address"), this, &DDAddressBookPage::onCopyAddress);
    m_contextMenu->addAction(tr("Copy &Label"), this, [this]() {
        int row = m_table->currentRow();
        if (row >= 0) {
            QTableWidgetItem* item = m_table->item(row, Column::Label);
            if (item) QApplication::clipboard()->setText(item->text());
        }
    });
    m_contextMenu->addAction(tr("&Edit"), this, &DDAddressBookPage::onEditAddress);
    m_contextMenu->addAction(tr("&Delete"), this, &DDAddressBookPage::onDeleteAddress);

    setLayout(m_mainLayout);
}

void DDAddressBookPage::setWalletModel(WalletModel* model)
{
    m_walletModel = model;
    applyTheme();
    if (m_walletModel) {
        refreshAddressList();
    }
}

void DDAddressBookPage::applyTheme()
{
    setStyleSheet(DigiDollarAddressBookStyleSheet(
        UseDarkDigiDollarAddressBookTheme(m_walletModel, this)));
}

void DDAddressBookPage::refreshAddressList()
{
    if (!m_walletModel) return;

    m_table->setRowCount(0);
    m_table->setSortingEnabled(false);

    QString searchText = m_searchEdit ? m_searchEdit->text().toLower() : QString();

    for (const auto& addr : m_walletModel->wallet().getAddresses()) {
        if (addr.purpose == wallet::AddressPurpose::DIGIDOLLAR) {
            QString label = QString::fromStdString(addr.name);
            QString addressStr = QString::fromStdString(EncodeDigiDollarAddress(addr.dest));

            if (!searchText.isEmpty()) {
                if (!label.toLower().contains(searchText) && !addressStr.toLower().contains(searchText)) {
                    continue;
                }
            }

            int row = m_table->rowCount();
            m_table->insertRow(row);

            QTableWidgetItem* labelItem = new QTableWidgetItem(label);
            m_table->setItem(row, Column::Label, labelItem);

            QTableWidgetItem* addrItem = new QTableWidgetItem(addressStr);
            addrItem->setFont(GUIUtil::fixedPitchFont());
            m_table->setItem(row, Column::Address, addrItem);
        }
    }

    m_table->setSortingEnabled(true);
    selectionChanged();
}

void DDAddressBookPage::onSearchTextChanged(const QString& /*text*/)
{
    refreshAddressList();
}

QString DDAddressBookPage::getSelectedAddress() const
{
    int row = m_table->currentRow();
    if (row >= 0) {
        QTableWidgetItem* item = m_table->item(row, Column::Address);
        if (item) return item->text();
    }
    return QString();
}

void DDAddressBookPage::accept()
{
    if (m_mode == ForSelection) {
        m_returnValue = getSelectedAddress();
    }
    QDialog::accept();
}

void DDAddressBookPage::onNewAddress()
{
    if (!m_walletModel) return;

    bool ok;
    QString label = QInputDialog::getText(this, tr("New DigiDollar Address"),
        tr("Label:"), QLineEdit::Normal, QString(), &ok);

    if (ok) {
        QString newAddress = m_walletModel->getNewDigiDollarAddress(label);
        if (!newAddress.isEmpty()) {
            refreshAddressList();
        } else {
            QMessageBox::warning(this, tr("Error"),
                tr("Failed to create new DigiDollar address."));
        }
    }
}

void DDAddressBookPage::onEditAddress()
{
    if (!m_walletModel) return;

    int row = m_table->currentRow();
    if (row < 0) return;

    QTableWidgetItem* labelItem = m_table->item(row, Column::Label);
    QTableWidgetItem* addrItem = m_table->item(row, Column::Address);
    if (!labelItem || !addrItem) return;

    bool ok;
    QString newLabel = QInputDialog::getText(this, tr("Edit Label"),
        tr("Label:"), QLineEdit::Normal, labelItem->text(), &ok);

    if (ok && newLabel != labelItem->text()) {
        CTxDestination dest = DecodeDigiDollarAddress(addrItem->text().toStdString());
        if (IsValidDestination(dest)) {
            m_walletModel->wallet().setAddressBook(dest, newLabel.toStdString(),
                wallet::AddressPurpose::DIGIDOLLAR);
            refreshAddressList();
        }
    }
}

void DDAddressBookPage::onDeleteAddress()
{
    if (!m_walletModel) return;

    QString address = getSelectedAddress();
    if (address.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Delete Address"),
        tr("Are you sure you want to delete this address from your address book?\n\n%1")
        .arg(address), QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        CTxDestination dest = DecodeDigiDollarAddress(address.toStdString());
        if (IsValidDestination(dest)) {
            m_walletModel->wallet().delAddressBook(dest);
            refreshAddressList();
        }
    }
}

void DDAddressBookPage::onCopyAddress()
{
    QString address = getSelectedAddress();
    if (!address.isEmpty()) {
        QApplication::clipboard()->setText(address);
    }
}

void DDAddressBookPage::onExport()
{
    QString filename = QFileDialog::getSaveFileName(this,
        tr("Export DigiDollar Address Book"),
        QString(),
        tr("Comma separated file") + QLatin1String(" (*.csv)"));

    if (filename.isEmpty()) return;

    if (!filename.endsWith(".csv", Qt::CaseInsensitive)) {
        filename += ".csv";
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Could not open file %1 for writing.").arg(filename));
        return;
    }

    QTextStream out(&file);
    out << "\"Label\",\"Address\"\n";

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QString label = m_table->item(row, Column::Label)->text();
        QString address = m_table->item(row, Column::Address)->text();
        label.replace("\"", "\"\"");
        out << "\"" << label << "\",\"" << address << "\"\n";
    }

    file.close();

    if (file.error() == QFile::NoError) {
        QMessageBox::information(this, tr("Export Successful"),
            tr("Address book was successfully saved to %1.").arg(filename));
    }
}

void DDAddressBookPage::showContextMenu(const QPoint& pos)
{
    if (m_table->currentRow() >= 0) {
        m_contextMenu->popup(m_table->viewport()->mapToGlobal(pos));
    }
}

void DDAddressBookPage::selectionChanged()
{
    bool hasSelection = m_table->currentRow() >= 0;
    m_copyButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}
