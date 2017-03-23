
#include "sgregex.c"

static void _failed( const char* msg, int line ){ printf( "\nERROR: condition failed - \"%s\"\n\tline %d\n", msg, line ); exit( 1 ); }
#define RX_ASSERT( cond ) if( !(cond) ) _failed( #cond, __LINE__ ); else printf( "+" );
#define SLB( s ) s, sizeof(s)-1
static int rxTest2( rxExecute* e, rxInstr* ins, rxChar* chr, const char* s )
{
	int i, ret;
	
	printf( "#" );
	rxInitExecute( e, srx_DefaultMemFunc, NULL, ins, chr );
	e->str = s;
	ret = rxExecDo( e, s, s, strlen( s ) );
	for( i = 0; i < RX_MAX_CAPTURES; ++i )
	{
		if( e->captures[ i ][0] != RX_NULL_OFFSET || e->captures[ i ][1] != RX_NULL_OFFSET )
		{
			printf( "  capture group %d: %d - %d\n", i, e->captures[ i ][0], e->captures[ i ][1] );
		}
	}
	if( ret == 0 )
	{
		/* iteration count stack should be properly reset on failure */
		if( e->iternum_count != 0 )
			printf( "\nERROR: expected iteration number value count = 0, got %d\n", (int) e->iternum_count );
		RX_ASSERT( e->iternum_count == 0 );
	}
	return ret;
}
static void rxFreeExecMNO( rxExecute* e )
{
	e->instrs = NULL; /* prevent this from a free() attempt */
	e->chars = NULL;
	rxFreeExecute( e );
}
static int rxTest( rxInstr* ins, rxChar* chr, const char* s )
{
	rxExecute e;
	int ret = rxTest2( &e, ins, chr, s );
	rxFreeExecMNO( &e );
	return ret;
}

char* allocNNT( const char* str, size_t* len )
{
	char* out;
	*len = strlen( str );
	out = (char*) malloc( *len + 1 );
	strcpy( out, str );
	out[ *len ] = (char) 0x8A;
	return out;
}

static int rxCompTest2( const char* expr, const char* mods, rxInstr* instrs, size_t ilen, rxChar* chars )
{
	size_t i, exprlen = strlen( expr );
	char* exprNNT = allocNNT( expr, &exprlen );
	
	rxCompiler c;
	rxInitCompiler( &c, srx_DefaultMemFunc, NULL );
	while( *mods )
	{
		switch( *mods )
		{
		case 'm': c.flags |= RCF_MULTILINE; break;
		case 'i': c.flags |= RCF_CASELESS; break;
		case 's': c.flags |= RCF_DOTALL; break;
		}
		mods++;
	}
	rxCompile( &c, exprNNT, exprlen );
	if( c.errcode != RXSUCCESS )
	{
		printf( " ^ return code = %d, return offset = %d\n", c.errcode, c.errpos );
	}
	else
	{
		RX_LOG( rxDumpToFile( c.instrs, c.chars, stdout ) );
	}
	
	if( instrs )
	{
		RX_ASSERT( c.errcode == RXSUCCESS );
		if( c.instrs_count != ilen ) printf( "\n ^ instruction count MISMATCH: expected %d, got %d\n", (int) ilen, (int) c.instrs_count );
		RX_ASSERT( c.instrs_count == ilen );
		for( i = 0; i < ilen; ++i )
		{
			RX_ASSERT( c.instrs[ i ].op == instrs[ i ].op );
			RX_ASSERT( c.instrs[ i ].start == instrs[ i ].start );
			RX_ASSERT( c.instrs[ i ].from == instrs[ i ].from );
			RX_ASSERT( c.instrs[ i ].len == instrs[ i ].len );
		}
	}
	
	if( chars )
	{
		RX_ASSERT( c.errcode == RXSUCCESS );
		RX_ASSERT( c.chars_count == strlen( chars ) );
		for( i = 0; i < c.chars_count; ++i )
		{
			RX_ASSERT( c.chars[ i ] == chars[ i ] );
		}
	}
	
	rxFreeCompiler( &c );
	
	free( exprNNT );
	return c.errcode; /* assuming rxFreeCompiler does not nuke this */
}

static int rxCompTest( const char* expr, const char* mods )
{
	return rxCompTest2( expr, mods, NULL, 0, NULL );
}


#define TEST_DUMP 1
int err[2], col, flags = 0;
srx_Context* R;

static void comptest_ext( const char* pat, const char* mod, int retcode )
{
	size_t patlen;
	char* patNNT = allocNNT( pat, &patlen );
	
	col = printf( "compile test: '%s'", pat );
	if( mod )
		col += printf( "(%s)", mod );
	if( col > 25 )
		col = 0;
	else
		col = 25 - col;
	R = srx_CreateExt( patNNT, patlen, mod, err, NULL, NULL );
	if( err[0] != retcode )
	{
		printf( " ^ return code = %d, return offset = %d\n", err[0], err[1] );
	}
	RX_ASSERT( err[0] == retcode );
	
	printf( "%*s output code: %d, position %d\n", col, "", err[0], err[1] );
	
	if( R && ( flags & TEST_DUMP ) )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	
	if( R )
		srx_Destroy( R );
	
	free( patNNT );
}
#define COMPTEST( pat, err ) comptest_ext( pat, NULL, err )
#define COMPTEST2( pat, mod, err ) comptest_ext( pat, mod, err )

void matchtest_ext( const char* mst, const char* pat, const char* mod, int ismatch )
{
	int match;
	size_t mstlen, patlen;
	char* mstNNT = allocNNT( mst, &mstlen );
	char* patNNT = allocNNT( pat, &patlen );
	
	col = printf( "match test: '%s' like '%s'", mst, pat );
	if( mod )
		col += printf( "(%s)", mod );
	if( col > 35 )
		col = 0;
	else
		col = 35 - col;
	R = srx_CreateExt( patNNT, patlen, mod, err, NULL, NULL );
	RX_ASSERT( R );
	match = srx_MatchExt( R, mstNNT, mstlen, 0 );
	printf( ", match: %s\n", match ? "TRUE" : "FALSE" );
	RX_ASSERT( match == ismatch );
	
	if( flags & TEST_DUMP )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	srx_Destroy( R );
	
	free( mstNNT );
	free( patNNT );
}
#define MATCHTEST( mst, pat, ismatch ) matchtest_ext( mst, pat, NULL, ismatch )
#define MATCHTEST2( mst, pat, mod, ismatch ) matchtest_ext( mst, pat, mod, ismatch )

void reptest_ext( const char* mst, const char* pat, const char* mod, const char* rep, const char* res )
{
	char* out;
	size_t mstlen, patlen, replen, reslen, outsize = 0;
	char* mstNNT = allocNNT( mst, &mstlen );
	char* patNNT = allocNNT( pat, &patlen );
	char* repNNT = allocNNT( rep, &replen );
	reslen = strlen( res );
	
	col = printf( "replace test: '%s' like '%s'", mst, pat );
	if( mod )
		col += printf( "(%s)", mod );
	col += printf( " to '%s'", rep );
	if( col > 40 )
		col = 0;
	else
		col = 35 - col;
	R = srx_CreateExt( patNNT, patlen, mod, err, NULL, NULL );
	RX_ASSERT( R );
	
	out = srx_ReplaceExt( R, mstNNT, mstlen, repNNT, replen, &outsize );
	RX_ASSERT( out && outsize );
	printf( " => [%d] '%s'\n", (int) outsize, out );
	RX_ASSERT( outsize == reslen );
	RX_ASSERT( memcmp( out, res, reslen ) == 0 );
	srx_FreeReplaced( R, out );
	
	if( flags & TEST_DUMP )
	{
		srx_DumpToStdout( R );
		puts("");
	}
	srx_Destroy( R );
	
	free( mstNNT );
	free( patNNT );
	free( repNNT );
}
#define REPTEST( mst, pat, rep, res ) reptest_ext( mst, pat, NULL, rep, res )
#define REPTEST2( mst, pat, mod, rep, res ) reptest_ext( mst, pat, mod, rep, res )


int main()
{
	int i;
	
	puts( "##### REGEX ENGINE tests #####" );
	
	puts( "=== basic matching (string) ===" );
	{
		rxInstr instrs[] = /* aa */
		{
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "aa";
		RX_ASSERT( rxTest( instrs, chars, "aa" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "aaa" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "aab" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "ab" ) == 0 );
		RX_ASSERT( rxTest( instrs, chars, "a" ) == 0 );
	}
	puts( "" );
	
	puts( "=== more matching (char ranges) ===" );
	{
		rxInstr instrs[] = /* aa */
		{
			{ RX_OP_MATCH_CHARSET, 0, 0, 2 },
			{ RX_OP_MATCH_CHARSET_INV, 0, 2, 2 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "azAZ";
		RX_ASSERT( rxTest( instrs, chars, "a0" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "z#" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "f!" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "ff" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "Az" ) == 0 );
		RX_ASSERT( rxTest( instrs, chars, "Za" ) == 0 );
		RX_ASSERT( rxTest( instrs, chars, "aZ" ) == 0 );
		RX_ASSERT( rxTest( instrs, chars, "zA" ) == 0 );
	}
	puts( "" );
	
	puts( "=== basic branching ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( i = 0; i < 2; ++i )
		{
			rxInstr instrs[] = /* a*b */
			{
				{ RX_OP_JUMP, 2, 0, 0 },
				{ RX_OP_MATCH_STRING, 0, 0, 1 },
				{ types[ i ], 1, 0, RX_MAX_REPEATS },
				{ RX_OP_MATCH_STRING, 0, 1, 1 },
				{ RX_OP_MATCH_DONE, 0, 0, 0 }
			};
			rxChar chars[] = "ab";
			RX_ASSERT( rxTest( instrs, chars, "ab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "b" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "aaaab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "aaaa" ) == 0 );
		}
	}
	puts( "" );
	
	puts( "=== specific number of iterations ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( i = 0; i < 2; ++i )
		{
			rxInstr instrs[] = /* a{3}b */
			{
				{ RX_OP_JUMP, 2, 0, 0 },
				{ RX_OP_MATCH_STRING, 0, 0, 1 },
				{ types[ i ], 1, 3, 3 },
				{ RX_OP_MATCH_STRING, 0, 1, 1 },
				{ RX_OP_MATCH_DONE, 0, 0, 0 }
			};
			rxChar chars[] = "ab";
			RX_ASSERT( rxTest( instrs, chars, "aaab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "aaabb" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "aaa" ) == 0 );
			RX_ASSERT( rxTest( instrs, chars, "aab" ) == 0 );
			RX_ASSERT( rxTest( instrs, chars, "aaaaa" ) == 0 );
			RX_ASSERT( rxTest( instrs, chars, "aaaaab" ) == 0 );
		}
	}
	puts( "" );
	
	puts( "=== 'or' (|) / backtrack jump ===" );
	{
		rxInstr instrs[] = /* a|b */
		{
			{ RX_OP_BACKTRK_JUMP, 3, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 1 },
			{ RX_OP_JUMP, 4, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 1, 1 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "ab";
		RX_ASSERT( rxTest( instrs, chars, "a" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "b" ) == 1 );
		RX_ASSERT( rxTest( instrs, chars, "c" ) == 0 );
	}
	puts( "" );
	
	puts( "=== nested iterations ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( i = 0; i < 2; ++i )
		{
			rxInstr instrs[] = /* (a{1,2}b){1,3} */
			{
				{ RX_OP_JUMP, 5, 0, 0 },
				{ RX_OP_JUMP, 3, 0, 0 },
				{ RX_OP_MATCH_STRING, 0, 0, 1 },
				{ types[ i ], 2, 1, 2 },
				{ RX_OP_MATCH_STRING, 0, 1, 1 },
				{ types[ i ], 1, 1, 3 },
				{ RX_OP_MATCH_DONE, 0, 0, 0 }
			};
			rxChar chars[] = "ab";
			RX_ASSERT( rxTest( instrs, chars, "aab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "aabab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "ababaab" ) == 1 );
			RX_ASSERT( rxTest( instrs, chars, "" ) == 0 );
			RX_ASSERT( rxTest( instrs, chars, "aaabb" ) == 0 );
			RX_ASSERT( rxTest( instrs, chars, "aaa" ) == 0 );
		}
	}
	puts( "" );
	
	puts( "=== basic capturing ===" );
	{
		rxInstr instrs[] = /* (ab)(cd) */
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_CAPTURE_START, 0, 1, 0 },
			{ RX_OP_MATCH_STRING, 0, 2, 2 },
			{ RX_OP_CAPTURE_END, 0, 1, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "abcd";
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, instrs, chars, "abcd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == 2 );
			RX_ASSERT( e.captures[1][1] == 4 );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
			
			/* test if changes are reverted */
			RX_ASSERT( rxTest2( &e, instrs, chars, "abc" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
		}
	}
	puts( "" );
	
	puts( "=== optional capturing 1 ===" );
	{
		rxInstr instrs[] = /* (ab)|(cd) */
		{
			{ RX_OP_BACKTRK_JUMP, 5, 0, 0 },
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_JUMP, 8, 0, 0 },
			{ RX_OP_CAPTURE_START, 0, 1, 0 },
			{ RX_OP_MATCH_STRING, 0, 2, 2 },
			{ RX_OP_CAPTURE_END, 0, 1, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "abcd";
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, instrs, chars, "ab" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
			
			RX_ASSERT( rxTest2( &e, instrs, chars, "cd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == 0 );
			RX_ASSERT( e.captures[1][1] == 2 );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
			
			RX_ASSERT( rxTest2( &e, instrs, chars, "ac" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
		}
	}
	puts( "" );
	
	puts( "=== optional capturing 2 ===" );
	{
		rxInstr instrs[] = /* (ab|cd) */
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_BACKTRK_JUMP, 4, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_JUMP, 5, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 2, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "abcd";
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, instrs, chars, "ab" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
			
			RX_ASSERT( rxTest2( &e, instrs, chars, "cd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
			
			RX_ASSERT( rxTest2( &e, instrs, chars, "ac" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecMNO( &e );
		}
	}
	puts( "" );
	
	puts( "=== all tests done! ===" );
	
	puts( "##### REGEX COMPILER tests #####" );
	
	puts( "=== plain string ===" );
	RX_ASSERT( rxCompTest( "a", "" ) == RXSUCCESS );
	RX_ASSERT( rxCompTest( "ab", "" ) == RXSUCCESS );
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 1 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "a", "", instrs, 4, "a" ) == RXSUCCESS );
	}
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "ab", "", instrs, 4, "ab" ) == RXSUCCESS );
	}
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 3 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "abc", "", instrs, 4, "abc" ) == RXSUCCESS );
	}
	puts( "" );
	
	puts( "=== character classes ===" );
	RX_ASSERT( rxCompTest( "[a]", "" ) == RXSUCCESS );
	RX_ASSERT( rxCompTest( "[ab]", "" ) == RXSUCCESS );
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_CHARSET, 0, 0, 4 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "[ab]", "", instrs, 4, "aabb" ) == RXSUCCESS );
	}
	RX_ASSERT( rxCompTest( "[^a]", "" ) == RXSUCCESS );
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_CHARSET_INV, 0, 0, 4 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "[^ab]", "", instrs, 4, "aabb" ) == RXSUCCESS );
	}
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_CHARSET, 0, 0, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "[a-b]", "", instrs, 4, "ab" ) == RXSUCCESS );
	}
	RX_ASSERT( rxCompTest( "[a-bcd-e]", "" ) == RXSUCCESS );
	RX_ASSERT( rxCompTest( "[]]", "" ) == RXSUCCESS );
	RX_ASSERT( rxCompTest( "[^]]", "" ) == RXSUCCESS );
	RX_ASSERT( rxCompTest( "[", "" ) == RXEPART );
	RX_ASSERT( rxCompTest( "[a", "" ) == RXEPART );
	RX_ASSERT( rxCompTest( "[b-a]", "" ) == RXERANGE );
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_MATCH_CHARSET, 0, 2, 2 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "aa[b]", "", instrs, 5, "aabb" ) == RXSUCCESS );
	}
	puts( "" );
	
	puts( "=== some escaped special chars ===" );
	{
		rxInstr instrs[] =
		{
			{ RX_OP_CAPTURE_START, 0, 0, 0 },
			{ RX_OP_MATCH_STRING, 0, 0, 1 },
			{ RX_OP_CAPTURE_END, 0, 0, 0 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 },
		};
		RX_ASSERT( rxCompTest2( "\\.", "m", instrs, 4, "." ) == RXSUCCESS );
	}
	puts( "" );
	
	puts( "##### REGEX USAGE tests #####" );
	
	printf( "\n> compilation tests\n\n" );
	COMPTEST( "", RXEEMPTY );
	COMPTEST( "a", RXSUCCESS );
	COMPTEST( "[a-z]", RXSUCCESS );
	COMPTEST( "[^a-z]", RXSUCCESS );
	COMPTEST( "[^a-z]*", RXSUCCESS );
	COMPTEST( "*", RXEUNEXP );
	COMPTEST( "(a)", RXSUCCESS );
	COMPTEST( "(.)(.)", RXSUCCESS );
	COMPTEST( "a)", RXEUNEXP );
	COMPTEST( "a{1,2}", RXSUCCESS );
	COMPTEST( "a{1,}", RXSUCCESS );
	COMPTEST( "a{1}", RXSUCCESS );
	COMPTEST( "5*?", RXSUCCESS );
	COMPTEST( ".*?", RXSUCCESS );
	COMPTEST( "^.*X.*$", RXSUCCESS );
	COMPTEST( ".*?*", RXEUNEXP );
	COMPTEST( "[-z]", RXSUCCESS );
	COMPTEST( "[a-]", RXSUCCESS );
	COMPTEST( "[^]z]", RXSUCCESS );
	COMPTEST( "|b", RXEUNEXP );
	COMPTEST( "a|", RXEPART );
	COMPTEST( "a|b", RXSUCCESS );
	COMPTEST( "a|b|c", RXSUCCESS );
	COMPTEST( "a|b|c|d", RXSUCCESS );
	COMPTEST( "a(b|c)d", RXSUCCESS );
	COMPTEST( "( [a-z]{2,8}){1,2}", RXSUCCESS );
	COMPTEST( " |[a-z]{2,8}", RXSUCCESS );
	COMPTEST( "<([a-z]+)>.*?<\\1>", RXSUCCESS );
	
	printf( "\n> matching tests\n\n" );
	MATCHTEST( "a cat", " c", 1 );
	MATCHTEST( " in the 2013-01-02...", "[0-9]{4}-[0-9]{2}-[0-9]{2}", 1 );
	MATCHTEST( "a cat", "(f|c)at", 1 );
	MATCHTEST( "a cat", "(f|r)at", 0 );
	MATCHTEST( "a cat", "a cat", 1 );
	MATCHTEST( "a cat", "cat$", 1 );
	MATCHTEST( "a cat", "^cat", 0 );
	MATCHTEST( "a cat", "^a", 1 );
	MATCHTEST( "captured", "((x(y))|(p))", 1 );
	MATCHTEST( "a cat", ".*", 1 );
	MATCHTEST( "a cat", ".*?", 1 );
	MATCHTEST( "<tag> b <tag>", "<([a-z]{1,5})>.*?<\\1>", 1 );
	MATCHTEST( "<tag> b <tag>", "[a-z]+", 1 );
	MATCHTEST( "<tag>some text</tag>", "<([a-z]+)>.*?</\\1>", 1 );
	MATCHTEST( " text", "\\*.*?\\*", 0 );
	MATCHTEST( "some *special* text", "\\*.*?\\*", 1 );
	MATCHTEST( "some *special* text", "\\*(.*?)\\*", 1 );
	
	MATCHTEST( "The C API has @\xFFseveral functions\xFF\xFDIterators\xFE used to deal with iterators. "
		"There are essentially three kinds of functions: initialization (@\xFFPushIterator(P)\xFF, "
		"@\xFFGetIterator(P)\xFF), advancing (@\xFFIterAdvance(P)\xFF) and data retrieval "
		"(@\xFFIterPushData(P)\xFF, @\xFFIterGetData(P)\xFF).",
		"(^|[ \xFF\xFC\n\r\t,.])@\xFF([^\xFF\xFD]+)\xFF(\xFD([^\xFE]+)\xFE)?($|[ \xFF\xFC\n\r\t,.])", 1 );
	
	MATCHTEST( "The C API has @\"several functions\"<Iterators> used to deal with iterators. "
		"There are essentially three kinds of functions: initialization (@\"PushIterator(P)\", "
		"@\"GetIterator(P)\"), advancing (@\"IterAdvance(P)\") and data retrieval "
		"(@\"IterPushData(P)\", @\"IterGetData(P)\").",
		"(^|[ \"'\n\r\t,.])@\"([^\"<]+)\"(<([^>]+)>)?($|[ \"'\n\r\t,.])", 1 );
	
	MATCHTEST( " @\"several functions\"<Iterators> ",
		"@\"([^\"<]+)\"(<([^>]+)>)?($|[ \"'\n\r\t,.])", 1 );
	MATCHTEST( " @\"several functions\"<Iterators> ",
		"@\"([^\"<]+)\"(<([^>]+)>)?($|[ \"'\n\r\t,.])", 1 );
	MATCHTEST( " @\"several functions\"<Iterators> ",
		"(^|[ \"'\n\r\t,.])@\"([^\"<]+)\"(<([^>]+)>)?($|[ \"'\n\r\t,.])", 1 );
	
	MATCHTEST( " @\xFFseveral functions\xFF<Iterators> ",
		"[^\xFF]+\xFF(\xFD[^\xFE]+\xFE)? ", 0 );
	MATCHTEST( " @\xFFseveral functions\xFF<Iterators> ",
		"\xFF([^\xFF]+)\xFF(\xFD([^\xFE]+)\xFE)?($|[ ])", 0 );
	MATCHTEST( " @\xFFseveral functions\xFF<Iterators> ",
		"@\xFF([^\xFF\xFD]+)\xFF(\xFD([^\xFE]+)\xFE)?($|[ \xFF\xFC\n\r\t,.])", 0 );
	MATCHTEST( " @\xFFseveral functions\xFF<Iterators> ",
		"@\xFF([^\xFF\xFD]+)\xFF(\xFD([^\xFE]+)\xFE)?($|[ \xFF\xFC\n\r\t,.])", 0 );
	MATCHTEST( " @\xFFseveral functions\xFF<Iterators> ",
		"(^|[ \xFF\xFC\n\r\t,.])@\xFF([^\xFF\xFD]+)\xFF(\xFD([^\xFE]+)\xFE)?($|[ \xFF\xFC\n\r\t,.])", 0 );
	
	printf( "\n> replacement tests\n\n" );
	REPTEST( "some *special* text", "\\*.*?\\*", "SpEcIaL", "some SpEcIaL text" );
	REPTEST( "some *special* text", "\\*(.*?)\\*", "<b>\\1</b>", "some <b>special</b> text" );
	
	printf( "\n> modifier tests\n\n" );
	/* modifier - 'i' */
	COMPTEST2( "A", "i", RXSUCCESS );
	COMPTEST2( "[a-z]", "i", RXSUCCESS );
	MATCHTEST2( "Cat", "A", "i", 1 );
	MATCHTEST2( "CAT", "a", "i", 1 );
	MATCHTEST2( "X", "[a-z]", "i", 1 );
	REPTEST2( "some text here", "[a-z]+", "i", "\\\\($0)$$", "\\(some)$ \\(text)$ \\(here)$" );
	
	/* modifier - 's' */
	COMPTEST2( ".", "s", RXSUCCESS );
	MATCHTEST2( "\r\n", ".", "", 0 );
	MATCHTEST2( "\r\n", ".", "s", 1 );
	
	/* modifier - 'm' */
	COMPTEST2( "^$", "m", RXSUCCESS );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 1$", "m", 1 );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 2$", "m", 1 );
	MATCHTEST2( "line 1\nline 2\nline 3", "^line 3$", "m", 1 );
	
	/* new features */
	printf( "\n> feature tests\n\n" );
	REPTEST( "test 55 cc", "\\d+", "+", "test + cc" );
	REPTEST( "test 66 cc", "[\\d]+", "!", "test ! cc" );
	REPTEST( "aasd 453 dasf78adsf", "\\w+", "[word:$0]", "[word:aasd] [word:453] [word:dasf78adsf]" );
	REPTEST( "abc 234\tdef1", "\\H+", "[not-hspace:$0]", "[not-hspace:abc] [not-hspace:234]\t[not-hspace:def1]" );
	
	/* tests taken from other libraries */
	printf( "\n> misc. tests\n\n" );
	
	MATCHTEST( "feb 6,", "(^|[ (,;])((([Ff]eb[^ ]* *|0*2/|\\* */?)0*[6-7]))([^0-9]|$)", 1 );
	MATCHTEST( "2/7", "(^|[ (,;])((([Ff]eb[^ ]* *|0*2/|\\* */?)0*[6-7]))([^0-9]|$)", 1 );
	MATCHTEST( "feb 1,Feb 6", "(^|[ (,;])((([Ff]eb[^ ]* *|0*2/|\\* */?)0*[6-7]))([^0-9]|$)", 1 );
	MATCHTEST( "x", "((((((((((((((((((((((((((((((x))))))))))))))))))))))))))))))", 1 );
	
	/* random test cases (bugs and such) */
	printf( "\n> bug tests\n\n" );
	MATCHTEST2( "something great", "([a-z]+)thing", "i", 1 );
	MATCHTEST2( " great", "([a-z]+)thing", "i", 0 );
	REPTEST2( "something great", "([a-z]+)thing", "i", "$1what", "somewhat great" );
	
	REPTEST( "`if/else`, `while`, `for`, `do/while`, `foreach`",
		"(^|[ \n\r\t,.])`([^`]+)`($|[ \n\r\t,.])", "$1##$2##$3",
		"##if/else##, ##while##, ##for##, ##do/while##, ##foreach##" );
	
	MATCHTEST( "|  add                    |`   A  +  B    `|   arithmetic   |  no    |   2   |", "(`.*)\\|(.*`)", 0 );
	
	REPTEST( "SGScript API", "[^a-zA-Z0-9]+", "-", "SGScript-API" );
	
#define RX1 "^\\$(\\d{1,3}(\\,\\d{3})*|(\\d+))(\\.\\d{2})?$"
	MATCHTEST( "$0.84", RX1, 1 );
	MATCHTEST( "$123458", RX1, 1 );
	MATCHTEST( "$1,234.89", RX1, 1 );
	MATCHTEST( "$123,456.89", RX1, 1 );
	MATCHTEST( "$1,234,567.89", RX1, 1 );
#define RX2 "^A(BX)*C$"
	MATCHTEST( "AC", RX2, 1 );
	MATCHTEST( "ABXC", RX2, 1 );
	MATCHTEST( "ABXBXC", RX2, 1 );
	
	MATCHTEST( "|`     ! A      `|",
		"(`.*)!(.*`)", 1 );
	REPTEST( "|`     ! A      `|",
		"(`.*)!(.*`)", "$1-~EXCL~-$2",
		"|`     -~EXCL~- A      `|" );
	MATCHTEST( " aaa = 0,", "(a+)( +)?,", 0 );
	MATCHTEST( ", asdf qwe = 0,", " +([a-zA-Z0-9_*& ]+?) +([a-zA-Z0-9_]+)( += +)?,", 0 );
	
	/* http://www.regexlib.com/REDetails.aspx?regexp_id=75 */
#define ISSUE_2_REGEX "(^\\+[0-9]{2}|^\\+[0-9]{2}\\(0\\)|^\\(\\+[0-9]{2}\\)\\(0\\)|^00[0-9]{2}|^0)([0-9]{9}$|[0-9\\-\\s]{10}$)"
	COMPTEST( ISSUE_2_REGEX, RXSUCCESS );
	MATCHTEST( "+31235256677", ISSUE_2_REGEX, 1 );
	MATCHTEST( "+31(0)235256677", ISSUE_2_REGEX, 1 );
	MATCHTEST( "023-5256677", ISSUE_2_REGEX, 1 );
	MATCHTEST( "+3123525667788999", ISSUE_2_REGEX, 0 );
	MATCHTEST( "3123525667788", ISSUE_2_REGEX, 0 );
	MATCHTEST( "232-2566778", ISSUE_2_REGEX, 0 );
	
	MATCHTEST( "const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0,",
		" +([a-zA-Z0-9_*& ]+?) +([a-zA-Z0-9_]+)( += +)?,", 1 );
	
	puts( "=== all tests done! ===" );
	
	return 0;
}
