#!/bin/sh
make clean
make CC=gcc CCFLAGS="-g -Wall" LDFLAGS="-g -Wall -I/"