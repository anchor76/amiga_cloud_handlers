#ifdef __AMIGAOS4__

#include <proto/exec.h>

struct ExecIFace *IExec;
struct Library *newlibbase;
struct Interface *INewlib;
#include "sys/types.h" 

extern int main();

void _start()
{
	IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;
	main();
}

void __initclib(void)
{
	newlibbase = OpenLibrary("newlib.library", 52);
	if ( newlibbase )
	{
		INewlib = GetInterface(newlibbase, "main", 1, NULL); 
	}
}

#endif
