sudo ../aesd-char-driver/aesdchar_unload
sudo ../aesd-char-driver/aesdchar_load
strace -o /tmp/strace-aesdsocket.txt -f ./aesdsocket