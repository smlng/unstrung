#!/bin/sh

BUILDDEPS=${HOME}/deps
mkdir -p $BUILDDEPS

MBEDTLS=${BUILDDEPS}/mbedtls
LIBCBOR=${BUILDDEPS}/libcbor

[ ! -d $MBEDTLS ] &&  {
    git clone -b mcr_add_otherName https://github.com/mcr/mbedtls.git $MBEDTLS
}

(cd $MBEDTLS && cmake -DCMAKE_INSTALL_PREFIX=$BUILDDEPS . && make && make install )

[ ! -d $LIBCBOR ] && {
    git clone https://github.com/mcr/libcbor.git $LIBCBOR
}

(cd $LIBCBOR && cmake . -DCMAKE_INSTALL_PREFIX:PATH=${BUILDDEPS} && make && make install)

touch Makefile.local
if grep MBEDTLS Makefile.local
then
    :
else
    echo MBEDTLSH=-I${BUILDDEPS}/include          >>Makefile.local
    echo MBEDTLSLIB=${BUILDDEPS}/lib              >>Makefile.local
fi

if grep CBOR_LIB Makefile.local
then
    :
else
    echo CBOR_LIB=${BUILDDEPS}/libcbor/src/libcbor.a      >>Makefile.local
    echo CBOR_INCLUDE=-I${BUILDDEPS}/include -Drestrict=__restrict__   >>Makefile.local
fi
