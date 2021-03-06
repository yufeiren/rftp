/*
 * Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* 
 * From: @(#)ftpd.c	8.4 (Berkeley) 4/16/94
 * From: NetBSD: ftpd.c,v 1.15 1995/06/03 22:46:47 mycroft Exp
 * From: OpenBSD: ftpd.c,v 1.26 1996/12/07 09:00:22 bitblt Exp
 * From: OpenBSD: ftpd.c,v 1.35 1997/05/01 14:45:37 deraadt Exp
 * From: OpenBSD: ftpd.c,v 1.54 1999/04/29 21:38:43 downsj Exp
 */
char ftpd_rcsid[] = 
  "$Id: ftpd.c,v 1.20 2000/07/23 03:34:56 dholland Exp $";

char copyright[] =
  "@(#) Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994\n"
  "     The Regents of the University of California.  All rights reserved.\n";

/*
 * FTP server.
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define	FTP_NAMES
#include <arpa/ftp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h> /* for CHAR_BIT */
#include <netdb.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <vis.h>
#include <unistd.h>
#include <utmp.h>

#ifndef __linux__
#include <util.h>
#include <err.h>
#else
#include <grp.h>       /* for initgroups() */
/* #include <sys/file.h>  * for L_SET et al. * <--- not used? */
/*typedef int64_t quad_t;*/
typedef unsigned int useconds_t;
#endif

#include "../version.h"

/* glibc 2.[01] does not have TCP_CORK, so define it here */ 
#if __GLIBC__ >= 2 && defined(__linux__) && !defined(TCP_CORK)
#define TCP_CORK 3
#endif

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifdef USE_SHADOW
#include <shadow.h>
#include "isexpired.h"
#endif

#if defined(TCPWRAPPERS)
#include <tcpd.h>
#endif	/* TCPWRAPPERS */

#if defined(SKEY)
#include <skey.h>
#endif

#include "pathnames.h"
#include "extern.h"

#include "errors.h"

#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include "rdma.h"
#include "init.h"
#include "utils.h"

static char versionpre[] = "Version 0.15/Linux";
static char version[sizeof(versionpre)+sizeof(pkg)];


extern	off_t restart_point;
extern	char cbuf[];

struct	sockaddr_in server_addr;
struct	sockaddr_in ctrl_addr;
struct	sockaddr_in data_source;
struct	sockaddr_in data_dest;
struct	sockaddr_in his_addr;
struct	sockaddr_in pasv_addr;

int	daemon_mode = 0;
int	data;
jmp_buf	errcatch, urgcatch;
int	logged_in;
struct	passwd *pw;
#ifdef USE_SHADOW
struct	spwd *spw = NULL;
#endif
int	debug = 0;
int	timeout = 900;    /* timeout after 15 minutes of inactivity */
int	maxtimeout = 7200;/* don't allow idle time to be set beyond 2 hours */
int	logging;
int	high_data_ports = 0;
int	anon_only = 0;
int	multihome = 0;
int	guest;
int	stats;
int	statfd = -1;
int	portcheck = 1;
int	dochroot;
int	type;
int	form;
int	stru;			/* avoid C keyword */
int	mode;
int	doutmp = 0;		/* update utmp file */
int	usedefault = 1;		/* for data transfers */
int	pdata = -1;		/* for passive mode */
sig_atomic_t transflag;
off_t	file_size;
off_t	byte_count;
#if !defined(CMASK) || CMASK == 0
#undef CMASK
#define CMASK 027
#endif
int	defumask = CMASK;		/* default umask value */
char	tmpline[7];
char	hostname[MAXHOSTNAMELEN];
char	remotehost[MAXHOSTNAMELEN];
char	dhostname[MAXHOSTNAMELEN];
char	*guestpw;
static char ttyline[20];
char	*tty = ttyline;		/* for klogin */
static struct utmp utmp;	/* for utmp */

#if defined(TCPWRAPPERS)
int	allow_severity = LOG_INFO;
int	deny_severity = LOG_NOTICE;
#endif	/* TCPWRAPPERS */

#if defined(KERBEROS)
int	notickets = 1;
char	*krbtkfile_env = NULL;
#endif

char	*ident = NULL;

rdma_cb *dc_cb;
int rdma_debug = 0;
struct acptq acceptedTqh;

struct options opt;

/* it is not safe to create directory simutanuously  */
pthread_mutex_t dir_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t transcurrlen_mutex = PTHREAD_MUTEX_INITIALIZER;

int is_disconnected_event = 0;

struct proc_rusage_time self_ru;

struct timespec total_rd_cpu;
struct timespec total_rd_real;
struct timespec total_net_cpu;
struct timespec total_net_real;
struct timespec total_wr_cpu;
struct timespec total_wr_real;

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

int	swaitmax = SWAITMAX;
int	swaitint = SWAITINT;

#ifdef HASSETPROCTITLE
char	proctitle[BUFSIZ];	/* initial part of title */
#endif /* HASSETPROCTITLE */

#define LOGCMD(cmd, file) \
	if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s", cmd, \
		*(file) == '/' ? "" : curdir(), file);
#define LOGCMD2(cmd, file1, file2) \
	 if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s %s%s", cmd, \
		*(file1) == '/' ? "" : curdir(), file1, \
		*(file2) == '/' ? "" : curdir(), file2);
#define LOGBYTES(cmd, file, cnt) \
	if (logging > 1) { \
		if (cnt == (off_t)-1) \
		    syslog(LOG_INFO,"%s %s%s", cmd, \
			*(file) == '/' ? "" : curdir(), file); \
		else \
		    syslog(LOG_INFO, "%s %s%s = %qd bytes", cmd, \
			*(file) == '/' ? "" : curdir(), file, (quad_t)(cnt)); \
	}

static void	 ack __P((const char *));
static void	 myoob __P((int));
static int	 checkuser __P((const char *, const char *));
static FILE	*dataconn __P((const char *, off_t, const char *));
static int	 rdmadataconn __P((const char *, off_t, const char *));
static void	 dolog __P((struct sockaddr_in *));
static const char	*curdir __P((void));
static void	 end_login __P((void));
static FILE	*getdatasock __P((const char *));
static int	guniquefd __P((const char *, char **));
static void	 lostconn __P((int));
static void	 sigquit __P((int));
static int	 receive_data __P((FILE *, FILE *));
static int	 rreceive_data __P((FILE *));
static void	 replydirname __P((const char *, const char *));
static void	 send_data __P((FILE *, FILE *, off_t, off_t, int));
static void	 rsend_data __P((FILE *, FILE *, off_t, off_t, int));
static struct passwd *
		 sgetpwnam __P((const char *));
static char	*sgetsave __P((char *));
static void	 reapchild __P((int));

#if defined(TCPWRAPPERS)
static int	 check_host __P((struct sockaddr_in *));
#endif

void logxfer __P((const char *, off_t, time_t));

#ifdef __linux__
static void warnx(const char *format, ...) 
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "ftpd: ");
	vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}
#endif /* __linux__ */

static const char *
curdir(void)
{
	static char path[MAXPATHLEN+1];	/* path + '/' */

	if (getcwd(path, sizeof(path)-1) == NULL)
		return ("");
	if (path[1] != '\0')		/* special case for root dir. */
		strcat(path, "/");
	/* For guest account, skip / since it's chrooted */
	return (guest ? path+1 : path);
}

int
main(int argc, char *argv[], char **envp)
{
	int ch, on = 1, tos;
	socklen_t addrlen;
	char *cp, line[LINE_MAX];
	FILE *fd;
	const char *argstr = "AdDhlMSt:T:u:UvVP";
	struct hostent *hp;

#ifdef __linux__
	initsetproctitle(argc, argv, envp);
	srandom(time(NULL)^(getpid()<<8));

	/*
	 * Get the version number from pkg[] and put it in version[]
	 * (we do this like this because pkg[] gets updated by the build
	 * environment)
	 */
	{
	   char *tmp, *tmp2, tbuf[sizeof(pkg)+1];
	   strcpy(tbuf, pkg);
	   strcpy(version, versionpre);
	   tmp = strchr(tbuf, '-');
	   if (tmp) {
	      tmp2 = strchr(tmp, ' ');
	      if (tmp2) *tmp2=0;
	      strcat(version, tmp);
	   }
	}
#endif

	tzset();	/* in case no timezone database in ~ftp */

	/* set this here so klogin can use it... */
	(void)snprintf(ttyline, sizeof(ttyline), "ftp%d", getpid());

	while ((ch = getopt(argc, argv, argstr)) != -1) {
		switch (ch) {
		case 'A':
			anon_only = 1;
			break;

		case 'd':
			debug = 1;
			break;

		case 'D':
			daemon_mode = 1;
			break;

		case 'P':
			portcheck = 0;
			break;

		case 'h':
			high_data_ports = 1;
			break;

		case 'l':
			logging++;	/* > 1 == extra logging */
			break;

		case 'M':
			multihome = 1;
			break;

		case 'S':
			stats = 1;
			break;

		case 't':
			timeout = atoi(optarg);
			if (maxtimeout < timeout)
				maxtimeout = timeout;
			break;

		case 'T':
			maxtimeout = atoi(optarg);
			if (timeout > maxtimeout)
				timeout = maxtimeout;
			break;

		case 'u':
		    {
			long val = 0;

			val = strtol(optarg, &optarg, 8);
			if (*optarg != '\0' || val < 0 || (val & ~ACCESSPERMS))
				warnx("bad value for -u");
			else
				defumask = val;
			break;
		    }

		case 'U':
			doutmp = 1;
			break;

		case 'v':
			debug = 1;
			break;

		case 'V':
			printf("%s\n", version);
			exit(0);

		default:
			warnx("unknown flag -%c ignored", optopt);
			break;
		}
	}

	(void) freopen(_PATH_DEVNULL, "w", stderr);

	/*
	 * LOG_NDELAY sets up the logging connection immediately,
	 * necessary for anonymous ftp's that chroot and can't do it later.
	 */
#ifndef LOG_FTP
#define LOG_FTP LOG_DAEMON
#endif
	openlog("rftpd", LOG_PID | LOG_NDELAY, LOG_FTP);
	if (daemon_mode) {
		int ctl_sock, fd2;
		struct servent *sv;
		/*
		 * Detach from parent.
		 */
		if (daemon(1, 1) < 0) {
			syslog(LOG_ERR, "failed to become a daemon");
			exit(1);
		}
		(void) signal(SIGCHLD, reapchild);
		/*
		 * Get port number for ftp/tcp.
		 * resolve /etc/services		 
		 */
		sv = getservbyname("ftp", "tcp");
		if (sv == NULL) {
			syslog(LOG_ERR, "getservbyname for ftp failed");
			exit(1);
		}

		/*
		 * Open a socket, bind it to the FTP port, and start
		 * listening.
		 */
		ctl_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (ctl_sock < 0) {
			syslog(LOG_ERR, "control socket: %m");
			exit(1);
		}
		if (setsockopt(ctl_sock, SOL_SOCKET, SO_REUSEADDR,
		    (char *)&on, sizeof(on)) < 0)
			syslog(LOG_ERR, "control setsockopt: %m");;
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = INADDR_ANY;
/*		server_addr.sin_addr.s_addr = inet_addr("192.168.1.2"); */
		server_addr.sin_port = sv->s_port;
/*		server_addr.sin_port = htons(19987); */
		
		initialize();
		/* set new listening port */
		server_addr.sin_port = htons((short)opt.srvcomport);
		
		if (bind(ctl_sock, (struct sockaddr *)&server_addr,
			 sizeof(server_addr))) {
			syslog(LOG_ERR, "control bind: %m");
			exit(1);
		}

		if (listen(ctl_sock, 32) < 0) {
			syslog(LOG_ERR, "control listen: %m");
			exit(1);
		}

		/*
		 * Loop forever accepting connection requests and forking off
		 * children to handle them.
		 */
		while (1) {
			addrlen = sizeof(his_addr);
			fd2 = accept(ctl_sock, (struct sockaddr *)&his_addr,
				    &addrlen);
			if (fork() == 0) {
				/* child */
				(void) dup2(fd2, 0);
				(void) dup2(fd2, 1);
				close(ctl_sock);
				break;
			}
			close(fd2);
		}

#if defined(TCPWRAPPERS)
		/* ..in the child. */
		if (!check_host(&his_addr))
			exit(1);
#endif	/* TCPWRAPPERS */
	} else {
		addrlen = sizeof(his_addr);
		if (getpeername(0, (struct sockaddr *)&his_addr,
				&addrlen) < 0) {
			syslog(LOG_ERR, "getpeername (%s): %m", argv[0]);
			exit(1);
		}
	}

	(void) signal(SIGHUP, sigquit);
	(void) signal(SIGINT, sigquit);
	(void) signal(SIGQUIT, sigquit);
	(void) signal(SIGTERM, sigquit);
	(void) signal(SIGPIPE, lostconn);
	(void) signal(SIGCHLD, SIG_IGN);
	if (signal(SIGURG, myoob) == SIG_ERR)
		syslog(LOG_ERR, "signal: %m");

	addrlen = sizeof(ctrl_addr);
	if (getsockname(0, (struct sockaddr *)&ctrl_addr, &addrlen) < 0) {
		syslog(LOG_ERR, "getsockname (%s): %m", argv[0]);
		exit(1);
	}
#ifdef IP_TOS
	tos = IPTOS_LOWDELAY;
	if (setsockopt(0, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int)) < 0)
		syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
#endif
	data_source.sin_port = htons(ntohs(ctrl_addr.sin_port) - 1);

	/* Try to handle urgent data inline */
#ifdef SO_OOBINLINE
	if (setsockopt(0, SOL_SOCKET, SO_OOBINLINE, (char *)&on, sizeof(on)) < 0)
		syslog(LOG_ERR, "setsockopt: %m");
#endif

#ifdef	F_SETOWN
	if (fcntl(fileno(stdin), F_SETOWN, getpid()) == -1)
		syslog(LOG_ERR, "fcntl F_SETOWN: %m");
#endif
	dolog(&his_addr);
	/*
	 * Set up default state
	 */
	data = -1;
	type = TYPE_A;
	form = FORM_N;
	stru = STRU_F;
	mode = MODE_S;
	tmpline[0] = '\0';

	/* If logins are disabled, print out the message. */
	if ((fd = fopen(_PATH_NOLOGIN,"r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(530, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
		reply(530, "System not available.");
		exit(0);
	}
	if ((fd = fopen(_PATH_FTPWELCOME, "r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(220, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
		/* reply(220,) must follow */
	}
	(void) gethostname(hostname, sizeof(hostname));

	/* Make sure hostname is fully qualified. */
	hp = gethostbyname(hostname);
	if (hp != NULL)
		strcpy(hostname, hp->h_name);

	if (multihome) {
		hp = gethostbyaddr((char *) &ctrl_addr.sin_addr,
		    sizeof (struct in_addr), AF_INET);
		if (hp != NULL) {
			strcpy(dhostname, hp->h_name);
		} else {
			/* Default. */
			strcpy(dhostname, inet_ntoa(ctrl_addr.sin_addr));
		}
	}
	reply(220, "%s FTP server (%s) ready.",
	      (multihome ? dhostname : hostname), version);
	(void) setjmp(errcatch);
	for (;;)
		(void) yyparse();
	/* NOTREACHED */
}

/*
 * Signal handlers.
 */

static void
lostconn(int signo)
{
	(void)signo;

	if (debug)
		syslog(LOG_DEBUG, "lost connection");
	dologout(-1);
}

static void sigquit(int signo)
{
	syslog(LOG_ERR, "got signal %s", strsignal(signo));

	dologout(-1);
}

/*
 * Helper function for sgetpwnam().
 */
static char * sgetsave(char *s)
{
	char *new = malloc((unsigned) strlen(s) + 1);

	if (new == NULL) {
		perror_reply(421, "Local resource failure: malloc");
		dologout(1);
		/* NOTREACHED */
	}
	(void) strcpy(new, s);
	return (new);
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 */
static struct passwd *sgetpwnam(const char *name)
{
	static struct passwd save;
	struct passwd *p;

	if ((p = getpwnam(name)) == NULL)
		return (p);
#ifdef USE_SHADOW
	if ((spw = getspnam(name)) != NULL)
		p->pw_passwd = spw->sp_pwdp;
	endspent();  /* ? */
#endif
	if (save.pw_name) {
		free(save.pw_name);
		memset(save.pw_passwd, 0, strlen(save.pw_passwd));
		free(save.pw_passwd);
		free(save.pw_gecos);
		free(save.pw_dir);
		free(save.pw_shell);
	}
	save = *p;
	save.pw_name = sgetsave(p->pw_name);
	save.pw_passwd = sgetsave(p->pw_passwd);
	save.pw_gecos = sgetsave(p->pw_gecos);
	save.pw_dir = sgetsave(p->pw_dir);
	save.pw_shell = sgetsave(p->pw_shell);
	return (&save);
}

static int login_attempts;	/* number of failed login attempts */
static int askpasswd;		/* had user command, ask for passwd */
static char curname[16];	/* current USER name */

/*
 * USER command.
 * Sets global passwd pointer pw if named account exists and is acceptable;
 * sets askpasswd if a PASS command is expected.  If logged in previously,
 * need to reset state.  If name is "ftp" or "anonymous", the name is not in
 * _PATH_FTPUSERS, and ftp account exists, set guest and pw, then just return.
 * If account doesn't exist, ask for passwd anyway.  Otherwise, check user
 * requesting login privileges.  Disallow anyone who does not have a standard
 * shell as returned by getusershell().  Disallow anyone mentioned in the file
 * _PATH_FTPUSERS to allow people such as root and uucp to be avoided.
 */
void user(char *name)
{
	const char *cp, *shell;

	if (logged_in) {
		if (guest) {
			reply(530, "Can't change user from guest login.");
			return;
		} else if (dochroot) {
			reply(530, "Can't change user from chroot user.");
			return;
		}
		end_login();
	}

	guest = 0;
	if (strcmp(name, "ftp") == 0 || strcmp(name, "anonymous") == 0) {
		if (checkuser(_PATH_FTPUSERS, "ftp") ||
		    checkuser(_PATH_FTPUSERS, "anonymous"))
			reply(530, "User %s access denied.", name);
		else if ((pw = sgetpwnam("ftp")) != NULL) {
			guest = 1;
			askpasswd = 1;
			reply(331,
			    "Guest login ok, type your name as password.");
		} else
			reply(530, "User %s unknown.", name);
		if (!askpasswd && logging)
			syslog(LOG_NOTICE,
			    "ANONYMOUS FTP LOGIN REFUSED FROM %s", remotehost);
		return;
	}
	if (anon_only && !checkuser(_PATH_FTPCHROOT, name)) {
		reply(530, "Sorry, only anonymous ftp allowed.");
		return;
	}

	if ((pw = sgetpwnam(name))!=NULL) {
		if ((shell = pw->pw_shell) == NULL || *shell == 0)
			shell = _PATH_BSHELL;
		while ((cp = getusershell()) != NULL)
			if (strcmp(cp, shell) == 0)
				break;
		endusershell();

		if (cp == NULL || checkuser(_PATH_FTPUSERS, name)) {
			reply(530, "User %s access denied.", name);
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN REFUSED FROM %s, %s",
				    remotehost, name);
			pw = (struct passwd *) NULL;
			return;
		}
	}
	if (logging) {
		strncpy(curname, name, sizeof(curname)-1);
		curname[sizeof(curname)-1] = '\0';
	}
#ifdef SKEY
	if (!skey_haskey(name)) {
		char *myskey, *skey_keyinfo __P((char *name));

		myskey = skey_keyinfo(name);
		reply(331, "Password [ %s ] for %s required.",
		    myskey ? myskey : "error getting challenge", name);
	} else
#endif
		reply(331, "Password required for %s.", name);

	askpasswd = 1;
	/*
	 * Delay before reading passwd after first failed
	 * attempt to slow down passwd-guessing programs.
	 */
	if (login_attempts)
		sleep((unsigned) login_attempts);
}

/*
 * Check if a user is in the file "fname"
 */
static int checkuser(const char *fname, const char *name)
{
	FILE *fd;
	int found = 0;
	char *p, line[BUFSIZ];

	if ((fd = fopen(fname, "r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL)
			if ((p = strchr(line, '\n')) != NULL) {
				*p = '\0';
				if (line[0] == '#')
					continue;
				if (strcmp(line, name) == 0) {
					found = 1;
					break;
				}
			}
		(void) fclose(fd);
	}
	return (found);
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static void end_login(void)
{
	sigset_t allsigs;
	sigfillset (&allsigs);
	sigprocmask (SIG_BLOCK, &allsigs, NULL);
	(void) seteuid((uid_t)0);
	if (logged_in) {
		ftpdlogwtmp(ttyline, "", "");
		if (doutmp)
			logout(utmp.ut_line);
	}
	pw = NULL;
	logged_in = 0;
	guest = 0;
	dochroot = 0;
}

void pass(char *passwd)
{
	int rval;
	FILE *fd;
	static char homedir[MAXPATHLEN];
	char rootdir[MAXPATHLEN];
	sigset_t allsigs;

	if (logged_in || askpasswd == 0) {
		reply(503, "Login with USER first.");
		return;
	}
	askpasswd = 0;
	if (!guest) {		/* "ftp" is only account allowed no password */
		if (pw == NULL) {
			useconds_t us;

			/* Sleep between 1 and 3 seconds to emulate a crypt. */
#ifndef __linux__
			us = arc4random() % 3000000;
#else
			us = random() % 3000000;
#endif
			usleep(us);
			rval = 1;	/* failure below */
			goto skip;
		}
#if defined(KERBEROS)
		rval = klogin(pw, "", hostname, passwd);
		if (rval == 0)
			goto skip;
#endif
#ifdef SKEY
		if (skey_haskey(pw->pw_name) == 0 &&
		   (skey_passcheck(pw->pw_name, passwd) != -1)) {
			rval = 0;
			goto skip;
		}
#endif
		/* the strcmp does not catch null passwords! */
		if (strcmp(crypt(passwd, pw->pw_passwd), pw->pw_passwd) ||
		    *pw->pw_passwd == '\0') {
			rval = 1;	 /* failure */
			goto skip;
		}
		rval = 0;

skip:
		/*
		 * If rval == 1, the user failed the authentication check
		 * above.  If rval == 0, either Kerberos or local authentication
		 * succeeded.
		 */
		if (rval) {
			reply(530, "Login incorrect.");
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s, %s",
				    remotehost, curname);
			pw = NULL;
			if (login_attempts++ >= 5) {
				syslog(LOG_NOTICE,
				    "repeated login failures from %s",
				    remotehost);
				exit(0);
			}
			return;
		}
	} else {
		/* Save anonymous' password. */
		guestpw = strdup(passwd);
		if (guestpw == (char *)NULL)
			fatal("Out of memory");
	}
	login_attempts = 0;		/* this time successful */
#ifdef USE_SHADOW
	switch (isexpired(spw)) {
	  case 0: /* success */
		break;
	  case 1:
		syslog(LOG_NOTICE, "expired password from %s, %s",
		       remotehost, pw->pw_name);
		reply(530, "Please change your password and try again.");
		return;
	  case 2:
       		syslog(LOG_NOTICE, "inactive login from %s, %s",
		       remotehost, pw->pw_name);
		reply(530, "Login inactive -- contact administrator.");
		return;
	  case 3:
		syslog(LOG_NOTICE, "expired login from %s, %s",
		       remotehost, pw->pw_name);
		reply(530, "Account expired -- contact administrator.");
		return;
	}
#endif
if (strcmp(pw->pw_name, "ftp") != 0) {
	if (setegid((gid_t)pw->pw_gid) < 0) {
		reply(550, "Can't set gid.");
		return;
	}
	(void) initgroups(pw->pw_name, pw->pw_gid);
}

	/* open wtmp before chroot */
	ftpdlogwtmp(ttyline, pw->pw_name, remotehost);

	/* open utmp before chroot */
	if (doutmp) {
		memset((void *)&utmp, 0, sizeof(utmp));
		(void)time(&utmp.ut_time);
		(void)strncpy(utmp.ut_name, pw->pw_name, sizeof(utmp.ut_name));
		(void)strncpy(utmp.ut_host, remotehost, sizeof(utmp.ut_host));
		(void)strncpy(utmp.ut_line, ttyline, sizeof(utmp.ut_line));
		login(&utmp);
	}

	/* open stats file before chroot */
	if (guest && (stats == 1) && (statfd < 0))
		if ((statfd = open(_PATH_FTPDSTATFILE, O_WRONLY|O_APPEND)) < 0)
			stats = 0;

	logged_in = 1;

	dochroot = checkuser(_PATH_FTPCHROOT, pw->pw_name);
	if (guest || dochroot) {
		if (multihome && guest) {
			struct stat ts;

			/* Compute root directory. */
			snprintf(rootdir, sizeof(rootdir), "%s/%s",
				  pw->pw_dir, dhostname);
			if (stat(rootdir, &ts) < 0) {
				snprintf(rootdir, sizeof(rootdir), "%s/%s",
					  pw->pw_dir, hostname);
			}
		} else
			strcpy(rootdir, pw->pw_dir);
	}
	if (guest) {
		/*
		 * We MUST do a chdir() after the chroot. Otherwise
		 * the old current directory will be accessible as "."
		 * outside the new root!
		 
		if (chroot(rootdir) < 0 || chdir("/") < 0) {
			reply(550, "Can't set guest privileges.");
			goto bad;
		}*/
		strcpy(pw->pw_dir, "/");
		setenv("HOME", "/", 1);
	} else if (dochroot) {
		if (chroot(rootdir) < 0 || chdir("/") < 0) {
			reply(550, "Can't change root.");
			goto bad;
		}
		strcpy(pw->pw_dir, "/");
		setenv("HOME", "/", 1);
	} else if (chdir(pw->pw_dir) < 0) {
		if (chdir("/") < 0) {
			reply(530, "User %s: can't change directory to %s.",
			    pw->pw_name, pw->pw_dir);
			goto bad;
		} else
			lreply(230, "No directory! Logging in with home=/");
	}
if (strcmp(pw->pw_name, "ftp") != 0) {
	if (seteuid((uid_t)pw->pw_uid) < 0) {
		reply(550, "Can't set uid.");
		goto bad;
	}
}
	sigfillset(&allsigs);
	sigprocmask(SIG_UNBLOCK,&allsigs,NULL);

	/*
	 * Set home directory so that use of ~ (tilde) works correctly.
	 */
	if (getcwd(homedir, MAXPATHLEN) != NULL)
		setenv("HOME", homedir, 1);

	/*
	 * Display a login message, if it exists.
	 * N.B. reply(230,) must follow the message.
	 */
	if ((fd = fopen(_PATH_FTPLOGINMESG, "r")) != NULL) {
		char *cp, line[LINE_MAX];

		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(230, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
	}
	if (guest) {
		if (ident != NULL)
			free(ident);
		ident = strdup(passwd);
		if (ident == (char *)NULL)
			fatal("Ran out of memory.");
		reply(230, "Guest login ok, access restrictions apply.");
#ifdef HASSETPROCTITLE
		snprintf(proctitle, sizeof(proctitle),
		    "%s: anonymous/%.*s", remotehost,
		    (int)(sizeof(proctitle) - sizeof(remotehost) -
		    sizeof(": anonymous/")), passwd);
		setproctitle("%s", proctitle);
#endif /* HASSETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "ANONYMOUS FTP LOGIN FROM %s, %s",
			    remotehost, passwd);
	} else {
		reply(230, "User %s logged in.", pw->pw_name);
#ifdef HASSETPROCTITLE
		snprintf(proctitle, sizeof(proctitle),
		    "%s: %s", remotehost, pw->pw_name);
		setproctitle("%s", proctitle);
#endif /* HASSETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "FTP LOGIN FROM %s as %s",
			    remotehost, pw->pw_name);
	}
	(void) umask(defumask);
	return;
bad:
	/* Forget all about it... */
	end_login();
}

void retrieve(const char *cmd, const char *name)
{
	FILE *fin, *dout;
	struct stat st;
	int (*closefunc) __P((FILE *));
	time_t start;

	if (cmd == 0) {
		fin = fopen(name, "r"), closefunc = fclose;
		st.st_size = 0;
	} else {
		char line[BUFSIZ];

		(void) snprintf(line, sizeof(line), cmd, name);
		name = line;
		fin = ftpd_popen(line, "r"), closefunc = ftpd_pclose;
		st.st_size = -1;
		st.st_blksize = BUFSIZ;
	}
	if (fin == NULL) {
		if (errno != 0) {
			perror_reply(550, name);
			if (cmd == 0) {
				LOGCMD("get", name);
			}
		}
		return;
	}
	byte_count = -1;
	if (cmd == 0 && (fstat(fileno(fin), &st) < 0 || !S_ISREG(st.st_mode))) {
		reply(550, "%s: not a plain file.", name);
		goto done;
	}
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fin)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
		} else if (lseek(fileno(fin), restart_point, SEEK_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	dout = dataconn(name, st.st_size, "w");
	if (dout == NULL)
		goto done;
	time(&start);
	send_data(fin, dout, st.st_blksize, st.st_size,
		  (restart_point == 0 && cmd == 0 && S_ISREG(st.st_mode)));
	if ((cmd == 0) && stats)
		logxfer(name, st.st_size, start);
	(void) fclose(dout);
	data = -1;
	pdata = -1;
done:
	if (cmd == 0)
		LOGBYTES("get", name, byte_count);
	(*closefunc)(fin);
}

void rretrieve(const char *cmd, const char *name)
{
	FILE *fin, *dout;
	struct stat st;
	int (*closefunc) __P((FILE *));
	time_t start;
	int ret;

	TAILQ_INIT(&free_tqh);
	TAILQ_INIT(&sender_tqh);
	TAILQ_INIT(&writer_tqh);
	TAILQ_INIT(&waiting_tqh);
	
	TAILQ_INIT(&free_rmtaddr_tqh);
	TAILQ_INIT(&rmtaddr_tqh);

	TAILQ_INIT(&free_evwr_tqh);
	TAILQ_INIT(&evwr_tqh);
	
	TAILQ_INIT(&recvwr_tqh);
	
	TAILQ_INIT(&dcqp_tqh);
	
	TAILQ_INIT(&schedule_tqh);
	TAILQ_INIT(&finfo_tqh);
	
	TAILQ_INIT(&rcif_tqh);
	
	parsepath(name, name);

	/* establish data connection */
	ret = rdmadataconn(name, st.st_size, "w");
	if (ret != 0)
		goto done;
	time(&start);
	
	rsend_data(fin, dout, st.st_blksize, st.st_size,
		  (restart_point == 0 && cmd == 0 && S_ISREG(st.st_mode)));
	if ((cmd == 0) && stats)
		logxfer(name, st.st_size, start);
/*	(void) fclose(dout); */
	data = -1;
	pdata = -1;
done:
	if (cmd == 0)
		LOGBYTES("get", name, byte_count);
}

void mretrieve(const char *cmd, const char *name, int conn_number)
{
	FILE *fin, *dout;
	struct stat st;
	int (*closefunc) __P((FILE *));
	time_t start;
	int fd;
	int i;
	int ret;
	int conns[1024];
	pthread_t sender_tid[1024];
	
	TAILQ_INIT(&finfo_tqh);
	parsepath(name, name);
	
	syslog(LOG_ERR, "connection number is %d", conn_number);

	if (pdata < 0) {
		syslog(LOG_ERR, "the listening port doesn't exist");
		return;
	}

	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	for (i = 0; i < conn_number; i ++) {
		signal (SIGALRM, toolong);
		(void) alarm ((unsigned) timeout);
		conns[i] = accept(pdata, (struct sockaddr *)&from, &fromlen);
		(void) alarm (0);
		if (conns[i] < 0) {
			reply(425, "Can't open data connection.");
			(void) close(pdata);
			pdata = -1;
			return (NULL);
		}
		if (ntohs(from.sin_port) < IPPORT_RESERVED) {
			perror_reply(425, "Can't build data connection");
			(void) close(pdata);
			(void) close(conns[i]);
			pdata = -1;
			return (NULL);
		}
		if (from.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
			perror_reply(435, "Can't build data connection"); 
			(void) close(pdata);
			(void) close(conns[i]);
			pdata = -1;
			return (NULL);
		}
		
		ret = pthread_create(&sender_tid[i], NULL, \
			tcp_sender, &conns[i]);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_create sender:");
			return;
		}
	}

	(void) close(pdata);
	
	reply(150, "Opening %s mode data connection.",
		     type == TYPE_A ? "ASCII" : "BINARY");
	
	for (i = 0; i < conn_number; i ++) {
		pthread_join(sender_tid[i], NULL);
	}
	
	reply(226, "Transfer complete.");
	
	data = -1;
	pdata = -1;
	
	return;
}

void store(const char *name, const char *mode, int unique)
{
	FILE *fout, *din;
	int (*closefunc) __P((FILE *));
	struct stat st;
	int fd;

	if (unique && stat(name, &st) == 0) {
		char *nam;

		fd = guniquefd(name, &nam);
		if (fd == -1) {
			LOGCMD(*mode == 'w' ? "put" : "append", name);
			return;
		}
		name = nam;
		if (restart_point)
			mode = "r+";
		fout = fdopen(fd, mode);
	} else
		fout = fopen(name, mode);

	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		LOGCMD(*mode == 'w' ? "put" : "append", name);
		return;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fout)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseek(fout, 0L, SEEK_CUR) < 0) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	din = dataconn(name, (off_t)-1, "r");
	if (din == NULL)
		goto done;
	if (receive_data(din, fout) == 0) {
		if (unique)
			reply(226, "Transfer complete (unique file name:%s).",
			    name);
		else
			reply(226, "Transfer complete.");
	}
	(void) fclose(din);
	data = -1;
	pdata = -1;
done:
	LOGBYTES(*mode == 'w' ? "put" : "append", name, byte_count);
	(*closefunc)(fout);
}

void rstore(const char *name, const char *mode, int unique)
{
	FILE *fout, *din;
	int (*closefunc) __P((FILE *));
	struct stat st;
	int fd;
	int ret;
	
	TAILQ_INIT(&free_tqh);
	TAILQ_INIT(&sender_tqh);
	TAILQ_INIT(&writer_tqh);
	TAILQ_INIT(&waiting_tqh);
	
	TAILQ_INIT(&free_rmtaddr_tqh);
	TAILQ_INIT(&rmtaddr_tqh);

	TAILQ_INIT(&free_evwr_tqh);
	TAILQ_INIT(&evwr_tqh);
	
	TAILQ_INIT(&recvwr_tqh);
	
	TAILQ_INIT(&dcqp_tqh);
	
	TAILQ_INIT(&schedule_tqh);
	TAILQ_INIT(&finfo_tqh);
	
	TAILQ_INIT(&rcif_tqh);
	
	if (unique && stat(name, &st) == 0) {
		char *nam;

		fd = guniquefd(name, &nam);
		if (fd == -1) {
			LOGCMD(*mode == 'w' ? "put" : "append", name);
			return;
		}
		name = nam;
		if (restart_point)
			mode = "r+";
		fout = fdopen(fd, mode);
	} /* else
		fout = fopen(name, mode); */

	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		LOGCMD(*mode == 'w' ? "put" : "append", name);
		return;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fout)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseek(fout, 0L, SEEK_CUR) < 0) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	/* din = dataconn(name, (off_t)-1, "r"); */
	ret = rdmadataconn(name, (off_t)-1, "r");
	if (ret != 0)
		goto done;
	
	if (rreceive_data(fout) == 0) {
		if (unique)
			reply(226, "Transfer complete (unique file name:%s).",
			    name);
		else
			reply(226, "Transfer complete. Server user %.4f %%, sys %.4f %%, total %.4f %%.", \
			      self_ru.cpu_user, self_ru.cpu_sys, self_ru.cpu_total);

	}
	data = -1;
	pdata = -1;
done:
	LOGBYTES(*mode == 'w' ? "put" : "append", name, byte_count);
/*	(*closefunc)(fout); */
}

void mstore(const char *name, const char *mode, int unique)
{
	FILE *fout, *din;
	int (*closefunc) __P((FILE *));
	struct stat st;
	int fd;
	int i;
	int ret;
	int conns[1024];
	pthread_t recver_tid[1024];
	
	int connnum;
	connnum = atoi(name);
	syslog(LOG_ERR, "connection number is %d", connnum);

	if (pdata < 0) {
		syslog(LOG_ERR, "the listening port doesn't exist");
		return;
	}

	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	for (i = 0; i < connnum; i ++) {
		signal (SIGALRM, toolong);
		(void) alarm ((unsigned) timeout);
		conns[i] = accept(pdata, (struct sockaddr *)&from, &fromlen);
		(void) alarm (0);
		if (conns[i] < 0) {
			reply(425, "Can't open data connection.");
			(void) close(pdata);
			pdata = -1;
			return (NULL);
		}
		if (ntohs(from.sin_port) < IPPORT_RESERVED) {
			perror_reply(425, "Can't build data connection");
			(void) close(pdata);
			(void) close(conns[i]);
			pdata = -1;
			return (NULL);
		}
		if (from.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
			perror_reply(435, "Can't build data connection"); 
			(void) close(pdata);
			(void) close(conns[i]);
			pdata = -1;
			return (NULL);
		}
		
		ret = pthread_create(&recver_tid[i], NULL, \
			tcp_recver, &conns[i]);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_create sender:");
			return;
		}
	}

	(void) close(pdata);
	        
	reply(150, "Opening %s mode data connection.",
		     type == TYPE_A ? "ASCII" : "BINARY");
	
	for (i = 0; i < connnum; i ++) {
		pthread_join(recver_tid[i], NULL);
	}
	
	reply(226, "Transfer complete.");
	
	data = -1;
	pdata = -1;
	
	return;
}

static FILE * getdatasock(const char *mode)
{
	int on = 1, s, t, tries;
	sigset_t allsigs;

	if (data >= 0)
		return (fdopen(data, mode));
	sigfillset(&allsigs);
	sigprocmask (SIG_BLOCK, &allsigs, NULL);
	(void) seteuid((uid_t)0);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		goto bad;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	    (char *) &on, sizeof(on)) < 0)
		goto bad;
	/* anchor socket to avoid multi-homing problems */
#ifndef __linux__
	data_source.sin_len = sizeof(struct sockaddr_in);
#endif
	data_source.sin_family = AF_INET;
	data_source.sin_addr = ctrl_addr.sin_addr;
	for (tries = 1; ; tries++) {
		if (bind(s, (struct sockaddr *)&data_source,
		    sizeof(data_source)) >= 0)
			break;
		if (errno != EADDRINUSE || tries > 10)
			goto bad;
		sleep(tries);
	}
	(void) seteuid((uid_t)pw->pw_uid);
	sigfillset(&allsigs);
	sigprocmask (SIG_UNBLOCK, &allsigs, NULL);

#ifdef IP_TOS
	on = IPTOS_THROUGHPUT;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&on, sizeof(int)) < 0)
		syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
#endif
#ifdef TCP_NOPUSH
	/*
	 * Turn off push flag to keep sender TCP from sending short packets
	 * at the boundaries of each write().  Should probably do a SO_SNDBUF
	 * to set the send buffer size as well, but that may not be desirable
	 * in heavy-load situations.
	 */
	on = 1;
	if (setsockopt(s, IPPROTO_TCP, TCP_NOPUSH, (char *)&on, sizeof on) < 0)
		syslog(LOG_WARNING, "setsockopt (TCP_NOPUSH): %m");
#endif
#if 0 /* Respect the user's settings */
#ifdef SO_SNDBUF
	on = 65536;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&on, sizeof on) < 0)
		syslog(LOG_WARNING, "setsockopt (SO_SNDBUF): %m");
#endif
#endif

	return (fdopen(s, mode));
bad:
	/* Return the real value of errno (close may change it) */
	t = errno;
	(void) seteuid((uid_t)pw->pw_uid);
	sigfillset (&allsigs);
	sigprocmask (SIG_UNBLOCK, &allsigs, NULL);
	(void) close(s);
	errno = t;
	return (NULL);
}

static FILE * dataconn(const char *name, off_t size, const char *mode)
{
	char sizebuf[32];
	FILE *file;
	int retry = 0, tos;

	file_size = size;
	byte_count = 0;
	if (size != (off_t) -1) {
		(void) snprintf(sizebuf, sizeof(sizebuf), " (%lld bytes)", 
				(quad_t) size);
	} else
		sizebuf[0] = '\0';
	if (pdata >= 0) {
		struct sockaddr_in from;
		int s;
		socklen_t fromlen = sizeof(from);

		signal (SIGALRM, toolong);
		(void) alarm ((unsigned) timeout);
		s = accept(pdata, (struct sockaddr *)&from, &fromlen);
		(void) alarm (0);
		if (s < 0) {
			reply(425, "Can't open data connection.");
			(void) close(pdata);
			pdata = -1;
			return (NULL);
		}
		if (ntohs(from.sin_port) < IPPORT_RESERVED) {
			perror_reply(425, "Can't build data connection");
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		if (from.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
			perror_reply(435, "Can't build data connection"); 
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		(void) close(pdata);
		pdata = s;
#ifdef IP_TOS
		tos = IPTOS_THROUGHPUT;
		(void) setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos,
		    sizeof(int));
#endif
		reply(150, "Opening %s mode data connection for '%s'%s.",
		     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		return (fdopen(pdata, mode));
	}
	if (data >= 0) {
		reply(125, "Using existing data connection for '%s'%s.",
		    name, sizebuf);
		usedefault = 1;
		return (fdopen(data, mode));
	}
	if (usedefault)
		data_dest = his_addr;
	usedefault = 1;
	file = getdatasock(mode);
	if (file == NULL) {
		reply(425, "Can't create data socket (%s,%d): %s.",
		    inet_ntoa(data_source.sin_addr),
		    ntohs(data_source.sin_port), strerror(errno));
		return (NULL);
	}
	data = fileno(file);

	/*
	 * attempt to connect to reserved port on client machine;
	 * this looks like an attack
	 */
	if (ntohs(data_dest.sin_port) < IPPORT_RESERVED ||
	    ntohs(data_dest.sin_port) == 2049) {		/* XXX */
		perror_reply(425, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return NULL;
	}
	if (data_dest.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
		perror_reply(435, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return NULL;
	}
	while (connect(data, (struct sockaddr *)&data_dest,
	    sizeof(data_dest)) < 0) {
		if (errno == EADDRINUSE && retry < swaitmax) {
			sleep((unsigned) swaitint);
			retry += swaitint;
			continue;
		}
		perror_reply(425, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return (NULL);
	}
	reply(150, "Opening %s mode data connection for '%s'%s.",
	     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
	return (file);
}

static int rdmadataconn(const char *name, off_t size, const char *mode)
{
	char sizebuf[32];
	FILE *file;
	int retry = 0, tos;
	
	struct ibv_send_wr *bad_send_wr;
	struct ibv_recv_wr *bad_recv_wr;
	int ret;

	file_size = size;
	byte_count = 0;
	if (size != (off_t) -1) {
		(void) snprintf(sizebuf, sizeof(sizebuf), " (%lld bytes)", 
				(quad_t) size);
	} else
		sizebuf[0] = '\0';
	if (pdata >= 0) {
		struct sockaddr_in from;
		int s;
		socklen_t fromlen = sizeof(from);

		signal (SIGALRM, toolong);
		(void) alarm ((unsigned) timeout);
		s = accept(pdata, (struct sockaddr *)&from, &fromlen);
		(void) alarm (0);
		if (s < 0) {
			reply(425, "Can't open data connection.");
			(void) close(pdata);
			pdata = -1;
			return (NULL);
		}
		if (ntohs(from.sin_port) < IPPORT_RESERVED) {
			perror_reply(425, "Can't build data connection");
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		if (from.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
			perror_reply(435, "Can't build data connection"); 
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		(void) close(pdata);
		pdata = s;
#ifdef IP_TOS
		tos = IPTOS_THROUGHPUT;
		(void) setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos,
		    sizeof(int));
#endif
		reply(150, "Opening %s mode data connection for '%s'%s.",
		     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		return (fdopen(pdata, mode));
	}
	if (data >= 0) {
		reply(125, "Using existing data connection for '%s'%s.",
		    name, sizebuf);
		usedefault = 1;
		return (fdopen(data, mode));
	}
	if (usedefault)
		data_dest = his_addr;
	usedefault = 1;
	
	dc_cb = (rdma_cb *) malloc(sizeof(rdma_cb));
	if (dc_cb == NULL) {
		syslog(LOG_ERR, "malloc: %m");
		return(NULL);
	}
	
	memset(dc_cb, '\0', sizeof(rdma_cb));
	rdma_cb_init(dc_cb);
	
	dc_cb->size = opt.cbufsiz;
	dc_cb->server = 0;
	
	sem_init(&dc_cb->sem, 0, 0);
	
	/*
	 * attempt to connect to reserved port on client machine;
	 * this looks like an attack
	 */
	if (ntohs(data_dest.sin_port) < IPPORT_RESERVED ||
	    ntohs(data_dest.sin_port) == 2049) {		/* XXX */
		perror_reply(425, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return NULL;
	}
	if (data_dest.sin_addr.s_addr != his_addr.sin_addr.s_addr) {
		perror_reply(435, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return NULL;
	}
	
	ret = rdma_resolve_addr(dc_cb->cm_id, NULL, \
		(struct sockaddr *) &data_dest, 2000);
	if (ret) {
		syslog(LOG_ERR, "rdma_resolve_addr: %m");
		return;
	}
	
	sem_wait(&dc_cb->sem);
	if (dc_cb->state != ROUTE_RESOLVED) {
		syslog(LOG_ERR, "waiting for addr/route resolution state %d\n", 
			dc_cb->state);
		return;
	}
	
	ret = iperf_setup_qp(dc_cb, dc_cb->cm_id);
	if (ret) {
		syslog(LOG_ERR, "iperf_setup_qp: %m");
		goto err0;
	}

	/* update parameters */
	update_param(&opt);

	ret = iperf_setup_buffers(dc_cb);
	if (ret) {
		syslog(LOG_ERR, "iperf_setup_buffers: %m");
		goto err1;
	}
	
	ret = pthread_create(&dc_cb->cqthread, NULL, cq_thread, dc_cb);
	if (ret) {
		syslog(LOG_ERR, "pthread_create: %m");
		goto err2;
	}
	
	/* setup buffers */
	ret = tsf_setup_buf_list(dc_cb);
	if (ret) {
		syslog(LOG_ERR, "tsf_setup_buf_list fail");
		goto err3;
	}
	syslog(LOG_ERR, "tsf_setup_buf_list success\n");
	
	/* multiple streams
	create_dc_stream_client(dc_cb, opt.rcstreamnum, &data_dest); */
	
	reply(150, "Opening %s mode data connection for '%s'%s.",
	     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
	
	/* rdma connect */
	ret = rdma_connect_client(dc_cb);
	if (ret) {
		syslog(LOG_ERR, "rdma_connect_client: %m");
		goto err3;
	}
	
	return (0);
	
err3:
	pthread_cancel(dc_cb->cqthread);
	pthread_join(dc_cb->cqthread, NULL);
err2:
	iperf_free_buffers(dc_cb);
err1:
	iperf_free_qp(dc_cb);
err0:
	return (1);
	
/*	while (connect(data, (struct sockaddr *)&data_dest,
	    sizeof(data_dest)) < 0) {
		if (errno == EADDRINUSE && retry < swaitmax) {
			sleep((unsigned) swaitint);
			retry += swaitint;
			continue;
		}
		perror_reply(425, "Can't build data connection");
		(void) fclose(file);
		data = -1;
		return (NULL);
	} */
	
}

/*
 * Tranfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static void send_data(FILE *instr, FILE *outstr, off_t blksize, off_t filesize, int isreg)
{
	int c, cnt, filefd, netfd;
	char *buf, *bp;
	size_t len,size;

	transflag++;
	if (setjmp(urgcatch)) {
		transflag = 0;
		return;
	}
	switch (type) {

	case TYPE_A:
		while ((c = getc(instr)) != EOF) {
			byte_count++;
			if (c == '\n') {
				if (ferror(outstr))
					goto data_err;
				(void) putc('\r', outstr);
			}
			(void) putc(c, outstr);
		}
		fflush(outstr);
		transflag = 0;
		if (ferror(instr))
			goto file_err;
		if (ferror(outstr))
			goto data_err;
		reply(226, "Transfer complete.");
		return;

	case TYPE_I:
	case TYPE_L:
		/*
		 * isreg is only set if we are not doing restart and we
		 * are sending a regular file
		 */
		netfd = fileno(outstr);
		filefd = fileno(instr);

		if (isreg && filesize < (off_t)16 * 1024 * 1024) {
			buf = mmap(0, filesize, PROT_READ, MAP_SHARED, filefd,
				   (off_t)0);
			if (buf==MAP_FAILED || buf==NULL) {
				syslog(LOG_WARNING, "mmap(%lu): %m",
				       (unsigned long)filesize);
				goto oldway;
			}
			bp = buf;
			len = filesize;
			do {
				cnt = write(netfd, bp, len);
				len -= cnt;
				bp += cnt;
				if (cnt > 0) byte_count += cnt;
			} while(cnt > 0 && len > 0);

			transflag = 0;
			munmap(buf, (size_t)filesize);
			if (cnt < 0)
				goto data_err;
			reply(226, "Transfer complete.");
			return;
		}

oldway:
		size = blksize * 16; 
	
		if ((buf = malloc(size)) == NULL) {
			transflag = 0;
			perror_reply(451, "Local resource failure: malloc");
			return;
		}

#ifdef TCP_CORK
		{
		int on = 1;
		setsockopt(netfd, SOL_TCP, TCP_CORK, &on, sizeof on); 
		/* failure is harmless */ 
		}
#endif	
		while ((cnt = read(filefd, buf, size)) > 0 &&
		    write(netfd, buf, cnt) == cnt)
			byte_count += cnt;
#ifdef TCP_CORK
		{
		int off = 0;
		setsockopt(netfd, SOL_TCP, TCP_CORK, &off, sizeof off); 
		}
#endif	
		transflag = 0;
		(void)free(buf);
		if (cnt != 0) {
			if (cnt < 0)
				goto file_err;
			goto data_err;
		}
		reply(226, "Transfer complete.");
		return;
	default:
		transflag = 0;
		reply(550, "Unimplemented TYPE %d in send_data", type);
		return;
	}

data_err:
	transflag = 0;
	perror_reply(426, "Data connection");
	return;

file_err:
	transflag = 0;
	perror_reply(551, "Error on input file");
}

/*
 * Tranfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject to Mode, Structure, and Type.
 * RDMA channel
 * NB: Form isn't handled.
 */
static void rsend_data(FILE *instr, FILE *outstr, off_t blksize, off_t filesize, int isreg)
{
	int c, cnt, filefd, netfd;
	char *buf, *bp;
	size_t len,size;
	
	/* for rdma */
	struct ibv_send_wr *bad_wr;
	int ret;

	pthread_t scheduler_tid;
	pthread_t sender_tid;
	pthread_t reader_tid[200];
	
	void      *tret;

	/* wait for DC_CONNECTION_REQ */
	sem_wait(&dc_cb->sem);
/*	syslog(LOG_ERR, "start connection num: %d", opt.rcstreamnum);
	create_dc_stream_client(dc_cb, opt.rcstreamnum, &data_dest);
*/	
	transflag++;
	if (setjmp(urgcatch)) {
		transflag = 0;
		return;
	}

		/* create sender and reader */
		ret = pthread_create(&scheduler_tid, NULL, scheduler, dc_cb);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_create scheduler fail");
			exit(EXIT_FAILURE);
		}
		
		/* wait for file session negotiation finish */
		ret = pthread_join(scheduler_tid, &tret);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_join scheduler fail");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_ERR, "join scheduler success");
		
		syslog(LOG_ERR, "start connection num: %d", opt.rcstreamnum);
		create_dc_stream_client(dc_cb, opt.rcstreamnum, &data_dest);
		
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		getrusage(RUSAGE_SELF, &(self_ru.ru_start));
		self_ru.real_start.tv_sec = ts.tv_sec;
		self_ru.real_start.tv_usec = ts.tv_nsec / 1000;

		ret = pthread_create(&sender_tid, NULL, sender, dc_cb);
		if (ret != 0) {
			syslog(LOG_ERR, "pthread_create sender fail");
			exit(EXIT_FAILURE);
		}
		
		/* create multiple reader */
		int i;
		for (i = 0; i < opt.readernum; i ++) {
			ret = pthread_create(&reader_tid[i], NULL, \
				reader, dc_cb);
			if (ret != 0) {
				syslog(LOG_ERR, "pthread_create reader fail");
				exit(EXIT_FAILURE);
			}
		}
		
		clock_gettime(CLOCK_REALTIME, &ts);

		getrusage(RUSAGE_SELF, &(self_ru.ru_end));
		self_ru.real_end.tv_sec = ts.tv_sec;
		self_ru.real_end.tv_usec = ts.tv_nsec / 1000;

		cal_rusage(&self_ru);

		pthread_join(sender_tid, NULL);
		syslog(LOG_ERR, "join sender success");
		
		for (i = 0; i < opt.readernum; i ++) {
			pthread_join(reader_tid[i], NULL);
			syslog(LOG_ERR, "join reader[%d] success", i);
		}
		
		/* release the connected rdma_cm_id */
		/* cq_thread - cm_thread */
		tsf_free_buf_list();
		
		rdma_disconnect(dc_cb->cm_id);
		iperf_free_buffers(dc_cb);
		iperf_free_qp(dc_cb);
		syslog(LOG_ERR, "free buffers and qp success");
		
		pthread_cancel(dc_cb->cqthread);
		pthread_join(dc_cb->cqthread, NULL);
		syslog(LOG_ERR, "pthread_join cqthread success");
		
		pthread_cancel(dc_cb->cmthread);
		pthread_join(dc_cb->cmthread, NULL);
		syslog(LOG_ERR, "pthread_join cmthread success");
		
		rdma_destroy_id(dc_cb->cm_id);
		
		sem_destroy(&dc_cb->sem);
		
		free(dc_cb);
		
		reply(226, "Transfer complete. Server user %.4f %%, sys %.4f %%, total %.4f %%.", \
		      self_ru.cpu_user, self_ru.cpu_sys, self_ru.cpu_total);
		return;
	
	tsf_free_buf_list();
	
data_err:
	transflag = 0;
	perror_reply(426, "Data connection");
	return;

file_err:
	transflag = 0;
	perror_reply(551, "Error on input file");
}

/*
 * Transfer data from peer to "outstr" using the appropriate encapulation of
 * the data subject to Mode, Structure, and Type.
 *
 * N.B.: Form isn't handled.
 */
static int receive_data(FILE *instr, FILE *outstr)
{
	int c;
	int cnt;
	volatile int bare_lfs = 0;
	char buf[BUFSIZ];

	transflag++;
	if (setjmp(urgcatch)) {
		transflag = 0;
		return (-1);
	}
	switch (type) {

	case TYPE_I:
	case TYPE_L:
		signal (SIGALRM, lostconn);

		if (opt.usesplice == true) {
			off_t offset;
			offset = 0;
			syslog(LOG_ERR, "start sf_splice");
			cnt = sf_splice(fileno(outstr), fileno(instr), offset, 0);
		} else
		do {
			(void) alarm ((unsigned) timeout);
			cnt = read(fileno(instr), buf, sizeof(buf));
			(void) alarm (0);

			if (cnt > 0) {
				if (write(fileno(outstr), buf, cnt) != cnt)
					goto file_err;
				byte_count += cnt;
			}
		} while (cnt > 0);
		if (cnt < 0)
			goto data_err;
		transflag = 0;
		return (0);

	case TYPE_E:
		reply(553, "TYPE E not implemented.");
		transflag = 0;
		return (-1);

	case TYPE_A:
		while ((c = getc(instr)) != EOF) {
			byte_count++;
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				if (ferror(outstr))
					goto data_err;
				if ((c = getc(instr)) != '\n') {
					(void) putc ('\r', outstr);
					if (c == '\0' || c == EOF)
						goto contin2;
				}
			}
			(void) putc(c, outstr);
	contin2:	;
		}
		fflush(outstr);
		if (ferror(instr))
			goto data_err;
		if (ferror(outstr))
			goto file_err;
		transflag = 0;
		if (bare_lfs) {
			lreply(226,
		"WARNING! %d bare linefeeds received in ASCII mode",
			    bare_lfs);
		(void)printf("   File may not have transferred correctly.\r\n");
		}
		return (0);
	default:
		reply(550, "Unimplemented TYPE %d in receive_data", type);
		transflag = 0;
		return (-1);
	}

data_err:
	transflag = 0;
	perror_reply(426, "Data Connection");
	return (-1);

file_err:
	transflag = 0;
	perror_reply(452, "Error writing file");
	return (-1);
}

static int rreceive_data(FILE *outstr)
{
	int c;
	int cnt;
	volatile int bare_lfs = 0;
	char buf[BUFSIZ];
	FILE *instr;
	
	/* for rdma */
	struct ibv_send_wr *bad_wr;
	int ret;
	
/*	tsf_setup_buf_list(dc_cb); */
	
	/* wait for DC_CONNECTION_REQ */
	sem_wait(&dc_cb->sem);
	create_dc_stream_client(dc_cb, opt.rcstreamnum, &data_dest);
	
	int i;

	transflag++;
	if (setjmp(urgcatch)) {
		transflag = 0;
		return (-1);
	}

		signal (SIGALRM, lostconn);
		
		/* create recver and writer */
		pthread_t recver_tid;
		pthread_t writer_tid[200];
		void      *tret;
		
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		getrusage(RUSAGE_SELF, &(self_ru.ru_start));
		self_ru.real_start.tv_sec = ts.tv_sec;
		self_ru.real_start.tv_usec = ts.tv_nsec / 1000;

		/* create multiple writer */
		for (i = 0; i < opt.writernum; i ++) {
			ret = pthread_create(&writer_tid[i], NULL, \
				writer, dc_cb);
			if (ret != 0) {
				syslog(LOG_ERR, "create writer fail");
				exit(EXIT_FAILURE);
			}
		}
		
		for ( ; ; ) {
			if (transcurrlen >= transtotallen)
				break;

			/* during the data transfer if disconnect event 
			 * happened, just exit the process
			 */
			if (is_disconnected_event == 1)
				break;

			sleep(1);
		}

		clock_gettime(CLOCK_REALTIME, &ts);

		getrusage(RUSAGE_SELF, &(self_ru.ru_end));
		self_ru.real_end.tv_sec = ts.tv_sec;
		self_ru.real_end.tv_usec = ts.tv_nsec / 1000;

		cal_rusage(&self_ru);

		for (i = 0; i < opt.writernum; i ++) {
			pthread_cancel(writer_tid[i]);
			syslog(LOG_ERR, "cancel writer[%d] success", i);
		}
		
		/* release the connected rdma_cm_id */
		/* cq_thread - cm_thread */
		if (is_disconnected_event != 1)
			tsf_waiting_to_free();
		tsf_free_buf_list();
		
		rdma_disconnect(dc_cb->cm_id);
		rdma_destroy_id(dc_cb->cm_id);
		
		iperf_free_buffers(dc_cb);
		iperf_free_qp(dc_cb);
		syslog(LOG_ERR, "free buffers and qp success");
		
		/* disconnect dc channel */
		RCINFO *item;
		for (i = 0; i < opt.rcstreamnum; i++) {
			item = TAILQ_FIRST(&rcif_tqh);
			TAILQ_REMOVE(&rcif_tqh, item, entries);
			
			rdma_disconnect(item->cm_id);
			rdma_destroy_id(item->cm_id);
			free(item);
		}
		
		pthread_cancel(dc_cb->cqthread);
		pthread_join(dc_cb->cqthread, NULL);
		syslog(LOG_ERR, "pthread_join cqthread success");
		
		pthread_cancel(dc_cb->cmthread);
		pthread_join(dc_cb->cmthread, NULL);
		syslog(LOG_ERR, "pthread_join cmthread success");
		
		sem_destroy(&dc_cb->sem);
		syslog(LOG_ERR, "sem_destroy success");
		
		free(dc_cb);
		syslog(LOG_ERR, "free success");
		
		return (0);

	
	tsf_free_buf_list();

data_err:
	transflag = 0;
	perror_reply(426, "Data Connection");
	return (-1);

file_err:
	transflag = 0;
	perror_reply(452, "Error writing file");
	return (-1);
}

void statfilecmd(char *filename)
{
	FILE *fin;
	int c;
	char line[LINE_MAX];

	(void)snprintf(line, sizeof(line), "/bin/ls -lgA %s", filename);
	fin = ftpd_popen(line, "r");
	lreply(211, "status of %s:", filename);
	while ((c = getc(fin)) != EOF) {
		if (c == '\n') {
			if (ferror(stdout)){
				perror_reply(421, "control connection");
				(void) ftpd_pclose(fin);
				dologout(1);
				/* NOTREACHED */
			}
			if (ferror(fin)) {
				perror_reply(551, filename);
				(void) ftpd_pclose(fin);
				return;
			}
			(void) putc('\r', stdout);
		}
		(void) putc(c, stdout);
	}
	(void) ftpd_pclose(fin);
	reply(211, "End of Status");
}

void statcmd(void)
{
	struct sockaddr_in *sn;
	u_char *a, *p;

	lreply(211, "%s FTP server status:", hostname, version);
	printf("     %s\r\n", version);
	printf("     Connected to %s", remotehost);
	if (!isdigit(remotehost[0]))
		printf(" (%s)", inet_ntoa(his_addr.sin_addr));
	printf("\r\n");
	if (logged_in) {
		if (guest)
			printf("     Logged in anonymously\r\n");
		else
			printf("     Logged in as %s\r\n", pw->pw_name);
	} else if (askpasswd)
		printf("     Waiting for password\r\n");
	else
		printf("     Waiting for user name\r\n");
	printf("     TYPE: %s", typenames[type]);
	if (type == TYPE_A || type == TYPE_E)
		printf(", FORM: %s", formnames[form]);
	if (type == TYPE_L)
#if CHAR_BIT == 8
		printf(" %d", CHAR_BIT);
#else
		printf(" %d", bytesize);	/* need definition! */
#endif
	printf("; STRUcture: %s; transfer MODE: %s\r\n",
	    strunames[stru], modenames[mode]);
	if (data != -1)
		printf("     Data connection open\r\n");
	else if (pdata != -1) {
		printf("     in Passive mode");
		sn = &pasv_addr;
		goto printaddr;
	} else if (usedefault == 0) {
		printf("     PORT");
		sn = &data_dest;
printaddr:
		a = (u_char *) &sn->sin_addr;
		p = (u_char *) &sn->sin_port;
#define UC(b) (((int) b) & 0xff)
		printf(" (%d,%d,%d,%d,%d,%d)\r\n", UC(a[0]),
			UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
#undef UC
	} else
		printf("     No data connection\r\n");
	reply(211, "End of status");
}

void fatal(const char *s)
{

	reply(451, "Error in server: %s\n", s);
	reply(221, "Closing connection due to server error.");
	dologout(0);
	/* NOTREACHED */
}

void
#ifdef __STDC__
reply(int n, const char *fmt, ...)
#else
reply(int n, char *fmt, va_dcl va_alist)
#endif
{
	va_list ap;
#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	(void)printf("%d ", n);
	(void)vprintf(fmt, ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (debug) {
		syslog(LOG_DEBUG, "<--- %d ", n);
		vsyslog(LOG_DEBUG, fmt, ap);
	}
}

void
#ifdef __STDC__
lreply(int n, const char *fmt, ...)
#else
lreply(n, fmt, va_alist)
	int n;
	char *fmt;
	va_dcl
#endif
{
	va_list ap;
#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	(void)printf("%d- ", n);
	(void)vprintf(fmt, ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (debug) {
		syslog(LOG_DEBUG, "<--- %d- ", n);
		vsyslog(LOG_DEBUG, fmt, ap);
	}
}

static void ack(const char *s)
{

	reply(250, "%s command successful.", s);
}

void nack(const char *s)
{

	reply(502, "%s command not implemented.", s);
}

/* ARGSUSED */
void yyerror(char *s)
{
	char *cp;
        
	(void)s; /* ignore argument */

	if ((cp = strchr(cbuf,'\n'))!=NULL)
		*cp = '\0';
	reply(500, "'%s': command not understood.", cbuf);
}

void delete(char *name)
{
	struct stat st;

	LOGCMD("delete", name);
	if (stat(name, &st) < 0) {
		perror_reply(550, name);
		return;
	}
	if ((st.st_mode&S_IFMT) == S_IFDIR) {
		if (rmdir(name) < 0) {
			perror_reply(550, name);
			return;
		}
		goto done;
	}
	if (unlink(name) < 0) {
		perror_reply(550, name);
		return;
	}
done:
	ack("DELE");
}

void cwd(const char *path)
{
	FILE *message;

	if (chdir(path) < 0)
		perror_reply(550, path);
	else {
		if ((message = fopen(_PATH_CWDMESG, "r")) != NULL) {
			char *cp, line[LINE_MAX];

			while (fgets(line, sizeof(line), message) != NULL) {
				if ((cp = strchr(line, '\n')) != NULL)
					*cp = '\0';
				lreply(250, "%s", line);
			}
			(void) fflush(stdout);
			(void) fclose(message);
		}
		ack("CWD");
	}
}

void replydirname(const char *name, const char *message)
{
	char npath[MAXPATHLEN];
	int i;

	for (i = 0; *name != '\0' && i < (int)sizeof(npath) - 1; i++, name++) {
		npath[i] = *name;
		if (*name == '"')
			npath[++i] = '"';
	}
	npath[i] = '\0';
	reply(257, "\"%s\" %s", npath, message);
}

void makedir(char *name)
{

	LOGCMD("mkdir", name);
	if (mkdir(name, 0777) < 0)
		perror_reply(550, name);
	else
		replydirname(name, "directory created.");
}

void removedir(char *name)
{

	LOGCMD("rmdir", name);
	if (rmdir(name) < 0)
		perror_reply(550, name);
	else
		ack("RMD");
}
void pwd(void)
{
	char path[MAXPATHLEN];

	if (getcwd(path, sizeof path) == (char *)NULL)
		reply(550, "%s.", path);
	else
		replydirname(path, "is current directory.");
}

char * renamefrom(char *name)
{
	struct stat st;

	if (stat(name, &st) < 0) {
		perror_reply(550, name);
		return ((char *)0);
	}
	reply(350, "File exists, ready for destination name");
	return (name);
}

void renamecmd(char *from, char *to)
{

	LOGCMD2("rename", from, to);
	if (rename(from, to) < 0)
		perror_reply(550, "rename");
	else
		ack("RNTO");
}

static void dolog(struct sockaddr_in *sn)
{
	struct hostent *hp = gethostbyaddr((char *)&sn->sin_addr,
		sizeof(struct in_addr), AF_INET);

	if (hp)
		(void) strncpy(remotehost, hp->h_name, sizeof(remotehost)-1);
	else
		(void) strncpy(remotehost, inet_ntoa(sn->sin_addr),
		    sizeof(remotehost)-1);
	remotehost[sizeof(remotehost)-1] = '\0';
#ifdef HASSETPROCTITLE
	snprintf(proctitle, sizeof(proctitle), "%s: connected", remotehost);
	setproctitle("%s", proctitle);
#endif /* HASSETPROCTITLE */

	if (logging)
		syslog(LOG_INFO, "connection from %s", remotehost);
}

/*
 * Record logout in wtmp file
 * and exit with supplied status.
 */
void dologout(int status)
{
	sigset_t allsigs;

	transflag = 0;

	if (logged_in) {
		sigfillset(&allsigs);
		sigprocmask(SIG_BLOCK, &allsigs, NULL);
		(void) seteuid((uid_t)0);
		ftpdlogwtmp(ttyline, "", "");
		if (doutmp)
			logout(utmp.ut_line);
#if defined(KERBEROS)
		if (!notickets && krbtkfile_env)
			unlink(krbtkfile_env);
#endif
	}
	/* beware of flushing buffers after a SIGPIPE */
	_exit(status);
}

static void myoob(int signo)
{
	char *cp;
	int save_errno = errno;

	(void)signo;

	/* only process if transfer occurring */
	if (!transflag)
		return;
	cp = tmpline;
	if (ftpd_getline(cp, 7, stdin) == NULL) {
		reply(221, "You could at least say goodbye.");
		dologout(0);
	}
	upper(cp);
	if (strcmp(cp, "ABOR\r\n") == 0) {
		tmpline[0] = '\0';
		reply(426, "Transfer aborted. Data connection closed.");
		reply(226, "Abort successful");
		longjmp(urgcatch, 1);
	}
	if (strcmp(cp, "STAT\r\n") == 0) {
		if (file_size != (off_t) -1)
			reply(213, "Status: %qd of %qd bytes transferred",
			    (quad_t) byte_count, (quad_t) file_size);
		else
			reply(213, "Status: %qd bytes transferred", 
			    (quad_t)byte_count);
	}
	errno = save_errno;
}

/*
 * Note: a response of 425 is not mentioned as a possible response to
 *	the PASV command in RFC959. However, it has been blessed as
 *	a legitimate response by Jon Postel in a telephone conversation
 *	with Rick Adams on 25 Jan 89.
 */
void passive(void)
{
	socklen_t len;
#ifdef IP_PORTRANGE
	int on;
#else
	u_short port;
#endif
	char *p, *a;

	if (pw == NULL) {
		reply(530, "Please login with USER and PASS");
		return;
	}
	if (pdata >= 0)
		close(pdata);
	pdata = socket(AF_INET, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}

#ifdef IP_PORTRANGE
	on = high_data_ports ? IP_PORTRANGE_HIGH : IP_PORTRANGE_DEFAULT;
	if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
		       (char *)&on, sizeof(on)) < 0)
		goto pasv_error;
#else
#define FTP_DATA_BOTTOM 40000
#define FTP_DATA_TOP    44999
	if (high_data_ports) {
		for (port = FTP_DATA_BOTTOM; port <= FTP_DATA_TOP; port++) {
			pasv_addr = ctrl_addr;
			pasv_addr.sin_port = htons(port);
			if (bind(pdata, (struct sockaddr *) &pasv_addr,
				 sizeof(pasv_addr)) == 0)
				break;
			if (errno != EADDRINUSE)
				goto pasv_error;
		}
		if (port > FTP_DATA_TOP)
			goto pasv_error;
	}
	else
#endif
	{
		pasv_addr = ctrl_addr;
		pasv_addr.sin_port = 0;
		if (bind(pdata, (struct sockaddr *)&pasv_addr,
			 sizeof(pasv_addr)) < 0)
			goto pasv_error;
	}

	len = sizeof(pasv_addr);
	if (getsockname(pdata, (struct sockaddr *) &pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, TCPLISTENBACKLOG) < 0)
		goto pasv_error;
	a = (char *) &pasv_addr.sin_addr;
	p = (char *) &pasv_addr.sin_port;

#define UC(b) (((int) b) & 0xff)

	reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", UC(a[0]),
		UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
	return;

pasv_error:
	(void) close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

/*
 * Generate unique name for file with basename "local".
 * The file named "local" is already known to exist.
 * Generates failure reply on error.
 */
static int guniquefd(const char *local, char **nam)
{
	static char new[MAXPATHLEN];
	struct stat st;
	int count, len, fd;
	char *cp;

	cp = strrchr(local, '/');
	if (cp)
		*cp = '\0';
	if (stat(cp ? local : ".", &st) < 0) {
		perror_reply(553, cp ? local : ".");
		return (-1);
	}
	if (cp)
		*cp = '/';
	(void) strncpy(new, local, sizeof(new)-1);
	new[sizeof(new)-1] = '\0';
	len = strlen(new);
	if (len+2+1 >= (int)sizeof(new)-1)
		return (-1);
	cp = new + len;
	*cp++ = '.';
	for (count = 1; count < 100; count++) {
		(void)snprintf(cp, sizeof(new) - (cp - new), "%d", count);
		fd = open(new, O_RDWR|O_CREAT|O_EXCL, 0666);
		if (fd == -1)
			continue;
		if (nam)
			*nam = new;
		return (fd);
	}
	reply(452, "Unique file name cannot be created.");
	return (-1);
}

/*
 * Format and send reply containing system error number.
 */
void perror_reply(int code, const char *string)
{

	reply(code, "%s: %s.", string, strerror(errno));
}

static const char *onefile[] = {
	"",
	0
};

void send_file_list(const char *whichf)
{
	struct stat st;
	DIR *dirp = NULL;
	struct dirent *dir;
	FILE *volatile dout = NULL;
	char const *const *volatile dirlist;
	const char *dirname;
	volatile int simple = 0;
	volatile int freeglob = 0;
	glob_t gl;

	/* XXX: should the { go away if __linux__? */
	if (strpbrk(whichf, "~{[*?") != NULL) {
#ifdef __linux__
	        /* see popen.c */
		int flags = GLOB_NOCHECK;
#else
		int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE;
#endif

		memset(&gl, 0, sizeof(gl));
		freeglob = 1;
		if (glob(whichf, flags, 0, &gl)) {
			reply(550, "not found");
			goto out;
		} else if (gl.gl_pathc == 0) {
			errno = ENOENT;
			perror_reply(550, whichf);
			goto out;
		}
		/* The cast is necessary because of bugs in C's type system */
		dirlist = (char const *const *) gl.gl_pathv;
	} else {
		onefile[0] = whichf;
		dirlist = onefile;
		simple = 1;
	}

	if (setjmp(urgcatch)) {
		transflag = 0;
		goto out;
	}
	while ((dirname = *dirlist++)!=NULL) {
		if (stat(dirname, &st) < 0) {
			/*
			 * If user typed "ls -l", etc, and the client
			 * used NLST, do what the user meant.
			 */
			if (dirname[0] == '-' && *dirlist == NULL &&
			    transflag == 0) {
				retrieve("/bin/ls %s", dirname);
				goto out;
			}
			perror_reply(550, whichf);
			if (dout != NULL) {
				(void) fclose(dout);
				transflag = 0;
				data = -1;
				pdata = -1;
			}
			goto out;
		}

		if (S_ISREG(st.st_mode)) {
			if (dout == NULL) {
				dout = dataconn("file list", (off_t)-1, "w");
				if (dout == NULL)
					goto out;
				transflag++;
			}
			fprintf(dout, "%s%s\n", dirname,
				type == TYPE_A ? "\r" : "");
			byte_count += strlen(dirname) + 1;
			continue;
		} else if (!S_ISDIR(st.st_mode))
			continue;

		if ((dirp = opendir(dirname)) == NULL)
			continue;

		while ((dir = readdir(dirp)) != NULL) {
			char nbuf[MAXPATHLEN];

#ifdef __linux__
			if (!strcmp(dir->d_name, "."))
				continue;
			if (!strcmp(dir->d_name, ".."))
				continue;
#else
			if (dir->d_name[0] == '.' && dir->d_namlen == 1)
				continue;
			if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
			    dir->d_namlen == 2)
				continue;
#endif

			snprintf(nbuf, sizeof(nbuf), "%s/%s", dirname,
				 dir->d_name);

			/*
			 * We have to do a stat to insure it's
			 * not a directory or special file.
			 */
			if (simple || (stat(nbuf, &st) == 0 &&
			    S_ISREG(st.st_mode))) {
				if (dout == NULL) {
					dout = dataconn("file list", (off_t)-1,
						"w");
					if (dout == NULL)
						goto out;
					transflag++;
				}
				if (nbuf[0] == '.' && nbuf[1] == '/')
					fprintf(dout, "%s%s\n", &nbuf[2],
						type == TYPE_A ? "\r" : "");
				else
					fprintf(dout, "%s%s\n", nbuf,
						type == TYPE_A ? "\r" : "");
				byte_count += strlen(nbuf) + 1;
			}
		}
		(void) closedir(dirp);
	}

	if (dout == NULL)
		reply(550, "No files found.");
	else if (ferror(dout) != 0)
		perror_reply(550, "Data connection");
	else
		reply(226, "Transfer complete.");

	transflag = 0;
	if (dout != NULL)
		(void) fclose(dout);
	data = -1;
	pdata = -1;
out:
	if (freeglob) {
		freeglob = 0;
		globfree(&gl);
	}
}

static void reapchild(int signo)
{
	int save_errno = errno;

	(void)signo;

	while (wait3(NULL, WNOHANG, NULL) > 0)
		;
	errno = save_errno;
}

void logxfer(const char *name, off_t size, time_t start)
{
	char buf[400 + MAXHOSTNAMELEN*4 + MAXPATHLEN*4];
	char dir[MAXPATHLEN], path[MAXPATHLEN], rpath[MAXPATHLEN];
	char vremotehost[MAXHOSTNAMELEN*4], vpath[MAXPATHLEN*4];
	char *vpw;
	time_t now;

	if ((statfd >= 0) && (getcwd(dir, sizeof(dir)) != NULL)) {
		time(&now);

		vpw = (char *)malloc(strlen((guest) ? guestpw : pw->pw_name)*4+1);
		if (vpw == NULL)
			return;

		snprintf(path, sizeof path, "%s/%s", dir, name);
		if (realpath(path, rpath) == NULL) {
			strncpy(rpath, path, sizeof rpath-1);
			rpath[sizeof rpath-1] = '\0';
		}
		strvis(vpath, rpath, VIS_SAFE|VIS_NOSLASH);

		strvis(vremotehost, remotehost, VIS_SAFE|VIS_NOSLASH);
		strvis(vpw, (guest) ? guestpw : pw->pw_name, VIS_SAFE|VIS_NOSLASH);

		snprintf(buf, sizeof(buf),
		    "%.24s %ld %s %qd %s %c %s %c %c %s ftp %d %s %s\n",
		    ctime(&now), (long)(now - start + (now == start)),
		    vremotehost, (long long) size, vpath,
		    ((type == TYPE_A) ? 'a' : 'b'), "*" /* none yet */,
		    'o', ((guest) ? 'a' : 'r'),
		    vpw, 0 /* none yet */,
		    ((guest) ? "*" : pw->pw_name),
		    dhostname);
		write(statfd, buf, strlen(buf));
		free(vpw);
	}
}

#if defined(TCPWRAPPERS)
static int check_host(struct sockaddr_in *sin)
{
	struct hostent *hp = gethostbyaddr((char *)&sin->sin_addr,
		sizeof(struct in_addr), AF_INET);
	char *addr = inet_ntoa(sin->sin_addr);

	if (hp) {
		if (!hosts_ctl("ftpd", hp->h_name, addr, STRING_UNKNOWN)) {
			syslog(LOG_NOTICE, "tcpwrappers rejected: %s [%s]",
			    hp->h_name, addr);
			return (0);
		}
	} else {
		if (!hosts_ctl("ftpd", STRING_UNKNOWN, addr, STRING_UNKNOWN)) {
			syslog(LOG_NOTICE, "tcpwrappers rejected: [%s]", addr);
			return (0);
		}
	}
	return (1);
}
#endif	/* TCPWRAPPERS */

