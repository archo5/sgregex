

#ifndef SG_REGEX_H_
#define SG_REGEX_H_


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

#define RX_ALLMODS "mis"

#ifndef RX_STRLENGTHFUNC
#define RX_STRLENGTHFUNC( str ) strlen( str )
#endif


typedef void* (*srx_MemFunc)
(
	void* /* userdata */,
	void* /* ptr */,
	size_t /* size */
);

#ifdef RX_NEED_DEFAULT_MEMFUNC
static void* srx_DefaultMemFunc( void* userdata, void* ptr, size_t size )
{
	(void) userdata;
	if( size )
		return realloc( ptr, size );
	free( ptr );
	return NULL;
}
#endif


typedef char rxChar;
typedef struct _srx_Context srx_Context;


srx_Context* srx_CreateExt( const rxChar* str, size_t strsize, const rxChar* mods, int* errnpos, srx_MemFunc memfn, void* memctx );
#define srx_Create( str, mods ) srx_CreateExt( str, RX_STRLENGTHFUNC(str), mods, NULL, NULL, NULL )
void srx_Destroy( srx_Context* R );
void srx_DumpToFile( srx_Context* R, FILE* fp );
#define srx_DumpToStdout( R ) srx_DumpToFile( R, stdout )

int srx_MatchExt( srx_Context* R, const rxChar* str, size_t size, size_t offset );
#define srx_Match( R, str, off ) srx_MatchExt( R, str, RX_STRLENGTHFUNC(str), off )


#ifdef __cplusplus
}
#endif


#endif /* SG_REGEX_H_ */

