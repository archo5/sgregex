
#include "regex.c"

static void _failed( const char* msg, int line ){ printf( "\nERROR: condition failed - \"%s\"\n\tline %d\n", msg, line ); exit( 1 ); }
#define RX_ASSERT( cond ) if( !(cond) ) _failed( #cond, __LINE__ ); else printf( "+" );
#define SLB( s ) s, sizeof(s)-1
int rxTest2( rxExecute* e, rxProgram* p, const char* s )
{
	int i, ret;
	
	printf( "#" );
	rxInitExecute( e, p, s, strlen( s ) );
	ret = rxExecDo( e );
	for( i = 0; i < 10; ++i )
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
int rxTest( rxProgram* p, const char* s )
{
	rxExecute e;
	int ret = rxTest2( &e, p, s );
	rxFreeExecute( &e );
	return ret;
}

int main()
{
	rxExecute exec;
	
	puts( "=== basic matching (string) ===" );
	{
		rxInstr instrs[] = /* aa */
		{
			{ RX_OP_MATCH_STRING, 0, 0, 2 },
			{ RX_OP_MATCH_DONE, 0, 0, 0 }
		};
		rxChar chars[] = "aa";
		rxProgram program = { instrs, chars };
		RX_ASSERT( rxTest( &program, "aa" ) == 1 );
		RX_ASSERT( rxTest( &program, "aaa" ) == 1 );
		RX_ASSERT( rxTest( &program, "aab" ) == 1 );
		RX_ASSERT( rxTest( &program, "ab" ) == 0 );
		RX_ASSERT( rxTest( &program, "a" ) == 0 );
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
		rxProgram program = { instrs, chars };
		RX_ASSERT( rxTest( &program, "a0" ) == 1 );
		RX_ASSERT( rxTest( &program, "z#" ) == 1 );
		RX_ASSERT( rxTest( &program, "f!" ) == 1 );
		RX_ASSERT( rxTest( &program, "ff" ) == 1 );
		RX_ASSERT( rxTest( &program, "Az" ) == 0 );
		RX_ASSERT( rxTest( &program, "Za" ) == 0 );
		RX_ASSERT( rxTest( &program, "aZ" ) == 0 );
		RX_ASSERT( rxTest( &program, "zA" ) == 0 );
	}
	puts( "" );
	
	puts( "=== basic branching ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( int i = 0; i < 2; ++i )
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
			rxProgram program = { instrs, chars };
			RX_ASSERT( rxTest( &program, "ab" ) == 1 );
			RX_ASSERT( rxTest( &program, "b" ) == 1 );
			RX_ASSERT( rxTest( &program, "aaaab" ) == 1 );
			RX_ASSERT( rxTest( &program, "aaaa" ) == 0 );
		}
	}
	puts( "" );
	
	puts( "=== specific number of iterations ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( int i = 0; i < 2; ++i )
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
			rxProgram program = { instrs, chars };
			RX_ASSERT( rxTest( &program, "aaab" ) == 1 );
			RX_ASSERT( rxTest( &program, "aaabb" ) == 1 );
			RX_ASSERT( rxTest( &program, "aaa" ) == 0 );
			RX_ASSERT( rxTest( &program, "aab" ) == 0 );
			RX_ASSERT( rxTest( &program, "aaaaa" ) == 0 );
			RX_ASSERT( rxTest( &program, "aaaaab" ) == 0 );
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
		rxProgram program = { instrs, chars };
		RX_ASSERT( rxTest( &program, "a" ) == 1 );
		RX_ASSERT( rxTest( &program, "b" ) == 1 );
		RX_ASSERT( rxTest( &program, "c" ) == 0 );
	}
	puts( "" );
	
	puts( "=== nested iterations ===" );
	{
		static const uint32_t types[] = { RX_OP_REPEAT_GREEDY, RX_OP_REPEAT_LAZY };
		for( int i = 0; i < 2; ++i )
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
			rxProgram program = { instrs, chars };
			RX_ASSERT( rxTest( &program, "aab" ) == 1 );
			RX_ASSERT( rxTest( &program, "aabab" ) == 1 );
			RX_ASSERT( rxTest( &program, "ababaab" ) == 1 );
			RX_ASSERT( rxTest( &program, "" ) == 0 );
			RX_ASSERT( rxTest( &program, "aaabb" ) == 0 );
			RX_ASSERT( rxTest( &program, "aaa" ) == 0 );
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
		rxProgram program = { instrs, chars };
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, &program, "abcd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == 2 );
			RX_ASSERT( e.captures[1][1] == 4 );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
			
			/* test if changes are reverted */
			RX_ASSERT( rxTest2( &e, &program, "abc" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
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
		rxProgram program = { instrs, chars };
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, &program, "ab" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
			
			RX_ASSERT( rxTest2( &e, &program, "cd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == 0 );
			RX_ASSERT( e.captures[1][1] == 2 );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
			
			RX_ASSERT( rxTest2( &e, &program, "ac" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[2][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
		}
	}
	
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
		rxProgram program = { instrs, chars };
		{
			rxExecute e;
			RX_ASSERT( rxTest2( &e, &program, "ab" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
			
			RX_ASSERT( rxTest2( &e, &program, "cd" ) == 1 );
			RX_ASSERT( e.captures[0][0] == 0 );
			RX_ASSERT( e.captures[0][1] == 2 );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
			
			RX_ASSERT( rxTest2( &e, &program, "ac" ) == 0 );
			RX_ASSERT( e.captures[0][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[0][1] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][0] == RX_NULL_OFFSET );
			RX_ASSERT( e.captures[1][1] == RX_NULL_OFFSET );
			rxFreeExecute( &e );
		}
	}
	
	puts( "=== all tests done! ===" );
	return 0;
}
