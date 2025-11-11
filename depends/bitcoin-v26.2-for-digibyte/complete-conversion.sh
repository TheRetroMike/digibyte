#!/bin/bash
# Complete the Bitcoin to DigiByte content conversion
# This script focuses on file content updates after file/directory renaming

set -e

echo "Completing Bitcoin to DigiByte content conversion..."
echo ""

# Function to safely perform sed replacements on macOS
safe_sed() {
    local file="$1"
    shift

    # Create backup
    cp "$file" "$file.bak"

    # Apply all sed commands
    for expr in "$@"; do
        sed -i '' "$expr" "$file"
    done

    # Remove backup if successful
    rm "$file.bak"
}

# Find all text files and update contents
find . -type f \( \
    -name "*.cpp" -o -name "*.h" -o -name "*.c" -o \
    -name "*.mk" -o -name "*.am" -o -name "*.ac" -o \
    -name "*.py" -o -name "*.sh" -o -name "*.bash" -o \
    -name "*.md" -o -name "*.txt" -o -name "*.rc" -o \
    -name "*.xml" -o -name "*.json" -o -name "*.yml" -o -name "*.yaml" -o \
    -name "*.conf" -o -name "*.ini" -o -name "*.cfg" -o \
    -name "*.pro" -o -name "*.pri" -o -name "*.qrc" -o \
    -name "*.ts" -o -name "*.ui" -o -name "*.forms" -o \
    -name "*.cmake" -o -name "CMakeLists.txt" -o \
    -name "*.in" -o -name "*.include" -o \
    -name "*.vcxproj" -o -name "*.vcxproj.filters" -o \
    -name "*.sln" -o -name "*.props" -o \
    -name "Makefile" -o -name "makefile" -o \
    -name "*.plist" -o -name "*.strings" -o \
    -name "*.desktop" -o -name "*.service" -o \
    -name "*.policy" -o -name "*.rules" -o \
    -name "*.nsi" -o -name "*.wxs" -o \
    -name "*.spec" -o -name "*.control" -o \
    -name ".gitignore" -o -name ".gitattributes" -o \
    -name "README*" -o -name "LICENSE*" -o -name "COPYING*" -o \
    -name "AUTHORS*" -o -name "INSTALL*" -o -name "NEWS*" -o \
    -name "CONTRIBUTING*" -o -name "*.1" -o -name "*.5" -o \
    -name "*.java" \
\) | grep -v ".git/" | while read file; do

    echo "Processing: $file"

    safe_sed "$file" \
        's/digibyted/digibyted/g' \
        's/digibyte-cli/digibyte-cli/g' \
        's/digibyte-tx/digibyte-tx/g' \
        's/digibyte-wallet/digibyte-wallet/g' \
        's/digibyte-qt/digibyte-qt/g' \
        's/digibyte-util/digibyte-util/g' \
        's/digibyte-chainstate/digibyte-chainstate/g' \
        's/digibyte-node/digibyte-node/g' \
        's/libdigibyteconsensus/libdigibyteconsensus/g' \
        's/libdigibyte_/libdigibyte_/g' \
        's/libdigibyte/libdigibyte/g' \
        's/LIBDIGIBYTECONSENSUS/LIBDIGIBYTECONSENSUS/g' \
        's/LIBDIGIBYTE_/LIBDIGIBYTE_/g' \
        's/LIBDIGIBYTE/LIBDIGIBYTE/g' \
        's/digibyteconsensus/digibyteconsensus/g' \
        's/DIGIBYTECONSENSUS/DIGIBYTECONSENSUS/g' \
        's/DIGIBYTE_/DIGIBYTE_/g' \
        's/_DIGIBYTE_H/_DIGIBYTE_H/g' \
        's/DigiByteGUI/DigiByteGUI/g' \
        's/DigiByteUnits/DigiByteUnits/g' \
        's/DigiByteApplication/DigiByteApplication/g' \
        's/DigiByteCore/DigiByteCore/g' \
        's/DigiByteConsensus/DigiByteConsensus/g' \
        's/namespace digibyte/namespace digibyte/g' \
        's/digibyte\.conf/digibyte.conf/g' \
        's/digibyte\.pid/digibyte.pid/g' \
        's/\.digibyte/\.digibyte/g' \
        's/digibyte:/digibyte:/g' \
        's/digibyte\.org/digibyte.org/g' \
        's/org\.digibyte/org.digibyte/g' \
        's/digibyte-core/digibyte-core/g' \
        's/digibyte_core/digibyte_core/g' \
        's/DigiByte Core/DigiByte Core/g' \
        's/DigiByte network/DigiByte network/g' \
        's/DigiByte protocol/DigiByte protocol/g' \
        's/DigiByte address/DigiByte address/g' \
        's/DigiByte transaction/DigiByte transaction/g' \
        's/DigiByte blockchain/DigiByte blockchain/g' \
        's/DigiByte wallet/DigiByte wallet/g' \
        's/DigiByte node/DigiByte node/g' \
        's/DigiByte mining/DigiByte mining/g' \
        's/The DigiByte/The DigiByte/g' \
        's/DIGIBYTE/DIGIBYTE/g'
done

# Handle currency codes separately with word boundaries
echo ""
echo "Updating currency codes..."
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.md" \) | grep -v ".git/" | while read file; do
    echo "Currency update: $file"
    safe_sed "$file" \
        's/ DGB / DGB /g' \
        's/(DGB)/(DGB)/g' \
        's/"DGB"/"DGB"/g' \
        "s/'DGB'/'DGB'/g" \
        's/mDGB/mDGB/g' \
        's/uDGB/uDGB/g' \
        's/satoshi/koinu/g' \
        's/Satoshi/Koinu/g'
done

# Final pass for Bitcoin/digibyte words
echo ""
echo "Final pass for Bitcoin references..."
find . -type f -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.md" | grep -v ".git/" | while read file; do
    # Skip copyright lines
    if grep -q "Bitcoin" "$file" && ! grep -q "Copyright.*DigiByte Core developers" "$file"; then
        echo "Final update: $file"
        safe_sed "$file" \
            's/Bitcoin/DigiByte/g' \
            's/digibyte/digibyte/g'
    fi
done

# Add DigiByte copyright to files with Bitcoin copyright
echo ""
echo "Adding DigiByte copyrights..."
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) | grep -v ".git/" | while read file; do
    if grep -q "Copyright.*The DigiByte Core developers" "$file" && ! grep -q "Copyright.*The DigiByte Core developers" "$file"; then
        echo "Adding copyright to: $file"
        # Use a different approach for macOS sed
        awk '/Copyright.*The DigiByte Core developers/ && !done {print; print "// Copyright (c) 2014-2025 The DigiByte Core developers"; done=1; next} {print}' "$file" > "$file.tmp"
        mv "$file.tmp" "$file"
    fi
done

echo ""
echo "Checking results..."
echo "Remaining Bitcoin references (excluding copyright):"
grep -r "digibyte\|Bitcoin\|DGB" . --exclude-dir=.git | grep -v "Copyright.*DigiByte Core developers" | wc -l

echo ""
echo "Content conversion complete!"
