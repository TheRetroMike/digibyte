// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/digidollartab.h>

#include <qt/digidollaroverviewwidget.h>
#include <qt/digidollarreceivewidget.h>
#include <qt/digidollarsendwidget.h>
#include <qt/digidollarmintwidget.h>
#include <qt/digidollarredeemwidget.h>
#include <qt/digidollarpositionswidget.h>
#include <qt/digidollartransactionswidget.h>
#include <qt/walletmodel.h>
#include <qt/clientmodel.h>
#include <qt/platformstyle.h>

#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QLabel>
#include <QStackedWidget>
#include <QFont>

#include <chainparams.h>
#include <consensus/params.h>
#include <digidollar/digidollar.h>
#include <interfaces/node.h>
#include <node/context.h>
#include <validation.h>
#include <versionbits.h>

DigiDollarTab::DigiDollarTab(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    m_tabWidget(nullptr),
    m_mainLayout(nullptr),
    m_overviewWidget(nullptr),
    m_receiveWidget(nullptr),
    m_sendWidget(nullptr),
    m_mintWidget(nullptr),
    m_redeemWidget(nullptr),
    m_positionsWidget(nullptr),
    m_transactionsWidget(nullptr),
    m_walletModel(nullptr),
    m_clientModel(nullptr),
    m_platformStyle(platformStyle),
    m_stackedWidget(nullptr),
    m_activationLabel(nullptr),
    m_activationTimer(nullptr),
    m_activated(false)
{
    setupUI();
    connectSignals();
}

DigiDollarTab::~DigiDollarTab()
{
    // Qt will handle cleanup of child widgets
}

void DigiDollarTab::setupUI()
{
    setObjectName("digiDollarTab");

    // Create main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("digiDollarSubTabs");
    m_tabWidget->tabBar()->setElideMode(Qt::ElideNone);      // Don't truncate tab text
    m_tabWidget->tabBar()->setExpanding(true);               // Expand tabs to fill width
    m_tabWidget->tabBar()->setUsesScrollButtons(true);       // Use scroll if needed

    // Create sub-widgets
    m_overviewWidget = new DigiDollarOverviewWidget(this);
    m_overviewWidget->setObjectName("overviewWidget");

    m_receiveWidget = new DigiDollarReceiveWidget();
    m_receiveWidget->setObjectName("receiveWidget");

    m_sendWidget = new DigiDollarSendWidget(m_platformStyle, this);
    m_sendWidget->setObjectName("sendWidget");

    m_mintWidget = new DigiDollarMintWidget(this);
    m_mintWidget->setObjectName("mintWidget");

    m_redeemWidget = new DigiDollarRedeemWidget(this);
    m_redeemWidget->setObjectName("redeemWidget");

    m_positionsWidget = new DigiDollarPositionsWidget(this);
    m_positionsWidget->setObjectName("positionsWidget");

    m_transactionsWidget = new DigiDollarTransactionsWidget(this);
    m_transactionsWidget->setObjectName("transactionsWidget");

    // Add tabs in order: $DD Overview, Send $DD, Receive $DD, Mint $DD, Redeem $DD, $DD Vault, $DD Transactions
    m_tabWidget->addTab(m_overviewWidget, tr("$DD Overview"));
    m_tabWidget->addTab(m_sendWidget, tr("Send $DD"));
    m_tabWidget->addTab(m_receiveWidget, tr("Receive $DD"));
    m_tabWidget->addTab(m_mintWidget, tr("Mint $DD"));
    m_tabWidget->addTab(m_redeemWidget, tr("Redeem $DD"));
    m_tabWidget->addTab(m_positionsWidget, tr("$DD Vault"));
    m_tabWidget->addTab(m_transactionsWidget, tr("$DD Transactions"));

    // Create activation status overlay
    m_activationLabel = new QLabel(this);
    m_activationLabel->setAlignment(Qt::AlignCenter);
    m_activationLabel->setWordWrap(true);
    m_activationLabel->setTextFormat(Qt::RichText);
    QFont labelFont = m_activationLabel->font();
    labelFont.setPointSize(14);
    m_activationLabel->setFont(labelFont);
    m_activationLabel->setStyleSheet("QLabel { color: #CCCCCC; padding: 40px; }");

    // Use stacked widget to switch between activation message and DD tabs
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName("digiDollarStack");
    m_stackedWidget->addWidget(m_activationLabel);  // index 0: activation message
    m_stackedWidget->addWidget(m_tabWidget);         // index 1: DD functionality

    // Add stacked widget to main layout
    m_mainLayout->addWidget(m_stackedWidget);

    setLayout(m_mainLayout);

    // Start activation check timer (every 5 seconds)
    m_activationTimer = new QTimer(this);
    connect(m_activationTimer, &QTimer::timeout, this, &DigiDollarTab::checkActivationStatus);
    m_activationTimer->start(5000);

    // Check immediately
    checkActivationStatus();
}

void DigiDollarTab::connectSignals()
{
    // Connect tab change signal
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &DigiDollarTab::onTabChanged);

    // Connect sub-widget signals
    if (m_overviewWidget) {
        connect(m_overviewWidget, &DigiDollarOverviewWidget::message,
                this, &DigiDollarTab::message);
        connect(m_overviewWidget, &DigiDollarOverviewWidget::recentTransactionActivated,
                this, &DigiDollarTab::showTransaction);
    }

    if (m_receiveWidget) {
        connect(m_receiveWidget, &DigiDollarReceiveWidget::message,
                this, &DigiDollarTab::message);
    }

    if (m_sendWidget) {
        connect(m_sendWidget, &DigiDollarSendWidget::message,
                this, &DigiDollarTab::message);
    }

    if (m_mintWidget) {
        connect(m_mintWidget, &DigiDollarMintWidget::message,
                this, &DigiDollarTab::message);
    }

    if (m_redeemWidget) {
        connect(m_redeemWidget, &DigiDollarRedeemWidget::message,
                this, &DigiDollarTab::message);
    }

    if (m_positionsWidget) {
        connect(m_positionsWidget, &DigiDollarPositionsWidget::message,
                this, &DigiDollarTab::message);
        connect(m_positionsWidget, &DigiDollarPositionsWidget::redeemRequested,
                this, &DigiDollarTab::onRedeemRequested);
    }

    if (m_transactionsWidget) {
        connect(m_transactionsWidget, &DigiDollarTransactionsWidget::message,
                this, &DigiDollarTab::message);
    }
}

void DigiDollarTab::setWalletModel(WalletModel* model)
{
    m_walletModel = model;

    // Pass wallet model to sub-widgets
    if (m_overviewWidget)
        m_overviewWidget->setWalletModel(model);
    if (m_receiveWidget)
        m_receiveWidget->setWalletModel(model);
    if (m_sendWidget)
        m_sendWidget->setWalletModel(model);
    if (m_mintWidget)
        m_mintWidget->setWalletModel(model);
    if (m_redeemWidget)
        m_redeemWidget->setWalletModel(model);
    if (m_positionsWidget)
        m_positionsWidget->setWalletModel(model);
    if (m_transactionsWidget)
        m_transactionsWidget->setWalletModel(model);

    // Keep the tab's cached DGB/DD balance displays live while the user stays
    // on DigiDollar. The signal's WalletBalances payload is intentionally
    // discarded, and the slot re-reads each child widget's current balance.
    if (m_walletModel) {
        connect(m_walletModel, &WalletModel::balanceChanged,
                this, &DigiDollarTab::updateBalance);
    }

    // Update view when wallet model changes
    updateView();
}

void DigiDollarTab::setClientModel(ClientModel* model)
{
    m_clientModel = model;

    // Pass client model to sub-widgets
    if (m_overviewWidget)
        m_overviewWidget->setClientModel(model);
    if (m_receiveWidget)
        m_receiveWidget->setClientModel(model);
    if (m_sendWidget)
        m_sendWidget->setClientModel(model);
    if (m_mintWidget)
        m_mintWidget->setClientModel(model);
    if (m_redeemWidget)
        m_redeemWidget->setClientModel(model);
    if (m_positionsWidget)
        m_positionsWidget->setClientModel(model);
    if (m_transactionsWidget)
        m_transactionsWidget->setClientModel(model);
}

void DigiDollarTab::updateView()
{
    // Update all sub-widgets
    if (m_overviewWidget)
        m_overviewWidget->updateView();
    if (m_receiveWidget)
        m_receiveWidget->updateView();
    if (m_sendWidget)
        m_sendWidget->updateView();
    if (m_mintWidget)
        m_mintWidget->updateView();
    if (m_redeemWidget)
        m_redeemWidget->updateView();
    if (m_positionsWidget)
        m_positionsWidget->updateView();
    if (m_transactionsWidget)
        m_transactionsWidget->updateView();
}

void DigiDollarTab::incomingDDTransaction(const QString& date, const QString& amount,
                                         const QString& type, const QString& address)
{
    // Notify overview widget of incoming transaction
    if (m_overviewWidget) {
        m_overviewWidget->incomingDDTransaction(date, amount, type, address);
    }

    // Update balance displays
    updateBalance();
}

void DigiDollarTab::updateBalance()
{
    if (m_overviewWidget)
        m_overviewWidget->updateBalance();
    if (m_sendWidget)
        m_sendWidget->updateBalance();
    if (m_mintWidget)
        m_mintWidget->updateBalance();
    if (m_redeemWidget)
        m_redeemWidget->updateBalance();
}

void DigiDollarTab::updateOraclePrice()
{
    if (m_overviewWidget)
        m_overviewWidget->updateOraclePrice();
    if (m_sendWidget)
        m_sendWidget->updateOraclePrice();
    if (m_mintWidget)
        m_mintWidget->updateOraclePrice();
}

void DigiDollarTab::updateSystemHealth()
{
    if (m_overviewWidget)
        m_overviewWidget->updateSystemHealth();
}

void DigiDollarTab::updatePositions()
{
    if (m_positionsWidget)
        m_positionsWidget->updatePositions();
    if (m_redeemWidget)
        m_redeemWidget->updatePositions();
}

void DigiDollarTab::onTabChanged(int index)
{
    // Update the active tab when switching
    switch (index) {
    case 0: // Overview
        if (m_overviewWidget)
            m_overviewWidget->updateView();
        break;
    case 1: // Send
        if (m_sendWidget)
            m_sendWidget->updateView();
        break;
    case 2: // Receive
        if (m_receiveWidget)
            m_receiveWidget->updateView();
        break;
    case 3: // Mint
        if (m_mintWidget)
            m_mintWidget->updateView();
        break;
    case 4: // Redeem
        if (m_redeemWidget)
            m_redeemWidget->updateView();
        break;
    case 5: // Vault
        if (m_positionsWidget)
            m_positionsWidget->updateView();
        break;
    case 6: // Transactions
        if (m_transactionsWidget)
            m_transactionsWidget->updateView();
        break;
    default:
        break;
    }
}

void DigiDollarTab::setPrivacy(bool privacy)
{
    m_privacy = privacy;

    // Relay privacy setting to all sub-widgets
    if (m_overviewWidget)
        m_overviewWidget->setPrivacy(privacy);
    if (m_sendWidget)
        m_sendWidget->setPrivacy(privacy);
    if (m_mintWidget)
        m_mintWidget->setPrivacy(privacy);
    if (m_redeemWidget)
        m_redeemWidget->setPrivacy(privacy);
    if (m_positionsWidget)
        m_positionsWidget->setPrivacy(privacy);
    if (m_transactionsWidget)
        m_transactionsWidget->setPrivacy(privacy);
}

void DigiDollarTab::onRedeemRequested(const QString &positionId)
{
    // Switch to Redeem tab (index 4) and populate the position ID
    if (m_redeemWidget) {
        m_redeemWidget->setPosition(positionId);
        m_tabWidget->setCurrentIndex(4); // Redeem tab
    }
}

void DigiDollarTab::showTransaction(const QString& txid)
{
    if (!m_tabWidget || !m_transactionsWidget || txid.isEmpty()) {
        return;
    }

    if (m_stackedWidget) {
        m_stackedWidget->setCurrentWidget(m_tabWidget);
    }
    m_tabWidget->setCurrentWidget(m_transactionsWidget);
    m_transactionsWidget->updateView();
    m_transactionsWidget->focusTransaction(txid);
}

void DigiDollarTab::checkActivationStatus()
{
    if (m_activated) {
        // Already activated, stop checking
        if (m_activationTimer) m_activationTimer->stop();
        return;
    }

    QString status = getDeploymentStatus();
    bool active = isDigiDollarActive();

    if (active) {
        m_activated = true;
        m_stackedWidget->setCurrentIndex(1); // Show DD tabs
        if (m_activationTimer) m_activationTimer->stop();
        // Trigger initial data load
        updateBalance();
        updateOraclePrice();
        updatePositions();
        return;
    }

    // Build activation status message
    QString msg = QString(
        "<div style='text-align: center;'>"
        "<h2 style='color: #0066CC;'>💎 DigiDollar</h2>"
        "<p style='font-size: 16px; margin: 20px 0;'>"
        "DigiDollar is not yet active on this blockchain.</p>"
        "<p style='font-size: 13px; color: #999999;'>"
        "BIP9 Deployment Status: <b>%1</b></p>"
        "<p style='font-size: 12px; color: #777777; margin-top: 20px;'>"
        "DigiDollar will activate after miners signal support.<br>"
        "Use <code>getdigidollardeploymentinfo</code> for details.</p>"
        "</div>"
    ).arg(status.toUpper());

    m_activationLabel->setText(msg);
    m_stackedWidget->setCurrentIndex(0); // Show activation message
}

QString DigiDollarTab::getDeploymentStatus() const
{
    if (!m_clientModel) return "unknown";

    try {
        interfaces::Node& node = m_clientModel->node();
        node::NodeContext* ctx = node.context();
        if (!ctx || !ctx->chainman) return "unknown";

        ChainstateManager& chainman = *ctx->chainman;
        const ThresholdState state = WITH_LOCK(cs_main, {
            const CBlockIndex* tip = chainman.ActiveChain().Tip();
            return chainman.m_versionbitscache.State(tip, chainman.GetConsensus(),
                                                     Consensus::DEPLOYMENT_DIGIDOLLAR);
        });

        switch (state) {
        case ThresholdState::DEFINED: return "defined";
        case ThresholdState::STARTED: return "started";
        case ThresholdState::LOCKED_IN: return "locked_in";
        case ThresholdState::ACTIVE: return "active";
        case ThresholdState::FAILED: return "failed";
        }
    } catch (...) {
        return "unknown";
    }

    return "unknown";
}

bool DigiDollarTab::isDigiDollarActive() const
{
    if (!m_clientModel) return false;

    try {
        interfaces::Node& node = m_clientModel->node();
        // Use the node's context to check actual BIP9 status
        node::NodeContext* ctx = node.context();
        if (!ctx || !ctx->chainman) return false;

        ChainstateManager& chainman = *ctx->chainman;
        const CBlockIndex* tip = WITH_LOCK(cs_main, return chainman.ActiveChain().Tip());
        if (!tip) return false;

        return DigiDollar::IsDigiDollarEnabled(tip, chainman);
    } catch (...) {
        return false;
    }
}
