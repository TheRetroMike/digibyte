# DigiDollar Test PRD (Product Requirements Document)

**Version**: 1.0  
**Date**: January 2, 2026  
**Status**: Implementation Ready  
**Purpose**: Comprehensive test plan for AI orchestrators and sub-agents

---

## Executive Summary

This document provides a complete test specification for DigiDollar functionality in DigiByte v8.26. It serves as an authoritative guide for AI agents implementing test coverage.

### Current Test Inventory

| Category | Files | Tests | Coverage |
|----------|-------|-------|----------|
| DigiDollar Unit Tests (C++) | 27 | 297 | 75% |
| Oracle Unit Tests (C++) | 9 | 149 | 85% |
| Functional Tests (Python) | 30 | 180+ | 90% |
| Qt GUI Tests | 2 | 8 | **70%** |
| **Total** | **68** | **634+** | **~85%** |

### Gaps Addressed in Recent Updates (Jan 2026)

1. ~~**Qt GUI Tests**: ZERO tests exist for 10 DigiDollar widgets~~ ✅ 7 tests + 1 skipped
2. ~~**Wallet Operations**: Backup/restore, encryption, descriptor migration untested~~ ✅ 4 test files
3. ~~**RPC Error Paths**: 8+ commands lack error case testing~~ ✅ Covered in new RPC tests
4. ~~**Address Management**: 3 RPC commands completely untested~~ ✅ digidollar_rpc_addresses.py

### Remaining Gaps

1. **transactionsWidgetTests**: Requires RPC mocking infrastructure (currently QSKIP'd)
2. **Oracle-only RPCs**: sendoracleprice, listoracles, startoracle, stoporacle, getoraclepubkey

---

## Part 1: Existing Test Inventory

### 1.1 DigiDollar Unit Tests (src/test/)

#### File: digidollar_mint_tests.cpp (28 tests)
```cpp
// Core minting functionality
mint_minimum_amount, mint_standard_amounts, mint_maximum_amount, mint_invalid_amounts
collateral_all_lock_tiers, collateral_calculation_consistency, collateral_price_dependency
collateral_insufficient_rejection, p2tr_script_creation_through_transaction
p2tr_redemption_paths_verification, transaction_version_field, transaction_input_consumption
transaction_output_creation, transaction_fee_calculation, transaction_signing_preparation
edge_case_exact_collateral_no_change, edge_case_multiple_inputs, edge_case_invalid_lock_times
edge_case_oracle_price_unavailable, edge_case_extreme_fee_rates, edge_case_invalid_keys
integration_complete_mint_flow, mint_with_dca_healthy_system, mint_with_dca_warning_system
mint_with_dca_critical_system, mint_with_dca_emergency_system, mint_dca_applies_to_all_lock_tiers
mint_insufficient_funds_with_dca, mint_dca_real_time_adjustment
```

#### File: digidollar_redeem_tests.cpp
```cpp
// Redemption functionality - verify exact tests in file
```

#### File: digidollar_transfer_tests.cpp
```cpp
// Transfer functionality - verify exact tests in file
```

#### File: digidollar_timelock_tests.cpp (36 tests)
```cpp
// CRITICAL: Timelock security tests
cltv_block_height_enforcement, cltv_timestamp_enforcement, cltv_bypass_IMPOSSIBLE
cltv_nlocktime_integration, cltv_sequence_interaction, cltv_boundary_conditions
cltv_script_validation, cltv_replacement_prevention, csv_relative_timelock
csv_bip68_encoding, csv_bypass_IMPOSSIBLE, csv_utxo_age_validation
csv_median_time_past, csv_replacement_prevention, nlocktime_absolute_height
nlocktime_absolute_timestamp, nlocktime_mempool_validation, nlocktime_finality
nlocktime_threshold_boundary, timelock_schnorr_validation, timelock_ecdsa_validation
timelock_p2tr_witness, timelock_mast_path_selection, timelock_replay_prevention
timelock_signature_order, timelock_multi_input_validation, timelock_sighash_validation
collateral_vault_timelock_tiers, normal_redemption_requires_timelock
emergency_redemption_NEVER_bypasses_timelock, err_redemption_RESPECTS_timelock_ALWAYS
timelock_8_lock_tiers, timelock_dos_prevention, timelock_grief_prevention
timelock_frontrun_prevention, timelock_rbf_protection, timelock_malleability_protection
```

#### File: digidollar_dca_tests.cpp (22 tests)
```cpp
// Dynamic Collateral Adjustment
system_health_calculation_basic, system_health_calculation_edge_cases
system_health_various_scenarios, dca_multiplier_healthy_system, dca_multiplier_warning_system
dca_multiplier_critical_system, dca_multiplier_emergency_system, dca_multiplier_boundary_conditions
apply_dca_to_base_ratios, apply_dca_all_lock_tiers, get_current_tier_information
emergency_state_detection, total_system_collateral_calculation, total_dd_supply_calculation
integration_with_consensus_params, performance_health_calculation, numerical_stability
gradual_transition_hooks, emergency_recovery_hooks, test_dca_extreme_scenarios
test_dca_system_state_transitions, test_dca_integration_stress
```

#### File: digidollar_oracle_tests.cpp (37 tests)
```cpp
// Oracle price system
oracle_price_message_basic_construction, oracle_price_message_validation
oracle_price_message_signature_validation, oracle_price_message_serialization
oracle_bundle_basic_construction, oracle_bundle_consensus_requirement
oracle_bundle_median_calculation, oracle_bundle_outlier_filtering
oracle_bundle_epoch_validation, oracle_bundle_serialization
oracle_node_basic_construction, oracle_node_validation, oracle_node_serialization
oracle_selection_deterministic, oracle_selection_insufficient_oracles
oracle_selection_inactive_oracles, chainparams_mainnet_oracle_count
chainparams_testnet_oracle_count, chainparams_regtest_oracle_count
chainparams_oracle_data_validity, chainparams_oracle_getter_functions
chainparams_oracle_endpoint_uniqueness, chainparams_oracle_deterministic_across_networks
oracle_bundle_manager_basic, oracle_bundle_manager_message_handling
oracle_bundle_manager_bundle_creation, oracle_node_struct_tests
exchange_price_mock_tests, oracle_integration_price_retrieval
oracle_integration_system_readiness, oracle_block_integration
oracle_data_validation, test_signature_verification_edge_cases
test_price_aggregation_outliers, test_p2p_message_validation
```

#### File: digidollar_wallet_tests.cpp (10 tests)
```cpp
test_update_ddtimelock_status, test_update_ddtimelock_status_invalid_id
test_get_ddtimelock_status, test_is_ddtimelock_redeemable_locked
test_is_ddtimelock_redeemable_inactive, test_is_ddtimelock_redeemable_no_dd
test_burn_digidollars_basic, test_burn_digidollars_insufficient_balance
test_close_collateral_position_full, test_burn_and_close_integration
```

#### File: digidollar_persistence_walletbatch_tests.cpp (18 tests)
```cpp
walletbatch_write_position, walletbatch_write_ddtransaction
walletbatch_write_ddbalance, walletbatch_write_ddoutput, walletbatch_write_ddmetadata
walletbatch_write_read_roundtrip, walletbatch_read_position, walletbatch_read_ddtransaction
walletbatch_read_ddbalance, walletbatch_read_ddoutput, walletbatch_read_ddmetadata
walletbatch_read_nonexistent, walletbatch_erase_position, walletbatch_erase_ddtransaction
walletbatch_erase_ddbalance, walletbatch_erase_ddoutput, walletbatch_erase_nonexistent
walletbatch_complete_lifecycle
```

#### Additional Unit Test Files (27 total)
- digidollar_activation_tests.cpp (5 tests)
- digidollar_address_tests.cpp (11 tests)
- digidollar_change_tests.cpp
- digidollar_consensus_tests.cpp (11 tests)
- digidollar_err_tests.cpp
- digidollar_gui_tests.cpp
- digidollar_health_tests.cpp
- digidollar_opcodes_tests.cpp (21 tests)
- digidollar_p2p_tests.cpp (12 tests)
- digidollar_persistence_keys_tests.cpp (3 tests)
- digidollar_persistence_serialization_tests.cpp (3 tests)
- digidollar_restore_tests.cpp (13 tests)
- digidollar_rpc_tests.cpp
- digidollar_scripts_tests.cpp (17 tests)
- digidollar_structures_tests.cpp (18 tests)
- digidollar_transaction_tests.cpp (19 tests)
- digidollar_txbuilder_tests.cpp (13 tests)
- digidollar_validation_tests.cpp
- digidollar_volatility_tests.cpp

### 1.2 Oracle Unit Tests (src/test/)

#### File: oracle_exchange_tests.cpp (56 tests)
```cpp
// 8 exchanges x 7 test types = 56 tests
// Exchanges: Binance, CoinMarketCap, CoinGecko, Coinbase, Kraken, Messari, KuCoin, Crypto.com
fetch_[exchange]_price_success (8 tests)
[exchange]_timeout_handling (8 tests)
[exchange]_invalid_json_handling (8 tests)
parse_[exchange]_json_format (8 tests)
outlier_filter_mad_removes_outliers, outlier_filter_mad_keeps_valid_prices
outlier_filter_with_all_identical_prices, outlier_filter_with_single_outlier
outlier_filter_with_multiple_outliers, outlier_filter_with_insufficient_data
outlier_filter_preserves_order, outlier_filter_empty_input
median_odd_number_of_prices, median_even_number_of_prices
median_single_price, median_two_prices, median_preserves_precision
median_micro_usd_format, median_large_values, median_small_values
aggregator_fetches_all_8_exchanges, aggregator_handles_partial_failures
aggregator_applies_outlier_filter, aggregator_calculates_median
aggregator_returns_micro_usd, aggregator_caches_results
aggregator_timeout_configuration, aggregator_concurrent_fetching
```

#### File: oracle_message_tests.cpp (15 tests)
```cpp
schnorr_signature_creation_valid, schnorr_signature_verification_valid
schnorr_signature_verification_invalid, schnorr_signature_wrong_pubkey
schnorr_signature_tampered_message, schnorr_signature_64_bytes
oracle_message_creation, oracle_message_serialization
oracle_message_deserialization, oracle_message_hash_calculation
oracle_message_sign_and_verify, oracle_message_micro_usd_format
oracle_message_timestamp_validation, oracle_message_reject_future_timestamp
oracle_message_reject_old_timestamp
```

#### File: oracle_p2p_tests.cpp (17 tests)
```cpp
p2p_oracle_message_relay_valid, p2p_oracle_message_relay_duplicate
p2p_oracle_message_relay_invalid_sig, p2p_oracle_message_relay_old_timestamp
p2p_oracle_message_relay_future_timestamp, p2p_oracle_message_signature_verification
p2p_oracle_message_timestamp_check, p2p_oracle_message_price_sanity_check
p2p_oracle_message_duplicate_detection, p2p_oracle_message_malformed_rejection
p2p_oracle_message_broadcasts_to_all_peers, p2p_oracle_message_not_sent_to_sender
p2p_oracle_message_inventory_announcement, p2p_oracle_message_getdata_request
p2p_oracle_price_msg_serialization, p2p_oracle_cinv_helpers
zzz_cleanup_oracle_singleton_state
```

#### File: oracle_bundle_manager_tests.cpp (8 tests)
```cpp
phase_one_bundle_creation, message_validation, exchange_aggregator_integration
bundle_persistence_cleanup, oracle_node_price_fetching, bundle_validation_rules
phase_one_testnet_config, oracle_stats_reporting
```

#### File: oracle_config_tests.cpp (13 tests)
```cpp
testnet_oracle_activation_height, testnet_oracle_epoch_length
testnet_oracle_consensus_requirements, testnet_oracle_public_keys
oracle_inactive_before_activation, oracle_active_at_activation
oracle_activation_check_function, phase_one_single_oracle_requirement
phase_one_consensus_one_of_one, phase_one_no_mainnet_activation
regtest_oracle_configuration, oracle_epoch_calculation
oracle_update_interval_configuration
```

#### File: oracle_miner_tests.cpp (6 tests)
```cpp
add_oracle_bundle_to_coinbase, oracle_bundle_serialization_format
oracle_bundle_size_limit, create_new_block_includes_oracle_bundle
create_new_block_no_oracle_if_unavailable, create_new_block_phase_one_single_oracle
```

#### File: oracle_block_validation_tests.cpp (5 tests)
```cpp
checkblock_accepts_valid_oracle_bundle, checkblock_rejects_invalid_bundle_signature
checkblock_rejects_bundle_wrong_consensus, contextual_checkblock_timestamp_validation
contextual_checkblock_rejects_old_bundle
```

#### File: oracle_integration_tests.cpp (3 tests)
```cpp
end_to_end_oracle_flow, oracle_graceful_degradation, verify_integration_points
```

### 1.3 Functional Tests (test/functional/)

#### Core Transaction Tests
| File | Test Methods | RPC Commands Tested |
|------|--------------|---------------------|
| digidollar_basic.py | 5 | getdigidollaraddress, mintdigidollar, senddigidollar, getdigidollarbalance |
| digidollar_mint.py | 7 | mintdigidollar, calculatecollateralrequirement, getdcamultiplier |
| digidollar_transfer.py | 8 | senddigidollar |
| digidollar_redeem.py | 9 | redeemdigidollar, getredemptioninfo, getprotectionstatus |
| digidollar_transactions.py | 13 | All core transaction RPCs |

#### Oracle Tests
| File | Test Methods | RPC Commands Tested |
|------|--------------|---------------------|
| digidollar_oracle.py | 15 | getoracleprice, setmockoracleprice, getoracleconfig, getoracleepoch |

#### Wallet & Persistence Tests
| File | Test Methods | RPC Commands Tested |
|------|--------------|---------------------|
| digidollar_wallet.py | 8 | listdigidollarpositions, getdigidollarbalance, backupwallet |
| digidollar_persistence.py | 6 | All wallet persistence RPCs |

#### Network & Stress Tests
| File | Test Methods | RPC Commands Tested |
|------|--------------|---------------------|
| digidollar_network_relay.py | 6 | Network relay testing |
| digidollar_network_tracking.py | 1 | getdigidollarstats (network-wide) |
| digidollar_stress.py | 20 | Stress testing framework |
| digidollar_protection.py | 7 | getprotectionstatus, DCA/ERR testing |

#### Verification Tests
| File | Test Methods | Purpose |
|------|--------------|---------|
| digidollar_rpc.py | 8 | RPC interface validation |
| digidollar_activation.py | 7 | BIP9 activation testing |
| digidollar_redeem_stats.py | 4 | Redemption statistics |
| digidollar_redemption_amounts.py | 1 | Amount verification |
| digidollar_redemption_e2e.py | 1 | End-to-end redemption |
| digidollar_tx_amounts_debug.py | 2 | Transaction amount debugging |

---

## Part 2: RPC Command Coverage Matrix

### 2.1 Complete RPC Command List (27 commands)

| Command | Unit Tests | Func Tests | Status |
|---------|------------|------------|--------|
| getdigidollarstats | ✅ | ✅ | **Well Tested** |
| getdcamultiplier | ✅ | ✅ | **Well Tested** (digidollar_rpc_dca.py) |
| calculatecollateralrequirement | ✅ | ✅ | **Well Tested** (digidollar_rpc_collateral.py) |
| getdigidollardeploymentinfo | ❌ | ✅ | **Tested** (digidollar_rpc_deployment.py) |
| mintdigidollar | ✅ | ✅ | **Well Tested** |
| senddigidollar | ✅ | ✅ | **Well Tested** |
| redeemdigidollar | ✅ | ✅ | **Well Tested** |
| listdigidollarpositions | ✅ | ✅ | **Well Tested** |
| getdigidollaraddress | ✅ | ✅ | **Well Tested** (digidollar_rpc_addresses.py) |
| validateddaddress | ✅ | ✅ | **Well Tested** (digidollar_rpc_addresses.py) |
| listdigidollaraddresses | ✅ | ✅ | **Well Tested** (digidollar_rpc_addresses.py) |
| importdigidollaraddress | ✅ | ✅ | **Well Tested** (digidollar_rpc_addresses.py) |
| getdigidollarbalance | ✅ | ✅ | **Well Tested** |
| estimatecollateral | ✅ | ✅ | **Well Tested** (digidollar_rpc_estimate.py) |
| getredemptioninfo | ⚠️ | ✅ | **Tested** (digidollar_rpc_redemption.py) |
| listdigidollartxs | ❌ | ❌ | **Not Implemented** |
| getoracleprice | ❌ | ✅ | **Tested** (digidollar_rpc_oracle.py) |
| getprotectionstatus | ❌ | ✅ | **Tested** (digidollar_rpc_protection.py) |
| sendoracleprice | ❌ | ❌ | **Oracle Only** |
| listoracles | ❌ | ❌ | **Oracle Only** |
| startoracle | ❌ | ❌ | **Oracle Only** |
| stoporacle | ❌ | ❌ | **Oracle Only** |
| getoraclepubkey | ❌ | ❌ | **Oracle Only** |
| setmockoracleprice | ❌ | ✅ | **Testing Only** |
| getmockoracleprice | ❌ | ✅ | **Testing Only** |
| simulatepricevolatility | ❌ | ❌ | **UNTESTED** |
| enablemockoracle | ❌ | ✅ | **Testing Only** |

---

## Part 3: Gap Analysis & Required Tests

### 3.1 CRITICAL: Qt GUI Tests (Priority 1)

**Current State**: ZERO tests exist for DigiDollar Qt widgets

**Required Test File**: `src/qt/test/digidollarwidgettests.cpp`

#### 3.1.1 DigiDollarOverviewWidget Tests
```cpp
// File: src/qt/test/digidollarwidgettests.cpp

BOOST_AUTO_TEST_SUITE(digidollar_overview_widget_tests)

// Balance Display Tests
BOOST_AUTO_TEST_CASE(test_dd_balance_display_update)
// Verify: DD balance updates correctly when WalletModel changes
// Input: Mock WalletModel with 1000.00 DD balance
// Expected: Label shows "1,000.00 DD"

BOOST_AUTO_TEST_CASE(test_dgb_collateral_display_update)
// Verify: DGB collateral displays correctly
// Input: Mock position with 50000 DGB collateral
// Expected: Label shows "50,000.00 DGB"

BOOST_AUTO_TEST_CASE(test_usd_value_calculation)
// Verify: USD value calculated from oracle price
// Input: DD balance=1000, Oracle price=$0.006/DGB
// Expected: USD value displayed correctly

// Oracle Price Display Tests
BOOST_AUTO_TEST_CASE(test_oracle_price_display)
// Verify: Oracle price shows correctly in UI
// Input: Oracle price = 6000 micro-USD ($0.006)
// Expected: Label shows "$0.006000"

BOOST_AUTO_TEST_CASE(test_oracle_price_stale_warning)
// Verify: Warning displayed when oracle price is stale (>1 hour)
// Input: Oracle timestamp > 3600 seconds old
// Expected: Warning icon/text visible

// System Health Tests
BOOST_AUTO_TEST_CASE(test_system_health_indicator_healthy)
// Verify: Green indicator when system health > 160%
// Input: System collateral ratio = 200%
// Expected: Green progress bar, "Healthy" label

BOOST_AUTO_TEST_CASE(test_system_health_indicator_warning)
// Verify: Yellow indicator when 120% < health < 160%
// Input: System collateral ratio = 140%
// Expected: Yellow progress bar, "Warning" label

BOOST_AUTO_TEST_CASE(test_system_health_indicator_critical)
// Verify: Red indicator when health < 120%
// Input: System collateral ratio = 100%
// Expected: Red progress bar, "Critical" label

// Recent Transactions Tests
BOOST_AUTO_TEST_CASE(test_recent_transactions_list_population)
// Verify: Recent DD transactions display correctly
// Input: 5 mock DD transactions
// Expected: Table shows 5 rows with correct data

BOOST_AUTO_TEST_CASE(test_transaction_click_emits_signal)
// Verify: Clicking transaction emits navigation signal
// Input: Click on transaction row
// Expected: Signal emitted with txid

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.2 DigiDollarMintWidget Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_mint_widget_tests)

// Amount Validation Tests
BOOST_AUTO_TEST_CASE(test_amount_field_accepts_valid_input)
// Input: "100.00"
// Expected: Field accepts, no error

BOOST_AUTO_TEST_CASE(test_amount_field_rejects_negative)
// Input: "-50.00"
// Expected: Field rejects, error message shown

BOOST_AUTO_TEST_CASE(test_amount_field_rejects_below_minimum)
// Input: "0.50" (below $1.00 minimum)
// Expected: Error: "Minimum mint amount is $1.00"

BOOST_AUTO_TEST_CASE(test_amount_field_rejects_above_maximum)
// Input: "1000001.00" (above $1M maximum)
// Expected: Error: "Maximum mint amount is $1,000,000.00"

// Lock Tier Selection Tests
BOOST_AUTO_TEST_CASE(test_lock_tier_combo_box_population)
// Verify: All 10 lock tiers available (0-9)
// Expected: Combo box has 10 items

BOOST_AUTO_TEST_CASE(test_lock_tier_selection_updates_collateral)
// Input: Select Tier 4 (1 year, 300%)
// Expected: Collateral requirement updates based on tier

BOOST_AUTO_TEST_CASE(test_lock_tier_displays_correct_info)
// Input: Select Tier 7 (5 years, 225%)
// Expected: Label shows "5 years - 225% collateral"

// Collateral Calculation Tests
BOOST_AUTO_TEST_CASE(test_collateral_calculation_tier_0)
// Input: $100 DD, Tier 0 (1000%)
// Expected: Requires 166,666.67 DGB at $0.006

BOOST_AUTO_TEST_CASE(test_collateral_calculation_tier_4)
// Input: $100 DD, Tier 4 (300%)
// Expected: Requires 50,000 DGB at $0.006

BOOST_AUTO_TEST_CASE(test_collateral_calculation_with_dca)
// Input: $100 DD, Tier 4, DCA multiplier 1.25
// Expected: Requires 62,500 DGB (300% * 1.25)

BOOST_AUTO_TEST_CASE(test_insufficient_balance_warning)
// Input: Required collateral > wallet balance
// Expected: Warning message, mint button disabled

// Mint Button State Tests
BOOST_AUTO_TEST_CASE(test_mint_button_enabled_when_valid)
// Input: Valid amount, sufficient balance
// Expected: Mint button enabled

BOOST_AUTO_TEST_CASE(test_mint_button_disabled_when_invalid)
// Input: Invalid amount or insufficient balance
// Expected: Mint button disabled

BOOST_AUTO_TEST_CASE(test_mint_button_click_creates_transaction)
// Input: Click mint button with valid inputs
// Expected: Transaction created, confirmation dialog shown

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.3 DigiDollarSendWidget Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_send_widget_tests)

// Address Validation Tests
BOOST_AUTO_TEST_CASE(test_address_field_accepts_valid_dd_address)
// Input: "DD1abc..." (valid mainnet DD address)
// Expected: Green checkmark, no error

BOOST_AUTO_TEST_CASE(test_address_field_accepts_valid_td_address)
// Input: "TD1abc..." (valid testnet DD address)
// Expected: Green checkmark (if on testnet)

BOOST_AUTO_TEST_CASE(test_address_field_rejects_dgb_address)
// Input: "dgb1abc..." (regular DGB address)
// Expected: Error: "Please enter a DigiDollar address (DD/TD/RD prefix)"

BOOST_AUTO_TEST_CASE(test_address_field_rejects_invalid_checksum)
// Input: "DD1abc...xyz" (invalid checksum)
// Expected: Error: "Invalid address checksum"

// Amount Validation Tests
BOOST_AUTO_TEST_CASE(test_send_amount_exceeds_balance)
// Input: Balance=100 DD, send amount=150 DD
// Expected: Error: "Insufficient DigiDollar balance"

BOOST_AUTO_TEST_CASE(test_send_amount_shows_usd_equivalent)
// Input: 100 DD at $1.00/DD
// Expected: Shows "≈ $100.00 USD"

// Confirmation Dialog Tests
BOOST_AUTO_TEST_CASE(test_confirmation_dialog_shows_details)
// Input: Send 100 DD to address
// Expected: Dialog shows recipient, amount, fee

BOOST_AUTO_TEST_CASE(test_confirmation_dialog_countdown_timer)
// Input: Open confirmation dialog
// Expected: 3-second countdown before confirm enabled

BOOST_AUTO_TEST_CASE(test_confirmation_dialog_cancel)
// Input: Click cancel
// Expected: Dialog closes, no transaction

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.4 DigiDollarReceiveWidget Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_receive_widget_tests)

// Address Generation Tests
BOOST_AUTO_TEST_CASE(test_generate_new_address_button)
// Input: Click "Generate New Address"
// Expected: New DD address generated and displayed

BOOST_AUTO_TEST_CASE(test_address_display_format)
// Expected: Address displayed in monospace font with copy button

// QR Code Tests
BOOST_AUTO_TEST_CASE(test_qr_code_generation)
// Input: Generate address
// Expected: QR code displayed for address

BOOST_AUTO_TEST_CASE(test_qr_code_includes_amount)
// Input: Set requested amount to 100 DD
// Expected: QR code encodes amount in URI

BOOST_AUTO_TEST_CASE(test_qr_code_save_functionality)
// Input: Click "Save QR Code"
// Expected: File dialog opens, PNG saved

// Payment Request Tests
BOOST_AUTO_TEST_CASE(test_payment_request_label)
// Input: Set label "Invoice #123"
// Expected: Label saved with address

BOOST_AUTO_TEST_CASE(test_payment_request_message)
// Input: Set message "Payment for services"
// Expected: Message included in request

BOOST_AUTO_TEST_CASE(test_recent_requests_table)
// Input: Generate 3 addresses
// Expected: Table shows 3 rows

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.5 DigiDollarRedeemWidget Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_redeem_widget_tests)

// Position Selection Tests
BOOST_AUTO_TEST_CASE(test_position_dropdown_population)
// Input: Wallet has 3 positions
// Expected: Dropdown shows 3 positions with IDs

BOOST_AUTO_TEST_CASE(test_position_selection_shows_details)
// Input: Select position
// Expected: Shows DD minted, collateral locked, health, unlock date

// Redemption Validation Tests
BOOST_AUTO_TEST_CASE(test_redeem_locked_position_rejected)
// Input: Select locked position, click redeem
// Expected: Error: "Position locked until [date]"

BOOST_AUTO_TEST_CASE(test_redeem_unlocked_position_allowed)
// Input: Select unlocked position
// Expected: Redeem button enabled

BOOST_AUTO_TEST_CASE(test_partial_redemption_rejected)
// Input: Try to redeem 50% of position
// Expected: Error: "Full redemption required"

// Collateral Return Display Tests
BOOST_AUTO_TEST_CASE(test_collateral_return_calculation)
// Input: Position with 50000 DGB collateral
// Expected: Shows "You will receive: 50,000.00 DGB"

BOOST_AUTO_TEST_CASE(test_collateral_return_after_fees)
// Input: Position with fees
// Expected: Shows net return after fees

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.6 DigiDollarPositionsWidget (Vault Manager) Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_positions_widget_tests)

// Table Population Tests
BOOST_AUTO_TEST_CASE(test_positions_table_columns)
// Expected: Columns: ID, DD Amount, Collateral, Health, Unlock Date, Actions

BOOST_AUTO_TEST_CASE(test_positions_table_data_accuracy)
// Input: Mock 3 positions with known data
// Expected: Table displays correct values

BOOST_AUTO_TEST_CASE(test_positions_table_sorting)
// Input: Click column headers
// Expected: Table sorts by clicked column

// Health Visualization Tests
BOOST_AUTO_TEST_CASE(test_health_bar_green)
// Input: Position health > 200%
// Expected: Green health bar

BOOST_AUTO_TEST_CASE(test_health_bar_yellow)
// Input: Position health 150-200%
// Expected: Yellow health bar

BOOST_AUTO_TEST_CASE(test_health_bar_red)
// Input: Position health < 150%
// Expected: Red health bar

// Action Tests
BOOST_AUTO_TEST_CASE(test_redeem_button_emits_signal)
// Input: Click redeem button on position row
// Expected: redeemRequested signal emitted with position ID

BOOST_AUTO_TEST_CASE(test_context_menu_actions)
// Input: Right-click on position
// Expected: Context menu with Copy ID, View Details, Redeem

BOOST_AUTO_TEST_SUITE_END()
```

#### 3.1.7 DigiDollarTransactionsWidget Tests
```cpp
BOOST_AUTO_TEST_SUITE(digidollar_transactions_widget_tests)

// Filtering Tests
BOOST_AUTO_TEST_CASE(test_filter_by_type_mint)
// Input: Select "Mint" filter
// Expected: Only mint transactions shown

BOOST_AUTO_TEST_CASE(test_filter_by_type_transfer)
// Input: Select "Transfer" filter
// Expected: Only transfer transactions shown

BOOST_AUTO_TEST_CASE(test_filter_by_type_redeem)
// Input: Select "Redeem" filter
// Expected: Only redemption transactions shown

// Search Tests
BOOST_AUTO_TEST_CASE(test_search_by_txid)
// Input: Enter partial txid
// Expected: Matching transactions shown

BOOST_AUTO_TEST_CASE(test_search_by_address)
// Input: Enter DD address
// Expected: Transactions involving address shown

// Display Tests
BOOST_AUTO_TEST_CASE(test_amount_color_coding)
// Expected: Received=green, Sent=red

BOOST_AUTO_TEST_CASE(test_transaction_details_on_click)
// Input: Click transaction row
// Expected: Details panel shows full info

BOOST_AUTO_TEST_SUITE_END()
```

### 3.2 CRITICAL: Missing RPC Tests (Priority 2)

#### 3.2.1 getdcamultiplier Tests
```python
# File: test/functional/digidollar_rpc_dca.py

class DigiDollarDCAMultiplierTest(BitcoinTestFramework):
    
    def test_dca_multiplier_healthy_system(self):
        """Test DCA multiplier returns 1.0 for healthy system (>160%)"""
        # Setup: System health = 200%
        result = self.nodes[0].getdcamultiplier()
        assert_equal(result['multiplier'], 1.0)
        assert_equal(result['health_status'], 'healthy')
    
    def test_dca_multiplier_warning_system(self):
        """Test DCA multiplier increases for warning state (120-160%)"""
        # Setup: System health = 140%
        result = self.nodes[0].getdcamultiplier()
        assert_greater_than(result['multiplier'], 1.0)
        assert_equal(result['health_status'], 'warning')
    
    def test_dca_multiplier_critical_system(self):
        """Test DCA multiplier at maximum for critical state (<120%)"""
        # Setup: System health = 100%
        result = self.nodes[0].getdcamultiplier()
        assert_equal(result['multiplier'], 3.125)  # Max multiplier
        assert_equal(result['health_status'], 'critical')
    
    def test_dca_multiplier_with_custom_health(self):
        """Test getdcamultiplier with custom health parameter"""
        result = self.nodes[0].getdcamultiplier(150)  # 150% health
        assert_greater_than(result['multiplier'], 1.0)
    
    def test_dca_multiplier_invalid_health(self):
        """Test getdcamultiplier rejects invalid health values"""
        assert_raises_rpc_error(-8, "Invalid system_health", 
                               self.nodes[0].getdcamultiplier, -1)
        assert_raises_rpc_error(-8, "Invalid system_health",
                               self.nodes[0].getdcamultiplier, 1001)
```

#### 3.2.2 calculatecollateralrequirement Tests
```python
# File: test/functional/digidollar_rpc_collateral.py

class DigiDollarCollateralTest(BitcoinTestFramework):
    
    def test_collateral_tier_0(self):
        """Test collateral calculation for Tier 0 (1 hour, 1000%)"""
        # $100 DD at $0.006/DGB = 166,666.67 DGB
        result = self.nodes[0].calculatecollateralrequirement(10000, 0)
        assert_approx_equal(result['dgb_required'], 166666.67, places=2)
        assert_equal(result['ratio_percent'], 1000)
    
    def test_collateral_tier_4(self):
        """Test collateral calculation for Tier 4 (1 year, 300%)"""
        result = self.nodes[0].calculatecollateralrequirement(10000, 4)
        assert_approx_equal(result['dgb_required'], 50000.00, places=2)
        assert_equal(result['ratio_percent'], 300)
    
    def test_collateral_tier_9(self):
        """Test collateral calculation for Tier 9 (10 years, 200%)"""
        result = self.nodes[0].calculatecollateralrequirement(10000, 9)
        assert_approx_equal(result['dgb_required'], 33333.33, places=2)
        assert_equal(result['ratio_percent'], 200)
    
    def test_collateral_with_custom_price(self):
        """Test collateral calculation with custom oracle price"""
        # $100 DD at $0.01/DGB = different collateral
        result = self.nodes[0].calculatecollateralrequirement(10000, 4, 10000)
        assert_approx_equal(result['dgb_required'], 30000.00, places=2)
    
    def test_collateral_invalid_tier(self):
        """Test rejection of invalid lock tier"""
        assert_raises_rpc_error(-8, "Invalid lock_tier",
                               self.nodes[0].calculatecollateralrequirement, 10000, 10)
    
    def test_collateral_invalid_amount(self):
        """Test rejection of invalid amounts"""
        assert_raises_rpc_error(-8, "Amount must be positive",
                               self.nodes[0].calculatecollateralrequirement, -100, 4)
        assert_raises_rpc_error(-8, "Amount below minimum",
                               self.nodes[0].calculatecollateralrequirement, 50, 4)
```

#### 3.2.3 Address Management Tests
```python
# File: test/functional/digidollar_rpc_addresses.py

class DigiDollarAddressTest(BitcoinTestFramework):
    
    # validateddaddress tests
    def test_validate_valid_mainnet_address(self):
        """Test validation of valid DD address"""
        result = self.nodes[0].validateddaddress("DD1abc...")
        assert_equal(result['isvalid'], True)
        assert_equal(result['network'], 'mainnet')
    
    def test_validate_valid_testnet_address(self):
        """Test validation of valid TD address"""
        result = self.nodes[0].validateddaddress("TD1abc...")
        assert_equal(result['isvalid'], True)
        assert_equal(result['network'], 'testnet')
    
    def test_validate_invalid_prefix(self):
        """Test rejection of invalid prefix"""
        result = self.nodes[0].validateddaddress("XX1abc...")
        assert_equal(result['isvalid'], False)
        assert_in('Invalid prefix', result['error'])
    
    def test_validate_invalid_checksum(self):
        """Test rejection of invalid checksum"""
        result = self.nodes[0].validateddaddress("DD1abc...modified")
        assert_equal(result['isvalid'], False)
        assert_in('Invalid checksum', result['error'])
    
    # listdigidollaraddresses tests
    def test_list_addresses_empty(self):
        """Test listing addresses from new wallet"""
        result = self.nodes[0].listdigidollaraddresses()
        assert_equal(len(result), 0)
    
    def test_list_addresses_after_generation(self):
        """Test listing after generating addresses"""
        self.nodes[0].getdigidollaraddress()
        self.nodes[0].getdigidollaraddress()
        result = self.nodes[0].listdigidollaraddresses()
        assert_equal(len(result), 2)
    
    def test_list_addresses_with_balance_filter(self):
        """Test filtering by minimum balance"""
        # Generate address and mint DD
        result = self.nodes[0].listdigidollaraddresses(min_balance=100)
        # Verify only addresses with balance >= 100 returned
    
    # importdigidollaraddress tests
    def test_import_valid_address(self):
        """Test importing valid DD address"""
        result = self.nodes[0].importdigidollaraddress("DD1abc...", "imported")
        assert_equal(result['success'], True)
    
    def test_import_with_rescan(self):
        """Test import with blockchain rescan"""
        result = self.nodes[0].importdigidollaraddress("DD1abc...", "imported", True)
        # Verify rescan completed
    
    def test_import_duplicate(self):
        """Test importing already-known address"""
        self.nodes[0].importdigidollaraddress("DD1abc...")
        assert_raises_rpc_error(-8, "Address already exists",
                               self.nodes[0].importdigidollaraddress, "DD1abc...")
```

#### 3.2.4 estimatecollateral Tests
```python
# File: test/functional/digidollar_rpc_estimate.py

class DigiDollarEstimateTest(BitcoinTestFramework):
    
    def test_estimate_basic(self):
        """Test basic collateral estimation"""
        result = self.nodes[0].estimatecollateral(10000, 365)  # $100 for 1 year
        assert 'dgb_required' in result
        assert 'ratio_percent' in result
        assert 'lock_tier' in result
    
    def test_estimate_returns_correct_tier(self):
        """Test that correct tier is identified from lock days"""
        result = self.nodes[0].estimatecollateral(10000, 365)
        assert_equal(result['lock_tier'], 4)  # 1 year = Tier 4
    
    def test_estimate_with_dca_impact(self):
        """Test estimation includes DCA multiplier"""
        # When system health is warning, DCA applies
        result = self.nodes[0].estimatecollateral(10000, 365)
        assert 'dca_multiplier' in result
        assert 'adjusted_dgb_required' in result
```

### 3.3 CRITICAL: Wallet Operations Tests (Priority 3)

#### 3.3.1 Backup/Restore with DD Data
```python
# File: test/functional/wallet_digidollar_backup.py

class DigiDollarBackupTest(BitcoinTestFramework):
    
    def test_backup_includes_dd_positions(self):
        """Verify backup file includes DD position data"""
        # Create DD position
        self.nodes[0].mintdigidollar(10000, 4)
        positions_before = self.nodes[0].listdigidollarpositions()
        
        # Create backup
        backup_path = os.path.join(self.options.tmpdir, "wallet_backup.dat")
        self.nodes[0].backupwallet(backup_path)
        
        # Verify backup file size indicates DD data included
        assert os.path.getsize(backup_path) > 1000
        
        # Restore and verify
        self.restart_node(0)
        self.nodes[0].restorewallet("restored", backup_path)
        positions_after = self.nodes[0].listdigidollarpositions()
        
        assert_equal(len(positions_before), len(positions_after))
        assert_equal(positions_before[0]['dd_amount'], positions_after[0]['dd_amount'])
    
    def test_backup_includes_dd_balance(self):
        """Verify backup preserves DD balance"""
        # Mint and transfer to create balance
        self.nodes[0].mintdigidollar(10000, 4)
        balance_before = self.nodes[0].getdigidollarbalance()
        
        # Backup and restore
        backup_path = os.path.join(self.options.tmpdir, "wallet_backup.dat")
        self.nodes[0].backupwallet(backup_path)
        
        self.restart_node(0)
        self.nodes[0].restorewallet("restored", backup_path)
        balance_after = self.nodes[0].getdigidollarbalance()
        
        assert_equal(balance_before, balance_after)
    
    def test_restore_corrupted_backup(self):
        """Test handling of corrupted backup file"""
        # Create corrupted backup
        backup_path = os.path.join(self.options.tmpdir, "corrupted.dat")
        with open(backup_path, 'wb') as f:
            f.write(b'corrupted data')
        
        assert_raises_rpc_error(-4, "Error loading wallet",
                               self.nodes[0].restorewallet, "bad", backup_path)
```

#### 3.3.2 Encryption with DD Data
```python
# File: test/functional/wallet_digidollar_encryption.py

class DigiDollarEncryptionTest(BitcoinTestFramework):
    
    def test_encrypt_wallet_with_dd(self):
        """Test encrypting wallet containing DD data"""
        # Create DD position
        self.nodes[0].mintdigidollar(10000, 4)
        
        # Encrypt wallet
        self.nodes[0].encryptwallet("testpassphrase")
        self.restart_node(0)
        
        # Verify DD data accessible after unlock
        self.nodes[0].walletpassphrase("testpassphrase", 60)
        positions = self.nodes[0].listdigidollarpositions()
        assert_greater_than(len(positions), 0)
    
    def test_dd_operations_require_unlock(self):
        """Test DD operations require wallet unlock"""
        self.nodes[0].encryptwallet("testpassphrase")
        self.restart_node(0)
        
        # Should fail without unlock
        assert_raises_rpc_error(-13, "Please enter wallet passphrase",
                               self.nodes[0].mintdigidollar, 10000, 4)
        assert_raises_rpc_error(-13, "Please enter wallet passphrase",
                               self.nodes[0].senddigidollar, "TD1...", 100)
    
    def test_dd_read_operations_without_unlock(self):
        """Test read-only DD operations work without unlock"""
        self.nodes[0].mintdigidollar(10000, 4)
        self.nodes[0].encryptwallet("testpassphrase")
        self.restart_node(0)
        
        # These should work without unlock
        balance = self.nodes[0].getdigidollarbalance()
        positions = self.nodes[0].listdigidollarpositions()
        assert_greater_than(balance, 0)
```

#### 3.3.3 Descriptor Migration
```python
# File: test/functional/wallet_digidollar_descriptors.py

class DigiDollarDescriptorTest(BitcoinTestFramework):
    
    def test_export_dd_descriptors(self):
        """Test exporting descriptors with DD keys"""
        # Create DD position
        self.nodes[0].mintdigidollar(10000, 4)
        
        # Export descriptors
        descriptors = self.nodes[0].listdescriptors(True)  # Include private
        
        # Verify DD-related descriptors present
        dd_descriptors = [d for d in descriptors['descriptors'] 
                          if 'digidollar' in d.get('label', '').lower()]
        assert_greater_than(len(dd_descriptors), 0)
    
    def test_import_dd_descriptors(self):
        """Test importing DD descriptors to new wallet"""
        # Create source wallet with DD
        self.nodes[0].mintdigidollar(10000, 4)
        source_descriptors = self.nodes[0].listdescriptors(True)
        source_positions = self.nodes[0].listdigidollarpositions()
        
        # Create new descriptor wallet
        self.nodes[0].createwallet("dd_imported", descriptors=True)
        imported = self.nodes[0].get_wallet_rpc("dd_imported")
        
        # Import descriptors
        imported.importdescriptors(source_descriptors['descriptors'])
        
        # Rescan and verify
        imported.rescanblockchain()
        imported_positions = imported.listdigidollarpositions()
        
        assert_equal(len(source_positions), len(imported_positions))
```

#### 3.3.4 Rescan Reconstruction
```python
# File: test/functional/wallet_digidollar_rescan.py

class DigiDollarRescanTest(BitcoinTestFramework):
    
    def test_rescan_reconstructs_positions(self):
        """Test blockchain rescan reconstructs DD positions"""
        # Create positions at different heights
        self.nodes[0].mintdigidollar(10000, 4)
        self.generate(self.nodes[0], 10)
        self.nodes[0].mintdigidollar(5000, 2)
        self.generate(self.nodes[0], 10)
        
        positions_before = self.nodes[0].listdigidollarpositions()
        
        # Force rescan
        self.nodes[0].rescanblockchain()
        
        positions_after = self.nodes[0].listdigidollarpositions()
        assert_equal(len(positions_before), len(positions_after))
    
    def test_rescan_partial_range(self):
        """Test partial blockchain rescan"""
        self.nodes[0].mintdigidollar(10000, 4)
        height = self.nodes[0].getblockcount()
        
        # Rescan only recent blocks
        self.nodes[0].rescanblockchain(height - 10)
        
        # Verify position still found
        positions = self.nodes[0].listdigidollarpositions()
        assert_greater_than(len(positions), 0)
```

---

## Part 4: Implementation Guidelines for Sub-Agents

### 4.1 C++ Unit Test Implementation

**File Location**: `src/test/digidollar_*.cpp`

**Template**:
```cpp
#include <boost/test/unit_test.hpp>
#include <test/util/setup_common.h>
// Include relevant headers

BOOST_FIXTURE_TEST_SUITE(test_suite_name, TestingSetup)

BOOST_AUTO_TEST_CASE(test_name)
{
    // Arrange
    // ... setup test data
    
    // Act
    // ... call function under test
    
    // Assert
    BOOST_CHECK_EQUAL(expected, actual);
    BOOST_CHECK(condition);
    BOOST_CHECK_THROW(expression, exception_type);
}

BOOST_AUTO_TEST_SUITE_END()
```

**Key Points**:
- Use `BasicTestingSetup` for isolated tests
- Use `TestingSetup` for tests needing chain/wallet
- Use `TestChain100Setup` for tests needing mature coins
- Mock oracle prices with `SetMockOraclePrice()`
- Use BOOST_CHECK_* macros consistently

### 4.2 Python Functional Test Implementation

**File Location**: `test/functional/digidollar_*.py`

**Template**:
```python
#!/usr/bin/env python3
"""Description of test purpose."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class DigiDollarTestName(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Add any extra args
        self.extra_args = [["-digidollar=1"], ["-digidollar=1"]]
    
    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
    
    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.sync_all()
    
    def run_test(self):
        self.log.info("Test description...")
        
        # Setup mock oracle price
        self.nodes[0].setmockoracleprice(6000)  # $0.006
        
        # Generate blocks for maturity
        self.generate(self.nodes[0], 110)
        
        # Run individual tests
        self.test_specific_functionality()
    
    def test_specific_functionality(self):
        """Test specific DigiDollar functionality"""
        # Arrange
        # ...
        
        # Act
        result = self.nodes[0].some_rpc_command()
        
        # Assert
        assert_equal(expected, result['field'])

if __name__ == '__main__':
    DigiDollarTestName().main()
```

**Key Points**:
- Always set mock oracle price before DD operations
- Generate 110+ blocks for coinbase maturity
- Use `self.sync_all()` for multi-node tests
- Use `assert_raises_rpc_error()` for error testing
- Log progress with `self.log.info()`

### 4.3 Qt Test Implementation

**File Location**: `src/qt/test/digidollarwidgettests.cpp`

**Template**:
```cpp
#include <qt/test/digidollarwidgettests.h>
#include <qt/digidollaroverviewwidget.h>
#include <qt/walletmodel.h>
#include <QTest>

void DigiDollarWidgetTests::testOverviewBalanceDisplay()
{
    // Create widget
    DigiDollarOverviewWidget widget;
    
    // Create mock model
    MockWalletModel model;
    model.setDDBalance(1000 * CENT);  // 1000 DD
    
    // Set model
    widget.setWalletModel(&model);
    
    // Verify display
    QCOMPARE(widget.ui->ddBalanceLabel->text(), QString("1,000.00 DD"));
}

void DigiDollarWidgetTests::testMintButtonState()
{
    DigiDollarMintWidget widget;
    MockWalletModel model;
    
    // Insufficient balance
    model.setBalance(100);  // 100 DGB
    widget.setWalletModel(&model);
    widget.setAmount(1000);  // Need more collateral
    
    QVERIFY(!widget.ui->mintButton->isEnabled());
    
    // Sufficient balance
    model.setBalance(1000000);  // 1M DGB
    widget.updateView();
    
    QVERIFY(widget.ui->mintButton->isEnabled());
}
```

**Key Points**:
- Use `QTest` macros (QCOMPARE, QVERIFY, etc.)
- Create mock models for isolation
- Test both valid and invalid states
- Verify signal emissions with `QSignalSpy`

---

## Part 5: Priority Matrix

### P0 - Critical (Must have before release)
1. **Qt GUI Tests** - All 10 widgets need basic coverage
2. **Wallet Backup/Restore Tests** - Data integrity verification
3. **RPC Error Path Tests** - All 27 commands need error testing

### P1 - High (Should have before release)
1. **Untested RPC Commands** - getdcamultiplier, calculatecollateralrequirement, etc.
2. **Wallet Encryption Tests** - DD operations with encrypted wallet
3. **Descriptor Migration Tests** - Wallet upgrade scenarios

### P2 - Medium (Nice to have)
1. **Performance Tests** - Large position counts, rescan efficiency
2. **Stress Tests** - Complete stress test implementation
3. **Edge Case Tests** - Boundary conditions, race conditions

### P3 - Low (Future consideration)
1. **GUI Accessibility Tests** - Screen reader compatibility
2. **Internationalization Tests** - Multi-language support
3. **Advanced Oracle Tests** - Phase Two 8-of-15 consensus

---

## Part 6: Test Execution Commands

```bash
# Run all DigiDollar unit tests
./src/test/test_digibyte --run_test=digidollar*

# Run specific unit test suite
./src/test/test_digibyte --run_test=digidollar_mint_tests

# Run all Oracle unit tests
./src/test/test_digibyte --run_test=oracle*

# Run DigiDollar functional tests
./test/functional/test_runner.py digidollar_

# Run specific functional test
./test/functional/digidollar_basic.py

# Run Qt tests
./src/qt/test/test_digibyte-qt

# Run with verbose output
./test/functional/digidollar_basic.py --loglevel=DEBUG

# Run with valgrind (memory check)
valgrind ./src/test/test_digibyte --run_test=digidollar*
```

---

## Appendix A: Test File Checklist

### Unit Tests (src/test/)
- [x] digidollar_activation_tests.cpp
- [x] digidollar_address_tests.cpp
- [x] digidollar_change_tests.cpp
- [x] digidollar_consensus_tests.cpp
- [x] digidollar_dca_tests.cpp
- [x] digidollar_err_tests.cpp
- [x] digidollar_gui_tests.cpp
- [x] digidollar_health_tests.cpp
- [x] digidollar_mint_tests.cpp
- [x] digidollar_opcodes_tests.cpp
- [x] digidollar_oracle_tests.cpp
- [x] digidollar_p2p_tests.cpp
- [x] digidollar_persistence_keys_tests.cpp
- [x] digidollar_persistence_serialization_tests.cpp
- [x] digidollar_persistence_walletbatch_tests.cpp
- [x] digidollar_redeem_tests.cpp
- [x] digidollar_restore_tests.cpp
- [x] digidollar_rpc_tests.cpp
- [x] digidollar_scripts_tests.cpp
- [x] digidollar_structures_tests.cpp
- [x] digidollar_timelock_tests.cpp
- [x] digidollar_transaction_tests.cpp
- [x] digidollar_transfer_tests.cpp
- [x] digidollar_txbuilder_tests.cpp
- [x] digidollar_validation_tests.cpp
- [x] digidollar_volatility_tests.cpp
- [x] digidollar_wallet_tests.cpp
- [x] oracle_block_validation_tests.cpp
- [x] oracle_bundle_manager_tests.cpp
- [x] oracle_config_tests.cpp
- [x] oracle_exchange_tests.cpp
- [x] oracle_integration_tests.cpp
- [x] oracle_message_tests.cpp
- [x] oracle_miner_tests.cpp
- [x] oracle_p2p_tests.cpp

### Functional Tests (test/functional/)
- [x] digidollar_activation.py
- [x] digidollar_basic.py
- [x] digidollar_mint.py
- [x] digidollar_network_relay.py
- [x] digidollar_network_tracking.py
- [x] digidollar_oracle.py
- [x] digidollar_persistence.py
- [x] digidollar_protection.py
- [x] digidollar_redeem.py
- [x] digidollar_redeem_stats.py
- [x] digidollar_redemption_amounts.py
- [x] digidollar_redemption_e2e.py
- [x] digidollar_rpc.py
- [x] digidollar_stress.py
- [x] digidollar_transactions.py
- [x] digidollar_transfer.py
- [x] digidollar_tx_amounts_debug.py
- [x] digidollar_wallet.py
- [x] digidollar_rpc_deployment.py (NEW - Jan 2026)
- [x] digidollar_rpc_redemption.py (NEW - Jan 2026)
- [x] digidollar_rpc_oracle.py (NEW - Jan 2026)
- [x] digidollar_rpc_protection.py (NEW - Jan 2026)

### Missing Functional Tests - COMPLETED
- [x] digidollar_rpc_dca.py (DCA multiplier tests)
- [x] digidollar_rpc_collateral.py (collateral calculation tests)
- [x] digidollar_rpc_addresses.py (address management tests)
- [x] digidollar_rpc_estimate.py (estimation RPC tests)
- [x] wallet_digidollar_backup.py (backup/restore tests)
- [x] wallet_digidollar_encryption.py (encrypted wallet tests)
- [x] wallet_digidollar_descriptors.py (descriptor wallet tests)
- [x] wallet_digidollar_rescan.py (rescan/recovery tests)

---

## Appendix B: Constants Reference

```python
# DigiDollar Constants for Tests
DD_MIN_AMOUNT_CENTS = 100           # $1.00 minimum
DD_MAX_AMOUNT_CENTS = 100000000     # $1,000,000 maximum
DD_CENT = 100                       # 1 DD = 100 cents

# Lock Tiers
LOCK_TIERS = {
    0: {'days': 0, 'hours': 1, 'ratio': 1000},      # 1 hour, 1000%
    1: {'days': 30, 'ratio': 500},                   # 30 days, 500%
    2: {'days': 90, 'ratio': 400},                   # 90 days, 400%
    3: {'days': 180, 'ratio': 350},                  # 180 days, 350%
    4: {'days': 365, 'ratio': 300},                  # 1 year, 300%
    5: {'days': 730, 'ratio': 275},                  # 2 years, 275%
    6: {'days': 1095, 'ratio': 250},                 # 3 years, 250%
    7: {'days': 1825, 'ratio': 225},                 # 5 years, 225%
    8: {'days': 2555, 'ratio': 212},                 # 7 years, 212%
    9: {'days': 3650, 'ratio': 200},                 # 10 years, 200%
}

# DCA Thresholds
DCA_HEALTHY_THRESHOLD = 160         # >160% = healthy
DCA_WARNING_THRESHOLD = 120         # 120-160% = warning
DCA_CRITICAL_THRESHOLD = 100        # <120% = critical
DCA_MAX_MULTIPLIER = 3.125          # Maximum DCA multiplier

# Oracle Constants
ORACLE_PRICE_DECIMALS = 6           # micro-USD (1,000,000 = $1.00)
ORACLE_TIMESTAMP_TOLERANCE = 3600   # ±1 hour
ORACLE_MIN_CONSENSUS = 8            # Phase Two: 8-of-15
ORACLE_PHASE_ONE_CONSENSUS = 1      # Phase One: 1-of-1

# DigiByte Block Constants
BLOCK_TIME_SECONDS = 15
BLOCKS_PER_HOUR = 240
BLOCKS_PER_DAY = 5760
COINBASE_MATURITY = 8
```

---

*Document generated by AI analysis of DigiByte v8.26 codebase*
*For questions or updates, contact the DigiByte Core development team*
