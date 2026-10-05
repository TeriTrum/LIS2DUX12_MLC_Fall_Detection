#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "lis2dux12_reg.h"
#include "mlc_adl_fall.h"
#include "lis2dux12_init.h"

LOG_MODULE_REGISTER(ACCE_INIT);

#define I2C_DEV_NODE DT_NODELABEL(i2c24)
#define SENSOR_NODE DT_NODELABEL(lis2dux12)
#define INT2_PIN 23

static const struct i2c_dt_spec sensor_i2c = I2C_DT_SPEC_GET(SENSOR_NODE);
static const uint8_t prev_mlc_out = 4U; // Trang thai truoc khi so sanh luon la ADL

static stmdev_ctx_t dev_ctx;
static lis2dux12_priv_t dev_priv;
static uint8_t whoamI;

// Callback cho chan ACCE_INT2
static struct gpio_callback lis2dux12_int_cb_data;

// Khai báo Semaphore
K_SEM_DEFINE(mlc_sync_sem, 0, 1);

static void lis2dux12_int_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    k_sem_give(&mlc_sync_sem);
}

static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return i2c_burst_write_dt((const struct i2c_dt_spec *)handle, reg, buf, len);
}

static int32_t platform_read(void *handle, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return i2c_burst_read_dt((const struct i2c_dt_spec *)handle, reg, buf, len);
}

static void platform_delay(uint32_t ms)
{
    k_msleep(ms);
}

static int load_mlc_config(void)
{
    LOG_INF("Dang nap cau hinh AI vao cam bien...");

    for (int i = 0; i < MLC_CONFIG_SIZE; i++)
    {
        uint8_t data = mlc_adl_fall_config[i].data;

        int ret = lis2dux12_write_reg(&dev_ctx, mlc_adl_fall_config[i].address, &data, 1);

        if (ret != 0)
        {
            LOG_ERR("Lenh %d: ghi thanh ghi 0x%02X that bai (ret=%d)", i, (unsigned int)mlc_adl_fall_config[i].address, ret);
            return ret; // Dung nap khi co loi
        }

        if (i == 0)
        {
            k_msleep(5); // WAIT 5ms sau lenh ghi dau tien
        }
    }
    LOG_INF("Da ghi xong cau hinh MLC.");
    return 0;
}

void lis2dux12_init(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);
    if (!device_is_ready(i2c_dev))
    {
        LOG_ERR("Bus I2C chua san sang!\n");
        return;
    }

    dev_ctx.write_reg = platform_write;
    dev_ctx.read_reg = platform_read;
    dev_ctx.mdelay = platform_delay;
    dev_ctx.handle = (void *)&sensor_i2c;
    dev_ctx.priv_data = &dev_priv;

    lis2dux12_exit_deep_power_down(&dev_ctx);

    // Sensor ID Check
    lis2dux12_device_id_get(&dev_ctx, &whoamI);
    if (whoamI != LIS2DUX12_ID)
    {
        LOG_ERR("Error: Sensor ID mismatch\n");
        // while (1);
    }
    LOG_INF("LIS2DUX12 ID OK: 0x47\n");

    lis2dux12_sw_reset(&dev_ctx);
    k_msleep(10);
    lis2dux12_init_set(&dev_ctx);

    // Nap cau hinh MLC vao cam bien
    LOG_INF("Dang nap cau hinh AI vao cam bien...\n");
    int32_t ret = load_mlc_config();

    if (ret != 0)
    {
        // Dung khoi tao MLC
        LOG_ERR("Nap cau hinh MLC that bai.");
    }
    else
    {
        // Khoi tao MLC thanh cong
        LOG_INF("San sang doc ket qua MLC.");
    }

    // Cau hinh chan va callback cho chan ACCE_INT2
    const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (device_is_ready(gpio1_dev))
    {
        gpio_pin_configure(gpio1_dev, INT2_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
        gpio_pin_interrupt_configure(gpio1_dev, INT2_PIN, GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&lis2dux12_int_cb_data, lis2dux12_int_handler, BIT(INT2_PIN));
        gpio_add_callback(gpio1_dev, &lis2dux12_int_cb_data);
    }

    /* Thanh ghi cua lis2dux12 chia thanh 2 bank (main bank va embedded func bank)
     * Cung la dia chi 0x11 nhung co 2 thanh ghi la CTRL2 va MLC_INT2
     * Muon doc ghi thanh ghi MLC_INT2 thi phai chuyen sang embedded func bank
     * neu khong se nham voi CTRL2
     */
    lis2dux12_mlc_int2_t mlc_int2 = {0};

    // Chuyen sang bank nhung
    int32_t ret = lis2dux12_mem_bank_set(&dev_ctx, LIS2DUX12_EMBED_FUNC_MEM_BANK);

    // Cau hinh MLC_INT2
    if (ret == 0)
    {
        ret = lis2dux12_read_reg(&dev_ctx, LIS2DUX12_MLC_INT2, (uint8_t *)&mlc_int2, 1);
    }

    if (ret == 0)
    {
        mlc_int2.int2_mlc1 = 1;

        ret = lis2dux12_write_reg(&dev_ctx, LIS2DUX12_MLC_INT2, (uint8_t *)&mlc_int2, 1);
    }

    // Chuyen ve bank main
    lis2dux12_mem_bank_set(&dev_ctx, LIS2DUX12_MAIN_MEM_BANK);

    // Dinh tuyen Embedded Functions ra chan INT2
    lis2dux12_pin_int_route_t pin_int2_route;
    lis2dux12_pin_int2_route_get(&dev_ctx, &pin_int2_route);
    pin_int2_route.emb_function = 1;
    lis2dux12_pin_int2_route_set(&dev_ctx, &pin_int2_route);
}

void lis2dux12_mlc_out(void)
{
    // Reset thanh ghi trang thai de chuan bi lan doc tiep theo
    lis2dux12_all_sources_t all_src;
    lis2dux12_all_sources_get(&dev_ctx, &all_src);

    lis2dux12_mlc_status_mainpage_t status;

    // Ham mlc_out ghi 4 byte vao dia chi duoc chuyen vao nen phai khai bao mang 4 phan tu
    uint8_t mlc_out[4] = {0};

    // Chi doc khi co trang thai moi
    lis2dux12_mlc_status_get(&dev_ctx, &status);

    if (status.is_mlc1)
    {
        lis2dux12_mlc_out_get(&dev_ctx, mlc_out);

        switch (*mlc_out)
        {
        case 0:
            LOG_INF("Fall\n");
            break;
        case 4:
            LOG_INF("ADL\n");
            break;
        }
    }

    // Check dieu kien nga
    if (prev_mlc_out == 4U && *mlc_out == 0U)
    {
        LOG_INF("Canh Bao: Phat hien nga !!\n");
    }
}