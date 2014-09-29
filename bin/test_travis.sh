#! /usr/bin/env bash

# Exit on error
set -e
# Echo each command
set -x

mkdir -p empty
cd empty
cat << EOF | python
import fastcache
if not fastcache.test():
    raise Exception('Tests failed')
EOF
