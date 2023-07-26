mkdir m4
cp ax_cuda.m4 m4/ax_cuda.m4
autoreconf -i
mkdir build
cd build
../configure --enable-experimental --with-cuda=yes
make -j50
