#ifndef __MAIN_H
#define __MAIN_H

#ifndef __ASSEMBLER__
#include <stdint.h>
extern volatile uint8_t uart_rx_char;
#endif

#define MAIN_THREAD_ROM_SWAP_COMMAND    0x20000000   //b29
#define MAIN_THREAD_REQUEST_ADC_CONVERSION_COMMAND    0x10000000   //b28
#define MAIN_THREAD_COMMANDS_MASK	(MAIN_THREAD_ROM_SWAP_COMMAND | MAIN_THREAD_REQUEST_ADC_CONVERSION_COMMAND)
//#define MAIN_THREAD_COMMANDS_MASK	(MAIN_THREAD_ROM_SWAP_COMMAND)

// If your joystick can go from one extreme to the other then maybe use 0 to 255
#define ADC_LOW_THRESHOLD 0
#define ADC_HIGH_THRESHOLD 255

// If your joystick does not go full swing from 0 to 255, you might need these 'jump thresholds' to help
//#define ADC_LOW_THRESHOLD 64
//#define ADC_HIGH_THRESHOLD 192
#ifndef __ASSEMBLER__
extern volatile uint8_t uart_rx_char;
#endif

#endif

