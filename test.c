
#include <assert.h>

#include "regex.c"


size_t memusage = 0;

void* testmemfunc( void* ud, void* ptr, size_t sz )
{
	(void) ud;
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

void mem_validate( byte* pp, byte bits, int numbytes, int region )
{
	byte* ppe = pp + numbytes;
	while( pp < ppe )
	{
		if( region )
			assert( *pp == bits && "AFTER" );
		else
			assert( *pp == bits && "BEFORE" );
		pp++;
	}
}

void* testmemfunc2( void* ud, void* ptr, size_t sz )
{
	(void) ud;
	if( ptr )
	{
		size_t* sp, size;
		byte* pp = (byte*) ptr;
		pp -= PADDING;
		sp = (size_t*) pp;
		size = *(sp-1);
		mem_validate( pp, GUARD, PADDING, 0 );
		mem_validate( pp + size - PADDING, GUARD, PADDING, 1 );
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
	
int err[2], col, flags = 0;
srx_Context* R;

void comptest_ext( const char* pat, const char* mod )
{
	col = printf( "compile test: '%s'", pat );
	if( mod )
		col += printf( "(%s)", mod );
	if( col > 25 )
		col = 0;
	else
		col = 25 - col;
	R = srx_CreateExt( pat, mod, err, testmemfunc2, NULL );
	printf( "%*s output code: %d, position %d\n", col, "", err[0], err[1] );
	if( R && ( flags & TEST_DUMP ) )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	srx_Destroy( R );
	assert( memusage == 0 );
}
#define COMPTEST( pat ) comptest_ext( pat, NULL )
#define COMPTEST2( pat, mod ) comptest_ext( pat, mod )

void matchtest_ext( const char* str, const char* pat, const char* mod )
{
	col = printf( "match test: '%s' like '%s'", str, pat );
	if( mod )
		col += printf( "(%s)", mod );
	if( col > 35 )
		col = 0;
	else
		col = 35 - col;
	R = srx_CreateExt( pat, mod, err, testmemfunc2, NULL );
	printf( "%*s output code: %d, position %d", col, "", err[0], err[1] );
	if( R )
	{
		printf( ", match: %s\n", srx_Match( R, str, 0 ) ? "TRUE" : "FALSE" );
	}
	else
		puts("");
	if( R && ( flags & TEST_DUMP ) )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	srx_Destroy( R );
	assert( memusage == 0 );
}
#define MATCHTEST( str, pat ) matchtest_ext( str, pat, NULL )
#define MATCHTEST2( str, pat, mod ) matchtest_ext( str, pat, mod )

void reptest_ext( const char* str, const char* pat, const char* mod, const char* rep )
{
	char* out;
	col = printf( "replace test: '%s' like '%s'", str, pat );
	if( mod )
		col += printf( "(%s)", mod );
	col += printf( " to '%s'", rep );
	if( col > 40 )
		col = 0;
	else
		col = 35 - col;
	R = srx_CreateExt( pat, mod, err, testmemfunc2, NULL );
	printf( "%*s output code: %d, position %d", col, "", err[0], err[1] );
	if( R )
	{
		out = srx_Replace( R, str, rep );
		printf( " => '%s'\n", out );
		srx_FreeReplaced( R, out );
	}
	else puts("");
	if( R && ( flags & TEST_DUMP ) )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	srx_Destroy( R );
	assert( memusage == 0 );
}
#define REPTEST( str, pat, rep ) reptest_ext( str, pat, NULL, rep )
#define REPTEST2( str, pat, mod, rep ) reptest_ext( str, pat, mod, rep )


int main( int argc, char* argv[] )
{
	int i;
	for( i = 1; i < argc; ++i )
	{
		if( STREQ( argv[i], "dump" ) )
			flags |= TEST_DUMP;
		else if( STREQ( argv[i], "monkey" ) )
			flags |= TEST_MONKEY;
	}
	
	printf( "\n> compilation tests\n\n" );
	COMPTEST( "" );
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
	COMPTEST( "[-z]" );
	COMPTEST( "[a-]" );
	COMPTEST( "[^]z]" );
	COMPTEST( "|b" );
	COMPTEST( "a|" );
	COMPTEST( "a|b" );
	COMPTEST( "a(b|c)d" );
	COMPTEST( "( [a-z]{2,8}){1,2}" );
	COMPTEST( " |[a-z]{2,8}" );
	COMPTEST( "<([a-z]+)>.*?<\\1>" );
	
	printf( "\n> matching tests\n\n" );
	MATCHTEST( "a cat", " c" );
	MATCHTEST( " in the 2013-01-02...", "[0-9]{4}-[0-9]{2}-[0-9]{2}" );
	MATCHTEST( "a cat", "(f|c)at" );
	MATCHTEST( "a cat", "(f|r)at" );
	MATCHTEST( "a cat", "a cat" );
	MATCHTEST( "a cat", "cat$" );
	MATCHTEST( "a cat", "^cat" );
	MATCHTEST( "a cat", "^a" );
	MATCHTEST( "captured", "((x(y))|(p))" );
	MATCHTEST( "a cat", ".*" );
	MATCHTEST( "a cat", ".*?" );
	MATCHTEST( "<tag> b <tag>", "<([a-z]{1,5})>.*?<\\1>" );
	MATCHTEST( "<tag> b <tag>", "[a-z]+" );
	MATCHTEST( "<tag>some text</tag>", "<([a-z]+)>.*?</\\1>" );
	MATCHTEST( " text", "\\*.*?\\*" );
	MATCHTEST( "some *special* text", "\\*.*?\\*" );
	MATCHTEST( "some *special* text", "\\*(.*?)\\*" );
	
	printf( "\n> replacement tests\n\n" );
	REPTEST( "some *special* text", "\\*.*?\\*", "SpEcIaL" );
	REPTEST( "some *special* text", "\\*(.*?)\\*", "<b>\\1</b>" );
	
	printf( "\n> modifier tests\n\n" );
	/* modifier - 'i' */
	COMPTEST2( "A", "i" );
	COMPTEST2( "[a-z]", "i" );
	MATCHTEST2( "Cat", "A", "i" );
	MATCHTEST2( "CAT", "a", "i" );
	MATCHTEST2( "X", "[a-z]", "i" );
	REPTEST2( "some text here", "[a-z]+", "i", "\\\\($0)$$" );
	
	/* modifier - 's' */
	COMPTEST2( ".", "s" );
	MATCHTEST2( "\r\n", ".", "" );
	MATCHTEST2( "\r\n", ".", "s" );
	
	/* modifier - 'm' */
	COMPTEST2( "^$", "m" );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 1$", "m" );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 2$", "m" );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 3$", "m" );
	
	/* random test cases (bugs and such) */
	MATCHTEST2( "something awful", "([a-z]+)thing", "i" );
	MATCHTEST2( " awful", "([a-z]+)thing", "i" );
	REPTEST2( "something awful", "([a-z]+)thing", "i", "$1what" );
	
	REPTEST( "`if/else`, `while`, `for`, `do/while`, `foreach`", "(^|[ \n\r\t,.])`([^`]+)`($|[ \n\r\t,.])", "$1##$2##$3" );
	
	MATCHTEST( "|  add                    |`   A  +  B    `|   arithmetic   |  no    |   2   |", "(`.*)\\|(.*`)" );
	
	assert( memusage == 0 );
	
	return 0;
}

