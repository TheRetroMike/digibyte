// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_RPC_DIGIDOLLAR_H
#define DIGIBYTE_RPC_DIGIDOLLAR_H

#include <rpc/server.h>

class CRPCTable;

// DigiDollar RPC command declarations

// System monitoring commands
RPCHelpMan getdigidollarstats();
RPCHelpMan getdcamultiplier();
RPCHelpMan calculatecollateralrequirement();
RPCHelpMan getdigidollarstatus();

// Core transaction commands (Task 5.7)
RPCHelpMan mintdigidollar();
RPCHelpMan senddigidollar();
RPCHelpMan sendmanydigidollar();
RPCHelpMan redeemdigidollar();
RPCHelpMan listdigidollarpositions();

// Address management commands (Task 5.7)
RPCHelpMan getdigidollaraddress();
RPCHelpMan validateddaddress();
RPCHelpMan listdigidollaraddresses();
RPCHelpMan importdigidollaraddress();

// Utility commands (Task 5.8)
RPCHelpMan getdigidollarbalance();
RPCHelpMan estimatecollateral();
RPCHelpMan getredemptioninfo();
RPCHelpMan listdigidollartxs();
RPCHelpMan listdigidollarunspent();
RPCHelpMan listdigidollarutxos();
RPCHelpMan getoracleprice();
RPCHelpMan getprotectionstatus();

// Oracle key management
RPCHelpMan createoraclekey();
RPCHelpMan exportoracleprivkey();
RPCHelpMan importoracleprivkey();
RPCHelpMan startoracle();

// Register all DigiDollar RPC commands
void RegisterDigiDollarRPCCommands(CRPCTable& t);

#endif // DIGIBYTE_RPC_DIGIDOLLAR_H
