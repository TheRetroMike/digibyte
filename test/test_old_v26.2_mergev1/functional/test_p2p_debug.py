#!/usr/bin/env python3
"""Minimal test to debug P2P connection issues"""

from test_framework.test_framework import DigiByteTestFramework
from test_framework.p2p import P2PInterface
from test_framework.util import assert_equal
import time

class TestP2PConn(P2PInterface):
    def __init__(self):
        super().__init__()
        self.connected = False
        
    def on_open(self):
        self.connected = True
        print(f"P2P connection opened")
        
    def on_close(self):
        self.connected = False
        print(f"P2P connection closed")

class MinimalP2PTest(DigiByteTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Try without Dandelion first
        self.extra_args = [["-dandelion=0", "-debug=net", "-debug=dandelion", "-printtoconsole=1"]]
        
    def run_test(self):
        self.log.info("Testing P2P connection without Dandelion")
        
        # First, check that the node is running
        info = self.nodes[0].getnetworkinfo()
        self.log.info(f"Network info: version={info['version']}, connections={info['connections']}")
        
        # Try to add a P2P connection
        self.log.info("Adding P2P test connection...")
        try:
            test_conn = self.nodes[0].add_p2p_connection(TestP2PConn(), wait_for_verack=False)
            self.log.info("P2P connection object created")
            
            # Wait a bit
            time.sleep(2)
            
            # Check if connected
            self.log.info(f"Connection status: is_connected={test_conn.is_connected}")
            
            # Keep test directory for debugging
            self.options.nocleanup = True
            
        except Exception as e:
            self.log.error(f"Failed to add P2P connection: {e}")
            raise

if __name__ == '__main__':
    MinimalP2PTest().main()