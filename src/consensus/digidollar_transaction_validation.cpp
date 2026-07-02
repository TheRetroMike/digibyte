// Copyright (c) 2024 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/digidollar_transaction_validation.h>

#include <consensus/digidollar.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <primitives/transaction.h>
#include <base58.h>
#include <util/strencodings.h>

#include <algorithm>
#include <set>

// =====================================
// Mint Validation Functions
// =====================================

bool ValidateMintAmount(CAmount amount, const DigiDollar::ConsensusParams& ddParams) {
    return DigiDollar::IsValidMintAmount(amount, ddParams);
}

bool ValidateCollateralRatio(CAmount ddAmount, CAmount collateralAmount, CAmount oraclePrice, int requiredRatio) {
    if (oraclePrice <= 0) return false;
    if (ddAmount <= 0) return false;
    if (collateralAmount <= 0) return false;

    // Calculate required collateral value in cents
    // ddAmount is in cents (100 cents = $1.00)
    // requiredRatio is percentage (200 = 200%)
    // oraclePrice is cents per DGB (5000 = $50.00 per DGB)
    // collateralAmount is in DGB satoshis (100000000 satoshis = 1 DGB)

    // Required USD value = ddAmount * requiredRatio / 100
    CAmount requiredUSDCents = (ddAmount * requiredRatio) / 100;

    // Collateral value in cents = (collateralAmount / COIN) * oraclePrice
    // = collateralAmount * oraclePrice / COIN
    CAmount collateralValueCents = (collateralAmount * oraclePrice) / COIN;

    return collateralValueCents >= requiredUSDCents;
}

bool ValidateOraclePrice(CAmount price) {
    // Price must be positive
    if (price <= 0) return false;

    // Price must be within reasonable bounds (1 cent to $10 per DGB)
    const CAmount MIN_PRICE = 100;    // 1 cent
    const CAmount MAX_PRICE = 1000000; // $10

    return price >= MIN_PRICE && price <= MAX_PRICE;
}

// =====================================
// Transfer Validation Functions
// =====================================

bool ValidateDDConservation(CAmount inputs, CAmount outputs, CAmount fee) {
    // Inputs must equal outputs plus fee (conservation of DD)
    return inputs == (outputs + fee);
}

bool ValidateDDAddress(const std::string& address) {
    // Basic format validation for DD addresses
    // DD addresses start with "dd1" (bech32 format)
    // Minimum length is 4 ("dd1" + at least 1 character)
    if (address.length() < 4) return false;
    if (address.substr(0, 3) != "dd1") return false;

    // Additional validation would check bech32 encoding
    // For now, just check basic format
    return true;
}

bool ValidateNoDoubleSpend(const std::vector<COutPoint>& inputs1, const std::vector<COutPoint>& inputs2) {
    std::set<COutPoint> set1(inputs1.begin(), inputs1.end());
    std::set<COutPoint> set2(inputs2.begin(), inputs2.end());

    // Check for intersection (common elements indicate double spend)
    std::vector<COutPoint> intersection;
    std::set_intersection(set1.begin(), set1.end(),
                         set2.begin(), set2.end(),
                         std::back_inserter(intersection));

    return !intersection.empty(); // Return true if double spend detected
}

std::vector<size_t> SelectDDUTXOs(const std::vector<CAmount>& amounts, CAmount target) {
    std::vector<size_t> selected;
    CAmount total = 0;

    // Simple greedy selection - largest first
    std::vector<std::pair<CAmount, size_t>> indexed_amounts;
    for (size_t i = 0; i < amounts.size(); ++i) {
        indexed_amounts.emplace_back(amounts[i], i);
    }

    std::sort(indexed_amounts.rbegin(), indexed_amounts.rend());

    for (const auto& [amount, index] : indexed_amounts) {
        if (total >= target) break;
        selected.push_back(index);
        total += amount;
    }

    return selected;
}

// =====================================
// Redeem Validation Functions
// =====================================

bool ValidateRedemptionPath(DigiDollarTxType type, int currentHeight, int lockHeight, bool errActive) {
    // NOTE: Only DD_TX_REDEEM exists. ERR is handled via burn amount, not tx type.
    // Both NORMAL and ERR paths require timelock expiry.
    if (type != DD_TX_REDEEM) {
        return false;
    }
    // Timelock must be expired for all redemptions
    return currentHeight >= lockHeight;
}

bool ValidateRedemptionAmount(CAmount redeemAmount, CAmount totalHeld, bool isFullRedeem) {
    // DigiDollar only supports FULL redemption - no partial redemption allowed
    if (redeemAmount <= 0) return false;
    if (totalHeld <= 0) return false;

    if (isFullRedeem) {
        // Full redemption must match exactly
        return redeemAmount == totalHeld;
    } else {
        // Partial redemption is NOT allowed in DigiDollar
        // All redemptions must be full redemptions
        return false;
    }
}

bool ValidateTimelockForRedeem(DigiDollarTxType type, int currentHeight, int lockHeight, bool /* errActive */) {
    // NOTE: Only DD_TX_REDEEM exists. Both NORMAL and ERR paths require timelock expiry.
    // There is NO early redemption, NO partial redemption, NO emergency oracle override.
    if (type != DD_TX_REDEEM) {
        return false;
    }
    // Timelock must be expired for all redemptions
    return currentHeight >= lockHeight;
}

bool ShouldActivateERR(int collateralPercentage) {
    const int ERR_THRESHOLD = 100; // 100% collateral threshold
    return collateralPercentage < ERR_THRESHOLD;
}

// =====================================
// Script Execution Functions
// =====================================

CScript CreateDDOutputScript(CAmount amount, int64_t lockBlocks) {
    CScript script;

    // Embed DD amount and lock time in script
    script << amount;
    script << lockBlocks;

    // Add P2TR-like structure (simplified)
    script << OP_1; // Version 1
    script << OP_DUP;
    script << OP_HASH256;

    // Add 32-byte hash placeholder
    std::vector<unsigned char> hash(32, 0x42);
    script << hash;

    script << OP_EQUALVERIFY;
    script << OP_CHECKSIG;

    return script;
}

bool ValidateDDScript(const CScript& script) {
    if (script.empty()) return false;

    // Basic structure validation
    // DD scripts should have minimum required elements
    if (script.size() < 10) return false;

    return true;
}

bool ValidateDDWitnessStack(const std::vector<std::vector<unsigned char>>& stack) {
    // DD witness stack requires at least 3 elements:
    // 1. DD amount (8 bytes)
    // 2. Lock time (8 bytes)
    // 3. Signature (64 bytes)
    if (stack.size() < 3) return false;

    if (stack[0].size() != 8) return false; // DD amount
    if (stack[1].size() != 8) return false; // Lock time
    if (stack[2].size() != 64) return false; // Signature

    // Optional 4th element (e.g., pubkey) but must not be empty
    if (stack.size() >= 4) {
        // Check that any additional elements are not empty
        for (size_t i = 3; i < stack.size(); ++i) {
            if (stack[i].empty()) return false; // Empty elements not allowed
        }
    }

    return true;
}

ScriptExecutionResult ExecuteDDScript(const CScript& script) {
    ScriptExecutionResult result;
    result.success = false;
    result.stackSize = 0;

    // Empty script is valid and succeeds
    if (script.empty()) {
        result.success = true;
        result.stackSize = 0;
        return result;
    }

    // Simplified script execution for testing
    std::vector<std::vector<unsigned char>> stack;

    try {
        // Basic opcode processing
        CScript::const_iterator pc = script.begin();
        while (pc != script.end()) {
            opcodetype opcode;
            if (!script.GetOp(pc, opcode)) {
                break;
            }

            switch (opcode) {
                case OP_1:
                    stack.push_back({1});
                    break;
                case OP_2:
                    stack.push_back({2});
                    break;
                case OP_3:
                    stack.push_back({3});
                    break;
                case OP_4:
                    stack.push_back({4});
                    break;
                case OP_ADD:
                    if (stack.size() >= 2) {
                        auto b = stack.back(); stack.pop_back();
                        auto a = stack.back(); stack.pop_back();
                        if (!a.empty() && !b.empty()) {
                            int sum = a[0] + b[0];
                            stack.push_back({static_cast<unsigned char>(sum)});
                        }
                    }
                    break;
                case OP_EQUAL:
                    if (stack.size() >= 2) {
                        auto b = stack.back(); stack.pop_back();
                        auto a = stack.back(); stack.pop_back();
                        bool equal = (a == b);
                        stack.push_back(equal ? std::vector<unsigned char>{1} : std::vector<unsigned char>{0});
                    }
                    break;
                default:
                    // Unknown opcode - fail execution
                    result.stackSize = stack.size();
                    result.success = false;
                    return result;
            }
        }

        result.stackSize = stack.size();

        // Script succeeds if stack is empty OR top element is true (non-zero)
        if (stack.empty()) {
            result.success = true;
        } else if (!stack.back().empty() && stack.back()[0] != 0) {
            result.success = true;
        } else {
            result.success = false;
        }

    } catch (...) {
        result.success = false;
    }

    return result;
}

bool ValidateDDOpcode(opcodetype opcode) {
    // List of allowed opcodes in DD scripts
    switch (opcode) {
        case OP_0:
        case OP_1:
        case OP_2:
        case OP_3:
        case OP_4:
        case OP_5:
        case OP_6:
        case OP_7:
        case OP_8:
        case OP_9:
        case OP_10:
        case OP_11:
        case OP_12:
        case OP_13:
        case OP_14:
        case OP_15:
        case OP_16:
        case OP_PUSHDATA1:
        case OP_PUSHDATA2:
        case OP_PUSHDATA4:
        case OP_1NEGATE:
        case OP_NOP:
        case OP_IF:
        case OP_NOTIF:
        case OP_ELSE:
        case OP_ENDIF:
        case OP_VERIFY:
        case OP_RETURN:
        case OP_TOALTSTACK:
        case OP_FROMALTSTACK:
        case OP_2DROP:
        case OP_2DUP:
        case OP_3DUP:
        case OP_2OVER:
        case OP_2ROT:
        case OP_2SWAP:
        case OP_IFDUP:
        case OP_DEPTH:
        case OP_DROP:
        case OP_DUP:
        case OP_NIP:
        case OP_OVER:
        case OP_PICK:
        case OP_ROLL:
        case OP_ROT:
        case OP_SWAP:
        case OP_TUCK:
        case OP_SIZE:
        case OP_EQUAL:
        case OP_EQUALVERIFY:
        case OP_1ADD:
        case OP_1SUB:
        case OP_NEGATE:
        case OP_ABS:
        case OP_NOT:
        case OP_0NOTEQUAL:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_BOOLAND:
        case OP_BOOLOR:
        case OP_NUMEQUAL:
        case OP_NUMEQUALVERIFY:
        case OP_NUMNOTEQUAL:
        case OP_LESSTHAN:
        case OP_GREATERTHAN:
        case OP_LESSTHANOREQUAL:
        case OP_GREATERTHANOREQUAL:
        case OP_MIN:
        case OP_MAX:
        case OP_WITHIN:
        case OP_RIPEMD160:
        case OP_SHA1:
        case OP_SHA256:
        case OP_HASH160:
        case OP_HASH256:
        case OP_CHECKSIG:
        case OP_CHECKSIGVERIFY:
            return true;

        // Specifically disallowed opcodes
        case OP_CHECKMULTISIG:
        case OP_CHECKMULTISIGVERIFY:
        case OP_CHECKLOCKTIMEVERIFY:
        case OP_CHECKSEQUENCEVERIFY:
            return false;

        default:
            // Conservative approach - disallow unknown opcodes
            return false;
    }
}