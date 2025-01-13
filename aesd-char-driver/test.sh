# try to unload the module
sudo ./aesdchar_unload

make clean && make

# Load the module
sudo ./aesdchar_load

# run test
# ../assignment-autotest/test/assignment9/drivertest.sh