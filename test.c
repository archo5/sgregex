
#include <assert.h>
#include <inttypes.h>

#include "regex.c"


int memusage = 0;

void* testmemfunc( void* ud, void* ptr, size_t sz )
{
	if( ptr )
	{
		size_t* pp = (size_t*) ptr;
		pp--;
		memusage -= *pp;
		ptr = pp;
	}
	ptr = srx_DefaultMemFunc( NULL, ptr, sz ? sz + sizeof(size_t) : 0 );
	if( ptr )
	{
		size_t* pp = (size_t*) ptr;
		*pp = sz;
		memusage += sz;
		return pp+1;
	}
	return NULL;
}

typedef unsigned char byte;
#define GUARD 0xA5
#define PADDING 64

void mem_validate( byte* pp, byte bits, int numbytes )
{
	byte* ppe = pp + numbytes;
	while( pp < ppe )
	{
		assert( *pp == bits );
		pp++;
	}
}

void* testmemfunc2( void* ud, void* ptr, size_t sz )
{
	if( ptr )
	{
		size_t* sp, size;
		byte* pp = (byte*) ptr;
		pp -= PADDING;
		sp = (size_t*) pp;
		size = *(sp-1);
		mem_validate( pp, GUARD, PADDING );
		mem_validate( pp + size - PADDING, GUARD, PADDING );
		ptr = pp;
	}
	ptr = testmemfunc( NULL, ptr, sz ? sz + PADDING * 2 : 0 );
	if( ptr )
	{
		byte* pp = (byte*) ptr;
		memset( pp, GUARD, PADDING );
		memset( pp + sz + PADDING, GUARD, PADDING );
		return pp + PADDING;
	}
	return NULL;
}


#define TEST_DUMP 1
#define TEST_MONKEY 2
#define STREQ( a, b ) (strcmp(a,b)==0)

#define COMPTEST( pat ) \
	col = printf( "compile test: '%s'", pat ); \
	if( col > 25 ) col = 0; else col = 25 - col; \
	R = srx_CreateExt( pat, "", err, testmemfunc2, NULL ); \
	printf( "%*s output code: %d, position %d\n", col, "", err[0], err[1] ); \
	if( R && ( flags & TEST_DUMP ) ){ srx_DumpToStdout( R ); puts(""); } \
	srx_Destroy( R ); \
	assert( memusage == 0 );


int main( int argc, char* argv[] )
{
	int flags = 0, i;
	int err[2], col;
	srx_Context* R;
	
	for( i = 1; i < argc; ++i )
	{
		if( STREQ( argv[i], "dump" ) )
			flags |= TEST_DUMP;
		else if( STREQ( argv[i], "monkey" ) )
			flags |= TEST_MONKEY;
	}
	
	COMPTEST( "a" );
	COMPTEST( "[a-z]" );
	COMPTEST( "[^a-z]" );
	COMPTEST( "[^a-z]*" );
	COMPTEST( "*" );
	COMPTEST( "(a)" );
	COMPTEST( "(.)(.)" );
	COMPTEST( "a{1,2}" );
	COMPTEST( "5*?" );
	COMPTEST( ".*?" );
	COMPTEST( "^.*X.*$" );
	COMPTEST( ".*?*" );
	COMPTEST( "a|b" );
	COMPTEST( "[-z]" );
	COMPTEST( "[a-]" );
	COMPTEST( "[^]z]" );
	
#if 0
	const char* A = "a cat where";
	RX_Char range[] = { 'a', 'z' };
	regex_item items[] =
	{
		{
			NULL, NULL, NULL, NULL,
			range, 1,
			RIT_MATCH, 0, ' ',
			1, 1,
			NULL, NULL, 0,
		},
		{
			NULL, NULL, NULL, NULL,
			range, 1,
			RIT_RANGE, 0, 'c',
			2, 8,
			NULL, NULL, 0,
		},
		{
			NULL, NULL, NULL, NULL,
			range, 1,
			RIT_SPCEND, 0, 'c',
			1, 1,
			NULL, NULL, 0,
		},
		{
			NULL, NULL, NULL, NULL,
			NULL, 0,
			RIT_SUBEXP, 0, 0,
			1, 2,
			NULL, NULL, 0,
		},
		{
			NULL, NULL, NULL, NULL,
			NULL, 0,
			RIT_SUBEXP, 0, 0,
			1, 1,
			NULL, NULL, 0,
		},
		{
			NULL, NULL, NULL, NULL,
			NULL, 0,
			RIT_EITHER, 0, 0,
			0, 1,
			NULL, NULL, 0,
		},
	};
//	items[0].next = items + 1;
//	items[1].prev = items + 0;
//	items[1].next = items + 2;
//	items[2].prev = items + 1;
	items[3].ch = items + 0;
	items[4].ch = items + 3;
	items[5].ch = items + 0;
	items[5].ch2 = items + 1;
	/* ( [a-z]){1,2} */
	/*  |[a-z]{2,8} */
	printf( "'%s' (%p) => %d\n", A, A, regex_scan( A, items+5 ) );
	regex_dump_list( items+5, 0 );
#endif
	
	return 0;
}

