#include "includes.h"
#include "norflash.h"
#include "app_config.h"
#include "asm/clock.h"
#include "system/timer.h"
#include "norflash_config.h"

#undef LOG_TAG_CONST
#define LOG_TAG     "[FLASH]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

/* DEFINE *******************************************************************************************************/
#define FLASH_CACHE_ENABLE      0
#define FLASH_NO_FS             0

// #define FLASH_SPI_INDEX_SEL     2
// #define FLASH_SPI_PIN_CS        IO_PORTB_06

#if	TCFG_HW_SPI2_ENABLE
const struct spi_platform_data spi2_p_data = {
    .port = TCFG_HW_SPI2_PORT,
    .mode = TCFG_HW_SPI2_MODE,
    .clk = TCFG_HW_SPI2_BAUD / 16,
    .role = TCFG_HW_SPI2_ROLE,
};
#endif

const struct norflash_dev_platform_data flash_spi_dev_data = {
    .spi_hw_num     = FLASH_SPI_INDEX_SEL,
    .spi_cs_port    = FLASH_SPI_PIN_CS,
#if (FLASH_SPI_INDEX_SEL == 1)
    .spi_pdata      = &spi1_p_data,
#elif (FLASH_SPI_INDEX_SEL == 2)
    .spi_pdata      = &spi2_p_data,
#endif
    .start_addr     = 0,
    .size           = 512 * 1024,  // 2*1024*1024,
};

/* TYPEDEF ******************************************************************************************************/
struct norflash_info {
    u32 flash_id;
    u32 flash_capacity;
    int spi_num;
    int spi_err;
    u8 spi_cs_io;
    u8 spi_r_width;
    u8 open_cnt;
    OS_MUTEX mutex;
    u32 max_end_addr;
};

/* PARAMS *******************************************************************************************************/
static struct norflash_info _norflash = {
    .spi_num = (int) - 1,
};
static u8 is4byte_mode;

#if FLASH_CACHE_ENABLE
static u32 flash_cache_addr;
static u8 *flash_cache_buf = NULL; //缓存4K的数据，与flash里的数据一样。
static u8 flash_cache_is_dirty;
static u16 flash_cache_timer;

#define FLASH_CACHE_SYNC_T_INTERVAL     60

static int _check_0xff(u8 *buf, u32 len)
{
    for (u32 i = 0; i < len; i ++) {
        if ((*(buf + i)) != 0xff) {
            return 1;
        }
    }
    return 0;
}
#endif

/* DRIVER *******************************************************************************************************/

#define spi_cs_init() \
    do { \
        gpio_set_die(_norflash.spi_cs_io, 1); \
        gpio_set_direction(_norflash.spi_cs_io, 0); \
        gpio_write(_norflash.spi_cs_io, 1); \
    } while (0)

#define spi_cs_uninit() \
    do { \
        gpio_set_die(_norflash.spi_cs_io, 0); \
        gpio_set_direction(_norflash.spi_cs_io, 1); \
        gpio_set_pull_up(_norflash.spi_cs_io, 0); \
        gpio_set_pull_down(_norflash.spi_cs_io, 0); \
    } while (0)
#define spi_cs_h()                  gpio_write(_norflash.spi_cs_io, 1)
#define spi_cs_l()                  gpio_write(_norflash.spi_cs_io, 0)
#define spi_read_byte()             spi_recv_byte(_norflash.spi_num, &_norflash.spi_err)
#define spi_write_byte(x)           spi_send_byte(_norflash.spi_num, x)
#define spi_dma_read(x, y)          spi_dma_recv(_norflash.spi_num, x, y)
#define spi_dma_write(x, y)         spi_dma_send(_norflash.spi_num, x, y)
#define spi_set_width(x)            spi_set_bit_mode(_norflash.spi_num, x)


/* FUNCTION *****************************************************************************************************/
/* func-init */
int _norflash_init(struct norflash_dev_platform_data *pdata)
{
    log_info("norflash_init ! %x %x", pdata->spi_cs_port, pdata->spi_read_width);
    if (_norflash.spi_num == (int) - 1) {
        _norflash.spi_num = pdata->spi_hw_num;
        _norflash.spi_cs_io = pdata->spi_cs_port;
        _norflash.spi_r_width = pdata->spi_read_width;
        _norflash.flash_id = 0;
        _norflash.flash_capacity = 0;
        os_mutex_create(&_norflash.mutex);
        _norflash.max_end_addr = 0;
    }
    ASSERT(_norflash.spi_num == pdata->spi_hw_num);
    ASSERT(_norflash.spi_cs_io == pdata->spi_cs_port);
    ASSERT(_norflash.spi_r_width == pdata->spi_read_width);
    return 0;
}

/* func-open */
static u32 _norflash_read_id()
{
    u8 id[3];
    spi_cs_l();
    spi_write_byte(WINBOND_JEDEC_ID);
    for (u8 i = 0; i < sizeof(id); i++) {
        id[i] = spi_read_byte();
    }
    spi_cs_h();
    return id[0] << 16 | id[1] << 8 | id[2];
}

static u32 _pow(u32 num, int n)
{
    u32 powint = 1;
    int i;
    for (i = 1; i <= n; i++) {
        powint *= num;
    }
    return powint;
}

static void norflash_enter_4byte_addr()
{
    spi_cs_l();
    spi_write_byte(0xb7);
    spi_cs_h();
}

static void _norflash_send_addr(u32 addr)
{
    if (is4byte_mode) {
        spi_write_byte(addr >> 24);
    }
    spi_write_byte(addr >> 16);
    spi_write_byte(addr >> 8);
    spi_write_byte(addr);
}

int _norflash_read(u32 addr, u8 *buf, u32 len, u8 cache)
{
    int reg = 0;
    u32 align_addr;
    os_mutex_pend(&_norflash.mutex, 0);
    /* y_printf("flash read  addr = %d, len = %d\n", addr, len); */
#if FLASH_CACHE_ENABLE
    if (!cache) {
        goto __no_cache1;
    }
    u32 r_len = 4096 - (addr % 4096);
    if ((addr >= flash_cache_addr) && (addr < (flash_cache_addr + 4096))) {
        if (len <= r_len) {
            memcpy(buf, flash_cache_buf + (addr - flash_cache_addr), len);
            goto __exit;
        } else {
            memcpy(buf, flash_cache_buf + (addr - flash_cache_addr), r_len);
            addr += r_len;
            buf += r_len;
            len -= r_len;
        }
    }
__no_cache1:
#endif
    spi_cs_l();
    if (_norflash.spi_r_width == 2) {
        spi_write_byte(WINBOND_FAST_READ_DUAL_OUTPUT);
        _norflash_send_addr(addr);
        spi_write_byte(0);
        spi_set_width(SPI_MODE_UNIDIR_2BIT);
        spi_dma_read(buf, len);
        spi_set_width(SPI_MODE_BIDIR_1BIT);
    } else if (_norflash.spi_r_width == 4) {
        spi_write_byte(0x6b);
        _norflash_send_addr(addr);
        spi_write_byte(0);
        spi_set_width(SPI_MODE_UNIDIR_4BIT);
        spi_dma_read(buf, len);
        spi_set_width(SPI_MODE_BIDIR_1BIT);
    } else {
        spi_write_byte(WINBOND_FAST_READ_DATA);
        _norflash_send_addr(addr);
        spi_write_byte(0);
        spi_dma_read(buf, len);
    }
    spi_cs_h();
__exit:
    os_mutex_post(&_norflash.mutex);
    return reg;
}

int _norflash_open(void *arg)
{
    int reg = 0;
    os_mutex_pend(&_norflash.mutex, 0);
    log_info("norflash open\n");
    if (!_norflash.open_cnt) {
        spi_cs_init();
        spi_open(_norflash.spi_num);
        _norflash.flash_id = _norflash_read_id();
        log_info("norflash_read_id: 0x%x\n", _norflash.flash_id);
        if ((_norflash.flash_id == 0) || (_norflash.flash_id == 0xffffff)) {
            log_error("read norflash id error !\n");
            reg = -ENODEV;
            goto __exit;
        }
        _norflash.flash_capacity = 64 * _pow(2, (_norflash.flash_id & 0xff) - 0x10) * 1024;
        // log_info("norflash_capacity: 0x%x\n", _norflash.flash_capacity);
        log_info("norflash_capacity: %dB, %dKB, %dMB\n", 
            _norflash.flash_capacity, 
            _norflash.flash_capacity / 1024, 
            _norflash.flash_capacity / 1024 / 1024
        );

        is4byte_mode = 0;
        if (_norflash.flash_capacity > 16 * 1024 * 1024) {
            norflash_enter_4byte_addr();
            is4byte_mode = 1;
        }
#if FLASH_CACHE_ENABLE
        flash_cache_buf = (u8 *)malloc(4096);
        ASSERT(flash_cache_buf, "flash_cache_buf is not ok\n");
        flash_cache_addr = 4096;//先给一个大于4096的数
        _norflash_read(0, flash_cache_buf, 4096, 1);
        flash_cache_addr = 0;
#endif
        log_info("norflash open success !\n");
    }
    if (_norflash.flash_id == 0 || _norflash.flash_id == 0xffffff)  {
        log_error("re-open norflash id error !\n");
        reg = -EFAULT;
        goto __exit;
    }
    ASSERT(_norflash.max_end_addr <= _norflash.flash_capacity, "max partition end address is greater than flash capacity\n");
    _norflash.open_cnt++;

__exit:
    os_mutex_post(&_norflash.mutex);
    return reg;
}

/* func-close */
static void _norflash_send_write_enable()
{
    spi_cs_l();
    spi_write_byte(WINBOND_WRITE_ENABLE);
    spi_cs_h();
}

static int _norflash_wait_ok()
{
    u32 timeout = 8 * 1000 * 1000 / 100;
    // while (timeout--)
    while (1)
    {
        wdt_clear();
        spi_cs_l();
        spi_write_byte(WINBOND_READ_SR1);
        u8 reg_1 = spi_read_byte();
        spi_cs_h();
        if (!(reg_1 & BIT(0))) {
            break;
        }
        delay(100);
    }
    if (timeout == 0) {
        log_error("norflash_wait_ok timeout!\r\n");
        return 1;
    }
    return 0;
}

int _norflash_eraser(u8 eraser, u32 addr)
{
    u8 eraser_cmd;
    switch (eraser) {
    case FLASH_PAGE_ERASER:
        eraser_cmd = WINBOND_PAGE_ERASE;
        addr = addr / 256 * 256;
        break;
    case FLASH_SECTOR_ERASER:
        eraser_cmd = WINBOND_SECTOR_ERASE;
        //r_printf(">>>[test]:addr = %d\n", addr);
        addr = addr / 4096 * 4096;
        break;
    case FLASH_BLOCK_ERASER:
        eraser_cmd = WINBOND_BLOCK_ERASE;
        addr = addr / 65536 * 65536;
        break;
    case FLASH_CHIP_ERASER:
        eraser_cmd = WINBOND_CHIP_ERASE;
        break;
    }
    _norflash_send_write_enable();
    spi_cs_l();
    spi_write_byte(eraser_cmd);
    if (eraser_cmd != WINBOND_CHIP_ERASE) {
        _norflash_send_addr(addr);
    }
    spi_cs_h();
    return _norflash_wait_ok();
}

static int _norflash_write_pages(u32 addr, u8 *buf, u32 len)
{
    /* y_printf("flash write addr = %d, num = %d\n", addr, len); */

    int reg;
    u32 first_page_len = 256 - (addr % 256);
    first_page_len = len > first_page_len ? first_page_len : len;
    _norflash_send_write_enable();
    spi_cs_l();
    spi_write_byte(WINBOND_PAGE_PROGRAM);
    _norflash_send_addr(addr) ;
    spi_dma_write(buf, first_page_len);
    spi_cs_h();
    reg = _norflash_wait_ok();
    if (reg) {
        return 1;
    }
    addr += first_page_len;
    buf += first_page_len;
    len -= first_page_len;
    while (len) {
        u32 cnt = len > 256 ? 256 : len;
        _norflash_send_write_enable();
        spi_cs_l();
        spi_write_byte(WINBOND_PAGE_PROGRAM);
        _norflash_send_addr(addr) ;
        spi_dma_write(buf, cnt);
        spi_cs_h();
        reg = _norflash_wait_ok();
        if (reg) {
            return 1;
        }
        addr += cnt;
        buf += cnt;
        len -= cnt;
    }
    return 0;
}

static int _norflash_erase_and_write_pages(u32 addr, u8 *buf, u32 len)
{
    /* y_printf("flash write addr = %d, num = %d\n", addr, len); */

    int reg;
    u32 first_page_len = 256 - (addr % 256);
    first_page_len = len > first_page_len ? first_page_len : len;
    _norflash_eraser(FLASH_PAGE_ERASER, addr);
    _norflash_send_write_enable();
    spi_cs_l();
    spi_write_byte(WINBOND_PAGE_PROGRAM);
    _norflash_send_addr(addr) ;
    spi_dma_write(buf, first_page_len);
    spi_cs_h();
    reg = _norflash_wait_ok();
    if (reg) {
        return 1;
    }
    addr += first_page_len;
    buf += first_page_len;
    len -= first_page_len;
    while (len) {
        u32 cnt = len > 256 ? 256 : len;
        _norflash_eraser(FLASH_PAGE_ERASER, addr);
        _norflash_send_write_enable();
        spi_cs_l();
        spi_write_byte(WINBOND_PAGE_PROGRAM);
        _norflash_send_addr(addr) ;
        spi_dma_write(buf, cnt);
        spi_cs_h();
        reg = _norflash_wait_ok();
        if (reg) {
            return 1;
        }
        addr += cnt;
        buf += cnt;
        len -= cnt;
    }
    return 0;
}

int _norflash_close(void)
{
    os_mutex_pend(&_norflash.mutex, 0);
    log_info("norflash close\n");
    if (_norflash.open_cnt) {
        _norflash.open_cnt--;
    }
    if (!_norflash.open_cnt) {
#if FLASH_CACHE_ENABLE
        if (flash_cache_is_dirty) {
            flash_cache_is_dirty = 0;
            _norflash_eraser(FLASH_SECTOR_ERASER, flash_cache_addr);
            _norflash_write_pages(flash_cache_addr, flash_cache_buf, 4096);
        }
        free(flash_cache_buf);
        flash_cache_buf = NULL;
#endif
        spi_close(_norflash.spi_num);
        spi_cs_uninit();

        log_info("norflash close done\n");
    }
    os_mutex_post(&_norflash.mutex);
    return 0;
}

/* func-write */
#if FLASH_CACHE_ENABLE
static void _norflash_cache_sync_timer(void *priv)
{
    int reg = 0;
    os_mutex_pend(&_norflash.mutex, 0);
    if (flash_cache_is_dirty) {
        flash_cache_is_dirty = 0;
        reg = _norflash_eraser(FLASH_SECTOR_ERASER, flash_cache_addr);
        if (reg) {
            goto __exit;
        }
        reg = _norflash_write_pages(flash_cache_addr, flash_cache_buf, 4096);
    }
    if (flash_cache_timer) {
        sys_timeout_del(flash_cache_timer);
        flash_cache_timer = 0;
    }
__exit:
    os_mutex_post(&_norflash.mutex);
}
#endif

int _norflash_write(u32 addr, void *buf, u32 len, u8 cache)
{
    int reg = 0;
    os_mutex_pend(&_norflash.mutex, 0);

    u8 *w_buf = (u8 *)buf;
    u32 w_len = len;

    /* y_printf("flash write addr = %d, num = %d\n", addr, len); */
#if FLASH_CACHE_ENABLE
    if (!cache) {
        reg = _norflash_write_pages(addr, w_buf, w_len);
        goto __exit;
    }
    u32 align_addr = addr / 4096 * 4096;
    u32 align_len = 4096 - (addr - align_addr);
    align_len = w_len > align_len ? align_len : w_len;
    if (align_addr != flash_cache_addr) {
        if (flash_cache_is_dirty) {
            flash_cache_is_dirty = 0;
            reg = _norflash_eraser(FLASH_SECTOR_ERASER, flash_cache_addr);
            if (reg) {
                goto __exit;
            }
            reg = _norflash_write_pages(flash_cache_addr, flash_cache_buf, 4096);
            if (reg) {
                goto __exit;
            }
        }
        _norflash_read(align_addr, flash_cache_buf, 4096, 0);
        flash_cache_addr = align_addr;
    }
    memcpy(flash_cache_buf + (addr - align_addr), w_buf, align_len);
    if ((addr + align_len) % 4096) {
        flash_cache_is_dirty = 1;
        if (flash_cache_timer) {
            sys_timer_re_run(flash_cache_timer);
        } else {
            flash_cache_timer = sys_timeout_add(0, _norflash_cache_sync_timer, FLASH_CACHE_SYNC_T_INTERVAL);
        }
    } else {
        flash_cache_is_dirty = 0;
        reg = _norflash_eraser(FLASH_SECTOR_ERASER, align_addr);
        if (reg) {
            goto __exit;
        }
        reg = _norflash_write_pages(align_addr, flash_cache_buf, 4096);
        if (reg) {
            goto __exit;
        }
    }
    addr += align_len;
    w_buf += align_len;
    w_len -= align_len;
    while (w_len) {
        u32 cnt = w_len > 4096 ? 4096 : w_len;
        _norflash_read(addr, flash_cache_buf, 4096, 0);
        flash_cache_addr = addr;
        memcpy(flash_cache_buf, w_buf, cnt);
        if ((addr + cnt) % 4096) {
            flash_cache_is_dirty = 1;
            if (flash_cache_timer) {
                sys_timer_re_run(flash_cache_timer);
            } else {
                flash_cache_timer = sys_timeout_add(0, _norflash_cache_sync_timer, FLASH_CACHE_SYNC_T_INTERVAL);
            }
        } else {
            flash_cache_is_dirty = 0;
            reg = _norflash_eraser(FLASH_SECTOR_ERASER, addr);
            if (reg) {
                goto __exit;
            }
            reg = _norflash_write_pages(addr, flash_cache_buf, 4096);
            if (reg) {
                goto __exit;
            }
        }
        addr += cnt;
        w_buf += cnt;
        w_len -= cnt;
    }
#else
    reg = _norflash_write_pages(addr, w_buf, w_len);
    // reg = _norflash_erase_and_write_pages(addr, w_buf, w_len);
#endif
__exit:
    os_mutex_post(&_norflash.mutex);
    return reg;
}

/* func-ioctl */
#if !FLASH_NO_FS
int _norflash_ioctl(u32 cmd, u32 arg, u32 unit)
{
    int reg = 0;
    os_mutex_pend(&_norflash.mutex, 0);
    switch (cmd) {
    case IOCTL_GET_STATUS:
        *(u32 *)arg = 1;
        break;
    case IOCTL_GET_ID:
        *((u32 *)arg) = _norflash.flash_id;
        break;
    case IOCTL_GET_CAPACITY:
        if (_norflash.flash_capacity == 0)  {
            *(u32 *)arg = 0;
        } else {
            *(u32 *)arg = _norflash.flash_capacity / unit;
        }
        break;
    case IOCTL_GET_BLOCK_SIZE:
        *(u32 *)arg = 512;
        break;
    case IOCTL_ERASE_PAGE:
        reg = _norflash_eraser(FLASH_PAGE_ERASER, arg * unit);
        break;
    case IOCTL_ERASE_SECTOR:
        reg = _norflash_eraser(FLASH_SECTOR_ERASER, arg * unit);
        break;
    case IOCTL_ERASE_BLOCK:
        reg = _norflash_eraser(FLASH_BLOCK_ERASER, arg * unit);
        break;
    case IOCTL_ERASE_CHIP:
        reg = _norflash_eraser(FLASH_CHIP_ERASER, 0);
        break;
    case IOCTL_FLUSH:
#if FLASH_CACHE_ENABLE
        if (flash_cache_is_dirty) {
            flash_cache_is_dirty = 0;
            reg = _norflash_eraser(FLASH_SECTOR_ERASER, flash_cache_addr);
            if (reg) {
                goto __exit;
            }
            reg = _norflash_write_pages(flash_cache_addr, flash_cache_buf, 4096);
        }
#endif
        break;
    case IOCTL_CMD_RESUME:
        break;
    case IOCTL_CMD_SUSPEND:
        break;
    case IOCTL_GET_PART_INFO: {
        u32 *info = (u32 *)arg;
        u32 *start_addr = &info[0];
        u32 *part_size = &info[1];
        *start_addr = 0;
        *part_size = _norflash.flash_capacity;
    }
    break;
    default:
        reg = -EINVAL;
        break;
    }
__exit:
    os_mutex_post(&_norflash.mutex);
    return reg;
}
#endif

static int norfs_dev_init(void *arg)
{
    struct norflash_dev_platform_data *pdata = arg;
    return _norflash_init(pdata);
}

static int norfs_dev_open(void *arg)
{
    return _norflash_open(arg);
}

static int norfs_dev_close(void)
{
    return _norflash_close();
}

static int norfs_dev_read(void *buf, u32 len, u32 offset)
{
    int reg;
    /* printf("flash read sector = %d, num = %d\n", offset, len); */
    reg = _norflash_read(offset, buf, len, FLASH_CACHE_ENABLE);
    if (reg) {
        printf(">>>[r error]:\n");
        len = 0;
    }

    return len;
}

static int norfs_dev_write(void *buf, u32 len, u32 offset)
{
    /* printf("flash write sector = %d, num = %d\n", offset, len); */
    int reg = 0;
    reg = _norflash_write(offset, buf, len, FLASH_CACHE_ENABLE);
    if (reg) {
        printf(">>>[w error]:\n");
        len = 0;
    }
    return len;
}

static bool norfs_dev_online(void)
{
    return 1;
}

#if !FLASH_NO_FS
static int norfs_dev_ioctl(u32 cmd, u32 arg)
{
    return _norflash_ioctl(cmd, arg, 1);
}
#endif


struct flash_spi_operations {
    bool (*online)(void);                           ///<设备在线状态查询
    int (*init)(void *arg);                         ///<设备初始化
    int (*open)(void *arg);                         ///<设备开启
    int (*read)(void *buf, u32 len, u32 offset);    ///<读操作
    int (*write)(void *buf, u32 len, u32 offset);   ///<写操作
#if !FLASH_NO_FS
    int (*seek)(u32 offset, int orig);              ///<设备搜索
    int (*ioctl)(u32 cmd, u32 arg);                 ///<I/O控制
#endif
    int (*close)(void);                             ///<设备关闭
};

const struct flash_spi_operations flash_spi_ops = {
    .online = norfs_dev_online,
    .init = norfs_dev_init,
    .open = norfs_dev_open,
    .read = norfs_dev_read,
    .write = norfs_dev_write,
#if !FLASH_NO_FS
    .ioctl = norfs_dev_ioctl,
#endif
    .close = norfs_dev_close,
};

/* APP *****************************************************************************************************/

static u32 w_offset_addr = 0;
static u32 r_offset_addr = 0;
static u32 c_offset_addr = 0;
static u8 cache_buff[512] __attribute__((aligned(4)));

void flash_spi_init(void)
{
    int ret = 0;
    flash_spi_ops.init(&flash_spi_dev_data);
    flash_spi_ops.open(&flash_spi_dev_data);

    // ret = flash_spi_ops.ioctl(IOCTL_ERASE_CHIP, 0);
    // printf("erase chip:%d\n", ret);

    // flash_spi_ops.close();
}

int flash_spi_write(u8 *buffer, u32 len)
{
#if 0
    int wlen = 0;
    // memcpy(cache_buff, buffer, len);

    // wlen = flash_spi_ops.write(cache_buff, len, w_offset_addr);
    wlen = flash_spi_ops.write(buffer, len, w_offset_addr);
    if (wlen != 0)
        w_offset_addr += len;
    return wlen;
#else
#define WRITE_SIZE (256)
    u32 check_len = 0;
    int wlen = 0;
    for (u32 offset = w_offset_addr; offset < w_offset_addr + len; offset += WRITE_SIZE)
    {
        if ((offset + WRITE_SIZE) < (w_offset_addr + len))
            check_len = WRITE_SIZE;
        else
            check_len = w_offset_addr + len - offset;

        if (check_len == 0)
        {
            putchar('=');
            break;
        }

        memcpy(cache_buff, buffer + offset - w_offset_addr, check_len);
        wlen = flash_spi_ops.write(cache_buff, check_len, offset);
        if (wlen == 0)
        {
            putchar(')');
            break;
        }
        // else if (w_offset_addr == 0 && w_offset_addr == offset)
        // {
        //     // printf("write:%d\n", wlen);
        //     flash_spi_ops.read(cache_buff, check_len, offset);
        //     // for (u32 index = 0; index < check_len; index++)
        //     // {
        //     //     printf("%02X:%02X\n", cache_buff[index], *(buffer + offset - w_offset_addr + index));
        //     // }
        // }
    }
    if (wlen != 0)
        w_offset_addr += len;
    return len;
#endif
}

void flash_spi_read(u8 *buffer, u32 len, u32 offset, u8 start)
{
    if (buffer == NULL || len == 0)
        return;

    // flash_spi_ops.read(cache_buff, len, offset);
    flash_spi_ops.read(buffer, len, offset);

    // memcpy(buffer, cache_buff, len);
}

int flash_spi_erase_chip(void)
{
    int ret = flash_spi_ops.ioctl(IOCTL_ERASE_CHIP, 0);
    printf("erase chip:%d\n", ret);
    w_offset_addr = 0;

    return ret;
}

int flash_spi_check_data(u8 *buffer, u32 len, u8 start)
{
#define CHECK_SIZE (256)
    u32 check_len = 0;

    if (start)
        c_offset_addr = 0;
    if (buffer == NULL || len == 0)
    {
        putchar('(');
        return -1;
    }

    for (u32 offset = c_offset_addr; offset < c_offset_addr + len; offset += CHECK_SIZE)
    {
        if ((offset + CHECK_SIZE) < (c_offset_addr + len))
            check_len = CHECK_SIZE;
        else
            check_len = c_offset_addr + len - offset;

        if (check_len == 0)
        {
            putchar('=');
            return 1;
        }

        memset(cache_buff, 0x00, check_len);
        flash_spi_ops.read(cache_buff, check_len, offset);
        if (memcmp(cache_buff, buffer + offset - c_offset_addr, check_len))
        {
            for (u32 index = 0; index < check_len; index++)
            {
                printf("%02X:%02X\n", cache_buff[index], *(buffer + offset - c_offset_addr + index));
            }
            putchar(')');
            return 1;
        }
    }
    c_offset_addr += len;
    return 0;
}
