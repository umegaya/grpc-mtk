#!/bin/bash
echo "set(grpc_core_src `cat - | sed -e 's|src/core|ext/grpc/src/core|g' | sed -e 's|third_party/|ext/grpc/third_party/|g'`)" > $1