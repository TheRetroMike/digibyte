# DigiDollar: A Fully Decentralized USD Stablecoin on The DigiByte Blockchain

## By Sogobi Napiasi

# 1. Abstract  
DigiDollar is proposed as the first fully decentralized stablecoin native to the DigiByte blockchain. It enables users to lock DigiByte (DGB) coins as collateral and mint a token pegged to the US Dollar (USD) at a 1:1 value. The system operates completely on-chain without any centralized issuer, using time-locked DGB outputs and decentralized price oracles to maintain the peg. DigiDollar’s design ensures that each stablecoin is backed by DGB reserves and can be redeemed in a non-custodial manner at any time for $1 worth of DGB. This whitepaper details the technical architecture, consensus changes, oracle mechanism, and implementation steps required to integrate DigiDollar into DigiByte Core v8.22.0. It covers how DigiDollar maintains its USD parity through real-time pricing and on-chain enforcement, how users interact via core wallet and RPC, and how security issues (like oracle manipulation or spam attacks) are mitigated. By introducing a USD-pegged asset into the DigiByte ecosystem, DigiDollar aims to combine the stability of fiat currency with DigiByte’s speed and security, enhancing DigiByte’s utility for everyday transactions and decentralized finance.

# 2. Introduction  
## Background – DigiByte as a UTXO Blockchain:
DigiByte (DGB) is a UTXO-based public blockchain launched in 2014, derived from the Bitcoin protocol. Over years of development, DigiByte has implemented numerous upgrades from Bitcoin’s codebase, and as of version 8.22.0 it incorporates changes up through Bitcoin Core v22.  
DigiByte is known for its high throughput and decentralization: it uses five mining algorithms for security and achieves 15-second block times (approximately 40× faster than Bitcoin).  
This makes DigiByte one of the longest and fastest UTXO blockchains in production, well-suited to handle a high volume of transactions. DigiByte’s scripting system is based on Bitcoin’s, enabling features like multisig, time-locks, and SegWit, but it does not natively support complex smart contracts or stable-value tokens in its base layer.

## Motivation – The Need for a Decentralized Stablecoin:
Cryptocurrency volatility is a major barrier for everyday use and financial applications. Stablecoins address this by pegging value to fiat (e.g. USD), but popular stablecoins like Tether (USDT) or USDC are centralized, requiring trust in custodians holding equivalent fiat reserves. Decentralized stablecoins, by contrast, maintain their peg through on-chain collateral and algorithms, removing the need for a single issuing authority. Projects like MakerDAO’s DAI on Ethereum have demonstrated the viability of collateral-backed decentralized stablecoins – DAI is maintained by smart contracts with no single entity in control, and is backed by crypto assets to hold a 1:1 peg to USD.  
However, until now the DigiByte ecosystem has not had a native stablecoin due to the limited script capabilities of UTXO systems. Introducing a stablecoin on DigiByte can greatly enhance its utility: users and dApps (decentralized applications) could transact in a stable unit of account while still benefiting from DigiByte’s speed, low fees, and security. A decentralized stablecoin also aligns with DigiByte’s ethos of trustless decentralization – there is no central issuer that could censor transactions or fail to honor redemptions.

## Overview of the DigiDollar Concept:  
DigiDollar is a USD-pegged token fully implemented within DigiByte’s UTXO framework. The core idea is locking DGB as collateral to generate DigiDollars. A user who wants DigiDollars will send DGB into a special time-locked output that cannot be spent normally. In return, the protocol mints a corresponding amount of DigiDollar tokens (equal to the USD value of the locked DGB at that moment) to the user. The USD value is determined by a real-time oracle price feed for DGB/USD, sourced from multiple exchanges to ensure accuracy. Once issued, DigiDollars are freely transferable between DigiByte addresses, just like DGB, except they represent a stable USD value. When a user wants to redeem DigiDollars for the underlying DGB, they initiate a burn of their DigiDollar tokens, and the locked DGB collateral is released back to them (in proportion to the amount redeemed). This redemption is non-custodial – it does not require permission from any third party or centralized entity; the blockchain protocol itself enforces that presenting and burning DigiDollar tokens unlocks the corresponding DGB. Throughout this process, the peg is maintained by always valuing DGB collateral at the current market price and ensuring that the amount of DigiDollars in circulation never exceeds the collateral value (with a safety margin). The result is a trust-minimized stablecoin: users trust only the blockchain’s consensus rules and the distributed oracles, not a company or bank, to guarantee that each DigiDollar is backed and redeemable.

## Advantages in the DigiByte Ecosystem:
DigiDollar brings several advantages:  
1. **Stability for Commerce** – Merchants and users can accept DigiDollar without worrying about immediate value fluctuation, yet settle on the DigiByte chain with its fast confirmation times.  
2. **DeFi Building Block** – A stablecoin on DigiByte can enable lending, borrowing, or trading applications entirely on-chain, expanding DigiByte’s capabilities beyond a payment coin.  
3. **No Counterparty Risk** – Unlike centralized stablecoins, DigiDollar holders are not exposed to the solvency or honesty of an issuing company; their funds are secured by on-chain DGB collateral.  
4. **Leverage and Hedging for DGB Holders** – Users can borrow against their DGB (by minting DigiDollars) to obtain liquidity in USD terms without selling their DGB, which is useful for hedging or leveraging positions, similar to how DAI allows crypto-backed loans.  

Competing UTXO-based blockchains have begun exploring stablecoin protocols (e.g., Ergo’s SigmaUSD stablecoin which inspired Cardano’s Djed), demonstrating that it is possible to achieve a decentralized stable asset on a UTXO ledger. DigiDollar extends this innovation to DigiByte, leveraging its unique strengths (decentralization, speed) to create a stablecoin implementation that is efficient and secure at scale.

This whitepaper proceeds to detail the system architecture and components of DigiDollar (Section 3), the required consensus changes (Section 4), the decentralized oracle network (Section 5), and the specific transaction and script designs for minting, transferring, and redeeming DigiDollars (Section 6). We then describe user-facing integration in the core wallet GUI (Section 7) and new RPC calls for monitoring (Section 8). Transaction and fee considerations are discussed in Section 9, followed by a thorough analysis of security concerns (Section 10). Finally, Sections 11 and 12 outline potential future improvements (Taproot, Schnorr, etc.) and conclude with the impact on the DigiByte ecosystem and next implementation steps.

# 3. DigiDollar System Architecture

## On-Chain Implementation within DigiByte:
DigiDollar is implemented directly on the DigiByte blockchain as an extension of its native transaction protocol. There is no separate sidechain or off-chain token ledger – the existence and state of DigiDollar tokens are embedded in DigiByte’s UTXO set and maintained by network consensus. Key components of the architecture include: the collateral UTXOs (time-locked DGB outputs that back DigiDollars), the DigiDollar token UTXOs (outputs that represent holdings of the stablecoin), and the oracle data that provides the exchange rate. The design philosophy is to reuse Bitcoin-compatible primitives (UTXO, scripts, transactions) and augment them with minimal new features necessary for stablecoin functionality. This ensures that DigiDollar leverages DigiByte’s proven infrastructure (mining, validation, networking) and remains lightweight.

## Time-Locked DGB as Collateral:  
Collateral for DigiDollar is provided by DGB coins that are locked in special outputs using script conditions. When a user locks DGB to mint DigiDollars, those DGB become unspendable for a certain duration or until certain conditions are met (specifically, until the corresponding DigiDollars are returned and burned). The primary mechanism used is Bitcoin’s time-lock functionality: e.g., an output script can use OP_CHECKLOCKTIMEVERIFY (CLTV) to prevent spending until a future block height or timestamp.  
In DigiDollar’s context, each collateral output may include a timelock that enforces a minimum lock period (for example, 30 days) during which the collateral cannot be reclaimed by the original owner except via the stablecoin redemption process. Time-locking has two purposes here:  
1. It guarantees the collateral remains in place for a known period, supporting the stability of the peg (users cannot rapidly withdraw collateral and leave outstanding DigiDollars unbacked).  
2. It can serve as a mechanism for eventual collateral release or liquidation if the stablecoin isn’t redeemed in time.  

The lock duration might be user-selectable (e.g., 1 month, 3 months, 1 year) at the time of minting, and the system can track statistics by duration. Longer lock periods could be encouraged (as they provide longer stability) or required for higher minting ratios. The locked UTXOs still belong to the user (they control the private keys to spend it), but the script encumbrance means those coins are effectively escrowed for the benefit of DigiDollar holders until redemption conditions are fulfilled.

## Real-Time Pricing via Decentralized Oracles:  
A crucial component for maintaining the USD peg is obtaining the current USD value of DGB in real time. DigiDollar relies on a decentralized oracle system to feed the DGB/USD exchange rate into the blockchain. This is implemented by having a set of independent oracle nodes (which could be community-run or elected entities) that pull price data from external sources (e.g., major cryptocurrency exchanges trading DGB/USD or DGB/BTC and a reference BTC/USD). These oracle nodes digitally sign the price data (with their unique private keys), and the signatures are broadcast and included in DigiByte blocks (details in Section 5). The consensus protocol will use this price data to validate DigiDollar issuance and redemption transactions. By taking an aggregate of multiple sources (for example, a median of prices reported by 5 out of 7 trusted oracles), the system minimizes the risk of any single bad data point. The pricing information is updated frequently (potentially every block or at fixed intervals like every N blocks) so that the exchange rate used is as current as possible at the time of any DigiDollar transaction. Each full node maintains the latest valid price from oracle data embedded in the blockchain and uses it to check the value of new DigiDollar mints or redeems.

## Core Wallet Integration & User Experience: 
DigiDollar functionality will be integrated into the DigiByte Core wallet (v8.22.0 and above) so that users can easily access stablecoin features through a graphical interface. From the user’s perspective, the wallet will simply provide new options to “Mint DigiDollar” or “Redeem DigiDollar” alongside normal send/receive functions. Under the hood, the core wallet handles the specialized transaction construction and communicates with the blockchain to obtain oracle prices. The integration also means DigiDollar transactions propagate and confirm just like normal DGB transactions, and they are stored in the same blockchain ledger. To external observers (and older wallets), a DigiDollar transaction will appear as a transaction with some unfamiliar outputs (new script types or OP_RETURN metadata). Only updated clients will interpret those outputs as DigiDollar tokens. This approach ensures backward compatibility: nodes that have not upgraded will reject unknown transaction types (if not made backward-compatible), but the goal is to implement DigiDollar via a soft fork or as standard transactions so that non-upgraded nodes simply treat them as anyone-can-spend or benign data (more on this in Section 4). Overall, the system architecture strives to keep all DigiDollar logic on-chain and transparent, with the core nodes and wallets providing the necessary logic to enforce the peg and facilitate user interactions.

## Summary:
In DigiDollar’s architecture, the DigiByte blockchain is the foundation providing security and record-keeping, script-locked collateral UTXOs ensure each DigiDollar is backed by DGB, oracle-fed price data provides the dynamic link to USD value, and the wallet/UI layer makes it accessible. The design does not introduce a new token standard or complex scripting language; instead, it extends DigiByte’s existing UTXO model with a few new consensus rules and scripts tailored to stablecoin operations. In doing so, DigiDollar retains the decentralization (miners validate stablecoin transactions just like any other), speed (15-second blocks for fast settlement), and security (tens of thousands of nodes verifying transactions) of the DigiByte network, while adding a stablecoin capability that operates seamlessly within this environment.

# 4. DigiByte Consensus and Protocol Changes

Implementing DigiDollar requires changes to DigiByte’s consensus rules and transaction processing. These changes ensure that the creation and redemption of the stablecoin are validated by every node, preventing improper issuance or double spending of collateral. We outline the necessary modifications, including introducing new transaction types or script opcodes, handling locked UTXOs in consensus, and extending the script interpreter for DigiDollar-specific logic.

## New Transaction Types for Minting and Burning:  
DigiDollar introduces two new logical transaction types: Mint transactions and Redeem transactions (and by extension, standard Transfer transactions for the stablecoin). While on a technical level these might not be distinct versioned transaction formats, the network will treat transactions that involve DigiDollar outputs in specific ways. A Mint transaction is one where a user provides DGB as input and outputs a corresponding DigiDollar token. It typically has (a) one or more DGB funding inputs from the user, (b) one output that locks the provided DGB as collateral (time-locked and script-encumbered), (c) one output that is a DigiDollar token assigned to the user’s address representing the newly minted stablecoins, and (d) possibly a small DGB change output or fee output. A Redeem transaction does the reverse: (a) it takes one or more DigiDollar token inputs (the user’s stablecoin holdings being spent/burned), (b) it takes the corresponding locked DGB collateral input, and (c) it outputs DGB back to the redeemer (and if applicable, a residual locked collateral output if not all collateral is released). In both cases, the transaction must obey specific rules: the amount of DigiDollars minted or burned must be consistent with the DGB collateral and the current price. While the base transaction format (inputs, outputs) remains the same as Bitcoin/DigiByte, these transactions carry additional semantic meaning and need special validation. To facilitate recognition, we may use unused bits in the transaction version or a marker in the outputs. For example, we could designate a new transaction version number (e.g., nVersion=0x0D1G as a flag for DigiDollar transactions) that indicates to nodes that this transaction includes stablecoin logic. Alternatively, the presence of a special script opcode or an identifying pattern in an output (like a specific OP_RETURN tag or OP_DIGIDOLLAR opcode) can serve to classify the transaction type. These identifiers ensure older software (not DigiDollar-aware) will not accept such transactions as valid (if the rules are a soft fork) and allow upgraded nodes to apply new consensus checks to them.

## DGB-Specific Opcodes vs. OP_RETURN Metadata: 
We consider two approaches to implementing the stablecoin operations in the scripting system: introducing a new DigiByte-specific opcode (such as OP_DIGIDOLLAR) or leveraging the existing OP_RETURN opcode for carrying metadata.

## New Opcode Approach:  
In this approach, we add one or more opcodes to the DigiByte script language that directly handle stablecoin logic. For instance, OP_DIGIDOLLAR could be an opcode used in the output script of a DigiDollar UTXO to mark it as a stablecoin token and possibly encode the amount. The script interpreter would be modified to understand this opcode: when validating transactions, encountering OP_DIGIDOLLAR could trigger special behaviour (like verifying that the total DigiDollar outputs equal the allowed amount given the inputs). We might also add an opcode like OP_CHECKORACLESIG or OP_CHECKPRICE to allow scripts to verify the included oracle price data against a known public key (though this could also be handled outside the script by consensus). The new opcode approach has the advantage of consensus-level enforcement using the existing validation framework – the rules for minting/burning can be embedded in script execution, making them tamper-proof. This approach is more elegant and secure, but requires a soft fork or hard fork to introduce the opcodes. If we repurpose existing disabled opcodes (e.g., use an OP_NOPx slot) and activate it as OP_DIGIDOLLAR with new meaning, it can be done as a soft fork (similar to how OP_NOP2 was repurposed for OP_CHECKLOCKTIMEVERIFY via BIP65). Every node would need to upgrade to enforce the new rules, but older nodes would see these scripts as anyone-can-spend (if using NOP originally) and thus not violate old rules.

## OP_RETURN Metadata Approach: 
Alternatively, we could implement DigiDollar using OP_RETURN outputs to carry token metadata, akin to how the Omni Layer and other protocols issue tokens on Bitcoin by embedding data. For example, a DigiDollar Mint transaction could include an OP_RETURN output that contains an identifier (like a tag “DigiDollar”) and the amount of stablecoin issued. The collateral output might be a standard P2SH or P2PKH that is locked by script, and the OP_RETURN would tie the stablecoin amount to that transaction. Transfer of DigiDollars would similarly be done by OP_RETURN outputs indicating a token transfer from one address to another. This approach has the benefit of simplicity – it doesn’t require changing the script interpreter since OP_RETURN data is ignored by script and simply stored in the blockchain for external interpretation. However, to achieve consensus enforcement, relying purely on OP_RETURN would fall short: the network by default does not validate the meaning of OP_RETURN data (e.g., anyone could craft an OP_RETURN claiming an issuance without collateral, and vanilla nodes would still consider the transaction valid as long as it met basic rules). We would need to implement additional consensus checks at the mempool or block validation level to interpret the OP_RETURN and enforce the stablecoin rules (effectively baking the DigiDollar protocol into the node software). In essence, the node would need to parse the OP_RETURN in each transaction, and if it matches the DigiDollar format, perform the necessary validations (collateral amount, price, etc.). This is a heavier and somewhat more ad-hoc approach, as it separates the logic from the script execution path.

## Comparison and Chosen Approach:  
We lean towards the new opcode approach (OP_DIGIDOLLAR) for DigiDollar due to the stronger security and cleaner integration. Embedding logic in the script ensures that all checks happen during the normal script validation pass, and invalid transactions (e.g., minting more stablecoins than allowed by collateral) can be rejected precisely at script evaluation with a clear failure condition. It also means that the rules governing DigiDollar are fully transparent in the script itself, which can be audited and reasoned about. The OP_RETURN approach, while used by protocols like Omni, essentially treats the base blockchain as a dumb carrier of data and relies on overlay logic – this introduces the risk of consensus divergence (if not all nodes apply the logic consistently) and complicates the implementation. Additionally, OP_RETURN outputs are unspendable and thus do not naturally support transfers – Omni solved that by making balances managed off-chain by wallet software reading OP_RETURNs, but in DigiDollar we want the tokens to be real UTXOs that are part of the state, not just logs. Therefore, we propose introducing new DigiByte opcodes for stablecoin support. Specifically, OP_DIGIDOLLAR will be used in DigiDollar token outputs (marking an output as representing a certain amount of DigiDollar), and possibly companion opcodes or script patterns for the collateral outputs as well. We will detail example scripts in Section 6.

## Handling of Locked UTXOs and Consensus Enforcement:  
Once DGB are locked as collateral, the consensus rules must strictly ensure those UTXOs cannot be spent arbitrarily. In practice, this means a collateral output’s script will make it unspendable by normal means – it might require a combination of conditions such as a specific signature and the presence of a DigiDollar burn proof. The network will enforce that for the life of that UTXO (until it’s unlocked via redemption), no transaction can appear that spends it unless it meets the exact script conditions. This is largely achieved by the script itself (if someone tries to spend the output without burning stablecoins, the script evaluation will fail and miners/nodes will reject it). In addition, we may have consensus-level tracking of outstanding stablecoin. Each mint transaction increases total DigiDollar supply and “binds” a certain DGB collateral to that supply. We can conceptualize that the system keeps a mapping of Collateral ID -> Stablecoin amount. In a simple implementation, the Collateral ID could be the outpoint (txid:vout) of the locked DGB UTXO. When a redeem occurs, it references that outpoint and includes stablecoin inputs that sum up to the required amount; the node validates that those stablecoins correspond to the same ID and then allows the outpoint to be spent. This could be done by encoding the Collateral ID within the DigiDollar token output script itself (so that any spend of the token carries that reference). An alternative (more fungible) design is to treat all DigiDollars as a single fungible currency not tied to specific collateral – in that case, we would treat the entire pool of locked DGB as backing the entire supply of DigiDollars. However, that approach either requires global tracking (which complicates partial redemptions and could allow arbitrage on specific collateral as discussed in Section 6 security) or introduces liquidation mechanics. For simplicity and predictability, each DigiDollar issuance is linked to specific collateral at the time of mint. Consensus enforcement then ensures that the same collateral cannot be used to back two different sets of stablecoins (no double minting), and that stablecoins cannot be redeemed for collateral other than their own (unless we later allow some form of pooled collateral with global redeemability).

## Script Interpreter Changes for DigiDollar: 
To implement the new opcodes and verification rules, the DigiByte script interpreter (in the core code, e.g. EvalScript in DigiByte Core) will be extended. For instance, defining OP_DIGIDOLLAR in script.h and implementing its logic in opcode.cpp (or equivalent) would be necessary. The OP_DIGIDOLLAR when executed could do the following: consume certain items from the stack (for example, it might expect the stablecoin amount and an oracle signature or price value to be on the stack), perform validation, and push a boolean result or require a subsequent OP_VERIFY. One possible implementation is that OP_DIGIDOLLAR is only used in the locking script of collateral outputs as a sort of assertion opcode that ensures redemption conditions. For example, the collateral output’s script could be:  

    [oracle_price] [stable_amount] OP_DIGIDOLLAR OP_CHECKLOCKTIMEVERIFY

Here, OP_DIGIDOLLAR could verify that if this output is spent, the spending transaction has burned stable_amount of DigiDollars at a price at least [oracle_price] (to safeguard value). Another usage is in the token output’s script, which could simply indicate this output represents X stablecoins and restrict who can spend it (owner’s public key). For instance:  

    OP_DIGIDOLLAR <amount> OP_DROP <OwnerPubKey> OP_CHECKSIG

In this hypothetical script, OP_DIGIDOLLAR `<amount>` might signal to the interpreter that this output is a stablecoin UTXO of “amount” units, and perhaps the interpreter records that amount in validation state. The actual enforcement might then be: when this output is later spent, the interpreter ensures the same amount is either transferred to other outputs with OP_DIGIDOLLAR or, if not, then it must be redeemed (meaning the output is consumed without outputting new stable tokens, effectively a burn). This would enforce a conservation of stablecoin principle: except when burning, the total DigiDollar amount in outputs must equal the total in inputs (similar to how DGB must balance, except DGB can also be paid as fee whereas stablecoin might not be allowed as fee).

### To summarize the interpreter changes:

- Add recognition of new opcodes (e.g., OP_DIGIDOLLAR, OP_ORACLEVERIFY, etc.).  
- Enforce new rules during transaction validation:  
  - For mint tx: ensure presence of required opcodes and that collateral output and stablecoin output relate correctly.  
  - For transfer tx: ensure the sum of stablecoin input amounts equals sum of stablecoin output amounts (no creation or destruction except in mint/burn transactions).  
  - For redeem tx: ensure stablecoin inputs are removed and corresponding collateral is released, using current oracle price for valuation.  
- Possibly extend the standard script verification flags and IsStandard() policy to allow these new scripts (so that miners will include them).  

These changes will be implemented as a network upgrade (soft fork) activated by supermajority of miners (similar to previous DigiByte upgrades). Non-upgraded nodes would reject transactions with unknown opcodes (if we use an OP_NOP slot, old nodes may think it’s NOP which always true and might accept weird transactions – hence careful fork logic is needed so old nodes don’t erroneously accept something invalid under new rules). The deployment would likely follow a BIP9-style signaling period, ensuring a smooth activation once the majority has upgraded.

In conclusion, DigiDollar requires careful but achievable consensus changes: defining new script semantics and transaction verification logic to handle the minting, transferring, and burning of a USD-pegged token. By adopting a strategy of minimal but sufficient modifications (favoring built-in script opcodes and consensus checks over external systems), we maintain DigiByte’s robustness while adding this significant new functionality.

# 5. Decentralized Price Oracle System

A reliable and tamper-resistant oracle mechanism is the linchpin that connects DigiDollar to the real-world USD value of DGB. Here we describe the design of the decentralized oracle system: how price data is gathered and aggregated, how it is injected into the blockchain and verified, and measures to secure it against manipulation or failure.

## Aggregation of USD/DGB Price Data:  
DigiDollar’s oracles pull the price of DGB in USD from multiple sources to ensure accuracy and robustness. The sources can include major exchanges (for example: Binance, KuCoin, Bittrex, etc., as well as aggregate price feeds like CoinGecko or CoinMarketCap). To avoid reliance on any single exchange (which might have API issues or an outlier price), the oracle nodes will retrieve prices from a set of (say) 5-10 exchanges. Each oracle node then computes a consolidated price – commonly the median of the collected exchange prices is used to reduce the effect of outliers or temporarily erroneous data. By using a median or trimmed mean, we ensure that even if one source reports an off price (due to low liquidity or error), it will not significantly skew the reported value. For instance, if 7 exchanges are queried and their DGB/USD rates are [0.105, 0.106, 0.108, 0.500, 0.107, 0.104, 0.106] USD, the oracle would sort these and perhaps take a median (0.106 USD in this hypothetical), ignoring the aberrant 0.500 value. Additionally, to get USD pricing indirectly, the oracle could use a BTC reference: fetch DGB/BTC from exchanges and multiply by BTC/USD from a reliable source if needed (though direct DGB/USD is preferable for simplicity). The aggregated price is then signed by the oracle and broadcast. We assume oracles update this price at a regular interval—potentially every block or every few blocks. A reasonable design is to have oracles update their price data every N seconds (e.g., 60 seconds) and whenever a new block is found, the miner can include the latest oracle reports.

## Inclusion of Price Data in Blocks: 
To make the oracle data available to scripts and consensus, each block of the DigiByte blockchain will contain a price record for DigiDollar. We propose a scheme where multiple oracle signatures are included in each block header or coinbase transaction. One approach is to utilize the coinbase transaction’s coinbase data field (which miners already use to include extra nonce and messages) to embed a small data structure containing the price and oracle attestations. For example, the coinbase’s scriptSig could contain a tag (like 0xD1G1 to indicate DigiDollar oracle data follows), then the price in a standardized format (e.g., a 4-byte or 8-byte integer representing price in micro-USD per DGB), followed by a set of Schnorr/ECDSA signatures from approved oracle public keys. The block validation logic will be extended to detect and parse this. Another approach is to dedicate an auxiliary block header field for the price (some blockchains extend the header or use OP_RETURN in coinbase output). Since DigiByte is derived from Bitcoin, directly extending the 80-byte block header is non-trivial without a hard fork; a simpler method is to use the coinbase transaction’s output: we could require that the coinbase TX has an OP_RETURN output that contains an “oracle bundle” – a piece of data encoding the price and signatures. This OP_RETURN would be unspendable and just carry info. During block validation, nodes will look for this output, decode the price and verify the signatures (outside of the normal script system, since OP_RETURN has no script execution effect).

## Verification of Oracle Signatures in Block Headers:  
Only data signed by trusted oracle keys should be accepted. At the genesis of DigiDollar (the activation point), the network or community will designate a set of oracle public keys that are allowed to feed prices. This could be a fixed set or modifiable via governance (e.g., a DigiByte Improvement Proposal to add/remove oracles). For each block, the validation rules might require at least M out of N valid oracle signatures on a price value for it to be considered the official price. For example, if there are 7 oracle providers, the rule might be that at least 5 signatures must be present on the same price value. The block would include those 5 (or more) signatures. Full nodes, upon receiving a new block, will extract the price and the signatures, then check each signature against the stored list of oracle pubkeys (using standard ECC signature verification). Only if the threshold condition is met and all signatures are valid and correspond to the claimed price will the block’s price be considered valid. This process is somewhat analogous to multi-signature consensus on a data feed. If a block is found without the required oracle data or with invalid signatures, nodes would reject it as it violates consensus (similar to how a block without the required difficulty or wrong coinbase is invalid). We assume miners will not mine a block without including the oracle data, because it would be futile (other nodes wouldn’t accept it). Miners themselves will typically not generate the price data – they will rely on the oracle nodes. A prudent design is for oracle nodes to broadcast their signed price messages to miners and the network; miners simply gather the latest signatures and insert them into their candidate block. This creates a weak dependency of miners on oracles: a miner needs a recent price update to construct a valid block. If oracles are slow or offline, it could delay block acceptance, which we discuss next.

## Handling Missing or Stale Price Data:  
It’s critical that the blockchain doesn’t grind to a halt if oracle data is briefly unavailable. We design a grace period or fallback. For instance, the consensus rules could allow the reuse of the last known valid price for a certain number of blocks if fresh signatures are not available. Concretely, suppose oracles are expected to update every block, but if in block B the miner cannot get new signatures in time, they might reuse the oracle data from block B-1 (just copy the same price and signatures). Nodes would accept it as long as it’s within an allowed window (maybe up to X consecutive blocks can carry the same oracle info). However, to avoid abuse of this (e.g., if price is changing rapidly, a miner shouldn’t keep using an outdated price to allow over- or under-collateralized actions), the protocol might restrict minting or risky transactions when price data is stale. Another fallback strategy is to have a secondary tier of data: for example, if the primary oracles fail, a default or “emergency” price feed from a backup source could be used. But that introduces complexity and trust issues. A simpler method is: if no oracle signatures are present in a block, then that block cannot include any DigiDollar mint or redeem transactions (it could still include normal DGB transfers). In other words, stablecoin-affecting transactions require a fresh price. A block without price data could still be mined (to not halt the chain), but it would effectively pause the stablecoin functionality until oracles resume. We expect oracles to be highly available, but network partitions or downtime are planned for with this design. Additionally, oracles themselves can use redundant infrastructure (multiple servers, fallback exchange APIs) to minimize failures.

## Prevention of Price Manipulation and Sybil Attacks:  
The decentralized oracle system is engineered to resist manipulation by any single actor. By drawing from multiple exchanges and requiring multiple independent signatures, no single oracle can unilaterally push a false price without collusion. To become an oracle, one must be recognized by the community (likely through an on-chain governance or multi-party agreement), making it hard for a malicious actor to insert sybil or fake oracles. The use of medians means that even if one exchange’s data is compromised or if one oracle tries to post an outlandish price, the others will override it. To further secure the feed, the oracle identities might be required to stake DGB or otherwise have skin in the game (though implementing a full staking slashable system on DigiByte may be outside the initial scope, it could be a future improvement). Additionally, there can be sanity checks on the price transitions: for instance, if the reported price deviates by more than, say, 20% from the previous block’s price, nodes could flag it and require extra signatures or a waiting period. This would prevent sudden swings caused by error (but if an actual market crash of >20% happens, we might not want to block it—so this is a tunable parameter).

From a security standpoint, one scenario to consider is a miner-oracle collusion: what if a majority of miners and a majority of oracles collude to manipulate the price feed and exploit DigiDollar? This would be a complex and risky endeavor, as they would essentially attack their own blockchain’s credibility. For example, if they artificially lower the price feed, they could redeem stablecoins for more DGB than they should (draining collateral), but such an event would be evident on-chain and would destroy trust in the stablecoin (and thus DGB’s value likely). Nevertheless, the multi-oracle design makes it so that an attacker would need control of several independent organizations to significantly skew the price, which is much more difficult than a single point of failure. Regular audits and possibly public reputation of oracle providers will add social trust on top of the technical safeguards.

## In summary, the decentralized oracle system for DigiDollar works as follows:

- Multiple independent oracles fetch DGB/USD prices from diverse sources.  
- They digitally sign the price value and broadcast it.  
- Miners include a collection of these signatures with the price in each block (likely via coinbase transaction).  
- Nodes verify the signatures against known keys and ensure sufficient consensus among oracles on the price.  
- If data is missing, fallback rules allow short-term continuity of the blockchain but restrict stablecoin usage to maintain safety.  

The design uses redundancy and consensus to prevent manipulation, ensuring the stablecoin’s peg reflects a true market-driven DGB/USD rate.  
This robust oracle mechanism is what enables DigiDollar to be fully decentralized; the blockchain itself “knows” the exchange rate and can autonomously enforce the value equivalence between DGB and the DigiDollar token.

# 6. DigiDollar Implementation Details

This section provides a detailed walkthrough of how DigiDollar is implemented in practice, including the exact mechanisms of locking DGB, minting DigiDollars, transferring them, and redeeming them back to DGB. We also illustrate sample transaction structures and script snippets to clarify the design.

## Locking Mechanism for Collateral

When a user decides to mint DigiDollars, they must lock a certain amount of DGB as collateral. The core wallet will guide the user through this process:

- **User Input:** The user specifies the amount of DigiDollars they want to receive (or equivalently, how much DGB they want to lock). Optionally, they choose a lock duration (e.g., 90 days) if different durations are offered.
- **Fetch Oracle Price:** The wallet queries the current DGB/USD price (via getoracleprice RPC or from its synced block data). For example, suppose the price is $0.10 per DGB.
- **Calculate Required Collateral:** The system will determine how many DGB are needed to back the requested stablecoins. Typically, we might enforce an over-collateralization ratio for safety – for example, 150% collateralization like MakerDAO (meaning for $100 worth of stablecoin, $150 worth of DGB must be locked). The specific ratio can be a policy; to start, we might allow up to 100% (1:1) or require something like 133% to provide a buffer. Let’s say the user wants 100 DigiDollars (i.e., $100). At $0.10/DGB, $100 would be 1000 DGB at 1:1 backing. If we require 150% collateral, the user would need 1500 DGB. The wallet will calculate this and inform the user.
- **Transaction Construction:** The wallet creates a Mint transaction with the following parts:
  - **Inputs:** The user’s provided DGB inputs totaling at least the required collateral + fees. These could be UTXOs from the user’s wallet (just like funding a normal transaction).
  - **Collateral Output:** An output that locks the provided DGB amount. This output’s script is the key to the whole system. It will contain the conditions under which the DGB can be unlocked. A possible script template (using pseudocode for clarity) might be:

    ```php-template
    ScriptPubKey: 
      <LockDurationHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP 
      <CollateralKey> OP_CHECKSIGVERIFY 
      <StablecoinAmount> <OraclePrice> OP_DIGIDOLLARVERIFY
    ```

    Let’s break this down:  
    - `<LockDurationHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP` ensures that until a certain block height (current height + lock period), this script cannot be fulfilled by the owner alone. It basically enforces the time-lock. The OP_DROP removes the time value from stack after verification so it doesn’t interfere with subsequent ops.
    - `<CollateralKey> OP_CHECKSIGVERIFY` could be the public key of the original user (or a multi-sig of participants if needed). This means that to spend this output, the user’s signature is required (they remain the owner of the collateral). We include this so that both the stablecoin redemption and any eventual fallback require the user’s consent. (We might also design it so that redemption by others doesn’t require this – if we want any holder to redeem, we might omit this check or replace it with stablecoin proof; see discussion below).
    - `<StablecoinAmount> <OraclePrice> OP_DIGIDOLLARVERIFY` is a hypothetical opcode sequence that ensures the stablecoin conditions are met. We imagine OP_DIGIDOLLARVERIFY as an opcode that will only succeed if the spending transaction of this output is burning at least `<StablecoinAmount>` of DigiDollar tokens given the `<OraclePrice>`. Essentially, it checks that the output is being unlocked in tandem with the appropriate stablecoin burn.
    
    In summary, this script makes the output spendable only if (a) the time lock has expired or the user consents (depending on implementation), and (b) the required stablecoins are provided for redemption. We might refine this script later with Taproot (Section 11) to have multiple spend paths (one for normal redemption by anyone with tokens, one for owner reclaim after time).

  - **DigiDollar Output:** Another output represents the newly minted DigiDollar tokens delivered to the user. Since DigiDollar is not a built-in currency, we represent it via script. For instance, this could be a colored coin UTXO that carries the stablecoin value. An example script could be:

    ```php-template
    <UserStablecoinAddr> OP_DIGIDOLLAR <Amount> OP_TOKEN
    ```

    Here, `<UserStablecoinAddr>` might be a public key hash or script that locks the stablecoin to the user’s control (similar to how a normal output locks DGB to an address). `OP_DIGIDOLLAR <Amount>` indicates that this UTXO is a DigiDollar token of a certain amount. We might not literally have an OP_TOKEN opcode, but conceptually, this script is marked as a token rather than a normal coin. In practice, we might implement DigiDollar outputs as anyone-can-spend from the base layer’s perspective but with metadata. If using a new opcode, perhaps:

    ```php-template
    <AmountBytes> OP_DIGIDOLLAR <PubKeyHash> OP_DROP
    ```

    such that the presence of OP_DIGIDOLLAR triggers consensus to treat this output’s value field differently (maybe the DGB value is zero or dust). A simpler approach is to use an OP_RETURN output to represent the issuance and simultaneously credit the user’s wallet with the balance. But to keep it UTXO-based, let’s assume stablecoin outputs are actual UTXOs with special script. In any case, the output effectively says “X DigiDollars belonging to user Y”. The user’s wallet will recognize it and treat it as a balance of stablecoins.
  - **Change Output (optional):** If the user provided more DGB than needed (to cover fees or due to UTXO denominations), any leftover DGB is returned as a normal change output to the user’s wallet.
  - **Fee:** A small DGB fee is attached like a normal transaction to incentivize miners to include it. This fee must be in DGB; stablecoin cannot pay miner fees.
- **Transaction Signing and Broadcast:** The wallet then signs the inputs (the user’s DGB inputs) with the user’s keys. Note that the collateral output is encumbered by the user’s pubkey as well, which means to spend it later, the user will also need to sign. So the user’s signature is also effectively committing to the stablecoin issuance conditions. The transaction is broadcast to the network.

## Consensus Validation of Mint Transaction:  
When this transaction is broadcast and a miner attempts to include it, each validating node checks:

- The DGB inputs are sufficient and properly signed.
- The collateral output script is valid (no disallowed opcodes, etc.) and that the amount of DigiDollars requested is not more than allowed. Here, the node uses the oracle price: e.g., locked 1500 DGB, oracle price $0.10 means $150 value; user requested 100 DigiD (=$100) which is <= $150 allowed, so OK. If user tried to mint more than collateral value (violating required ratio), the OP_DIGIDOLLARVERIFY or equivalent logic would fail or a custom consensus rule would catch it (like “Collateral * Price * (1/ratio) >= StablecoinAmount”).
- The stablecoin output is well-formed (e.g., correct format, amount field consistent with what the transaction said).
- The total DigiDollar supply increase equals the stablecoin output amount, and total system collateral increases accordingly (some nodes might keep running totals, but that’s not strictly necessary if each tx individually is correct).

If all checks pass, the transaction is valid and can be mined. After confirmation, the DigiDollar is officially in circulation.

## Minting Example:  
To illustrate with numbers, assume no over-collateral for simplicity: Alice locks 1000 DGB when price is $0.10, to mint 100 DigiDollar (DD). The Mint transaction might look like:

- **Inputs:** [Alice’s 1005 DGB] (1000 for collateral, ~5 DGB for fees and change).
- **Outputs:**
  - **Collateral:** 1000 DGB to script: `H=height+43200 OP_CLTV OP_DROP AlicePubKey OP_CHECKSIGVERIFY 100 OP_DIGIDOLLARVERIFY`. (This locks 1000 DGB until ~30 days (assuming 43200 blocks at 15s each) and requires 100 DD to redeem).
  - **DigiDollar token:** 0 DGB (or dust 0.0001 DGB) to script: `OP_DIGIDOLLAR 100 OP_DROP <AlicePubKeyHash> OP_CHECKSIG`. (This is a token output indicating Alice has 100 DD; her signature is required to spend it, meaning she “owns” these tokens).
  - **Change:** 4.9 DGB to Alice’s normal DGB address (if 0.1 DGB was fee, for example).

Alice’s wallet now shows 100 DigiDollar balance and 4.9 DGB change. The 1000 DGB is no longer spendable by normal means; it’s locked under the stablecoin contract.

## DigiDollar Transfer (Sending and Receiving)

Once DigiDollars are minted, they can be sent to others just like a cryptocurrency:

- If Alice wants to pay Bob 50 DigiDollar, her wallet will create a Transfer transaction. The input will be her DigiDollar UTXO (the one with 100 DD). To spend it, Alice must provide the unlocking script that satisfies the output’s locking conditions. In our example output script, it likely required Alice’s signature (OP_CHECKSIG). So she signs it, indicating she’s spending those stablecoins.
- For outputs, since she’s sending 50 to Bob, and perhaps keeping 50 as change, the outputs would be:
  - 50 DigiDollar to Bob’s stablecoin address (which might be Bob’s pubkey hash embedded in a similar OP_DIGIDOLLAR output script).
  - 50 DigiDollar back to Alice as change (if she doesn’t send all).
- She also needs to include a DGB input to pay the transaction fee (since the stablecoin input itself does not carry DGB value to pay miners). So a small input of, say, 0.1 DGB from her wallet is added, and an equivalent fee output (or just leaving it as fee via difference).

After signing (Alice signs the DGB input with her key and the stablecoin input with her stablecoin key which is the same as her DGB key if we used pubkey hash, or anyway she has the key), she broadcasts the TX.

Nodes validate that the stablecoin input (100) equals the outputs (50+50) stablecoin (no loss or gain), and that the DGB in equals DGB out + fee. They also validate the scripts: Alice’s stablecoin input had OP_DIGIDOLLAR script which is now being consumed – likely the interpreter sees that and requires that outputs properly carry forward the token. This could be enforced by requiring the sum of OP_DIGIDOLLAR `<amt>` in outputs equals the input `<amt>` for stablecoin. If valid, the TX is mined.

Bob’s wallet, upon seeing the confirmed TX, now recognizes he has an output with 50 DigiD. It will show 50 DigiDollar in his balance.

This transfer is “free” in the sense that neither Alice nor Bob paid anything except the minor DGB network fee. There’s no stability fee or other charge by the protocol for normal sends.

## User Warnings for External Transfers:
If Bob were using an outdated wallet or an exchange that doesn’t support DigiDollar, sending these tokens could result in loss (since an unaware wallet might ignore or accidentally burn the OP_DIGIDOLLAR output). Therefore, the core wallet will include warnings/pop-ups if the user attempts to send DigiDollars to an address that is not recognized as supporting stablecoin. This could be heuristically determined (perhaps DigiDollar addresses have a distinguishable format or prefix if using a new version of address encoding). Ideally, Bob should also use a DigiByte wallet updated for DigiDollar. The UI will emphasize safe usage, and perhaps maintain a whitelist of known services supporting DigiDollar.

## Redemption Process (Burning DigiDollars to Unlock DGB)

Redeeming is the reverse of minting: a user destroys a certain amount of DigiDollar and gets the equivalent value in DGB from the locked collateral. Let’s say Bob now wants to redeem his 50 DigiD for DGB:

- Bob’s wallet finds a corresponding collateral UTXO that can be unlocked by burning 50 DigiD. The challenge here is if stablecoins are globally fungible or tied. We have design choices:
  - **Direct Redemption from Specific Collateral:** If Bob’s 50 DigiD originally came from Alice’s locked 1000 DGB, Bob could redeem from Alice’s collateral. This requires that the collateral script allows anyone with DigiD tokens to unlock. If Alice’s collateral script included her pubkey (as in our earlier example), then it implies only Alice can sign to redeem, which would block Bob. To allow Bob, we might design the collateral script differently: instead of requiring Alice’s signature, require proof of stablecoin ownership. Perhaps the script could say: “to spend this output, present a proof that X stablecoins are burned”. In practice, this could be an inclusion of the stablecoin UTXO in the same transaction. If Bob includes Alice’s collateral output as an input in his transaction and also includes the 50 DigiD input, the script for the collateral could check that the stablecoin input amount ≥ some threshold. Implementing that in Bitcoin Script is tricky, but with a new opcode we can coordinate it.
  - **Global Pool Redemption:** Alternatively, if the system treats all DigiDollars as collectively backed by all collateral, Bob could redeem from a common pool. But since each collateral is an individual UTXO, he’d have to pick one or multiple to draw from. A pragmatic approach is that the wallet software picks the same UTXO that originally backed those stablecoins if possible (traceable via an ID). If partial, it can redeem partly. Or, the system might allow merging collateral pools, which we won’t cover here for simplicity.

Bob’s wallet constructs a Redeem transaction:

- **Inputs:**
  - Bob’s 50 DigiDollar UTXO (from the transfer he got). This represents the stablecoins he’s returning/burning.
  - The corresponding Collateral UTXO (Alice’s 1000 DGB locked). Bob doesn’t have Alice’s private key, but he doesn’t need it if the script is such that providing the stablecoin is sufficient (i.e., the script might not require Alice’s signature for redemption path, only for an alternative path like time expiry).
- **Outputs:**
  - Bob receives DGB equal to 50 USD worth, based on current price. Suppose the price at redemption time is still $0.10 (for simplicity). Then 50 DigiD corresponds to 500 DGB. So one output will pay 500 DGB to Bob’s DGB address.
  - Another output may return the leftover collateral (if any) locked back under the same conditions for the remaining stablecoin amount. For example, after redeeming 50 out of 100 DigiD from that position, there are 50 DigiD still outstanding and originally 1000 DGB collateral. Assuming we release 500 DGB, there should be 500 DGB left still locked to back the remaining 50 DigiD. So we would create a new collateral output locking 500 DGB with the same script adjusted for 50 DigiD outstanding. In effect, Bob’s redemption transaction splits Alice’s original collateral UTXO: one part goes to Bob (freed), the other remains locked.
  - If any fees need to be paid, Bob (or the collateral) must provide. Likely Bob will also include a small DGB fee input of his own if needed. But since he’s receiving DGB, perhaps a tiny portion of that could be set aside for fee.
  
  Note: Bob could also redeem the full 100 if he had them, which would release all collateral and remove that UTXO entirely.

## Validation:  
Nodes validate this redemption carefully:

- The stablecoin input is 50; it is consumed and not re-issued in outputs (so supply decreases by 50).
- The collateral input script is evaluated. In our earlier example script, OP_DIGIDOLLARVERIFY would check that at least 100 stablecoins are provided – but Bob only provided 50. So that script as initially written would fail. We need a script that allows partial redemption. One solution: the script could allow spending with fewer stablecoins if accompanied by a new locked output carrying forward the remainder. This implies some covenant-like behavior (making sure the output still has the same script with updated values). A simpler approach: disallow partial redemption directly; require full redemption of a collateral lot in one transaction. But that reduces flexibility. We likely want partial. We might achieve partial by an iterative approach: maybe Alice (collateral owner) has to cooperate to split the collateral if needed. However, to keep it trustless for Bob, the script path for any redeemer should exist.
- For now, let’s imagine the consensus can handle partial via the transaction as constructed. The result after the transaction: 50 DigiD is gone, Bob has 500 DGB, Alice’s collateral is now only 500 DGB locked for the remaining 50 DigiD.
- The script for the new collateral output (500 DGB) might be automatically enforced by the OP_DIGIDOLLARVERIFY opcode—i.e., it might require that any spend of the original collateral must output any leftover collateral with a script that still binds the remaining stablecoin. This is a kind of covenant (where an input constrains the outputs). If Bitcoin’s proposed OP_CHECKOUTPUT or OP_CTV existed, it would help. Without it, our new opcode might implement a custom check: verifying the new output’s script has the correct updated stable amount. This is complex but doable at consensus level.
- The oracle price is also checked. If price had changed, the number of DGB released for 50 DigiD would differ. For example, if price fell to $0.08, then $50 would require 625 DGB to redeem. But only 1000 DGB was total; if 625 is taken, 375 left for 50 remaining stable (which is undercollateralized actually – that position would be in trouble). Ideally, to maintain 150% collateral, the system might not even allow redemption if it would drop the remaining collateral below the required ratio… these edge cases can be handled by governance or a liquidation mechanism beyond scope here. For now, assume moderate price moves or user being prompt.
  
Provided all these conditions hold, the transaction is valid.  
After Redemption: Bob receives his 500 DGB (which he can now spend freely), and the DigiDollar supply decreases by 50 (from 100 to 50). Alice’s position now backs only 50 DigiD with 500 DGB collateral. If Alice wanted to, she could redeem the remaining 50 DigiD (if she holds them) or someone else holding them can, or she might need to top-up if price changes, etc.

It’s evident that the redemption logic is the most complex part of the design due to handling partial redemptions and ensuring fairness. In a simpler model, we might enforce that only the original minter can redeem, but that breaks the “non-custodial” aspect for others. Alternatively, we could require full collateral lots to be redeemed in one go – meaning stablecoins from one issuance can’t be partially redeemed by multiple parties easily. However, that reduces liquidity (one might have to gather all tokens from that issuance to redeem, which is like an ERC20 with unique lots).

For the scope of this whitepaper, we assume the script/consensus is capable of handling proportional redemption to fulfill the non-custodial promise: any holder of DigiDollar can trustlessly convert their tokens back to DGB at the current market rate. This might be achieved by a design where collateral is pooled or at least any stablecoin can trigger a collateral release (with perhaps the original owner’s signature not required on that code path).

## Transaction Structure Summary:

- **Mint TX:** Standard inputs of DGB -> outputs: [Time-locked collateral (DGB), DigiDollar token (data), Change (DGB, if any)]. Includes OP_CHECKLOCKTIMEVERIFY and stablecoin verification in script.
- **Transfer TX:** Inputs: [User’s DigiDollar token UTXO, plus a DGB input for fee] -> outputs: [New DigiDollar UTXOs to recipients, change in DigiDollar if any]. Only DGB fee output (or implicit) as needed. Ensures stablecoin amount conserved.
- **Redeem TX:** Inputs: [DigiDollar token(s), Collateral UTXO(s), possibly fee DGB] -> outputs: [DGB back to redeemer, any residual collateral re-locked, maybe change stablecoin outputs if combining]. Total DigiDollar in inputs > outputs (difference is burned), DGB outputs correspondingly released.

Below is a sample script illustrating a possible collateral output script, highlighting how the redemption might be enforced (pseudo-code for concept):

```yaml
# Collateral locking script template (simplified example)
# Variables (to be replaced with actual values in script):
#  - CollateralPKH: PubKeyHash of collateral owner (or could be 0 if not needed for redemption)
#  - Expiry: block height after which owner can reclaim (for emergency)
#  - TotalStable: total stablecoin amount this collateral backs (in smallest unit, e.g., cents)
#
<Expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP 
OP_IF
    # If redeem path (before expiry)
    <TotalStable> OP_DROP        # maybe push the value for internal check
    <OraclePubKey1> <OracleSig1> OP_CHECKSIGVERIFY   # (pretend we can verify oracle sigs here in script for current price)
    <OraclePubKey2> <OracleSig2> OP_CHECKSIGVERIFY   # Need a mechanism to get price, assume done via opcode or passed in
    # Pseudocode: verify provided stablecoin burn >= TotalStable or appropriate portion:
    OP_VERIFYSTABLEBURN          # custom opcode: checks that stablecoin inputs in this tx >= TotalStable
OP_ELSE
    # Else branch: after Expiry, allow owner to reclaim regardless of stablecoin (this is risky, maybe only if stablecoin =0 or with penalty)
    <CollateralPKH> OP_CHECKSIG  # owner can spend after time (assuming they have likely bought back or stablecoin expired)
OP_ENDIF
```

In practice, it’s likely cleaner with Taproot to have separate spending paths (one that requires stable burn, one that requires time lock + owner sig). Above script is a conceptual mix in legacy style. The actual implementation may lean on direct consensus checks rather than script opcodes for verifying stablecoin burns and oracle data, because Bitcoin script isn’t currently capable of such global checks without new opcodes.

Despite the complexity, the net effect is straightforward for users:

To mint: Lock DGB and get stablecoins.  
To send: Use stablecoins like normal coins (with the wallet hiding the technical details).  
To redeem: Send stablecoins back to a redemption address (possibly an integrated function in wallet), receive DGB at market rate.  
Throughout these processes, the data structures and function names in code would be extended. For example, we might have new functions in DigiByte Core such as CreateStablecoinMintTx(amount, duration) in the wallet API, and validation functions like CheckStablecoinTx(const CTransaction& tx, CValidationState& state) that encapsulate the consensus checks. The RPCs described in Section 8 will allow developers to query the state (total supply, collateral, etc.) to verify that the system is sound at any point.

# 7. DigiByte Core GUI Enhancements
To make DigiDollar accessible to everyday users, the DigiByte Core wallet GUI will be enhanced with dedicated interfaces for the stablecoin. The goal is to seamlessly integrate DigiDollar management without requiring users to manually craft transactions or understand script details. Below are the planned GUI components:

## DigiDollar Dashboard: 
A new section in the wallet (likely a tab or panel) will provide an overview of the user’s DigiDollar balance and collateral positions. This dashboard will display:

- **DigiDollar Balance:** The total amount of DigiDollar the user currently holds, presented in USD (since 1 DigiDollar ≈ $1). This will appear alongside the traditional DGB balance.
- **Collateral Locked:** If the user has minted DigiDollars, the dashboard will list the user’s active collateral positions. For each position, show the amount of DGB locked, the amount of DigiDollar issued against it, the lock expiry date (if any), and the current collateral-to-loan ratio. For example: “1500 DGB locked for 100 DigiD, Lock expires: Jan 1, 2026, Collateral Ratio: 150%.”
- **Mint & Redeem Buttons:** Prominent buttons or forms to “Mint DigiDollar” and “Redeem DigiDollar.” The Mint form will allow the user to input an amount of DGB to lock or an amount of DigiDollar to receive (with the form computing the other side and showing the required collateral and perhaps allowing selection of lock time). The Redeem form will allow input of how many DigiDollar to redeem (or selection of “redeem all”) and show the expected DGB that will be released.
- **Price Info:** The current oracle price (DGB to USD) will be shown on the dashboard so users know the conversion rate being used. Possibly: “Oracle Price: 1 DGB = $0.1234 (updated 15 seconds ago)”.
- **Warnings/Status:** The dashboard may display system-wide info like total supply (for curiosity) and warnings if, for example, any of the user’s positions are undercollateralized due to price drops (e.g., highlight in red “Collateral ratio fell to 110%, consider adding collateral or redeeming”).

This DigiDollar dashboard provides a one-stop view for stablecoin management, analogous to how a DeFi app would show your balances and loans, but built into the native wallet.

## Transaction History Integration
The wallet’s transaction history will be updated to clearly label DigiDollar-related transactions:

- Minting transactions could be labeled as “Minted DigiDollar” with details like “Locked X DGB to receive Y DigiDollar.”
- Redemption transactions labeled as “Redeemed DigiDollar” with something like “Burned Y DigiDollar to unlock X DGB.”
- Transfers of DigiDollar labeled as “Sent DigiDollar” or “Received DigiDollar” analogous to normal send/receive, but with a different icon or color to distinguish from DGB transfers.
- Possibly filter checkboxes or tabs to show only DigiDollar transactions, since some power users might want to separate them from the flood of normal transactions.

Each DigiDollar transaction entry can display the amount in DigiDollar and possibly an approximate USD or DGB equivalent for context. For example, “Sent 50 DigiDollar (≈ 50 USD) to address D...abcd”. The wallet will detect transactions that have OP_DIGIDOLLAR outputs or similar markers to classify them accordingly in the UI.

## Address Book and QR Codes
The wallet will support DigiDollar addresses similar to DGB addresses. If DigiDollar uses the same address format (which it might if we simply reuse pubkey hashes), there may not be a distinction. However, we might introduce a prefix or a notation to indicate an address is specifically for stablecoin. For instance, a user might have a single key that can control both DGB and DigiDollar, so the address is same. But to be safe, we might generate distinct receiving addresses for DigiDollar (with a tag in the wallet like “Stablecoin address” which could be the same string as a normal address but the wallet knows to use it in token outputs). The GUI will allow users to request DigiDollar payments, showing a QR code or URI that encodes an address and amount in DigiDollar terms (e.g., `digibyte:DigiDollar:<address>?amount=50)`. This might be an extension of BIP21 URIs or a new scheme.

## Sending DigiDollar
The send screen will have an option or toggle to send DigiDollar instead of DGB. For example, a dropdown to choose currency: DGB or DigiDollar. When DigiDollar is selected:

- The amount field is denominated in DigiDollar (effectively USD).
- The From account is the user’s DigiDollar balance.
- The wallet will automatically handle adding a DGB input for network fee if needed, and warn if the user has no DGB for fee.
- If the user pastes an address, the wallet might try to detect if that address is capable of receiving DigiDollar. If uncertain, a warning as mentioned will pop up: “Warning: You are sending DigiDollar to an address that may not support it. Only send DigiDollar to DigiByte addresses that you trust are using updated software.” The user has to confirm acknowledgement.

## Enhanced User Warnings and Safeguards
- If the user attempts to send their locked collateral (which they shouldn’t directly, as it’s encumbered), the wallet will likely hide those UTXOs from the “spendable balance” to prevent accidental attempts. Instead, redemption must be done via the interface.
- If the user’s collateral ratio is dropping (meaning a risk of insolvency), the wallet might display alerts like “Collateral for 100 DigiD has fallen to 110% of its value. If DGB price falls further, your DigiDollar may become undercollateralized. Consider adding more DGB to collateral or redeeming some DigiDollar.” While the protocol might not have a direct “add collateral” method (we could allow a transaction that just increases the DGB in a locked output without changing stable amount), the user could effectively add collateral by paying back some DigiD (redeem partially) to raise the ratio.
- When lock expiration is approaching for a position, a notification could inform the user: “Collateral lock for 1000 DGB (minted 600 DigiD) expires in 5 days. After expiry, collateral may be reclaimable by you even if DigiD is not returned (which could leave DigiD unbacked). It’s recommended to redeem the DigiDollar or renew the lock.” This reminds them to maintain stability or roll over the lock.

## DigiDollar Network Status (optional)
Possibly a GUI element showing network-wide stats (like total DigiDollar supply, system collateral level, current price feed health). This might not be crucial for average users but is nice for transparency. It could be part of an “Advanced” section or the debug window.

By implementing these GUI enhancements, we ensure that users without technical knowledge can safely use DigiDollar:

- Minting is as simple as filling a form and clicking “Mint,”
- Sending stablecoins is as straightforward as sending DGB,
- Redeeming is a guided process rather than manually constructing a special transaction.

The UI will heavily emphasize clarity, since dealing with locked funds and a new asset could be confusing. Labels, tooltips, and documentation (perhaps an integrated help explaining what DigiDollar is) will accompany these features. The formal, academic details (like script conditions or oracle details) are abstracted away in the GUI, but the wallet might provide power-user tools (like a console command to manually create a stablecoin script) for advanced experimentation.

In summary, the core wallet will treat DigiDollar as a first-class citizen alongside DGB, providing an intuitive interface for all operations (mint, send, receive, burn) and safeguarding the user with appropriate warnings when crossing the boundary between stablecoin-aware and unaware contexts.

# 8. New RPC Calls for Monitoring
To support developers, exchanges, and power users in monitoring the DigiDollar system, several new RPC (Remote Procedure Call) commands will be added to DigiByte Core. These RPC calls provide information about the stablecoin’s state and allow retrieval of relevant data for wallets or analytical tools. Below we describe each new RPC call and the details it returns:

## getdigidollarstats 
This RPC provides a summary of the DigiDollar stablecoin’s overall status on the network. It returns data such as:

- **total_locked_dgb:** The total amount of DGB (in whole coins or satoshis) currently locked as collateral for DigiDollar. This gives an idea of how much DGB supply is tied up backing the stablecoin.

- **total_digidollar_supply:** The total circulating supply of DigiDollar tokens. This would be equal (in USD units) to the value of DGB locked times collateral ratio (minus any system over-collateralization). Essentially, how many DigiDollar exist.

- **average_collateral_ratio:** (optional) The average or minimum collateralization ratio across all positions, to gauge system health.

- **breakdown_by_lock_duration:** A breakdown of the above figures categorized by lock duration or type of collateral contract. For example, it could be a JSON object like:

```ruby
{
  "1_month": {"locked_dgb": 500000, "digidollar": 300000},
  "3_months": {"locked_dgb": 1000000, "digidollar": 600000},
  "6_months": {"locked_dgb": 2000000, "digidollar": 1200000},
  "12_months": {"locked_dgb": 500000, "digidollar": 250000},
  "no_expiry": {"locked_dgb": 100000, "digidollar": 50000}
}
```
This example shows how much is locked in different time buckets. It helps to see, for instance, if most people lock short-term or long-term.

- **oracle_count:** (optional) Number of active oracles or last known oracles providing price.

- **last_price:** (maybe better provided by getoracleprice, see below).

This RPC basically gives a high-level dashboard programmatically. An exchange might use getdigidollarstats to see if the stablecoin is growing and collateralized, etc. All values are likely returned as strings or numeric values (the same way Bitcoin RPCs return supply stats).

Example usage:

```ruby
$ digibyte-cli getdigidollarstats
{
  "total_locked_dgb": 3500000,
  "total_digidollar_supply": 2300000,
  "breakdown_by_lock_duration": {
    "1_month": {"locked":1500000, "stable":1000000},
    "3_months": {"locked":1000000, "stable":750000},
    "6_months": {"locked":800000, "stable":500000},
    "12_months": {"locked":200000, "stable":50000}
  },
  "average_collateral_ratio": 152.3
}
```
This indicates 3.5 million DGB locked, 2.3 million DigiDollar out, etc.

## getoracleprice
This RPC returns the most recent DGB/USD price that the DigiDollar system is using. It likely includes:

- **price:** The current price in numeric form (e.g., 0.101234 USD per DGB). Could be given as a float or as an integer (like 101234 in units of 1e-6 USD).
- **last_update_height:** The block height of when this price was updated from the oracles.

- **last_update_time:** The timestamp of the price update.

- **status:** If the price is fresh, stale, or using fallback. For example, "status": "active" if updated this block, or "stale for 3 blocks" if it’s been reused for a few blocks.

- **oracle_signers:** Possibly the list of oracle identifiers that contributed.

This RPC is useful for wallets that want to display the current conversion rate or for anyone verifying the oracles. It could also be used to check if the oracles are functioning (if status shows stale for too long, something is wrong).

Example:

```ruby
$ digibyte-cli getoracleprice
{
  "price": 0.101234,
  "last_update_height": 1450000,
  "last_update_time": 1700000000,
  "status": "active",
  "oracle_signers": ["oracle1", "oracle2", "oracle3"],
  "aggregated_from": {"binance":0.101, "bittrex":0.102, "kucoin":0.1018, "coinbase":0.100}
}
```

## getdigidollartransactions
 This RPC allows filtering and retrieving transactions related to DigiDollar in the wallet or in the blockchain:

If called without arguments, it could default to listing recent DigiDollar transactions in the user’s wallet (similar to how listtransactions works but filtered).

**Possible parameters:** count, skip for pagination, include_watchonly, etc., similar to listtransactions.
It might also accept a filter argument, e.g., type which could be “mint”, “burn”, “transfer”, or an address to filter by.
For each transaction, it would return details such as txid, type (Mint/Burn/Transfer), amount of DigiDollar involved, collateral amount (if applicable), time, confirmations, and maybe the involved addresses.
Alternatively, we might have specialized calls:

listlockeddgb to list the user’s collateral outputs and details.
liststablecoinbalances to list stablecoin UTXOs under wallet control.
However, the user specifically mentioned getdigidollartransactions for history filtering. So likely:

```ruby
$ digibyte-cli getdigidollartransactions 10 0
[
  {
    "txid": "abcd1234...",
    "type": "mint",
    "digidollar_amount": 100.0,
    "locked_dgb": 150.0,
    "lock_duration": "3_months",
    "confirmations": 12,
    "time": 1699990000,
    "details": {"to":"StableAddr1...", "collateral_change":"DGBAddr..."}
  },
  {
    "txid": "efgh5678...",
    "type": "transfer",
    "digidollar_amount": 50.0,
    "from": "StableAddr1...",
    "to": "StableAddr2...",
    "confirmations": 3,
    "time": 1700000000
  },
  {
    "txid": "zzzz9999...",
    "type": "redeem",
    "digidollar_amount": 50.0,
    "unlocked_dgb": 60.0,
    "to": "DGBAddrXYZ...",
    "confirmations": 1,
    "time": 1700001000
  }
]
```

This example shows one mint, one transfer, one redeem. The fields include what was locked/unlocked. This RPC helps users or tools audit their DigiDollar activity. An explorer or monitoring tool could also use this to track network usage of stablecoin (though an explorer likely would parse the blockchain directly rather than RPC).

# Integration with existing RPCs:

The existing getbalance or listunspent might be updated to reflect stablecoin balances. Possibly getbalance could have multiple accounts or entries like “DGB”: X, “DigiDollar”: Y. Alternatively, new RPCs like getdigidollarbalance for wallet's own stablecoin holdings might be introduced for clarity.
decoderawtransaction should be updated to decode new opcodes (like showing “OP_DIGIDOLLAR 100” etc.) so that when users decode a stablecoin transaction, they see the human-readable meaning.
validateaddress might indicate if an address is involved in stablecoin.

## Security of RPC data:
The RPC calls do not reveal private info beyond what’s needed. For example, getdigidollarstats is likely only available in a full node (not something a lightweight client can get unless they trust an API) because it requires scanning the UTXO set or maintaining counters in memory. It might be a relatively heavy call unless we maintain running totals. But since nodes already track supply or could easily sum outputs with OP_DIGIDOLLAR (because each such output's amount is known), it should be fine. We might maintain these stats incrementally at block connect/disconnect for efficiency.

# 9. DigiDollar Transactions & Fee Structure
In designing DigiDollar transactions, we aim to make the stablecoin as easy and cost-effective to use as DGB itself, while also preserving network integrity through appropriate fees. Here we discuss how DigiDollar transactions are handled in terms of fees and what rules are set to prevent spam and ensure sustainability.

## Free Transferability of DigiDollars:
DigiDollar tokens, once minted, are intended to be freely transferable between any DigiByte addresses. “Free” in this context means that the protocol itself does not impose any additional charge or toll on moving DigiDollars around (no built-in transfer tax or seigniorage). If Alice sends 10 DigiDollar to Bob, Bob receives exactly 10 DigiDollar, with no deduction. This is important for DigiDollar to function as a true currency – users can pass it around just like they do with any crypto or fiat, and it always retains its full value. The only cost in transferring comes from the standard network transaction fee that miners require to include the transaction in a block.

## Requirement of DGB Fees for DigiDollar Transactions:
The DigiByte network uses DGB for transaction fees (miners are paid in DGB). That does not change with DigiDollar transactions. Any transaction that involves DigiDollar outputs or inputs must still include a sufficient amount of DGB as a fee to be relayed and mined. This is analogous to how token transactions on Ethereum still require ETH for gas. For example, if Alice is sending Bob 100 DigiDollar, her transaction might have:

- An input of her 100 DigiDollar UTXO.
- An output of 100 DigiDollar to Bob.
- Separately, an input of a small amount of DGB (say 0.05 DGB) from Alice’s normal balance.
- No DGB change output (meaning 0.05 DGB is left as fee). From the miner’s perspective, they see that this transaction pays 0.05 DGB in fees, so it’s acceptable (assuming that meets the min fee rate per kB). The presence of DigiDollar is incidental to the miner unless they run a policy to prefer or limit them.

This implies a usability consideration: users must have some DGB dust to move DigiDollars. If someone only holds DigiDollar and has zero DGB, they cannot directly pay the network fee to send a stablecoin transaction. This is the same scenario as an ERC-20 token holder needing ether gas. We will address this by:

- Encouraging users to retain a small DGB balance for fees (the GUI can warn or automatically reserve some when minting).
- Possibly enabling a feature where when a user mints DigiDollar, the wallet can optionally reserve a tiny portion of their DGB collateral as a separate output just for future fees. For instance, if Alice locks 1000 DGB to mint, the wallet might not convert 100% of that to stablecoin; it could leave, say, 0.5 DGB aside in her wallet to cover transaction fees for a while.
- Future work might consider fees-in-token (like letting miners accept DigiDollar fees), but that complicates consensus – miners would need to trust the stablecoin’s value or convert it, etc. – likely not doing that initially.

## Fee Structure and Amounts:
DigiByte’s fee structure (as noted in v8.22.0) is around 0.1 DGB per kB minimum. DigiDollar transactions might be slightly larger in size due to extra script data (e.g., the OP_DIGIDOLLAR marker and amount). However, they are not huge – likely similar to an extra output with some bytes of data. For instance, a transfer of stablecoin might be ~200 bytes. At 0.1 DGB/kB, that’s 0.02 DGB fee, which is a few cents (given DGB’s price usually low). This is negligible for typical transactions, ensuring DigiDollar is cheap to use.

We should however consider optimal fee structure in terms of:

- **Preventing spam:** If fees are too low and there's no other barrier, someone could flood the network with tons of tiny stablecoin transactions. On DigiByte, block time is short and capacity is quite high (especially with SegWit and 15s blocks, throughput is significant). But spam could still bloat the UTXO set if someone creates millions of dust stablecoin outputs.
- **Dust and Minimum Output:** Normally, Bitcoin-derived nodes have a dust threshold – outputs less than a certain value (like ~5460 satoshi for a typical P2PKH) are not relayed unless OP_RETURN. For stablecoin outputs, if they carry 0 DGB value and rely on script, they violate the typical dust rules (value 0 is dust). We will adjust the policy to allow 0-value outputs if they contain OP_DIGIDOLLAR (treat them similar to OP_RETURN in terms of relay, since they are intentional and have value in the separate domain). Alternatively, require a tiny DGB (like 1 satoshi or 5460 sats) in each stablecoin output to make it non-dust. That would mean every stablecoin UTXO has a negligible DGB included. But that might complicate tracking and redemption slightly. It's likely easier to exempt stablecoin outputs from dust rules and handle them via new policy.
- **Fee Rate for Stablecoin TXs:** We probably stick to using the same fee rate as normal transactions. There’s no need for a special fee rate just for stablecoin. Miners will treat them equally. If stablecoin TXs become a large part of traffic, miners might choose to raise fees generally if blocks get full, which applies to all.
- **No Additional Protocol Fees:** Some stablecoin systems charge a stability fee or a mint/burn fee (e.g., Djed charges a fee on every mint and burn to build reserves; Maker charges interest via MKR). For simplicity, DigiDollar v1 will not have any extra protocol-level fees beyond the normal network transaction fee. Minting 100 DigiDollar yields exactly 100 to the user (their cost is opportunity cost of locking DGB). Redeeming 100 DigiDollar yields exactly $100 in DGB (no fee taken out) – again the only cost was the transaction fee to do the redemption. This makes the system simpler and user-friendly, though in long term, a stability fee could be introduced through governance if needed to fund oracle operation or other costs.
- **Optimal Fee from a Sustainability Standpoint:**
  We want to ensure that DigiDollar transactions are not abusing the network by being feeless or extremely low fee compared to the resources they consume:
  - Each stablecoin UTXO likely is small in size (32 bytes outpoint, maybe 20-30 bytes script), so similar to a normal output. It does add to UTXO set size. If stablecoin usage grows, thousands or millions of additional UTXOs could exist (the collateral ones and the token ones). That’s a trade-off for adding functionality. As long as each came with a fee to pay miners, it’s fair usage.
  - We may consider requiring a slightly higher minimum fee for stablecoin minting transactions because they introduce a new UTXO (the collateral) that might remain for a long time (locked coins). But Bitcoin’s model doesn’t differentiate UTXO longevity for fees (though some have proposed ideas like that). It might be overkill to differentiate.
  - The network can rely on market-driven fees: if someone spams lots of stablecoin dust TX, they’d have to pay DGB fees for each, which deters large-scale spam unless they burn a lot of money doing so.

## Preventing DigiDollar Spam/Dust:
To explicitly address spam:

- We can enforce a minimum DigiDollar output amount to avoid tiny outputs that bloat the UTXO set or mempool. For example, we might say the minimum stablecoin output is 0.01 DigiDollar (one cent). But that is already extremely low. Perhaps a higher minimum like 1 DigiDollar to discourage creating outputs of a few cents. However, that might be too restrictive and unnecessary if fees handle it.
- Perhaps more relevant: minimum mint amount. We might not want users to create a collateralized position for say 0.1 DigiDollar – the overhead is not worth it. So we could require that at least, say, $10 or $50 of DigiDollar be minted in one go. This ensures each collateral UTXO has a meaningful size and we don’t get millions of micro-loans. This is similar to how Maker has a minimum debt size to avoid spam vaults. We can tune this parameter.
- For transfers, if someone tries to split 1 DigiDollar into 100 outputs of 0.01 each, the fee per output would overshadow the usefulness, so economic disincentive is there.

### Fee Structure Summary:
- All DigiDollar transactions require normal DGB fees. No free lunch in terms of block space.
- No extra protocol fees or charges for using stablecoin beyond what miners collect.
- The existing fee rate (0.1 DGB/kB default) likely suffices; no new fee calculation.
- Potential policy: a small minimum stablecoin amount for issuance or output to reduce dust.

## Sustainability Considerations:
We want DigiDollar transactions to remain lightweight and not burden the network:

- If DigiDollar usage drives a lot of volume, there might be an argument to adjust fees or block size. For now, we trust the default and market dynamics.
- If DigiDollar sees DeFi-level usage of thousands of TX per day, the fee costs are negligible for users, which is a competitive advantage (cheap to use). But if usage soared to the point of saturating blocks, miners would raise required fee (market-driven) which would naturally throttle spam.

### Example of Fee in Context:
If Alice mints stablecoin, that transaction might be around 250 bytes, incurring say 0.025 DGB fee (a few pennies). If she later sends stablecoin to 5 friends (5 outputs), maybe a 300-byte TX, maybe 0.03 DGB fee. These fees are minimal. Even if DigiDollar sees heavy usage, the fee costs remain a small fraction compared to the value transferred.

# 10. Security Considerations
Introducing a decentralized stablecoin at the protocol level raises several security issues that must be carefully considered. These include oracle price manipulation, blockchain reorganization handling, protection against malicious or accidental stablecoin usage patterns, and ensuring the stablecoin’s integrity even under adverse conditions. We address each of these below.

## Oracle Price Manipulation and Trust:
The oracle system is arguably the most vulnerable point, as DigiDollar’s correctness depends on accurate price feeds. If an attacker could manipulate the reported DGB/USD price, they might profit by minting or redeeming DigiDollars at false rates. For instance, if they push the oracle price too high, they could lock relatively fewer DGB to mint a large amount of DigiDollar (effectively getting more USD value than the collateral is truly worth, then potentially defaulting if price corrects). Conversely, if they push the price too low, they could redeem DigiDollar for more DGB than they should receive (draining collateral). To mitigate this:

- **Multiple Oracles & Medianization:** As described in Section 5, using multiple independent oracles and taking a median price provides resilience. An attacker would need to corrupt a majority of oracles or their data sources simultaneously to significantly skew the median. Each oracle could be run by a separate entity, making collusion difficult.
- **On-Chain Verification of Oracle Identities:** Only signatures from known oracle public keys are accepted. This prevents random nodes from injecting false prices. The initial set of oracles would be chosen for their reputation and security track record.
- **Rate Limiting Price Changes:** We could implement a rule that the oracle price used for stablecoin operations cannot change by more than a certain percentage per block (or per minute) unless a majority of oracles confirm it. For example, limit price movement to 5% per block. If the market truly moves faster, the oracles will still report it, but maybe require an additional block to fully reflect. This slows down potential exploitation of flash crashes or spikes.
- **Auditing and Transparency:** All price inputs and oracle signatures are recorded on-chain. This means the community can audit in real-time: if an oracle posts an outlier price, it will be visible and can be investigated. If an oracle misbehaves, it can be flagged and replaced.
- **Incentive Alignment:** Oracles might be required to stake DGB or have some incentive (like being rewarded with small fees or newly minted DGB for their service). If they misbehave, they lose reputation and potentially the value of their stake.

## Handling Blockchain Reorganizations:
Blockchain reorgs (where a set of blocks get replaced by an alternate chain due to a longer chain found) can impact DigiDollar in a few ways:

- **Oracle Price Differences:** If two chains diverge, they might have different oracle inputs if blocks were solved at different times. This means a stablecoin transaction that was valid in one chain with price P might become invalid in another chain if the price was Q and conditions differ. In such cases, the transaction simply would not exist on the main fork (it might get rejected or never included). The user would have to resubmit given the new price.
- **Redeem/Collateral Reorg Complexity:** Consider a scenario where Bob redeems stablecoin and spends a collateral UTXO on a fork, but that block is lost in a reorg. In the main chain, that collateral UTXO is still unspent. Bob might attempt again. Standard wallet handling of reorgs covers such scenarios as transactions revert and re-enter the mempool.

We must ensure that the node software handles reorgs gracefully with respect to tracking stablecoin state. If nodes keep an internal map of collateral to stable supply, they must rollback those changes on reorg as part of usual block disconnect/undo processes.

## Double-Spending and Script Validity:
DigiDollar introduces new script paths and output types. We need to ensure no unintended spending of stablecoin outputs:

- A stablecoin UTXO should not be spendable as a normal DGB output. For example, if OP_DIGIDOLLAR outputs were treated as anyone-can-spend by old nodes, that’s dangerous. That’s why it must be a soft-fork rule: old nodes would reject transactions with unknown opcodes.
- Once stablecoin is burned, it’s gone. We must ensure an attacker can’t replay a burn in a different context to free collateral twice. Standard double-spend prevention (each output can only be spent once) covers this.
- Attacks where someone floods many tiny mint and burn cycles to spam: since each costs fees and on-chain operations, they are limited by fees and block capacity.
- Ensure that over-collateralization rules are enforced at all times so no user can deliberately mint more than allowed or avoid redemption.

### Prevention of DigiDollar Spam or Dust Attacks:
To explicitly address spam:

- Enforce a minimum issuance size so that each collateralized position is of a meaningful size.
- Use fees to discourage spam; each transaction costs DGB fees.
- Adjust relay policy to mark extremely small stablecoin outputs as non-standard if necessary.

## Peg Stability Under Extreme Conditions:
If DGB’s price plummets quickly, collateral might become insufficient for some issued stablecoins. In such an event, undercollateralization might occur. Ideally, arbitrageurs will exploit the discount, or the system may trigger a liquidation process. For DigiDollar v1, we rely on high collateralization ratios and timely redemptions to mitigate this risk.

## Other Vectors:
- **Contract Complexity and Bugs:** Introducing new opcodes and scripts has the risk of implementation bugs. A careful code review and independent audits of the stablecoin-related code are essential.
- **Replay of Oracle Signatures:** Ensure that oracle signatures cannot be reused maliciously by including timestamps or block heights in the signed messages.
- **Sybil Transactions:** Rigorously define the script pattern for DigiDollar so that only correctly formed transactions are recognized by the wallet and nodes.
- **Privacy Considerations:** Stablecoin transactions might be more transparent due to fixed denominations, so privacy improvements should be considered in future upgrades.
- **Denial of Service on Oracle System:** An attacker might attempt to disrupt oracle services via DDoS. However, since the network only accepts valid, signed data, this would only delay price updates and temporarily affect stablecoin functionality, not compromise funds.

#### Conclusion of Security Section:
We discourage minimal collateral and enforce robust multi-oracle designs, rate limits, and transparency to secure DigiDollar. While no system is entirely without risk, the layered approach described here minimizes the possibility of exploitation via price manipulation, reorg-induced inconsistencies, double-spending, or spam attacks. Ongoing monitoring, third-party audits, and the potential for emergency governance interventions further reinforce the system's integrity.

# 11. Future Upgrades & Enhancements
The initial implementation of DigiDollar provides a fully functional stablecoin on DigiByte, but there are several areas of improvement and modernization that can be pursued in future upgrades. These enhancements aim to increase efficiency, security, and functionality, often by leveraging new features in Bitcoin/DigiByte’s evolution such as Taproot and Schnorr, or refining consensus to better accommodate the stablecoin.

## Integrating Taproot for Simplified and Efficient Locking Scripts:
Taproot (and the associated upgrades like SegWit v1 and Schnorr signatures) was slated for activation in DigiByte. By embracing Taproot, we can significantly improve the DigiDollar script design:

- **Single Output, Multiple Conditions:** Taproot allows multiple spending conditions to be hidden behind a single output public key (the Taproot tweak). For DigiDollar collateral outputs, we can have at least two conditions: (1) Redemption path – spend is allowed if corresponding stablecoins are provided (and price conditions met), and (2) Timeout path – spend is allowed by original owner after expiry (in case of emergency or contract expiration). With Taproot, these can be two branches of a Taproot script tree. In the UTXO, only a single tweaked public key is stored. This means that on-chain the collateral output looks like a simple pay-to-pubkey, revealing no details. Only when one of the conditions is executed, the spending transaction reveals that branch of the script.
- **Efficiency and Privacy:** This improves privacy (observers cannot immediately tell which outputs are DigiDollar collateral vs normal outputs, until spent). It also saves space – instead of storing a long script in each UTXO, we store one key (32 bytes). The script is revealed only in spending, and even then only the branch taken is revealed.
- **Schnorr Signatures and MAST:** With Schnorr, we could also aggregate signatures in some cases or use the key-path spend in Taproot to allow spending with just a signature if certain conditions are met. This could simplify the redemption process and reduce transaction sizes.

## Using Schnorr Signatures for More Efficient Oracle Price Verification:
- **Batch Verification:** If multiple oracle signatures (which would be Schnorr signatures if we upgrade oracles to use Schnorr keys) are included in a block, nodes can batch-verify them faster than with traditional signatures.
- **Threshold Signatures:** Schnorr enables the possibility of threshold signatures – multiple oracles could produce a single aggregated signature on the median price. This single signature in the block header is smaller than including several individual signatures, reducing block space usage and simplifying validation.

## Potential Improvements in Consensus Model for Stablecoin Efficiency:
Beyond Taproot and Schnorr, we consider other consensus improvements tailored to DigiDollar:

- **Covenants (Output Commitments):** Future Bitcoin proposals like OP_CHECKTEMPLATEVERIFY (CTV) or OP_CHECKOUTPUTSHASHVERIFY can enforce that when a collateral output is spent, any residual collateral is re-locked with updated parameters. This would enhance security in partial redemptions.
- **Dedicated Asset Support:** If DigiByte were to support native multi-assets in the future, DigiDollar could be implemented as a first-class asset, simplifying tracking and transfer.
- **Layer-2 Solutions:** Off-chain or Layer-2 protocols could complement DigiDollar by enabling fast, low-fee transactions using the stablecoin, possibly via channels analogous to the Lightning Network.
- **Automatic Liquidation Triggers:** In the future, consensus rules might allow for automatic liquidation of undercollateralized positions to protect the peg. This would require a mechanism for fair, decentralized auctions or a first-come-first-served redemption process.
- **Governance Hooks:** Future iterations might include on-chain governance mechanisms to adjust parameters (like collateral ratios or oracle lists) dynamically as market conditions change.

### Taproot & Schnorr Activation Timeline:
Once Taproot and Schnorr are activated and stable, the improvements outlined above can be gradually integrated into DigiDollar via soft forks or backward-compatible upgrades. This forward-looking approach ensures that DigiDollar remains efficient, secure, and adaptable as the underlying technology evolves.

# 12. Conclusion
DigiDollar represents a significant innovation for the DigiByte ecosystem: a stable, USD-pegged asset achieved through on-chain mechanisms and without centralized backing. By leveraging DigiByte’s UTXO heritage and recent advancements, we have outlined a design that integrates a decentralized stablecoin into the core protocol in a secure and efficient manner.

In this whitepaper, we presented the full implementation details of DigiDollar:

- In the Abstract and Introduction, we clarified DigiDollar’s purpose and motivations, highlighting how it provides a stable medium of exchange and store of value within DigiByte, complementing DGB’s volatility with a pegged asset.
- The System Architecture detailed how existing blockchain components (UTXO model, time-locks, oracles, wallet) are orchestrated to enable DigiDollar. Collateralized, time-locked DGB serves as the trust anchor for every stablecoin in circulation, while decentralized oracles inject real-time market data to maintain the 1:1 USD parity.
- We specified necessary Consensus and Protocol Changes, such as the introduction of stablecoin-specific opcodes (e.g., OP_DIGIDOLLAR) and new transaction validation rules to prevent misuse. We contrasted using custom opcodes versus an OP_RETURN overlay and chose an integrated opcode approach for robustness.
- A Decentralized Oracle System was described that aggregates price feeds from multiple exchanges, ensuring reliable USD valuation. We discussed how oracle signatures are embedded in block data and validated to thwart manipulation, and how fallback mechanisms handle missing data without halting the chain.
- Under Implementation Details, we walked through the user-level operations of locking DGB, minting DigiDollar, transferring it, and redeeming it. Example transaction structures and scripts illustrated the locking script logic (using CLTV for time-locks and new script checks for stablecoin verification) and the flow of mint and burn transactions.
- We highlighted GUI Enhancements in the DigiByte Core Wallet that will make DigiDollar accessible: a new dashboard showing balances and collateral, easy one-click minting and redeeming, transaction labeling, and warnings to educate users.
- We introduced new RPC calls like getdigidollarstats, getoracleprice, and getdigidollartransactions to query the status of the stablecoin and facilitate monitoring by users and services.
- The Transactions & Fee Structure section reasoned about how DigiDollar transactions remain free of additional fees beyond the normal DGB transaction fee, ensuring usability. We also discussed the network’s approach to preventing spam via fee requirements and potential minimum output rules.
- In Security Considerations, we examined the main risks—oracle attacks, reorgs, undercollateralization, spam—and described mitigation strategies for each, from robust oracle design and multi-signature validation to collateralization safeguards and spam disincentives. We stressed that while no system is without risk, DigiDollar’s design minimizes single points of failure and aligns incentives to uphold the peg.
- Finally, we looked ahead in Future Upgrades & Enhancements to how DigiDollar can evolve. By adopting Taproot and Schnorr, we can simplify scripts and improve efficiency. We discussed potential new opcodes for even greater security (like covenants for collateral outputs) and the possibility of broadening collateral types or adding governance in future. This forward-looking view ensures that DigiDollar can adapt to technological progress and growing user needs, maintaining its robustness and utility.

## Improvements to the DigiByte Ecosystem:
DigiDollar’s introduction is poised to have several positive impacts:

- It unlocks the potential for DeFi on DigiByte – with a stablecoin, one can build lending platforms, decentralized exchanges trading DGB vs DigiDollar, payment solutions for merchants wanting USD value, etc., all on the DigiByte chain.
- It provides stability for users who want to hedge against DGB’s volatility without leaving the network. During market downturns, users can convert DGB to DigiDollar to preserve value, and convert back when desired, all trustlessly.
- It increases DigiByte’s utility: rather than just a speculative asset, DGB becomes the collateral backing a stable currency. This could increase demand for DGB (to use as collateral) and reduce circulating supply (locked coins).
- The process of implementing DigiDollar also strengthens DigiByte’s infrastructure: by necessity we improve oracle mechanisms, add script capabilities, and move towards Taproot activation, which benefits the whole network beyond just stablecoin usage.

## Next Steps (Implementation, Testing, Governance):
Implementing DigiDollar requires a coordinated effort:

- **Development and Code Integration:** The DigiByte Core developers will implement the new opcodes, transaction rules, and wallet/RPC changes described. This involves modifications at the consensus layer and at the wallet layer.
- **Testing:** A feature as critical as a stablecoin must be thoroughly tested. We will utilize unit tests for script evaluation, functional tests for end-to-end flows, and testnet deployments. Economic edge cases (like rapid price changes) will be simulated.
- **Security Audit:** Engaging independent auditors to review the DigiDollar-related code and oracle system will add assurance. They will examine the cryptographic aspects and game-theoretic exploits.
- **Activation Plan:** Because DigiDollar introduces consensus changes, it will be deployed via a planned upgrade mechanism (soft fork) so that miners signal readiness and then enforce the new rules. Community consultation on parameters is essential before finalizing the deployment.
- **Community Governance and Oracle Selection:** The community must establish who will run oracles initially and how to rotate them if needed. Transparent criteria for oracles will help build trust. Emergency procedures should also be discussed.
- **Education and Documentation:** Clear documentation should be disseminated to educate users on how DigiDollar works, its risks, and proper usage to ensure smooth adoption.

By proceeding with careful development, testing, and community involvement, DigiDollar can be launched as a reliable component of the DigiByte blockchain. Its success will rely on both the soundness of the technical implementation and the support of the DigiByte community in utilizing and promoting it.

In conclusion, DigiDollar has the potential to significantly improve the DigiByte ecosystem by marrying DigiByte’s technical strengths with the stability of the US dollar. It demonstrates DigiByte’s flexibility and commitment to innovation, positioning DigiByte not just as a fast payments network but also as a platform for decentralized finance. With DigiDollar, DigiByte users gain a powerful new tool – the ability to seamlessly move between a volatile asset (for growth) and a stable asset (for safety) all within the same decentralized network. This enhances user autonomy and financial freedom, aligning with the broader vision of cryptocurrency as an empowering technology. The implementation details laid out in this whitepaper serve as a blueprint to realize this vision. As we move from design to deployment, rigorous validation and community governance will be key in ensuring DigiDollar’s success as the world’s first fully decentralized stablecoin on DigiByte. Together, these efforts will pave the way for a more versatile and resilient DigiByte blockchain for years to come.
