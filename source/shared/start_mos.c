extern int main();

void startup()
{
	main();
}

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/filehandler.h>
#include <dos/notify.h>
#include <dos/exall.h>
#include <exec/memory.h>
#include <exec/initializers.h>
#include <exec/types.h>
#include <utility/utility.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/locale.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <clib/debug_protos.h>

void *libnix_mempool;
int ThisRequiresConstructorHandling;

struct ExecBase *SysBase = NULL;

struct CTDT
{
	int (*fp)(void);
	long priority;
};

extern const void (* const __ctrslist[])(void);
extern const void (* const __dtrslist[])(void);
extern const struct CTDT __ctdtlist[];
static struct CTDT *sort_ctdt(struct CTDT **last);
static struct CTDT *ctdt, *last_ctdt;

struct FuncSeg
{
	ULONG size;
	struct FuncSeg *next;
};

static  void CallFuncArray(const void (* const FuncArray[])(void))
{
	struct FuncSeg *seg;
	int i, num;
	seg = (struct FuncSeg *)(((IPTR)FuncArray) - sizeof(struct FuncSeg));
	num = (seg->size - sizeof(struct FuncSeg)) / sizeof(APTR);
	for (i=0; (i < num) && FuncArray[i]; i++)
	{
		if (FuncArray[i] != ((const void (* const)(void))-1))
			(*FuncArray[i])();
	}
}

static int comp_ctdt(struct CTDT *a, struct CTDT *b)
{
	if (a->priority == b->priority)
		return (0);
	if ((unsigned long)a->priority < (unsigned long) b->priority)
		return (-1);
	return (1);
}

static struct CTDT *sort_ctdt(struct CTDT **last)
{
	struct FuncSeg *seg;
	struct CTDT *last_ctdt;
	seg = (struct FuncSeg *)(((IPTR)__ctdtlist) - sizeof(struct FuncSeg));
	last_ctdt = (struct CTDT *)(((IPTR)seg) + seg->size);
	qsort((struct CTDT *)__ctdtlist, (IPTR)(last_ctdt - __ctdtlist), sizeof(*__ctdtlist), (int (*)(const void *, const void *))comp_ctdt);
	*last = last_ctdt;
	return ((struct CTDT *) __ctdtlist);
} 

static int InitLibnix(void)
{
	ctdt = sort_ctdt(&last_ctdt);
	while (ctdt < last_ctdt)
	{
		if (ctdt->priority >= 0)
		{
			if(ctdt->fp() != 0)
			{
				return 0;
			}
		}
		ctdt++;
	}
	malloc(0);
	CallFuncArray(__ctrslist);
	return 1;
}

static void UnInitLibnix(void)
{
	if (ctdt == last_ctdt)
		CallFuncArray(__dtrslist);
	ctdt = (struct CTDT *)__ctdtlist;
	while (ctdt < last_ctdt)
	{
		if (ctdt->priority < 0)
		{
			if(ctdt->fp != (int (*)(void)) -1)
			{
				ctdt->fp();
			}
		}
		ctdt++;
	}
}

void abort(void) { for (;;) Wait(0); }
void exit(int i) { for (;;) Wait(0); } 
void __chkabort(void) { }

/* De/constructor section-placeholders (MUST be last in the source (don't compile this with -O3)!) */
__asm("\n.section \".ctdt\",\"a\",@progbits\n__ctdtlist:\n.long -1,-1\n");
__asm("\n.section \".ctors\",\"a\",@progbits\n__ctrslist:\n.long -1\n");
__asm("\n.section \".dtors\",\"a\",@progbits\n__dtrslist:\n.long -1\n"); 

# if defined(__PPC__)
ULONG __abox__ = 1;
# endif 

void __initclib(void)
{
	InitLibnix();
}