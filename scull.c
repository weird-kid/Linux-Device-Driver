#include<linux/module.h>
#include<linux/fs.h>
#include<linux/stat.h>
#include<linux/errno.h>
#include<asm/uaccess.h>
#include<linux/slab.h>
#include<linux/cdev.h>
#include<linux/kernel.h>
#include<linux/types.h>
MODULE_LICENSE("GPL");
#define scull_major 50

void check_else(int, char*);
int scull_init(void);
void scull_exit(void);
void scull_chrdev_setup(struct scull_dev*, int);
int  scull_open(struct inode*, struct file*);
ssize_t scull_read(struct file*, char __user*, size_t , loff_t*);
ssize_t scull_write(struct file*, char __user*, size_t, loff_t*);
struct scull_qset* scull_follow(struct scull_dev*, int);
int scull_trim(struct scull_dev*);
int scull_release(struct inode*, struct file*);

struct scull_qset{
	void **data;
	struct scull_qset *next;
};

struct file_operations scull_fops =  {                           
	.open = scull_open,
	.release = scull_release,
	.read = scull_read,
	.write = scull_write,
	//.ioctl = scull_ioctl,
	.owner  = THIS_MODULE,                               
	//.llseek = scull_llseek,
};

struct scull_dev {
	struct scull_qset* data;
	unsigned long size;
	unsigned int access_key;
	int quantum;
	int qset;
	struct semaphore sem;
	struct cdev cdev;
};

dev_t dev = MKDEV(scull_major, 0);
struct cdev* chr_dev; 


/* It iterates the qset and frees any quantumd data it finds */

int scull_trim(struct scull_dev* dev){	
    int qset = dev->qset;
    struct scull_qset* dptr;     
    int i;

    for(dptr=dev->data; dptr; dptr=dptr->next){
        if(dptr->data){
            for(i=0; i<qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
    }
    dev->size = 0;
    dev->data = NULL;
   
    return 0;
}



int scull_init() {
    struct scull_dev* dev;
	check_else(register_chrdev_region(dev, 4, "scull"), "Char dev not registered properly\n" );
	scull_chrdev_setup(dev, 0);

	return 0;
}

int scull_open(struct inode* inode, struct file* flip){
	/* adding the scull_dev properties of i_cdev to open file structure & trimming it when opened in write mode*/
	struct scull_dev* dev;
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);                                     /* Check in ./Doc */
	flip->private_data = dev;

	if(flip->f_flags & O_ACCMODE == O_WRONLY){
		scull_trim(dev);
	}
	
	return 0;
	}

int scull_release(struct inode* inode, struct file* flip){
	return 0;
}

void scull_chrdev_setup(struct scull_dev* s_dev, int index){
	int err, usrdev_no = MKDIR(scull_major, 0 + index);
	cdev_init(&s_dev->cdev, &scull_fops);
	s_dev->cdev.owner = THIS_MODULE;
	s_dev->cdev.ops =  &scull_fops;
	check_else( cdev_add(&s_dev->cdev, devno, 1), "Error adding chr_dev at specified dev_no\n");
	}

ssize_t  scull_read(struct file* flip, char __user* buffer, size_t count, loff_t* f_pos){

	// Integer variables defined in driver --> int  , while rest-> ssize_t , so platform independent.	
	struct scull_dev* dev = flip->private_data;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int itemsize = quantum*q_set;
	struct scull_qset* dptr ;
	ssize_t retval = 0;
	int item,rest,s_pos, q_pos;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	// If the first byte to be read  beyond EOF 	
	if(*f_pos >= dev->size)
		goto out;
	
	// If part of data to be read is beyond EOF	
	if(*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	// Getting itemno, qset_pos and q_pos 
	item = (long )*f_pos/itemsize;                        // I don't understand why typecast f_pos -> long ?
	rest   = (long )*f_pos % itemsize;
	s_pos = rest/quantum;
	q_pos = rest % quantum;
	
	//dptr should point to current itemno
	dptr = scull_follow(dev, item);
	if (!dptr || !dptr->data || !dptr->data[s_pos])
		goto out;
	
	//Take the userspace buffer and put it into kernelspace buffer safely.
	if(copy_from_user(buffer,dptr->data[s_pos] + q_pos, count) != count){
		retval = -EFAULT;                                                 //bad address given by user-space 
		goto out;
	}
	*f_pos += count;
	retval = count;
	out :   up(&dev->sem); 
		    return retval;
}

ssize_t scull_write(struct file* flip, char __user* buffer, ssize_t count, loff_t* f_pos){

	struct scull_dev* dev = flip->private_data;
	int quantum = dev->quantum; int qset = dev->qset;
	int itemsize = quantum * qset;
	struct scull_qset* dptr;
	int item, rest, s_pos, q_pos;
	ssize_t retval = 0;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	item = (long)*f_pos/itemsize;
    rest = (long)*f_pos % itemsize;
	s_pos = rest/quantum; 
    q_pos = rest % quantum; 

	dptr = scull_follow(dev, item);
	
	if (!dptr)
		goto out;

	if(!dptr->data){
		dptr->data = kmalloc(count * sizeof(char *), GFP_KERNEL);
		if(!dptr->data){
			goto out;
		}
		memset(dptr->data, 0, count * sizeof(char *));
	}

	if(!dptr->data[s_pos]){
		dptr->data[s_pos] = kmalloc(qset, GFP_KERNEL);
		if(!dptr->data[s_pos]){
			goto out;
		}
	}

	if(q_pos + count > quantum)
		count = quantum - q_pos;
	
	if(copy_from_user(dptr->data[s_pos] + q_pos, buffer, count) != count){
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	if (dev->size < *f_pos)
		dev->size = *f_pos;

	out:up(&dev->sem) ;
	    return retval;


}

struct scull_qset*  scull_follow(struct scull_dev* dev,int count){
	struct scull_qset*  qs = dev->data;
	int size = sizeof(struct scull_qset);

	/*intialize the first qset */
	if(!qs){
		qs = (struct scull_qset*)kmalloc(size, GFP_KERNEL);
		if(!qs)
			return NULL;
		memset(qs, 0, sizeof(struct scull_qset));
	}

	 while(count--){
		 if(qs->next){
			 qs->next = (struct scull_qset *)kmalloc(size, GFP_KERNEL);
			 if(!qs->next)
				 return NULL;
			memset(qs->next, 0, size);
		 }
		 qs = qs->next;
	 }
	 return qs;
}

void  scull_exit() {
	if (chr_dev)
		cdev_del(chr_dev);
	if (dev) 
		unregister_chrdev_region(dev, 4);
	printk(KERN_ALERT "Module exited function called \n");
}

void check_else(int ret_val, char* err_msg){
	if (ret_val < 0){
		printk(KERN_ALERT "Error:%s\n",err_msg);
		scull_exit();
	}
}

module_init(scull_init);
module_exit(scull_exit);
MODULE_VERSION("0.3");
MODULE_AUTHOR("Mr Curiosity");
