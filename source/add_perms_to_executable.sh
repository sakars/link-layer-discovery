USER_HOME=$(getent passwd $SUDO_USER | cut -d: -f6)
mkdir -p ${USER_HOME}/bin/ndisc
cp ../out/ndisc ${USER_HOME}/bin/ndisc/
setcap cap_net_raw,cap_net_admin+eip ${USER_HOME}/bin/ndisc/ndisc

