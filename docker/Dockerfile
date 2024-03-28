FROM openanolis/anolisos:8.8 as builder

# build nsjail
RUN yum install -y \
    wget autoconf bison flex git protobuf-devel libnl3-devel \
    libtool make pkg-config protobuf-compiler \
    && yum clean all

RUN cd / && git clone https://github.com/google/nsjail.git \
    && cd /nsjail && git checkout 3.3 -b v3.3 \
    && make && mv /nsjail/nsjail /bin

FROM openanolis/anolisos:8.8

ARG TARGETPLATFORM

ENV TZ="Asia/Shanghai"

RUN yum install -y libgomp && yum clean all

COPY --from=builder /bin/nsjail /usr/local/bin/
COPY .nsjail /root/.nsjail

COPY --chown=root:root ./$TARGETPLATFORM/main /root/main

LABEL maintainer="secretflow-contact@service.alipay.com"

ARG version
ENV version $version

ARG config_templates=""
LABEL kuscia.secretflow.config-templates=$config_templates

ARG deploy_templates=""
LABEL kuscia.secretflow.deploy-templates=$deploy_templates

# run as root for now
WORKDIR /root

CMD ["/bin/bash"]
