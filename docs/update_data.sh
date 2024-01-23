#! /bin/bash

# psi_config_md.tmpl is adapted from https://github.com/pseudomuto/protoc-gen-doc/blob/master/examples/templates/grpc-md.tmpl.
docker run --rm -v $(pwd)/reference/:/out \
                -v $(pwd)/../:/protos \
                pseudomuto/protoc-gen-doc \
                --doc_opt=/out/psi_config_md.tmpl,psi_config.md psi/proto/psi.proto


# psi_v2_config_md.tmpl is adapted from https://github.com/pseudomuto/protoc-gen-doc/blob/master/examples/templates/grpc-md.tmpl.
docker run --rm -v $(pwd)/reference/:/out \
                -v $(pwd)/../:/protos \
                pseudomuto/protoc-gen-doc \
                --doc_opt=/out/psi_v2_config_md.tmpl,psi_v2_config.md psi/proto/psi_v2.proto

# launch_config_md.tmpl is adapted from https://github.com/pseudomuto/protoc-gen-doc/blob/master/examples/templates/grpc-md.tmpl.
docker run --rm -v $(pwd)/reference/:/out \
                -v $(pwd)/../:/protos \
                -v $(pwd)/../../yacl/yacl:/protos/yacl \
                pseudomuto/protoc-gen-doc \
                --doc_opt=/out/launch_config_md.tmpl,launch_config.md psi/proto/entry.proto yacl/link/link.proto
