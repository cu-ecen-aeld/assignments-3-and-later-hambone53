#!/bin/sh
# Created by Ryan Hamor 1/29/2024

case $1 in
    start)
        echo "Starting aesdsocket"
        start-stop-daemon -S -n aesdsocket --exec aesdsocket -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0