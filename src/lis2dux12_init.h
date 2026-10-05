#ifndef MLC_FALL_H
#define MLC_FALL_H

#include <stdint.h>

extern struct k_sem mlc_sync_sem;

/* Khoi tao cam bien + nap cau hinh AI (MLC). Khong cau hinh ngat GPIO. */
void lis2dux12_init(void);

/* Doc trang thai MLC 1 lan va tu kiem tra chuyen trang thai.
 * Goi ham nay LAP LAI (vd trong vong lap while cua main, cach nhau
 * mot khoang k_msleep) de MLC "chay ngam" lien tuc. */
void lis2dux12_mlc_out(void);

#endif /* MLC_FALL_H */