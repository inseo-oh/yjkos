#!/bin/sh
# Rebuilds toolchain

if [ -z $MAKEJOBS ]; then
    MAKEJOBS=1
fi

if [ $# -lt 2 ]; then
    echo "Usage: buildtoolchain.sh [Target architecture] [Destination dir]"
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
PREFIX=$2

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
BUILD_DIR=$PREFIX/build
mkdir -p $DOWNLOAD_DIR
mkdir -p $SRC_DIR
mkdir -p $BUILD_DIR

echo "Maximum jobs for Make(Change with MAKEJOBS environment variable): $MAKEJOBS"

BINUTILS_VERSION=2.40
BINUTILS_TARFILE=binutils-$BINUTILS_VERSION.tar.gz
BINUTILS_LOCAL_TARPATH=$DOWNLOAD_DIR/$BINUTILS_TARFILE
BINUTILS_SRC_DIR=$SRC_DIR/binutils-$BINUTILS_VERSION
BINUTILS_BUILD_DIR=$BUILD_DIR/binutils
echo " >>> Delete existing binutils directories"
rm -rf $BINUTILS_SRC_DIR $BINUTILS_BUILD_DIR
mkdir -p $BINUTILS_BUILD_DIR

GCC_VERSION=12.2.0
GCC_DIRNAME=gcc-$GCC_VERSION
GCC_TARFILE=gcc-$GCC_VERSION.tar.gz
GCC_LOCAL_TARPATH=$DOWNLOAD_DIR/$GCC_TARFILE
GCC_SRC_DIR=$SRC_DIR/gcc-$GCC_VERSION
GCC_BUILD_DIR=$BUILD_DIR/gcc
echo " >>> Delete existing gcc directories"
rm -rf $GCC_SRC_DIR $GCC_BUILD_DIR
mkdir -p $GCC_BUILD_DIR


GDB_VERSION=15.1
GDB_TARFILE=gdb-$GDB_VERSION.tar.gz
GDB_LOCAL_TARPATH=$DOWNLOAD_DIR/$GDB_TARFILE
GDB_SRC_DIR=$SRC_DIR/gdb-$GDB_VERSION
GDB_BUILD_DIR=$BUILD_DIR/gdb
echo " >>> Delete existing gdb directories"
rm -rf $GDB_SRC_DIR $GDB_BUILD_DIR
mkdir -p $GDB_BUILD_DIR


GRUB_VERSION=2.06
GRUB_TARFILE=grub-$GRUB_VERSION.tar.gz
GRUB_LOCAL_TARPATH=$DOWNLOAD_DIR/$GRUB_TARFILE
GRUB_SRC_DIR=$SRC_DIR/grub-$GRUB_VERSION
GRUB_BUILD_DIR=$BUILD_DIR/grub
echo " >>> Delete existing grub directories"
rm -rf $GRUB_SRC_DIR $GRUB_BUILD_DIR
mkdir -p $GRUB_BUILD_DIR


echo " >>> Download binutils"
if [ ! -f $BINUTILS_LOCAL_TARPATH ]; then
    curl https://ftp.gnu.org/gnu/binutils/$BINUTILS_TARFILE -o $BINUTILS_LOCAL_TARPATH
fi

echo " >>> Download gcc"
if [ ! -f $GCC_LOCAL_TARPATH ]; then
    curl http://ftp.tsukuba.wide.ad.jp/software/gcc/releases/$GCC_DIRNAME/$GCC_TARFILE -o $GCC_LOCAL_TARPATH
fi

echo " >>> Download gdb"
if [ ! -f $GDB_LOCAL_TARPATH ]; then
    curl https://ftp.gnu.org/gnu/gdb/$GDB_TARFILE -o $GDB_LOCAL_TARPATH
fi

echo " >>> Download grub"
if [ ! -f $GRUB_LOCAL_TARPATH ]; then
    curl https://ftp.gnu.org/gnu/grub/$GRUB_TARFILE -o $GRUB_LOCAL_TARPATH
fi

cd $SRC_DIR

echo " >>> Extract binutils"
tar -zxf $BINUTILS_LOCAL_TARPATH

echo " >>> Extract gcc"
tar -zxf $GCC_LOCAL_TARPATH

echo " >>> Extract gdb"
tar -zxf $GDB_LOCAL_TARPATH

echo " >>> Extract grub"
tar -zxf $GRUB_LOCAL_TARPATH

# echo " >>> Patch GCC"
# cd $GCC_SRC_DIR
# patch -p0 < $SUPPORT_DIR/patches/gcc.patch

# echo " >>> Patch binutils"
# cd $BINUTILS_SRC_DIR
# patch -p0 < $SUPPORT_DIR/patches/binutils.patch

cd $GDB_BUILD_DIR
echo " >>> Configure gdb"
$GDB_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror --with-expat
echo " >>> Compile gdb"
gmake -j $MAKEJOBS all-gdb MAKEINFO=true
echo " >>> Install gdb"
gmake install-gdb  MAKEINFO=true

cd $BINUTILS_BUILD_DIR
echo " >>> Configure binutils"
$BINUTILS_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
echo " >>> Compile binutils"
gmake -j $MAKEJOBS MAKEINFO=true
echo " >>> Install binutils"
gmake install MAKEINFO=true

cd $GCC_BUILD_DIR
echo " >>> Configure gcc"
$GCC_SRC_DIR/configure --target=$TARGET --prefix="$PREFIX" --without-headers --disable-nls --enable-languages=c,c++
echo " >>> Compile gcc"
gmake all-gcc all-target-libgcc -j $MAKEJOBS
echo " >>> Install gcc"
gmake install-gcc install-target-libgcc
cd $GCC_BUILD_DIR


cd $GRUB_BUILD_DIR
echo " >>> Configure grub"
$GRUB_SRC_DIR/configure --disable-werror --target=$TARGET --prefix="$PREFIX" 
echo " >>> Compile grub"
gmake all -j $MAKEJOBS
echo " >>> Install grub (not to the computer itself - don't worry)"
gmake install
cd $GRUB_BUILD_DIR


echo " >>> Toolchain build successful"
cd $OLDPWD
