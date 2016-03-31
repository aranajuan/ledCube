/* ###################################################################
**     Filename    : main.c
**     Project     : LedCube20
**     Processor   : MC9S08SH8CPJ
**     Version     : Driver 01.12
**     Compiler    : CodeWarrior HCS08 C Compiler
**     Date/Time   : 2016-03-30, 22:23, # CodeGen: 0
**     Abstract    :
**         Main module.
**         This module contains user's application code.
**     Settings    :
**     Contents    :
**         No public methods
**
** ###################################################################*/
/*!
** @file main.c
** @version 01.12
** @brief
**         Main module.
**         This module contains user's application code.
*/         
/*!
**  @addtogroup main_module main module documentation
**  @{
*/         
/* MODULE main */


/* Including needed modules to compile this module/procedure */
#include "Cpu.h"
#include "Events.h"
#include "SPI.h"
#include "PTC.h"
#include "PTA.h"
#include "PTB.h"
#include "TPM1.h"
/* Include shared modules, which are used for whole project */
#include "PE_Types.h"
#include "PE_Error.h"
#include "PE_Const.h"
#include "IO_Map.h"

/* User includes (#include below this line is not maintained by Processor Expert) */
#define SER1 PTAD_PTAD0
#define SER2 PTAD_PTAD1
#define	CK_SIPO	PTAD_PTAD2=1;PTAD_PTAD2=0;
#define	CK_OUT	PTAD_PTAD3=1;PTAD_PTAD3=0;
#define CAPA0	PTCD_PTCD0
#define CAPA1	PTBD_PTBD0
#define CAPA2	PTCD_PTCD1
#define CAPA3	PTBD_PTBD1
#define ALLOFF CAPA0=0;CAPA1=0;CAPA2=0;CAPA3=0;

typedef struct {
	uint8_t cluster_sectors;
	uint16_t sector_size;
	uint16_t fat_start;	//sector
	uint16_t root_dir_start;	//sector
	uint16_t dir_cnt;
	uint16_t data_start;	//sector
	uint32_t fsize;	//bytes
	uint16_t fcurr_clust;	//current cluster
	uint8_t fcurr_sector;	// current sector
	uint16_t fcurr_offset;
} FAT16Info;

uint8_t strCMP(uint8_t * c1, uint8_t * c2, uint8_t len) {
	len--;
	while (len) {
		if (c1[len] != c2[len]) {
			return 0;
		}
		len--;
	}
	if (c1[0] != c2[0]) {
		return 0;
	}
	return 1;
}

uint8_t SPI_write(uint8_t ch) {
	while (!SPIS_SPTEF) {
	}
	SPID = ch;
	while (SPIS_SPTEF == 0) {
	}
	while (SPIS_SPRF == 0) {
	}
	return SPID;
}

uint8_t SPI_read() {
	return SPI_write(0xFF);
}

uint8_t SD_command(uint8_t cmd, uint32_t arg, uint8_t crc) {

	uint8_t i;

	SPI_write(cmd);
	SPI_write((arg & 0xFF000000) >> 24);
	SPI_write((arg & 0x00FF0000) >> 16);
	SPI_write((arg & 0x0000FF00) >> 8);
	SPI_write(arg & 0x000000FF);
	SPI_write(crc);

	cmd = SPI_read();
	for (i = 0; i < 10 && cmd == 0xFF; i++) {
		cmd = SPI_read();
	}
	return cmd;
}

uint8_t SD_read(uint32_t sector, uint16_t offset, uint8_t * buffer,
		uint16_t len) {

	uint8_t response, lim;

	SPI_read();
	sector = sector * 512;
	response = SD_command(0x51, sector, 0xFF);

	if (response != 0x00) {
		return response;
	}

	response = SPI_read();
	for (lim = 0; response != 0xFE; lim++) {
		if (lim == 100) {
			return 0xFE;
		}
		response = SPI_read();
	}

	for (; offset != 0; offset--) // "skip" bytes
		SPI_read();

	for (offset = 0; offset < len; offset++) // read len bytes
		buffer[offset] = SPI_read();

	/*stop reading*/
	response = SD_command(0x4C, 0x00000000, 0x95);

	// skip checksum
	SPI_read();
	SPI_read();
	return 0x00;
}

uint8_t SD_init() {
	uint8_t i, lim;
	for (i = 0; i < 10; i++) { // idle for 1 bytes / 80 clocks
		SPI_write(0xFF);
	}

	i = SD_command(0x40, 0x00000000, 0x95);
	if (i != 0x01) {
		return i;
	}

	i = SD_command(0x41, 0x00000000, 0xFF);
	for (lim = 0; i != 0x00; lim++) {
		if (lim == 5) {
			return i;
		}
		i = SD_command(0x41, 0x00000000, 0xFF);
	}
	return SD_command(0x50, 0x00000200, 0xFF);

}

void fixEndian(uint8_t *ptr, uint8_t len) {
	uint8_t swap, i;
	i = 0;
	len--;
	while (len > i) {
		swap = ptr[len];
		ptr[len] = ptr[i];
		ptr[i] = swap;
		i++;
		len--;
	}

}

void FSmount(FAT16Info * FS) {
	uint32_t PT1startSector;
	uint16_t FATlen;
	SD_read(0x0, 0x1C6, &PT1startSector, 4);
	fixEndian(&PT1startSector, 4);

	SD_read(PT1startSector, 0x00B, &FS->sector_size, 2);
	fixEndian(&FS->sector_size, 2);
	SD_read(PT1startSector, 0x011, &FS->dir_cnt, 2);
	fixEndian(&FS->dir_cnt, 2);

	FS->fat_start = PT1startSector + 1;

	SD_read(PT1startSector, 0xD, &FS->cluster_sectors, 1);

	SD_read(PT1startSector, 0x16, &FATlen, 2);
	fixEndian(&FATlen, 2);

	FS->root_dir_start = FS->fat_start + (FATlen * 2);

	FS->data_start = FS->root_dir_start + 32;
}

uint8_t FSopen_file(uint8_t * name, FAT16Info * FS) {
	uint8_t i;
	uint8_t buffer[11];
	for (i = 0; i <= FS->dir_cnt; i++) {
		SD_read(FS->root_dir_start, (i * 32), buffer, 11);
		PTCD_PTCD1 = 0;
		if (strCMP(name, buffer, 11) == 1) {
			SD_read(FS->root_dir_start, (i * 32) + 26, buffer, 6);
			FS->fcurr_clust = (buffer[0] | buffer[1] << 8);
			FS->fsize = buffer[5] << 24 | buffer[4] << 16 | buffer[3] << 8
					| buffer[2];
			FS->fcurr_sector = 0;
			FS->fcurr_offset = 0;
			return 1;
		}
	}
	return 0;
}

void FS_read(uint8_t * buffer, FAT16Info * FS, uint8_t len) {
	if (FS->fcurr_offset == FS->sector_size) {
		FS->fcurr_offset = 0;
		FS->fcurr_sector++;
	}
	if (FS->fcurr_sector == FS->cluster_sectors) {
		FS->fcurr_sector = 0;
		/*Buscar nuevo cluster*/
	}
	/*
	 if(len>FS->fsize){
	 len=FS->fsize;
	 }
	 FS->fsize=FS->fsize-len;
	 */
	SD_read(FS->data_start + ((FS->fcurr_clust - 2) * FS->cluster_sectors), 0,
			buffer, len);
}

void delay() {

	uint8_t i, f;
	for (i = 0; i < 0xff; i++) {
		for (f = 0; f < 0xff; f++) {

		}
	}

}

void loadSIPOS(uint8_t c1, uint8_t c2) {
	SER1 = (c1 >> 6 & 0x01);
	SER2 = (c2 >> 6 & 0x01);
	CK_SIPO

	SER1 = (c1 >> 7 & 0x01);
	SER2 = (c2 >> 7 & 0x01);
	CK_SIPO

	SER1 = (c1 >> 1 & 0x01);
	SER2 = (c2 >> 4 & 0x01);
	CK_SIPO

	SER1 = (c1 & 0x01);
	SER2 = (c2 >> 5 & 0x01);
	CK_SIPO

	SER1 = (c1 >> 3 & 0x01);
	SER2 = (c2 >> 2 & 0x01);
	CK_SIPO

	SER1 = (c1 >> 2 & 0x01);
	SER2 = (c2 >> 3 & 0x01);
	CK_SIPO

	SER1 = (c1 >> 5 & 0x01);
	SER2 = (c2 & 0x01);
	CK_SIPO
	;
	SER1 = (c1 >> 4 & 0x01);
	SER2 = (c2 >> 1 & 0x01);
	CK_SIPO

	CK_OUT
}

FAT16Info FATFS;
uint32_t BUFFERLEN;
uint8_t bufferF[300];
uint8_t timer_effect = 0;
uint16_t ief = 0;
uint8_t capa = 0;
uint8_t cycles = 0;
uint8_t jmpto;
uint8_t jmpin;

ISR(ISR_TIMER) {
	TPM1SC_TOIE = 0;
	ALLOFF
	timer_effect++;
	if (bufferF[ief * 9 + 8] == 0x0) {
		jmpin = ief + bufferF[ief * 9];
		jmpto = ief + 1;
		cycles = bufferF[ief * 9 + 1]-1;
		ief++;
	}
	if (timer_effect >= bufferF[ief * 9 + 8]) {
		timer_effect = 0;
		if (cycles == 0 || ief != jmpin) {
			ief++;
			if ((ief * 9) >= BUFFERLEN) {
				ief = 0;
			}
		} else {
			ief = jmpto;
			cycles--;
		}
	}
	switch (capa) {
	case 0:
		//loadSIPOS(0x81,0x81);
		loadSIPOS(bufferF[ief * 9], bufferF[ief * 9 + 1]);
		CAPA0 = 1;
		break;
	case 1:
		//loadSIPOS(0x42,0x42);
		loadSIPOS(bufferF[ief * 9 + 2], bufferF[ief * 9 + 3]);
		CAPA1 = 1;
		break;
	case 2:
		//loadSIPOS(0x24,0x24);
		loadSIPOS(bufferF[ief * 9 + 4], bufferF[ief * 9 + 5]);
		CAPA2 = 1;
		break;
	case 3:
		//loadSIPOS(0x18,0x18);
		loadSIPOS(bufferF[ief * 9 + 6], bufferF[ief * 9 + 7]);
		CAPA3 = 1;
		break;
	default:
		capa = 0;
	}
	capa++;
	if (capa > 3) {
		capa = 0;
	}
	TPM1SC_TOF = 0;
	TPM1SC_TOIE = 1;
}



void main(void)
{
  /* Write your local variable definition here */

  /*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/
  PE_low_level_init();
  /*** End of Processor Expert internal initialization.                    ***/

  /* Write your code here */
  /* For example: for(;;) { } */

	ALLOFF
	TPM1SC_TOIE = 0;
	/*
	 loadSIPOS(0xff,0xff);
	 CAPA1=1;
	 for(;;){}
	 */
	delay();
	while (SD_init() != 0x00) {
		delay();
	}

	FSmount(&FATFS);
	if (FSopen_file("INDEX   CBP", &FATFS)) {
		BUFFERLEN = FATFS.fsize;
		FS_read(bufferF, &FATFS, BUFFERLEN);
	}
	TPM1SC_TOIE = 1; // start timer
	for (;;) {

	}

  /*** Don't write any code pass this line, or it will be deleted during code generation. ***/
  /*** RTOS startup code. Macro PEX_RTOS_START is defined by the RTOS component. DON'T MODIFY THIS CODE!!! ***/
  #ifdef PEX_RTOS_START
    PEX_RTOS_START();                  /* Startup of the selected RTOS. Macro is defined by the RTOS component. */
  #endif
  /*** End of RTOS startup code.  ***/
  /*** Processor Expert end of main routine. DON'T MODIFY THIS CODE!!! ***/
  for(;;){}
  /*** Processor Expert end of main routine. DON'T WRITE CODE BELOW!!! ***/
} /*** End of main routine. DO NOT MODIFY THIS TEXT!!! ***/

/* END main */
/*!
** @}
*/
/*
** ###################################################################
**
**     This file was created by Processor Expert 10.3 [05.09]
**     for the Freescale HCS08 series of microcontrollers.
**
** ###################################################################
*/
