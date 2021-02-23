/*
 * Copyright (c) 2015-2020 ACOINFO Co., Ltd.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ms_fatfs.c FATFS implement.
 *
 * Author: Jiao.jinxing <jiaojinxing@acoinfo.com>
 *
 */

#define __MS_IO
#include "ms_kern.h"
#include "ms_io_core.h"
#include "ms_fatfs.h"

#undef DIR
#include "fatfs/source/ff.h"

#include <string.h>
#include <stdio.h>

/**
 * @brief FAT file system.
 */

static int __ms_fatfs_result_to_errno(FRESULT fresult)
{
    int err;

    switch (fresult) {
    case FR_OK:
        err = 0;
        break;

    case FR_DISK_ERR:
        err = EIO;
        break;

    case FR_INT_ERR:
        err = EFAULT;
        break;

    case FR_NOT_READY:
        err = EIO;
        break;

    case FR_NO_FILE:
        err = ENOENT;
        break;

    case FR_NO_PATH:
        err = ENOTDIR;
        break;

    case FR_INVALID_NAME:
        err = EINVAL;
        break;

    case FR_DENIED:
        err = EACCES;
        break;

    case FR_EXIST:
        err = EEXIST;
        break;

    case FR_INVALID_OBJECT:
        err = EBADF;
        break;

    case FR_WRITE_PROTECTED:
        err = EACCES;
        break;

    case FR_INVALID_DRIVE:
        err = ENODEV;
        break;

    case FR_NOT_ENABLED:
        err = ENODEV;
        break;

    case FR_NO_FILESYSTEM:
        err = EINVAL;
        break;

    case FR_MKFS_ABORTED:
        err = EIO;
        break;

    case FR_TIMEOUT:
        err = ETIMEDOUT;
        break;

    case FR_LOCKED:
        err = EBUSY;
        break;

    case FR_NOT_ENOUGH_CORE:
        err = ENOMEM;
        break;

    case FR_TOO_MANY_OPEN_FILES:
        err = ENFILE;
        break;

    case FR_INVALID_PARAMETER:
        err = EINVAL;
        break;

    default:
        err = EFAULT;
        break;
    }

    return err;
}

static int __ms_oflag_to_fatfs_oflag(int oflag)
{
    int ret = 0;

    switch (oflag & O_ACCMODE) {
    case O_RDONLY:
        ret |= FA_READ;
        break;

    case O_WRONLY:
        ret |= FA_WRITE;
        break;

    case O_RDWR:
        ret |= FA_READ | FA_WRITE;
        break;
    }

    if (oflag & O_CREAT) {
        if (oflag & O_TRUNC) {
            ret |= FA_CREATE_ALWAYS;
        } else {
            ret |= FA_OPEN_ALWAYS;
        }

        if (oflag & O_EXCL) {
            ret |= FA_CREATE_NEW;
            ret &= ~(FA_CREATE_ALWAYS | FA_OPEN_ALWAYS);
        }
    }

    if (oflag & O_APPEND) {
        ret |= FA_OPEN_APPEND;
    }

    return ret;
}

static int __ms_fatfs_mount(ms_io_mnt_t *mnt, ms_io_device_t *dev, const char *dev_name, ms_const_ptr_t param)
{
    FATFS *fatfs;
    FRESULT fresult;
    int ret;

    if (dev != MS_NULL) {
        fatfs = ms_kzalloc(sizeof(FATFS));
        if (fatfs != MS_NULL) {
            fatfs->pdrv  = dev;
            fatfs->ipart = (BYTE)(((ms_addr_t)param) & 0xffUL);

            fatfs->win = ms_kmalloc_align(FF_MAX_SS, MS_ARCH_CACHE_LINE_SIZE);
            if (fatfs->win != MS_NULL) {
                fresult = f_mount(fatfs, "/", 1U);
                if (fresult != FR_OK) {
                    (void)ms_kfree(fatfs->win);
                    (void)ms_kfree(fatfs);
                    ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
                    ret = -1;
                } else {
                    mnt->ctx = fatfs;
                    ret = 0;
                }
            } else {
                (void)ms_kfree(fatfs);
                ms_thread_set_errno(ENOMEM);
                ret = -1;
            }
        } else {
            ms_thread_set_errno(ENOMEM);
            ret = -1;
        }
    } else {
        ms_thread_set_errno(EFAULT);
        ret = -1;
    }

    return ret;
}

static int __ms_fatfs_mkfs(ms_io_mnt_t *mnt, ms_const_ptr_t param)
{
    FATFS *fatfs = mnt->ctx;
    BYTE work[FF_MAX_SS];
    FRESULT fresult;
    int ret;

    fresult = f_mkfs(fatfs, "", MS_NULL, work, sizeof(work));
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_unmount(ms_io_mnt_t *mnt, ms_const_ptr_t param)
{
    FATFS *fatfs = mnt->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_unmount(fatfs);
    if ((fresult != FR_OK) && !mnt->umount_req) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        mnt->ctx = MS_NULL;
        (void)ms_kfree(fatfs->win);
        (void)ms_kfree(fatfs);
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_open(ms_io_mnt_t *mnt, ms_io_file_t *file, const char *path, int oflag, ms_mode_t mode)
{
    FATFS *fatfs = mnt->ctx;
    FIL *fatfs_file;
    FRESULT fresult;
    int ret;

    fatfs_file = ms_kzalloc(sizeof(FIL));
    if (fatfs_file != MS_NULL) {
        fatfs_file->buf = ms_kmalloc_align(FF_MAX_SS, MS_ARCH_CACHE_LINE_SIZE);
        if (fatfs_file->buf != MS_NULL) {
            oflag = __ms_oflag_to_fatfs_oflag(oflag);
            fresult = f_open(fatfs, fatfs_file, path, oflag);
            if (fresult != FR_OK) {
                (void)ms_kfree(fatfs_file->buf);
                (void)ms_kfree(fatfs_file);
                ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
                ret = -1;
            } else {
                file->ctx = fatfs_file;
                ret = 0;
            }
        } else {
            (void)ms_kfree(fatfs_file);
            ms_thread_set_errno(ENOMEM);
            ret = -1;
        }
    } else {
        ms_thread_set_errno(ENOMEM);
        ret = -1;
    }

    return ret;
}

static int __ms_fatfs_close(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_close(fatfs_file);
    if ((fresult != FR_OK) && !mnt->umount_req) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        (void)ms_kfree(fatfs_file->buf);
        (void)ms_kfree(fatfs_file);
        file->ctx = MS_NULL;
        ret = 0;
    }

    return ret;
}

static ms_ssize_t __ms_fatfs_read(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_ptr_t buf, ms_size_t len)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    ms_ssize_t ret;
    UINT rlen;

    fresult = f_read(fatfs_file, buf, len, &rlen);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = rlen;
    }

    return ret;
}

static ms_ssize_t __ms_fatfs_write(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_const_ptr_t buf, ms_size_t len)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    ms_ssize_t ret;
    UINT wlen;

    fresult = f_write(fatfs_file, buf, len, &wlen);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = wlen;
    }

    return ret;
}

static int __ms_fatfs_fcntl(ms_io_mnt_t *mnt, ms_io_file_t *file, int cmd, int arg)
{
    int ret;

    switch (cmd) {
    case F_GETFL:
        ret = file->flags;
        break;

    case F_SETFL:
        if ((!(file->flags & FWRITE)) && (arg & FWRITE)) {
            ms_thread_set_errno(EACCES);
            ret = -1;
        } else {
            file->flags = arg;
            ret = 0;
        }
        break;

    default:
        ms_thread_set_errno(EINVAL);
        ret = -1;
        break;
    }

    return ret;
}

static int __ms_fatfs_fstat(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_stat_t *buf)
{
    FIL *fatfs_file = file->ctx;

    bzero(buf, sizeof(ms_stat_t));

    buf->st_size = f_size(fatfs_file);
    buf->st_mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFREG;

    return 0;
}

static int __ms_fatfs_isatty(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    return 0;
}

static int __ms_fatfs_fsync(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_sync(fatfs_file);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_ftruncate(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_off_t len)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    FSIZE_t old_off;
    int ret;

    old_off = f_tell(fatfs_file);

    fresult = f_lseek(fatfs_file, len);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;

    } else {
        fresult = f_truncate(fatfs_file);
        if (fresult != FR_OK) {
            ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
            ret = -1;

        } else {
            old_off = MS_MIN(len, old_off);
            fresult = f_lseek(fatfs_file, old_off);
            if (fresult != FR_OK) {
                ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
                ret = -1;
            } else {
                ret = 0;
            }
        }
    }

    return ret;
}

static ms_off_t __ms_fatfs_lseek(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_off_t offset, int whence)
{
    FIL *fatfs_file = file->ctx;
    FRESULT fresult;
    ms_off_t ret;
    ms_off_t pos;

    ret = 0;
    switch (whence) {
    case SEEK_SET:
        pos = offset;
        break;

    case SEEK_CUR:
        pos = f_tell(fatfs_file) + offset;
        break;

    case SEEK_END:
        pos = f_size(fatfs_file) + offset;
        break;

    default:
        ret = EINVAL;
        break;
    }

    if (ret == 0) {
        if (pos < 0) {
            ret = EINVAL;
        }
    }

    if (ret == 0) {
        fresult = f_lseek(fatfs_file, pos);
        if (fresult != FR_OK) {
            ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
            ret = -1;
        } else {
            ret = f_tell(fatfs_file);
        }

    } else {
        ms_thread_set_errno(ret);
        ret = -1;
    }

    return ret;
}

static int __ms_fatfs_stat(ms_io_mnt_t *mnt, const char *path, ms_stat_t *buf)
{
    FATFS *fatfs = mnt->ctx;
    FILINFO finfo;
    FRESULT fresult;
    int ret;

    bzero(buf, sizeof(ms_stat_t));

    if (MS_IO_PATH_IS_ROOT(path)) {
        buf->st_mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFDIR;
        ret = 0;

    } else {
        fresult = f_stat(fatfs, path, &finfo);
        if (fresult != FR_OK) {
            ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
            ret = -1;

        } else {
            if (finfo.fattrib & AM_DIR) {
                buf->st_mode = S_IFDIR;
            } else {
                buf->st_mode = S_IFREG;
                buf->st_size = finfo.fsize;
            }
            if (finfo.fattrib & AM_RDO) {
                buf->st_mode |= S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
            } else {
                buf->st_mode |= S_IRWXU | S_IRWXG | S_IRWXO;
            }

            ret = 0;
        }
    }

    return ret;
}

static int __ms_fatfs_statvfs(ms_io_mnt_t *mnt, ms_statvfs_t *buf)
{
    FATFS *fatfs = mnt->ctx;
    FATFS *out_fs;
    FRESULT fresult;
    int ret;

    fresult = f_getfree(fatfs, "", (DWORD *)&buf->f_bfree, &out_fs);
    if (fresult != FR_OK) {
        bzero(buf, sizeof(ms_statvfs_t));
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        buf->f_bsize  = FF_MIN_SS;
        buf->f_frsize = fatfs->csize * buf->f_bsize;
        buf->f_blocks = (fatfs->n_fatent - 2);
        buf->f_files  = 0UL;
        buf->f_ffree  = 0UL;
        buf->f_dev    = mnt->dev->nnode.name;
        buf->f_mnt    = mnt->nnode.name;
        buf->f_fsname = MS_FATFS_NAME;
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_unlink(ms_io_mnt_t *mnt, const char *path)
{
    FATFS *fatfs = mnt->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_unlink(fatfs, path);
    if (fresult != FR_OK) {
        if (fresult == FR_DENIED) {
            ms_thread_set_errno(ENOTEMPTY);
        } else {
            ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        }
        ret = -1;

    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_mkdir(ms_io_mnt_t *mnt, const char *path, ms_mode_t mode)
{
    FATFS *fatfs = mnt->ctx;
    FRESULT fresult;
    int ret;

    (void)mode;

    fresult = f_mkdir(fatfs, path);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_rename(ms_io_mnt_t *mnt, const char *old, const char *_new)
{
    FATFS *fatfs = mnt->ctx;
    FRESULT fresult;
    FILINFO finfo;
    int ret;

    /*
     * Check if '_new' path exists; remove it first
     */
    fresult = f_stat(fatfs, _new, &finfo);
    if (fresult == FR_OK) {
        fresult = f_unlink(fatfs, _new);
    } else {
        fresult = FR_OK;
    }

    if (fresult == FR_OK) {
        fresult = f_rename(fatfs, old, _new);
    }

    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_opendir(ms_io_mnt_t *mnt, ms_io_file_t *file, const char *path)
{
    FATFS *fatfs = mnt->ctx;
    DIR *fatfs_dir;
    FRESULT fresult;
    int ret;

    if (MS_IO_PATH_IS_ROOT(path)) {
        path = "/";
    }

    fatfs_dir = ms_kzalloc(sizeof(DIR));
    if (fatfs_dir != MS_NULL) {
        fresult = f_opendir(fatfs, fatfs_dir, path);
        if (fresult != FR_OK) {
            (void)ms_kfree(fatfs_dir);
            ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
            ret = -1;
        } else {
            file->ctx = fatfs_dir;
            ret = 0;
        }

    } else {
        ms_thread_set_errno(ENOMEM);
        ret = -1;
    }

    return ret;
}

static int __ms_fatfs_closedir(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    DIR *fatfs_dir = file->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_closedir(fatfs_dir);
    if ((fresult != FR_OK) && !mnt->umount_req) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        (void)ms_kfree(fatfs_dir);
        file->ctx = MS_NULL;
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_readdir_r(ms_io_mnt_t *mnt, ms_io_file_t *file, ms_dirent_t *entry, ms_dirent_t **result)
{
    DIR *fatfs_dir = file->ctx;
    FILINFO finfo;
    FRESULT fresult;
    int ret;

    fresult = f_readdir(fatfs_dir, &finfo);
    if (fresult == FR_OK) {
        if (finfo.fname[0] != '\0') {
            strlcpy(entry->d_name, finfo.fname, sizeof(entry->d_name));
            entry->d_type = (finfo.fattrib & AM_DIR) ? DT_DIR : DT_REG;

            if (result != MS_NULL) {
                *result = entry;
            }

            ret = 1;

        } else {
            if (result != MS_NULL) {
                *result = MS_NULL;
            }

            ret = 0;
        }

    } else {
        if (result != MS_NULL) {
            *result = MS_NULL;
        }

        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    }

    return ret;
}

static int __ms_fatfs_rewinddir(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    DIR *fatfs_dir = file->ctx;
    FRESULT fresult;
    int ret;

    fresult = f_rewinddir(fatfs_dir);
    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int __ms_fatfs_seekdir(ms_io_mnt_t *mnt, ms_io_file_t *file, long loc)
{
    DIR *fatfs_dir = file->ctx;
    FRESULT fresult;
    FILINFO finfo;
    long dptr;
    int ret;

    dptr = fatfs_dir->dptr;
    if (loc < dptr) {
        fresult = f_rewinddir(fatfs_dir);
    } else {
        fresult = FR_OK;
    }

    if (fresult == FR_OK) {
        while (dptr < loc) {
            fresult = f_readdir(fatfs_dir, &finfo);
            if (fresult != FR_OK) {
                break;
            } else if (finfo.fname[0] == '\0') {
                fresult = FR_INVALID_PARAMETER;
                break;
            }

            dptr = fatfs_dir->dptr;
        }
    }

    if (fresult != FR_OK) {
        ms_thread_set_errno(__ms_fatfs_result_to_errno(fresult));
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static long __ms_fatfs_telldir(ms_io_mnt_t *mnt, ms_io_file_t *file)
{
    DIR *fatfs_dir = file->ctx;

    return fatfs_dir->dptr;
}

static ms_io_fs_ops_t ms_io_fatfs_ops = {
        .type       = MS_IO_FS_TYPE_DISKFS,
        .mount      = __ms_fatfs_mount,
        .unmount    = __ms_fatfs_unmount,
        .mkfs       = __ms_fatfs_mkfs,

        .link       = MS_NULL,
        .unlink     = __ms_fatfs_unlink,
        .mkdir      = __ms_fatfs_mkdir,
        .rmdir      = __ms_fatfs_unlink,
        .rename     = __ms_fatfs_rename,
        .sync       = MS_NULL,
        .truncate   = MS_NULL,

        .stat       = __ms_fatfs_stat,
        .lstat      = __ms_fatfs_stat,
        .statvfs    = __ms_fatfs_statvfs,

        .open       = __ms_fatfs_open,
        .close      = __ms_fatfs_close,
        .read       = __ms_fatfs_read,
        .write      = __ms_fatfs_write,
        .ioctl      = MS_NULL,
        .fcntl      = __ms_fatfs_fcntl,
        .fstat      = __ms_fatfs_fstat,
        .isatty     = __ms_fatfs_isatty,
        .fsync      = __ms_fatfs_fsync,
        .fdatasync  = __ms_fatfs_fsync,
        .ftruncate  = __ms_fatfs_ftruncate,
        .lseek      = __ms_fatfs_lseek,
        .poll       = MS_NULL,

        .opendir    = __ms_fatfs_opendir,
        .closedir   = __ms_fatfs_closedir,
        .readdir_r  = __ms_fatfs_readdir_r,
        .rewinddir  = __ms_fatfs_rewinddir,
        .seekdir    = __ms_fatfs_seekdir,
        .telldir    = __ms_fatfs_telldir,
};

static ms_io_fs_t ms_io_fatfs = {
        .nnode = {
            .name = MS_FATFS_NAME,
        },
        .ops = &ms_io_fatfs_ops,
};

ms_err_t ms_fatfs_register(void)
{
    return ms_io_fs_register(&ms_io_fatfs);
}
