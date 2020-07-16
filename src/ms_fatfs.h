/*
 * Copyright (c) 2015-2020 ACOINFO Co., Ltd.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ms_fatfs.h FATFS implement.
 *
 * Author: Jiao.jinxing <jiaojinxing@acoinfo.com>
 *
 */

#ifndef MS_FATFS_H
#define MS_FATFS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MS_FATFS_NAME       "fatfs"

ms_err_t ms_fatfs_register(void);

#ifdef __cplusplus
}
#endif

#endif /* MS_FATFS_H */
