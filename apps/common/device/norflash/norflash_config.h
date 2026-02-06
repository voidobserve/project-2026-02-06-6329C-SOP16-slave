#ifndef _NORFLASH_CONFIG_H_
#define _NORFLASH_CONFIG_H_

void flash_spi_init(void);
int flash_spi_write(u8 *buffer, u32 len);
void flash_spi_read(u8 *buffer, u32 len, u32 offset, u8 start);
int flash_spi_erase_chip(void);
int flash_spi_check_data(u8 *buffer, u32 len, u8 start);

#endif
