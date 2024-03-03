#include "adxl345.h"


static int adxl345_probe(struct i2c_client *client,
    const struct i2c_device_id *id)
{
    char addr = 0x00;
    char buf = 0;
    struct adxl345_device *adxl345dev;

    // Display a message when a supported device is detected
    pr_info("adxl345: detected device %s\n", client->name);

    // Allocate memory for the device structure
    adxl345dev = devm_kzalloc(&client->dev, sizeof(struct adxl345_device), GFP_KERNEL);
    if (!adxl345dev) return -ENOMEM;

    // Associate this instance with the struct i2c_client
    i2c_set_clientdata(client, adxl345dev);

    // Initialize the misc device structure
    adxl345dev->miscdev.name = kasprintf(GFP_KERNEL, "adxl345-%d", adxl_count++);
    adxl345dev->miscdev.fops =  &adxl345_fops;
    adxl345dev->miscdev.parent = &client->dev;
    adxl345dev->miscdev.minor = MISC_DYNAMIC_MINOR;

    // Register the misc device
    if (misc_register(&adxl345dev->miscdev) != 0) {
        pr_warn("adxl345: misc_register failed\n");
        goto err_misc_register;
    }


    // Register the interrupt handler bottom half
    if (    devm_request_threaded_irq(adxl345dev->miscdev.parent,
                                      client->irq, NULL, adxl345_int,
                                      IRQF_ONESHOT, adxl345dev->miscdev.name , (void*)adxl345dev)) {
        pr_warn("adxl345: devm_request_threaded_irq failed\n");
        goto err_threadedIRQ_register;
    }


    // Initialize the FIFO
    INIT_KFIFO(adxl345dev->samples_fifo);

    // Initialize the wait queue
    init_waitqueue_head(&adxl345dev->queue);


    // Set the output data rate to 100 Hz
    addr = BW_RATE;
    buf = 0x0A;
    if (write_reg(client, &addr, &buf) != 0) {
        pr_warn("adxl345: write_reg failed\n");
        goto err_threadedIRQ_register;
    }


    // Set the default data format
    addr = DATA_FORMAT;
    buf = 0;
    if (write_reg(client, &addr, &buf) != 0) {
        pr_warn("adxl345: write_reg failed\n");
        return -1;
    }

    // Set the FIFO control to Steam mode, watermark treshhold = 20
    addr = FIFO_CTL;
    buf = 148;
    if (write_reg(client, &addr, &buf) != 0) {
        pr_warn("adxl345: write_reg failed\n");
        return -1;
    }

    // Set the measurement mode
    addr = POWER_CTL;
    buf = 0x08;
    if (write_reg(client, &addr, &buf) != 0) {
        pr_warn("adxl345: write_reg failed\n");
        return -1;
    }

    // Enable interrupts Watermark
    addr = INT_ENABLE;
    buf = 1;
    if (write_reg(client, &addr, &buf) != 0) {
        pr_warn("adxl345: write_reg failed\n");
        goto err_threadedIRQ_register;
    }

    return 0;

    err_threadedIRQ_register:
        misc_deregister(&adxl345dev->miscdev);

    err_misc_register:
        devm_kfree(&client->dev, adxl345dev);

    return -1;
}

static int adxl345_remove(struct i2c_client *client)
{
    char addr = 0x00;
    char buf = 0;
    struct adxl345_device *adxl345dev;
    
    adxl345dev = i2c_get_clientdata(client);

    // Unregister the misc device
    misc_deregister(&adxl345dev->miscdev);

    // Free the memory allocated for the device name
    kfree(adxl345dev->miscdev.name);

    // Free the memory allocated for the device structure
    devm_kfree(&client->dev, adxl345dev);

    // Decrease the number of devices
    adxl_count--;

    // Display a message when a supported device is removed
    printk(KERN_INFO "adxl345: removed device %s\n", client->name);

    // Set the measurement mode to standby
    addr = 0x2D;
    buf = 0x00;
    if (write_reg(client, &addr, &buf) != 0)
    {
        pr_warn("adxl345: write_reg failed\n");
        return -1;
    }

    return 0;
}





/**
 * -------------------------------------------------------------------------
 * 
 * FUNCTIONS
 * 
 * -------------------------------------------------------------------------
**/


/**
 * @brief Function for writing a register of the device
 * @param client: pointer to the i2c_client structure
 * @param addr: address of the register to write
 * @param buf: pointer to the buffer containing the data to write
 * @param len: number of bytes to write
 * @return 0 if successful, -1 otherwise
**/
static int write_reg(struct i2c_client *client, char *addr, char *buf) {
    
    char b[2] = {addr[0], buf[0]};

    if (i2c_master_send(client, b, sizeof(b)/sizeof(char) ) != sizeof(b)/sizeof(char))
    {
        pr_warn("adxl345: i2c_master_send failed\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Function for reading a register of the device
 * @param client: pointer to the i2c_client structure
 * @param addr: address of the register to read
 * @param buf: pointer to the buffer containing the data to read
 * @param len: number of bytes to read
 * @return 0 if successful, -1 otherwise
*/
static int read_reg(struct i2c_client *client, char addr, char *buf, int len) {
    // Set the register address to read
    if (i2c_master_send(client , &addr, 1) != 1)
    {
        pr_warn("adxl345: i2c_master_send failed\n");
        return -1;
    }
    // Read the register value
    if(i2c_master_recv(client , buf, len) != len)
    {
        pr_warn("adxl345: i2c_master_recv failed\n");
        return -1;
    }

    return 0;
}


static long adxl345_write_read_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct adxl345_device *dev;
    struct i2c_client *client;
    struct ioctl_data io_data;
    char len;
    short k_buf = 0;


    dev = (struct adxl345_device *)file->private_data;
    client = to_i2c_client(dev->miscdev.parent);


    if (copy_from_user(&io_data, (struct ioctl_data *)arg, sizeof(struct ioctl_data)) != 0)
    {
        return -EFAULT; // Bad address
    }

    len = io_data.write_data[1]; // number of bytes to be read

    if (cmd == RWR_VALUE)
    {
        switch (io_data.write_data[0])
        {
        case X_IOCTL:
            read_reg(client, DATAX0, &((char*)&k_buf)[0], 1);
            io_data.read_data[0] = k_buf;
            if (len > 1){
                read_reg(client, DATAX1, &((char*)&k_buf)[0], 1);
                io_data.read_data[1] = k_buf;
            };
            break;

        case Y_IOCTL:
            read_reg(client, DATAY0, &((char*)&k_buf)[0], 1);
            io_data.read_data[0] = k_buf;
            if (len > 1){
                read_reg(client, DATAY1, &((char*)&k_buf)[0], 1);
                io_data.read_data[1] = k_buf;
            };       
            break;

        case Z_IOCTL:
            read_reg(client, DATAZ0, &((char*)&k_buf)[0], 1);
            io_data.read_data[0] = k_buf;
            if (len > 1){
                read_reg(client, DATAZ1, &((char*)&k_buf)[0], 1);
                io_data.read_data[1] = k_buf;
            };
            break;
        default:
            return -1;
            break;
        }
    }

    if (copy_to_user((struct ioctl_data *)arg, &io_data, sizeof(struct ioctl_data)))
    {
        return -EFAULT;
    }

    return 0;
}



ssize_t adxl345_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    struct adxl345_device *dev = (struct adxl345_device *)file->private_data;
    struct miscdevice *miscdev = &dev->miscdev;
    struct i2c_client *client = to_i2c_client(miscdev->parent);
    // int retval;
    short k_buf = 0;
    char fifo_len = 0;
    struct fifo_element fifo;
    int i, bytes_read;
    int axis;

    // Wait until there is data available in the FIFO
    wait_event_interruptible(dev->queue, !kfifo_is_empty(&dev->samples_fifo));

    // Determine the length of the FIFO
    fifo_len = kfifo_len(&dev->samples_fifo);

    // Limit size to the length of the FIFO
    if (count > fifo_len)
        count = fifo_len;

    // Read data from FIFO and copy to user buffer
    bytes_read = 0;
    axis = dev->addr[0] - DATAX0; // Axis offset
    for (i = 0; i < count; i++)
    {
        if (!kfifo_get(&dev->samples_fifo, &fifo))
        {
            pr_err("[ERROR] failed to get data from FIFO");
            return -1;
        }

        // Copy data from FIFO to user buffer
        if (copy_to_user(buf, fifo.data + axis, 2 * sizeof(char)))
        {
            pr_err("[ERROR] failed to copy data to user buffer");
            return -1;
        }

        // Update bytes read
        bytes_read += sizeof(fifo.data);
    }

    // Return the total size of data read
    return bytes_read;
}



irqreturn_t adxl345_int(int irq, void *dev_id)
{
	struct adxl345_device *adxl345_device_;
	struct i2c_client* client;
	uint8_t h_fifo_elements = 0;
	int err;
	
	adxl345_device_ = (struct adxl345_device *)dev_id;

	client = container_of(adxl345_device_->miscdev.parent, struct i2c_client, dev);
	if (irq != client->irq)
		return IRQ_NONE;

	err = read_register(client,FIFO_STATUS,&h_fifo_elements);
	h_fifo_elements = h_fifo_elements & 0x1F;
	//pr_info("adxl345 called\n");
	//Empty HW FIFO
	while(h_fifo_elements > 0){
		struct adxl345_sample sample;
		struct adxl345_sample discarded;
		err = read_multiple_registers(client, DATAX0,(char*)&sample, 6);
		if(err != 6){
			pr_warn("Failed to read from adxl345 fifo\n");
			h_fifo_elements--;
			continue;
		}
		//pr_info("IRQ Test Read X:%d\tY:%d\tZ:%d\n",sample.X,sample.Y,sample.Z);
		while((err = kfifo_put(&adxl345_device_->samples_fifo, sample)) == 0){
			err = kfifo_get(&adxl345_device_->samples_fifo, &discarded);
			//pr_warn("discarding value X:%d\tY:%d\tZ:%d\n",discarded.X,discarded.Y,discarded.Z);
		}	
		h_fifo_elements--;	
	}

	wake_up(&adxl345_device_->queue);
	return IRQ_HANDLED;
}




/* The following list allows the association between a device and its driver
    in the case of a static initialization without using device tree.

    Each entry contains a string used to make the association
    association and an integer that can be used by the driver to
    driver to perform different treatments depending on the physical
    the physical device detected (case of a driver that can manage
    different device models).*/

static struct i2c_device_id adxl345_idtable[] = {
    { "adxl345", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, adxl345_idtable);

#ifdef CONFIG_OF
/* If device tree support is available, the following list
    allows to make the association using the device tree.

    Each entry contains a structure of type of_device_id. The field
    compatible field is a string that is used to make the association
    with the compatible fields in the device tree. The data field is
    a void* pointer that can be used by the driver to perform different
    perform different treatments depending on the physical device detected.*/
static const struct of_device_id adxl345_of_match[] = {
    { .compatible = "qemu,adxl345",
    .data = NULL },
    {}
};

MODULE_DEVICE_TABLE(of, adxl345_of_match);
#endif

static struct i2c_driver adxl345_driver = {
    .driver   = {
        /* The name field must correspond to the name of the module
        and must not contain spaces. */
        .name           = "adxl345",
        .of_match_table = of_match_ptr(adxl345_of_match),
    },
    .id_table = adxl345_idtable,
    .probe    = adxl345_probe,
    .remove   = adxl345_remove,
};

module_i2c_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("adxl345 driver");
MODULE_AUTHOR("Bruno Pons");
