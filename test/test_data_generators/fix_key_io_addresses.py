#!/usr/bin/env python3
"""
Convert Bitcoin addresses in key_io_valid.json to DigiByte addresses
while preserving the script hex values.
"""

import json
import hashlib
import base58

# Address version bytes
BITCOIN_P2PKH_MAINNET = 0x00  # '1' addresses
BITCOIN_P2SH_MAINNET = 0x05   # '3' addresses
DIGIBYTE_P2PKH_MAINNET = 0x1E # 30 decimal, 'D' addresses
DIGIBYTE_P2SH_MAINNET = 0x3F  # 63 decimal, 'S' addresses

BITCOIN_P2PKH_TESTNET = 0x6F  # 'm' or 'n' addresses  
BITCOIN_P2SH_TESTNET = 0xC4   # '2' addresses
DIGIBYTE_P2PKH_TESTNET = 0x7E # 126 decimal, 's' addresses
DIGIBYTE_P2SH_TESTNET = 0x8C  # 140 decimal, 'y' addresses

def decode_base58_check(address):
    """Decode a Base58Check encoded address"""
    try:
        decoded = base58.b58decode(address)
        # Extract version byte(s) and payload
        if len(decoded) < 25:
            return None, None
        
        # Check if it's a valid checksum
        checksum = decoded[-4:]
        data = decoded[:-4]
        hash_result = hashlib.sha256(hashlib.sha256(data).digest()).digest()
        if hash_result[:4] != checksum:
            return None, None
            
        version = decoded[0]
        payload = decoded[1:-4]
        return version, payload
    except:
        return None, None

def encode_base58_check(version, payload):
    """Encode data with Base58Check"""
    data = bytes([version]) + payload
    checksum = hashlib.sha256(hashlib.sha256(data).digest()).digest()[:4]
    return base58.b58encode(data + checksum).decode('ascii')

def convert_address(address, chain):
    """Convert Bitcoin address to DigiByte address"""
    version, payload = decode_base58_check(address)
    if version is None:
        return address  # Return unchanged if not a valid address
    
    # Determine new version based on address type and chain
    if chain == "main":
        if version == BITCOIN_P2PKH_MAINNET:
            new_version = DIGIBYTE_P2PKH_MAINNET
        elif version == BITCOIN_P2SH_MAINNET:
            new_version = DIGIBYTE_P2SH_MAINNET
        else:
            return address  # Already a DigiByte address or other
    elif chain == "test":
        if version == BITCOIN_P2PKH_TESTNET:
            new_version = DIGIBYTE_P2PKH_TESTNET
        elif version == BITCOIN_P2SH_TESTNET:
            new_version = DIGIBYTE_P2SH_TESTNET
        else:
            return address  # Already a DigiByte address or other
    else:
        return address  # Other chains (regtest, signet) - keep as is for now
    
    # Re-encode with DigiByte version
    return encode_base58_check(new_version, payload)

def process_json_file(input_file, output_file):
    """Process the key_io_valid.json file"""
    with open(input_file, 'r') as f:
        data = json.load(f)
    
    converted_count = 0
    
    for entry in data:
        if len(entry) >= 3 and isinstance(entry[2], dict):
            address = entry[0]
            metadata = entry[2]
            
            # Skip private keys
            if metadata.get('isPrivkey', False):
                continue
                
            chain = metadata.get('chain', 'main')
            
            # Check if it's a Bitcoin address that needs conversion
            if isinstance(address, str) and len(address) > 0:
                first_char = address[0]
                if chain == "main" and first_char in ['1', '3']:
                    new_address = convert_address(address, chain)
                    if new_address != address:
                        print(f"Converting {address} -> {new_address}")
                        entry[0] = new_address
                        converted_count += 1
                elif chain == "test" and first_char in ['m', 'n', '2']:
                    new_address = convert_address(address, chain)
                    if new_address != address:
                        print(f"Converting {address} -> {new_address}")
                        entry[0] = new_address
                        converted_count += 1
    
    # Write the updated data
    with open(output_file, 'w') as f:
        json.dump(data, f, indent=4)
    
    print(f"\nTotal addresses converted: {converted_count}")

if __name__ == "__main__":
    # First install base58 if needed
    import subprocess
    import sys
    
    try:
        import base58
    except ImportError:
        print("Installing base58 module...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "base58"])
        import base58
    
    input_file = "src/test/data/key_io_valid.json"
    output_file = "src/test/data/key_io_valid.json"
    
    process_json_file(input_file, output_file)
    print("Conversion complete!")