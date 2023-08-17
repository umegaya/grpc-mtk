#!/bin/bash
CWD=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)

set -e

curl -L https://raw.githubusercontent.com/leetal/ios-cmake/master/ios.toolchain.cmake -o ${CWD}/ios.toolchain.cmake
