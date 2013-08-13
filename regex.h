

#pragma once
#include <stddef.h>


#ifdef __cplusplus
#define SRX_DECLARE extern
#else
#define SRX_DECLARE static
#endif


#if SRX_DLL && !defined( __GNUC__ )
#  if BUILDING_SRX
#    define SRX_APIFUNC __declspec(dllexport)
#  else
#    define SRX_APIFUNC __declspec(dllimport)
#  endif
#else
#  define SRX_APIFUNC
#endif


#define RXSUCCESS 0
#define RXEINMOD  -1 /* invalid modifier */
#define RXEPART   -2 /* partial expression (not all groups were closed) */
#define RXEUNEXP  -3 /* unexpected character */
#define RXERANGE  -4 /* invalid range (min > max) */
#define RXELIMIT  -5 /* too many digits */


typedef void* (*srx_MemFunc)
(
	void* /* userdata */,
	void* /* ptr */,
	size_t /* size */
);

static void* srx_DefaultMemFunc( void* userdata, void* ptr, size_t size )
{
	if( size )
		return realloc( ptr, size );
	free( ptr );
	return NULL;
}


#ifdef RX_CHARACTER_TYPE
typedef RX_CHARACTER_TYPE RX_Char;
#else
typedef char RX_Char;
#endif


typedef struct _srx_Item srx_Item;
typedef struct _srx_Context
{
	const RX_Char* string;
	srx_Item*      item;
	srx_MemFunc    memfn;
	void*          memctx;
}
srx_Context;


SRX_APIFUNC srx_Context* srx_CreateExt( const RX_Char* str, const RX_Char* mods, int* errnpos, srx_MemFunc memfn, void* memctx );
#define srx_Create( str, mods ) srx_CreateExt( str, mods, NULL, srx_DefaultMemFunc, NULL )
SRX_APIFUNC int srx_Destroy( srx_Context* R );
SRX_APIFUNC void srx_DumpToStdout( srx_Context* R );

