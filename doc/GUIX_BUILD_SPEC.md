# DigiByte GUIX Build Specification for macOS Host

This document provides a complete specification for setting up a **persistent, reusable** GUIX build environment on macOS to build DigiByte Core binaries for all four target platforms without signing or attestation. The container is designed to be long-lived and support iterative development.

## Target Platforms

1. **macOS Intel** (x86_64-apple-darwin)
2. **macOS Apple Silicon** (arm64-apple-darwin)  
3. **Windows 64-bit** (x86_64-w64-mingw32)
4. **Linux 64-bit** (x86_64-linux-gnu)

## Prerequisites

- macOS host system (Intel or Apple Silicon)
- At least 40GB free disk space
- 16GB+ RAM recommended
- Xcode Command Line Tools installed
- Docker Desktop for Mac installed and running

## Persistent Container Architecture

The GUIX container is designed to:
- **Persist between builds** - No need to recreate for each build
- **Mount source code** - Edit on macOS, build in container
- **Cache everything** - Dependencies, sources, and build artifacts
- **Support incremental builds** - Only rebuild what changed

## Build Environment Setup

### Step 1: Install Docker Desktop

1. Download Docker Desktop from https://www.docker.com/products/docker-desktop/
2. Install and start Docker Desktop
3. Verify installation:
   ```bash
   docker --version
   docker run hello-world
   ```

### Step 2: Set Up GUIX in Docker

Since GUIX doesn't run natively on macOS, we'll use a Docker container:

```bash
# Create a dedicated directory for GUIX builds
mkdir -p ~/digibyte-guix-builds
cd ~/digibyte-guix-builds

# Create Dockerfile for GUIX environment
cat > Dockerfile << 'EOF'
FROM debian:bullseye-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    wget \
    gpg \
    xz-utils \
    git \
    make \
    g++ \
    autoconf \
    automake \
    libtool \
    pkg-config \
    bsdmainutils \
    python3 \
    curl \
    ca-certificates \
    sudo \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Create build user
RUN useradd -m -s /bin/bash guixbuilder && \
    echo "guixbuilder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Switch to build user
USER guixbuilder
WORKDIR /home/guixbuilder

# Install GUIX
RUN cd /tmp && \
    wget https://git.savannah.gnu.org/cgit/guix.git/plain/etc/guix-install.sh && \
    chmod +x guix-install.sh && \
    sudo GUIX_BINARY_FILE_NAME="guix-binary-1.4.0.x86_64-linux.tar.xz" ./guix-install.sh --yes

# Set up GUIX environment
RUN echo 'export PATH="/home/guixbuilder/.config/guix/current/bin:$PATH"' >> ~/.bashrc && \
    echo 'export GUIX_LOCPATH="$HOME/.guix-profile/lib/locale"' >> ~/.bashrc

# Create persistent directories inside container
RUN mkdir -p /home/guixbuilder/.cache/guix && \
    mkdir -p /home/guixbuilder/.config/guix && \
    mkdir -p /home/guixbuilder/work

VOLUME ["/digibyte", "/output", "/cache", "/home/guixbuilder/.cache", "/home/guixbuilder/.config"]

# Keep container running
CMD ["tail", "-f", "/dev/null"]
EOF

# Build Docker image
docker build -t digibyte-guix-builder .
```

### Step 3: Prepare Build Directories

```bash
# Create directory structure on host (outside git repo)
mkdir -p ~/digibyte-guix-builds/{output,cache,depends-cache,sources-cache}

# Use existing DigiByte repository (not a separate clone)
# The container will mount your working directory
cd ~/Code/digibyte
```

### Step 4: Obtain macOS SDK

The macOS SDK is required for building macOS binaries:

```bash
# Download Xcode 12.2 (requires Apple Developer account)
# SHA256: 28d352f8c14a43d9b8a082ac6338dc173cb153f964c6e8fb6ba389e5be528bd0
# From: https://developer.apple.com/download/all/?q=Xcode%2012.2

# After downloading Xcode_12.2.xip, extract it
cd ~/Downloads
xip -x Xcode_12.2.xip

# Generate SDK tarball
cd ~/Code/digibyte
./contrib/macdeploy/gen-sdk ~/Downloads/Xcode.app

# Expected output: Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers.tar.gz
# SHA256: df75d30ecafc429e905134333aeae56ac65fac67cb4182622398fd717df77619

# Store SDK outside git repository to avoid commits
mkdir -p ~/digibyte-guix-builds/SDKs
mv Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers.tar.gz ~/digibyte-guix-builds/SDKs/

# The SDK will be mounted into the container at the correct location
```

## Persistent Container Management Scripts

Create helper scripts to manage the persistent Docker container:

### Container Lifecycle Management Script

```bash
cat > ~/digibyte-guix-builds/guix-container.sh << 'EOF'
#!/bin/bash

# Configuration
CONTAINER_NAME="digibyte-guix-persistent"
IMAGE_NAME="digibyte-guix-builder"
DIGIBYTE_DIR="${DIGIBYTE_DIR:-$HOME/Code/digibyte}"
BUILD_ROOT="$HOME/digibyte-guix-builds"

# Persistent directories (all outside git repo)
OUTPUT_DIR="$BUILD_ROOT/output"
CACHE_DIR="$BUILD_ROOT/cache"
DEPENDS_CACHE="$BUILD_ROOT/depends-cache"
SOURCES_CACHE="$BUILD_ROOT/sources-cache"
GUIX_CONFIG="$BUILD_ROOT/guix-config"
GUIX_CACHE="$BUILD_ROOT/guix-cache"
SDK_DIR="$BUILD_ROOT/SDKs"
DEPENDS_WORK="$BUILD_ROOT/depends-work"

# Ensure directories exist
mkdir -p "$OUTPUT_DIR" "$CACHE_DIR" "$DEPENDS_CACHE" "$SOURCES_CACHE" "$GUIX_CONFIG" "$GUIX_CACHE" "$SDK_DIR" "$DEPENDS_WORK"

case "$1" in
    start)
        echo "Starting persistent GUIX container..."
        if docker ps -a | grep -q "$CONTAINER_NAME"; then
            echo "Container already exists, starting it..."
            docker start "$CONTAINER_NAME"
        else
            echo "Creating new persistent container..."
            docker run -d \
                --name "$CONTAINER_NAME" \
                -v "$DIGIBYTE_DIR:/digibyte:rw" \
                -v "$OUTPUT_DIR:/output:rw" \
                -v "$CACHE_DIR:/cache:rw" \
                -v "$DEPENDS_CACHE:/depends-cache:rw" \
                -v "$SOURCES_CACHE:/sources-cache:rw" \
                -v "$GUIX_CONFIG:/home/guixbuilder/.config:rw" \
                -v "$GUIX_CACHE:/home/guixbuilder/.cache:rw" \
                -v "$SDK_DIR:/sdk:ro" \
                -v "$DEPENDS_WORK:/digibyte/depends/work:rw" \
                -e HOSTS='x86_64-apple-darwin arm64-apple-darwin x86_64-w64-mingw32 x86_64-linux-gnu' \
                -e SOURCES_PATH=/sources-cache \
                -e BASE_CACHE=/depends-cache \
                -e SDK_PATH=/sdk \
                -e JOBS=4 \
                "$IMAGE_NAME"
            
            # Wait for container to start
            sleep 3
            
            # Initialize GUIX daemon
            docker exec -u root "$CONTAINER_NAME" bash -c "guix-daemon --build-users-group=guixbuild &"
        fi
        echo "Container '$CONTAINER_NAME' is running"
        ;;
        
    stop)
        echo "Stopping container..."
        docker stop "$CONTAINER_NAME"
        ;;
        
    shell)
        echo "Entering container shell..."
        docker exec -it "$CONTAINER_NAME" bash
        ;;
        
    build)
        echo "Starting build in container..."
        docker exec -it "$CONTAINER_NAME" bash -c "cd /digibyte && ./contrib/guix/guix-build"
        ;;
        
    clean-build)
        echo "Cleaning previous build artifacts..."
        docker exec -it "$CONTAINER_NAME" bash -c "cd /digibyte && ./contrib/guix/guix-clean"
        ;;
        
    status)
        if docker ps | grep -q "$CONTAINER_NAME"; then
            echo "Container '$CONTAINER_NAME' is running"
            docker exec "$CONTAINER_NAME" df -h /cache /depends-cache /sources-cache
        else
            echo "Container '$CONTAINER_NAME' is not running"
        fi
        ;;
        
    logs)
        docker logs -f "$CONTAINER_NAME"
        ;;
        
    remove)
        echo "Removing container (preserving caches)..."
        docker stop "$CONTAINER_NAME" 2>/dev/null || true
        docker rm "$CONTAINER_NAME"
        echo "Container removed. Caches preserved in $BUILD_ROOT"
        ;;
        
    *)
        echo "Usage: $0 {start|stop|shell|build|clean-build|status|logs|remove}"
        echo ""
        echo "Commands:"
        echo "  start       - Start or create the persistent container"
        echo "  stop        - Stop the running container"
        echo "  shell       - Open interactive shell in container"
        echo "  build       - Run GUIX build"
        echo "  clean-build - Clean build artifacts"
        echo "  status      - Show container status"
        echo "  logs        - Show container logs"
        echo "  remove      - Remove container (keeps caches)"
        exit 1
        ;;
esac
EOF

chmod +x ~/digibyte-guix-builds/guix-container.sh
```

### Quick Build Script for Development

```bash
cat > ~/digibyte-guix-builds/quick-build.sh << 'EOF'
#!/bin/bash

# Quick rebuild script for development
# Assumes container is already running with caches populated

CONTAINER_NAME="digibyte-guix-persistent"

# Check if container is running
if ! docker ps | grep -q "$CONTAINER_NAME"; then
    echo "Container not running. Starting..."
    ~/digibyte-guix-builds/guix-container.sh start
    sleep 5
fi

# Clean only the current build outputs (keep dependencies)
echo "Cleaning previous build outputs..."
docker exec "$CONTAINER_NAME" bash -c "cd /digibyte && rm -rf guix-build-*"

# Run build with progress
echo "Starting incremental build..."
docker exec -it "$CONTAINER_NAME" bash -c '
cd /digibyte
export V=1
export HOSTS="x86_64-apple-darwin arm64-apple-darwin x86_64-w64-mingw32 x86_64-linux-gnu"
export SOURCES_PATH=/sources-cache
export BASE_CACHE=/depends-cache
export SDK_PATH=/sdk
./contrib/guix/guix-build
'
EOF

chmod +x ~/digibyte-guix-builds/quick-build.sh
```

## Building DigiByte with Persistent Container

### Initial Setup (First Time Only)

```bash
# 1. Build the Docker image
cd ~/digibyte-guix-builds
docker build -t digibyte-guix-builder .

# 2. Start the persistent container
./guix-container.sh start

# 3. Initialize GUIX inside container (first time only)
./guix-container.sh shell

# Inside the container shell:
# Update GUIX
guix pull
guix --version

# Exit shell
exit
```

### Development Workflow

For ongoing development with code changes:

```bash
# 1. Make your code changes in macOS
cd ~/Code/digibyte
# Edit files, commit changes, etc.

# 2. Run incremental build
~/digibyte-guix-builds/quick-build.sh

# 3. Or manually control the build process
~/digibyte-guix-builds/guix-container.sh shell
# Inside container:
cd /digibyte
git status  # See your changes
./contrib/guix/guix-build
```

### Container Management Commands

```bash
# Start container (if stopped)
~/digibyte-guix-builds/guix-container.sh start

# Stop container (preserves state)
~/digibyte-guix-builds/guix-container.sh stop

# Check container status and disk usage
~/digibyte-guix-builds/guix-container.sh status

# Open interactive shell
~/digibyte-guix-builds/guix-container.sh shell

# Run full build
~/digibyte-guix-builds/guix-container.sh build

# Clean build artifacts (keeps caches)
~/digibyte-guix-builds/guix-container.sh clean-build

# View container logs
~/digibyte-guix-builds/guix-container.sh logs

# Remove container completely (keeps caches)
~/digibyte-guix-builds/guix-container.sh remove
```

### Step 3: Prepare Build Environment

```bash
# Inside container
cd /digibyte

# Clean any previous builds
git clean -xfd
git status  # Should be clean

# Set up environment variables
export HOSTS='x86_64-apple-darwin arm64-apple-darwin x86_64-w64-mingw32 x86_64-linux-gnu'
export SOURCES_PATH=/sources-cache
export BASE_CACHE=/depends-cache
export SDK_PATH=/sdk
export JOBS=4
export V=1

# Verify SDK is accessible
ls -la $SDK_PATH/
# Should show: Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers/
```

### Step 4: Run GUIX Build

```bash
# Inside container
cd /digibyte

# Start the build (this will take several hours)
./contrib/guix/guix-build

# Monitor progress
# The build will:
# 1. Build all dependencies
# 2. Build DigiByte for each platform
# 3. Create distribution packages
```

### Step 5: Collect Build Output

After successful build completion:

```bash
# Inside container
# Copy outputs to mounted volume
cp -r guix-build-*/output/* /output/

# List built artifacts
ls -la /output/
```

Expected output files:
- `digibyte-<version>-x86_64-apple-darwin.tar.gz`
- `digibyte-<version>-arm64-apple-darwin.tar.gz`
- `digibyte-<version>-win64.zip`
- `digibyte-<version>-x86_64-linux-gnu.tar.gz`

## Build Optimizations

### Using Substitutes (Pre-built Dependencies)

To speed up builds, you can use pre-built packages:

```bash
# Inside container
# Authorize official GUIX substitutes
sudo guix archive --authorize < /var/guix/profiles/per-user/root/current-guix/share/guix/ci.guix.gnu.org.pub

# The build will now download pre-built dependencies when available
```

### Incremental Builds

The cache directories persist between container runs:
- `/depends-cache` - Built dependencies
- `/sources-cache` - Downloaded source files

This significantly speeds up subsequent builds.

## Troubleshooting

### Common Issues

1. **Out of disk space**
   ```bash
   # Clean GUIX store
   guix gc
   
   # Clean build artifacts
   cd /digibyte
   ./contrib/guix/guix-clean
   ```

2. **SDK not found**
   - Verify SDK extraction in `depends/SDKs/`
   - Check `SDK_PATH` environment variable

3. **Build failures**
   - Check full output in `guix-build-*/build.log`
   - Ensure all environment variables are set correctly

### Debug Build

For debugging build issues:

```bash
# More verbose output
export V=1
export VERBOSE=1

# Keep failed build directories
export KEEP_FAILED_BUILD_DIRS=1
```

## Version Management

For test releases, update version in `/digibyte/configure.ac`:

```bash
# Inside container
cd /digibyte
vim configure.ac

# Set version numbers
# CLIENT_VERSION_MAJOR=8
# CLIENT_VERSION_MINOR=26
# CLIENT_VERSION_BUILD=0
# CLIENT_VERSION_RC=1  # For release candidate
```

## Security Notes

This build process:
- Creates deterministic, reproducible binaries
- Does NOT include code signing (requires separate process)
- Does NOT create attestations (for testing only)
- Should produce bit-identical outputs when run on different systems

## Post-Build Verification

After successful build:

```bash
# On host machine
cd ~/digibyte-guix-builds/output

# Verify file checksums
shasum -a 256 *.tar.gz *.zip > SHA256SUMS

# Extract and test a binary
tar -xzf digibyte-*-x86_64-linux-gnu.tar.gz
./digibyte-*/bin/digibyted --version
```

## Clean Up

To remove Docker containers and images:

```bash
# Stop and remove container
docker stop digibyte-guix
docker rm digibyte-guix

# Remove image (optional)
docker rmi digibyte-guix-builder

# Clean build outputs (optional)
rm -rf ~/digibyte-guix-builds/output/*
```

## Directory Structure Summary

All build artifacts and caches are kept outside the git repository:

```
~/digibyte-guix-builds/
├── SDKs/                    # macOS SDK files
├── output/                  # Final build outputs (.tar.gz, .zip)
├── cache/                   # General build cache
├── depends-cache/           # Compiled dependencies
├── depends-work/            # Build work directory
├── sources-cache/           # Downloaded source tarballs
├── guix-config/             # Persistent GUIX configuration
├── guix-cache/              # GUIX package cache
├── guix-container.sh        # Container management script
└── quick-build.sh           # Quick rebuild script

~/Code/digibyte/             # Your git repository (clean)
```

## Key Benefits of This Setup

1. **No Git Pollution**: All build artifacts stay outside your repository
2. **Persistent Container**: Start once, use many times
3. **Incremental Builds**: Only rebuild what changed
4. **Cached Dependencies**: First build is slow, subsequent builds are fast
5. **Easy Management**: Simple scripts for all operations
6. **Development Friendly**: Edit on macOS, build in container

## Summary

This specification enables building DigiByte Core for four platforms using GUIX on macOS:
1. Sets up Docker-based GUIX environment with persistent container
2. Keeps all build artifacts outside git repository
3. Handles macOS SDK requirements properly
4. Provides reproducible build process
5. Outputs unsigned binaries for testing
6. Supports iterative development workflow

Total build time: 
- First build: 4-8 hours (downloads and builds everything)
- Subsequent builds: 30-60 minutes (uses caches)
- Incremental builds: 5-15 minutes (only changed files)

Disk space required: ~40GB (mostly in ~/digibyte-guix-builds/)