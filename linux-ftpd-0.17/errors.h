/*
 *  Copyright 2005, China Union Pay Co., Ltd.  All right reserved.
 *
 *  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF CHINA UNION PAY CO.,
 *  LTD.  THE CONTENTS OF THIS FILE MAY NOT BE DISCLOSED TO THIRD
 *  PARTIES, COPIED OR DUPLICATED IN ANY FORM, IN WHOLE OR IN PART,
 *  WITHOUT THE PRIOR WRITTEN PERMISSION OF CHINA UNION PAY CO., LTD.
 *
 *  $Id: errors.h,v 1.2 2009/12/06 04:55:13 liqin Exp $ *
 *    
 *
 *  Edit History:
 *
 *    2005/09/23 - Created by Ren Yufei.
 *
 */

#ifndef __ERRORS_H
#define __ERRORS_H

#include <errno.h>

/*
 * 调试工具：编译时增加参数 -DDEBUG。
 */

#ifdef DEBUG
/*    void _Assert(char *, unsigned);       原形 */
    #define ASSERT(condition)          \
        if (condition)                     \
            NULL;                      \
        else                               \
            assert(__FILE__, __LINE__)
#else
    #define ASSERT(condition)          NULL
#endif /* DEBUG */


#ifdef DEBUG
    #define DPRINTF(arg)    printf arg
#else
    #define DPRINTF(arg)
#endif /* DEBUG */


#ifdef DEBUG
	#define DDP_HEX(msg,buf,len)       do { \
			int _itmp = len; \
			char *_tmpbuf = buf; \
			fprintf(stderr, "%s", msg); \
			do { \
				fprintf(stderr, " %x%x", \
					*(_tmpbuf)>>4, *(_tmpbuf++)&0x0F); \
			} while(--_itmp > 0); \
			fprintf(stderr, "\t(HEX)\n"); \
		} while (0)
	#define DDP_ASC(msg,buf,len)       do { \
			int _itmp = len; \
			char *_tmpbuf = buf; \
			fprintf(stderr, "%s", msg); \
			do { \
				fprintf(stderr, " %c", *(_tmpbuf++)); \
			} while(--_itmp > 0); \
			fprintf(stderr, "\t(ASC)\n"); \
		} while (0)
	#define DDP_BCD(msg,buf,len)       do { \
			int _itmp = len; \
			char *_tmpbuf = buf; \
			fprintf(stderr, "%s", msg); \
			do { \
				fprintf(stderr, " %x %x", \
					*(_tmpbuf)>>4, *(_tmpbuf++)&0x0F); \
			} while(--_itmp > 0); \
			fprintf(stderr, "\t(BCD)\n"); \
		} while (0)
#else
    #define DDP_HEX(msg,buf,len)
    #define DDP_ASC(msg,buf,len)
    #define DDP_BCD(msg,buf,len)
#endif /* DEBUG */


#endif /* __ERRORS_H */
