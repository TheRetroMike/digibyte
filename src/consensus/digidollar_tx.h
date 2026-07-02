// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_CONSENSUS_DIGIDOLLAR_TX_H
#define DIGIBYTE_CONSENSUS_DIGIDOLLAR_TX_H

#include <primitives/transaction.h>
#include <digidollar/validation.h>
#include <string>

/** Validate a DigiDollar transaction type */
bool IsValidDigiDollarType(DigiDollar::DigiDollarTxType type);

/** Validate DigiDollar transaction structure */
bool ValidateDigiDollarTxStructure(const CTransaction& tx, std::string& strError);

#endif // DIGIBYTE_CONSENSUS_DIGIDOLLAR_TX_H