make clean
make
sudo rmmod petmem.ko
sudo insmod petmem.ko
sudo ../../setup.sh petmem
scp add_module_symbol.gdb mdathikulislam@onyxnode32.boisestate.edu:/data/mdathikulislam/
sudo ./user/petmem 128
