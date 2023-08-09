FROM ubuntu:22.04

RUN apt-get update && apt-get install -y g++ git cmake && \
	rm -rf /var/lib/apt/lists/*

# if you want to install bazel, uncomment this
# ENV BAZELISK_VERSION v1.17.0
# RUN case "$(uname -m)" in \
# 		"aarch64") echo "arm64" > /tmp/arch ;; \
# 		"x86_64") echo "amd64" > /tmp/arch ;; \
# 		*) echo "invalid arch: $(uname -m)" && exit 1 ;; \
# 	esac
# RUN curl -L https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-$(cat /tmp/arch) \
# 	-o /usr/local/bin/bazel && chmod +x /usr/local/bin/bazel

# install protobuf and grpc_cpp_plugin corresponding to mtk 
ADD ext/grpc /grpc
RUN mkdir -p /grpc/cmake/build && cd /grpc/cmake/build && cmake ../.. && make protoc grpc_cpp_plugin && \
	cp -Lr ./third_party/protobuf/protoc /usr/local/bin && mv grpc_cpp_plugin /usr/local/bin && chmod +x /usr/local/bin/*

# install python bindings for using protobuf plugin
# RUN apt-get update && apt-get install -y python python-pip && pip install protobuf && \
# 	rm -rf /var/lib/apt/lists/*
