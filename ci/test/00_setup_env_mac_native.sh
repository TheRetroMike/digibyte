#!/usr/bin/env bash
#
# Copyright (c) 2019-present The DigiByte Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export HOST=x86_64-apple-darwin
# Homebrew's python@3.12 is marked as externally managed (PEP 668).
# Therefore, `--break-system-packages` is needed.
export PIP_PACKAGES="--break-system-packages zmq"
export GOAL="install"
export DIGIBYTE_CONFIG="--with-gui=no --enable-reduce-exports"
export CI_OS_NAME="macos"
# Remove NO_DEPENDS=1 to use depends system as requested
export OSX_SDK=""
export CCACHE_MAXSIZE=400M
# Enable unit and functional tests as requested
export RUN_UNIT_TESTS=true
export RUN_FUNCTIONAL_TESTS=true
