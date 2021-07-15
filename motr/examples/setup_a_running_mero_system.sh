#!/usr/bin/env bash
#
# Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# For any questions about this software or licensing,
# please email opensource@seagate.com or cortx-questions@seagate.com.
#

# Daniar modify this to suit Mero configuration (referring to clovis ut clovis_dgmode_io_st.sh)

TOPDIR="$(dirname "$0")/../.."

clovis_st_util_dir="${TOPDIR}/clovis/st/utils"

. "${TOPDIR}/m0t1fs/linux_kernel/st/common.sh"
. "${TOPDIR}/m0t1fs/linux_kernel/st/m0t1fs_common_inc.sh"
. "${TOPDIR}/m0t1fs/linux_kernel/st/m0t1fs_client_inc.sh"
. "${TOPDIR}/m0t1fs/linux_kernel/st/m0t1fs_server_inc.sh"
. "${TOPDIR}/m0t1fs/linux_kernel/st/m0t1fs_sns_common_inc.sh"
CURRDIR=$(pwd)

cd $clovis_st_util_dir
./clovis_local_conf.sh
./clovis_st_inc.sh
cd $CURRDIR

N=2
K=1
P=4
stride=32
BLOCKSIZE=""
BLOCKCOUNT=""
OBJ_ID1="1048577"
OBJ_ID2="1048578"
# The second half is hex representations of OBJ_ID1 and OBJ_ID2.
OBJ_HID1="0:100001"
OBJ_HID2="0:100002"
PVER_1="7600000000000001:a"
PVER_2="7680000000000001:42"
read_verify="true"
clovis_pids=""
export cnt=1

echo "clovis_st_util_dir = $clovis_st_util_dir"
echo "TOPDIR = ${TOPDIR}"
# MOTR = 32-2-1--4          MERO = 32-3-3--15      MODIFIED MERO CONFIG = 32-2-1--4          
echo "$stride-$N-$K-$S-$P"

# Dgmode IO
SANDBOX_DIR=/var/mero
CLOVIS_TEST_DIR=$SANDBOX_DIR
CLOVIS_TEST_LOGFILE=$SANDBOX_DIR/clovis_`date +"%Y-%m-%d_%T"`.log
CLOVIS_TRACE_DIR=$SANDBOX_DIR/clovis

# This variable will be used by m0t1fs/linux_kernel/st/m0t1fs_server_inc.sh
# Specifically when doing "ios mkfs"
mero_STOB_DOMAIN="ad" # similar to m0t1fs/linux_kernel/st/m0t1fs_common_inc.sh

echo "LogDir at  $CLOVIS_TEST_LOGFILE"

# export MERO_CLIENT_ONLY=1
wait_and_exit()
{
    while [ true ] ; do
        echo "Please type EXIT or QUIT to quit"
        read keystroke
        if [ "$keystroke" == "EXIT" -o "$keystroke" == "QUIT" ] ; then
            return 0
        fi
    done
}

show_config_and_wait()
{
    echo
    echo
    echo
    echo
    echo "MERO is UP."
    echo "Mero client config:"
    echo
    echo "HA_addr    : ${lnet_nid}:$HA_EP           "
    echo "Client_addr: ${lnet_nid}:$SNS_CLI_EP "
    echo "Profile_fid: $PROF_OPT                    "
    echo "Process_fid: $M0T1FS_PROC_ID              "
    echo
    echo "Now please use another terminal to run the example"
    echo "with the above command line arguments.            "
    echo "Please add double quote \"\" around the arguments "
    echo
    echo
    echo

    wait_and_exit

    return 0
}


main()
{
    sandbox_init

    NODE_UUID=`uuidgen`
    clovis_dgmode_sandbox="$CLOVIS_TEST_DIR/sandbox"
    src_file="$CLOVIS_TEST_DIR/clovis_source"
    dest_file="$CLOVIS_TEST_DIR/clovis_dest"
    rc=0
    
    mkdir $CLOVIS_TRACE_DIR
    
    # at the newest Motr, this function is implemented at m0t1fs/linux_kernel/st/m0t1fs_server_inc.sh
    # MERO also has similar function at that file
    local multiple_pools=0
    mero_service start "$multiple_pools" "$stride" "$N" "$K" "$P" || {
        echo "Failed to start Mero Service."
        return 1
    }

    #Initialise dix
    # dix_init
       
    show_config_and_wait
    
    # ==============
    mero_service stop || {
        echo "Failed to stop Mero Service."
        rc=1
    }

    if [ $rc -eq 0 ]; then
        sandbox_fini
    else
        echo "Test log available at $CLOVIS_TEST_LOGFILE."
        error_handling $rc
    fi
    return $rc
}

trap unprepare EXIT
main
