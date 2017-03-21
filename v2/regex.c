

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define RX_NEED_DEFAULT_MEMFUNC
#define _srx_Context rxExecute
#include "sgregex.h"


#define RX_LOG( x ) x


#define RX_MAX_CAPTURES 10
#define RX_MAX_REPEATS 0xffffffff
#define RX_NULL_OFFSET 0xffffffff

#define RCF_MULTILINE 0x01 /* ^/$ matches beginning/end of line too */
#define RCF_CASELESS  0x02 /* pre-equalized case for match/range */
#define RCF_DOTALL    0x04 /* "." is compiled as "[^]" instead of "[^\r\n]" */


#define RX_OP_MATCH_DONE        0 /* end of regexp */
#define RX_OP_MATCH_CHARSET     1 /* [...] / character ranges */
#define RX_OP_MATCH_CHARSET_INV 2 /* [^...] / inverse character ranges */
#define RX_OP_MATCH_STRING      3 /* plain sequence of non-special characters */
#define RX_OP_REPEAT_GREEDY     4 /* try repeated match before proceeding */
#define RX_OP_REPEAT_LAZY       5 /* try proceeding before repeated match */
#define RX_OP_JUMP              6 /* jump to the specified instruction */
#define RX_OP_BACKTRK_JUMP      7 /* jump if backtracked */
#define RX_OP_CAPTURE_START     8 /* save starting position of capture range */
#define RX_OP_CAPTURE_END       9 /* save ending position of capture range */


typedef struct rxInstr
{
	uint32_t op : 4;     /* opcode */
	uint32_t start : 28; /* pointer to starting instruction in range */
	uint32_t from;       /* beginning of character data / min. repeat count / capture ID */
	uint32_t len;        /* length of character data / max. repeat count */
}
rxInstr;

#define RX_STATE_BACKTRACKED 0x1
typedef struct rxState
{
	uint32_t off : 28;  /* offset in string */
	uint32_t flags : 4;
	uint32_t instr;     /* instruction */
	uint32_t numiters;  /* current iteration count / previous capture value */
}
rxState;

typedef struct rxCompiler
{
	srx_MemFunc memfn;
	void*      memctx;
	
	rxInstr*   instrs;
	size_t     instrs_count;
	size_t     instrs_mem;
	
	rxChar*    chars;
	size_t     chars_count;
	size_t     chars_mem;
	
	uint8_t    flags;
	uint8_t    capture_count;
	int        errcode;
	int        errpos;
}
rxCompiler;

#define RX_LAST_INSTR( e ) ((e)->instrs[ (e)->instrs_count - 1 ])
#define RX_LAST_CHAR( e ) ((e)->chars[ (e)->chars_count - 1 ])

struct rxExecute
{
	srx_MemFunc memfn;
	void*      memctx;
	
	/* compiled program */
	rxInstr*   instrs; /* instruction data (opcodes and fixed-length arguments) */
	rxChar*    chars;  /* character data (ranges and plain sequences for opcodes) */
	
	/* runtime data */
	rxState*   states;
	size_t     states_count;
	size_t     states_mem;
	uint32_t*  iternum;
	size_t     iternum_count;
	size_t     iternum_mem;
	const rxChar* str;
	size_t     str_size;
	uint32_t   captures[ RX_MAX_CAPTURES ][2];
};
typedef struct rxExecute rxExecute;

#define RX_NUM_ITERS( e ) ((e)->iternum[ (e)->iternum_count - 1 ])
#define RX_LAST_STATE( e ) ((e)->states[ (e)->states_count - 1 ])


static int rxMatchCharset( const rxChar* ch, const rxChar* charset, size_t cslen )
{
	const rxChar* cc = charset;
	const rxChar* charset_end = charset + cslen;
	while( cc != charset_end )
	{
		if( *ch >= cc[0] && *ch <= cc[1] )
			return 1;
		cc += 2;
	}
	return 0;
}


void rxDumpToFile( rxInstr* instrs, rxChar* chars, FILE* fp )
{
	size_t i;
	rxInstr* ip = instrs;
	fprintf( fp, "instructions\n{\n" );
	for(;;)
	{
		switch( ip->op )
		{
		case RX_OP_MATCH_DONE:
			fprintf( fp, "    MATCH_DONE\n" );
			break;
			
		case RX_OP_MATCH_CHARSET:
		case RX_OP_MATCH_CHARSET_INV:
			fprintf( fp, "    %s (ranges[%u]=",
				ip->op == RX_OP_MATCH_CHARSET ? "MATCH_CHARSET" : "MATCH_CHARSET_INV",
				(unsigned) ip->len );
			for( i = ip->from; i < ip->from + ip->len; ++i )
			{
				rxChar ch = chars[ i ];
				if( i % 2 == 1 )
					fprintf( fp, "-" );
				if( ch < 32 || ch > 126 )
					fprintf( fp, "[%u]", (unsigned) ch );
				else
					fprintf( fp, "%c", ch );
			}
			fprintf( fp, ")\n" );
			break;
			
		case RX_OP_MATCH_STRING:
			fprintf( fp, "    MATCH_STRING (str[%u]=", (unsigned) ip->len );
			for( i = ip->from; i < ip->from + ip->len; ++i )
			{
				rxChar ch = chars[ i ];
				if( ch < 32 || ch > 126 )
					fprintf( fp, "[%u]", (unsigned) ch );
				else
					fprintf( fp, "%c", ch );
			}
			fprintf( fp, ")\n" );
			break;
			
		case RX_OP_REPEAT_GREEDY:
			fprintf( fp, "    REPEAT_GREEDY (%u-%u, jump=%u)\n", (unsigned) ip->from, (unsigned) ip->len, (unsigned) ip->start );
			break;
			
		case RX_OP_REPEAT_LAZY:
			fprintf( fp, "    REPEAT_LAZY (%u-%u, jump=%u)\n", (unsigned) ip->from, (unsigned) ip->len, (unsigned) ip->start );
			break;
			
		case RX_OP_JUMP:
			fprintf( fp, "    JUMP (to=%u)\n", (unsigned) ip->start );
			break;
			
		case RX_OP_BACKTRK_JUMP:
			fprintf( fp, "    BACKTRK_JUMP (to=%u)\n", (unsigned) ip->start );
			break;
			
		case RX_OP_CAPTURE_START:
			fprintf( fp, "    CAPTURE_START (slot=%d)\n", (int) ip->from );
			break;
			
		case RX_OP_CAPTURE_END:
			fprintf( fp, "    CAPTURE_END (slot=%d)\n", (int) ip->from );
			break;
			
		}
		if( ip->op == RX_OP_MATCH_DONE )
			break;
		ip++;
	}
	fprintf( fp, "}\n" );
}


static void rxInitCompiler( rxCompiler* c, srx_MemFunc memfn, void* memctx )
{
	c->memfn = memfn;
	c->memctx = memctx;
	
	c->instrs = NULL;
	c->instrs_count = 0;
	c->instrs_mem = 0;
	
	c->chars = NULL;
	c->chars_count = 0;
	c->chars_mem = 0;
	
	c->flags = 0;
	c->capture_count = 0;
	c->errcode = RXSUCCESS;
	c->errpos = 0;
}

static void rxFreeCompiler( rxCompiler* c )
{
	if( c->instrs )
	{
		c->memfn( c->memctx, c->instrs, 0 );
		c->instrs = NULL;
	}
	if( c->chars )
	{
		c->memfn( c->memctx, c->chars, 0 );
		c->chars = NULL;
	}
}

static void rxFixLastInstr( rxCompiler* c )
{
	if( c->instrs_count >= 2 &&
		RX_LAST_INSTR( c ).op == RX_OP_MATCH_STRING &&
		c->instrs[ c->instrs_count - 2 ].op == RX_OP_MATCH_STRING )
	{
		/* already have 2 string values, about to change repeat target */
		c->instrs[ c->instrs_count - 2 ].len++;
		c->instrs_count--;
	}
}

static void rxPushInstr( rxCompiler* c, uint32_t op, uint32_t start, uint32_t from, uint32_t len )
{
	rxFixLastInstr( c );
	if( c->instrs_count == c->instrs_mem )
	{
		size_t ncnt = c->instrs_mem * 2 + 16;
		rxInstr* ni = (rxInstr*) c->memfn( c->memctx, c->instrs, sizeof(*ni) * ncnt );
		c->instrs = ni;
		c->instrs_mem = ncnt;
	}
	
	{
		rxInstr I = { op, start, from, len };
		c->instrs[ c->instrs_count++ ] = I;
	}
}

static void rxPushChar( rxCompiler* c, rxChar ch )
{
	if( c->chars_count == c->chars_mem )
	{
		size_t ncnt = c->chars_mem * 2 + 16;
		rxChar* nc = (rxChar*) c->memfn( c->memctx, c->chars, sizeof(*nc) * ncnt );
		c->chars = nc;
		c->chars_mem = ncnt;
	}
	
	c->chars[ c->chars_count++ ] = ch;
}

static void rxCompile( rxCompiler* c, const rxChar* str, size_t strsize )
{
	const rxChar* s = str;
	const rxChar* strend = str + strsize;
	
#define RX_SAFE_INCR( s ) if( ++(s) == strend ){ c->errpos = (s) - str; goto reached_end_too_soon; }
	
	RX_LOG(printf("COMPILE START (first capture)\n"));
	
	rxPushInstr( c, RX_OP_CAPTURE_START, 0, 0, 0 );
	c->capture_count++;
	
	while( s != strend )
	{
		switch( *s )
		{
		case '[':
			{
				const rxChar* sc;
				uint32_t op = RX_OP_MATCH_CHARSET;
				uint32_t start = c->chars_count;
				
				RX_LOG(printf("CHAR '['\n"));
				
				RX_SAFE_INCR( s );
				if( *s == '^' )
				{
					op = RX_OP_MATCH_CHARSET_INV;
					RX_SAFE_INCR( s );
				}
				sc = s;
				
				if( *s == ']' )
				{
					RX_SAFE_INCR( s );
					rxPushChar( c, *s );
					rxPushChar( c, *s );
				}
				while( s != strend && *s != ']' )
				{
					if( *s == '-' && s > sc && s + 1 != strend && s[1] != ']' )
					{
						if( c->chars_count - start )
						{
							if( (unsigned) s[1] < (unsigned) RX_LAST_CHAR( c ) )
							{
								c->errcode = RXERANGE;
								c->errpos = s - str;
								return;
							}
							RX_LAST_CHAR( c ) = s[1];
						}
						RX_SAFE_INCR( s );
					}
					else
					{
						rxPushChar( c, *s );
						rxPushChar( c, *s );
					}
					RX_SAFE_INCR( s );
				}
				if( *s == ']' )
					s++; /* incr may be unsafe as ending here is valid */
				
				rxPushInstr( c, op, 0, start, c->chars_count - start );
			}
			break;
			
			/* already handled by starting characters as these do not support nesting */
		case ']':
		case '}':
			RX_LOG(printf("CHAR ']' or '}' (UNEXPECTED)\n"));
			c->errcode = RXEUNEXP;
			c->errpos = s - str;
			return;
			
		case '(':
			RX_LOG(printf("CHAR '('\n"));
			
			if( c->capture_count < RX_MAX_CAPTURES )
			{
				rxPushInstr( c, RX_OP_CAPTURE_START, 0, c->capture_count, 0 );
				c->capture_count++;
			}
			break;
			
		case ')':
			RX_LOG(printf("CHAR ')'\n"));
			
			rxPushInstr( c, RX_OP_CAPTURE_END, 0, s - str, 0 );
			break;
			
		default:
			RX_LOG(printf("CHAR '%c' (string fallback)\n", *s));
			
			rxPushInstr( c, RX_OP_MATCH_STRING, 0, c->chars_count, 1 );
			rxPushChar( c, *s++ );
			break;
		}
	}
	
	RX_LOG(printf("COMPILE END (last capture)\n"));
	
	rxPushInstr( c, RX_OP_CAPTURE_END, 0, 0, 0 );
	rxPushInstr( c, RX_OP_MATCH_DONE, 0, 0, 0 );
	return;
	
reached_end_too_soon:
	c->errcode = RXEPART;
	return;
}


static void rxInitExecute( rxExecute* e, srx_MemFunc memfn, void* memctx, rxInstr* instrs, rxChar* chars )
{
	int i;
	
	e->memfn = memfn;
	e->memctx = memctx;
	
	e->instrs = instrs;
	e->chars = chars;
	
	e->states = NULL;
	e->states_count = 0;
	e->states_mem = 0;
	e->iternum = NULL;
	e->iternum_count = 0;
	e->iternum_mem = 0;
	
	for( i = 0; i < RX_MAX_CAPTURES; ++i )
	{
		e->captures[ i ][0] = RX_NULL_OFFSET;
		e->captures[ i ][1] = RX_NULL_OFFSET;
	}
}

static void rxFreeExecute( rxExecute* e )
{
	if( e->instrs )
	{
		e->memfn( e->memctx, e->instrs, 0 );
		e->instrs = NULL;
	}
	if( e->chars )
	{
		e->memfn( e->memctx, e->chars, 0 );
		e->chars = NULL;
	}
	if( e->states )
	{
		e->memfn( e->memctx, e->states, 0 );
		e->states = NULL;
	}
	if( e->iternum )
	{
		e->memfn( e->memctx, e->iternum, 0 );
		e->iternum = NULL;
	}
}

static void rxPushState( rxExecute* e, uint32_t off, uint32_t instr )
{
	if( e->states_count == e->states_mem )
	{
		size_t ncnt = e->states_mem * 2 + 16;
		rxState* ns = (rxState*) e->memfn( e->memctx, e->states, sizeof(*ns) * ncnt );
		e->states = ns;
		e->states_mem = ncnt;
	}
	
	rxState* out = &e->states[ e->states_count++ ];
	out->off = off;
	out->flags = 0;
	out->instr = instr;
	out->numiters = 0; /* iteration count is only set from stack */
}

static void rxPushIterCnt( rxExecute* e, uint32_t it )
{
	if( e->iternum_count == e->iternum_mem )
	{
		size_t ncnt = e->iternum_mem * 2 + 16;
		uint32_t* ni = (uint32_t*) e->memfn( e->memctx, e->iternum, sizeof(*ni) * ncnt );
		e->iternum = ni;
		e->iternum_mem = ncnt;
	}
	
	e->iternum[ e->iternum_count++ ] = it;
}

#ifdef NDEBUG
#  define RX_POP_STATE( e ) ((e)->states_count--)
#  define RX_POP_ITER_CNT( e ) ((e)->iternum_count--)
#else
#  define RX_POP_STATE( e ) assert((e)->states_count-- < 0xffffffff)
#  define RX_POP_ITER_CNT( e ) assert((e)->iternum_count-- < 0xffffffff)
#endif

static int rxExecDo( rxExecute* e, const rxChar* strstart, const rxChar* str, size_t str_size )
{
	const rxInstr* instrs = e->instrs;
	const rxChar* chars = e->chars;
	
	rxPushState( e, 0, 0 );
	
	while( e->states_count )
	{
		int match;
		rxState* s = &RX_LAST_STATE( e );
		const rxInstr* op = &instrs[ s->instr ];
		
		RX_LOG(printf("[%d]", s->instr));
		switch( op->op )
		{
		case RX_OP_MATCH_DONE:
			RX_LOG(printf("MATCH_DONE\n"));
			return 1;
			
		case RX_OP_MATCH_CHARSET:
		case RX_OP_MATCH_CHARSET_INV:
			RX_LOG(printf("MATCH_CHARSET%s at=%d size=%d: ",op->op == RX_OP_MATCH_CHARSET_INV ? "_INV" : "",s->off,op->len));
			match = str_size >= s->off + 1;
			if( match )
			{
				match = rxMatchCharset( &str[ s->off ], &chars[ op->from ], op->len );
				if( op->op == RX_OP_MATCH_CHARSET_INV )
					match = !match;
			}
			RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
			if( match )
			{
				/* replace current single path state with next */
				s->off++;
				s->instr++;
				continue;
			}
			else goto did_not_match;
			
		case RX_OP_MATCH_STRING:
			RX_LOG(printf("MATCH_STRING at=%d size=%d: ",s->off,op->len));
			match = str_size >= s->off + op->len;
			if( match )
				match = memcmp( str + s->off, &chars[ op->from ], op->len ) == 0;
			RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
			if( match )
			{
				/* replace current single path state with next */
				s->off += op->len;
				s->instr++;
				continue;
			}
			else goto did_not_match;
			
		case RX_OP_REPEAT_GREEDY:
			RX_LOG(printf("REPEAT_GREEDY flags=%d numiters=%d itercount=%d iterssz=%d\n", s->flags, s->numiters, e->iternum_count ? RX_NUM_ITERS(e) : -1, e->iternum_count ));
			if( s->flags & RX_STATE_BACKTRACKED )
			{
				/* backtracking because next match failed, try advancing */
				if( e->iternum_count && s->numiters + 1 == RX_NUM_ITERS( e ) )
				{
					RX_POP_ITER_CNT( e );
				}
				if( s->numiters < op->from )
					goto did_not_match;
				
				rxPushState( e, s->off, s->instr + 1 ); /* invalidates 's' */
			}
			else
			{
				/* try to match one more */
				s->numiters = RX_NUM_ITERS( e )++;
				if( s->numiters == op->len )
					s->flags = RX_STATE_BACKTRACKED;
				else
					rxPushState( e, s->off, op->start ); /* invalidates 's' */
			}
			continue;
			
		case RX_OP_REPEAT_LAZY:
			RX_LOG(printf("REPEAT_LAZY flags=%d numiters=%d itercount=%d iterssz=%d\n", s->flags, s->numiters, e->iternum_count ? RX_NUM_ITERS(e) : -1, e->iternum_count ));
			if( s->flags & RX_STATE_BACKTRACKED )
			{
				/* backtracking because next match failed, try matching one more of previous */
				if( s->numiters == op->len )
					goto did_not_match;
				
				rxPushState( e, s->off, op->start ); /* invalidates 's' */
				rxPushIterCnt( e, s->numiters + 1 );
			}
			else
			{
				/* try to advance first */
				s->numiters = RX_NUM_ITERS( e );
				RX_POP_ITER_CNT( e );
				if( s->numiters < op->from )
					s->flags = RX_STATE_BACKTRACKED;
				else
					rxPushState( e, s->off, s->instr + 1 ); /* invalidates 's' */
			}
			continue;
			
		case RX_OP_JUMP:
			RX_LOG(printf("JUMP to=%d\n", op->start));
			rxPushIterCnt( e, 0 );
			s->instr = op->start;
			continue;
			
		case RX_OP_BACKTRK_JUMP:
			RX_LOG(printf("BACKTRK_JUMP to=%d\n", op->start));
			if( s->flags & RX_STATE_BACKTRACKED )
			{
				rxPushState( e, s->off, op->start ); /* invalidates 's' */
			}
			else
			{
				rxPushState( e, s->off, s->instr + 1 ); /* invalidates 's' */
			}
			continue;
			
		case RX_OP_CAPTURE_START:
			RX_LOG(printf("CAPTURE_START to=%d off=%d\n", op->from, s->off));
			s->flags |= RX_STATE_BACKTRACKED; /* no branching */
			RX_LAST_STATE( e ).numiters = e->captures[ op->from ][0];
			e->captures[ op->from ][0] = s->off;
			rxPushState( e, s->off, s->instr + 1 );
			continue;
			
		case RX_OP_CAPTURE_END:
			RX_LOG(printf("CAPTURE_END to=%d off=%d\n", op->from, s->off));
			s->flags |= RX_STATE_BACKTRACKED; /* no branching */
			RX_LAST_STATE( e ).numiters = e->captures[ op->from ][1];
			e->captures[ op->from ][1] = s->off;
			rxPushState( e, s->off, s->instr + 1 );
			continue;
		}
		
did_not_match:
		/* backtrack until last untraversed branching op, fail if none found */
		RX_POP_STATE( e );
		while( e->states_count && e->states[ e->states_count - 1 ].flags & RX_STATE_BACKTRACKED )
		{
			RX_POP_STATE( e );
			rxState* s = &e->states[ e->states_count ];
			const rxInstr* op = &instrs[ s->instr ];
			if( op->op == RX_OP_REPEAT_LAZY && e->iternum_count && s->numiters == RX_NUM_ITERS( e ) - 1 )
			{
				RX_POP_ITER_CNT( e );
			}
			if( op->op == RX_OP_CAPTURE_START )
			{
				e->captures[ op->from ][0] = s->numiters;
			}
			if( op->op == RX_OP_CAPTURE_END )
			{
				e->captures[ op->from ][1] = s->numiters;
			}
		}
		if( e->states_count == 0 )
		{
			/* backtracked to the beginning, no matches found */
			return 0;
		}
		e->states[ e->states_count - 1 ].flags |= RX_STATE_BACKTRACKED;
	}
}


srx_Context* srx_CreateExt( const rxChar* str, size_t strsize, const rxChar* mods, int* errnpos, srx_MemFunc memfn, void* memctx )
{
	rxCompiler c;
	srx_Context* R = NULL;
	
	if( !memfn )
		memfn = srx_DefaultMemFunc;
	
	rxInitCompiler( &c, memfn, memctx );
	
	if( mods )
	{
		const rxChar* modbegin = mods;
		while( *mods )
		{
			switch( *mods )
			{
			case 'm': c.flags |= RCF_MULTILINE; break;
			case 'i': c.flags |= RCF_CASELESS; break;
			case 's': c.flags |= RCF_DOTALL; break;
			default:
				c.errcode = RXEINMOD;
				c.errpos = mods - modbegin;
				goto fail;
			}
			mods++;
		}
	}
	
	rxCompile( &c, str, strsize );
	if( c.errcode != RXSUCCESS )
		goto fail;
	
	/* create context */
	R = (rxExecute*) memfn( memctx, NULL, sizeof(rxExecute) );
	rxInitExecute( R, memfn, memctx, c.instrs, c.chars );
	/* transfer ownership of program data */
	c.instrs = NULL;
	c.chars = NULL;
	
	RX_LOG(srx_DumpToStdout( R ));
	
fail:
	if( errnpos )
	{
		errnpos[0] = c.errcode;
		errnpos[1] = c.errpos;
	}
	rxFreeCompiler( &c );
	return R;
}

void srx_Destroy( srx_Context* R )
{
	srx_MemFunc memfn = R->memfn;
	void* memctx = R->memctx;
	rxFreeExecute( R );
	memfn( memctx, R, 0 );
}

void srx_DumpToFile( srx_Context* R, FILE* fp )
{
	rxDumpToFile( R->instrs, R->chars, fp );
}

int srx_MatchExt( srx_Context* R, const rxChar* str, size_t size, size_t offset )
{
	const rxChar* strstart = str;
	const rxChar* strend = str + size;
	if( offset > size )
		return 0;
	str += offset;
	while( str < strend )
	{
		if( rxExecDo( R, strstart, str, size ) )
			return 1;
		str++;
	}
	return 0;
}


