#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test transaction request failover across fresh peer identities."""

import time

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


MSG_TX = 1
NONPREF_PEER_TX_DELAY = 2
GETDATA_TX_INTERVAL = 60


class TxRequestPeer(SingleNodeConnCB):
    def __init__(self):
        super().__init__()
        self.requested = []

    def on_inv(self, conn, message):
        pass

    def on_getdata(self, conn, message):
        with mininode_lock:
            self.requested.extend((inv.type, inv.hash) for inv in message.inv if inv.type in (MSG_TX, MSG_TX | MSG_WITNESS_FLAG))


class P2PTxRequestTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.nodes = start_nodes(
            self.num_nodes,
            self.options.tmpdir,
            [["-debug", "-whitelist=127.0.0.2"]],
        )

    def connect_peer(self, source_address=None, services=NODE_NETWORK):
        peer = TxRequestPeer()
        connection = NodeConn(
            "127.0.0.1",
            p2p_port(0),
            self.nodes[0],
            peer,
            services=services,
            source_address=source_address,
        )
        peer.add_connection(connection)
        return peer

    def run_test(self):
        self.nodes[0].generate(1)

        attacker1 = self.connect_peer()
        attacker2 = self.connect_peer()
        honest = self.connect_peer(source_address="127.0.0.2", services=NODE_NETWORK | NODE_WITNESS)
        NetworkThread().start()
        for peer in (attacker1, attacker2, honest):
            peer.wait_for_verack()
        peer_info = self.nodes[0].getpeerinfo()
        assert_equal(sum(info["whitelisted"] for info in peer_info), 1)
        assert_equal(sum(not info["whitelisted"] for info in peer_info), 2)

        now = int(time.time())
        self.nodes[0].setmocktime(now)
        txid = 0xDEADBEEF
        announcement = msg_inv([CInv(MSG_TX, txid)])

        attacker1.send_and_ping(announcement)
        with mininode_lock:
            assert_equal(attacker1.requested, [])

        self.nodes[0].setmocktime(now + NONPREF_PEER_TX_DELAY)
        attacker1.sync_with_ping()
        assert(wait_until(lambda: (MSG_TX, txid) in attacker1.requested, timeout=10))

        # A fresh non-preferred identity must not advance the preferred peer's
        # request time while the first request is in flight.
        attacker2.send_and_ping(announcement)
        honest.send_and_ping(announcement)
        with mininode_lock:
            assert_equal(attacker2.requested, [])
            assert_equal(honest.requested, [])

        self.nodes[0].setmocktime(now + NONPREF_PEER_TX_DELAY + GETDATA_TX_INTERVAL)
        honest.sync_with_ping()
        assert(wait_until(lambda: (MSG_TX | MSG_WITNESS_FLAG, txid) in honest.requested, timeout=10))
        with mininode_lock:
            assert_equal(attacker2.requested, [])

        # A NOTFOUND response immediately selects the remaining candidate.
        honest.send_and_ping(msg_generic(b"notfound", ser_vector([CInv(MSG_TX, txid)])))
        attacker2.sync_with_ping()
        assert(wait_until(lambda: (MSG_TX, txid) in attacker2.requested, timeout=10))


if __name__ == "__main__":
    P2PTxRequestTest().main()
