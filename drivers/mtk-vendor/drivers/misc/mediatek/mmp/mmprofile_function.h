/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */


#ifndef __MMPROFILE_FUNCTION_H__
#define __MMPROFILE_FUNCTION_H__

#include "mmprofile.h"
#include "mmprofile_static_event.h"


#if IS_ENABLED(CONFIG_MMPROFILE)
mmp_event mmprofile_register_event(mmp_event parent, const char *name);
mmp_event mmprofile_find_event(mmp_event parent, const char *name);
void mmprofile_enable_event(mmp_event event, long enable);
void mmprofile_enable_ftrace_event(mmp_event event, long enable, long ftrace);
void mmprofile_enable_event_recursive(mmp_event event, long enable);
void mmprofile_enable_ftrace_event_recursive(mmp_event event, long enable,
	long ftrace);
long mmprofile_query_enable(mmp_event event);
void mmprofile_log(mmp_event event, enum mmp_log_type type);
void mmprofile_log_ex(mmp_event event, enum mmp_log_type type,
	unsigned long data1, unsigned long data2);
long mmprofile_log_meta(mmp_event event, enum mmp_log_type type,
	struct mmp_metadata_t *p_meta_data);
long mmprofile_log_meta_string(mmp_event event, enum mmp_log_type type,
	const char *str);
long mmprofile_log_meta_string_ex(mmp_event event, enum mmp_log_type type,
		unsigned long data1, unsigned long data2, const char *str);
long mmprofile_log_meta_structure(mmp_event event, enum mmp_log_type type,
	struct mmp_metadata_structure_t *p_meta_data);
long mmprofile_log_meta_bitmap(mmp_event event, enum mmp_log_type type,
	struct mmp_metadata_bitmap_t *p_meta_data);
long mmprofile_log_meta_yuv_bitmap(mmp_event event, enum mmp_log_type type,
	struct mmp_metadata_bitmap_t *p_meta_data);
void mmprofile_start(int start);
void mmprofile_enable(int enable);
unsigned int mmprofile_get_dump_size(void);
void mmprofile_get_dump_buffer(unsigned int start, unsigned long *p_addr,
	unsigned int *p_size);
/* !CONFIG_MMPROFILE stubs live in mmprofile.h (fire: avoid redefinition) */
#endif

#endif
