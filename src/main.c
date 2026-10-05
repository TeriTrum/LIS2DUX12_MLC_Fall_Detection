#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/debug/thread_analyzer.h>

#include "lis2dux12_init.h"

/* ------------------------------------------------------------------ */
int main(void)
{
    lis2dux12_init();

    while (1)
    {
        if (k_sem_take(&mlc_sync_sem, K_FOREVER) == 0)
        {
            lis2dux12_mlc_out();
        }
    }
}