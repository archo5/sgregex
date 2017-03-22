

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
#define RX_MAX_SUBEXPRS 255
#define RX_MAX_REPEATS 0xffffffff
#define RX_NULL_OFFSET 0xffffffff

#define RCF_MULTILINE 0x01 /* ^/$ matches beginning/end of line too */
#define RCF_CASELESS  0x02 /* pre-equalized case for match/range */
#define RCF_DOTALL    0x04 /* "." is compiled as "[^]" instead of "[^\r\n]" */


#define RX_OP_MATCH_DONE        0 /* end of regexp */
#define RX_OP_MATCH_CHARSET     1 /* [...] / character ranges */
#define RX_OP_MATCH_CHARSET_INV 2 /* [^...] / inverse character ranges */
#define RX_OP_MATCH_STRING      3 /* plain sequence of non-special characters */
#define RX_OP_MATCH_BACKREF     4 /* previously found capture group */
#define RX_OP_MATCH_SLSTART     5 /* string / line start */
#define RX_OP_MATCH_SLEND       6 /* string / line end */
#define RX_OP_REPEAT_GREEDY     7 /* try repeated match before proceeding */
#define RX_OP_REPEAT_LAZY       8 /* try proceeding before repeated match */
#define RX_OP_JUMP              9 /* jump to the specified instruction */
#define RX_OP_BACKTRK_JUMP     10 /* jump if backtracked */
#define RX_OP_CAPTURE_START    11 /* save starting position of capture range */
#define RX_OP_CAPTURE_END      12 /* save ending position of capture range */


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

typedef struct rxSubexpr
{
	uint32_t start;
	uint8_t  capture_slot;
}
rxSubexp;

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
	
	rxSubexp   subexprs[ RX_MAX_SUBEXPRS ];
	int        subexprs_count;
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
	uint8_t    flags;
	uint8_t    capture_count;
	
	/* runtime data */
	rxState*   states;
	size_t     states_count;
	size_t     states_mem;
	uint32_t*  iternum;
	size_t     iternum_count;
	size_t     iternum_mem;
	const rxChar* str;
	uint32_t   captures[ RX_MAX_CAPTURES ][2];
};
typedef struct rxExecute rxExecute;

#define RX_NUM_ITERS( e ) ((e)->iternum[ (e)->iternum_count - 1 ])
#define RX_LAST_STATE( e ) ((e)->states[ (e)->states_count - 1 ])


#define rxIsDigit( v ) ((v) >= '0' && (v) <= '9')

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
		fprintf( fp, "  [%03u] ", (unsigned) ( ip - instrs ) );
		switch( ip->op )
		{
		case RX_OP_MATCH_DONE:
			fprintf( fp, "MATCH_DONE\n" );
			break;
			
		case RX_OP_MATCH_CHARSET:
		case RX_OP_MATCH_CHARSET_INV:
			fprintf( fp, "%s (ranges[%u]=",
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
			fprintf( fp, "MATCH_STRING (str[%u]=", (unsigned) ip->len );
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
			
		case RX_OP_MATCH_BACKREF:
			fprintf( fp, "MATCH_BACKREF (slot=%d)\n", (int) ip->from );
			break;
			
		case RX_OP_MATCH_SLSTART:
			fprintf( fp, "MATCH_SLSTART\n" );
			break;
			
		case RX_OP_MATCH_SLEND:
			fprintf( fp, "MATCH_SLEND\n" );
			break;
			
		case RX_OP_REPEAT_GREEDY:
			fprintf( fp, "REPEAT_GREEDY (%u-%u, jump=%u)\n", (unsigned) ip->from, (unsigned) ip->len, (unsigned) ip->start );
			break;
			
		case RX_OP_REPEAT_LAZY:
			fprintf( fp, "REPEAT_LAZY (%u-%u, jump=%u)\n", (unsigned) ip->from, (unsigned) ip->len, (unsigned) ip->start );
			break;
			
		case RX_OP_JUMP:
			fprintf( fp, "JUMP (to=%u)\n", (unsigned) ip->start );
			break;
			
		case RX_OP_BACKTRK_JUMP:
			fprintf( fp, "BACKTRK_JUMP (to=%u)\n", (unsigned) ip->start );
			break;
			
		case RX_OP_CAPTURE_START:
			fprintf( fp, "CAPTURE_START (slot=%d)\n", (int) ip->from );
			break;
			
		case RX_OP_CAPTURE_END:
			fprintf( fp, "CAPTURE_END (slot=%d)\n", (int) ip->from );
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
	
	c->subexprs_count = 1;
	c->subexprs[ 0 ].start = 1;
	c->subexprs[ 0 ].capture_slot = 0;
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

static void rxInstrReserveSpace( rxCompiler* c )
{
	if( c->instrs_count == c->instrs_mem )
	{
		size_t ncnt = c->instrs_mem * 2 + 16;
		rxInstr* ni = (rxInstr*) c->memfn( c->memctx, c->instrs, sizeof(*ni) * ncnt );
		c->instrs = ni;
		c->instrs_mem = ncnt;
	}
}

#define RX_INSTR_REFS_OTHER( op ) ((op) == RX_OP_REPEAT_GREEDY || (op) == RX_OP_REPEAT_LAZY || (op) == RX_OP_JUMP || (op) == RX_OP_BACKTRK_JUMP)

static void rxInsertInstr( rxCompiler* c, uint32_t pos, uint32_t op, uint32_t start, uint32_t from, uint32_t len )
{
	size_t i;
	rxInstr I = { op, start, from, len };
	rxInstrReserveSpace( c );
	assert( pos < c->instrs_count ); /* cannot insert at end, PUSH op needs additional work */
	
	memmove( c->instrs + pos + 1, c->instrs + pos, sizeof(*c->instrs) * ( c->instrs_count - pos ) );
	c->instrs_count++;
	
	for( i = 0; i < c->instrs_count; ++i )
	{
		/* any refs to instructions after the split should be fixed */
		if( c->instrs[ i ].start >= pos &&
			RX_INSTR_REFS_OTHER( c->instrs[ i ].op ) )
			c->instrs[ i ].start++;
	}
	c->instrs[ pos ] = I; /* assume 'start' is pre-adjusted */
}

static void rxPushInstr( rxCompiler* c, uint32_t op, uint32_t start, uint32_t from, uint32_t len )
{
	rxInstr I = { op, start, from, len };
	rxFixLastInstr( c );
	rxInstrReserveSpace( c );
	c->instrs[ c->instrs_count++ ] = I;
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
	int empty = 1;
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
				
				empty = 0;
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
			
		case '^':
			RX_LOG(printf("[^] START (LINE/STRING)\n"));
			
			rxPushInstr( c, RX_OP_MATCH_SLSTART, 0, 0, 0 );
			empty = 0;
			s++;
			break;
			
		case '$':
			RX_LOG(printf("[$] END (LINE/STRING)\n"));
			
			rxPushInstr( c, RX_OP_MATCH_SLEND, 0, 0, 0 );
			empty = 0;
			s++;
			break;
			
		case '(':
			RX_LOG(printf("[(] CAPTURE_START\n"));
			
			if( c->subexprs_count >= RX_MAX_SUBEXPRS )
				goto over_limit;
			
			c->subexprs[ c->subexprs_count ].capture_slot = 0;
			if( c->capture_count < RX_MAX_CAPTURES )
			{
				rxPushInstr( c, RX_OP_CAPTURE_START, 0, c->capture_count, 0 );
				c->subexprs[ c->subexprs_count ].capture_slot = c->capture_count;
				c->capture_count++;
			}
			c->subexprs[ c->subexprs_count ].start = c->instrs_count;
			
			c->subexprs_count++;
			s++;
			break;
			
		case ')':
			RX_LOG(printf("[)] CAPTURE_END\n"));
			
			c->subexprs_count--;
			rxPushInstr( c, RX_OP_CAPTURE_END, 0, c->subexprs[ c->subexprs_count ].capture_slot, 0 );
			s++;
			break;
			
		case '?':
			RX_LOG(printf("[?] 0-1 REPEAT / LAZIFIER\n"));
			if( c->instrs_count && RX_LAST_INSTR( c ).op == RX_OP_REPEAT_GREEDY )
			{
				RX_LAST_INSTR( c ).op = RX_OP_REPEAT_LAZY;
				s++;
				break;
			}
			/* pass thru */
		
		case '+':
		case '*':
		case '{':
			RX_LOG(printf("[*+{?] REPEATS\n"));
			
			/* already has a repeat as last op */
			if( c->instrs_count &&
				( RX_LAST_INSTR( c ).op == RX_OP_REPEAT_LAZY || RX_LAST_INSTR( c ).op == RX_OP_REPEAT_GREEDY ) )
			{
				goto unexpected_token;
			}
			/* must have something to repeat (TODO: support subexprs) */
			if( c->instrs_count &&
				RX_LAST_INSTR( c ).op != RX_OP_MATCH_STRING &&
				RX_LAST_INSTR( c ).op != RX_OP_MATCH_CHARSET &&
				RX_LAST_INSTR( c ).op != RX_OP_MATCH_CHARSET_INV &&
				RX_LAST_INSTR( c ).op != RX_OP_MATCH_BACKREF )
			{
				goto unexpected_token;
			}
			
			/* state validated, add repeats */
			{
				uint32_t min = 0, max = RX_MAX_REPEATS;
				if( *s == '*' ){}
				else if( *s == '+' ){ min = 1; }
				else if( *s == '?' ){ max = 0; }
				else if( *s == '{' )
				{
					RX_SAFE_INCR( s );
					if( !rxIsDigit( *s ) )
						goto unexpected_token;
					min = 0;
					while( rxIsDigit( *s ) )
					{
						uint32_t nmin = min * 10 + *s - '0';
						if( nmin < min )
							goto over_limit;
						min = nmin;
						RX_SAFE_INCR( s );
					}
					if( *s == ',' )
					{
						RX_SAFE_INCR( s );
						if( *s == '}' )
							max = RX_MAX_REPEATS;
						else
						{
							if( !rxIsDigit( *s ) )
								goto unexpected_token;
							max = 0;
							while( rxIsDigit( *s ) )
							{
								uint32_t nmax = max * 10 + *s - '0';
								if( nmax < max )
									goto over_limit;
								max = nmax;
								RX_SAFE_INCR( s );
							}
							if( min > max )
							{
								c->errcode = RXERANGE;
								c->errpos = s - str;
								return;
							}
						}
					}
					else
						max = min;
					if( *s != '}' )
						goto unexpected_token;
				}
				
				rxInsertInstr( c, c->instrs_count - 1, RX_OP_JUMP, c->instrs_count + 1, 0, 0 );
				rxPushInstr( c, RX_OP_REPEAT_GREEDY, c->instrs_count - 1, min, max );
			}
			s++;
			break;
			
		case '.':
			RX_LOG(printf("DOT\n"));
			empty = 0;
			if( c->flags & RCF_DOTALL )
				rxPushInstr( c, RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 0 );
			else
			{
				rxPushChar( c, '\n' );
				rxPushChar( c, '\n' );
				rxPushChar( c, '\r' );
				rxPushChar( c, '\r' );
				rxPushInstr( c, RX_OP_MATCH_CHARSET_INV, 0, c->chars_count - 4, 4 );
			}
			s++;
			break;
			
		case '\\':
			RX_SAFE_INCR( s );
			if( *s == '.' )
			{
				rxPushInstr( c, RX_OP_MATCH_STRING, 0, c->chars_count, 0 );
				rxPushChar( c, *s++ );
				break;
			}
			else if( rxIsDigit( *s ) )
			{
				int dig = *s++ - '0';
				if( dig == 0 || dig >= c->capture_count )
				{
					c->errcode = RXENOREF;
					c->errpos = s - str;
					return;
				}
				rxPushInstr( c, RX_OP_MATCH_BACKREF, 0, dig, 0 );
				break;
			}
			else if( *s == 'd' || *s == 'D' )
			{
				rxPushInstr( c, *s == 'd' ? RX_OP_MATCH_CHARSET : RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 2 );
				rxPushChar( c, '0' );
				rxPushChar( c, '9' );
				s++;
				break;
			}
			else if( *s == 'h' || *s == 'H' )
			{
				rxPushInstr( c, *s == 'h' ? RX_OP_MATCH_CHARSET : RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 4 );
				rxPushChar( c, '\t' );
				rxPushChar( c, '\t' );
				rxPushChar( c, ' ' );
				rxPushChar( c, ' ' );
				s++;
				break;
			}
			else if( *s == 'v' || *s == 'V' )
			{
				rxPushInstr( c, *s == 'v' ? RX_OP_MATCH_CHARSET : RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 2 );
				rxPushChar( c, 0x0A );
				rxPushChar( c, 0x0D );
				s++;
				break;
			}
			else if( *s == 's' || *s == 'S' )
			{
				rxPushInstr( c, *s == 's' ? RX_OP_MATCH_CHARSET : RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 4 );
				rxPushChar( c, 0x09 );
				rxPushChar( c, 0x0D );
				rxPushChar( c, ' ' );
				rxPushChar( c, ' ' );
				s++;
				break;
			}
			else if( *s == 'w' || *s == 'W' )
			{
				rxPushInstr( c, *s == 'w' ? RX_OP_MATCH_CHARSET : RX_OP_MATCH_CHARSET_INV, 0, c->chars_count, 8 );
				rxPushChar( c, 'a' );
				rxPushChar( c, 'z' );
				rxPushChar( c, 'A' );
				rxPushChar( c, 'Z' );
				rxPushChar( c, '0' );
				rxPushChar( c, '9' );
				rxPushChar( c, '_' );
				rxPushChar( c, '_' );
				s++;
				break;
			}
			/* TODO: more character classes */
			/* fallback to character output */
			
		default:
			RX_LOG(printf("CHAR '%c' (string fallback)\n", *s));
			
			empty = 0;
			rxPushInstr( c, RX_OP_MATCH_STRING, 0, c->chars_count, 1 );
			rxPushChar( c, *s++ );
			break;
		}
	}
	
	if( empty )
	{
		RX_LOG(printf("EXPR IS EFFECTIVELY EMPTY\n"));
		c->errcode = RXEEMPTY;
		c->errpos = 0;
		return;
	}
	
	RX_LOG(printf("COMPILE END (last capture)\n"));
	
	rxPushInstr( c, RX_OP_CAPTURE_END, 0, 0, 0 );
	rxPushInstr( c, RX_OP_MATCH_DONE, 0, 0, 0 );
	return;
	
reached_end_too_soon:
	c->errcode = RXEPART;
	return;
unexpected_token:
	c->errcode = RXEUNEXP;
	c->errpos = s - str;
	return;
over_limit:
	c->errcode = RXELIMIT;
	c->errpos = s - str;
	return;
}


static void rxInitExecute( rxExecute* e, srx_MemFunc memfn, void* memctx, rxInstr* instrs, rxChar* chars )
{
	int i;
	
	e->memfn = memfn;
	e->memctx = memctx;
	
	e->instrs = instrs;
	e->chars = chars;
	e->flags = 0;
	e->capture_count = 0;
	
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

static int rxExecDo( rxExecute* e, const rxChar* str, const rxChar* soff, size_t str_size )
{
	const rxInstr* instrs = e->instrs;
	const rxChar* chars = e->chars;
	
	rxPushState( e, soff - str, 0 );
	
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
			match = str_size >= (size_t) ( s->off + 1 );
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
				match = memcmp( &str[ s->off ], &chars[ op->from ], op->len ) == 0;
			RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
			if( match )
			{
				/* replace current single path state with next */
				s->off += op->len;
				s->instr++;
				continue;
			}
			else goto did_not_match;
			
		case RX_OP_MATCH_BACKREF:
			RX_LOG(printf("MATCH_BACKREF at=%d slot=%d: ",s->off,op->from));
			match = e->captures[ op->from ][0] != RX_NULL_OFFSET
				&& e->captures[ op->from ][1] != RX_NULL_OFFSET;
			{
				size_t len = e->captures[ op->from ][1] - e->captures[ op->from ][0];
				printf("~~~len=%d\n",len);
				if( match )
				{
					match = str_size >= s->off + len;
					if( match )
						match = memcmp( &str[ s->off ], &str[ e->captures[ op->from ][0] ], len ) == 0;
				}
				RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
				if( match )
				{
					/* replace current single path state with next */
					s->off += len;
					s->instr++;
					continue;
				}
				else goto did_not_match;
			}
			
		case RX_OP_MATCH_SLSTART:
			RX_LOG(printf("MATCH_SLSTART at=%d: ",s->off));
			match = s->off == 0;
			if( e->flags & RCF_MULTILINE && s->off < str_size && ( str[ s->off ] == '\n' || str[ s->off ] == '\r' ) )
			{
				if( ((size_t)( s->off + 1 )) < str_size && str[ s->off ] == '\r' && str[ s->off + 1 ] == '\n' )
					s->off++;
				s->off++;
				match = 1;
			}
			RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
			if( match )
			{
				s->instr++;
				continue;
			}
			else goto did_not_match;
			
		case RX_OP_MATCH_SLEND:
			RX_LOG(printf("MATCH_SLEND at=%d: ",s->off));
			match = s->off == str_size;
			if( e->flags & RCF_MULTILINE && s->off < str_size && ( str[ s->off ] == '\n' || str[ s->off ] == '\r' ) )
			{
				match = 1;
			}
			RX_LOG(printf("%s\n", match ? "MATCHED" : "FAILED"));
			
			if( match )
			{
				s->instr++;
				continue;
			}
			else goto did_not_match;
			
		case RX_OP_REPEAT_GREEDY:
			RX_LOG(printf("REPEAT_GREEDY flags=%d numiters=%d itercount=%d iterssz=%d\n", s->flags, s->numiters, e->iternum_count ? (int) RX_NUM_ITERS(e) : -1, e->iternum_count ));
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
			RX_LOG(printf("REPEAT_LAZY flags=%d numiters=%d itercount=%d iterssz=%d\n", s->flags, s->numiters, e->iternum_count ? (int) RX_NUM_ITERS(e) : -1, e->iternum_count ));
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
			break;
		}
		e->states[ e->states_count - 1 ].flags |= RX_STATE_BACKTRACKED;
	}
	
	return 0;
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
	R->flags = c.flags;
	R->capture_count = c.capture_count;
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
	R->str = strstart;
	str += offset;
	while( str < strend )
	{
		if( rxExecDo( R, strstart, str, size ) )
			return 1;
		str++;
	}
	return 0;
}

int srx_GetCaptureCount( srx_Context* R )
{
	return R->capture_count;
}

int srx_GetCaptured( srx_Context* R, int which, size_t* pbeg, size_t* pend )
{
	if( which < 0 || which >= R->capture_count )
		return 0;
	if( R->captures[ which ][0] == RX_NULL_OFFSET ||
		R->captures[ which ][1] == RX_NULL_OFFSET )
		return 0;
	if( pbeg ) *pbeg = R->captures[ which ][0];
	if( pend ) *pend = R->captures[ which ][1];
	return 1;
}

int srx_GetCapturedPtrs( srx_Context* R, int which, const rxChar** pbeg, const rxChar** pend )
{
	size_t a, b;
	if( srx_GetCaptured( R, which, &a, &b ) )
	{
		if( pbeg ) *pbeg = R->str + a;
		if( pend ) *pend = R->str + b;
		return 1;
	}
	return 0;
}


rxChar* srx_ReplaceExt( srx_Context* R, const rxChar* str, size_t strsize, const rxChar* rep, size_t repsize, size_t* outsize )
{
	rxChar* out = "";
	const rxChar *from = str, *fromend = str + strsize, *repend = rep + repsize;
	size_t size = 0, mem = 0;
	
#define SR_CHKSZ( szext ) \
	if( (ptrdiff_t)( mem - size ) < (ptrdiff_t)(szext) ) \
	{ \
		size_t nsz = mem * 2 + (size_t)(szext); \
		out = (rxChar*) R->memfn( R->memctx, mem ? out : NULL, sizeof(rxChar) * nsz ); \
		mem = nsz; \
	}
#define SR_ADDBUF( from, to ) \
	SR_CHKSZ( to - from ) \
	memcpy( out + size, from, (size_t)( to - from ) ); \
	size += (size_t)( to - from );
	
	while( from < fromend )
	{
		const rxChar* ofp = NULL, *ep = NULL, *rp;
		if( !srx_MatchExt( R, from, (size_t)( fromend - from ), 0 ) )
			break;
		srx_GetCapturedPtrs( R, 0, &ofp, &ep );
		SR_ADDBUF( from, ofp );
		
		rp = rep;
		while( rp < repend )
		{
			rxChar rc = *rp;
			if( ( rc == '\\' || rc == '$' ) && rp + 1 < repend )
			{
				if( rxIsDigit( rp[1] ) )
				{
					int dig = rp[1] - '0';
					const rxChar *brp, *erp;
					if( srx_GetCapturedPtrs( R, dig, &brp, &erp ) )
					{
						SR_ADDBUF( brp, erp );
					}
					rp += 2;
					continue;
				}
				else if( rp[1] == rc )
				{
					rp++;
				}
			}
			SR_ADDBUF( rp, rp + 1 );
			rp++;
		}
		
		if( from == ep )
			from++;
		else
			from = ep;
	}
	
	SR_ADDBUF( from, fromend );
	if( outsize )
		*outsize = size;
	{
		char nul[1] = {0};
		SR_ADDBUF( nul, &nul[1] );
	}
	return out;
}

void srx_FreeReplaced( srx_Context* R, rxChar* repstr )
{
	R->memfn( R->memctx, repstr, 0 );
}


