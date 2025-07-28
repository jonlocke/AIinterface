make clean
make 
STM32_Programmer_CLI.exe -c port=USB1 -e all -d electron-rom-emulator.bin 0x8000000 -v
