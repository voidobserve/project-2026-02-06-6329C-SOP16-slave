#include "app_config.h"
#include "includes.h"
#include "system/includes.h"
#include "asm/includes.h"
#include "asm/cpu.h"
#include "asm/clock.h"
#include "system/timer.h"

#define LOG_TAG_CONST       EXIF3
#define LOG_TAG             "[EXIF3]"
#include "debug.h"
#define LOG_v(t)  log_tag_const_v_ ## t
#define LOG_i(t)  log_tag_const_i_ ## t
#define LOG_d(t)  log_tag_const_d_ ## t
#define LOG_w(t)  log_tag_const_w_ ## t
#define LOG_e(t)  log_tag_const_e_ ## t
#define LOG_c(t)  log_tag_const_c_ ## t
#define LOG_tag(tag, n) n(tag)
const char LOG_tag(LOG_TAG_CONST,LOG_v) AT(.LOG_TAG_CONST) = 0;
const char LOG_tag(LOG_TAG_CONST,LOG_i) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_d) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_w) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_e) AT(.LOG_TAG_CONST) = 1;
const char LOG_tag(LOG_TAG_CONST,LOG_c) AT(.LOG_TAG_CONST) = 1;

#define USER_FILE_NAME              SDFILE_APP_ROOT_PATH"EXIF3"
#define USER_FILE_START_ADDR        0x76000
#define USER_FILE_END_ADDR          0x7E000

extern bool sfc_erase(FLASH_ERASER cmd, u32 addr);
extern u32 sdfile_cpu_addr2flash_addr(u32 offset);

///自定义flash区域空间
//  #按照芯片flash大小，最后4K保留，往上取地址
//  #USERIF_ADR=0x50000;
//  #USERIF_ADR=AUTO;
//  USERIF_LEN=32K;
//  USERIF_OPT=1;
///isd_config.ini 配置的地址要对齐
///4K对齐才能SECTOR_ERASER
///64K对齐才能BLOCK_ERASER

typedef enum _FLASH_ERASER {
    CHIP_ERASER,
    BLOCK_ERASER, //64k
    SECTOR_ERASER,//4k
    PAGE_ERASER,  //256B
} FLASH_ERASER;

static FILE *exif_flash_fp = NULL;
static u8 exif_need_erase = 1;

void exif_flash_init(void)
{
    const char *path = USER_FILE_NAME;
    log_info("%s", path);

    struct vfs_attr attr = { 0 };

    exif_flash_fp = fopen(path, "r+w");
    if (exif_flash_fp == NULL)
    {
        log_info("%s[%s]", __func__, "open fail!!!");
        return;
    }
    fget_attrs(exif_flash_fp, &attr);
    u32 flash_addr = sdfile_cpu_addr2flash_addr(attr.sclust);
    log_info("%s[cpu_addr   :0x%x,fsize:%dK]", __func__, attr.sclust, attr.fsize / 1024);
    log_info("%s[flash_addr :0x%x,fsize:%dK]", __func__, flash_addr, attr.fsize / 1024);
}

void exif_Flash_Read(void *buf, u32 addr, uint len)
{
    int ret = 0;
    u32 uiAddr = addr - USER_FILE_START_ADDR;

    fseek(exif_flash_fp, uiAddr, SEEK_SET);
    ret = fread(exif_flash_fp, buf, len);
}

u8 exif_Flash_Write(void *buf, u32 addr, uint len)
{
    if ((addr < USER_FILE_START_ADDR) || (addr >= USER_FILE_END_ADDR))
        return 1;
    u32 uiAddr = addr - USER_FILE_START_ADDR;
    int ret = 0;

    fseek(exif_flash_fp, uiAddr, SEEK_SET);
    ret = fwrite(exif_flash_fp, buf, len);
    return 0;
}

u8 exif_Flash_Erase_4k(u32 addr)
{
    if ((addr < USER_FILE_START_ADDR) || (addr >= USER_FILE_END_ADDR))
        return 1;
    u32 uiAddr = addr - USER_FILE_START_ADDR;
    fseek(exif_flash_fp, uiAddr, SEEK_SET);
    sfc_erase(SECTOR_ERASER, addr);
    return 0;
}

uint8_t exif_uart_flash_write(uint32_t uiAddr, uint8_t *_pAlgBuf, uint32_t uiLen)
{
    uint8_t NumOfPage = 0, NumOfSingle = 0, i = 0;
    uint16_t j;
    uint8_t ucTmpBuf[256];
    uint32_t uiPageSize = 256;

    NumOfPage = uiLen / uiPageSize;
    NumOfSingle = uiLen % uiPageSize;

    if (exif_need_erase == 1)
    {
        exif_Flash_Erase_4k(uiAddr);
    }

    if (NumOfPage == 0)
    {
        exif_Flash_Write(_pAlgBuf, uiAddr, uiLen);

        exif_Flash_Read(ucTmpBuf, uiAddr, uiLen);
        for (j = 0; j < uiLen; j++)
        {
            if (ucTmpBuf[j] != _pAlgBuf[j])
            {
                return 1;
            }
        }
    }
    else
    {
        for (i = 0; i < NumOfPage; i++)
        {
            exif_Flash_Write((uint8_t *)&_pAlgBuf[i * uiPageSize], uiAddr, uiPageSize);

            exif_Flash_Read(ucTmpBuf, uiAddr, uiPageSize);

            for (j = 0; j < uiPageSize; j++)
            {
                if (ucTmpBuf[j] != _pAlgBuf[i * uiPageSize + j])
                {
                    return 1;
                }
            }
            uiAddr += uiPageSize;
        }

        if (NumOfSingle)
        {
            exif_Flash_Write((uint8_t *)&_pAlgBuf[NumOfPage * uiPageSize], uiAddr, uiPageSize);

            exif_Flash_Read(ucTmpBuf, uiAddr, uiPageSize);

            for (j = 0; j < NumOfSingle; j++)
            {
                if (ucTmpBuf[j] != _pAlgBuf[NumOfPage * uiPageSize + j])
                {
                    return 1;
                }
            }
        }
    }

    return 0;
}

void exif_uart_flash_read(uint32_t addr, uint8_t *data, uint32_t uisize)
{
    exif_Flash_Read(data, addr, uisize);
}

