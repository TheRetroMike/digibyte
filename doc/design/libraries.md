# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libdigibyte_cli*         | RPC client functionality used by *digibyte-cli* executable |
| *libdigibyte_common*      | Home for common functionality shared by different executables and libraries. Similar to *libdigibyte_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libdigibyte_consensus*   | Stable, backwards-compatible consensus functionality used by *libdigibyte_node* and *libdigibyte_wallet* and also exposed as a [shared library](../shared-libraries.md). |
| *libdigibyteconsensus*    | Shared library build of static *libdigibyte_consensus* library |
| *libdigibyte_kernel*      | Consensus engine and support library used for validation by *libdigibyte_node* and also exposed as a [shared library](../shared-libraries.md). |
| *libdigibyteqt*           | GUI functionality used by *digibyte-qt* and *digibyte-gui* executables |
| *libdigibyte_ipc*         | IPC functionality used by *digibyte-node*, *digibyte-wallet*, *digibyte-gui* executables to communicate when [`--enable-multiprocess`](multiprocess.md) is used. |
| *libdigibyte_node*        | P2P and RPC server functionality used by *digibyted* and *digibyte-qt* executables. |
| *libdigibyte_util*        | Home for common functionality shared by different executables and libraries. Similar to *libdigibyte_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libdigibyte_wallet*      | Wallet functionality used by *digibyted* and *digibyte-wallet* executables. |
| *libdigibyte_wallet_tool* | Lower-level wallet functionality used by *digibyte-wallet* executable. |
| *libdigibyte_zmq*         | [ZeroMQ](../zmq.md) functionality used by *digibyted* and *digibyte-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. Exceptions are *libdigibyte_consensus* and *libdigibyte_kernel* which have external interfaces documented at [../shared-libraries.md](../shared-libraries.md).

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`libdigibyte_*_SOURCES`](../../src/Makefile.am) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libdigibyte_node* code lives in `src/node/` in the `node::` namespace
  - *libdigibyte_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libdigibyte_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libdigibyte_util* code lives in `src/util/` in the `util::` namespace
  - *libdigibyte_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

digibyte-cli[digibyte-cli]-->libdigibyte_cli;

digibyted[digibyted]-->libdigibyte_node;
digibyted[digibyted]-->libdigibyte_wallet;

digibyte-qt[digibyte-qt]-->libdigibyte_node;
digibyte-qt[digibyte-qt]-->libdigibyteqt;
digibyte-qt[digibyte-qt]-->libdigibyte_wallet;

digibyte-wallet[digibyte-wallet]-->libdigibyte_wallet;
digibyte-wallet[digibyte-wallet]-->libdigibyte_wallet_tool;

libdigibyte_cli-->libdigibyte_util;
libdigibyte_cli-->libdigibyte_common;

libdigibyte_common-->libdigibyte_consensus;
libdigibyte_common-->libdigibyte_util;

libdigibyte_kernel-->libdigibyte_consensus;
libdigibyte_kernel-->libdigibyte_util;

libdigibyte_node-->libdigibyte_consensus;
libdigibyte_node-->libdigibyte_kernel;
libdigibyte_node-->libdigibyte_common;
libdigibyte_node-->libdigibyte_util;

libdigibyteqt-->libdigibyte_common;
libdigibyteqt-->libdigibyte_util;

libdigibyte_wallet-->libdigibyte_common;
libdigibyte_wallet-->libdigibyte_util;

libdigibyte_wallet_tool-->libdigibyte_wallet;
libdigibyte_wallet_tool-->libdigibyte_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class digibyte-qt,digibyted,digibyte-cli,digibyte-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Consensus* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libdigibyte_wallet* and *libdigibyte_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libdigibyte_consensus* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libdigibyte_util* should also be a standalone dependency that any library can depend on, and it should not depend on other internal libraries.

- *libdigibyte_common* should serve a similar function as *libdigibyte_util* and be a place for miscellaneous code used by various daemon, GUI, and CLI applications and libraries to live. It should not depend on anything other than *libdigibyte_util* and *libdigibyte_consensus*. The boundary between _util_ and _common_ is a little fuzzy but historically _util_ has been used for more generic, lower-level things like parsing hex, and _common_ has been used for digibyte-specific, higher-level things like parsing base58. The difference between util and common is mostly important because *libdigibyte_kernel* is not supposed to depend on *libdigibyte_common*, only *libdigibyte_util*. In general, if it is ever unclear whether it is better to add code to *util* or *common*, it is probably better to add it to *common* unless it is very generically useful or useful particularly to include in the kernel.


- *libdigibyte_kernel* should only depend on *libdigibyte_util* and *libdigibyte_consensus*.

- The only thing that should depend on *libdigibyte_kernel* internally should be *libdigibyte_node*. GUI and wallet libraries *libdigibyteqt* and *libdigibyte_wallet* in particular should not depend on *libdigibyte_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be get able it from *libdigibyte_consensus*, *libdigibyte_common*, and *libdigibyte_util*, instead of *libdigibyte_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libdigibyteqt*, *libdigibyte_node*, *libdigibyte_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](`../../src/interfaces/`) abstract interfaces.

## Work in progress

- Validation code is moving from *libdigibyte_node* to *libdigibyte_kernel* as part of [The libdigibytekernel Project #24303](https://github.com/digibyte/digibyte/issues/24303)
- Source code organization is discussed in general in [Library source code organization #15732](https://github.com/digibyte/digibyte/issues/15732)
