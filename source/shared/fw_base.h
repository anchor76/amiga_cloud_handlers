// fw_base.h - anchor

#ifndef _FW_BASE_H
#define _FW_BASE_H

//
// windows config
//
#if defined _WIN64 || defined _WIN32

# define PLATFORM_WINDOWS

// configure flags are:
// --------------------
//# define NO_SOUND_SUPPORT
//# define NO_OPENGL_SUPPORT
//# define NO_NETWORK_SUPPORT
//# define NO_BOX2D_SUPPORT
//# define _NOLOG

# define _CRT_SECURE_NO_WARNINGS
# include <winsock2.h>
# include <windows.h>
# include <io.h>
# include <share.h>
# ifdef _DEBUG
#  include "assert.h"
# else
#  define assert(_c_)
# endif

#elif defined __MORPHOS__
# define PLATFORM_AMIGA
# define PLATFORM_MORPHOS
#elif defined __AROS__
# define PLATFORM_AMIGA
# define PLATFORM_AROS
#elif defined __amigaos4__
# define PLATFORM_AMIGA
# define PLATFORM_AMIGAOS4
#elif defined __amigaos__
# define PLATFORM_AMIGA
# define PLATFORM_AMIGAOS3
#endif

#ifdef PLATFORM_AMIGA
//# include "../thirdparty/misc/amiga_math.h"
# include <stdarg.h>
# define assert(_c_)
#endif

#ifndef _CONSOLE

// sdl & opengl

#ifdef PLATFORM_AMIGA
# include <SDL/SDL.h>
# include <inline/SDL.h>
#else
# include <SDL/SDL.h>
#endif

#ifndef NO_SOUND_SUPPORT
# include <SDL/SDL_mixer.h>
#endif

#ifndef NO_OPENGL_SUPPORT
# include <GL/gl.h>
# include <GL/glu.h>
#endif

#if defined PLATFORM_WINDOWS
# pragma comment(lib,"sdl.lib")
# ifndef NO_SOUND_SUPPORT
#  pragma comment(lib,"sdl_mixer.lib")
# endif
# ifndef NO_OPENGL_SUPPORT
#  include "../thirdparty/misc/glext.h"
#  pragma comment(lib,"opengl32.lib")
#  pragma comment(lib,"glu32.lib")
# endif
#endif // PLATFORM_WINDOWS

// box2d

#ifndef NO_BOX2D_SUPPORT
// box2d
#include <Box2D/Box2D.h>
# if defined PLATFORM_WINDOWS
#  ifdef _DEBUG
#   pragma comment(lib,"../../thirdparty/box2d/lib/libbox2d_x64_debug.lib")
#  else
#   pragma comment(lib,"../../thirdparty/box2d/lib/libbox2d_x64_release.lib")
#  endif
# endif
#endif

#endif // _CONSOLE

// common
//#include "../thirdparty/misc/utImage.h"
#include "../thirdparty/misc/irrlicht.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

// fix sized array, for simple elements. (in-loop erase only supported in forward direction)
template <class T>
class cFixedArray
{
public:
	cFixedArray(u32 pcapacity=1)
	{
		_capacity = pcapacity;
		_data = new T[_capacity];
		_size = 0;
	}
	virtual ~cFixedArray()
	{
		delete [] _data;
	}
	T& operator [](u32 index)
	{
		_IRR_DEBUG_BREAK_IF(index>=_size) // access violation
			return _data[index];
	}
	const T& operator [](u32 index) const
	{
		_IRR_DEBUG_BREAK_IF(index>=_size) // access violation
			return _data[index];
	}
	void push_back(T item)
	{
		_IRR_DEBUG_BREAK_IF(_size==_capacity) // access violation
			_data[_size++] = item;
	}
	void pop_back()
	{
		_IRR_DEBUG_BREAK_IF(_size==0) // access violation
			_size--;
	}
	void insertOrdered(T item, u32 index)
	{
		_IRR_DEBUG_BREAK_IF(_size==_capacity) // access violation
			_IRR_DEBUG_BREAK_IF(index>_size) // access violation
			for(u32 _i=_size;_i!=index;_i--)
			{
				_data[_i] = _data[_i-1];
			}
			_data[index] = item;
			_size++;
	}
	void erase(u32 index) // quick, but the order will be changed !!!
	{
		_IRR_DEBUG_BREAK_IF(index>=_size) // access violation
			_data[index] = _data[--_size];
	}
	void eraseOrdered(u32 index, u32 count=1) // slow, but order will be kept
	{
		_IRR_DEBUG_BREAK_IF(index+count>_size) // access violation
			for(u32 _i=index;_i+count<_size;_i++)
			{
				_data[_i] = _data[_i+count];
			}
			_size -= count;
	}
	T& getLast()
	{
		_IRR_DEBUG_BREAK_IF(!_size) // access violation
			return _data[_size-1];
	}
	u32 capacity() const { return _capacity; }
	void __free() // before overwrite
	{
		delete [] _data;
		_data = NULL;
	}
	void setCapacity(u32 newCapacity, bool dirty=false)
	{
		if ( !dirty ) // data is invalid, no need to freed
		{
			delete [] _data;
		}
		_capacity = newCapacity;
		_data = new T[_capacity];
		_size = 0;
	}
	u32 size() const { return _size; }
	void setSize(u32 psize)
	{
		_IRR_DEBUG_BREAK_IF(psize>_capacity) // access violation
			_size = psize;
	}
	void clear() { _size = 0; }
	bool isFull() const { return (_size==_capacity); }
private:
	u32 _capacity;
	u32 _size;
	T* _data;
};

// defines

#define STR_LEN 256
#define NUM_KEYS 512

#endif // _FW_BASE_H
