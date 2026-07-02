// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_QT_DIGIDOLLARTAB_H
#define DIGIBYTE_QT_DIGIDOLLARTAB_H

#include <QWidget>

class DigiDollarOverviewWidget;
class DigiDollarReceiveWidget;
class DigiDollarSendWidget;
class DigiDollarMintWidget;
class DigiDollarRedeemWidget;
class DigiDollarPositionsWidget;
class DigiDollarTransactionsWidget;
class WalletModel;
class ClientModel;
class PlatformStyle;

QT_BEGIN_NAMESPACE
class QTabWidget;
class QVBoxLayout;
class QLabel;
class QTimer;
class QStackedWidget;
QT_END_NAMESPACE

/**
 * DigiDollar main tab widget containing all DigiDollar functionality.
 * This widget provides access to DigiDollar overview, sending, minting,
 * redeeming, and position management functionality.
 */
class DigiDollarTab : public QWidget
{
    Q_OBJECT

public:
    explicit DigiDollarTab(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~DigiDollarTab();

    void setWalletModel(WalletModel* model);
    void setClientModel(ClientModel* model);
    void updateView();

    /** Show incoming DigiDollar transaction notification */
    void incomingDDTransaction(const QString& date, const QString& amount,
                               const QString& type, const QString& address);

Q_SIGNALS:
    /** Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);

public Q_SLOTS:
    /** Update balance displays across all widgets */
    void updateBalance();
    /** Update oracle price displays */
    void updateOraclePrice();
    /** Update system health status */
    void updateSystemHealth();
    /** Update positions table */
    void updatePositions();
    /** Set privacy mode — relays to all sub-widgets */
    void setPrivacy(bool privacy);

private Q_SLOTS:
    /** Handle tab change to update the active widget */
    void onTabChanged(int index);
    /** Handle redeem request from Vault tab */
    void onRedeemRequested(const QString &positionId);
    /** Open and focus a transaction in the DD Transactions tab */
    void showTransaction(const QString& txid);
    /** Check DigiDollar activation status and update UI */
    void checkActivationStatus();

private:
    void setupUI();
    void connectSignals();
    /** Query the current BIP9 deployment status string */
    QString getDeploymentStatus() const;
    /** Check if DigiDollar is active via the node */
    bool isDigiDollarActive() const;

    // UI components
    QTabWidget* m_tabWidget;
    QVBoxLayout* m_mainLayout;
    QStackedWidget* m_stackedWidget;
    QLabel* m_activationLabel;
    QTimer* m_activationTimer;
    bool m_activated;

    // Sub-widgets
    DigiDollarOverviewWidget* m_overviewWidget;
    DigiDollarReceiveWidget* m_receiveWidget;
    DigiDollarSendWidget* m_sendWidget;
    DigiDollarMintWidget* m_mintWidget;
    DigiDollarRedeemWidget* m_redeemWidget;
    DigiDollarPositionsWidget* m_positionsWidget;
    DigiDollarTransactionsWidget* m_transactionsWidget;

    // Privacy
    bool m_privacy{false};

    // Models
    WalletModel* m_walletModel;
    ClientModel* m_clientModel;
    const PlatformStyle* m_platformStyle;
};

#endif // DIGIBYTE_QT_DIGIDOLLARTAB_H
