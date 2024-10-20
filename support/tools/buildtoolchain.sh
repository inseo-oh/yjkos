#!/bin/sh
# Rebuilds toolchain

if [ -z $MAKEJOBS ]; then
    MAKEJOBS=1
fi

if [ $# -lt 1 ]; then
    echo "Usage: buildtoolchain.sh [Target architecture]"
    exit 1
fi

set -ue

export CFLAGS="-I/usr/local/include"
export CXXFLAGS=$CFLAGS
export LDFLAGS="-L/usr/local/lib"

err_handler () {
    ERR_CODE=$?
    [ $ERR_CODE -eq 0 ] && exit
    echo " !!! subcommand failed with exit code $ERR_CODE"
}

trap err_handler EXIT

ARCH=$1
PREFIX=$PWD/toolchain

case $ARCH in
  "i586")
    TARGET=i586-elf;
    ;;
  *)
    echo "Unknown arch $ARCH";
    exit 1;
    ;;
esac
SUPPORT_DIR=$PWD/support

mkdir -p $PREFIX

PATH="$PREFIX/bin:$PATH"
OLDPWD=$PWD

DOWNLOAD_DIR=$PREFIX/downloads
SRC_DIR=$PREFIX/src
BUILD_DIR=$PREFIX/build-$ARCH
MARKERS=$PREFIX/markers-$ARCH
mkdir -p $DOWNLOAD_DIR
mkdir -p $SRC_DIR
mkdir -p $BUILD_DIR
mkdir -p $MARKERS

MAKEJOBS=$(echo $MAKEFLAGS |
    awk '{ match($0, "-j([0-9]+)", arr); print arr[1] }')
if [ -z $MAKEJOBS ]; then
    MAKEJOBS=1
fi
echo "[buildtoolchain] Using $MAKEJOBS job(s)"

BINUTILS_VERSION=2.40
BINUTILS_TARFILE=binutils-$BINUTILS_VERSION.tar.gz
BINUTILS_LOCAL_TARPATH=$DOWNLOAD_DIR/$BINUTILS_TARFILE
BINUTILS_SRC_DIR=$SRC_DIR/binutils-$BINUTILS_VERSION
BINUTILS_BUILD_DIR=$BUILD_DIR/binutils
mkdir -p $BINUTILS_BUILD_DIR

GCC_VERSION=12.2.0
GCC_DIRNAME=gcc-$GCC_VERSION
GCC_TARFILE=gcc-$GCC_VERSION.tar.gz
GCC_LOCAL_TARPATH=$DOWNLOAD_DIR/$GCC_TARFILE
GCC_SRC_DIR=$SRC_DIR/gcc-$GCC_VERSION
GCC_BUILD_DIR=$BUILD_DIR/gcc
mkdir -p $GCC_BUILD_DIR


GDB_VERSION=15.1
GDB_TARFILE=gdb-$GDB_VERSION.tar.gz
GDB_LOCAL_TARPATH=$DOWNLOAD_DIR/$GDB_TARFILE
GDB_SRC_DIR=$SRC_DIR/gdb-$GDB_VERSION
GDB_BUILD_DIR=$BUILD_DIR/gdb
GDB_CONFIGURE_LOG=$PREFIX/gdb.configure.log
GDB_COMPILE_LOG=$PREFIX/gdb.compile.log
GDB_INSTALL_LOG=$PREFIX/gdb.install.log
mkdir -p $GDB_BUILD_DIR


GRUB_VERSION=2.06
GRUB_TARFILE=grub-$GRUB_VERSION.tar.gz
GRUB_LOCAL_TARPATH=$DOWNLOAD_DIR/$GRUB_TARFILE
GRUB_SRC_DIR=$SRC_DIR/grub-$GRUB_VERSION
GRUB_BUILD_DIR=$BUILD_DIR/grub
mkdir -p $GRUB_BUILD_DIR


if [ ! -f $MARKERS/binutils.download ]; then
    rm -f $MARKERS/binutils.extract
    echo "[buildtoolchain] Download binutils"
    curl https://ftp.gnu.org/gnu/binutils/$BINUTILS_TARFILE \
        -o $BINUTILS_LOCAL_TARPATH
    touch $MARKERS/binutils.download
fi

if [ ! -f $MARKERS/gcc.download ]; then
    rm -f $MARKERS/gcc.extract
    echo "[buildtoolchain] Download gcc"
    curl http://ftp.gnu.org/gnu/gcc/$GCC_DIRNAME/$GCC_TARFILE \
        -o $GCC_LOCAL_TARPATH
    touch $MARKERS/gcc.download
fi

if [ ! -f $MARKERS/gdb.download ]; then
    rm -f $MARKERS/gdb.extract
    echo "[buildtoolchain] Download gdb"
    curl https://ftp.gnu.org/gnu/gdb/$GDB_TARFILE \
        -o $GDB_LOCAL_TARPATH
    touch $MARKERS/gdb.download
fi

if [ ! -f $MARKERS/grub.download ]; then
    rm -f $MARKERS/grub.extract
    echo "[buildtoolchain] Download grub"
    curl https://ftp.gnu.org/gnu/grub/$GRUB_TARFILE \
        -o $GRUB_LOCAL_TARPATH
    touch $MARKERS/grub.download
fi

cd $SRC_DIR

if [ ! -f $MARKERS/gdb.extract ]; then
    rm -f $MARKERS/gdb.configure
    echo "[buildtoolchain] Delete existing gdb directory"
    rm -rf $GDB_SRC_DIR $GDB_BUILD_DIR
    echo "[buildtoolchain] Extract gdb"
    tar -zxf $GDB_LOCAL_TARPATH
    touch $MARKERS/gdb.extract
fi

if [ ! -f $MARKERS/binutils.extract ]; then
    rm -f $MARKERS/binutils.configure
    echo "[buildtoolchain] Delete existing binutils directory"
    rm -rf $BINUTILS_SRC_DIR $BINUTILS_BUILD_DIR
    echo "[buildtoolchain] Extract binutils"
    tar -zxf $BINUTILS_LOCAL_TARPATH
    touch $MARKERS/binutils.extract
fi

if [ ! -f $MARKERS/gcc.extract ]; then
    rm -f $MARKERS/gcc.configure
    echo "[buildtoolchain] Delete existing gcc directory"
    rm -rf $GCC_SRC_DIR $GCC_BUILD_DIR
    echo "[buildtoolchain] Extract gcc"
    tar -zxf $GCC_LOCAL_TARPATH
    touch $MARKERS/gcc.extract
fi

if [ ! -f $MARKERS/grub.extract ]; then
    rm -f $MARKERS/grub.configure
    echo "[buildtoolchain] Delete existing grub directory"
    rm -rf $GRUB_SRC_DIR $GRUB_BUILD_DIR
    echo "[buildtoolchain] Extract grub"
    tar -zxf $GRUB_LOCAL_TARPATH
    touch $MARKERS/grub.extract
fi

# TODO: Use $MARKERS for below two as well when we start patching sources.

# echo "[buildtoolchain] Patch GCC"
# cd $GCC_SRC_DIR
# patch -p0 < $SUPPORT_DIR/patches/gcc.patch

# echo "[buildtoolchain] Patch binutils"
# cd $BINUTILS_SRC_DIR
# patch -p0 < $SUPPORT_DIR/patches/binutils.patch

mkdir -p $GDB_BUILD_DIR
cd $GDB_BUILD_DIR
if [ ! -f $MARKERS/gdb.configure ]; then
    rm -f $MARKERS/gdb.compile
    $GDB_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" \
        --with-sysroot --disable-nls --disable-werror --with-expat \
        | awk '{ print "[gdb.configure] "$0 }'
    touch $MARKERS/gdb.configure
fi
if [ ! -f $MARKERS/gdb.compile ]; then
    rm -f $MARKERS/gdb.install
    gmake -j $MAKEJOBS all-gdb MAKEINFO=true \
        | awk '{ print "[gdb.compile] "$0 }'
    touch $MARKERS/gdb.compile
fi
if [ ! -f $MARKERS/gdb.install ]; then
    gmake install-gdb MAKEINFO=true \
        | awk '{ print "[gdb.install] "$0 }'
    touch $MARKERS/gdb.install
fi

mkdir -p $BINUTILS_BUILD_DIR
cd $BINUTILS_BUILD_DIR
if [ ! -f $MARKERS/binutils.configure ]; then
    rm -f $MARKERS/binutils.compile
    echo "[buildtoolchain] Configure binutils"
    $BINUTILS_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" \
        --with-sysroot --disable-nls --disable-werror \
        | awk '{ print "[binutils.configure] "$0 }'
    touch $MARKERS/binutils.configure
fi
if [ ! -f $MARKERS/binutils.compile ]; then
    rm -f $MARKERS/binutils.install
    gmake -j $MAKEJOBS MAKEINFO=true \
        | awk '{ print "[binutils.compile] "$0 }'
    touch $MARKERS/binutils.compile
fi
if [ ! -f $MARKERS/binutils.install ]; then
    gmake install MAKEINFO=true \
        | awk '{ print "[binutils.install] "$0 }'
    touch $MARKERS/binutils.install
fi

mkdir -p $GCC_BUILD_DIR
cd $GCC_BUILD_DIR
if [ ! -f $MARKERS/gcc.configure ]; then
    rm -f $MARKERS/gcc.compile
    $GCC_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" \
        --without-headers --disable-nls --enable-languages=c,c++ \
        | awk '{ print "[gcc.configure] "$0 }'
    touch $MARKERS/gcc.configure
fi
if [ ! -f $MARKERS/gcc.compile ]; then
    rm -f $MARKERS/gcc.install
    gmake all-gcc all-target-libgcc -j $MAKEJOBS \
        | awk '{ print "[gcc.compile] "$0 }'
    touch $MARKERS/gcc.compile
fi
if [ ! -f $MARKERS/gcc.install ]; then
    gmake install-gcc install-target-libgcc \
        | awk '{ print "[gcc.install] "$0 }'
    touch $MARKERS/gcc.install
fi

mkdir -p $GRUB_BUILD_DIR
cd $GRUB_BUILD_DIR
if [ ! -f $MARKERS/grub.configure ]; then
    rm -f $MARKERS/grub.compile
    $GRUB_SRC_DIR/configure --disable-werror --target=$TARGET \
        --prefix="$PREFIX" \
        | awk '{ print "[grub.configure] "$0 }'
    touch $MARKERS/grub.configure
fi
if [ ! -f $MARKERS/grub.compile ]; then
    rm -f $MARKERS/grub.install
    gmake all -j $MAKEJOBS \
        | awk '{ print "[grub.configure] "$0 }'
    touch $MARKERS/grub.compile
fi
if [ ! -f $MARKERS/grub.install ]; then
    gmake install \
        | awk '{ print "[grub.install] "$0 }'
    touch $MARKERS/grub.install
fi

echo "[buildtoolchain] Toolchain build successful"
cd $OLDPWD
