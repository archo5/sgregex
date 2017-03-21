

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


typedef char rxChar;


#define RX_LOG( x ) x


#define RX_MAX_REPEATS 0xffffffff
#define RX_NULL_OFFSET 0xffffffff


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

typedef struct rxProgram
{
	rxInstr* instrs; /* instruction data (opcodes and fixed-length arguments) */
	rxChar*  chars;  /* character data (ranges and plain sequences for opcodes) */
}
rxProgram;

#define RX_STATE_BACKTRACKED 0x1
typedef struct rxState
{
	uint32_t off : 28;  /* offset in string */
	uint32_t flags : 4;
	uint32_t instr;     /* instruction */
	uint32_t numiters;  /* current iteration count / previous capture value */
}
rxState;

typedef struct rxExecute
{
	const rxProgram* program;
	rxState*   states;
	size_t     states_count;
	size_t     states_mem;
	uint32_t*  iternum;
	size_t     iternum_count;
	size_t     iternum_mem;
	const rxChar* str;
	size_t     str_size;
	uint32_t   captures[10][2];
}
rxExecute;

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


static void rxInitExecute( rxExecute* e, const rxProgram* p, const rxChar* str, size_t size )
{
	e->program = p;
	e->states = NULL;
	e->states_count = 0;
	e->states_mem = 0;
	e->iternum = NULL;
	e->iternum_count = 0;
	e->iternum_mem = 0;
	e->str = str;
	e->str_size = size;
	
	{
		int i;
		for( i = 0; i < 10; ++i )
		{
			e->captures[ i ][0] = RX_NULL_OFFSET;
			e->captures[ i ][1] = RX_NULL_OFFSET;
		}
	}
}

static void rxFreeExecute( rxExecute* e )
{
	if( e->states )
	{
		free( e->states );
		e->states = NULL;
	}
	if( e->iternum )
	{
		free( e->iternum );
		e->iternum = NULL;
	}
}

static void rxPushState( rxExecute* e, uint32_t off, uint32_t instr )
{
	if( e->states_count == e->states_mem )
	{
		size_t ncnt = e->states_mem * 2 + 16;
		rxState* ns = (rxState*) realloc( e->states, sizeof(*ns) * ncnt );
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
		uint32_t* ni = (uint32_t*) realloc( e->iternum, sizeof(*ni) * ncnt );
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

static int rxExecDo( rxExecute* e )
{
	const rxProgram* p = e->program;
	
	rxPushState( e, 0, 0 );
	
	while( e->states_count )
	{
		int match;
		rxState* s = &RX_LAST_STATE( e );
		rxInstr* op = &p->instrs[ s->instr ];
		
		RX_LOG(printf("[%d]", s->instr));
		switch( op->op )
		{
		case RX_OP_MATCH_DONE:
			RX_LOG(printf("MATCH_DONE\n"));
			return 1;
			
		case RX_OP_MATCH_CHARSET:
		case RX_OP_MATCH_CHARSET_INV:
			RX_LOG(printf("MATCH_CHARSET%s at=%d size=%d: ",op->op == RX_OP_MATCH_CHARSET_INV ? "_INV" : "",s->off,op->len));
			match = e->str_size >= s->off + 1;
			if( match )
			{
				match = rxMatchCharset( &e->str[ s->off ], &p->chars[ op->from ], op->len );
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
			match = e->str_size >= s->off + op->len;
			if( match )
				match = memcmp( e->str + s->off, &p->chars[ op->from ], op->len ) == 0;
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
			rxInstr* op = &p->instrs[ s->instr ];
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

