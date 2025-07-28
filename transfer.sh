#sudo dfu-util -v -d 0483:df11 -a 0 -s 0x08000000 -D electron-rom-emulator.bin
STM32_Programmer_CLI.exe -c port=USB1 -d electron-rom-emulator.bin 0x8000000 -v
