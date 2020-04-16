/*
 * Copyright (c) 2019 MS-RTOS Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ms_fatfs_porting.c FATFS porting.
 *
 * Author: Jiao.jinxing <jiaojixing@acoinfo.com>
 *
 */

#define __MS_IO
#include "ms_kern.h"
#include "ms_io_core.h"

#undef DIR
#include "fatfs/source/ff.h"
#include "fatfs/source/diskio.h"

/**
 * @brief FatFs porting.
 */

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
    void *pdrv              /* Physical drive nmuber to identify the drive */
)
{
    ms_io_device_t *dev = (ms_io_device_t *)pdrv;
    DSTATUS dstatus;

    if (dev->drv->ops->ioctl(dev->ctx, MS_NULL, MS_IO_BLKDEV_CMD_INIT, MS_NULL) < 0) {
        dstatus = STA_NOINIT;
    } else {
        dstatus = 0U;
    }

    return dstatus;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
    void *pdrv      /* Physical drive nmuber to identify the drive */
)
{
    ms_io_device_t *dev = (ms_io_device_t *)pdrv;
    DSTATUS dstatus;
    ms_uint32_t ms_status;

    if (dev->drv->ops->ioctl(dev->ctx, MS_NULL, MS_IO_BLKDEV_CMD_STATUS, &ms_status) < 0) {
        dstatus = STA_NOINIT;

    } else {
        switch (ms_status) {
        case MS_IO_BLKDEV_STA_OK:
            dstatus = 0U;
            break;

        case MS_IO_BLKDEV_STA_NOINIT:
            dstatus = STA_NOINIT;
            break;

        case MS_IO_BLKDEV_STA_NODISK:
            dstatus = STA_NODISK;
            break;

        case MS_IO_BLKDEV_STA_PROTECT:
            dstatus = STA_PROTECT;
            break;

        default:
            dstatus = STA_NOINIT;
            break;
        }
    }

    return dstatus;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
    void *pdrv,     /* Physical drive nmuber to identify the drive */
    BYTE *buff,     /* Data buffer to store read data */
    LBA_t sector,   /* Start sector in LBA */
    UINT count      /* Number of sectors to read */
)
{
    ms_io_device_t *dev = (ms_io_device_t *)pdrv;
    DRESULT dresult;

    if (dev->drv->ops->readblk(dev->ctx, MS_NULL, sector, count, buff) < 0) {
        dresult = RES_ERROR;
    } else {
        dresult = RES_OK;
    }

    return dresult;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
    void *pdrv,         /* Physical drive nmuber to identify the drive */
    const BYTE *buff,   /* Data to be written */
    LBA_t sector,       /* Start sector in LBA */
    UINT count          /* Number of sectors to write */
)
{
    ms_io_device_t *dev = (ms_io_device_t *)pdrv;
    DRESULT dresult;

    if (dev->drv->ops->writeblk(dev->ctx, MS_NULL, sector, count, buff) < 0) {
        dresult = RES_ERROR;
    } else {
        dresult = RES_OK;
    }

    return dresult;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
    void *pdrv,     /* Physical drive nmuber (0..) */
    BYTE cmd,       /* Control code */
    void *buff      /* Buffer to send/receive control data */
)
{
    ms_io_device_t *dev = (ms_io_device_t *)pdrv;
    DRESULT dresult = RES_OK;
    int ms_cmd;

    switch (cmd) {
    case CTRL_SYNC:
        ms_cmd = MS_IO_BLKDEV_CMD_SYNC;
        break;

    case GET_SECTOR_COUNT:
        ms_cmd = MS_IO_BLKDEV_CMD_SECT_NR;
        break;

    case GET_SECTOR_SIZE:
        ms_cmd = MS_IO_BLKDEV_CMD_SECT_SZ;
        break;

    case GET_BLOCK_SIZE:
        ms_cmd = MS_IO_BLKDEV_CMD_BLK_SZ;
        break;

    case CTRL_TRIM:
        ms_cmd = MS_IO_BLKDEV_CMD_TRIM;
        break;

    default:
        dresult = RES_ERROR;
        break;
    }

    if (dresult == RES_OK) {
        if (dev->drv->ops->ioctl(dev->ctx, MS_NULL, ms_cmd, buff) < 0) {
            dresult = RES_ERROR;
        }
    }

    return dresult;
}

#if FF_USE_LFN == 3 /* Dynamic memory allocation */

/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/

void* ff_memalloc ( /* Returns pointer to the allocated memory block (null if not enough core) */
    UINT msize      /* Number of bytes to allocate */
)
{
    return ms_kmalloc(msize);   /* Allocate a new memory block with POSIX API */
}

/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree (
    void* mblock    /* Pointer to the memory block to free (nothing to do if null) */
)
{
    (void)ms_kfree(mblock);   /* Free the memory block with POSIX API */
}

#endif

/*------------------------------------------------------------------------*/
/* Create a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to create a new
/  synchronization object for the volume, such as semaphore and mutex.
/  When a 0 is returned, the f_mount() function fails with FR_INT_ERR.
*/

int ff_cre_syncobj (    /* 1:Function succeeded, 0:Could not create the sync object */
    BYTE vol,           /* Corresponding volume (logical drive number) */
    FF_SYNC_t* sobj     /* Pointer to return the created sync object */
)
{
    int ret;

    if (ms_mutex_create("fat_lock", MS_WAIT_TYPE_PRIO, sobj) == MS_ERR_NONE) {
        ret = 1;
    } else {
        ret = 0;
    }

    return ret;
}

/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to delete a synchronization
/  object that created with ff_cre_syncobj() function. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_del_syncobj (    /* 1:Function succeeded, 0:Could not delete due to an error */
    FF_SYNC_t sobj      /* Sync object tied to the logical drive to be deleted */
)
{
    int ret;

    if (ms_mutex_destroy(sobj) == MS_ERR_NONE) {
        ret = 1;
    } else {
        ret = 0;
    }

    return ret;
}

/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (  /* 1:Got a grant to access the volume, 0:Could not get a grant */
    FF_SYNC_t sobj  /* Sync object to wait */
)
{
    int ret;

    if (ms_mutex_lock(sobj, FF_FS_TIMEOUT) == MS_ERR_NONE) {
        ret = 1;
    } else {
        ret = 0;
    }

    return ret;
}

/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
    FF_SYNC_t sobj  /* Sync object to be signaled */
)
{
    (void)ms_mutex_unlock(sobj);
}

/*------------------------------------------------------------------------*/
/* RTC function                                                           */
/*------------------------------------------------------------------------*/

DWORD get_fattime (void)
{
    time_t rawtime;
    struct tm rawtm;
    struct tm *ptm;

    time(&rawtime);

    ptm = localtime_r(&rawtime, &rawtm);

    return   ((DWORD)(ptm->tm_year - 80) << 25U)
           | ((DWORD)(ptm->tm_mon + 1) << 21U)
           | ((DWORD)(ptm->tm_mday) << 16U)
           | ((DWORD)(ptm->tm_hour) << 11U)
           | ((DWORD)(ptm->tm_min) << 5U)
           | ((DWORD)(ptm->tm_sec / 2U));
}
