#!/bin/sh
# Created by Ryan Hamor 3/27/2024
module=aesdchar
device=aesdchar
mode="664"

case $1 in
    start)
        echo "Loading aesdchar module"
        modprobe ${module} || exit 1
        major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
        rm -f /dev/${device}
        mknod /dev/${device} c $major 0 
        ;;
    stop)
        echo "Unloading aesdchar module"
        # invoke rmmod with all arguments we got
        rmmod $module || exit 1

        # Remove stale nodes
        rm -f /dev/${device}
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0