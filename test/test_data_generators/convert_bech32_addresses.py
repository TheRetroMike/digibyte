#!/usr/bin/env python3
"""
Convert Bitcoin bech32 addresses to DigiByte bech32 addresses
This properly recalculates the checksum for the new HRP
"""

import json

# Bech32 character set
CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"

def bech32_polymod(values):
    """Internal function for bech32 checksum"""
    GEN = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
    chk = 1
    for v in values:
        b = chk >> 25
        chk = (chk & 0x1ffffff) << 5 ^ v
        for i in range(5):
            chk ^= GEN[i] if ((b >> i) & 1) else 0
    return chk

def bech32_hrp_expand(hrp):
    """Expand the HRP into values for checksum computation."""
    return [ord(x) >> 5 for x in hrp] + [0] + [ord(x) & 31 for x in hrp]

def bech32_verify_checksum(hrp, data, spec):
    """Verify a checksum given HRP and converted data characters."""
    return bech32_polymod(bech32_hrp_expand(hrp) + data) == spec

def bech32_create_checksum(hrp, data, spec):
    """Compute the checksum values given HRP and data."""
    values = bech32_hrp_expand(hrp) + data
    polymod = bech32_polymod(values + [0, 0, 0, 0, 0, 0]) ^ spec
    return [(polymod >> 5 * (5 - i)) & 31 for i in range(6)]

def bech32_encode(hrp, data, spec):
    """Compute a bech32 string given HRP and data values."""
    combined = data + bech32_create_checksum(hrp, data, spec)
    return hrp + '1' + ''.join([CHARSET[d] for d in combined])

def bech32_decode(bech):
    """Validate a bech32 string, and determine HRP and data."""
    if ((any(ord(x) < 33 or ord(x) > 126 for x in bech)) or
            (bech.lower() != bech and bech.upper() != bech)):
        return (None, None, None)
    bech = bech.lower()
    pos = bech.rfind('1')
    if pos < 1 or pos + 7 > len(bech) or len(bech) > 90:
        return (None, None, None)
    if not all(x in CHARSET for x in bech[pos+1:]):
        return (None, None, None)
    hrp = bech[:pos]
    data = [CHARSET.find(x) for x in bech[pos+1:]]
    spec = 1 if hrp in ['bc', 'tb', 'bcrt', 'dgb', 'dgbt', 'dgbrt'] else 0x2bc830a3
    if not bech32_verify_checksum(hrp, data, spec):
        return (None, None, None)
    return (hrp, data[:-6], spec)

def convert_bech32_address(address, new_hrp):
    """Convert a bech32 address to use a different HRP"""
    hrp, data, spec = bech32_decode(address)
    if hrp is None:
        return None
    return bech32_encode(new_hrp, data, spec)

# HRP mappings
HRP_MAP = {
    'bc': 'dgb',      # Bitcoin mainnet -> DigiByte mainnet
    'tb': 'dgbt',     # Bitcoin testnet -> DigiByte testnet  
    'bcrt': 'dgbrt',  # Bitcoin regtest -> DigiByte regtest
}

def update_json_file(filename):
    """Update bech32 addresses in a JSON file"""
    with open(filename, 'r') as f:
        data = json.load(f)
    
    def process_value(value):
        """Recursively process JSON values"""
        if isinstance(value, str):
            # Check if it's a bech32 address
            for old_hrp, new_hrp in HRP_MAP.items():
                if value.startswith(old_hrp + '1'):
                    converted = convert_bech32_address(value, new_hrp)
                    if converted:
                        print(f"Converting: {value} -> {converted}")
                        return converted
            return value
        elif isinstance(value, dict):
            return {k: process_value(v) for k, v in value.items()}
        elif isinstance(value, list):
            return [process_value(item) for item in value]
        else:
            return value
    
    # Process the entire data structure
    updated_data = process_value(data)
    
    # Write back
    with open(filename, 'w') as f:
        json.dump(updated_data, f, indent=4)
    
    print(f"Updated {filename}")

if __name__ == "__main__":
    # Update test vector files
    files = [
        "src/test/data/bip341_wallet_vectors.json",
        "src/test/data/key_io_valid.json"
    ]
    
    for filename in files:
        update_json_file(filename)