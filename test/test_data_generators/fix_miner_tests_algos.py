#!/usr/bin/env python3
"""
Fix miner_tests.cpp to test all 5 DigiByte algorithms instead of just ALGO_SCRYPT
"""

import re
import sys

def fix_miner_tests(content):
    """Update miner tests to support all DigiByte algorithms"""
    
    # First, let's modify TestPackageSelection to accept an algo parameter
    content = re.sub(
        r'void MinerTestingSetup::TestPackageSelection\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst\)',
        'void MinerTestingSetup::TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int algo)',
        content
    )
    
    # Update the function declaration in the struct
    content = re.sub(
        r'void TestPackageSelection\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst\) EXCLUSIVE_LOCKS_REQUIRED',
        'void TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int algo) EXCLUSIVE_LOCKS_REQUIRED',
        content
    )
    
    # Replace all ALGO_SCRYPT with algo parameter in TestPackageSelection
    # Find the TestPackageSelection function and replace within it
    lines = content.split('\n')
    in_test_package = False
    modified_lines = []
    
    for i, line in enumerate(lines):
        if 'void MinerTestingSetup::TestPackageSelection' in line:
            in_test_package = True
        elif in_test_package and line.strip() == '}':
            # End of TestPackageSelection function
            in_test_package = False
            line = re.sub(r'ALGO_SCRYPT', 'algo', line)
        elif in_test_package:
            line = re.sub(r'ALGO_SCRYPT', 'algo', line)
        
        modified_lines.append(line)
    
    content = '\n'.join(modified_lines)
    
    # Similarly for TestBasicMining
    content = re.sub(
        r'void MinerTestingSetup::TestBasicMining\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight\)',
        'void MinerTestingSetup::TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight, int algo)',
        content
    )
    
    content = re.sub(
        r'void TestBasicMining\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight\) EXCLUSIVE_LOCKS_REQUIRED',
        'void TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight, int algo) EXCLUSIVE_LOCKS_REQUIRED',
        content
    )
    
    # Replace ALGO_SCRYPT in TestBasicMining
    lines = content.split('\n')
    in_test_basic = False
    modified_lines = []
    
    for i, line in enumerate(lines):
        if 'void MinerTestingSetup::TestBasicMining' in line:
            in_test_basic = True
        elif in_test_basic and line.strip().startswith('void MinerTestingSetup::TestPrioritisedMining'):
            # Start of next function
            in_test_basic = False
        elif in_test_basic:
            line = re.sub(r'ALGO_SCRYPT', 'algo', line)
        
        modified_lines.append(line)
    
    content = '\n'.join(modified_lines)
    
    # Similarly for TestPrioritisedMining
    content = re.sub(
        r'void MinerTestingSetup::TestPrioritisedMining\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst\)',
        'void MinerTestingSetup::TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int algo)',
        content
    )
    
    content = re.sub(
        r'void TestPrioritisedMining\(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst\) EXCLUSIVE_LOCKS_REQUIRED',
        'void TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int algo) EXCLUSIVE_LOCKS_REQUIRED',
        content
    )
    
    # Replace ALGO_SCRYPT in TestPrioritisedMining
    lines = content.split('\n')
    in_test_prioritised = False
    modified_lines = []
    
    for i, line in enumerate(lines):
        if 'void MinerTestingSetup::TestPrioritisedMining' in line:
            in_test_prioritised = True
        elif in_test_prioritised and (line.strip() == '}' and i > 0 and lines[i-1].strip() == ''):
            # End of TestPrioritisedMining function
            in_test_prioritised = False
            line = re.sub(r'ALGO_SCRYPT', 'algo', line)
        elif in_test_prioritised:
            line = re.sub(r'ALGO_SCRYPT', 'algo', line)
        
        modified_lines.append(line)
    
    content = '\n'.join(modified_lines)
    
    return content

def add_multi_algo_tests(content):
    """Add test cases that test all algorithms"""
    
    # Find the CreateNewBlock_validity test and modify it to test all algos
    test_insert_point = content.find('BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)')
    
    if test_insert_point != -1:
        # Find where the test calls TestPackageSelection
        package_call = content.find('TestPackageSelection(scriptPubKey, txFirst)', test_insert_point)
        if package_call != -1:
            # Replace with loop testing all algorithms
            old_call = 'TestPackageSelection(scriptPubKey, txFirst);'
            new_call = '''// Test all DigiByte algorithms
    const int algos[] = {ALGO_SHA256D, ALGO_SCRYPT, ALGO_GROESTL, ALGO_SKEIN, ALGO_QUBIT};
    for (int algo : algos) {
        TestPackageSelection(scriptPubKey, txFirst, algo);
    }'''
            content = content.replace(old_call, new_call)
            
        # Similarly for TestBasicMining
        basic_call = content.find('TestBasicMining(scriptPubKey, txFirst, baseheight)', test_insert_point)
        if basic_call != -1:
            old_call_match = re.search(r'TestBasicMining\(scriptPubKey, txFirst, baseheight\);', content[basic_call:])
            if old_call_match:
                old_call = old_call_match.group(0)
                new_call = '''// Test all DigiByte algorithms
    for (int algo : algos) {
        TestBasicMining(scriptPubKey, txFirst, baseheight, algo);
    }'''
                content = content[:basic_call] + content[basic_call:].replace(old_call, new_call, 1)
                
        # Similarly for TestPrioritisedMining
        prioritised_call = content.find('TestPrioritisedMining(scriptPubKey, txFirst)', test_insert_point)
        if prioritised_call != -1:
            old_call = 'TestPrioritisedMining(scriptPubKey, txFirst);'
            new_call = '''// Test all DigiByte algorithms
    for (int algo : algos) {
        TestPrioritisedMining(scriptPubKey, txFirst, algo);
    }'''
            content = content.replace(old_call, new_call)
    
    return content

def main():
    # Read the file
    with open('test/miner_tests.cpp', 'r') as f:
        content = f.read()
    
    # Fix the functions to accept algo parameter
    content = fix_miner_tests(content)
    
    # Add multi-algorithm testing
    content = add_multi_algo_tests(content)
    
    # Write the updated content
    with open('test/miner_tests.cpp', 'w') as f:
        f.write(content)
    
    print("Updated miner_tests.cpp to test all DigiByte algorithms")

if __name__ == '__main__':
    main()