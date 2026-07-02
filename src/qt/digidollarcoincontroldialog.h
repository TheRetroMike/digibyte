// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_QT_DIGIDOLLARCOINCONTROLDIALOG_H
#define DIGIBYTE_QT_DIGIDOLLARCOINCONTROLDIALOG_H

#include <consensus/amount.h>

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class PlatformStyle;
class WalletModel;

namespace wallet {
class DDCoinControl;
} // namespace wallet

namespace Ui {
    class DigiDollarCoinControlDialog;
}

#define ASYMP_UTF8 "\xE2\x89\x88"

class DigiDollarCoinControlWidgetItem : public QTreeWidgetItem
{
public:
    explicit DigiDollarCoinControlWidgetItem(QTreeWidget *parent, int type = Type) : QTreeWidgetItem(parent, type) {}
    explicit DigiDollarCoinControlWidgetItem(QTreeWidgetItem *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    bool operator<(const QTreeWidgetItem &other) const override;
};

/**
 * DigiDollar Coin Control Dialog
 * Allows manual selection of DD UTXOs for sending or redeeming
 */
class DigiDollarCoinControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DigiDollarCoinControlDialog(wallet::DDCoinControl& coin_control, WalletModel* model, const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~DigiDollarCoinControlDialog();

    // static because also called from send/redeem widgets
    static void updateLabels(wallet::DDCoinControl& m_coin_control, WalletModel*, QDialog*);

    static QList<CAmount> payAmounts;

protected:
    void changeEvent(QEvent* e) override;

private:
    Ui::DigiDollarCoinControlDialog *ui;
    wallet::DDCoinControl& m_coin_control;
    WalletModel *model;
    int sortColumn;
    Qt::SortOrder sortOrder;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;
    QAction* m_copy_transaction_outpoint_action;

    const PlatformStyle *platformStyle;

    void sortView(int, Qt::SortOrder);
    void updateView();

    enum
    {
        COLUMN_CHECKBOX = 0,
        COLUMN_AMOUNT,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_DATE,
        COLUMN_CONFIRMATIONS,
        COLUMN_TXID_VOUT,
    };

    enum
    {
        TxHashRole = Qt::UserRole,
        VOutRole
    };

    friend class DigiDollarCoinControlWidgetItem;

private Q_SLOTS:
    void showMenu(const QPoint &);
    void copyAmount();
    void copyLabel();
    void copyAddress();
    void copyTransactionOutpoint();
    void clipboardQuantity();
    void clipboardAmount();
    void radioTreeMode(bool);
    void radioListMode(bool);
    void viewItemChanged(QTreeWidgetItem*, int);
    void headerSectionClicked(int);
    void buttonBoxClicked(QAbstractButton*);
    void buttonSelectAllClicked();
};

#endif // DIGIBYTE_QT_DIGIDOLLARCOINCONTROLDIALOG_H
