#ifndef ADXL345_H
#define ADXL345_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/interrupt.h>


// REGISTERS
#define BW_RATE 0x2C
#define INT_ENABLE 0x2E
#define DATA_FORMAT 0x31
#define FIFO_CTL 0x38
#define POWER_CTL 0x2D
#define DATAX1 0x33
#define DATAX0 0x32
#define DATAY0 0x34
#define DATAY1 0x35
#define DATAZ0 0x36
#define DATAZ1 0x37

#define RATE_CODE_3200 0x0F
#define RATE_CODE_1600 0x0E
#define RATE_CODE_0800 0x0D
#define RATE_CODE_0400 0x0C
#define RATE_CODE_0200 0x0B
#define RATE_CODE_0100 0x0A

#define RWR_VALUE _IOWR(10, 2, char) // (type,nr,size) - The default major number of all the misc drivers is 10.
#define X_IOCTL 0
#define Y_IOCTL 1
#define Z_IOCTL 2

// FUNCTIONS
ssize_t adxl345_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos);
static long adxl345_write_read_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
irqreturn_t adxl345_int(int irq, void *dev_id);
static int write_reg(struct i2c_client *client, char *addr, char *buf);
static int read_reg(struct i2c_client *client, char addr, char *buf, int len);


/*
 * -------------------------------------------------------------------------
 * 
 * Structures
 * 
 * -------------------------------------------------------------------------
*/


struct file_operations adxl345_fops = {
    .owner = THIS_MODULE,
    .read = adxl345_read,
    .unlocked_ioctl = adxl345_write_read_ioctl,
};


/**
 * 
 * @brief Structure for the FIFO element
 *
**/
struct fifo_element { 
    // One element of the FIFO, in this case a sample of the accelerometer
    short x;
    short y;
    short z;
};


/**
 * 
 * @brief Structure for the device
 * 
**/
struct adxl345_device {
    struct miscdevice miscdev;
    char addr[2];
    // Create a waiting list
    wait_queue_head_t queue;
    // Create a FIFO, name=samples_fifo, elements=fifo_element, size=64
    DECLARE_KFIFO(samples_fifo, struct fifo_element, 64);
};


/**
 * 
 * @brief Structure for the ioctl data
 * 
**/
struct ioctl_data
{
    char write_data[2];
    char read_data[2];
};




/*
 * -------------------------------------------------------------------------
 * 
 * GLOBAL VARIABLES
 * 
 * -------------------------------------------------------------------------
*/
char adxl_count = 0;




#endif


