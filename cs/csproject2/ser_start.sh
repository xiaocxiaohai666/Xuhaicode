#!/bin/bash
if [ -f Makefile ]
then
    make clean
    make all
fi
if [ -f service ]
then
    ./service service.conf
fi