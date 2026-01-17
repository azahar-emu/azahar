#!/bin/bash -ex

GITREV="`git show -s --format='%h'`" || true

if [ "$GITHUB_REF_TYPE" = "tag" ]; then
    TAG_NAME=$GITHUB_REF_NAME
elif [[ -n $GITREV ]]; then
    TAG_NAME=$GITREV
else
    TAG_NAME=unknown
fi

echo "Tag name is: $TAG_NAME"

docker build -f docker/azahar-room/Dockerfile -t azahar-room:$TAG_NAME .
mkdir -p build
docker save azahar-room:$TAG_NAME > build/azahar-room-$TAG_NAME.dockerimage
