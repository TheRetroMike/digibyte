# DigiDollar Config Notes

## txindex Requirement

DigiDollar validation may need transaction lookups during reindex/IBD.
For DigiDollar-enabled chains, run with:

```ini
txindex=1
```

On startup, DigiByte Core will reject DigiDollar-enabled configurations that do not enable txindex.

## Put Settings Under The Correct Network Section

`digibyte.conf` is section-aware. A setting in the wrong section will not apply to the network you are running.

- Mainnet settings go under `[main]`
- Testnet settings go under `[test]`
- Signet settings go under `[signet]`
- Regtest settings go under `[regtest]`

Example:

```ini
[main]
txindex=1

[test]
txindex=1
```

If you are running regtest DigiDollar scenarios explicitly (for example with `-digidollar=1` or `-digidollaractivationheight`), set:

```ini
[regtest]
txindex=1
```
