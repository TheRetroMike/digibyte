#!/bin/bash
# Comprehensive DigiByte to DigiByte naming conversion script for v26.2 (macOS version)
# IMPORTANT: This script ensures ALL DigiByte references are converted to DigiByte
# to prevent merge conflicts. Only DigiByte copyright notices are preserved.

set -e  # Exit on error

echo "Starting comprehensive DigiByte v26.2 to DigiByte naming conversion..."
echo "This will rename ALL DigiByte references to DigiByte throughout the codebase."
echo ""

# Step 1: File and Directory Renaming (must be done first)
echo "Step 1: Renaming all files and directories..."

# First rename directories (deepest first to avoid issues)
find . -type d -name "*digibyte*" -o -type d -name "*DigiByte*" | grep -v ".git" | sort -r | while read dir; do
    newdir=$(echo "$dir" | sed -e 's/digibyte/digibyte/g' -e 's/DigiByte/DigiByte/g')
    if [ "$dir" != "$newdir" ] && [ -e "$dir" ]; then
        echo "  Renaming directory: $dir -> $newdir"
        git mv "$dir" "$newdir" 2>/dev/null || mv "$dir" "$newdir"
    fi
done

find . -type d -name "*dgb*" -o -type d -name "*DGB*" | grep -v ".git" | sort -r | while read dir; do
    newdir=$(echo "$dir" | sed -e 's/dgb/dgb/g' -e 's/DGB/DGB/g')
    if [ "$dir" != "$newdir" ] && [ -e "$dir" ]; then
        echo "  Renaming directory: $dir -> $newdir"
        git mv "$dir" "$newdir" 2>/dev/null || mv "$dir" "$newdir"
    fi
done

# Then rename files
find . -name "*digibyte*" -o -name "*DigiByte*" | grep -v ".git" | while read file; do
    newfile=$(echo "$file" | sed -e 's/digibyte/digibyte/g' -e 's/DigiByte/DigiByte/g')
    if [ "$file" != "$newfile" ] && [ -e "$file" ]; then
        echo "  Renaming file: $file -> $newfile"
        git mv "$file" "$newfile" 2>/dev/null || mv "$file" "$newfile"
    fi
done

find . -name "*dgb*" -o -name "*DGB*" | grep -v ".git" | while read file; do
    newfile=$(echo "$file" | sed -e 's/dgb/dgb/g' -e 's/DGB/DGB/g')
    if [ "$file" != "$newfile" ] && [ -e "$file" ]; then
        echo "  Renaming file: $file -> $newfile"
        git mv "$file" "$newfile" 2>/dev/null || mv "$file" "$newfile"
    fi
done

# Step 2: Update ALL file contents
echo ""
echo "Step 2: Updating all file contents..."

# Process all text files (including build files, configs, docs, etc.)
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
    -name "CONTRIBUTING*" -o -name "*.1" -o -name "*.5" \
    \) | grep -v ".git/" | while read file; do
    
    echo "  Processing: $file"
    
    # Create temporary file for sed operations
    cp "$file" "$file.tmp"
    
    # Binary names (order matters - do specific before general)
    sed -i '' \
        -e 's/digibyted/digibyted/g' \
        -e 's/digibyte-cli/digibyte-cli/g' \
        -e 's/digibyte-tx/digibyte-tx/g' \
        -e 's/digibyte-wallet/digibyte-wallet/g' \
        -e 's/digibyte-qt/digibyte-qt/g' \
        -e 's/digibyte-util/digibyte-util/g' \
        -e 's/digibyte-chainstate/digibyte-chainstate/g' \
        -e 's/digibyte-node/digibyte-node/g' \
        "$file.tmp"
    
    # Library names
    sed -i '' \
        -e 's/libdigibyteconsensus/libdigibyteconsensus/g' \
        -e 's/libdigibyte_/libdigibyte_/g' \
        -e 's/libdigibyte/libdigibyte/g' \
        -e 's/LIBDIGIBYTECONSENSUS/LIBDIGIBYTECONSENSUS/g' \
        -e 's/LIBDIGIBYTE_/LIBDIGIBYTE_/g' \
        -e 's/LIBDIGIBYTE/LIBDIGIBYTE/g' \
        -e 's/digibyteconsensus/digibyteconsensus/g' \
        -e 's/DIGIBYTECONSENSUS/DIGIBYTECONSENSUS/g' \
        "$file.tmp"
    
    # Header guards and macros
    sed -i '' \
        -e 's/DIGIBYTE_/DIGIBYTE_/g' \
        -e 's/_DIGIBYTE_H/_DIGIBYTE_H/g' \
        -e 's/ENABLE_DIGIBYTE/ENABLE_DIGIBYTE/g' \
        -e 's/HAVE_DIGIBYTE/HAVE_DIGIBYTE/g' \
        -e 's/USE_DIGIBYTE/USE_DIGIBYTE/g' \
        "$file.tmp"
    
    # Class and namespace names
    sed -i '' \
        -e 's/DigiByteGUI/DigiByteGUI/g' \
        -e 's/DigiByteUnits/DigiByteUnits/g' \
        -e 's/DigiByteApplication/DigiByteApplication/g' \
        -e 's/DigiByteCore/DigiByteCore/g' \
        -e 's/DigiByteConsensus/DigiByteConsensus/g' \
        -e 's/DigiByte(/DigiByte(/g' \
        -e 's/::DigiByte/::DigiByte/g' \
        -e 's/namespace digibyte/namespace digibyte/g' \
        "$file.tmp"
    
    # Configuration and data files
    sed -i '' \
        -e 's/digibyte\.conf/digibyte.conf/g' \
        -e 's/digibyte\.pid/digibyte.pid/g' \
        -e 's/\.digibyte/\.digibyte/g' \
        -e 's/DIGIBYTE_CONF_FILENAME/DIGIBYTE_CONF_FILENAME/g' \
        -e 's/DIGIBYTE_PID_FILENAME/DIGIBYTE_PID_FILENAME/g' \
        "$file.tmp"
    
    # Network and protocol
    sed -i '' \
        -e 's/digibyte:/digibyte:/g' \
        -e 's/digibyte\.org/digibyte.org/g' \
        -e 's/digibyte\.it/digibyte.org/g' \
        -e 's/digibytetalk/digibytetalk/g' \
        -e 's/digibyte-dev/digibyte-dev/g' \
        "$file.tmp"
    
    # Currency codes (word boundaries to avoid partial matches)
    sed -i '' \
        -e 's/[[:<:]]DGB[[:>:]]/DGB/g' \
        -e 's/[[:<:]]dgb[[:>:]]/dgb/g' \
        -e 's/[[:<:]]DGB[[:>:]]/DGB/g' \
        -e 's/[[:<:]]dgb[[:>:]]/dgb/g' \
        -e 's/[[:<:]]mDGB[[:>:]]/mDGB/g' \
        -e 's/[[:<:]]mdgb[[:>:]]/mdgb/g' \
        -e 's/[[:<:]]uDGB[[:>:]]/uDGB/g' \
        -e 's/[[:<:]]udgb[[:>:]]/udgb/g' \
        -e 's/[[:<:]]sDGB[[:>:]]/sDGB/g' \
        -e 's/[[:<:]]sdgb[[:>:]]/sdgb/g' \
        "$file.tmp"
    
    # Package and project names
    sed -i '' \
        -e 's/org\.digibyte/org.digibyte/g' \
        -e 's/digibyte-core/digibyte-core/g' \
        -e 's/digibyte_core/digibyte_core/g' \
        -e 's/digibyte-project/digibyte-project/g' \
        -e 's/digibyte_project/digibyte_project/g' \
        "$file.tmp"
    
    # General replacements (do these last to avoid double-replacements)
    sed -i '' \
        -e 's/DigiByte Core/DigiByte Core/g' \
        -e 's/DigiByte network/DigiByte network/g' \
        -e 's/DigiByte protocol/DigiByte protocol/g' \
        -e 's/DigiByte address/DigiByte address/g' \
        -e 's/DigiByte transaction/DigiByte transaction/g' \
        -e 's/DigiByte blockchain/DigiByte blockchain/g' \
        -e 's/DigiByte wallet/DigiByte wallet/g' \
        -e 's/DigiByte node/DigiByte node/g' \
        -e 's/DigiByte mining/DigiByte mining/g' \
        -e 's/DigiByte developers/DigiByte developers/g' \
        -e 's/The DigiByte/The DigiByte/g' \
        -e 's/[[:<:]]DigiByte[[:>:]]/DigiByte/g' \
        -e 's/[[:<:]]digibyte[[:>:]]/digibyte/g' \
        -e 's/DIGIBYTE/DIGIBYTE/g' \
        "$file.tmp"
    
    # Move temporary file back
    mv "$file.tmp" "$file"
done

# Step 3: Update Copyright (ADD DigiByte copyright, KEEP DigiByte copyright)
echo ""
echo "Step 3: Adding DigiByte copyright while preserving DigiByte copyright..."
find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) | grep -v ".git/" | while read file; do
    # Check if file has DigiByte copyright but not DigiByte copyright
    if grep -q "Copyright.*The DigiByte Core developers" "$file" && ! grep -q "Copyright.*The DigiByte Core developers" "$file"; then
        echo "  Adding DigiByte copyright to: $file"
        # Add DigiByte copyright after DigiByte copyright
        sed -i '' '/Copyright.*The DigiByte Core developers/a\
// Copyright (c) 2014-2025 The DigiByte Core developers' "$file"
    fi
done

# Step 4: Restore DigiByte references where they should be preserved
echo ""
echo "Step 4: Restoring DigiByte references in copyright notices only..."
find . -type f | grep -v ".git/" | while read file; do
    # Restore "DigiByte Core developers" in copyright lines only
    sed -i '' '/Copyright.*The DigiByte Core developers/! s/The DigiByte Core developers/The DigiByte Core developers/g' "$file"
done

# Step 5: Handle special build and config files
echo ""
echo "Step 5: Updating build system files..."

# Update autoconf files
if [ -f "configure.ac" ]; then
    echo "  Updating configure.ac..."
    sed -i '' 's/AC_INIT(\[DigiByte Core\]/AC_INIT([DigiByte Core]/g' configure.ac
    sed -i '' 's/PACKAGE_NAME="DigiByte Core"/PACKAGE_NAME="DigiByte Core"/g' configure.ac
fi

# Update Qt project files
find . -name "*.pro" -o -name "*.pri" | while read file; do
    echo "  Updating Qt project file: $file"
    sed -i '' 's/TARGET = digibyte/TARGET = digibyte/g' "$file"
done

# Update pkg-config files
find . -name "*.pc.in" | while read file; do
    echo "  Updating pkg-config file: $file"
    sed -i '' 's/Name: DigiByte/Name: DigiByte/g' "$file"
done

# Step 6: Verify critical files were updated
echo ""
echo "Step 6: Verifying critical files..."

critical_files=(
    "configure.ac"
    "Makefile.am"
    "src/Makefile.am"
    "src/qt/Makefile.am"
    "src/init.cpp"
    "src/chainparams.cpp"
    "src/net.cpp"
    "src/rpc/server.cpp"
)

for file in "${critical_files[@]}"; do
    if [ -f "$file" ]; then
        if grep -q "digibyte" "$file" || grep -q "DigiByte" "$file" || grep -q "DGB" "$file"; then
            echo "  WARNING: $file still contains DigiByte references!"
            grep -n -E "(digibyte|DigiByte|DGB)" "$file" | head -5
        else
            echo "  ✓ $file appears clean"
        fi
    fi
done

# Step 7: Final report
echo ""
echo "Step 7: Generating conversion report..."

# Count remaining DigiByte references (excluding copyright lines)
echo ""
echo "Remaining DigiByte references (excluding copyright):"
grep -r -E "(digibyte|DigiByte|DGB)" . --exclude-dir=.git | grep -v "Copyright.*DigiByte Core developers" | wc -l

echo ""
echo "Conversion complete!"
echo ""
echo "IMPORTANT NOTES:"
echo "1. DigiByte copyright notices have been preserved alongside DigiByte copyrights"
echo "2. Please review any warnings above for files that may need manual attention"
echo "3. Run 'git status' to see all changes"
echo "4. This script should prevent ~30,000 merge conflicts when merging with DigiByte"
echo ""
echo "Next steps:"
echo "1. Review the changes: git diff"
echo "2. Commit the changes: git add -A && git commit -m 'Pre-convert DigiByte v26.2 to DigiByte naming'"
echo "3. Proceed with the merge into DigiByte repository"