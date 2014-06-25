#! /usr/bin/env bash

# Exit on error
set -e
# Echo each command
set -x

mkdir empty
cd empty
py.test
