#~/bin/bash
BASEDIR=$(dirname "$0")

chmod +x $BASEDIR/bin/linux-*/avant
chmod +x $BASEDIR/bin/time_src_ctl
find $BASEDIR -type f -name "*.sh" -print0 | xargs -0 chmod +x

rm -f $BASEDIR/*.cfg