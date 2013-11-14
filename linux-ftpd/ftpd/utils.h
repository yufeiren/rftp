/*--------------------------------------------------------------- 
 * Copyright (c) 2011                              
 * BNL            
 * All Rights Reserved.                                           
 *--------------------------------------------------------------- 
 * Permission is hereby granted, free of charge, to any person    
 * obtaining a copy of this software (Iperf) and associated       
 * documentation files (the "Software"), to deal in the Software  
 * without restriction, including without limitation the          
 * rights to use, copy, modify, merge, publish, distribute,        
 * sublicense, and/or sell copies of the Software, and to permit     
 * persons to whom the Software is furnished to do
 * so, subject to the following conditions: 
 *
 *     
 * Redistributions of source code must retain the above 
 * copyright notice, this list of conditions and 
 * the following disclaimers. 
 *
 *     
 * Redistributions in binary form must reproduce the above 
 * copyright notice, this list of conditions and the following 
 * disclaimers in the documentation and/or other materials 
 * provided with the distribution. 
 * 
 *     
 * Neither the names of the University of Illinois, NCSA, 
 * nor the names of its contributors may be used to endorse 
 * or promote products derived from this Software without
 * specific prior written permission. 
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTIBUTORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, 
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
 * ________________________________________________________________
 *
 * utils.h
 * by Yufei Ren <renyufei83@gmail.com>
 * -------------------------------------------------------------------
 * Utility for development
 * 
 * byte_atoi from iperf source code  
 * ------------------------------------------------------------------- */

#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stdarg.h>
#include <locale.h>
#include <ctype.h>

/* Needed for Unix version of run_with_timeout. */
#include <signal.h>
#include <setjmp.h>

#include <sys/resource.h>

#include <linux/limits.h>

# ifndef countof
#  define countof(x) (sizeof (x) / sizeof ((x)[0]))
# endif

#define CMP1(p, c0) (tolower((p)[0]) == (c0) && (p)[1] == '\0')

#define CMP2(p, c0, c1) (tolower((p)[0]) == (c0)        \
                         && tolower((p)[1]) == (c1)     \
                         && (p)[2] == '\0')

#define CMP3(p, c0, c1, c2) (tolower((p)[0]) == (c0)    \
                     && tolower((p)[1]) == (c1)         \
                     && tolower((p)[2]) == (c2)         \
                     && (p)[3] == '\0')

/* Copy the data delimited with BEG and END to alloca-allocated
   storage, and zero-terminate it.  Arguments are evaluated only once,
   in the order BEG, END, PLACE.  */
#define BOUNDED_TO_ALLOCA(beg, end, place) do {	\
  const char *BTA_beg = (beg);			\
  int BTA_len = (end) - BTA_beg;		\
  char **BTA_dest = &(place);			\
  *BTA_dest = malloc (BTA_len + 1);		\
  memcpy (*BTA_dest, BTA_beg, BTA_len);		\
  (*BTA_dest)[BTA_len] = '\0';			\
} while (0)


typedef uint64_t max_size_t;

max_size_t transtotallen;		/* totally trans data */
max_size_t transcurrlen;

struct options {
	int    cbufnum;
	long   cbufsiz;
	long   maxbufpoolsiz;
	int    evbufnum;
	int    recvbufnum;
	int    rmtaddrnum;
	int    srvcomport;
	bool   usesendfile;
	bool   usesplice;
	long   devzerosiz;
	char   *ib_devname;
	int    rcstreamnum;	/* number of reliable connection */
	int    readernum;	/* number of reader if send data */
	int    writernum;	/* number of writer if recv data */
	char   *ioengine;
	long   disk_io_siz;	/* io size for disk io */
	bool   directio;
	int    rdma_cq_depth;
	int    rdma_qp_rq_depth;
	int    rdma_qp_sq_depth;
	int    wc_event_num;
	int    wc_thread_num;
	char   *ibaddr;
	struct sockaddr_in data_addr[32];
	int    data_addr_num;
};

/* Whole Network Data Transfer CPU usage is
 * (ru_end - ru_start) / (real_end - real_start) * 100%
 */
struct proc_rusage_time {
	struct rusage ru_start;
	struct rusage ru_end;
	struct timeval real_start;
	struct timeval real_end;

	double cpu_user;
	double cpu_sys;
	double cpu_total;
};

double byte_atof(const char *);
max_size_t byte_atoi(const char *);

bool file_exists_p (const char *);
char *read_whole_line (FILE *);
char *concat_strings (const char *, ...);

int parse_opt_addr(struct options *);

unsigned long long utime_since(struct timeval *, struct timeval *);

/* update parameters according to current setup */
void update_param(struct options *);

void cal_rusage(struct proc_rusage_time *self_ru);

/* thread */
void *anabw(void *);

#ifdef __cplusplus
} /* end extern "C" */
#endif

#endif
