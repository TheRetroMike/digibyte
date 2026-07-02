// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar_tx.h>
#include <primitives/transaction.h>
#include <digidollar/validation.h>

bool IsValidDigiDollarType(DigiDollar::DigiDollarTxType type) {
    // Valid types are MINT(1), TRANSFER(2), REDEEM(3) - not NONE(0) or MAX(4)
    return type >= DigiDollar::DD_TX_MINT && type < DigiDollar::DD_TX_MAX;
}

bool ValidateDigiDollarTxStructure(const CTransaction& tx, std::string& strError) {
    if (!DigiDollar::HasDigiDollarMarker(tx)) {
        return true; // Not DD, skip validation
    }

    DigiDollar::DigiDollarTxType type = DigiDollar::GetDigiDollarTxType(tx);
    if (!IsValidDigiDollarType(type)) {
        strError = "Invalid DigiDollar transaction type";
        return false;
    }

    // Type-specific structure validation
    switch(type) {
        case DigiDollar::DD_TX_MINT:
            // Must have collateral output and DD output
            // TODO: Validate specific structure when DD output format is defined
            break;
        case DigiDollar::DD_TX_TRANSFER:
            // DD inputs must equal DD outputs
            // TODO: Validate specific structure when DD output format is defined
            break;
        case DigiDollar::DD_TX_REDEEM:
            // Must burn DD and release collateral (ERR handled via burn amount)
            // TODO: Validate specific structure when DD output format is defined
            break;
        default:
            break;
    }

    return true;
}