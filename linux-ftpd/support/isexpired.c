#ifdef USE_SHADOW

#include <shadow.h>
#include <time.h>
#include "isexpired.h"

/*
 * Check password aging info when using shadow passwords.  Returns:
 *  3 - after account expiration date
 *  2 - expired password not changed for too long
 *  1 - password expired, must be changed
 *  0 - success, or no shadow password information at all
 *
 * Written by Marek Michalkiewicz <marekm@i17linuxb.ists.pwr.wroc.pl>,
 * public domain.
 */
int
isexpired(const struct spwd *sp)
{
	long now, change;

	if (!sp)
		return 0;

	now = (time(NULL) / (24L * 3600L));

	if (sp->sp_expire > 0 && now >= sp->sp_expire)
		return 3;

	if (sp->sp_lstchg > 0 && sp->sp_max > 0) {
		change = sp->sp_lstchg + sp->sp_max;

		if (sp->sp_inact >= 0 && now >= change + sp->sp_inact)
			return 2;

		if (now >= change)
			return 1;
	}

	if (sp->sp_lstchg == 0)
		return 1;

	return 0;
}
#endif
