#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#define DM_MSG_PREFIX "dmp"
// Size of of a sector; any BIO block must be a multiple of this
static const int BIO_SECTOR_SIZE = 512;

/**
 * Store information adout
 * underlying device.
 * 
 * dev - underlying device
 * start - starting sector number of the device
 */
struct dmp_dm_target
{
    struct dm_dev *dev;
    sector_t start;
};

/**
 * STATISTICS FUNCTIONALITY
 */
static struct kobject *stat_kobj;
struct stat_info
{
    u8 req;
    u8 size_sum;
};
static struct stat_info read_stat, write_stat, total_stat;

static ssize_t stat_info_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf)
{
    // NO float, in kernel-space FPU mode is disabled
    u8 read_avg_size  = read_stat.req  != 0 ? read_stat.size_sum  / read_stat.req  : 0;
    u8 write_avg_size = write_stat.req != 0 ? write_stat.size_sum / write_stat.req : 0;
    u8 total_avg_size = total_stat.req != 0 ? total_stat.size_sum / total_stat.req : 0;
    return sprintf(buf, "read:\n \treqs: %u\n \tavg size: %u\nwrite:\n \treqs: %u\n \tavg_size: %u\ntotal:\n \treqs: %u\n \tavg_size: %u\n",
			             read_stat.req,  read_avg_size, 
                         write_stat.req, write_avg_size, 
                         total_stat.req, total_avg_size);
}

static ssize_t stat_info_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	return count;
}

/**
 * name, mode (permissions on a sysfs file), show, store
 * No, I didn't miss the 0 prefix
 */
static struct kobj_attribute stat_info_attribute = __ATTR(volumes, 0444, stat_info_show, stat_info_store);

static struct attribute *attrs[] = {
	&stat_info_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

/**
 * This function gets called when the module get new bio request.
 * The request that it receives is submitted to device so 
 * bio->bi_bdev points to new device.
 * 
 * Param:
 * ti - the dm_target structure representing basic target
 * bio - the block IO request from upper layer
 * map_context - the mapping context of target
 * 
 * Return values:
 * < 0: error
 * = 0: The target will handle the IO by resubmitting it later (DM_MAPIO_SUBMITTED)
 * = 1: simple remap complete                                  (DM_MAPIO_REMAPPED)
 * = 2: The target wants to push back the IO
 */
static int dmp_map(struct dm_target* target, struct bio* bio)
{
    struct dmp_dm_target *mdt = (struct dmp_dm_target*) target->private;

    DMINFO("Entry: %s", __func__);

    // bio should perform on our underlying device
    bio_set_dev(bio, mdt->dev->bdev);

    // reg new new appeal
    total_stat.req += 1;
    total_stat.size_sum += bio_sectors(bio) * BIO_SECTOR_SIZE;

    // bio_data_dir - return data direction, READ or WRITE
    if(bio_data_dir(bio) == REQ_OP_WRITE)
    {
        DMINFO("bio is a write request");
        write_stat.req += 1;
        write_stat.size_sum += bio_sectors(bio) * BIO_SECTOR_SIZE;
    }
    else
    {
        DMINFO("bio is a read request");
        read_stat.req += 1;
        read_stat.size_sum += bio_sectors(bio) * BIO_SECTOR_SIZE;
    }

    // submit a bio to the block device layer for I/O
    submit_bio(bio);

    DMINFO("Exit : %s", __func__);

    return DM_MAPIO_SUBMITTED;
}

/**
 * This is constructor function of target gets called when we create some device of type 'dmp'.
 * i.e on execution of command 'dmsetup create'. It gets called per device.
 * 
 * 
 */
static int dmp_ctr(struct dm_target* target, unsigned int argc, char** argv)
{
    struct dmp_dm_target *mdt;
    unsigned long long start;
    int ret = 0;

    DMINFO("Entry: %s", __func__);

    if (argc != 2)
    {
        DMERR("Invalid no. of arguments.");
        target->error = "Invalid argument count";
        return -EINVAL;
    }

    // can put the current process into sleep to wait
    // for a page when called in situations of insufficient memory
    mdt = kmalloc(sizeof(struct dmp_dm_target), GFP_KERNEL);

    if(mdt == NULL)
    {
        DMERR("Error in kmalloc memmory allocation");
        target->error = "Cannot allocate linear context";
        return -ENOMEM;
    }       

    if(sscanf(argv[1], "%llu", &start) != 1)
    {
        target->error = "Invalid device sector";
        kfree(mdt);
        ret = -EINVAL;
    }

    mdt->start=(sector_t)start;

    // To add device in target's table and increment in device count
    if (dm_get_device(target, argv[0], dm_table_get_mode(target->table), &mdt->dev))
    {
        target->error = "Device lookup failed";
        goto out;
    }

    target->private = mdt;

    DMINFO("Exit : %s ", __func__);

    return ret;

out:
    kfree(mdt);
    DMERR("Exit : %s with ERROR", __func__);
    return ret;
}

/**
 * Destructor, gets called per device.
 */
static void dmp_dtr(struct dm_target *target)
{
    struct dmp_dm_target *mdt = (struct dmp_dm_target *) target->private;
    DMINFO("Entry: %s", __func__);
    dm_put_device(target, mdt->dev);
    kfree(mdt);
    DMINFO("Exit : %s", __func__);
}

/*
 * This structure is fops for dmp target.
 */
static struct target_type dmp = {
    .module     = THIS_MODULE,
    .name       = "dmp",
    .version    = {1,0,0},
    .ctr        = dmp_ctr,
    .dtr        = dmp_dtr,
    .map        = dmp_map,
};

static int __init dmp_init(void)
{
    int result;
    // get kobject from current module
    // declare here because ISO C90
    struct kobject mod_kobj = (((struct module*)(THIS_MODULE))->mkobj).kobj;

    DMINFO("Entry: %s", __func__);
    result = dm_register_target(&dmp);
    if(result < 0)
    {
        DMERR("Error in registering target");
    }
    else
    {
        DMINFO("Target registered");
    }

    stat_kobj = kobject_create_and_add("stat", &mod_kobj);
    if (!stat_kobj)
    {
        DMERR("Error in registering kobject");
        return -ENOMEM;
    }
    // function creates a group for the first time
    if (sysfs_create_group(stat_kobj, &attr_group))
    {
        kobject_put(stat_kobj);
    }

    DMINFO("Exit : %s", __func__);
    
    return result;
}

static void __exit dmp_exit(void)
{
    DMINFO("Entry: %s", __func__);
    kobject_put(stat_kobj);
    dm_unregister_target(&dmp);
    DMINFO("Target unregistered");
    DMINFO("Exit : %s", __func__);
}

module_init(dmp_init);
module_exit(dmp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikita Pochaev <pochaev.nik@gmail.com>");
MODULE_DESCRIPTION("Device mapper proxy");