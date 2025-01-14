#!/bin/bash

set -ex

show_help() {
    echo "Usage: bash build.sh [OPTION]... -v {the_version}"
    echo "  -v  --version"
    echo "          the version to build with."
    echo "  -l --latest"
    echo "          tag this version as latest."
    echo "  -u --upload"
    echo "          upload to docker registry."
    echo "  -s --skip"
    echo "          skip building binary."
}

source_file="../psi/version.h"
PSI_VERSION_MAJOR=$(grep "#define PSI_VERSION_MAJOR" $source_file | cut -d' ' -f3)
PSI_VERSION_MINOR=$(grep "#define PSI_VERSION_MINOR" $source_file | cut -d' ' -f3)
PSI_VERSION_PATCH=$(grep "#define PSI_VERSION_PATCH" $source_file | cut -d' ' -f3)
PSI_DEV_IDENTIFIER=$(grep "#define PSI_DEV_IDENTIFIER" $source_file | cut -d' ' -f3 | sed 's/"//g')

PSI_VERSION="${PSI_VERSION_MAJOR}.${PSI_VERSION_MINOR}.${PSI_VERSION_PATCH}${PSI_DEV_IDENTIFIER}"
echo "Current PSI version is $PSI_VERSION."

SKIP=0
while [[ "$#" -ge 1 ]]; do
    case $1 in
        -v|--version)
            VERSION="$2"
            shift
            if [[ "$#" -eq 0 ]]; then
                show_help
                exit 1
            fi
            shift
        ;;
        -l|--latest)
            LATEST=1
            shift
        ;;
        -u|--upload)
            UPLOAD=1
            shift
        ;;
        -s|--skip)
            SKIP=1
            shift
        ;;
        *)
            echo "Unknown argument passed: $1"
            exit 1
        ;;
    esac
done

if [[ -z ${VERSION} ]]; then
    echo "Verison is not provided. Use PSI_VERSION."
    VERSION=$PSI_VERSION
fi

GREEN="\033[32m"
NO_COLOR="\033[0m"

DOCKER_REG="secretflow"

IMAGE_TAG=${DOCKER_REG}/psi-anolis8:${VERSION}
LATEST_TAG=${DOCKER_REG}/psi-anolis8:latest

echo -e "Build psi binary ${GREEN}PSI ${PSI_VERSION}${NO_COLOR}..."

SCRIPT_DIR="$(realpath $(dirname $0))"

if [[ SKIP -eq 0 ]]; then
    docker run -it  --rm   --mount type=bind,source="${SCRIPT_DIR}/../",target=/home/admin/dev/src -w /home/admin/dev  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow/release-ci:1.7 /home/admin/dev/src/docker/entry.sh
    echo -e "Finish building psi binary ${GREEN}${IMAGE_LITE_TAG}${NO_COLOR}"
fi

cd $SCRIPT_DIR

echo -e "Building docker image ${GREEN}${IMAGE_TAG}${NO_COLOR}..."
docker buildx build --platform linux/amd64 -f Dockerfile -t ${IMAGE_TAG} --build-arg version=${VERSION} --build-arg config_templates="$(cat config_templates.yml)" --build-arg deploy_templates="$(cat deploy_templates.yml)" .
echo -e "Finish building docker image ${GREEN}${IMAGE_LITE_TAG}${NO_COLOR}"

if [[ UPLOAD -eq 1 ]]; then
    docker push ${IMAGE_TAG}
fi


if [[ LATEST -eq 1 ]]; then
    echo -e "Tag and push ${GREEN}${LATEST_TAG}${NO_COLOR} ..."
    docker tag ${IMAGE_TAG} ${LATEST_TAG}
    if [[ UPLOAD -eq 1 ]]; then
        docker push ${LATEST_TAG}
    fi
fi

echo ${VERSION} > version.txt

cd -
