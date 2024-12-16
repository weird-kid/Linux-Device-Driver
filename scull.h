#define scull_major 50

struct scull_qset {
    void **data;
    struct scull_qset *next;
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



void check_else(int, char*);
int scull_init(void);
void scull_exit(void);
void scull_chrdev_setup(struct scull_dev*, int);
int  scull_open(struct inode*, struct file*);
ssize_t scull_read(struct file*, char __user*, size_t , loff_t*);
ssize_t scull_write(struct file*,const  char __user *, size_t, loff_t*);
struct scull_qset* scull_follow(struct scull_dev*, int );
void scull_trim(struct scull_dev*);
int scull_release(struct inode*, struct file*);



