// SPDX-License-Identifier: GPL-2.0
/*
 * fire 6.12: touch proximity needs the MTK alsps sensor hub, which is not
 * ported. Keep the entry points the core calls and report the feature
 * as off.
 */
#include "focaltech_core.h"

int fts_enter_proximity_mode(int mode)
{
	return -EOPNOTSUPP;
}

int fts_proximity_recovery(struct fts_ts_data *ts_data)
{
	return 0;
}

int fts_proximity_readdata(struct fts_ts_data *ts_data)
{
	return -EPERM;
}

int fts_proximity_suspend(void)
{
	return -EPERM;
}

int fts_proximity_resume(void)
{
	return 0;
}

int fts_proximity_init(void)
{
	return 0;
}

int fts_proximity_exit(void)
{
	return 0;
}
