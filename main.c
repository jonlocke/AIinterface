#include <stdio.h>
#include <string.h>
#include "main.h"
#include "stm32f4xx.h"
#include "stm32f4xx_usart.h"
#include "ff.h"
#include "diskio.h"

void uart_send_string(const char *str);

GPIO_InitTypeDef  GPIO_InitStructure;

// Must be volatile to prevent optimiser doing stuff
volatile uint8_t uart_rx_char = 62;

// Below is for ring buffer
#define UART_BUF_SIZE 64

volatile uint8_t uart_rx_buffer[UART_BUF_SIZE];
volatile uint8_t uart_buf_head = 0;
volatile uint8_t uart_buf_tail = 0;

extern volatile uint32_t main_thread_command;
extern volatile uint32_t main_thread_data;
extern volatile uint8_t *rom_base_start;
extern volatile uint8_t *rom_base_end;
extern volatile uint8_t *swram_low_base;
extern volatile uint8_t *swram_high_base;

extern volatile uint8_t *slot_4_base;
extern volatile uint8_t *slot_5_base;
extern volatile uint8_t *slot_6_base;
extern volatile uint8_t *slot_7_base;
extern volatile uint8_t *slot_12_base;
extern volatile uint8_t *slot_13_base;
extern volatile uint8_t *slot_14_base;
extern volatile uint8_t *slot_15_base;
extern volatile uint8_t *slots_end;

// volatile uint32_t reg_dataout_moder_s8 = 0x55550000;  // Output mode for GPIOD15–8

#ifdef ENABLE_SEMIHOSTING
extern void initialise_monitor_handles(void);   /*rtt*/
#endif

FATFS fs32;
char temp_rom[16384];

#if _USE_LFN
    static char lfn[_MAX_LFN + 1];
        fno.lfname = lfn;
            fno.lfsize = sizeof lfn;
#endif

// Enable the FPU (Cortex-M4 - STM32F4xx and higher)
// http://infocenter.arm.com/help/topic/com.arm.doc.dui0553a/BEHBJHIG.html
// Also make sure lazy stacking is disabled
void enable_fpu_and_disable_lazy_stacking() {
  __asm volatile
  (
    "  ldr.w r0, =0xE000ED88    \n"  /* The FPU enable bits are in the CPACR. */
    "  ldr r1, [r0]             \n"  /* read CAPCR */
    "  orr r1, r1, #( 0xf << 20 )\n" /* Set bits 20-23 to enable CP10 and CP11 coprocessors */
    "  str r1, [r0]              \n" /* Write back the modified value to the CPACR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb                       \n" /* reset pipeline now the FPU is enabled */
    // Disable lazy stacking (the default) and effectively have no stacking since we're not really using the FPU for anything other than a fast register store
    "  ldr.w r0, =0xE000EF34    \n"  /* The FPU FPCCR. */
    "  ldr r1, [r0]             \n"  /* read FPCCR */
    "  bfc r1, #30,#2\n" /* Clear bits 30-31. ASPEN and LSPEN. This disables lazy stacking */
    "  str r1, [r0]              \n" /* Write back the modified value to the FPCCR */
    "  dsb                       \n" /* wait for store to complete */
    "  isb"                          /* reset pipeline  */
    :::"r0","r1"
    );
}

// From some reddit thread on fast itoa
static const char itoa_lookup[][4] = {"\x00\x00\x00\x01", "\x00\x00\x00\x02", "\x00\x00\x00\x04",
    "\x00\x00\x00\x08", "\x00\x00\x01\x06", "\x00\x00\x03\x02", "\x00\x00\x06\x04",
    "\x00\x01\x02\x08", "\x00\x02\x05\x06", "\x00\x05\x01\x02"};


void itoa_base10(int val, char *dest)
{
    int bitnum, dignum;
    char sum, carry, result[4] = "\x00\x00\x00\x00";
    const char *lookup;

    for(bitnum = 0; bitnum < 10; ++bitnum) {
        if(val & (1 << bitnum)) {
            carry = 0;
            lookup = itoa_lookup[bitnum];
            for(dignum = 3; dignum >= 0; --dignum) {
                sum = result[dignum] + lookup[dignum] + carry;
                if(sum < 10) {
                    carry = 0;
                } else {
                    carry = 1;
                    sum -= 10;
                }
                result[dignum] = sum;
            }
        }
    }
    for(dignum = 0; !result[dignum] && dignum < 3; ++dignum)
        ;

    for(; dignum < 4; ++dignum) {
        *dest++ = result[dignum] + '0';
    }
    *dest++ = 0;
}

void delay_ms(const uint16_t ms)
{
   uint32_t i = ms * 27778;
   while (i-- > 0) {
      __asm volatile ("nop");
   }
}

void my_memcpy(unsigned char *dest, unsigned char *from, unsigned char *to) {
	unsigned char *q = dest;
	unsigned char *p = from;

	while (p<to) {
		*q++ = *p++;
	}
}


void copy_rom_to_ram() {

	unsigned char *swram = (unsigned char *) &swram_low_base;

	if ((&slot_5_base - &slot_4_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_4_base,(unsigned char *)&slot_5_base);
	}		
	swram+=16384;
	if ((&slot_6_base - &slot_5_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_5_base,(unsigned char *)&slot_6_base);
	}		
	swram+=16384;
	if ((&slot_7_base - &slot_6_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_6_base,(unsigned char *)&slot_7_base);
	}		
	swram+=16384;
	if ((&slot_12_base - &slot_7_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_7_base,(unsigned char *)&slot_12_base);
	}		


	swram = (unsigned char *) &swram_high_base;

	if ((&slot_13_base - &slot_12_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_12_base,(unsigned char *)&slot_13_base);
	}		
	swram+=16384;
	if ((&slot_14_base - &slot_13_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_13_base,(unsigned char *)&slot_14_base);
	}		
	swram+=16384;
	if ((&slot_15_base - &slot_14_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_14_base,(unsigned char *)&slot_15_base);
	}		
	swram+=16384;
	if ((&slots_end - &slot_15_base)> 0) {
		my_memcpy(swram,(unsigned char *)&slot_15_base,(unsigned char *)&slots_end);
	}		

}





enum sysclk_freq {
    SYSCLK_42_MHZ=0,
    SYSCLK_84_MHZ,
    SYSCLK_168_MHZ,
    SYSCLK_200_MHZ,
    SYSCLK_240_MHZ,
};

uint32_t Get_APB_Prescaler(uint32_t ppre_bits)
{
    switch (ppre_bits) {
        case 0b000: return 1;
        case 0b100: return 2;
        case 0b101: return 4;
        case 0b110: return 8;
        case 0b111: return 16;
        default:    return 1;
    }
}
uint32_t Get_USART1_Clock(void) {
    uint32_t hclk = SystemCoreClock;  // e.g., 200 MHz
    uint32_t apb2_prescaler;

    switch ((RCC->CFGR >> 13) & 0x7) {
        case 0b000: apb2_prescaler = 1; break;
        case 0b100: apb2_prescaler = 2; break;
        case 0b101: apb2_prescaler = 4; break;
        case 0b110: apb2_prescaler = 8; break;
        case 0b111: apb2_prescaler = 16; break;
        default:    apb2_prescaler = 1;
    }

    return hclk / apb2_prescaler;
}

uint32_t Get_USART1_Clock2(void)
{
    uint32_t cfgr = RCC->CFGR;
    uint32_t ppre2_bits = (cfgr >> 13) & 0x7;
    uint32_t apb2_prescaler = Get_APB_Prescaler(ppre2_bits);
    uint32_t apb2_clock = SystemCoreClock / apb2_prescaler;

    // If prescaler > 1, the USART clock = 2 × APB clock
    return (apb2_prescaler > 1) ? apb2_clock * 2 : apb2_clock;
}

void uart_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Configure (white) PA9 (TX), (green) PA10 (RX)
    GPIOA->MODER &= ~((0b11 << (9 * 2)) | (0b11 << (10 * 2)));
    GPIOA->MODER |=  (0b10 << (9 * 2)) | (0b10 << (10 * 2));
    GPIOA->AFR[1] &= ~((0xF << (1 * 4)) | (0xF << (2 * 4)));
    GPIOA->AFR[1] |= (0x7 << (1 * 4)) | (0x7 << (2 * 4));

    // Reliable baud rate setup
    uint32_t usart_clk = Get_USART1_Clock();
    uint32_t baud = 2400;
    uint32_t brr = (usart_clk + (baud / 2)) / baud;
    USART1->BRR = brr;

    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE;
    USART1->CR1 |= USART_CR1_RXNEIE;
    USART1->CR1 |= USART_CR1_UE;

    NVIC_EnableIRQ(USART1_IRQn);

    uart_send_string("AI Interface Initialized\r\n");
    
    char buffer[64];
    sprintf(buffer, "SystemCoreClock: %lu\r\nUSART1 Clock: %lu\r\nBRR: %lu\r\n",
            SystemCoreClock, usart_clk, brr);
    uart_send_string(buffer);
}

void uart_init3(void) {
    // 1. Enable clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;     // Enable GPIOA
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;    // Enable USART1

    // 2. Configure PA9 (TX) and PA10 (RX) as Alternate Function
    GPIOA->MODER &= ~((3 << (9 * 2)) | (3 << (10 * 2)));       // Clear MODER
    GPIOA->MODER |=  (2 << (9 * 2)) | (2 << (10 * 2));         // Set to AF

    GPIOA->AFR[1] &= ~((0xF << ((9 - 8) * 4)) | (0xF << ((10 - 8) * 4)));
    GPIOA->AFR[1] |=  (0x7 << ((9 - 8) * 4)) | (0x7 << ((10 - 8) * 4));  // AF7 (USART1)

    // 3. Set baud rate dynamically based on actual PCLK2
    uint32_t usart_clk = Get_USART1_Clock();  // PCLK2 for USART1
    uint32_t brr = (usart_clk + (115200 / 2)) / 115200;  // Rounded
    USART1->BRR = brr;

    // 4. Enable USART1, TX, RX, RX interrupt
    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    // 5. Enable USART1 interrupt in NVIC
    NVIC_EnableIRQ(USART1_IRQn);

    // 6. Output confirmation
    uart_send_string("UART Initialized\r\n");

    char buffer[64];
    sprintf(buffer, "SystemCoreClock: %lu\r\nUSART1 Clock: %lu\r\nBRR: %lu\r\n",
            SystemCoreClock, usart_clk, brr);
    uart_send_string(buffer);
}

void uart_init2(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Configure (white) PA9 (TX), (green) PA10 (RX)
    GPIOA->MODER &= ~((0b11 << (9 * 2)) | (0b11 << (10 * 2)));
    GPIOA->MODER |=  (0b10 << (9 * 2)) | (0b10 << (10 * 2));
    GPIOA->AFR[1] &= ~((0xF << (1 * 4)) | (0xF << (2 * 4)));
    GPIOA->AFR[1] |= (0x7 << (1 * 4)) | (0x7 << (2 * 4));
    /*  Added to calc BRR from discovered speeds
    uint32_t usart_clk = Get_USART1_Clock();
    uint32_t brr = usart_clk / 115200;
    USART1->BRR = brr;
    */
    USART1->BRR = (SystemCoreClock / 4) / 115200; // Adjust depending on clock tree
    //USART1->BRR = 365; //168mhz // Adjust depending on clock tree
    //USART1->BRR = 434; //200mhz // Adjust depending on clock tree
    //USART1->BRR = 1736; // Adjust depending on clock tree
    USART1->CR1 |= USART_CR1_TE | USART_CR1_RE;
    USART1->CR1 |= USART_CR1_RXNEIE;
    USART1->CR1 |= USART_CR1_UE;

    NVIC_EnableIRQ(USART1_IRQn);
    uart_send_string("UART Initialized\r\n");
    char buffer[32];
    sprintf(buffer, "Clock: %lu\r\n", SystemCoreClock);
    uart_send_string(buffer);

}

void uart_send_string(const char *str)
{
    while (*str)
    {
        while (!(USART1->SR & USART_SR_TXE)); // Wait until TX buffer empty
        USART1->DR = *str++;
    }
}

void rcc_set_frequency(enum sysclk_freq freq)
{
    int freqs[]   = {42, 84, 168, 200, 240};
 
    /* USB freqs: 42MHz, 42Mhz, 48MHz, 50MHz, 48MHz */
    int pll_div[] = {2, 4, 7, 10, 10}; 
 
    /* PLL_VCO = (HSE_VALUE / PLL_M) * PLL_N */
    /* SYSCLK = PLL_VCO / PLL_P */
    /* USB OTG FS, SDIO and RNG Clock =  PLL_VCO / PLLQ */
    uint32_t PLL_P = 2;
    uint32_t PLL_N = freqs[freq] * 2;
    uint32_t PLL_M = (HSE_VALUE/1000000);
    uint32_t PLL_Q = pll_div[freq];
 
    RCC_DeInit();
 
    /* Enable HSE osscilator */
    RCC_HSEConfig(RCC_HSE_ON);
 
    if (RCC_WaitForHSEStartUp() == ERROR) {
        return;
    }
 
    /* Configure PLL clock M, N, P, and Q dividers */
    RCC_PLLConfig(RCC_PLLSource_HSE, PLL_M, PLL_N, PLL_P, PLL_Q);
 
    /* Enable PLL clock */
    RCC_PLLCmd(ENABLE);
 
    /* Wait until PLL clock is stable */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);
 
    /* Set PLL_CLK as system clock source SYSCLK */
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
 
    /* Set AHB clock divider */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
 
    //FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;
    FLASH->ACR =  FLASH_ACR_ICEN |FLASH_ACR_DCEN |FLASH_ACR_LATENCY_5WS;

    /* Set APBx clock dividers */
    switch (freq) {
        /* Max freq APB1: 42MHz APB2: 84MHz */
        case SYSCLK_42_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div1); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 42MHz */
            break;
        case SYSCLK_84_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div2); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div1); /* 84MHz */
            break;
        case SYSCLK_168_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 42MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 84MHz */
            break;
        case SYSCLK_200_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 50MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 100MHz */
            break;
        case SYSCLK_240_MHZ:
            RCC_PCLK1Config(RCC_HCLK_Div4); /* 60MHz */
            RCC_PCLK2Config(RCC_HCLK_Div2); /* 120MHz */
            break;
    }
 
    /* Update SystemCoreClock variable */
    SystemCoreClockUpdate();
}

//void SD_NVIC_Configuration(FunctionalState state)
//{
//        NVIC_InitTypeDef NVIC_InitStructure;
//
//        /* Configure the NVIC Preemption Priority Bits */
//        //NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
//        //NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
//
//        NVIC_InitStructure.NVIC_IRQChannel = SDIO_IRQn;
//        //NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
//        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;    // This must be a lower priority (ie. higher number) than the phi0 int
//        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
//        NVIC_InitStructure.NVIC_IRQChannelCmd = state;
//        NVIC_Init(&NVIC_InitStructure);
//}
//
//
//
//void SDIO_IRQHandler(void)
//{
//  /* Process All SDIO Interrupt Sources */
//  SD_ProcessIRQSrc();
//}

// _IORQ interrupt
void config_PC0_int(void) {
        EXTI_InitTypeDef EXTI_InitStruct;
        NVIC_InitTypeDef NVIC_InitStruct;

        /* Enable clock for SYSCFG */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

        SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource0);

        EXTI_InitStruct.EXTI_Line = EXTI_Line0;
        /* Enable interrupt */
        EXTI_InitStruct.EXTI_LineCmd = ENABLE;
        /* Interrupt mode */
        EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
        /* Triggers on rising and falling edge */
        //EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;
        EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
        /* Add to EXTI */
        EXTI_Init(&EXTI_InitStruct);

        /* Add IRQ vector to NVIC */
        /* PC0 is connected to EXTI_Line0, which has EXTI0_IRQn vector */
        NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
        /* Set priority */
        NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0x00;
        /* Set sub priority */
        NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0x00;
        /* Enable interrupt */
        NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
        /* Add to NVIC */
        NVIC_Init(&NVIC_InitStruct);
}




/* SD card uses PC10, PC11, PC12 out and PC8 in */
void config_gpio_portc(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOC Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	/* Configure GPIO Settings */
	// non SDIO
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12;

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
#ifdef DISABLE_PULLUPS_FOR_SDCARD
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
#else
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
#endif
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	// Also SD Card (so will inherit the pullup/nopull setting above
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
	//GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 ;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIOC->ODR = 0xFFFF;
}

/* Input/Output data GPIO pins on PD{8..15}. Also PD2 is used fo MOSI on the STM32F407VET6 board I have */
void config_gpio_data(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOD Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 ;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
#ifdef DISABLE_PULLUPS_FOR_SDCARD
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
#else
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
#endif
	GPIO_Init(GPIOD, &GPIO_InitStructure);
}

/* Input Address GPIO pins on PE{0..15} */
void config_gpio_addr(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOE Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = 
		GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
		GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7 | 
		GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | 
		GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
}

/* Debug GPIO pins on PA0 */
void config_gpio_dbg(void) {
	GPIO_InitTypeDef  GPIO_InitStructure;
	/* GPIOA Periph clock enable */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4,DISABLE);


	/* Configure GPIO Settings */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void ADC_Config(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    // Enable clock for ADC1
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    // Init GPIOA for ADC input
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Init ADC1
    ADC_InitTypeDef ADC_InitStruct;
    ADC_InitStruct.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStruct.ADC_ExternalTrigConv = DISABLE;
    ADC_InitStruct.ADC_ExternalTrigConvEdge =
    ADC_ExternalTrigConvEdge_None;
    ADC_InitStruct.ADC_NbrOfConversion = 1;
    ADC_InitStruct.ADC_Resolution = ADC_Resolution_8b;
    ADC_InitStruct.ADC_ScanConvMode = DISABLE;
    ADC_Init(ADC1, &ADC_InitStruct);
    ADC_Cmd(ADC1, ENABLE);


}

uint16_t ADC_Read(int channel)
{
   uint16_t v;
   switch(channel) {
	   case (0): 
    		ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 1, ADC_SampleTime_84Cycles);
		break;
	   case (1): 
    		ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 1, ADC_SampleTime_84Cycles);
		break;
	   default:
		return 0;
   }
    // Start ADC conversion
    ADC_SoftwareStartConv(ADC1);
    // Wait until conversion is finish
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    v = ADC_GetConversionValue(ADC1);
    return v;
}


void config_backup_sram(void) {

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
        PWR_BackupAccessCmd(ENABLE);
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE);
        PWR_BackupRegulatorCmd(ENABLE);
}

//
// My Stuff - JL

uint8_t uart_buffer_pop_or_null(void) {
    if (uart_buf_head == uart_buf_tail)
        return 0; // No data

    uint8_t c = uart_rx_buffer[uart_buf_tail];
    uart_buf_tail = (uart_buf_tail + 1) % UART_BUF_SIZE;
    return c;
}

void USART1_IRQHandler(void)
{
    if (USART1->SR & USART_SR_RXNE)
    {
        //uart_rx_char = USART1->DR;
        uart_rx_char = (uint8_t)(USART1->DR & 0xFF);

        /* Below is for ring buffer */
        //uint8_t c = USART1->DR;
        //uint8_t next = (uart_buf_head + 1) % UART_BUF_SIZE;
        //if (next != uart_buf_tail) {
        //    uart_rx_buffer[uart_buf_head] = c;
        //    uart_buf_head = next;
        //}
// else: overflow — optionally handle

        //uart_rx_char = 'B'; // to test to see if b is recieved when the interrupt hits
        uint8_t temp = (uint8_t)(USART1->DR & 0xFF);
        //uart_rx_char = temp;
        __DSB(); // Ensure the value is fully written to RAM before system continues
        //USART1->DR = 'R'; // test char to test uart output 
        //USART1->DR = temp & 0xFF; // enable for echoing back the char 
    }
}
//


void scan_and_load_roms() {
	FRESULT res;
	FIL	fil;
        TCHAR root_directory[11] = "/boot/";

        TCHAR full_filename[64];
        DIR dir;
        static FILINFO fno;
	UINT BytesRead;
        char *swram;
	int rom_offset;
	int  root_directory_base_length= strlen(root_directory);

	for (int highlow = 0;highlow <= 1 ; highlow++) {
           if (highlow==0) {
              swram = (char *) &swram_high_base;
           } else {
              swram = (char *) &swram_low_base;
           }
    	   for (int i = 0; i<=3 ; i++) {
              if (highlow==0) {
	         rom_offset=12;
              } else {
                 rom_offset=4;
              }
              itoa_base10(i+rom_offset, &root_directory[root_directory_base_length]);
#ifdef SEMIHOSTING_SDCARD
	      printf("about to open %s\n",root_directory);
#endif
              res = f_opendir(&dir, root_directory);
              if (res == FR_OK) {
#ifdef SEMIHOSTING_SDCARD
		   printf("dir open\n");
#endif
                   for (;;) {
                          res = f_readdir(&dir, &fno);                   /* Read a directory item */
			  if (res != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
                          strcpy(full_filename,root_directory);
                          strcat(full_filename,"/");
                          strcat(full_filename,fno.fname);
                          res = f_open(&fil, full_filename, FA_READ);
                          if (res == FR_OK) {
#ifdef SEMIHOSTING_SDCARD
                             printf("%d,%d: opened %s 0x%08x\n",highlow,i,full_filename, swram);
#endif
                             if (highlow==0) {
   	                        res = f_read(&fil, swram, 16384, &BytesRead);
                             } else {
                                // The f_read will never write directly to CCMRAM
   	                        res = f_read(&fil, temp_rom, 16384, &BytesRead);
                                memcpy(swram,temp_rom,16384);
                             }
                             f_close(&fil);
                          }
                          break;   // only interested in the first file in the dir
                   }
                   f_closedir(&dir);
              }
	      swram+=0x4000;
           }
        }
}


void load_rom(uint8_t slot, uint8_t rom_number_on_sdcard) {
	FRESULT res;
	FIL	fil;
        TCHAR root_directory[18] = "/roms/";
        TCHAR full_filename[64];
        DIR dir;
        static FILINFO fno;
	UINT BytesRead;
        char *swram;
	int root_directory_base_length = strlen(root_directory);
	int highslot;





	// only handle slots 4 to 7 and 12 to 15
	if (slot >=12 && slot <=15) {
        	swram = (char *) &swram_high_base;
		highslot=1;
	} else if (slot >=4 && slot <=7) {
        	swram = (char *) &swram_low_base;
		highslot=0;
	} else {
		return;
	}


	itoa_base10(rom_number_on_sdcard, &root_directory[root_directory_base_length]);

        res = f_opendir(&dir, root_directory);
	
        if (res == FR_OK) {
        	for (;;) {
                          res = f_readdir(&dir, &fno);                   /* Read a directory item */
                          if (res != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
                          strcpy(full_filename,root_directory);
                          strcat(full_filename,"/");
                          strcat(full_filename,fno.fname);
                          res = f_open(&fil, full_filename, FA_READ);
                          if (res == FR_OK) {
                             //printf(">>>opened %s 0x%08x\n",full_filename, swram);
			     if (highslot) {
   	                        swram += ((slot-12)<<14);
   	                        res = f_read(&fil, swram, 16384, &BytesRead);
                             } else {
   	                        swram += ((slot-4)<<14);
   	                        res = f_read(&fil, temp_rom, 16384, &BytesRead);
                                memcpy(swram,temp_rom,16384);
                             }

			     if (res == FR_OK) {
                                f_close(&fil);
		             } 
			  }
                          break;   // only interested in the first file in the dir
                   }
                   f_closedir(&dir);
         }
}

void	clear_rom_area(uint32_t *p) {
	for (int i = 0 ; i < 0x4000 ; i+= 0x1000) {
		p[i]=0; p[i+1]=0;   // clear 8 bytes at start of each rom
	}

}
// probably dont need to turn the optimiser off, but it kept on annoying me at the time
int __attribute__((optimize("O0")))  

main(void) {
	FRESULT res;

	// FPU related
	enable_fpu_and_disable_lazy_stacking();

	clear_rom_area((uint32_t *) &swram_high_base);
	clear_rom_area((uint32_t *) &swram_low_base);

	// Assign some of the FPU registers to be actually used as integer 'fast access' during the ISR
	// register types dont really matter, so long as we get the assignment to work.
	register uint32_t zero_register asm("s0") __attribute__((unused)) = 0;
	register uint32_t one_register asm("s1") __attribute__((unused)) = 1;

	register volatile unsigned char* copy_gpioc_base asm("s2") __attribute__((unused)) = (unsigned char*) GPIOC;
	register volatile unsigned char* copy_exti_base asm("s3") __attribute__((unused)) = (unsigned char*) EXTI;
	// combo register:
	//   b31-b16 - Effectively a copy of the lower 16 bits of the MODER register (for controlling whether PD2 is a GPIO or Alt function (for SDIO)
	//   b15-b8  - Current state of PD2. Either 04 for PD2=1, or 00 for PD2=0
	//   b7-b0   - Current ROM slot register (updated when a 6502 write to FE05 occurs)
	register uint32_t copy_combo_register asm("s4") __attribute__((unused)) = 0x00100000;
	register volatile unsigned char* copy_gpioa_base asm("s5") __attribute__((unused)) = (unsigned char*) GPIOA;
	register volatile uint8_t* copy_swram_high_base asm("s6") __attribute__((unused)) = (volatile uint8_t*) &swram_high_base;
	register volatile uint8_t* copy_swram_low_base asm("s7") __attribute__((unused)) = (volatile uint8_t*) &swram_low_base;
	// 5555 = d15-d8 outputs and 0010 is d2 out
	register uint32_t copy_dataout_moder asm("s8") __attribute__((unused)) = 0x55550010;
	register uint32_t copy_adc_data asm("s9") __attribute__((unused)) = 0x00000000;
	// Use some of the high fpu registers as a sort of stack. eg. save r11 to s30 on ISR entry, then put it back on ISR exit
	register volatile uint8_t* fake_stack_r11 asm("s30") __attribute__((unused));

	//__disable_irq();
	//
	// If you define roms to load in roms-preloaded-from-flash.S, they will get loaded in here.
	copy_rom_to_ram();

#ifdef ENABLE_SEMIHOSTING
        initialise_monitor_handles();   /*rtt*/
	printf("Semi hosting on\n");
#endif

	rcc_set_frequency(SYSCLK_240_MHZ);
	  // switch on compensation cell
	RCC->APB2ENR |= 0 |  RCC_APB2ENR_SYSCFGEN ;
	SYSCFG->CMPCR |= SYSCFG_CMPCR_CMP_PD; // enable compensation cell
	while ((SYSCFG->CMPCR & SYSCFG_CMPCR_READY) == 0);  // wait until ready

	//__disable_irq();
	
	// Enable CCMRAM clock
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_CCMDATARAMEN, ENABLE); 

        config_backup_sram();
	
	config_gpio_data();

	config_gpio_addr();

	config_gpio_portc();

        config_gpio_dbg();

	//NVIC_SystemLPConfig(NVIC_LP_SEVONPEND, ENABLE);
        NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); 

	SysTick->CTRL  = 0;

	ADC_Config();

        memset(&fs32, 0, sizeof(FATFS));
	
	// Change to lazy mounting the SD card
        res = f_mount(&fs32, "",0);


	if (res != FR_OK) {
#ifdef SEMIHOSTING_SDCARD
		printf("Failed to mount. Error = %d\n",res);
#endif
	   // TODO. Flash some LED or something if the SD card does not mount
	   while (1);
	} else {
#ifdef SEMIHOSTING_SDCARD
	   printf("mounted ok\n");
#endif
	}

	// Look for ROM images on the SD card and load them
	scan_and_load_roms();
	//f_mount(0, "1:", 1); // unmount

	config_PC0_int();

	int	check_adc_conversion_finished = 0;
	uint16_t adc_value;
    // Initialize Uart
    uart_init();
    // The main loop
	while(1) {
		if (check_adc_conversion_finished) {
			// check if conversion complete
			if (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
				adc_value = ADC_GetConversionValue(ADC1);
				// try to adjust the analog value at the extremes (in case the potentiometers in the joystick dont go all the way to 0 or 255)
				if (adc_value < ADC_LOW_THRESHOLD) {
					adc_value=0;
				} else if (adc_value > ADC_HIGH_THRESHOLD) {
					adc_value = 255;
				}

				// put adc value in lowest byte of s9. Clear b31 of s9 to say value available
				// NB: We lose the channel id out of s9 at this point. Not the end of the world.
    				__asm volatile ("mov r3,%[adc_value] \n bfc r3,#31,#1 \n vmov s9,r3 "::[adc_value] "r" (adc_value) : "r3");
				check_adc_conversion_finished=0;
			}
		}

                if (!(main_thread_command & 0xc0000000) && (main_thread_command & MAIN_THREAD_COMMANDS_MASK)) {
                        switch (main_thread_command & MAIN_THREAD_COMMANDS_MASK) {
                           case (MAIN_THREAD_ROM_SWAP_COMMAND):
                                // LOAD A NEW ROM INTO A SLOT
                                main_thread_command |= 0x40000000;
		
				load_rom( ((main_thread_command & 0x00000f00)>>8), (main_thread_command & 0x000000ff) );

				main_thread_command = 0x00000000;

                                break;
                           case (MAIN_THREAD_REQUEST_ADC_CONVERSION_COMMAND):
                                main_thread_command |= 0x40000000;
				// Start conversion of channel 2, or channel 3, or channel 4 etc.
                		ADC_RegularChannelConfig(ADC1, (uint8_t) ((main_thread_command & 0x00000003)+2), 1, ADC_SampleTime_480Cycles);
                		//ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 1, ADC_SampleTime_84Cycles);
    				ADC_SoftwareStartConv(ADC1);

				check_adc_conversion_finished=1;
				main_thread_command = 0x00000000;
				break;
			   default: 
				main_thread_command = 0x00000000;
				break;
			   
                        }
                }
        }
}

