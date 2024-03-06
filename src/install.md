# jemalloc

run jemalloc install with the je prefix

cd src/jemalloc
./autogen.sh
JEPREFIX=je_
./configure --with-jemalloc-prefix=$JEPREFIX --disable-cxx
make -j
sudo make install

