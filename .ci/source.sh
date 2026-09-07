#!/bin/bash -ex

GITDATE="`git show -s --date=short --format='%ad' | sed 's/-//g'`"
GITREV="`git show -s --format='%h'`"
REV_NAME="azahar-unified-source-${GITDATE}-${GITREV}"

if [ "$GITHUB_REF_TYPE" = "tag" ]; then
    REV_NAME="azahar-unified-source-$GITHUB_REF_NAME"
fi

COMPAT_LIST='dist/compatibility_list/compatibility_list.json'

mkdir artifacts

pip3 install git-archive-all
touch "${COMPAT_LIST}"
git describe --abbrev=0 --always HEAD > GIT-COMMIT
git describe --tags HEAD > GIT-TAG || echo 'unknown' > GIT-TAG
git archive-all --include "${COMPAT_LIST}" --include GIT-COMMIT --include GIT-TAG --force-submodules artifacts/"${REV_NAME}.tar"

cd artifacts/
tarlz -v -9z "${REV_NAME}.tar"
lziprecover -v -Fc "${REV_NAME}.tar.lz" || fec_failed=1
if [ ${fec_failed:-0} -eq 1 ]
then
 echo "fec file creation failed! this is likely due to lziprecover either being out of date (minimum of 1.25 required) or simply not installed."
fi
sha256sum "${REV_NAME}.tar.lz" > "${REV_NAME}.tar.lz.sha256sum"
cd ..
