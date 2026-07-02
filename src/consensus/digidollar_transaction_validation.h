// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DIGIBYTE_CONSENSUS_DIGIDOLLAR_TRANSACTION_VALIDATION_H
#define DIGIBYTE_CONSENSUS_DIGIDOLLAR_TRANSACTION_VALIDATION_H

#include <consensus/amount.h>
#include <consensus/digidollar.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <string>
#include <vector>

// Forward declarations

// =====================================
// Mint Validation Functions
// =====================================

/**
 * Validate mint amount against consensus parameters.
 * Checks minimum and maximum limits for minting operations.
 */
bool ValidateMintAmount(CAmount amount, const DigiDollar::ConsensusParams& ddParams);

/**
 * Validate collateral ratio for minting.
 * Ensures sufficient collateral is provided for the requested DD amount.
 */
bool ValidateCollateralRatio(CAmount ddAmount, CAmount collateralAmount, CAmount oraclePrice, int requiredRatio);

/**
 * Validate oracle price data.
 * Ensures price is positive and within reasonable bounds.
 */
bool ValidateOraclePrice(CAmount price);

// =====================================
// Transfer Validation Functions
// =====================================

/**
 * Validate DigiDollar conservation in transfers.
 * Ensures inputs equal outputs plus fees (no money creation/destruction).
 */
bool ValidateDDConservation(CAmount inputs, CAmount outputs, CAmount fee);

/**
 * Validate DigiDollar address format.
 * Checks address syntax and format validity.
 */
bool ValidateDDAddress(const std::string& address);

/**
 * Validate no double-spend in transaction inputs.
 * Checks for conflicting inputs between transactions.
 */
bool ValidateNoDoubleSpend(const std::vector<COutPoint>& inputs1, const std::vector<COutPoint>& inputs2);

/**
 * Select UTXOs for DigiDollar transaction.
 * Implements coin selection algorithm for DD transactions.
 */
std::vector<size_t> SelectDDUTXOs(const std::vector<CAmount>& amounts, CAmount target);

// =====================================
// Redeem Validation Functions
// =====================================

/**
 * Validate redemption path based on transaction type and conditions.
 * Checks if the specified redemption type is valid given current state.
 */
bool ValidateRedemptionPath(DigiDollarTxType type, int currentHeight, int lockHeight, bool errActive);

/**
 * Validate redemption amount.
 * Ensures redemption amount is valid relative to holdings and type.
 */
bool ValidateRedemptionAmount(CAmount redeemAmount, CAmount totalHeld, bool isFullRedeem);

/**
 * Validate timelock conditions for redemption.
 * Checks timelock requirements based on redemption type.
 */
bool ValidateTimelockForRedeem(DigiDollarTxType type, int currentHeight, int lockHeight, bool errActive = false);

/**
 * Check if Emergency Recovery Reserve (ERR) should be activated.
 * Determines if system collateral is below emergency threshold.
 */
bool ShouldActivateERR(int collateralPercentage);

// =====================================
// Script Execution Functions
// =====================================

/**
 * Create DigiDollar output script.
 * Generates P2TR script for DD output with embedded amount and timelock.
 */
CScript CreateDDOutputScript(CAmount amount, int64_t lockBlocks);

/**
 * Validate DigiDollar script structure.
 * Ensures script follows DD format requirements.
 */
bool ValidateDDScript(const CScript& script);

/**
 * Validate DigiDollar witness stack.
 * Ensures witness data is properly formatted and complete.
 */
bool ValidateDDWitnessStack(const std::vector<std::vector<unsigned char>>& stack);

/**
 * Script execution result structure.
 * Contains execution status and stack information.
 */
struct ScriptExecutionResult {
    bool success;
    size_t stackSize;
};

/**
 * Execute DigiDollar script with validation.
 * Runs script execution with DD-specific validation rules.
 */
ScriptExecutionResult ExecuteDDScript(const CScript& script);

/**
 * Validate opcode for DigiDollar context.
 * Checks if opcode is allowed in DD scripts.
 */
bool ValidateDDOpcode(opcodetype opcode);

#endif // DIGIBYTE_CONSENSUS_DIGIDOLLAR_TRANSACTION_VALIDATION_H