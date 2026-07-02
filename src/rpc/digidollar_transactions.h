// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_RPC_DIGIDOLLAR_TRANSACTIONS_H
#define DIGIBYTE_RPC_DIGIDOLLAR_TRANSACTIONS_H

#include <rpc/util.h>
#include <univalue.h>

class CRPCCommand;

// Minimal RPC interface for DigiDollar transactions
// These are placeholder implementations for the GREEN phase

/**
 * Get DigiDollar system information
 */
UniValue getdigidollarinfo(const JSONRPCRequest& request);

/**
 * Generate a new DigiDollar address
 */
UniValue getdigidollaraddress(const JSONRPCRequest& request);

/**
 * Get DigiDollar balance
 */
UniValue getdigidollarbalance(const JSONRPCRequest& request);

/**
 * Mint DigiDollar tokens
 */
UniValue mintdigidollar(const JSONRPCRequest& request);

/**
 * Transfer DigiDollar tokens
 */
UniValue transferdigidollar(const JSONRPCRequest& request);

/**
 * Redeem DigiDollar tokens
 */
UniValue redeemdigidollar(const JSONRPCRequest& request);

/**
 * Get redemption information for a collateral position
 */
UniValue getredemptioninfo(const JSONRPCRequest& request);

/**
 * List all redeemable collateral positions
 */
UniValue listredeemablepositions(const JSONRPCRequest& request);

/**
 * Set mock oracle price (for testing)
 */
UniValue setmockoracleprice(const JSONRPCRequest& request);

/**
 * Create raw DigiDollar transaction
 */
UniValue createrawddtransaction(const JSONRPCRequest& request);

// Get DigiDollar transaction RPC commands span
#include <span.h>
Span<const CRPCCommand> GetDigiDollarTransactionRPCCommands();

#endif // DIGIBYTE_RPC_DIGIDOLLAR_TRANSACTIONS_H