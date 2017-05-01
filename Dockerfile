FROM ubuntu:16.10

RUN apt-get update && \
	apt-get install -y zlib1g-dev libssl1.0.0 gcc g++ cmake git pkg-config build-essential autoconf libtool gdb curl unzip && \
	rm -rf /var/lib/apt/lists/*

# install protobuf corresponding to mtk 
ARG GRPC_COMMIT
RUN git clone https://github.com/umegaya/grpc && cd /grpc && git checkout $GRPC_COMMIT && \
	git submodule update --init && \
	cd third_party/protobuf && ./autogen.sh && ./configure && make && make install && \
	cd / &&  rm -rf /grpc

# install python bindings for using protobuf plugin
RUN apt-get update && apt-get install -y python python-pip && pip install protobuf && rm -rf /var/lib/apt/lists/*

