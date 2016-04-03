/* ###################################################################
 **     Filename    : main.c
 **     Project     : test1
 **     Processor   : MC9S08SH8CPJ
 **     Version     : Driver 01.12
 **     Compiler    : CodeWarrior HCS08 C Compiler
 **     Date/Time   : 2016-01-17, 18:33, # CodeGen: 0
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
#include "PTA.h"
#include "PTC.h"
#include "TPM1.h"
#include "SPI.h"
#include "PTB.h"
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

#define CACHE_SIZE	2
#define CACHE_ROW_SIZE	4
#define DELAY	for (i = 0; i < 0xff; i++) {for (f = 0; f < 0xff; f++) {}}

typedef struct {
	uint8_t layer[8];
	uint8_t time;
} Effect;

typedef struct {
	uint8_t ief;
	Effect effects[CACHE_ROW_SIZE];
} CacheItem;

typedef struct {
	uint8_t cluster_sectors;
	uint16_t sector_size;
	uint16_t fat_start;	//sector
	uint16_t root_dir_start;	//sector
	uint16_t dir_cnt;
	uint16_t data_start;	//sector
	uint32_t fsize;	//bytes
	uint16_t fcurr_clust;	//current cluster
	/*
	 uint8_t fcurr_sector;	// current sector
	 uint16_t fcurr_offset;
	 */
} FAT16Info;

typedef struct {
	uint8_t timer_effect;
	uint8_t ief;
	uint8_t capa;
	uint8_t cycles;
	uint8_t jmpto;
	uint8_t jmpin;
	uint8_t maxEff;
	Effect *ef;
	uint8_t lastcache;
} Status;

/* GLOBAL VARS */
static FAT16Info FATFS;
static CacheItem cache[CACHE_SIZE];
static Status status;
/* GLOBAL VARS */

/* SD FUNCTIONS*/
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
			/*
			 FS->fcurr_sector = 0;
			 FS->fcurr_offset = 0;
			 */
			return 1;
		}
	}
	return 0;
}

void FS_read(uint8_t * buffer, uint16_t offset, uint8_t len) {
	//calcular offset en sectores y clusters

	uint16_t clust_move;
	uint16_t of_b = offset % FATFS.sector_size;
	uint8_t of_sec = offset / FATFS.sector_size;
	uint8_t of_clus = of_sec / FATFS.cluster_sectors;
	uint8_t len2;
	of_sec = of_sec % FATFS.cluster_sectors;

	clust_move = FATFS.fcurr_clust;
	if (of_clus != 0) {
		for (; of_clus > 0; of_clus--) {
			SD_read(FATFS.fat_start, (clust_move * 2), &clust_move, 2);
			fixEndian(&clust_move, 2);
		}
	}
	len2 = len;
	if ((of_b + len) > FATFS.sector_size) {
		len2 = FATFS.sector_size - of_b;
		len = len - len2;
	} else {
		len = 0;
	}

	while (SD_read(
			FATFS.data_start + ((clust_move - 2) * FATFS.cluster_sectors)
					+ of_sec, of_b, buffer, len2) != 0x00) {
	}
	if (!len) {
		return;
	}
	of_sec++;
	of_clus = of_sec / FATFS.cluster_sectors;
	if (of_clus != 0) {
			of_sec = of_sec % FATFS.cluster_sectors;
			SD_read(FATFS.fat_start, (clust_move * 2), &clust_move, 2);
			fixEndian(&clust_move, 2);
	}

	while (SD_read(
			FATFS.data_start + ((clust_move - 2) * FATFS.cluster_sectors)
					+ of_sec, 0, (buffer+len2), len) != 0x00) {
	}
}

/* SD FUNCTIONS*/

/* CACHE FUNCTIONS*/

/* carga linea en cache correspondiente a ID solicitado */
Effect * loadEffect(uint16_t id, uint8_t online) {
	//buscar id de cache libre

	if (status.lastcache > CACHE_SIZE) {
		status.lastcache = 0;
	} else {
		status.lastcache++;
		if (status.lastcache == CACHE_SIZE) {
			status.lastcache = 0;
		}
		if (online && status.ief >= cache[status.lastcache].ief
				&& status.ief
						< (cache[status.lastcache].ief + CACHE_ROW_SIZE)) {
			status.lastcache++;
			if (status.lastcache == CACHE_SIZE) {
				status.lastcache = 0;
			}
		}
	}
	cache[status.lastcache].ief = (id / CACHE_ROW_SIZE) * CACHE_ROW_SIZE;
	FS_read(&(cache[status.lastcache].effects[0]),
			cache[status.lastcache].ief * 9, CACHE_ROW_SIZE * 9);
	return &(cache[status.lastcache].effects[(id - cache[status.lastcache].ief)]);
}
/* busca efecto en cache */
Effect * getEffect(uint16_t id) {
	uint8_t i;
	for (i = 0; i < CACHE_SIZE; i++) {
		if (id >= cache[i].ief && id < (cache[i].ief + CACHE_ROW_SIZE)) {
			return &cache[i].effects[(id - cache[i].ief)];
		}
	}
	return loadEffect(id, 1);
}

/* CACHE FUNCTIONS*/

/* CUBE FUNCTIONS*/
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

ISR(ISR_TIMER) {
	TPM1SC_TOIE = 0;
	ALLOFF
	status.timer_effect++;
	if (status.ef->time == 0x0) {
		status.jmpin = status.ief + status.ef->layer[0];
		status.jmpto = status.ief + 1;
		status.cycles = status.ef->layer[1] - 1;
		status.ief++;
		status.ef = getEffect(status.ief);
	}
	if (status.timer_effect >= status.ef->time) {
		status.timer_effect = 0;
		if (status.cycles == 0 || status.ief != status.jmpin) {
			status.ief++;
			if (status.ief >= status.maxEff) {
				status.ief = 0;
			}	
		} else {
			status.ief = status.jmpto;
			status.cycles--;
		}
		status.ef = getEffect(status.ief);
	}
	switch (status.capa) {
	case 0:
		loadSIPOS(status.ef->layer[0], status.ef->layer[1]);
		CAPA0 = 1;
		break;
	case 1:
		loadSIPOS(status.ef->layer[2], status.ef->layer[3]);
		CAPA1 = 1;
		break;
	case 2:
		loadSIPOS(status.ef->layer[4], status.ef->layer[5]);
		CAPA2 = 1;
		break;
	case 3:
		loadSIPOS(status.ef->layer[6], status.ef->layer[7]);
		CAPA3 = 1;
		break;
	default:
		status.capa = 0;
	}
	status.capa++;
	if (status.capa > 3) {
		status.capa = 0;
	}
	TPM1CNT = 0;
	TPM1SC_TOF = 0;
	TPM1SC_TOIE = 1;
}

/* CUBE FUNCTIONS*/

void main(void) {
	/* Write your local variable definition here */
	uint8_t i, f;
	/*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/
	PE_low_level_init();
	/*** End of Processor Expert internal initialization.                    ***/

	/* Write your code here */
	/* For example: for(;;) { } */

	ALLOFF
	TPM1SC_TOIE = 0;
	status.timer_effect = 0;
	status.ief = 0;
	status.capa = 0;
	status.cycles = 0;
	//load cache

	DELAY
	while (SD_init() != 0x00) {
		DELAY
	}

	FSmount(&FATFS);

	if (FSopen_file("INDEX   CBP", &FATFS)) {
		status.maxEff = FATFS.fsize / 9;
		status.lastcache = CACHE_SIZE + 1;
		for (i = 0; i < CACHE_SIZE; i++) {
			loadEffect(i * CACHE_ROW_SIZE, 0);
		}
	}

	TPM1SC_TOIE = 1;
	i=0;
	for (;;) {
		// precarga de chache
		if(i!=status.ief){
			i = status.ief;
			f=i+1;
			if(f>status.maxEff){
				f=0;
			}
			getEffect(f);
		}
		
	}

	/*** Don't write any code pass this line, or it will be deleted during code generation. ***/
	/*** RTOS startup code. Macro PEX_RTOS_START is defined by the RTOS component. DON'T MODIFY THIS CODE!!! ***/
#ifdef PEX_RTOS_START
	PEX_RTOS_START(); /* Startup of the selected RTOS. Macro is defined by the RTOS component. */
#endif
	/*** End of RTOS startup code.  ***/
	/*** Processor Expert end of main routine. DON'T MODIFY THIS CODE!!! ***/
	for (;;) {
	}
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

