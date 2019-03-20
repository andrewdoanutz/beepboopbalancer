/*
===============================================================================
 Name        : dac_output.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#include "board.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define DEFAULT_I2C          I2C0
#define DATA_SIZE 0x400
#define SPEED_100KHZ         100000

//left GPIO pins

#define GPIOLPI3    1
#define GPIOLPO3    5
#define GPIOLPI2	26
#define GPIOLPO2	2
#define GPIOLPI1	19
#define GPIOLPO1	0

//right GPIO pins

#define GPIORPI3    20
#define GPIORPO3    0
#define GPIORPI2	22
#define GPIORPO2	2
#define GPIORPI1	18
#define GPIORPO1	1


static int mode_poll;   /* Poll/Interrupt mode flag */
static I2C_ID_T i2cDev = DEFAULT_I2C; /* Currently active I2C device */

/* DAC sample rate request time */
#define DAC_TIMEOUT 0x3FF

//slave info
#define I2C_SLAVE_TEMP_ADDR         0x68

static I2C_XFER_T temp_xfer;

/* Data area for slave operations */

static uint8_t i2Cbuffer[2][256];

static volatile uint8_t channelTC, dmaChannelNum;
static volatile uint8_t DAC_Interrupt_Done_Flag, Interrupt_Continue_Flag;

//DMA handler
void DMA_IRQHandler(void)
{
	if (Chip_GPDMA_Interrupt(LPC_GPDMA, dmaChannelNum) == SUCCESS) {
		channelTC++;
	}
	else {
		/* Error, do nothing */
	}
}
//setup i2c functions
static void i2c_read_setup(I2C_XFER_T *xfer, uint8_t addr, int numBytes)
{
	xfer->slaveAddr = addr;
	xfer->rxBuff = 0;
	xfer->txBuff = 0;
	xfer->txSz = 0;
	xfer->rxSz = numBytes;
	xfer->rxBuff = i2Cbuffer[1];

}

static void i2c_send_setup(I2C_XFER_T *xfer, uint8_t addr, int numBytes)
{
	xfer->slaveAddr = addr;
	xfer->rxBuff = 0;
	xfer->txBuff = i2Cbuffer[0];
	xfer->txSz = numBytes;
	xfer->rxSz = 0;

}

static void i2c_state_handling(I2C_ID_T id)
{
	if (Chip_I2C_IsMasterActive(id)) {
		Chip_I2C_MasterStateHandler(id);
	} else {
		Chip_I2C_SlaveStateHandler(id);
	}
}

static void i2c_set_mode(I2C_ID_T id, int polling)
{
	if(!polling) {
		mode_poll &= ~(1 << id);
		Chip_I2C_SetMasterEventHandler(id, Chip_I2C_EventHandler);
		NVIC_EnableIRQ(id == I2C0 ? I2C0_IRQn : I2C1_IRQn);
	} else {
		mode_poll |= 1 << id;
		NVIC_DisableIRQ(id == I2C0 ? I2C0_IRQn : I2C1_IRQn);
		Chip_I2C_SetMasterEventHandler(id, Chip_I2C_EventHandlerPolling);
	}
}

static void i2c_app_init(I2C_ID_T id, int speed)
{
	Board_I2C_Init(id);

	/* Initialize I2C */
	Chip_I2C_Init(id);
	Chip_I2C_SetClockRate(id, speed);

	/* Set default mode to interrupt */
	i2c_set_mode(id, 0);
}

//i2c probe test
static void i2c_probe_slaves(I2C_ID_T i2c)
{
	int i;
	uint8_t ch[2];

	DEBUGOUT("Probing available I2C devices...\r\n");
	DEBUGOUT("\r\n     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
	DEBUGOUT("\r\n====================================================");
	for (i = 0; i <= 0x7F; i++) {
		if (!(i & 0x0F)) DEBUGOUT("\r\n%02X  ", i >> 4);
		if (i <= 7 || i > 0x78) {
			DEBUGOUT("   ");
			continue;
		}

		if(Chip_I2C_MasterRead(i2c, i, ch, 1 + (i == 0x48)) > 0)
			DEBUGOUT(" %02X", i);
		else
			DEBUGOUT(" --");
	}
	DEBUGOUT("\r\n");
}



//i2c handlers
void I2C1_IRQHandler(void)
{
	i2c_state_handling(I2C1);
}


void I2C0_IRQHandler(void)
{
	i2c_state_handling(I2C0);
}


int main(void) {

	uint32_t dacClk;
	uint32_t tmp = 0;


	//freq sounds to play through DAC
	uint16_t  freq_sound9 = 0x0F6;
	uint16_t  freq_sound1 = 0x0DC;
	uint16_t  freq_sound2 = 0x0C4;
	uint16_t  freq_sound3 = 0x0AE;
	uint16_t  freq_sound4 = 0x0AE;
	uint16_t  freq_sound5 = 0x0AE;
	uint16_t  freq_sound6 = 0x0A4;
	uint16_t  freq_sound7 = 0x092;
	uint16_t  freq_sound8 = 0x082;


	//y axis and other temp variables
	int16_t  y           = 0;
	int      tmp1        = 0;
	static  I2C_XFER_T xfer;

	SystemCoreClockUpdate();
	Board_Init();

	i2c_app_init(I2C0, SPEED_100KHZ);
	i2c_app_init(I2C1, SPEED_100KHZ);

	i2c_set_mode(I2C0, 0);
	i2cDev = I2C0;


	//set GPIO output pins for LEDs
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIOLPO3, GPIOLPI3);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIOLPO2, GPIOLPI2);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIOLPO1, GPIOLPI1);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIORPO3, GPIORPI3);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIORPO2, GPIORPI2);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, GPIORPO1, GPIORPI1);

	//Set Leds Off
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);

	/*power-on MPU, value 0 sent*/
	i2Cbuffer[0][0] = 0x6B;
    i2Cbuffer[0][1]	= 0x00;
	i2c_send_setup(&temp_xfer, I2C_SLAVE_TEMP_ADDR, 2);
	tmp1 = Chip_I2C_MasterSend(i2cDev, temp_xfer.slaveAddr, temp_xfer.txBuff, 2);
	/* Setup DAC pins for board and common CHIP code */
	Chip_DAC_Init(LPC_DAC);

	/* Setup DAC timeout for polled and DMA modes to 0x3FF */
#if defined(CHIP_LPC175X_6X)
	/* 175x/6x devices have a DAC divider, set it to 1 */
	Chip_Clock_SetPCLKDiv(SYSCTL_PCLK_DAC, SYSCTL_CLKDIV_1);
#endif
	Chip_DAC_SetDMATimeOut(LPC_DAC, DAC_TIMEOUT);

	/* Compute and show estimated DAC request time */
#if defined(CHIP_LPC175X_6X)
	dacClk = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_DAC);
#else
	dacClk = Chip_Clock_GetPeripheralClockRate();
#endif
		dacClk, (dacClk / DAC_TIMEOUT);

	/* Enable count and DMA support */
	Chip_DAC_ConfigDAConverterControl(LPC_DAC, DAC_CNT_ENA | DAC_DMA_ENA);

	while (1)
	{
			/* y values */
			i2Cbuffer[0][0] = 0x3D;
			i2c_send_setup(&temp_xfer, I2C_SLAVE_TEMP_ADDR, 1);
			tmp1 = Chip_I2C_MasterSend(i2cDev, temp_xfer.slaveAddr, temp_xfer.txBuff, 1);
			i2c_read_setup(&temp_xfer, I2C_SLAVE_TEMP_ADDR, 2);
			tmp1 = Chip_I2C_MasterRead(i2cDev, temp_xfer.slaveAddr, temp_xfer.rxBuff, 2);
			y = ((i2Cbuffer[1][0] << 8) | i2Cbuffer[1][1]);


			/* Change Pitch and LED state based on position*/
			if (y > 14500 )
			{
				tmp += (freq_sound9 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < 14500) && (y > 9500))
			{
				tmp += (freq_sound2 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < 9500) && (y > 4500))
			{
				tmp += (freq_sound3 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < 4500) && (y > 1000))
			{
				tmp += (freq_sound4 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < 1000) && (y > -1000))
			{
				tmp += (freq_sound1 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < -1000) && (y > -5000))
			{
				tmp += (freq_sound5 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < -5000) && (y > -10000))
			{
				tmp += (freq_sound6 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if ((y < -10000) && (y > -15000))
			{
				tmp += (freq_sound7 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}
			else if (y < -15000)
			{
				tmp += (freq_sound8 % DATA_SIZE);
				if (tmp == (DATA_SIZE - 1)) {
					tmp = 0;
				}
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO3, GPIOLPI3);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO2, GPIOLPI2);
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, GPIOLPO1, GPIOLPI1);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO3, GPIORPI3);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO2, GPIORPI2);
				Chip_GPIO_SetPinOutLow(LPC_GPIO, GPIORPO1, GPIORPI1);
			}


			Chip_DAC_UpdateValue(LPC_DAC, tmp);

			/* Wait for DAC (DMA) interrupt request */
		    while (!(Chip_DAC_GetIntStatus(LPC_DAC))) {}
	}

}
