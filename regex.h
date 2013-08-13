

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>


#define RXSUCCESS 0
#define RXEINMOD  -1 /* invalid modifier */
#define RXEPART   -2 /* partial (sub-)expression */
#define RXEUNEXP  -3 /* unexpected character */
#define RXERANGE  -4 /* invalid range (min > max) */
#define RXELIMIT  -5 /* too many digits */
#define RXEEMPTY  -6 /* expression is effectively empty */
#define RXENOREF  -7 /* the specified backreference cannot be used here */


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


typedef struct _srx_Context srx_Context;


srx_Context* srx_CreateExt( const RX_Char* str, const RX_Char* mods, int* errnpos, srx_MemFunc memfn, void* memctx );
#define srx_Create( str, mods ) srx_CreateExt( str, mods, NULL, srx_DefaultMemFunc, NULL )
int srx_Destroy( srx_Context* R );
void srx_DumpToStdout( srx_Context* R );

int srx_Match( srx_Context* R, const RX_Char* str, int offset );
int srx_GetCaptureCount( srx_Context* R );
int srx_GetCaptured( srx_Context* R, int which, int* pbeg, int* pend );
int srx_GetCapturedPtrs( srx_Context* R, int which, const RX_Char** pbeg, const RX_Char** pend );


#ifdef __cplusplus
}
#endif

