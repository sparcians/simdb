#!/usr/bin/env bash

set -x

echo "Starting Build Entry"
echo "HOME:" $HOME
echo "GITHUB_WORKSPACE:" $GITHUB_WORKSPACE
echo "GITHUB_EVENT_PATH:" $GITHUB_EVENT_PATH
echo "PWD:" `pwd`

cd ${GITHUB_WORKSPACE}
cd $SIMDB_BUILD_TYPE
make -j$(nproc --all) simdb_regress
REGRESS_SIMDB=$?
if [ ${REGRESS_SIMDB} -ne 0 ]; then
    echo "ERROR: regress of SimDB FAILED!!!"
    exit 1
fi

# simdb_regress includes ArgosSmoke, though it runs relatively quickly.
# For github checks, this will run for several minutes to fully bash
# collection and deserialization.
make ArgosSmoke_10k
REGRESS_ARGOS=$?
if [ ${REGRESS_ARGOS} -ne 0 ]; then
    echo "ERROR: ArgosSmoke FAILED!!!"
    exit 1
fi
