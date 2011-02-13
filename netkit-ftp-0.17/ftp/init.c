/* Reading/parsing the initialization file.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.

This file is part of GNU Wget.

GNU Wget is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

GNU Wget is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wget.  If not, see <http://www.gnu.org/licenses/>.

Additional permission under GNU GPL version 3 section 7

If you modify this program, or any covered work, by linking or
combining it with the OpenSSL project's OpenSSL library (or a
modified version of that library), containing parts covered by the
terms of the OpenSSL or SSLeay licenses, the Free Software Foundation
grants you additional permission to convey the resulting work.
Corresponding Source for a non-source form of such a combination
shall include the source code for the parts of OpenSSL used as well
as that of the covered work.  

  Yufei Ren modified from wget 1.12
  Feb 05 2011

*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <assert.h>

#include <pwd.h>


#include "utils.h"
#include "init.h"

extern struct options opt;

/* Return values of parse_line. */
enum parse_line {
  line_ok,
  line_empty,
  line_syntax_error,
  line_unknown_command
};

char *strdupdelim (const char *beg, const char *end);

#define CMD_DECLARE(func) static bool func (const char *, const char *, void *)

CMD_DECLARE (cmd_boolean);
CMD_DECLARE (cmd_number);
CMD_DECLARE (cmd_string);
CMD_DECLARE (cmd_byte);
/* CMD_DECLARE (cmd_file);
CMD_DECLARE (cmd_directory); */

/* List of recognized commands, each consisting of name, place and
   function.  When adding a new command, simply add it to the list,
   but be sure to keep the list sorted alphabetically, as
   command_by_name's binary search depends on it.  Also, be sure to
   add any entries that allocate memory (e.g. cmd_string and
   cmd_vector) to the cleanup() function below. */

static const struct {
	const char *name;
	void *place;
	bool (*action) (const char *, const char *, void *);
} commands[] = {
  /* KEEP THIS LIST ALPHABETICALLY SORTED */
	{ "cbufnum",       &opt.cbufnum,           cmd_number },
	{ "cbufsiz",       &opt.cbufsiz,           cmd_byte },
/* srvcommport - server side communication channel listening port */
	{ "srvcomport",    &opt.srvcomport,        cmd_number },
/*
readernum
writernum
sendernum
recvernum

active listening port area (min to max)

*/
};

static bool simple_atoi (const char *, const char *, int *);
static bool setval_internal (int, const char *, const char *);
static bool setval_internal_tilde (int, const char *, const char *);
static enum parse_line
parse_line (const char *line, char **com, char **val, int *comind);

/* Reset the variables to default values.  */
static void
defaults (void)
{
/*  char *tmp;*/

  /* Most of the default values are 0 (and 0.0, NULL, and false).
     Just reset everything, and fill in the non-zero values.  Note
     that initializing pointers to NULL this way is technically
     illegal, but porting Wget to a machine where NULL is not all-zero
     bit pattern will be the least of the implementors' worries.  */

  opt.cbufsiz = 5242880; /* default buffer size is 5MB */
  opt.cbufnum = 10;
  
  opt.srvcomport = 21;
}

/* Return the user's home directory (strdup-ed), or NULL if none is
   found.  */
char *
home_dir (void)
{
	static char buf[PATH_MAX];
	static char *home;
	
	if (!home)
	{
		home = getenv ("HOME");
		if (!home)
		{
			/* If HOME is not defined, try getting it 
			 from the password file.  */
			struct passwd *pwd = getpwuid (getuid ());
			if (!pwd || !pwd->pw_dir)
				return NULL;
			strcpy (buf, pwd->pw_dir);
			home = buf;
		}
	}

	return home ? strdup (home) : NULL;
}

/* Check the 'WGETRC' environment variable and return the file name
   if  'WGETRC' is set and is a valid file.
   If the `WGETRC' variable exists but the file does not exist, the
   function will exit().  */
char *
rftprc_env_file_name (void)
{
/* client side */
#if defined(RCFTPRC)
  char *env = getenv ("RCFTPRC");
#elif defined(RFTPDRC)
/* server side */
  char *env = getenv ("RFTPDRC");
#endif

  if (env && *env)
    {
      if (!file_exists_p (env))
        {
          fprintf (stderr, "RCFTPRC/RFTPDRC points to %s, which doesn't exist.\n",
                   env);
          exit (1);
        }
      return strdup (env);
    }
  return NULL;
}

/* Check for the existance of '$HOME/.wgetrc' and return it's path
   if it exists and is set.  */
char *
rftprc_user_file_name (void)
{
  char *home = home_dir ();
  char *file = NULL;
  
  int size = 128;
  file = (char *) malloc (size);
  assert(file != NULL);
  
  /* If that failed, try $HOME/.wgetrc (or equivalent).  */
  home = home_dir ();
  if (home)
#if defined(RCFTPRC)
    snprintf (file, 128, "%s/.rcftprc", home);
#elif defined(RFTPDRC)
    snprintf (file, 128, "%s/.rftpdrc", home);
#endif
  free (home);

  if (!file)
    return NULL;
  if (!file_exists_p (file))
    {
      free (file);
      return NULL;
    }
  return file;
}

/* Return the path to the user's .wgetrc.  This is either the value of
   `WGETRC' environment variable, or `$HOME/.wgetrc'.

   Additionally, for windows, look in the directory where wget.exe
   resides.  */
char *
rftprc_file_name (void)
{
  char *file = rftprc_env_file_name ();
  if (file && *file)
    return file;

  file = rftprc_user_file_name ();

  return file;
}

/* Initialize variables from a wgetrc file.  Returns zero (failure) if
   there were errors in the file.  */

static bool
run_rftprc (const char *file)
{
  FILE *fp;
  char *line;
  int ln;
  int errcnt = 0;

  fp = fopen (file, "r");
  if (!fp)
    {
      fprintf (stderr, "Cannot read %s (%s).\n",
               file, strerror (errno));
      return true;                      /* not a fatal error */
    }
  ln = 1;
  while ((line = read_whole_line (fp)) != NULL)
    {
      char *com = NULL, *val = NULL;
      int comind;

      /* Parse the line.  */
      switch (parse_line (line, &com, &val, &comind))
        {
        case line_ok:
          /* If everything is OK, set the value.  */
          if (!setval_internal_tilde (comind, com, val))
            {
              fprintf (stderr, "Error in %s at line %d.\n",
                       file, ln);
              ++errcnt;
            }
          break;
        case line_syntax_error:
          fprintf (stderr, "Syntax error in %s at line %d.\n",
                   file, ln);
          ++errcnt;
          break;
        case line_unknown_command:
          fprintf (stderr, "Unknown command %s in %s at line %d.\n",
                   com, file, ln);
          ++errcnt;
          break;
        case line_empty:
          break;
        default:
          abort ();
        }
      free (com);
      free (val);
      free (line);
      ++ln;
    }
  fclose (fp);

  return errcnt == 0;
}

/* Initialize the defaults and run the system wgetrc and user's own
   rcftprc or rftpdrc.  */
void
initialize (void)
{
  char *file, *env_sysrc;
  int ok = true;

  /* Load the hard-coded defaults.  */
  defaults ();

  /* Run a non-standard system rc file when the according environment
     variable has been set. For internal testing purposes only!  */
/* client side */
#if defined(RCFTPRC)
  env_sysrc = getenv ("SYSTEM_RCFTPRC");
#elif defined(RFTPDRC)
/* server side */
  env_sysrc = getenv ("SYSTEM_RFTPDRC");
#endif
  if (env_sysrc && file_exists_p (env_sysrc))
    ok &= run_rftprc (env_sysrc);
  /* Otherwise, if SYSTEM_xxxxRC is defined, use it.  */
#if defined(SYSTEM_RCFTPRC)
  else if (file_exists_p (SYSTEM_RCFTPRC))
    ok &= run_rftprc (SYSTEM_RCFTPRC);
#elif defined(SYSTEM_RFTPDRC)
  else if (file_exists_p (SYSTEM_RFTPDRC))
    ok &= run_rftprc (SYSTEM_RFTPDRC);
#endif
  /* Override it with your own, if one exists.  */
  file = rftprc_file_name ();
  if (!file)
    return;
  ok &= run_rftprc (file);

  /* If there were errors processing either `.wgetrc', abort. */
  if (!ok)
    exit (2);

  free (file);
  return;
}

/* A very simple atoi clone, more useful than atoi because it works on
   delimited strings, and has error reportage.  Returns true on success,
   false on failure.  If successful, stores result to *DEST.  */

static bool
simple_atoi (const char *beg, const char *end, int *dest)
{
  int result = 0;
  bool negative = false;
  const char *p = beg;

  while (p < end && isspace (*p))
    ++p;
  if (p < end && (*p == '-' || *p == '+'))
    {
      negative = (*p == '-');
      ++p;
    }
  if (p == end)
    return false;

  /* Read negative numbers in a separate loop because the most
     negative integer cannot be represented as a positive number.  */

  if (!negative)
    for (; p < end && isdigit (*p); p++)
      {
        int next = (10 * result) + (*p - '0');
        if (next < result)
          return false;         /* overflow */
        result = next;
      }
  else
    for (; p < end && isdigit (*p); p++)
      {
        int next = (10 * result) - (*p - '0');
        if (next > result)
          return false;         /* underflow */
        result = next;
      }

  if (p != end)
    return false;

  *dest = result;
  return true;
}

/* Look up CMDNAME in the commands[] and return its position in the
   array.  If CMDNAME is not found, return -1.  */

static int
command_by_name (const char *cmdname)
{
	/* Use binary search for speed. */
	int lo = 0, hi = countof (commands) - 1;

	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		int cmp = strcasecmp (cmdname, commands[mid].name);
		if (cmp < 0)
			hi = mid - 1;
		else if (cmp > 0)
			lo = mid + 1;
		else
			return mid;
	}
	return -1;
}

/* Store the boolean value from VAL to PLACE.  COM is ignored,
   except for error messages.  */
static bool
cmd_boolean (const char *com, const char *val, void *place)
{
  bool value;

  if (CMP2 (val, 'o', 'n') || CMP3 (val, 'y', 'e', 's') || CMP1 (val, '1'))
    /* "on", "yes" and "1" mean true. */
    value = true;
  else if (CMP3 (val, 'o', 'f', 'f') || CMP2 (val, 'n', 'o') || CMP1 (val, '0'))
    /* "off", "no" and "0" mean false. */
    value = false;
  else
    {
      fprintf (stderr,
               "%s: Invalid boolean %s; use `on' or `off'.\n",
               com, val);
      return false;
    }

  *(bool *) place = value;
  return true;
}

/* Set the non-negative integer value from VAL to PLACE.  With
   incorrect specification, the number remains unchanged.  */
static bool
cmd_number (const char *com, const char *val, void *place)
{
  if (!simple_atoi (val, val + strlen (val), place)
      || *(int *) place < 0)
    {
      fprintf (stderr, "%s: Invalid number %s.\n",
               com, val);
      return false;
    }
  return true;
}

/* Copy (strdup) the string at COM to a new location and place a
   pointer to *PLACE.  */
static bool
cmd_string (const char *com, const char *val, void *place)
{
  char **pstring = (char **)place;

  free (*pstring);
  *pstring = strdup (val);
  return true;
}

/* byte stands for xKB/kB/MB/mB/GB/gB  */
static bool
cmd_byte (const char *com, const char *val, void *place)
{
  *(long *)place = (long) byte_atoi(val);
  
  return true;
}

/* Copy the string formed by two pointers (one on the beginning, other
   on the char after the last char) to a new, malloc-ed location.
   0-terminate it.  */
char *
strdupdelim (const char *beg, const char *end)
{
	char *res = malloc (end - beg + 1);
	memcpy (res, beg, end - beg);
	res[end - beg] = '\0';
	return res;
}

/* Remove dashes and underscores from S, modifying S in the
   process. */

static void
dehyphen (char *s)
{
  char *t = s;                  /* t - tortoise */
  char *h = s;                  /* h - hare     */
  while (*h)
    if (*h == '_' || *h == '-')
      ++h;
    else
      *t++ = *h++;
  *t = '\0';
}

/* Parse the line pointed by line, with the syntax:
   <sp>* command <sp>* = <sp>* value <sp>*
   Uses malloc to allocate space for command and value.

   Returns one of line_ok, line_empty, line_syntax_error, or
   line_unknown_command.

   In case of line_ok, *COM and *VAL point to freshly allocated
   strings, and *COMIND points to com's index.  In case of error or
   empty line, their values are unmodified.  */

static enum parse_line
parse_line (const char *line, char **com, char **val, int *comind)
{
	const char *p;
	const char *end = line + strlen (line);
	const char *cmdstart, *cmdend;
	const char *valstart, *valend;
	
	char *cmdcopy;
	int ind;
	
	/* Skip leading and trailing whitespace.  */
	while (*line && isspace (*line))
		++line;
	while (end > line && isspace (end[-1]))
		--end;
	
	/* Skip empty lines and comments.  */
	if (!*line || *line == '#')
		return line_empty;
	
	p = line;
	
	cmdstart = p;
	while (p < end && (isalnum (*p) || *p == '_' || *p == '-'))
		++p;
	cmdend = p;
	
	/* Skip '=', as well as any space before or after it. */
	while (p < end && isspace (*p))
		++p;
	if (p == end || *p != '=')
		return line_syntax_error;
	++p;
	while (p < end && isspace (*p))
		++p;
	
	valstart = p;
	valend   = end;
	
	/* The syntax is valid (even though the command might not be).  Fill
	   in the command name and value.  */
	*com = strdupdelim (cmdstart, cmdend);
	*val = strdupdelim (valstart, valend);
	
	/* The line now known to be syntactically correct.  Check whether
	   the command is valid.  */
	BOUNDED_TO_ALLOCA (cmdstart, cmdend, cmdcopy);
	dehyphen (cmdcopy);
	ind = command_by_name (cmdcopy);
	if (ind == -1)
	  return line_unknown_command;
	
	/* Report success to the caller. */
	*comind = ind;
	return line_ok;
}

/* Run commands[comind].action. */

static bool
setval_internal (int comind, const char *com, const char *val)
{
  assert (0 <= comind && ((size_t) comind) < countof (commands));
/*  DEBUGP (("Setting %s (%s) to %s\n", com, commands[comind].name, val)); */
  return commands[comind].action (com, val, commands[comind].place);
}

#define ISSEP(c) ((c) == '/')

static bool
setval_internal_tilde (int comind, const char *com, const char *val)
{
  bool ret;
/*  int homelen;
  char *home;
  char **pstring;*/
  ret = setval_internal (comind, com, val);

  /* We make tilde expansion for cmd_file and cmd_directory
  if (((commands[comind].action == cmd_file) ||
       (commands[comind].action == cmd_directory))
      && ret && (*val == '~' && ISSEP (val[1])))
    {
      pstring = commands[comind].place;
      home = home_dir ();
      if (home)
	{
	  homelen = strlen (home);
	  while (homelen && ISSEP (home[homelen - 1]))
            home[--homelen] = '\0'; */

	  /* Skip the leading "~/".
	  for (++val; ISSEP (*val); val++)
  	    ;
	  *pstring = concat_strings (home, "/", val, (char *)0);
	}
    } */
  return ret;
}
