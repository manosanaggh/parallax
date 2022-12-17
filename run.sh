#Assuming you have built parallax, go to build/YCSB-CXX/ and add sudo before mkfs and ycsb-edb commands in run-ycsb.sh
cd build/YCSB-CXX/
sudo rm -rf /mnt/fmap/parallax.txt
sudo fallocate -l 10G /mnt/fmap/parallax.txt
./run-ycsb.sh /mnt/fmap/parallax.txt 1000 1000 sd
cd ../../
