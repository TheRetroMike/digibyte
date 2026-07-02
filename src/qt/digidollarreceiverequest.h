// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_QT_DIGIDOLLARRECEIVEREQUEST_H
#define DIGIBYTE_QT_DIGIDOLLARRECEIVEREQUEST_H

#include <qt/sendcoinsrecipient.h>

#include <QDialog>

class WalletModel;
class QRImageWidget;

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

/**
 * Dialog showing DigiDollar payment request details with QR code.
 * Similar to ReceiveRequestDialog but customized for DigiDollar.
 */
class DigiDollarReceiveRequestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DigiDollarReceiveRequestDialog(QWidget *parent = nullptr);
    ~DigiDollarReceiveRequestDialog();

    void setModel(WalletModel *model);
    void setInfo(const SendCoinsRecipient &info);
    /** Test hook for save-failure handling without opening a file dialog. */
    QString saveQRImageForTesting(const QString& fileName) const;

private Q_SLOTS:
    void onCopyURIClicked();
    void onCopyAddressClicked();
    void onSaveQRClicked();
    void updateDisplayUnit();

private:
    void setupUI();
    void applyTheme();
    QString linkColor() const;
    void updateUriContent();
    QString formatDDAmount(CAmount amount) const;
    QString formatDDURI(const SendCoinsRecipient &info) const;
    bool saveQRImageToFile(const QString& fileName, QString& error) const;

    // UI Components
    QLabel* m_titleLabel;
    QRImageWidget* m_qrWidget;

    QLabel* m_uriTagLabel;
    QLabel* m_uriContent;

    QLabel* m_addressTagLabel;
    QLabel* m_addressContent;

    QLabel* m_amountTagLabel;
    QLabel* m_amountContent;

    QLabel* m_labelTagLabel;
    QLabel* m_labelContent;

    QLabel* m_messageTagLabel;
    QLabel* m_messageContent;

    QLabel* m_walletTagLabel;
    QLabel* m_walletContent;

    QPushButton* m_copyURIButton;
    QPushButton* m_copyAddressButton;
    QPushButton* m_saveQRButton;
    QPushButton* m_verifyButton;

    WalletModel* m_model;
    SendCoinsRecipient m_info;
};

#endif // DIGIBYTE_QT_DIGIDOLLARRECEIVEREQUEST_H
