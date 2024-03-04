#ifdef __AROS__

#define DEBUG 0

#include <aros/config.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <aros/startup.h>

THIS_PROGRAM_HANDLES_SYMBOLSETS
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
extern int main();
//extern int main(int argc, char ** argv);
//int (*__main_function_ptr)(int argc, char ** argv) __attribute__((__weak__)) = main;
extern int __nocommandline;
asm(".set __importcommandline, __nocommandline");
extern int __nostdiowin;
asm(".set __importstdiowin, __nostdiowin");
extern int __nowbsupport;
asm(".set __importnowbsupport, __nowbsupport");
extern int __noinitexitsets;
asm(".set __importnoinitexitsets, __noinitexitsets");

extern void __startup_entries_init(void);

#if (AROS_FLAVOUR & AROS_FLAVOUR_BINCOMPAT)
AROS_UFP2(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0)
) __attribute__((section(".aros.startup")));

AROS_UFH2(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0)
)
{
    AROS_USERFUNC_INIT
    struct ExecBase *sysbase = *((APTR *)4);
#else
AROS_UFP3(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0),
    AROS_UFHA(struct ExecBase *,sysbase,A6)
) __attribute__((section(".aros.startup")));
AROS_UFH3(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0),
    AROS_UFHA(struct ExecBase *,sysbase,A6)
)
{
    AROS_USERFUNC_INIT
#endif
    SysBase = sysbase;

    D(bug("Entering __startup_entry(\"%s\", %d, %x)\n", argstr, argsize, SysBase));
    DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39);
    if (!DOSBase) return RETURN_FAIL;
    __argstr  = argstr;
    __argsize = argsize;
    __startup_error = RETURN_FAIL;
	main();
	/*
    __startup_entries_init();
    __startup_entries_next();
	*/
    CloseLibrary((struct Library *)DOSBase);
    D(bug("Leaving __startup_entry\n"));
    return __startup_error;
    AROS_USERFUNC_EXIT
}

/*
static void __startup_main(void)
{
    D(bug("Entering __startup_main\n"));
	main();
    //__startup_error = (*__main_function_ptr) (__argc, __argv);
    D(bug("Leaving __startup_main\n"));
}

ADD2SET(__startup_main, program_entries, 127); 
*/

static int __startup_entry_pos;

void __startup_entries_init(void)
{
	__startup_entry_pos = 1;
}

void __startup_entries_next(void)
{
	void (*entry_func)(void);

	entry_func = (void(*)()) SETNAME(PROGRAM_ENTRIES)[__startup_entry_pos];
	if (entry_func)
	{
		__startup_entry_pos++;
		entry_func();
	}
}

struct Library *aroscbase = NULL;
#include <aros/symbolsets.h>
//THIS_PROGRAM_HANDLES_SYMBOLSETS
DEFINESET(INIT)
DEFINESET(EXIT)
DEFINESET(CTORS)
DEFINESET(DTORS)
int set_call_funcs(const void * const set[], int direction, int test_fail)
{
	int pos, (*func)(void);

	ForeachElementInSet(set, direction, pos, func)
		if (!(*func)() && test_fail)
			return 0;

	return 1;
} 

void __initclib(void)
{
	if(!aroscbase)
	{
		if(!(aroscbase = OpenLibrary("arosc.library", 41)))
			return;

		if(!set_call_funcs(SETNAME(INIT), 1, 1))
		{
			CloseLibrary(aroscbase);
			aroscbase = NULL;
			return;
		}

		set_call_funcs(SETNAME(CTORS), -1, 0);
	} 
}

#endif
