// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_QT_DDADDRESSBOOKPAGE_H
#define DIGIBYTE_QT_DDADDRESSBOOKPAGE_H

#include <QDialog>

class WalletModel;
class PlatformStyle;

QT_BEGIN_NAMESPACE
class QTableWidget;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QLabel;
class QMenu;
class QLineEdit;
QT_END_NAMESPACE

class DDAddressBookPage : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        ForSelection,
        ForEditing
    };

    explicit DDAddressBookPage(const PlatformStyle *platformStyle, Mode mode, QWidget* parent = nullptr);
    ~DDAddressBookPage();

    void setWalletModel(WalletModel* model);
    const QString& getReturnValue() const { return m_returnValue; }

Q_SIGNALS:
    void sendCoins(QString addr);

public Q_SLOTS:
    void accept() override;

private Q_SLOTS:
    void onNewAddress();
    void onEditAddress();
    void onDeleteAddress();
    void onCopyAddress();
    void onExport();
    void showContextMenu(const QPoint& pos);
    void selectionChanged();
    void onSearchTextChanged(const QString& text);

private:
    void applyTheme();
    void setupUI();
    void refreshAddressList();
    QString getSelectedAddress() const;

    Mode m_mode;
    WalletModel* m_walletModel;
    const PlatformStyle* m_platformStyle;
    QString m_returnValue;

    QVBoxLayout* m_mainLayout;
    QLabel* m_explanationLabel;
    QLineEdit* m_searchEdit;
    QTableWidget* m_table;
    QHBoxLayout* m_buttonLayout;
    QPushButton* m_newButton;
    QPushButton* m_copyButton;
    QPushButton* m_deleteButton;
    QPushButton* m_exportButton;
    QPushButton* m_closeButton;
    QMenu* m_contextMenu;

    enum Column {
        Label = 0,
        Address,
        ColumnCount
    };
};

#endif // DIGIBYTE_QT_DDADDRESSBOOKPAGE_H
