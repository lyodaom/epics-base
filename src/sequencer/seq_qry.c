/**************************************************************************
			GTA PROJECT   AT division
	Copyright, 1990, The Regents of the University of California.
		         Los Alamos National Laboratory

	$Id$
	DESCRIPTION: Task querry & debug routines for run-time sequencer:
	seqShow - prints state set info.
	seqChanShow - printf channel (pv) info.

	ENVIRONMENT: VxWorks
	HISTORY:
25nov91,ajk	Display task names(s) with id(s).
		Display logfile name and file descriptor.
		Moved wait_rtn() to top of loop.
09dec91,ajk	Modified to used state program linked list.
		Added option to display all programs when tid=0.
19dec91,ajk	Allow task name as well as task id.
***************************************************************************/

/*	#define	DEBUG	1	*/

#include	"seq.h"

#ifdef	ANSI
int	seqShow(int);
int	seqChanShow(int);
LOCAL	VOID wait_rtn();
LOCAL	VOID printValue(void *, int, int, int);
LOCAL	VOID printType(int);
#else
int	seqShow();
int	seqChanShow();
LOCAL	VOID wait_rtn();
LOCAL	VOID printValue();
LOCAL	VOID printType();
#endif

/*
 * seqShow() - Querry the sequencer for state information.
 * If a non-zero task id is specified then print the information about
 * the state program, otherwise print a brief summary of all state programs
 */
int seqShow(tid)
int	tid;
{
	SPROG		*pSP;
	SSCB		*pSS;
	STATE		*pST;
	CHAN		*pDB;
	int		nss, nst, nch, status, n;
	float		time;
	char		file_name[100];
	extern SPROG	*seqQryFind();

	tid = taskIdFigure(tid);	/* convert (possible) name to id */
	pSP = seqQryFind(tid);
	if (pSP == NULL)
		return 0;

	/* Print info about state program */
	printf("State Program: \"%s\"\n", pSP->name);
	printf("  Memory layout:\n");
	printf("\tpSP=%d=0x%x\n", pSP, pSP);
	printf("\tdyn_ptr=%d=0x%x\n", pSP->dyn_ptr, pSP->dyn_ptr);
	printf("\tsscb=%d=0x%x\n", pSP->sscb, pSP->sscb);
	printf("\tstates=%d=0x%x\n", pSP->states, pSP->states);
	printf("\tuser_area=%d=0x%x\n", pSP->user_area, pSP->user_area);
	printf("\tuser_size=%d=0x%x\n", pSP->user_size, pSP->user_size);
	printf("\tmac_ptr=%d=0x%x\n", pSP->mac_ptr, pSP->mac_ptr);
	printf("\tscr_ptr=%d=0x%x\n", pSP->scr_ptr, pSP->scr_ptr);
	printf("\tscr_nleft=%d=0x%x\n\n", pSP->scr_nleft, pSP->scr_nleft);
	printf("  initial task id=%d=0x%x\n", pSP->task_id, pSP->task_id);
	printf("  task priority=%d\n", pSP->task_priority);
	printf("  number of state sets=%d\n", pSP->nss);
	printf("  number of channels=%d\n", pSP->nchan);
	printf("  number of channels connected=%d\n", pSP->conn_count);
	printf("  async flag=%d, debug flag=%d, reent flag=%d, conn flag=%d\n",
	 pSP->async_flag, pSP->debug_flag, pSP->reent_flag,
	 pSP->conn_flag);
	printf("  log file fd=%d\n", pSP->logFd);
	status = ioctl(pSP->logFd, FIOGETNAME, file_name);
	if (status != ERROR)
		printf("  log file name=\"%s\"\n", file_name);

	printf("\n");
	pSS = pSP->sscb;

	/* Print state set info */
	for (nss = 0; nss < pSP->nss; nss++, pSS++)
	{
		wait_rtn();

		printf("  State Set: \"%s\"\n", pSS->name);

		printf("  task name=%s;  ", taskName(pSS->task_id));

		printf("  id=%d=0x%x\n", pSS->task_id, pSS->task_id);

		pST = pSS->states;
		printf("  First state = \"%s\"\n", pSS->states->name);

		pST = pSS->states + pSS->current_state;
		printf("  Current state = \"%s\"\n", pST->name);

		pST = pSS->states + pSS->prev_state;
		printf("  Previous state = \"%s\"\n", pST->name);

		time = pSS->time/60.0;
		printf("\tTime state was entered = %.1f seconds)\n", time);
		time = (tickGet() - pSS->time)/60.0;
		printf("\tElapsed time since state was entered = %.1f seconds)\n",
		 time);

		printf("\tNumber delays queued=%d\n", pSS->ndelay);
		for (n = 0; n < pSS->ndelay; n++)
		{
			time = pSS->timeout[n]/60.0;
			printf("\t\ttimeout[%d] = %.1f seconds\n", n, time);
		}
	}

	return 0;
}
/*
 * seqChanQry() - Querry the sequencer for channel information.
 */
int seqChanShow(tid)
int	tid;
{
	SPROG		*pSP;
	extern SPROG	*seqQryFind();
	CHAN		*pDB;
	int		nch;
	float		time;

	tid = taskIdFigure(tid);	/* convert (possible) name to id */
	pSP = seqQryFind(tid);
	if (tid == NULL)
		return 0;

	pDB = pSP->channels;
	printf("State Program: \"%s\"\n", pSP->name);
	printf("Number of channels=%d\n", pSP->nchan);

	for (nch = 0; nch < pSP->nchan; nch++, pDB++)
	{
		printf("\nChannel name: \"%s\"\n", pDB->db_name);
		printf("  Unexpanded name: \"%s\"\n", pDB->db_uname);
		printf("  Variable=%d=0x%x\n", pDB->var, pDB->var);
		printf("  Size=%d bytes\n", pDB->size);
		printf("  Count=%d\n", pDB->count);
		printType(pDB->put_type);
		printValue((void *)pDB->var, pDB->size, pDB->count,
			   pDB->put_type);
		printf("  DB get request type=%d\n", pDB->get_type);
		printf("  DB put request type=%d\n", pDB->put_type);
		printf("  monitor flag=%d\n", pDB->mon_flag);

		if (pDB->monitored)
			printf("  Monitored\n");
		else
			printf("  Not monitored\n");

		if(pDB->connected)
			printf("  Connected\n");
		else
			printf("  Not connected\n");

		if(pDB->get_complete)
			printf("  Last get completed\n");
		else
			printf("  Get not completed or no get issued\n");

		printf("  Status=%d\n", pDB->status);
		printf("  Severity=%d\n", pDB->severity);

		printf("  Delta=%g\n", pDB->delta);
		printf("  Timeout=%g\n", pDB->timeout);
		wait_rtn();
	}

	return 0;
}

/* Read from console until a RETURN is detected */
LOCAL VOID wait_rtn()
{
	char	bfr;
	printf("Hit RETURN to continue\n");
	do {
		read(STD_IN, &bfr, 1);
	} while (bfr != '\n');
}

/* Special union to simplify printing values */
struct	dbr_union {
	union {
		char	c;
		short	s;
		long	l;
		float	f;
		double	d;
	} u;
};

/* Print the current internal value of a database channel */
LOCAL VOID printValue(pvar, size, count, type)
void		*pvar;
int		size, count, type;
{
	struct dbr_union	*pv = pvar;

	if (count > 5)
		count = 5;

	printf("  Value =");
	while (count-- > 0)
	{
		switch (type)
		{
		    case DBR_STRING:
			printf(" %s", pv->u.c);
			break;

	  	    case DBR_CHAR:
			printf(" %d", pv->u.c);
			break;

		    case DBR_SHORT:
			printf(" %d", pv->u.s);
			break;

		    case DBR_LONG:
			printf(" %d", pv->u.l);
			break;

		    case DBR_FLOAT:
			printf(" %g", pv->u.f);
			break;

		    case DBR_DOUBLE:
			printf(" %lg", pv->u.d);
			break;
		}

		pv = (struct dbr_union *)((char *)pv + size);
	}

	printf("\n");
}

/* Print the data type of a channel */
LOCAL VOID printType(type)
int		type;
{
	char		*type_str;

	switch (type)
	{
	    case DBR_STRING:
		type_str = "string";
		break;

	    case DBR_CHAR:
		type_str = "char";
		break;

	    case DBR_SHORT:
		type_str = "short";
		break;

	    case DBR_LONG:
		type_str = "long";
		break;

	    case DBR_FLOAT:
		type_str = "float";
		break;

	    case DBR_DOUBLE:
		type_str = "double";
		break;

	    default:
		type_str = "?";
		break;
	}
	printf("  Type = %s\n", type_str);
}

/* Find a state program associated with a given task id */
LOCAL SPROG *seqQryFind(tid)
int		tid;
{
	SPROG		*pSP;
	extern SPROG	*seqFindProg();

	if (tid == 0)
	{
		seqShowAll();
		return NULL;
	}

	/* Find a state program that has this task id */
	pSP = seqFindProg(tid);
	if (pSP == NULL)
	{
		printf("No state program exists for task id %d\n", tid);
		return NULL;
	}

	/* Check for correct magic number */
	if (pSP->magic != MAGIC)
	{
		printf("Sorry, wrong magic number\n");
		return NULL;
	}

	return pSP;
}

LOCAL int	seqProgCount;

/* This routine is called by seqTraverseProg() for seqShowAll() */
LOCAL seqShowSP(pSP)
SPROG		*pSP;
{
	SSCB		*pSS;

	pSS = pSP->sscb;
	if (seqProgCount++ == 0)
		printf("TID      Prog Name   Task Name\n");

	printf("%-8d %-12s %-12s\n", pSP->task_id,pSP->name, taskName(pSS->task_id) );
}

/* Print a brief summary of all state programs */
LOCAL seqShowAll()
{
	SPROG		*pSP;

	seqProgCount = 0;
	seqTraverseProg(seqShowSP, 0);
	return 0;
}
