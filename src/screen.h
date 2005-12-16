/* Copyright (c) 1993-2000
 *      Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
 *      Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
 * Copyright (c) 1987 Oliver Laumann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 ****************************************************************
 * $Id$ FAU
 */

#include "os.h"

#if defined(__STDC__)
# ifndef __P
#  define __P(a) a
# endif
#else
# ifndef __P
#  define __P(a) ()
# endif
# define const
#endif

#include "osdef.h"

#include "ansi.h"
#include "sched.h"
#include "acls.h"
#include "comm.h"
#include "layer.h"
#include "term.h"


#ifdef DEBUG
# define STATIC		/* a function that the debugger should see */
#else
# define STATIC static
#endif

#ifdef DEBUG
# define DEBUGDIR "/tmp/debug"
# define debugf(a)       do {if(dfp){fprintf a;fflush(dfp);}} while (0)
# define debug(x)        debugf((dfp,x))
# define debug1(x,a)     debugf((dfp,x,a))
# define debug2(x,a,b)   debugf((dfp,x,a,b))
# define debug3(x,a,b,c) debugf((dfp,x,a,b,c))
  extern FILE *dfp;
#else
# define debugf(a)       do {} while (0)
# define debug(x)        debugf(x)
# define debug1(x,a)     debugf(x)
# define debug2(x,a,b)   debugf(x)
# define debug3(x,a,b,c) debugf(x)
#endif

#ifndef DEBUG
# define NOASSERT
#endif

#ifndef NOASSERT
# if defined(__STDC__)
#  define ASSERT(lousy_cpp) do {if (!(lousy_cpp)) {if (!dfp) opendebug(0, 1);debug2("ASSERT("#lousy_cpp") failed file %s line %d\n", __FILE__, __LINE__);abort();}} while (0)
# else
#  define ASSERT(lousy_cpp) do {if (!(lousy_cpp)) {if (!dfp) opendebug(0, 1);debug2("ASSERT(lousy_cpp) failed file %s line %d\n", __FILE__, __LINE__);abort();}} while (0)
# endif
#else
# define ASSERT(lousy_cpp) do {} while (0)
#endif

/* here comes my own Free: jw. */
#define Free(a) {if ((a) == 0) abort(); else free((void *)(a)); (a)=0;}

#define Ctrl(c) ((c)&037)

#define MAXSTR		256
#define MAXARGS 	64
#define MSGWAIT 	5
#define MSGMINWAIT 	1
#define SILENCEWAIT	30

/*
 * if a nasty user really wants to try a history of 3000 lines on all 10
 * windows, he will allocate 8 MegaBytes of memory, which is quite enough.
 */
#define MAXHISTHEIGHT		3000
#define DEFAULTHISTHEIGHT	100
#ifdef NAME_MAX
# define DEFAULT_BUFFERFILE     "/tmp/screen-xchg"
#else
# define DEFAULT_BUFFERFILE	"/tmp/screen-exchange"
#endif


#if defined(hpux) && !(defined(VSUSP) && defined(VDSUSP) && defined(VWERASE) && defined(VLNEXT))
# define HPUX_LTCHARS_HACK
#endif

struct mode
{
#ifdef POSIX
  struct termios tio;
# ifdef HPUX_LTCHARS_HACK
  struct ltchars m_ltchars;
# endif /* HPUX_LTCHARS_HACK */
#else /* POSIX */
# ifdef TERMIO
  struct termio tio;
#  ifdef CYTERMIO
  int m_mapkey;
  int m_mapscreen;
  int m_backspace;
#  endif
# else /* TERMIO */
  struct sgttyb m_ttyb;
  struct tchars m_tchars;
  struct ltchars m_ltchars;
  int m_ldisc;
  int m_lmode;
# endif /* TERMIO */
#endif /* POSIX */
#if defined(KANJI) && defined(TIOCKSET)
  struct jtchars m_jtchars;
  int m_knjmode;
#endif
};


/* #include "logfile.h" */	/* (requires stat.h) struct logfile */
#include "image.h"
#include "display.h"
#include "window.h"

/*
 * Parameters for the Detach() routine
 */
#define D_DETACH	0
#define D_STOP		1
#define D_REMOTE	2
#define D_POWER 	3
#define D_REMOTE_POWER	4
#define D_LOCK		5
#define D_HANGUP	6

/*
 * Here are the messages the attacher sends to the backend
 */
#define MSG_CREATE	0
#define MSG_ERROR	1
#define MSG_ATTACH	2
#define MSG_CONT	3
#define MSG_DETACH	4
#define MSG_POW_DETACH	5
#define MSG_WINCH	6
#define MSG_HANGUP	7

/*
 * versions of struct msg:
 * 0:	screen version 3.6.6 (version count introduced)
 */
#define MSG_VERSION	0	
#define MSG_REVISION	(('m'<<24) | ('s'<<16) | ('g'<<8) | MSG_VERSION)
struct msg
{
  int protocol_revision;	/* reduce harm done by incompatible messages */
  int type;
  char m_tty[MAXPATHLEN];	/* ttyname */
  union
    {
      struct
	{
	  int lflag;
	  int aflag;
	  int flowflag;
	  int hheight;		/* size of scrollback buffer */
	  int nargs;
	  char line[MAXPATHLEN];
	  char dir[MAXPATHLEN];
	  char screenterm[20];	/* is screen really "screen" ? */
	}
      create;
      struct
	{
	  char auser[20 + 1];	/* username */
	  int apid;		/* pid of frontend */
	  int adaptflag;	/* adapt window size? */
	  int lines, columns;	/* display size */
	  char preselect[20];
	  int esc;		/* his new escape character unless -1 */
	  int meta_esc;		/* his new meta esc character unless -1 */
	  char envterm[20 + 1];	/* terminal type */
	}
      attach;
      struct 
	{
	  char duser[20 + 1];	/* username */
	  int dpid;		/* pid of frontend */
	}
      detach;
      char message[MAXPATHLEN * 2];
    } m;
};

/*
 * And the signals the attacher receives from the backend
 */
#define SIG_BYE		SIGHUP
#define SIG_POWER_BYE	SIGUSR1
#define SIG_LOCK	SIGUSR2
#define SIG_STOP	SIGTSTP
#ifdef SIGIO
#define SIG_NODEBUG	SIGIO		/* triggerd by command 'debug off' */
#endif


#define BELL		(Ctrl('g'))
#define VBELLWAIT	1 /* No. of seconds a vbell will be displayed */

#define BELL_ON		0 /* No bell has occurred in the window */
#define BELL_FOUND 	1 /* A bell has occurred, but user not yet notified */
#define BELL_DONE	2 /* A bell has occured, user has been notified */

#define BELL_VISUAL	3 /* A bell has occured in fore win, notify him visually */

#define MON_OFF 	0 /* Monitoring is off in the window */
#define MON_ON		1 /* No activity has occurred in the window */
#define MON_FOUND	2 /* Activity has occured, but user not yet notified */
#define MON_DONE	3 /* Activity has occured, user has been notified */

#define DUMP_TERMCAP	0 /* WriteFile() options */
#define DUMP_HARDCOPY	1
#define DUMP_EXCHANGE	2

#define SILENCE_OFF	0
#define SILENCE_ON	1

extern char strnomem[];

/*
 * line modes used by Input()
 */
#define INP_COOKED	0
#define INP_NOECHO	1
#define INP_RAW		2
#define INP_EVERY	4


#ifdef MULTIUSER
struct acl
{
  struct acl *next;
  char *name;
};
#endif

/* register list */
#define MAX_PLOP_DEFS 256
struct plop
{
  char *buf;
  int len;
};

struct baud_values
{
  int idx;	/* the index in the bsd-is padding lookup table */
  int bps;	/* bits per seconds */
  int sym;	/* symbol defined in ttydev.h */
};
