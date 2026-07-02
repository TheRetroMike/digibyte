#!/usr/bin/env python3
"""Focused DigiDollar transaction integration coverage.

This test intentionally exercises only real, shipped DigiDollar RPCs and asserts
actual product behavior instead of logging through failures.

Coverage:
- activation and oracle setup sanity
- mint -> confirm -> wallet/position visibility
- cross-node send -> confirm -> balance accounting
- exact-amount redemption after unlock
- RPC validation on bad parameters
- collateral/oracle sanity checks
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)


class DigiDollarTransactionsTest(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-debug=digidollar", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-debug=digidollar", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Setting up DigiDollar transaction integration test...")
        self.setup_environment()

        self.test_rpc_sanity()
        self.test_mint_confirm_and_position_tracking()
        self.test_cross_node_transfer_accounting()
        self.test_exact_redemption_flow()
        self.test_rpc_validation_errors()
        self.test_oracle_and_collateral_sanity()

    def setup_environment(self):
        for i in range(self.num_nodes):
            self.generate(self.nodes[i], 340, sync_fun=self.no_op)
        self.connect_nodes(0, 1)
        self.sync_all()

        # $0.50 / DGB = 500000 micro-USD.
        self.oracle_price_micro_usd = 500000
        self.publish_musig2_quotes()

        stats = self.nodes[0].getdigidollarstats()
        assert 'health_status' in stats
        assert 'total_dd_supply' in stats

    def publish_musig2_quotes(self, node_indices=None, price=None):
        """Publish fresh regtest MuSig2 oracle bundles for the next block."""
        if price is None:
            price = self.oracle_price_micro_usd
        if node_indices is None:
            node_indices = range(self.num_nodes)
        for index in node_indices:
            result = self.nodes[index].setmockoracleprice(price)
            assert_equal(result["price_micro_usd"], price)

    def mine_and_sync(self, node_idx=0, blocks=1):
        self.publish_musig2_quotes()
        self.generate(self.nodes[node_idx], blocks)
        self.sync_all()

    def get_total_balance(self, node_idx):
        balance = self.nodes[node_idx].getdigidollarbalance()
        assert 'total' in balance
        return balance['total']

    def get_active_positions(self, node_idx=0):
        positions = self.nodes[node_idx].listdigidollarpositions()
        return [p for p in positions if p.get('is_active', p.get('status', 'active') == 'active')]

    def test_rpc_sanity(self):
        self.log.info("Checking core DigiDollar RPC sanity...")

        addr0 = self.nodes[0].getdigidollaraddress()
        addr1 = self.nodes[1].getdigidollaraddress()
        assert addr0.startswith('RD') or addr0.startswith('DD') or addr0.startswith('TD')
        assert addr1.startswith('RD') or addr1.startswith('DD') or addr1.startswith('TD')
        assert addr0 != addr1

        stats = self.nodes[0].getdigidollarstats()
        assert 'health_percentage' in stats
        assert 'oracle_price_cents' in stats
        assert 'total_dd_supply' in stats

        oracle = self.nodes[0].getoracleprice()
        assert 'price_micro_usd' in oracle
        assert 'price_cents' in oracle
        assert_equal(int(oracle['price_micro_usd']), 500000)

        collateral = self.nodes[0].calculatecollateralrequirement(10000, 180)
        for field in ['required_dgb', 'effective_ratio', 'oracle_price_micro_usd', 'dca_multiplier']:
            assert field in collateral
        assert_equal(int(collateral['oracle_price_micro_usd']), 500000)
        assert_greater_than(collateral['required_dgb'], 0)

    def test_mint_confirm_and_position_tracking(self):
        self.log.info("Testing mint, confirmation, balance, and position tracking...")

        start_balance = self.get_total_balance(0)
        mint_result = self.nodes[0].mintdigidollar(10000, 3)  # $100, 180-day tier

        for field in ['txid', 'dd_minted', 'dgb_collateral', 'lock_tier', 'unlock_height', 'position_id']:
            assert field in mint_result
        assert_equal(mint_result['dd_minted'], 10000)
        assert_equal(mint_result['lock_tier'], 3)
        assert_equal(len(mint_result['txid']), 64)
        assert_equal(len(mint_result['position_id']), 64)

        self.mine_and_sync(0, 2)

        end_balance = self.get_total_balance(0)
        assert_equal(end_balance, start_balance + 10000)

        positions = self.get_active_positions(0)
        matching = [p for p in positions if p['position_id'] == mint_result['position_id']]
        assert_equal(len(matching), 1)
        assert_equal(matching[0]['dd_minted'], 10000)
        assert_equal(matching[0]['lock_tier'], 3)
        assert_greater_than_or_equal(matching[0]['unlock_height'], self.nodes[0].getblockcount())

    def test_cross_node_transfer_accounting(self):
        self.log.info("Testing cross-node DigiDollar send accounting...")

        sender_before = self.get_total_balance(0)
        receiver_before = self.get_total_balance(1)
        recv_addr = self.nodes[1].getdigidollaraddress()

        send_result = self.nodes[0].senddigidollar(recv_addr, 1000)  # $10
        for field in ['txid', 'amount', 'fee_paid', 'inputs_used', 'change_amount']:
            assert field in send_result
        assert_equal(send_result['amount'], 1000)
        assert_equal(len(send_result['txid']), 64)
        assert_greater_than_or_equal(send_result['inputs_used'], 0)
        assert_greater_than_or_equal(send_result['change_amount'], 0)

        self.mine_and_sync(0, 2)

        sender_after = self.get_total_balance(0)
        receiver_after = self.get_total_balance(1)
        assert_equal(sender_after, sender_before - 1000)
        assert_equal(receiver_after, receiver_before + 1000)

    def test_exact_redemption_flow(self):
        self.log.info("Testing exact-amount redemption flow...")

        mint_result = self.nodes[0].mintdigidollar(15000, 0)  # 1 hour testing tier
        position_id = mint_result['position_id']
        dd_minted = mint_result['dd_minted']
        unlock_height = mint_result['unlock_height']

        self.mine_and_sync(0, 1)

        current_height = self.nodes[0].getblockcount()
        if current_height <= unlock_height:
            self.mine_and_sync(0, unlock_height - current_height + 5)

        # Partial redemption must fail.
        assert_raises_rpc_error(
            -8,
            None,
            self.nodes[0].redeemdigidollar,
            position_id,
            dd_minted - 1,
        )

        balance_before = self.get_total_balance(0)
        redeem_result = self.nodes[0].redeemdigidollar(position_id, dd_minted)
        assert 'txid' in redeem_result
        assert_equal(redeem_result['position_id'], position_id)

        self.mine_and_sync(0, 1)

        balance_after = self.get_total_balance(0)
        assert_equal(balance_after, balance_before - dd_minted)

        active_ids = [p['position_id'] for p in self.get_active_positions(0)]
        assert position_id not in active_ids

    def test_rpc_validation_errors(self):
        self.log.info("Testing DigiDollar RPC validation errors...")

        recv_addr = self.nodes[1].getdigidollaraddress()

        assert_raises_rpc_error(-8, None, self.nodes[0].mintdigidollar, 0, 3)
        assert_raises_rpc_error(-8, None, self.nodes[0].mintdigidollar, 10000001, 3)
        assert_raises_rpc_error(-8, None, self.nodes[0].mintdigidollar, 10000, 10)
        assert_raises_rpc_error(-5, None, self.nodes[0].senddigidollar, "invalid_address", 1000)
        assert_raises_rpc_error(-8, None, self.nodes[0].setmockoracleprice, 0)

    def test_oracle_and_collateral_sanity(self):
        self.log.info("Testing oracle updates and collateral sanity...")

        low = self.nodes[0].calculatecollateralrequirement(10000, 180)
        low_required = low['required_dgb']

        for node in self.nodes:
            node.setmockoracleprice(1000000)  # $1.00 / DGB, should reduce required collateral vs $0.50
        self.oracle_price_micro_usd = 1000000

        high = self.nodes[0].calculatecollateralrequirement(10000, 180)
        high_required = high['required_dgb']

        assert_greater_than(low_required, high_required)

        oracle = self.nodes[0].getoracleprice()
        assert_equal(int(oracle['price_micro_usd']), 1000000)

        protection = self.nodes[0].getprotectionstatus()
        assert 'dca' in protection
        assert 'err' in protection
        assert 'volatility' in protection
        assert 'overall' in protection


if __name__ == '__main__':
    DigiDollarTransactionsTest().main()
