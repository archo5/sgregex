
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


#define srx_Item regex_item
#define _srx_Item _regex_item
#include "regex.h"


#define RX_MALLOC( bytes ) R->memfn( R->memctx, NULL, bytes )
#define RX_ALLOC_N( what, N ) (what*) R->memfn( R->memctx, NULL, sizeof( what ) * N )
#define RX_ALLOC( what ) RX_ALLOC_N( what, 1 )
#define RX_FREE( ptr ) R->memfn( R->memctx, ptr, 0 )


#define RIT_MATCH  1 /* matching */
#define RIT_RANGE  2
#define RIT_SPCBEG 3
#define RIT_SPCEND 4
#define RIT_BKREF  5
#define RIT_EITHER 11 /* control */
#define RIT_SUBEXP 12

#define RIF_LAZY   0x01
#define RIF_INVERT 0x02

#define RCF_MULTILINE 0x01 /* ^/$ matches beginning/end of line too */
#define RCF_CASELESS  0x02 /* pre-equalized case for match/range */
#define RCF_DOTALL    0x04 /* "." is compiled as "[^]" instead of "[^\r\n]" */


struct _regex_item
{
	/* structure */
	regex_item* prev;
	regex_item* next;
	regex_item* ch, *ch2;
	
	RX_Char* range;
	int count;
	
	int type, flags;
	RX_Char a;
	int min, max;
	
	/* match state */
	const RX_Char *matchbeg, *matchend;
	int counter;
};


static int regex_test( const RX_Char* str, srx_Context* ctx );


static int regex_match_once( srx_Context* ctx )
{
	int i;
	regex_item* item = ctx->item;
	const RX_Char* str = item->matchend;
	switch( item->type )
	{
	case RIT_MATCH:
		if( *str == item->a ) return 1;
		break;
	case RIT_RANGE:
		for( i = 0; i < item->count*2; i += 2 )
		{
			if( *str >= item->range[i] && *str <= item->range[i+1] )
				return 1;
		}
		break;
	case RIT_SPCBEG:
		return ctx->string == item->matchend;
	case RIT_SPCEND:
		return !*str;
	case RIT_SUBEXP:
		{
			srx_Context cc = { ctx->string, item->ch };
			if( regex_test( str, &cc ) )
			{
				regex_item* p = item->ch;
				while( p->next )
					p = p->next;
				item->matchend = p->matchend;
				return 1;
			}
		}
		break;
	}
	return 0;
}

static int regex_match_many( srx_Context* ctx )
{
	regex_item* item = ctx->item;
	int i, inv = ( item->flags & RIF_INVERT ) != 0;
	item->matchend = item->matchbeg;
	if( item->type == RIT_EITHER )
	{
		regex_item* chi = item->counter ? item->ch2 : item->ch;
		srx_Context cc = { ctx->string, chi };
		if( regex_test( item->matchbeg, &cc ) )
		{
			regex_item* p = chi;
			while( p->next )
				p = p->next;
			item->matchend = p->matchend;
			return !inv;
		}
		return inv;
	}
	else
	{
		for( i = 0; i < item->counter; ++i )
		{
			if( !*item->matchend )
				break;
			if( !regex_match_once( ctx ) )
				return inv;
			if( item->type == RIT_MATCH || item->type == RIT_RANGE )
				item->matchend++;
		}
	}
	return !inv;
}

static int regex_test( const RX_Char* str, srx_Context* ctx )
{
	regex_item* p = ctx->item;
	p->matchbeg = str;
	p->counter = p->flags & RIF_LAZY ? p->min : p->max;
	
	for(;;)
	{
		srx_Context cc = { ctx->string, p };
		int res = regex_match_many( &cc );
		if( res < 0 )
			return -1;
		else if( res > 0 )
		{
			p = p->next;
			if( !p )
				return 1;
			p->matchbeg = p->prev->matchend;
			p->counter = p->flags & RIF_LAZY ? p->min : p->max;
		}
		else
		{
			while( p )
			{
				if( p->flags & RIF_LAZY )
				{
					p->counter++;
					if( p->counter <= p->max )
						break;
				}
				else
				{
					p->counter--;
					if( p->counter >= p->min )
						break;
				}
				p = p->prev;
			}
			if( !p )
				return 0;
		}
	}
}

static int regex_scan( const RX_Char* str, regex_item* item )
{
	srx_Context ctx = { str, item };
	for(;;)
	{
		int ret = regex_test( str, &ctx );
		if( ret < 0 )
			return 0;
		if( ret > 0 )
			return 1;
		str++;
	}
}


/*
	mapping:
	- [^a-zA-Z] ... RIT_RANGE, optional RIF_INVERT
	- "." ... empty RIT_RANGE + RIF_INVERT
	- "\s" and others ... predefined RIT_RANGE with optional RIF_INVERT
	- "|" ... RIT_EITHER
	- "(..)" ... RIT_SUBEXP
	- "?" ... range = [0,1]
	- "*" ... range = [0,INT_MAX]
	- "+" ... range = [1,INT_MAX]
	- "{1,5}" ... range = [1,5] (other ranges mapped similarly)
	- "^" ... RIT_SPCBEG
	- "$" ... RIT_SPCEND
	- "\1" ... RIT_BKREF
*/

static void regex_free_item( srx_Context* R, regex_item* item );
static void regex_dealloc_item( srx_Context* R, regex_item* item )
{
	if( item->range )
		RX_FREE( item->range );
	if( item->ch ) regex_free_item( R, item->ch );
	if( item->ch2 ) regex_free_item( R, item->ch2 );
	RX_FREE( item );
}

static void regex_free_item( srx_Context* R, regex_item* item )
{
	regex_item *p, *c;
	if( !item )
		return;
	p = item->prev;
	while( p )
	{
		c = p;
		p = p->prev;
		regex_dealloc_item( R, c );
	}
	p = item->next;
	while( p )
	{
		c = p;
		p = p->next;
		regex_dealloc_item( R, c );
	}
	regex_dealloc_item( R, item );
}

static int regex_real_compile( srx_Context* R, const RX_Char** pstr, int sub, int modflags, regex_item** out )
{
#define _RX_ALLOC_NODE( ty ) \
	item = RX_ALLOC( regex_item ); \
	memset( item, 0, sizeof(*item) ); \
	if( citem ) \
	{ \
		citem->next = item; \
		item->prev = citem; \
	} \
	item->type = ty; \
	item->min = 1; \
	item->max = 1;

#define _RXE( err ) do{ error = err; goto fail; }while(1)
	
	const RX_Char* s = *pstr;
	regex_item* item = NULL, *citem = NULL;
	int error = 0;
	while( *s )
	{
		if( sub && *s == ')' )
			break;
		switch( *s )
		{
		case '[':
			{
				const RX_Char* sc;
				int inv = 0, cnt = 0;
				RX_Char* ri;
				s++;
				if( *s == '^' )
				{
					inv = 1;
					s++;
				}
				sc = s;
				if( *sc == ']' )
				{
					sc++;
					cnt++;
				}
				while( *sc && *sc != ']' )
				{
					if( *sc == '-' && sc > s && sc[1] != 0 && sc[1] != ']' )
						sc++;
					else
						cnt++;
					sc++;
				}
				if( !*sc )
					_RXE( RXEPART );
				_RX_ALLOC_NODE( RIT_RANGE );
				if( inv )
					item->flags |= RIF_INVERT;
				item->range = ri = RX_ALLOC_N( RX_Char, cnt * 2 );
				item->count = cnt;
				sc = s;
				if( *sc == ']' )
				{
					sc++;
					ri[0] = ri[1] = *sc;
					ri += 2;
				}
				while( *sc && *sc != ']' )
				{
					if( *sc == '-' && sc > s && sc[1] != 0 && sc[1] != ']' )
					{
						if( ri > item->range )
							*(ri-1) = sc[1];
						sc++;
					}
					else
					{
						ri[0] = ri[1] = *sc;
						ri += 2;
					}
					sc++;
				}
				s = sc;
				if( *s == ']' )
					s++;
			}
			break;
		case ']':
			_RXE( RXEUNEXP );
		case '(':
			{
				int r;
				_RX_ALLOC_NODE( RIT_SUBEXP );
				s++;
				r = regex_real_compile( R, &s, 1, modflags, &item->ch );
				if( r )
					_RXE( r );
				if( *s != ')' )
					_RXE( RXEUNEXP );
				s++;
			}
			break;
		case ')':
			_RXE( RXEUNEXP );
		case '{':
		case '?':
		case '*':
		case '+':
			if( s > *pstr && ( *(s-1) == '}' || *(s-1) == '?' || *(s-1) == '*' || *(s-1) == '+' ) )
			{
				if( *s == '?' )
					item->flags |= RIF_LAZY;
				else
					_RXE( RXEUNEXP );
			}
			else if( item && ( item->type == RIT_MATCH || item->type == RIT_RANGE || item->type == RIT_BKREF || item->type == RIT_SUBEXP ) )
			{
				int min, max;
				if( *s == '{' )
				{
					int ctr;
					s++;
					if( !isdigit( *s ) )
						_RXE( RXEUNEXP );
					min = 0;
					ctr = 8;
					while( isdigit( *s ) && ctr > 0 )
					{
						min = min * 10 + *s++ - '0';
						ctr--;
					}
					if( isdigit( *s ) && ctr == 0 )
						_RXE( RXELIMIT );
					if( *s != ',' || !isdigit(s[1]) )
						_RXE( RXEUNEXP );
					s++;
					max = 0;
					ctr = 8;
					while( isdigit( *s ) && ctr > 0 )
					{
						max = max * 10 + *s++ - '0';
						ctr--;
					}
					if( isdigit( *s ) && ctr == 0 )
						_RXE( RXELIMIT );
					if( min > max )
						_RXE( RXERANGE );
					if( *s != '}' )
						_RXE( RXEUNEXP );
				}
				else if( *s == '?' ){ min = 0; max = 1; }
				else if( *s == '*' ){ min = 0; max = INT_MAX; }
				else if( *s == '+' ){ min = 1; max = INT_MAX; }
				item->min = min;
				item->max = max;
			}
			else
				_RXE( RXEUNEXP );
			s++;
			break;
		case '}':
			_RXE( RXEUNEXP );
		case '|':
			_RX_ALLOC_NODE( RIT_EITHER );
			s++;
			break;
		case '^':
			_RX_ALLOC_NODE( RIT_SPCBEG );
			s++;
			break;
		case '$':
			_RX_ALLOC_NODE( RIT_SPCEND );
			s++;
			break;
		case '\\':
			if( s[1] )
			{
				s++;
				if( *s == '.' )
				{
					_RX_ALLOC_NODE( RIT_MATCH );
					item->a = *s++;
					break;
				}
				else if( isdigit( *s ) )
				{
					_RX_ALLOC_NODE( RIT_BKREF );
					item->a = *s++ - '0';
				}
				/* TODO: character classes */
			}
			else
				_RXE( RXEPART );
		default:
			if( *s == '.' )
			{
				_RX_ALLOC_NODE( RIT_RANGE );
				if( !( modflags & RCF_DOTALL ) )
				{
					item->range = RX_ALLOC_N( RX_Char, 2 * 2 );
					item->range[0] = item->range[1] = '\n';
					item->range[2] = item->range[3] = '\r';
					item->count = 2;
				}
				item->flags |= RIF_INVERT;
			}
			else
			{
				_RX_ALLOC_NODE( RIT_MATCH );
				item->a = *s;
			}
			s++;
			break;
		}
		citem = item;
	}
	*pstr = s;
	while( item->prev )
		item = item->prev;
	*out = item;
	return RXSUCCESS;
fail:
	regex_free_item( R, item );
	return ( error & 0xf ) | ( ( s - R->string ) << 4 );
}

/*
	#### srx_CreateExt ####
*/
srx_Context* srx_CreateExt( const RX_Char* str, const RX_Char* mods, int* errnpos, srx_MemFunc memfn, void* memctx )
{
	int flags = 0, err;
	srx_Context* R = NULL;
	while( *mods )
	{
		switch( *mods )
		{
		case 'm': flags |= RCF_MULTILINE; break;
		case 'i': flags |= RCF_CASELESS; break;
		case 's': flags |= RCF_DOTALL; break;
		default:
			err = RXEINMOD;
			goto fail;
		}
		mods++;
	}
	
	R = (srx_Context*) memfn( memctx, NULL, sizeof(srx_Context) );
	memset( R, 0, sizeof(*R) );
	R->memfn = memfn;
	R->memctx = memctx;
	R->string = str;
	
	err = regex_real_compile( R, &str, 0, flags, &R->item );
	
	if( err )
	{
		memfn( memctx, R, 0 );
		R = NULL;
	}
fail:
	if( errnpos )
	{
		errnpos[0] = err ? ( err & 0xf ) | 0xfffffff0 : 0;
		errnpos[1] = ( err & 0xfffffff0 ) >> 4;
	}
	return R;
}

/*
	#### srx_Destroy ####
*/
int srx_Destroy( srx_Context* R )
{
	if( R )
	{
		srx_MemFunc memfn = R->memfn;
		void* memctx = R->memctx;
		if( R->item )
			regex_free_item( R, R->item );
		memfn( memctx, R, 0 );
	}
	return !!R;
}


static void regex_dump_list( regex_item* items, int lev );
static void regex_dump_item( regex_item* item, int lev )
{
	const char* types[] =
	{
		"-", "MATCH", "RANGE", "SPCBEG", "SPCEND", "BKREF", "-", "-", "-", "-",
		"-", "EITHER", "SUBEXP", "-"
	};
	
	int l = lev;
	while( l --> 0 )
		printf( "- " );
	printf( "type %s ", types[ item->type ] );
	if( item->flags & RIF_INVERT ) printf( "INV " );
	if( item->flags & RIF_LAZY ) printf( "LAZY " );
	switch( item->type )
	{
	case RIT_MATCH: printf( "char %d", (int) item->a ); break;
	case RIT_RANGE:
		for( l = 0; l < item->count; ++l )
		{
			if( l > 0 )
				printf( ", " );
			printf( "%d - %d", (int) item->range[l*2], (int) item->range[l*2+1] );
		}
		break;
	case RIT_BKREF: printf( "#%d", (int) item->a ); break;
	}
	printf( " (%d to %d) (%#p => %#p)\n", item->min, item->max, item->matchbeg, item->matchend );
	
	if( item->ch )
	{
		regex_dump_list( item->ch, lev + 1 );
		if( item->ch2 )
		{
			int l = lev + 1;
			while( l --> 0 )
				printf( "--" );
			printf( "-|-\n" );
			regex_dump_list( item->ch2, lev + 1 );
		}
	}
}
static void regex_dump_list( regex_item* items, int lev )
{
	while( items )
	{
		regex_dump_item( items, lev );
		items = items->next;
	}
}

/*
	#### srx_DumpToStdout ####
*/
void srx_DumpToStdout( srx_Context* R )
{
	regex_dump_list( R->item, 0 );
}

