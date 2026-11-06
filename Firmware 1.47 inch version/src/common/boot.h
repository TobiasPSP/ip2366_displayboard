#ifndef BOOT_H
#define BOOT_H

#include <stdbool.h>
#include <stdint.h>

bool boot_is_app_valid(void);
void boot_jump_to_app(void);

#endif
