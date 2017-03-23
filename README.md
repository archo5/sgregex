# SGRegex [SRX] regular expression library v1.2
Simple regular expression library for C (ANSI characters only, limited feature set)

## Usage:

- add `sgregex.h` and `sgregex.c` to your project

## The library supports:

- `.` (dot)
- `*`, `+`, `?` (simple quantifiers)
- `*?`, `+?`, `??` (lazy quantifiers)
- `{<num>}`, `{<num1>,<num2>}` (complex quantifiers)
- `[...]`, `[^...]` (character classes)
- `(...)` (subexpressions/capture ranges)
- `|` (the "or" operator)
- `^`, `$` (beginning/end matchers)
- modifier `m` - multiline
- modifier `i` - case insensitive matcing
- modifier `s` - dot includes newlines

## Change log:

- 1.2 - partial rewrite to fix engine design issues

## API documentation:

#### srx_Create
		const rxChar* regex, // the regular expression
		const rxChar* mods // modifier char list

- creates a regular expression matcher from the specified expression and modifier list
- returns the regular expression matcher ("context")

#### srx_CreateExt
		const rxChar* regex, // the regular expression
		const rxChar* mods, // modifier char list (optional)
		int* errnpos, // pointer to an array of *two* int values: error code and error position (optional)
		srx_MemFunc memfn, // memory allocation function (optional)
		void* memctx // user pointer to pass to the allocation function (optional)

- creates a regular expression matcher from the specified expression and modifier list
- allows to specify custom memory allocation and error output
- returns the regular expression matcher ("context")

#### srx_Destroy
		srx_Context* R // the regex matcher context

- destroys the created matcher object

#### srx_DumpToFile
		srx_Context* R, // the regex matcher context
		FILE* fp // the file to dump the structure

- dumps the structure of the context to file

#### srx_DumpToStdout
		srx_Context* R // the regex matcher context

- dumps the structure of the context to standard output

#### srx_Match
		srx_Context* R, // the regex matcher context
		const rxChar* str, // the string to use for matching
		int offset // the starting point for matching

- searches for a match through the string
- offset is not "approached safely" (with a loop to check for a NUL-byte)
- returns whether a match was found

#### srx_MatchExt
		srx_Context* R, // the regex matcher context
		const rxChar* str, // the string to use for matching
		size_t size, // length of the string
		size_t offset // the starting point for matching

- searches for a match through the string
- string does not need to be null-terminated, size must be passed to `size` argument
- offset is not "approached safely" (with a loop to check for a NUL-byte)
- returns whether a match was found

#### srx_GetCaptureCount
		srx_Context* R // the regex matcher context

- returns the number of capture ranges that were found in the expression (there's always at least one - the whole match)
- the current upper limit is 10 (including the whole match)

#### srx_GetCaptured
		srx_Context* R, // the regex matcher context
		int which, // the capture range number
		int* pbeg, // pointer to output for start offset (optional)
		int* pend // pointer to output for end offset (optional)

- retrieves the offsets from the specified capture range
- returns if the capture range number is in range and if the last match included the capture range (which also means that data was written to specified pointers)

#### srx_GetCapturedPtrs
		srx_Context* R, // the regex matcher context
		int which, // the capture range number
		const rxChar** pbeg, // pointer to output for start offset pointer (optional)
		const rxChar** pend // pointer to output for end offset pointer (optional)

- retrieves the offset pointers from the specified capture range
- returns if the capture range number is in range and if the last match included the capture range (which also means that data was written to specified pointers)

#### srx_Replace
		srx_Context* R, // the regex matcher contex
		const rxChar* str, // the input string
		const rxChar* rep // the replacement string (supports capture ranges in the form of "\1")

- replaces occurrences of pattern in string `str` with string `rep`, returns the replaced string
- the returned string is allocated with the registered allocator

#### srx_ReplaceExt
		srx_Context* R, // the regex matcher contex
		const rxChar* str, // the input string
		size_t strsize, // the length of the input string
		const rxChar* rep, // the replacement string (supports capture ranges in the form of "\1")
		size_t repsize, // the length of the replacement string
		size_t* outsize // pointer to output for length of returned string (optional)

- replaces occurrences of pattern in string `str` with string `rep`, returns the replaced string
- the returned string is allocated with the registered allocator
- none of the strings involved need to be null-terminated

#### srx_FreeReplaced
		srx_Context* R,
		RX_Char* repstr

- frees the string returned by srx_Replace

---

This library was created by Arvīds Kokins (snake5)

