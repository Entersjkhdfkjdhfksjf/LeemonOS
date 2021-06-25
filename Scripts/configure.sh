SPATH=$(dirname $(readlink -f "$0"))
export LEMOND="$SPATH/.."

if [ -z "$LEMON_SYSROOT" ]; then
    export LEMON_SYSROOT=$HOME/.local/share/lemon/sysroot
fi

export PATH="$HOME/.local/share/lemon/bin:$PATH"

set -e

ln -sfT ../../../include/c++ $HOME/.local/share/lemon/sysroot/system/include/c++
cp $HOME/.local/share/lemon/lib/x86_64-lemon/c++/*.so* $HOME/.local/share/lemon/sysroot/system/lib

cd $SPATH
$SPATH/libc.sh

cd $SPATH/..

if ! [ -x "$(command -v lemon-clang)" ]; then
    echo "Lemon cross toolchain not found (Did you forget to build toolchain?)"
    exit 1
fi

meson Build --cross $SPATH/lemon-crossfile.txt

cd "$LEMOND/Ports"
./buildport.sh zlib
./buildport.sh libpng
./buildport.sh freetype
./buildport.sh libressl
