// Copyright (c) 2014-2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DIGIBYTE_RPC_NET_H
#define DIGIBYTE_RPC_NET_H

class CConnman;
class PeerManager;
namespace node {
struct NodeContext;
} // namespace node

CConnman& EnsureConnman(const node::NodeContext& node);
PeerManager& EnsurePeerman(const node::NodeContext& node);

#endif // DIGIBYTE_RPC_NET_H
