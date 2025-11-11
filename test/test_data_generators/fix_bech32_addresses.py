#!/usr/bin/env python3
"""
Update Bitcoin bech32 addresses to DigiByte bech32 addresses in test data
"""

import json
import re

# Bech32 prefix mappings
BECH32_REPLACEMENTS = {
    '"bc1': '"dgb1',      # Bitcoin mainnet -> DigiByte mainnet
    '"tb1': '"dgbt1',     # Bitcoin testnet -> DigiByte testnet  
    '"bcrt1': '"dgbrt1',  # Bitcoin regtest -> DigiByte regtest
}

def update_bech32_addresses(input_file, output_file):
    """Update bech32 addresses to use DigiByte prefixes"""
    with open(input_file, 'r') as f:
        content = f.read()
    
    # Count replacements
    replacement_count = 0
    
    # Replace each prefix
    for old_prefix, new_prefix in BECH32_REPLACEMENTS.items():
        matches = len(re.findall(re.escape(old_prefix), content))
        if matches > 0:
            content = content.replace(old_prefix, new_prefix)
            replacement_count += matches
            print(f"Replaced {matches} occurrences of {old_prefix} with {new_prefix}")
    
    # Write the updated content
    with open(output_file, 'w') as f:
        f.write(content)
    
    print(f"\nTotal replacements: {replacement_count}")

if __name__ == "__main__":
    input_file = "src/test/data/key_io_valid.json"
    output_file = "src/test/data/key_io_valid.json"
    
    update_bech32_addresses(input_file, output_file)
    print("Bech32 address update complete!")