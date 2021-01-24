#include <linux/module.h>
#include <linux/mm.h>

#define BIGBUF_MAJOR 60
#define BIGBUF_CONCURRENT 8
#define BIGBUF_MAXDEVICES 4
#define BIGBUF_MAXSIZE (8 * 1024 * 1024)
#define BIGBUF_CHRDEVN "bigbuf"

struct semaphore sem[BIGBUF_MAXDEVICES] = {MUTEX,MUTEX,MUTEX,MUTEX};
struct file *files[BIGBUF_MAXDEVICES][BIGBUF_CONCURRENT];
char *buffer[BIGBUF_MAXDEVICES];
int buffer_allocated_size[BIGBUF_MAXDEVICES] = {16,16,16,16},
buffer_used_size[BIGBUF_MAXDEVICES] = {0,0,0,0},
files_open[BIGBUF_MAXDEVICES] = {0,0,0,0};


int bigbuf_open(struct inode *inode, struct file *file) {
  int minor = MINOR(inode->i_rdev); 
  int file_id;


  if (files_open[minor] == BIGBUF_CONCURRENT) {
    printk("open: reached max files (%d) on minor dev %d", minor, BIGBUF_CONCURRENT);
    return -1;
  } else if (minor > BIGBUF_MAXDEVICES) {
    printk(KERN_ERR "open: max devno is %d but tried to open %d", BIGBUF_MAXDEVICES, minor);
    return -1;
  }

  for (file_id = 0; file_id < BIGBUF_CONCURRENT; file_id++) {
    if (files[minor][file_id] == NULL) {
      files[minor][file_id] = file;
      files_open[minor]++;
      MOD_INC_USE_COUNT;
      break;
    }
  }

  return 0;
}

void bigbuf_release(struct inode *inode, struct file *file) {
  int i;
  int minor = MINOR(inode->i_rdev);

  for (i = 0; i < BIGBUF_CONCURRENT; i++) {
    if (files[minor][i] == file) {
      files[minor][i] = NULL;
      files_open[minor]--;
      MOD_DEC_USE_COUNT;
      break;
    }
  }

}

int bigbuf_read(struct inode *inode, struct file *file, char *input, int count) {
  int minor = MINOR(inode->i_rdev),i;

  for (i = 0; i < count; i++) {
    char c;
    if (file->f_pos < buffer_used_size[minor]) {
      c = buffer[minor][file->f_pos];
      file->f_pos++;
    } else {
      return i;
    }
    put_user(c, input + i);
  }
  return count;
}

int __enlarge_buffer(int minor, int enlarged_buffer_allocated_size) {
  char *enlarged_buffer;
  int file_id, device;

  if (buffer_allocated_size[minor] > enlarged_buffer_allocated_size) {
    for (file_id = 0; file_id < BIGBUF_CONCURRENT; file_id++) {
      struct file *dfile = files[minor][file_id];
      if (dfile && dfile->f_pos >= enlarged_buffer_allocated_size) { 
        printk("enlarge: new buffer size would destroy cursor");
        return -1;
      }
    }
  }

  if (enlarged_buffer_allocated_size > BIGBUF_MAXSIZE) {
    printk("enlarge: tried to allocate over max size");
    return -1;
  }

  down(&sem[minor]);

  printk("enlarge: allocating from %d to %d\n", buffer_allocated_size[minor], enlarged_buffer_allocated_size);
  enlarged_buffer = vmalloc(enlarged_buffer_allocated_size);
  if (enlarged_buffer == NULL) {
    up(&sem[minor]);
    printk("enlarge: failed allocating buf\n");
    return -EINVAL;
  }

  for (device = 0; device < buffer_used_size[minor]; device++) {
    enlarged_buffer[device] = buffer[minor][device];
  }

  vfree(buffer[minor]);
  buffer_allocated_size[minor] = enlarged_buffer_allocated_size;
  buffer[minor] = enlarged_buffer;
  up(&sem[minor]);

  return 0;
}

int bigbuf_write(struct inode *inode, struct file *file, const char *input, int count) {
  int minor = MINOR(inode->i_rdev),i,enlarged_buffer;

  for (i = 0; i < count; i++) {
    char c = get_user(input + i);
    if (buffer_used_size[minor] == buffer_allocated_size[minor])
    {
      enlarged_buffer = __enlarge_buffer(minor, buffer_allocated_size[minor] + count - i);
      if (enlarged_buffer < 0) {
        return -1;
      }
    }

    buffer[minor][file->f_pos] = c;
    buffer_used_size[minor] = ++file->f_pos;
  }
  return count;
}

struct file_operations buffer_ops = {
  write : bigbuf_write,
  read : bigbuf_read,
  open : bigbuf_open,
  release : bigbuf_release,
};

int init_module() {
  int device_id,did_register;
  char *buffer_area;

  for (device_id = 0; device_id < BIGBUF_MAXDEVICES; device_id++) {
    down(&sem[device_id]);
    buffer[device_id] = vmalloc(buffer_allocated_size[device_id]);
    up(&sem[device_id]);

    if (buffer[device_id] == NULL) {
      printk("init: can't allocate");
      return -1;
    }
  }

  did_register = register_chrdev(BIGBUF_MAJOR, BIGBUF_CHRDEVN, &buffer_ops);
  if (did_register == 0) {
    printk("init: ok");
    return 0;
  };
  printk("init: failed to register");
  return did_register;
}

void cleanup_module() {
  int did_unregister = unregister_chrdev(BIGBUF_MAJOR, BIGBUF_CHRDEVN),
  device_id;
  for (device_id = 0; device_id < BIGBUF_MAXDEVICES; device_id++) {
    vfree(buffer[device_id]);
  }

  if (did_unregister == 0) {
    printk("cleanup: ok");
  } else {
    printk("cleanup: can't unregister");
  }
}
