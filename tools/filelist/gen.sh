#!/bin/bash
echo "set(grpc_core_src `cat - | sed -e 's|src/core|grpc/src/core|g' | sed -e 's|third_party/|grpc/third_party/|g'`)" > $1