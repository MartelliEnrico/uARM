#!/bin/bash

if [ `id -u` != "0" ]; then
    echo "You must be root to run this script" 1>&2
    exit 1
fi

OS=`uname -s`

if test $OS == "Linux"; then
    PREF="/usr"
else
    PREF="/usr/local"
fi
ICONSONLY="false"

while [ -n "$1" ]; do
    case "$1" in
        "-d")
            PREF="$2"
            echo "$2" >> custom_install_directory
            shift
            ;;
        "-i")
            ICONSONLY="true"
            ;;
        "*")
            echo "unknown option $1, skipping"
            ;;
    esac
    shift
done

ICONSD=$PREF"/lib/uarm/icons"
mkdir -p "$ICONSD"
cp icons/* "$ICONSD"

if [ "$ICONSONLY" == "false" ]; then
    INCLUDED=$PREF"/include/uarm"
    TESTD=$PREF"/share/doc/uarm/examples"
    LDSCRIPTSD=$INCLUDED"/ldscripts"
    DEFAULTD="/etc/default"
    BIND=$PREF"/bin"

    mkdir -p "$INCLUDED"
    mkdir -p "$TESTD"
    mkdir -p "$LDSCRIPTSD"

    cp ldscript/* "$LDSCRIPTSD"
    cp test/* "$TESTD"
    cp facilities/* "$INCLUDED"

    if test $OS == "Linux"; then
        cp uarm elf2uarm uarm-mkdev "$BIND"
    else
        cp -r uarm.app /applications
        cp elf2uarm "$BIND"
    fi

    cp default/* "$DEFAULTD"
fi

exit 0