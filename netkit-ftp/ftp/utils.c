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
 * utils.c
 * by Yufei Ren <renyufei83@gmail.com>
 * -------------------------------------------------------------------
 * Utility for development
 * 
 * Partily from wget v0.12 init.ch etc
 * ------------------------------------------------------------------- */

#include "utils.h"

const long kKilo_to_Unit = 1024;
const long kMega_to_Unit = 1024 * 1024;
const long kGiga_to_Unit = 1024 * 1024 * 1024;

const long kkilo_to_Unit = 1000;
const long kmega_to_Unit = 1000 * 1000;
const long kgiga_to_Unit = 1000 * 1000 * 1000;

/* Does FILENAME exist?  This is quite a lousy implementation, since
   it supplies no error codes -- only a yes-or-no answer.  Thus it
   will return that a file does not exist if, e.g., the directory is
   unreadable.  I don't mind it too much currently, though.  The
   proper way should, of course, be to have a third, error state,
   other than true/false, but that would introduce uncalled-for
   additional complexity to the callers.  */
bool
file_exists_p (const char *filename)
{
#ifdef HAVE_ACCESS
  return access (filename, F_OK) >= 0;
#else
  struct stat buf;
  return stat (filename, &buf) >= 0;
#endif
}


/* Read a line from FP and return the pointer to freshly allocated
   storage.  The storage space is obtained through malloc() and should
   be freed with free() when it is no longer needed.

   The length of the line is not limited, except by available memory.
   The newline character at the end of line is retained.  The line is
   terminated with a zero character.

   After end-of-file is encountered without anything being read, NULL
   is returned.  NULL is also returned on error.  To distinguish
   between these two cases, use the stdio function ferror().  */

char *
read_whole_line (FILE *fp)
{
  int length = 0;
  int bufsize = 128;
  char *line = malloc (bufsize);

  while (fgets (line + length, bufsize - length, fp))
    {
      length += strlen (line + length);
      if (length == 0)
        /* Possible for example when reading from a binary file where
           a line begins with \0.  */
        continue;

      if (line[length - 1] == '\n')
        break;

      /* fgets() guarantees to read the whole line, or to use up the
         space we've given it.  We can double the buffer
         unconditionally.  */
      bufsize <<= 1;
      line = realloc (line, bufsize);
    }
  if (length == 0 || ferror (fp))
    {
      free (line);
      return NULL;
    }
  if (length + 1 < bufsize)
    /* Relieve the memory from our exponential greediness.  We say
       `length + 1' because the terminating \0 is not included in
       LENGTH.  We don't need to zero-terminate the string ourselves,
       though, because fgets() does that.  */
    line = realloc (line, length + 1);
  return line;
}


/* Concatenate the NULL-terminated list of string arguments into
   freshly allocated space.  */

char *
concat_strings (const char *str0, ...)
{
  va_list args;
  int saved_lengths[5];         /* inspired by Apache's apr_pstrcat */
  char *ret, *p;

  const char *next_str;
  int total_length = 0;
  size_t argcount;

  /* Calculate the length of and allocate the resulting string. */

  argcount = 0;
  va_start (args, str0);
  for (next_str = str0; next_str != NULL; next_str = va_arg (args, char *))
    {
      int len = strlen (next_str);
      if (argcount < countof (saved_lengths))
        saved_lengths[argcount++] = len;
      total_length += len;
    }
  va_end (args);
  p = ret = malloc (total_length + 1);

  /* Copy the strings into the allocated space. */

  argcount = 0;
  va_start (args, str0);
  for (next_str = str0; next_str != NULL; next_str = va_arg (args, char *))
    {
      int len;
      if (argcount < countof (saved_lengths))
        len = saved_lengths[argcount++];
      else
        len = strlen (next_str);
      memcpy (p, next_str, len);
      p += len;
    }
  va_end (args);
  *p = '\0';

  return ret;
}


/* -------------------------------------------------------------------
 * byte_atof
 *
 * Given a string of form #x where # is a number and x is a format
 * character listed below, this returns the interpreted integer.
 * Gg, Mm, Kk are giga, mega, kilo respectively
 * ------------------------------------------------------------------- */

double byte_atof( const char *inString ) {
    double theNum;
    char suffix = '\0';

    assert( inString != NULL );

    /* scan the number and any suffices */
    sscanf( inString, "%lf%c", &theNum, &suffix );

    /* convert according to [Gg Mm Kk] */
    switch ( suffix ) {
        case 'G':  theNum *= kGiga_to_Unit;  break;
        case 'M':  theNum *= kMega_to_Unit;  break;
        case 'K':  theNum *= kKilo_to_Unit;  break;
        case 'g':  theNum *= kgiga_to_Unit;  break;
        case 'm':  theNum *= kmega_to_Unit;  break;
        case 'k':  theNum *= kkilo_to_Unit;  break;
        default: break;
    }
    return theNum;
} /* end byte_atof */

/* -------------------------------------------------------------------
 * byte_atoi
 *
 * Given a string of form #x where # is a number and x is a format
 * character listed below, this returns the interpreted integer.
 * Gg, Mm, Kk are giga, mega, kilo respectively
 * ------------------------------------------------------------------- */

max_size_t byte_atoi( const char *inString ) {
    double theNum;
    char suffix = '\0';

    assert( inString != NULL );

    /* scan the number and any suffices */
    sscanf( inString, "%lf%c", &theNum, &suffix );

    /* convert according to [Gg Mm Kk] */
    switch ( suffix ) {
        case 'G':  theNum *= kGiga_to_Unit;  break;
        case 'M':  theNum *= kMega_to_Unit;  break;
        case 'K':  theNum *= kKilo_to_Unit;  break;
        case 'g':  theNum *= kgiga_to_Unit;  break;
        case 'm':  theNum *= kmega_to_Unit;  break;
        case 'k':  theNum *= kkilo_to_Unit;  break;
        default: break;
    }
    return (max_size_t) theNum;
} /* end byte_atoi */

void
update_param(struct options *opt)
{
	opt->cbufnum = opt->maxbufpoolsiz / opt->cbufsiz;
	opt->rmtaddrnum = opt->cbufnum;

	if (opt->cbufnum < opt->rdma_qp_sq_depth)
		opt->evbufnum = opt->cbufnum;
	else
		opt->evbufnum = opt->rdma_qp_sq_depth - 1;

	if (opt->cbufnum < opt->rdma_qp_rq_depth)
		opt->recvbufnum = opt->cbufnum;
	else
		opt->recvbufnum = opt->rdma_qp_rq_depth - 1;

	if ((opt->evbufnum + opt->recvbufnum) < opt->rdma_cq_depth)
		opt->wc_event_num = opt->evbufnum + opt->recvbufnum;
	else
		opt->wc_event_num = opt->rdma_cq_depth - 1;

	return;
}

/* from fio */
unsigned long long
utime_since(struct timeval *s, struct timeval *e)
{
        long sec, usec;
        unsigned long long ret;

        sec = e->tv_sec - s->tv_sec;
        usec = e->tv_usec - s->tv_usec;
        if (sec > 0 && usec < 0) {
                sec--;
                usec += 1000000;
        }

        /*
         * time warp bug on some kernels?
         */
        if (sec < 0 || (sec == 0 && usec < 0))
                return 0;

        ret = sec * 1000000ULL + usec;

	return ret;
}

void
cal_rusage(struct proc_rusage_time *self_ru)
{
	uint64_t u_usec, s_usec, r_usec;

	u_usec = utime_since(&(self_ru->ru_start.ru_utime), &(self_ru->ru_end.ru_utime));
	s_usec = utime_since(&(self_ru->ru_start.ru_stime), &(self_ru->ru_end.ru_stime));
	r_usec = utime_since(&(self_ru->real_start), &(self_ru->real_end));

	self_ru->cpu_user = (double) u_usec * (double) 100 / (double) r_usec;
	self_ru->cpu_sys = (double) s_usec * (double) 100 / (double) r_usec;
	self_ru->cpu_total = self_ru->cpu_user + self_ru->cpu_sys;

	return;
}


void *
anabw(void *arg)
{
	long prelen = 0;
	long trans; /* bits */
	
	double percent;
	
	int banlen = 30;
	int i;
	
	for ( ; transcurrlen < transtotallen; ) {
		sleep(1);
		trans = (transcurrlen - prelen) * 8;
		
		/* percentage */
		percent = (double) transcurrlen / (double) transtotallen;
		
		printf(" ");
		printf("%6.2f", percent * 100);
		printf("%%");
		
		/* draw banner */
		printf(" ");
		printf("[");
		for (i = 0; i < banlen; i ++) {
			if (i < percent * banlen)
				printf("=");
			else
				printf(" ");
		}
		printf("]");
		
		/* total bytes */
		printf("%15ld", transcurrlen);
		
		/* bandwidth */
		printf("  ");
		
		if (trans < kkilo_to_Unit)
			printf("%7.2f Bits/sec", (double) trans);
		else if (trans < kmega_to_Unit)
			printf("%7.2f KBits/sec", \
				(double) trans / kkilo_to_Unit);
		else if (trans < kgiga_to_Unit)
			printf("%7.2f MBits/sec", \
				(double) trans / kmega_to_Unit);
		else
			printf("%7.2f GBits/sec", \
				(double) trans / kgiga_to_Unit);
		
		printf("\r");
		fflush(stdout);
		
		prelen = transcurrlen;
	}
	printf("\n");
	
	pthread_exit(NULL);
}

int
parse_opt_addr(struct options *opt)
{
  /* ibaddr = ip1, ip2, ip3, ..., ipN */

  char *start;
  char *end;
  int sep = ',';
  char buf[128];
  int ret;

	opt->data_addr_num = 0;
	if (opt->ibaddr == NULL)
		return 0;

  start = end = opt->ibaddr;
  while (start != NULL) {
    memset(buf, '\0', 128);
	end = strchr(start, sep);
	if (end == NULL) {
	  strcpy(buf, start);
	  ret = inet_pton(AF_INET, buf, &opt->data_addr[opt->data_addr_num]);
	  if (ret <= 0) {
	    fprintf(stderr, "illegal addr format: %s", buf);
	    return -1;
	  }
	  /*	  opt->data_addr[opt->data_addr_num].sin_addr.s_addr =	\
		  inet_addr(buf); */
	  opt->data_addr_num ++;
	  break;
	} else {
	  memcpy(buf, start, end - start);
	  ret = inet_pton(AF_INET, buf, &opt->data_addr[opt->data_addr_num]);
	  if (ret <= 0) {
	    fprintf(stderr, "illegal addr format: %s", buf);
	    return -1;
	  }
	  /* opt->data_addr[opt->data_addr_num].sin_addr.s_addr =	\
	     inet_addr(buf); */
	  opt->data_addr_num ++;
	  start = end + 1;
	}
  }

	return 0;
}

