if [ $1 -e "-c" ]
then
	rm -rf build
fi
mkdir -p build
cd build
if [ $1 -e "-c" ]
then
	cmake3 .. -DCMAKE_BUILD_TYPE="Release" .
fi
make
rm -rf /opt/manosanag/teraheap-opensrc/allocator/ummap-io/libparallax.so
cp -r lib/libparallax.so /opt/manosanag/teraheap-opensrc/allocator/ummap-io/
