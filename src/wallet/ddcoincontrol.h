// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_WALLET_DDCOINCONTROL_H
#define DIGIBYTE_WALLET_DDCOINCONTROL_H

#include <primitives/transaction.h>
#include <consensus/amount.h>

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace wallet {

/**
 * DigiDollar Coin Control Features.
 * Similar to CCoinControl but specifically for DigiDollar UTXOs.
 * Allows users to manually select which DD UTXOs to use in transactions.
 */
class DDCoinControl
{
public:
    //! If true, the selection process can add extra unselected DD inputs while requiring all selected inputs be used
    bool m_allow_other_inputs = true;

    //! Minimum chain depth value for DD UTXO availability
    int m_min_depth = 0;

    //! Maximum chain depth value for DD UTXO availability
    int m_max_depth = 9999999;

    DDCoinControl();

    /**
     * Returns true if there are pre-selected DD UTXOs.
     */
    bool HasSelected() const;

    /**
     * Returns true if the given DD UTXO is pre-selected.
     */
    bool IsSelected(const COutPoint& output) const;

    /**
     * Lock-in the given DD UTXO for spending.
     * The UTXO will be included in the transaction even if it's not the most optimal choice.
     */
    void Select(const COutPoint& output);

    /**
     * Unselects the given DD UTXO.
     */
    void UnSelect(const COutPoint& output);

    /**
     * Unselects all DD UTXOs.
     */
    void UnSelectAll();

    /**
     * List the selected DD UTXOs.
     */
    std::vector<COutPoint> ListSelected() const;

private:
    //! Selected DD UTXOs (inputs that will be used, regardless of whether they're optimal or not)
    std::set<COutPoint> m_selected_inputs;
};

} // namespace wallet

#endif // DIGIBYTE_WALLET_DDCOINCONTROL_H
