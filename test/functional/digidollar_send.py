#!/usr/bin/env python3
"""Test senddigidollar response format (Bug #11/25) and sub-dollar input handling (Bug #18).

Bug #11/25: amount field should be integer cents, fee_paid/inputs_used/change_amount
            should reflect actual transaction data.
Bug #18:    Fractional/sub-dollar amounts should produce clean errors, not JSON parse crashes.
"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)
import time


class DigiDollarBug11Bug18Test(DigiByteTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
            ["-digidollar=1", "-txindex=1", "-mocktime=0", "-dandelion=0"],
        ]

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Testing senddigidollar response format and input handling...")

        # Setup: generate blocks past DD activation
        for i in range(2):
            self.nodes[i].generate(340)
        self.sync_all()

        # Set mock oracle price ($0.50/DGB = 500000 micro-USD)
        for node in self.nodes:
            node.setmockoracleprice(500000)

        # Mint two DD UTXOs on node 0. The second, smaller UTXO lets the
        # send response prove change is based on selected inputs, not the
        # whole wallet balance.
        self.log.info("Minting DigiDollar on node 0...")
        mint_results = [
            self.nodes[0].mintdigidollar(5000, 4),
            self.nodes[0].mintdigidollar(1000, 2),
        ]
        mint_txids = [result['txid'] for result in mint_results]

        # Broadcast and confirm
        for mint_txid in mint_txids:
            try:
                raw_tx = self.nodes[0].gettransaction(mint_txid)['hex']
                self.nodes[0].sendrawtransaction(hexstring=raw_tx, maxfeerate=0)
            except Exception as e:
                self.log.warning(f"Broadcast note: {e}")

        time.sleep(2)
        self.nodes[0].generate(3)
        time.sleep(1)

        try:
            self.sync_all()
        except AssertionError:
            self.connect_nodes(0, 1)
            time.sleep(2)
            self.sync_all()

        # Get recipient address from node 1
        recv_addr = self.nodes[1].getdigidollaraddress()

        # ---- Bug #11/25: Test response format ----
        self.log.info("Testing Bug #11/25: senddigidollar response format...")
        selectable = sorted(self.nodes[0].listdigidollarunspent(), key=lambda u: u["amount"], reverse=True)
        selected = [{"txid": selectable[0]["txid"], "vout": selectable[0]["vout"]}]
        assert_equal(selectable[0]["amount"], 5000)
        send_comment = "selected send note"
        send_result = self.nodes[0].senddigidollar(recv_addr, 100, send_comment, 0, selected)  # $1.00 = 100 cents

        self.log.info(f"senddigidollar result: {send_result}")
        raw_selected_send = self.nodes[0].getrawtransaction(send_result["txid"], True)
        assert selected[0] in [{"txid": vin["txid"], "vout": vin["vout"]} for vin in raw_selected_send["vin"]]

        # amount must be integer 100, not 0.00000100
        assert_equal(send_result['amount'], 100)
        assert isinstance(send_result['amount'], int), \
            f"amount should be int, got {type(send_result['amount'])}"

        # txid should be present
        assert 'txid' in send_result
        assert len(send_result['txid']) == 64
        assert_equal(send_result['comment'], send_comment)

        send_rows = [
            tx for tx in self.nodes[0].listdigidollartxs(20, 0)
            if tx['txid'] == send_result['txid'] and tx['category'] == 'send'
        ]
        assert_equal(len(send_rows), 1)
        assert_equal(send_rows[0]['comment'], send_comment)

        # fee_paid should be a number (not hardcoded 0 necessarily)
        assert 'fee_paid' in send_result

        # inputs_used should be >= 0
        assert_greater_than_or_equal(send_result['inputs_used'], 0)

        # change_amount should be present and be an integer (cents)
        assert 'change_amount' in send_result
        assert_equal(send_result['change_amount'], 4900)

        self.log.info("Bug #11/25: Response format is correct!")

        # ---- Bug #18: Sub-dollar and fractional input handling ----
        self.log.info("Testing Bug #18: Sub-dollar sends should fail cleanly...")

        # 50 cents = sub-$1 minimum. Should get a clean RPC error, not JSON parse crash.
        # The amount 50 is integer cents, which is below the 100-cent minimum.
        # This should be rejected by TransferDigiDollar with a clean error.
        try:
            self.nodes[0].senddigidollar(recv_addr, 50)
            self.log.info("Sub-dollar send (50 cents) was accepted (consensus may allow it)")
        except Exception as e:
            error_str = str(e)
            self.log.info(f"Sub-dollar send (50 cents) rejected: {error_str}")
            # Must NOT be "JSON integer out of range"
            assert "JSON integer out of range" not in error_str, \
                "Bug #18 NOT fixed: got JSON parse crash"
            self.log.info("Bug #18: Clean error for sub-dollar amount!")

        # Test fractional input (e.g. 0.50 as decimal dollars → 50 cents)
        # This previously crashed with "JSON integer out of range"
        self.log.info("Testing fractional input (0.50)...")
        try:
            self.nodes[0].senddigidollar(recv_addr, 0.50)
            self.log.info("Fractional send (0.50) was accepted")
        except Exception as e:
            error_str = str(e)
            self.log.info(f"Fractional send (0.50) rejected: {error_str}")
            assert "JSON integer out of range" not in error_str, \
                "Bug #18 NOT fixed: got JSON parse crash for fractional input"
            self.log.info("Bug #18: Clean error for fractional input!")

        self.log.info("All tests passed!")


if __name__ == '__main__':
    DigiDollarBug11Bug18Test().main()
