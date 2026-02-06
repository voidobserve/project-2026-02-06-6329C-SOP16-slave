// binary representation
// attribute size in bytes (16), flags(16), handle (16), uuid (16/128), value(...)

#ifndef _BLE_TRANS_H
#define _BLE_TRANS_H

#include <stdint.h>
#include "app_config.h"
#include "gatt_common/le_gatt_common.h"

extern const gatt_client_cfg_t trans_client_init_cfg;
void trans_client_init(void);
void trans_client_exit(void);
int trans_client_search_remote_profile(u16 conn_handle);
int trans_client_search_remote_stop(u16 conn_handle);

void trans_ios_services_init(void);
void trans_ios_services_exit(void);


void rx_update_file_init(void);
void rx_update_file_clear(void);
void rx_update_file_write(u8 *buf, u32 len);
u32 rx_update_file_get_size(void);
u32 rx_update_file_read(u8 *buf, u32 len, u8 full);

void rx_command_init(void);
void rx_command_clear(void);
void rx_command_write(u8 *buf, u32 len);
u32 rx_command_get_size(void);
u32 rx_command_read(u8 *buf, u32 len, u8 full);

#endif
