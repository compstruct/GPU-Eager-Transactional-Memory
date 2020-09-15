/* A Bison parser, made by GNU Bison 3.0.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2013 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.0.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1


/* Substitute the variable and function names.  */
#define yyparse         ptx_parse
#define yylex           ptx_lex
#define yyerror         ptx_error
#define yydebug         ptx_debug
#define yynerrs         ptx_nerrs

#define yylval          ptx_lval
#define yychar          ptx_char

/* Copy the first part of user declarations.  */

#line 75 "ptx.tab.c" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "ptx.tab.h".  */
#ifndef YY_PTX_PTX_TAB_H_INCLUDED
# define YY_PTX_PTX_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int ptx_debug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    STRING = 258,
    OPCODE = 259,
    ALIGN_DIRECTIVE = 260,
    BRANCHTARGETS_DIRECTIVE = 261,
    BYTE_DIRECTIVE = 262,
    CALLPROTOTYPE_DIRECTIVE = 263,
    CALLTARGETS_DIRECTIVE = 264,
    CONST_DIRECTIVE = 265,
    CONSTPTR_DIRECTIVE = 266,
    PTR_DIRECTIVE = 267,
    ENTRY_DIRECTIVE = 268,
    EXTERN_DIRECTIVE = 269,
    FILE_DIRECTIVE = 270,
    FUNC_DIRECTIVE = 271,
    GLOBAL_DIRECTIVE = 272,
    LOCAL_DIRECTIVE = 273,
    LOC_DIRECTIVE = 274,
    MAXNCTAPERSM_DIRECTIVE = 275,
    MAXNNREG_DIRECTIVE = 276,
    MAXNTID_DIRECTIVE = 277,
    MINNCTAPERSM_DIRECTIVE = 278,
    PARAM_DIRECTIVE = 279,
    PRAGMA_DIRECTIVE = 280,
    REG_DIRECTIVE = 281,
    REQNTID_DIRECTIVE = 282,
    SECTION_DIRECTIVE = 283,
    SHARED_DIRECTIVE = 284,
    SREG_DIRECTIVE = 285,
    STRUCT_DIRECTIVE = 286,
    SURF_DIRECTIVE = 287,
    TARGET_DIRECTIVE = 288,
    TEX_DIRECTIVE = 289,
    UNION_DIRECTIVE = 290,
    VERSION_DIRECTIVE = 291,
    ADDRESS_SIZE_DIRECTIVE = 292,
    VISIBLE_DIRECTIVE = 293,
    IDENTIFIER = 294,
    INT_OPERAND = 295,
    FLOAT_OPERAND = 296,
    DOUBLE_OPERAND = 297,
    S8_TYPE = 298,
    S16_TYPE = 299,
    S32_TYPE = 300,
    S64_TYPE = 301,
    U8_TYPE = 302,
    U16_TYPE = 303,
    U32_TYPE = 304,
    U64_TYPE = 305,
    F16_TYPE = 306,
    F32_TYPE = 307,
    F64_TYPE = 308,
    FF64_TYPE = 309,
    B8_TYPE = 310,
    B16_TYPE = 311,
    B32_TYPE = 312,
    B64_TYPE = 313,
    BB64_TYPE = 314,
    BB128_TYPE = 315,
    PRED_TYPE = 316,
    TEXREF_TYPE = 317,
    SAMPLERREF_TYPE = 318,
    SURFREF_TYPE = 319,
    V2_TYPE = 320,
    V3_TYPE = 321,
    V4_TYPE = 322,
    COMMA = 323,
    PRED = 324,
    HALF_OPTION = 325,
    EQ_OPTION = 326,
    NE_OPTION = 327,
    LT_OPTION = 328,
    LE_OPTION = 329,
    GT_OPTION = 330,
    GE_OPTION = 331,
    LO_OPTION = 332,
    LS_OPTION = 333,
    HI_OPTION = 334,
    HS_OPTION = 335,
    EQU_OPTION = 336,
    NEU_OPTION = 337,
    LTU_OPTION = 338,
    LEU_OPTION = 339,
    GTU_OPTION = 340,
    GEU_OPTION = 341,
    NUM_OPTION = 342,
    NAN_OPTION = 343,
    CF_OPTION = 344,
    SF_OPTION = 345,
    NSF_OPTION = 346,
    LEFT_SQUARE_BRACKET = 347,
    RIGHT_SQUARE_BRACKET = 348,
    WIDE_OPTION = 349,
    SPECIAL_REGISTER = 350,
    MINUS = 351,
    PLUS = 352,
    COLON = 353,
    SEMI_COLON = 354,
    EXCLAMATION = 355,
    PIPE = 356,
    RIGHT_BRACE = 357,
    LEFT_BRACE = 358,
    EQUALS = 359,
    PERIOD = 360,
    BACKSLASH = 361,
    DIMENSION_MODIFIER = 362,
    RN_OPTION = 363,
    RZ_OPTION = 364,
    RM_OPTION = 365,
    RP_OPTION = 366,
    RNI_OPTION = 367,
    RZI_OPTION = 368,
    RMI_OPTION = 369,
    RPI_OPTION = 370,
    UNI_OPTION = 371,
    GEOM_MODIFIER_1D = 372,
    GEOM_MODIFIER_2D = 373,
    GEOM_MODIFIER_3D = 374,
    SAT_OPTION = 375,
    FTZ_OPTION = 376,
    NEG_OPTION = 377,
    ATOMIC_AND = 378,
    ATOMIC_OR = 379,
    ATOMIC_XOR = 380,
    ATOMIC_CAS = 381,
    ATOMIC_EXCH = 382,
    ATOMIC_ADD = 383,
    ATOMIC_INC = 384,
    ATOMIC_DEC = 385,
    ATOMIC_MIN = 386,
    ATOMIC_MAX = 387,
    LEFT_ANGLE_BRACKET = 388,
    RIGHT_ANGLE_BRACKET = 389,
    LEFT_PAREN = 390,
    RIGHT_PAREN = 391,
    APPROX_OPTION = 392,
    FULL_OPTION = 393,
    ANY_OPTION = 394,
    ALL_OPTION = 395,
    BALLOT_OPTION = 396,
    GLOBAL_OPTION = 397,
    CTA_OPTION = 398,
    SYS_OPTION = 399,
    EXIT_OPTION = 400,
    ABS_OPTION = 401,
    TO_OPTION = 402,
    CA_OPTION = 403,
    CG_OPTION = 404,
    CS_OPTION = 405,
    LU_OPTION = 406,
    CV_OPTION = 407,
    WB_OPTION = 408,
    WT_OPTION = 409
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE YYSTYPE;
union YYSTYPE
{
#line 30 "../src/cuda-sim/ptx.y" /* yacc.c:355  */

  double double_value;
  float  float_value;
  int    int_value;
  char * string_value;
  void * ptr_value;

#line 278 "ptx.tab.c" /* yacc.c:355  */
};
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE ptx_lval;

int ptx_parse (void);

#endif /* !YY_PTX_PTX_TAB_H_INCLUDED  */

/* Copy the second part of user declarations.  */
#line 194 "../src/cuda-sim/ptx.y" /* yacc.c:358  */

  	#include "ptx_parser.h"
	#include <stdlib.h>
	#include <string.h>
	#include <math.h>
	void syntax_not_implemented();
	extern int g_func_decl;
	int ptx_lex(void);
	int ptx_error(const char *);

#line 303 "ptx.tab.c" /* yacc.c:358  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif


#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   609

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  155
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  64
/* YYNRULES -- Number of rules.  */
#define YYNRULES  269
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  376

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   409

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   207,   207,   208,   209,   210,   213,   213,   214,   214,
     214,   217,   220,   221,   224,   225,   228,   228,   228,   229,
     229,   230,   233,   233,   233,   234,   237,   238,   239,   240,
     243,   244,   245,   245,   247,   247,   248,   248,   250,   251,
     252,   254,   255,   256,   258,   260,   262,   263,   264,   265,
     266,   267,   270,   271,   272,   273,   274,   275,   276,   277,
     278,   279,   280,   281,   284,   285,   286,   287,   290,   292,
     293,   295,   296,   308,   309,   312,   313,   315,   316,   317,
     318,   321,   323,   324,   325,   328,   329,   330,   331,   332,
     333,   334,   337,   338,   341,   342,   343,   346,   347,   348,
     349,   350,   351,   352,   353,   354,   355,   356,   357,   358,
     359,   360,   361,   362,   363,   364,   365,   366,   367,   370,
     371,   373,   374,   376,   377,   378,   380,   380,   381,   382,
     383,   384,   387,   387,   388,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   405,   406,
     408,   409,   410,   411,   412,   413,   414,   415,   416,   417,
     418,   419,   420,   421,   422,   423,   424,   425,   426,   427,
     428,   429,   430,   431,   432,   433,   434,   435,   436,   437,
     438,   439,   442,   443,   444,   445,   446,   447,   448,   449,
     450,   451,   454,   455,   457,   458,   459,   460,   463,   464,
     465,   466,   469,   470,   471,   472,   473,   474,   475,   476,
     477,   478,   479,   480,   481,   482,   483,   484,   485,   486,
     489,   490,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   507,   508,   509,
     510,   511,   514,   515,   516,   517,   520,   520,   525,   526,
     529,   530,   531,   532,   533,   536,   537,   538,   539,   540,
     541,   542,   545,   546,   547,   550,   551,   552,   553,   554
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "STRING", "OPCODE", "ALIGN_DIRECTIVE",
  "BRANCHTARGETS_DIRECTIVE", "BYTE_DIRECTIVE", "CALLPROTOTYPE_DIRECTIVE",
  "CALLTARGETS_DIRECTIVE", "CONST_DIRECTIVE", "CONSTPTR_DIRECTIVE",
  "PTR_DIRECTIVE", "ENTRY_DIRECTIVE", "EXTERN_DIRECTIVE", "FILE_DIRECTIVE",
  "FUNC_DIRECTIVE", "GLOBAL_DIRECTIVE", "LOCAL_DIRECTIVE", "LOC_DIRECTIVE",
  "MAXNCTAPERSM_DIRECTIVE", "MAXNNREG_DIRECTIVE", "MAXNTID_DIRECTIVE",
  "MINNCTAPERSM_DIRECTIVE", "PARAM_DIRECTIVE", "PRAGMA_DIRECTIVE",
  "REG_DIRECTIVE", "REQNTID_DIRECTIVE", "SECTION_DIRECTIVE",
  "SHARED_DIRECTIVE", "SREG_DIRECTIVE", "STRUCT_DIRECTIVE",
  "SURF_DIRECTIVE", "TARGET_DIRECTIVE", "TEX_DIRECTIVE", "UNION_DIRECTIVE",
  "VERSION_DIRECTIVE", "ADDRESS_SIZE_DIRECTIVE", "VISIBLE_DIRECTIVE",
  "IDENTIFIER", "INT_OPERAND", "FLOAT_OPERAND", "DOUBLE_OPERAND",
  "S8_TYPE", "S16_TYPE", "S32_TYPE", "S64_TYPE", "U8_TYPE", "U16_TYPE",
  "U32_TYPE", "U64_TYPE", "F16_TYPE", "F32_TYPE", "F64_TYPE", "FF64_TYPE",
  "B8_TYPE", "B16_TYPE", "B32_TYPE", "B64_TYPE", "BB64_TYPE", "BB128_TYPE",
  "PRED_TYPE", "TEXREF_TYPE", "SAMPLERREF_TYPE", "SURFREF_TYPE", "V2_TYPE",
  "V3_TYPE", "V4_TYPE", "COMMA", "PRED", "HALF_OPTION", "EQ_OPTION",
  "NE_OPTION", "LT_OPTION", "LE_OPTION", "GT_OPTION", "GE_OPTION",
  "LO_OPTION", "LS_OPTION", "HI_OPTION", "HS_OPTION", "EQU_OPTION",
  "NEU_OPTION", "LTU_OPTION", "LEU_OPTION", "GTU_OPTION", "GEU_OPTION",
  "NUM_OPTION", "NAN_OPTION", "CF_OPTION", "SF_OPTION", "NSF_OPTION",
  "LEFT_SQUARE_BRACKET", "RIGHT_SQUARE_BRACKET", "WIDE_OPTION",
  "SPECIAL_REGISTER", "MINUS", "PLUS", "COLON", "SEMI_COLON",
  "EXCLAMATION", "PIPE", "RIGHT_BRACE", "LEFT_BRACE", "EQUALS", "PERIOD",
  "BACKSLASH", "DIMENSION_MODIFIER", "RN_OPTION", "RZ_OPTION", "RM_OPTION",
  "RP_OPTION", "RNI_OPTION", "RZI_OPTION", "RMI_OPTION", "RPI_OPTION",
  "UNI_OPTION", "GEOM_MODIFIER_1D", "GEOM_MODIFIER_2D", "GEOM_MODIFIER_3D",
  "SAT_OPTION", "FTZ_OPTION", "NEG_OPTION", "ATOMIC_AND", "ATOMIC_OR",
  "ATOMIC_XOR", "ATOMIC_CAS", "ATOMIC_EXCH", "ATOMIC_ADD", "ATOMIC_INC",
  "ATOMIC_DEC", "ATOMIC_MIN", "ATOMIC_MAX", "LEFT_ANGLE_BRACKET",
  "RIGHT_ANGLE_BRACKET", "LEFT_PAREN", "RIGHT_PAREN", "APPROX_OPTION",
  "FULL_OPTION", "ANY_OPTION", "ALL_OPTION", "BALLOT_OPTION",
  "GLOBAL_OPTION", "CTA_OPTION", "SYS_OPTION", "EXIT_OPTION", "ABS_OPTION",
  "TO_OPTION", "CA_OPTION", "CG_OPTION", "CS_OPTION", "LU_OPTION",
  "CV_OPTION", "WB_OPTION", "WT_OPTION", "$accept", "input",
  "function_defn", "$@1", "$@2", "$@3", "block_spec", "block_spec_list",
  "function_decl", "$@4", "$@5", "$@6", "function_ident_param", "$@7",
  "$@8", "function_decl_header", "param_list", "$@9", "param_entry",
  "$@10", "$@11", "ptr_spec", "ptr_space_spec", "ptr_align_spec",
  "statement_block", "statement_list", "directive_statement",
  "variable_declaration", "variable_spec", "identifier_list",
  "identifier_spec", "var_spec_list", "var_spec", "align_spec",
  "space_spec", "addressable_spec", "type_spec", "vector_spec",
  "scalar_type", "initializer_list", "literal_list",
  "instruction_statement", "instruction", "$@12", "opcode_spec", "$@13",
  "pred_spec", "option_list", "option", "atomic_operation_spec",
  "rounding_mode", "floating_point_rounding_mode", "integer_rounding_mode",
  "compare_spec", "operand_list", "operand", "vector_operand",
  "tex_operand", "$@14", "builtin_operand", "memory_operand",
  "twin_operand", "literal_operand", "address_expression", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409
};
# endif

#define YYPACT_NINF -272

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-272)))

#define YYTABLE_NINF -135

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -272,   457,  -272,   -23,  -272,   -33,  -272,     6,    -4,  -272,
    -272,  -272,    25,    52,  -272,   205,  -272,  -272,  -272,  -272,
     180,  -272,   183,   200,   225,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,   -10,   -31,  -272,   145,   209,   520,  -272,  -272,  -272,
    -272,  -272,   545,  -272,  -272,   186,  -272,   252,   254,  -272,
     201,   241,   222,  -272,  -272,  -272,   207,   152,  -272,   284,
    -272,    62,   260,   226,  -272,  -272,  -272,   290,  -272,   316,
    -272,   323,  -272,   288,  -272,   328,   329,   334,  -272,   152,
     -13,   228,  -272,    59,   335,   209,    -8,   312,  -272,   313,
     319,   285,   -34,   283,  -272,   213,  -272,  -272,   286,   325,
     380,  -272,   318,  -272,   207,  -272,  -272,  -272,   251,   253,
     296,  -272,   256,  -272,  -272,  -272,  -272,    -8,  -272,  -272,
     352,   354,    -3,  -272,   131,   355,  -272,  -272,  -272,  -272,
    -272,   109,   130,   289,    93,   356,   358,   337,  -272,   330,
    -272,  -272,  -272,  -272,  -272,   300,   360,  -272,   520,   520,
    -272,  -272,  -272,  -272,   299,    90,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,    -3,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,  -272,  -272,  -272,  -272,   245,   362,   364,   365,    94,
    -272,   314,  -272,   204,   138,    -2,  -272,  -272,  -272,    92,
     269,   331,  -272,   338,   396,   209,   284,   -13,  -272,    56,
    -272,  -272,   -59,  -272,   317,   320,   342,  -272,   -52,   132,
    -272,  -272,  -272,   369,  -272,  -272,  -272,   211,   324,   373,
    -272,  -272,    61,  -272,   368,   384,   150,   209,  -272,  -272,
     -49,  -272,  -272,   -16,  -272,  -272,  -272,  -272,  -272,  -272,
    -272,   327,  -272,    97,   370,  -272,   303,   337,  -272,   401,
    -272,  -272,  -272,   437,  -272,  -272,  -272,  -272,   212,   144,
     350,   405,  -272,   337,  -272,  -272,  -272,   -13,  -272,  -272,
     218,  -272,  -272,    98,   377,  -272,  -272,  -272,   407,  -272,
     315,   345,   337,  -272,   322,  -272
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       2,     0,     1,     0,    85,     0,    26,    80,     0,    27,
      86,    87,     0,     0,    88,     0,    82,    89,    83,    90,
       0,    91,     0,     0,     0,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,    94,    95,    96,
       4,     5,    21,     3,     0,     0,    68,    75,    79,    77,
      84,    78,     0,    92,    81,     0,    29,     0,     0,    63,
       0,    58,    53,    55,    28,    62,     0,     0,    16,     0,
      52,    71,    64,    69,    80,    76,    93,     0,    59,     0,
      61,     0,    54,     0,     7,     0,     0,     0,    14,     9,
       0,    25,    20,     0,     0,     0,     0,     0,    60,    56,
     132,     0,     0,     0,    51,     0,    46,    47,     0,   131,
       0,    13,     0,    12,     0,    15,    34,    36,     0,     0,
       0,    73,     0,    70,   262,   263,   264,     0,    65,    66,
       0,     0,     0,   124,   135,     0,    45,    50,    48,    49,
     123,   222,     0,   249,     0,     0,     0,     0,   130,   220,
     228,   230,   227,   225,   226,     0,     0,    10,     0,     0,
      17,    23,    74,    72,     0,     0,   121,    67,    57,   174,
     202,   203,   204,   205,   206,   207,   208,   209,   210,   211,
     212,   213,   214,   215,   216,   217,   218,   219,   155,   194,
     195,   196,   197,   198,   199,   200,   201,   154,   162,   163,
     164,   165,   166,   167,   182,   183,   184,   185,   186,   187,
     188,   189,   190,   191,   168,   169,   156,   157,   158,   159,
     160,   161,   170,   171,   173,   175,   176,   177,   178,   179,
     180,   181,   152,   150,   133,   148,   172,   153,   192,   193,
     151,   138,   140,   137,   139,   141,   142,   144,   143,   145,
     146,   147,   136,   232,   234,     0,     0,     0,     0,   265,
     269,     0,   248,   224,     0,     0,   229,   254,   223,     0,
       0,     0,   125,     0,    38,     0,     0,    30,   120,     0,
     119,   149,   265,   262,     0,     0,     0,   231,   236,   239,
     246,   266,   267,     0,   250,   233,   235,   265,     0,     0,
     245,   126,     0,   221,   220,     0,     0,     0,    37,    18,
       0,    31,   122,     0,   253,   252,   251,   237,   238,   240,
     241,     0,   268,     0,     0,   129,     0,     0,    11,     0,
      41,    42,    43,     0,    40,    35,    32,    24,   255,     0,
       0,     0,   242,     0,   128,    44,    39,     0,   256,   257,
     258,   261,   247,     0,     0,    33,   259,   260,     0,   243,
       0,     0,     0,   244,     0,   127
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -272,  -272,  -272,  -272,  -272,  -272,   349,  -272,   448,  -272,
    -272,  -272,   165,  -272,  -272,  -272,  -272,  -272,  -271,  -272,
    -272,  -272,  -272,   110,    66,  -272,    83,  -272,    65,  -272,
    -103,  -272,   399,  -272,  -272,  -114,  -112,  -272,   390,   326,
    -272,   341,   339,  -272,  -272,  -272,  -272,   216,  -272,  -272,
    -272,  -272,  -272,  -272,  -119,  -118,  -151,  -272,  -272,  -272,
    -145,  -272,  -102,   199
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,    50,    76,    77,   124,    98,    99,   113,   100,
     286,    79,   102,   129,   287,    52,   320,   357,   128,   168,
     169,   317,   343,   344,    94,   115,    53,    54,    55,    82,
      83,    56,    57,    58,    59,    60,    61,    62,    63,   138,
     175,   117,   118,   334,   119,   142,   120,   244,   245,   246,
     247,   248,   249,   250,   313,   314,   160,   161,   331,   162,
     163,   294,   164,   271
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     158,   159,   133,   276,   139,   144,    65,     4,   -19,   277,
      -8,   126,    -8,   127,    10,    11,   321,    64,   301,   346,
     302,    14,    66,   348,   332,   327,    17,   328,   242,    19,
     243,    21,   134,   135,   136,   176,    67,   308,   323,   280,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    68,   145,   179,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   194,   195,   196,   197,   365,   347,   349,    75,
     274,   198,    69,    -6,   275,   137,   134,   135,   136,   130,
     151,   134,   135,   136,    78,   199,   200,   201,   202,   203,
     204,   205,   206,   207,   208,   209,   210,   211,   212,   213,
     214,   215,   216,   217,   218,   219,   220,   221,   222,   223,
     277,   242,   273,   243,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,   236,   237,   238,   239,
     240,   241,   131,   152,   103,   339,   153,   154,   289,   114,
     309,   155,   300,   295,   156,   351,   368,   340,   341,   269,
     270,   301,    95,   302,    96,    97,   116,   307,   270,   342,
     350,   147,   318,   360,   361,   274,   263,   322,   264,   275,
     167,   303,   290,   336,   310,   104,   156,   335,   148,   352,
     369,   265,   251,   252,   253,   254,   266,   255,    70,   329,
     267,   330,   256,   257,   345,   268,   258,   110,     3,    71,
     259,   260,   261,     4,     5,    72,     6,     7,     8,     9,
      10,    11,    12,   284,   285,   364,    13,    14,    15,    16,
      73,    74,    17,    18,    80,    19,    20,    21,    81,    22,
      23,    24,   111,   374,    87,    88,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,   305,   112,   306,   292,   293,   135,   136,   301,   358,
     302,   359,   110,     3,    89,   366,   265,   367,     4,     5,
      90,     6,     7,     8,     9,    10,    11,    12,   303,    91,
      93,    13,    14,    15,    16,   146,    93,    17,    18,    92,
      19,    20,    21,   101,    22,    23,    24,   111,   105,   107,
     106,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,   108,   112,  -134,  -134,
    -134,  -134,   109,   -22,   151,   134,   135,   136,   121,   122,
     151,   134,   135,   136,   123,   132,   151,   134,   135,   136,
     140,   141,    75,   143,   110,   150,   166,   170,   171,   172,
     173,    93,   177,   178,   262,   278,   272,   279,   281,   282,
     283,   288,   297,   298,   299,   311,   315,   304,   316,   332,
     324,  -134,   333,   325,  -134,  -134,   265,   152,  -134,  -134,
     153,   154,  -134,   152,   338,   155,   153,   154,   156,   152,
     156,   155,   153,   154,   156,   326,   337,   155,   353,   354,
     156,   355,   339,   362,   363,   370,   371,   373,   125,    51,
     372,   319,    86,   356,  -134,    85,   149,     2,   375,   165,
     157,   291,     3,   174,   296,     0,   312,     4,     5,     0,
       6,     7,     8,     9,    10,    11,    12,     0,     0,     0,
      13,    14,    15,    16,     0,     0,    17,    18,     0,    19,
      20,    21,     0,    22,    23,    24,     0,     0,     0,     0,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,     3,     0,     0,     0,     0,
       4,     0,     0,     0,    84,     0,     0,    10,    11,     0,
       0,     0,     0,     0,    14,     0,    16,     0,     0,    17,
      18,     0,    19,     0,    21,     0,     0,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46
};

static const yytype_int16 yycheck[] =
{
     119,   119,   105,   154,   106,    39,    39,    10,    39,   154,
      20,    24,    22,    26,    17,    18,   287,    40,    77,    68,
      79,    24,    16,    39,    40,    77,    29,    79,   142,    32,
     142,    34,    40,    41,    42,   137,    40,    39,    97,   157,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    40,   100,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,   357,   136,   104,    99,
      92,    94,    40,   103,    96,   103,    40,    41,    42,    40,
      39,    40,    41,    42,   135,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     275,   245,    39,   245,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,    93,    92,    92,     5,    95,    96,    68,    93,
      68,   100,    68,   265,   103,    68,    68,    17,    18,    39,
      40,    77,    20,    79,    22,    23,    93,    39,    40,    29,
     331,   115,   285,    39,    40,    92,    77,   289,    79,    96,
     124,    97,   102,   312,   102,   133,   103,   136,   115,   102,
     102,    92,    71,    72,    73,    74,    97,    76,     3,    77,
     101,    79,    81,    82,   317,   106,    85,     4,     5,    39,
      89,    90,    91,    10,    11,    42,    13,    14,    15,    16,
      17,    18,    19,   168,   169,   353,    23,    24,    25,    26,
      40,    16,    29,    30,    99,    32,    33,    34,    39,    36,
      37,    38,    39,   372,    68,     3,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    77,    69,    79,    39,    40,    41,    42,    77,    77,
      79,    79,     4,     5,    40,    77,    92,    79,    10,    11,
      99,    13,    14,    15,    16,    17,    18,    19,    97,    68,
     103,    23,    24,    25,    26,   102,   103,    29,    30,    97,
      32,    33,    34,    39,    36,    37,    38,    39,    68,    39,
     104,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    40,    69,    39,    40,
      41,    42,    39,   135,    39,    40,    41,    42,    40,    40,
      39,    40,    41,    42,    40,    40,    39,    40,    41,    42,
      68,    68,    99,    98,     4,    99,    68,   136,   135,    93,
     134,   103,    40,    39,    39,    39,   107,    39,    68,    99,
      40,   102,    40,    39,    39,   136,    68,    93,    12,    40,
      93,    92,    39,    93,    95,    96,    92,    92,    99,   100,
      95,    96,   103,    92,    40,   100,    95,    96,   103,    92,
     103,   100,    95,    96,   103,    93,    68,   100,    68,   136,
     103,    40,     5,    93,    39,    68,    39,   102,    99,     1,
     135,   286,    62,   343,   135,    56,   115,     0,   136,   120,
     135,   245,     5,   137,   265,    -1,   135,    10,    11,    -1,
      13,    14,    15,    16,    17,    18,    19,    -1,    -1,    -1,
      23,    24,    25,    26,    -1,    -1,    29,    30,    -1,    32,
      33,    34,    -1,    36,    37,    38,    -1,    -1,    -1,    -1,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,     5,    -1,    -1,    -1,    -1,
      10,    -1,    -1,    -1,    14,    -1,    -1,    17,    18,    -1,
      -1,    -1,    -1,    -1,    24,    -1,    26,    -1,    -1,    29,
      30,    -1,    32,    -1,    34,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   156,     0,     5,    10,    11,    13,    14,    15,    16,
      17,    18,    19,    23,    24,    25,    26,    29,    30,    32,
      33,    34,    36,    37,    38,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
     157,   163,   170,   181,   182,   183,   186,   187,   188,   189,
     190,   191,   192,   193,    40,    39,    16,    40,    40,    40,
       3,    39,    42,    40,    16,    99,   158,   159,   135,   166,
      99,    39,   184,   185,    14,   187,   193,    68,     3,    40,
      99,    68,    97,   103,   179,    20,    22,    23,   161,   162,
     164,    39,   167,    92,   133,    68,   104,    39,    40,    39,
       4,    39,    69,   163,   179,   180,   181,   196,   197,   199,
     201,    40,    40,    40,   160,   161,    24,    26,   173,   168,
      40,    93,    40,   185,    40,    41,    42,   103,   194,   217,
      68,    68,   200,    98,    39,   100,   102,   179,   181,   196,
      99,    39,    92,    95,    96,   100,   103,   135,   209,   210,
     211,   212,   214,   215,   217,   197,    68,   179,   174,   175,
     136,   135,    93,   134,   194,   195,   217,    40,    39,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    94,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   190,   191,   202,   203,   204,   205,   206,   207,
     208,    71,    72,    73,    74,    76,    81,    82,    85,    89,
      90,    91,    39,    77,    79,    92,    97,   101,   106,    39,
      40,   218,   107,    39,    92,    96,   211,   215,    39,    39,
     210,    68,    99,    40,   183,   183,   165,   169,   102,    68,
     102,   202,    39,    40,   216,   217,   218,    40,    39,    39,
      68,    77,    79,    97,    93,    77,    79,    39,    39,    68,
     102,   136,   135,   209,   210,    68,    12,   176,   185,   167,
     171,   173,   217,    97,    93,    93,    93,    77,    79,    77,
      79,   213,    40,    39,   198,   136,   209,    68,    40,     5,
      17,    18,    29,   177,   178,   185,    68,   136,    39,   104,
     211,    68,   102,    68,   136,    40,   178,   172,    77,    79,
      39,    40,    93,    39,   210,   173,    77,    79,    68,   102,
      68,    39,   135,   102,   209,   136
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   155,   156,   156,   156,   156,   158,   157,   159,   160,
     157,   161,   161,   161,   162,   162,   164,   165,   163,   166,
     163,   163,   168,   169,   167,   167,   170,   170,   170,   170,
     171,   171,   172,   171,   174,   173,   175,   173,   176,   176,
     176,   177,   177,   177,   178,   179,   180,   180,   180,   180,
     180,   180,   181,   181,   181,   181,   181,   181,   181,   181,
     181,   181,   181,   181,   182,   182,   182,   182,   183,   184,
     184,   185,   185,   185,   185,   186,   186,   187,   187,   187,
     187,   188,   189,   189,   189,   190,   190,   190,   190,   190,
     190,   190,   191,   191,   192,   192,   192,   193,   193,   193,
     193,   193,   193,   193,   193,   193,   193,   193,   193,   193,
     193,   193,   193,   193,   193,   193,   193,   193,   193,   194,
     194,   195,   195,   196,   196,   196,   198,   197,   197,   197,
     197,   197,   200,   199,   199,   201,   201,   201,   201,   201,
     201,   201,   201,   201,   201,   201,   201,   201,   202,   202,
     203,   203,   203,   203,   203,   203,   203,   203,   203,   203,
     203,   203,   203,   203,   203,   203,   203,   203,   203,   203,
     203,   203,   203,   203,   203,   203,   203,   203,   203,   203,
     203,   203,   204,   204,   204,   204,   204,   204,   204,   204,
     204,   204,   205,   205,   206,   206,   206,   206,   207,   207,
     207,   207,   208,   208,   208,   208,   208,   208,   208,   208,
     208,   208,   208,   208,   208,   208,   208,   208,   208,   208,
     209,   209,   210,   210,   210,   210,   210,   210,   210,   210,
     210,   210,   210,   210,   210,   210,   210,   210,   210,   210,
     210,   210,   211,   211,   211,   211,   213,   212,   214,   214,
     215,   215,   215,   215,   215,   216,   216,   216,   216,   216,
     216,   216,   217,   217,   217,   218,   218,   218,   218,   218
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     2,     2,     2,     0,     3,     0,     0,
       5,     6,     2,     2,     1,     2,     0,     0,     7,     0,
       3,     1,     0,     0,     6,     1,     1,     1,     2,     2,
       0,     1,     0,     4,     0,     5,     0,     4,     0,     3,
       2,     1,     1,     1,     2,     3,     1,     1,     2,     2,
       2,     1,     2,     2,     3,     2,     4,     6,     2,     3,
       4,     3,     2,     2,     2,     4,     4,     6,     1,     1,
       3,     1,     4,     3,     4,     1,     2,     1,     1,     1,
       1,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       3,     1,     3,     2,     2,     3,     0,    11,     6,     5,
       2,     1,     0,     3,     1,     2,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     1,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     2,     2,     1,     1,     1,     1,     2,
       1,     3,     2,     3,     2,     3,     3,     4,     4,     3,
       4,     4,     5,     7,     9,     3,     0,     6,     2,     1,
       3,     4,     4,     4,     2,     3,     4,     4,     4,     5,
       5,     4,     1,     1,     1,     1,     2,     2,     3,     1
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            /* Fall through.  */
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yystacksize);

        yyss = yyss1;
        yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 6:
#line 213 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { set_symtab((yyvsp[0].ptr_value)); func_header(".skip"); }
#line 1767 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 7:
#line 213 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { end_function(); }
#line 1773 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 8:
#line 214 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { set_symtab((yyvsp[0].ptr_value)); }
#line 1779 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 9:
#line 214 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { func_header(".skip"); }
#line 1785 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 10:
#line 214 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { end_function(); }
#line 1791 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 11:
#line 217 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {func_header_info_int(".maxntid", (yyvsp[-4].int_value));
										func_header_info_int(",", (yyvsp[-2].int_value));
										func_header_info_int(",", (yyvsp[0].int_value)); }
#line 1799 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 12:
#line 220 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { func_header_info_int(".minnctapersm", (yyvsp[0].int_value)); printf("GPGPU-Sim: Warning: .minnctapersm ignored. \n"); }
#line 1805 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 13:
#line 221 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { func_header_info_int(".maxnctapersm", (yyvsp[0].int_value)); printf("GPGPU-Sim: Warning: .maxnctapersm ignored. \n"); }
#line 1811 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 16:
#line 228 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { start_function((yyvsp[-1].int_value)); func_header_info("(");}
#line 1817 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 17:
#line 228 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {func_header_info(")");}
#line 1823 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 18:
#line 228 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.ptr_value) = reset_symtab(); }
#line 1829 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 19:
#line 229 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { start_function((yyvsp[0].int_value)); }
#line 1835 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 20:
#line 229 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.ptr_value) = reset_symtab(); }
#line 1841 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 21:
#line 230 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { start_function((yyvsp[0].int_value)); add_function_name(""); g_func_decl=0; (yyval.ptr_value) = reset_symtab(); }
#line 1847 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 22:
#line 233 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_function_name((yyvsp[0].string_value)); }
#line 1853 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 23:
#line 233 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {func_header_info("(");}
#line 1859 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 24:
#line 233 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { g_func_decl=0; func_header_info(")"); }
#line 1865 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 25:
#line 234 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_function_name((yyvsp[0].string_value)); g_func_decl=0; }
#line 1871 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 26:
#line 237 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.int_value) = 1; g_func_decl=1; func_header(".entry"); }
#line 1877 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 27:
#line 238 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.int_value) = 0; g_func_decl=1; func_header(".func"); }
#line 1883 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 28:
#line 239 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.int_value) = 0; g_func_decl=1; func_header(".func"); }
#line 1889 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 29:
#line 240 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { (yyval.int_value) = 2; g_func_decl=1; func_header(".func"); }
#line 1895 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 31:
#line 244 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_directive(); }
#line 1901 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 32:
#line 245 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {func_header_info(",");}
#line 1907 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 33:
#line 245 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_directive(); }
#line 1913 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 34:
#line 247 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_space_spec(param_space_unclassified,0); }
#line 1919 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 35:
#line 247 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_function_arg(); }
#line 1925 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 36:
#line 248 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_space_spec(reg_space,0); }
#line 1931 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 37:
#line 248 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_function_arg(); }
#line 1937 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 41:
#line 254 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_ptr_spec(global_space); }
#line 1943 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 42:
#line 255 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_ptr_spec(local_space); }
#line 1949 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 43:
#line 256 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_ptr_spec(shared_space); }
#line 1955 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 46:
#line 262 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_directive(); }
#line 1961 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 47:
#line 263 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_instruction(); }
#line 1967 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 48:
#line 264 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_directive(); }
#line 1973 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 49:
#line 265 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_instruction(); }
#line 1979 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 53:
#line 271 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_version_info((yyvsp[0].double_value), 0); }
#line 1985 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 54:
#line 272 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_version_info((yyvsp[-1].double_value),1); }
#line 1991 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 55:
#line 273 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {/*Do nothing*/}
#line 1997 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 56:
#line 274 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { target_header2((yyvsp[-2].string_value),(yyvsp[0].string_value)); }
#line 2003 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 57:
#line 275 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { target_header3((yyvsp[-4].string_value),(yyvsp[-2].string_value),(yyvsp[0].string_value)); }
#line 2009 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 58:
#line 276 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { target_header((yyvsp[0].string_value)); }
#line 2015 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 59:
#line 277 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_file((yyvsp[-1].int_value),(yyvsp[0].string_value)); }
#line 2021 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 61:
#line 279 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pragma((yyvsp[-1].string_value)); }
#line 2027 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 62:
#line 280 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {/*Do nothing*/}
#line 2033 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 63:
#line 281 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { printf("GPGPU-Sim: Warning: .minnctapersm directive ignored."); }
#line 2039 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 64:
#line 284 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_variables(); }
#line 2045 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 65:
#line 285 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_variables(); }
#line 2051 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 66:
#line 286 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_variables(); }
#line 2057 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 67:
#line 287 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_constptr((yyvsp[-4].string_value), (yyvsp[-2].string_value), (yyvsp[0].int_value)); }
#line 2063 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 68:
#line 290 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { set_variable_type(); }
#line 2069 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 71:
#line 295 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_identifier((yyvsp[0].string_value),0,NON_ARRAY_IDENTIFIER); func_header_info((yyvsp[0].string_value));}
#line 2075 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 72:
#line 296 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { func_header_info((yyvsp[-3].string_value)); func_header_info_int("<", (yyvsp[-1].int_value)); func_header_info(">");
		int i,lbase,l;
		char *id = NULL;
		lbase = strlen((yyvsp[-3].string_value));
		for( i=0; i < (yyvsp[-1].int_value); i++ ) { 
			l = lbase + (int)log10(i+1)+10;
			id = (char*) malloc(l);
			snprintf(id,l,"%s%u",(yyvsp[-3].string_value),i);
			add_identifier(id,0,NON_ARRAY_IDENTIFIER); 
		}
		free((yyvsp[-3].string_value));
	}
#line 2092 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 73:
#line 308 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_identifier((yyvsp[-2].string_value),0,ARRAY_IDENTIFIER_NO_DIM); func_header_info((yyvsp[-2].string_value)); func_header_info("["); func_header_info("]");}
#line 2098 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 74:
#line 309 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_identifier((yyvsp[-3].string_value),(yyvsp[-1].int_value),ARRAY_IDENTIFIER); func_header_info((yyvsp[-3].string_value)); func_header_info_int("[",(yyvsp[-1].int_value)); func_header_info("]");}
#line 2104 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 80:
#line 318 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_extern_spec(); }
#line 2110 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 81:
#line 321 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_alignment_spec((yyvsp[0].int_value)); }
#line 2116 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 82:
#line 323 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(reg_space,0); }
#line 2122 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 83:
#line 324 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(reg_space,0); }
#line 2128 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 85:
#line 328 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(const_space,(yyvsp[0].int_value)); }
#line 2134 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 86:
#line 329 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(global_space,0); }
#line 2140 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 87:
#line 330 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(local_space,0); }
#line 2146 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 88:
#line 331 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(param_space_unclassified,0); }
#line 2152 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 89:
#line 332 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(shared_space,0); }
#line 2158 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 90:
#line 333 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(surf_space,0); }
#line 2164 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 91:
#line 334 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_space_spec(tex_space,0); }
#line 2170 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 94:
#line 341 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_option(V2_TYPE); func_header_info(".v2");}
#line 2176 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 95:
#line 342 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_option(V3_TYPE); func_header_info(".v3");}
#line 2182 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 96:
#line 343 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    {  add_option(V4_TYPE); func_header_info(".v4");}
#line 2188 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 97:
#line 346 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( S8_TYPE ); }
#line 2194 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 98:
#line 347 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( S16_TYPE ); }
#line 2200 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 99:
#line 348 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( S32_TYPE ); }
#line 2206 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 100:
#line 349 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( S64_TYPE ); }
#line 2212 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 101:
#line 350 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( U8_TYPE ); }
#line 2218 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 102:
#line 351 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( U16_TYPE ); }
#line 2224 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 103:
#line 352 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( U32_TYPE ); }
#line 2230 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 104:
#line 353 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( U64_TYPE ); }
#line 2236 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 105:
#line 354 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( F16_TYPE ); }
#line 2242 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 106:
#line 355 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( F32_TYPE ); }
#line 2248 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 107:
#line 356 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( F64_TYPE ); }
#line 2254 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 108:
#line 357 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( FF64_TYPE ); }
#line 2260 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 109:
#line 358 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( B8_TYPE );  }
#line 2266 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 110:
#line 359 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( B16_TYPE ); }
#line 2272 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 111:
#line 360 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( B32_TYPE ); }
#line 2278 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 112:
#line 361 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( B64_TYPE ); }
#line 2284 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 113:
#line 362 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( BB64_TYPE ); }
#line 2290 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 114:
#line 363 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( BB128_TYPE ); }
#line 2296 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 115:
#line 364 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( PRED_TYPE ); }
#line 2302 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 116:
#line 365 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( TEXREF_TYPE ); }
#line 2308 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 117:
#line 366 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( SAMPLERREF_TYPE ); }
#line 2314 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 118:
#line 367 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_type_spec( SURFREF_TYPE ); }
#line 2320 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 119:
#line 370 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_array_initializer(); }
#line 2326 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 120:
#line 371 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { syntax_not_implemented(); }
#line 2332 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 124:
#line 377 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_label((yyvsp[-1].string_value)); }
#line 2338 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 126:
#line 380 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { set_return(); }
#line 2344 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 132:
#line 387 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_opcode((yyvsp[0].int_value)); }
#line 2350 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 134:
#line 388 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_opcode((yyvsp[0].int_value)); }
#line 2356 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 135:
#line 390 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[0].string_value),0, -1); }
#line 2362 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 136:
#line 391 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[0].string_value),1, -1); }
#line 2368 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 137:
#line 392 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,1); }
#line 2374 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 138:
#line 393 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,2); }
#line 2380 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 139:
#line 394 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,3); }
#line 2386 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 140:
#line 395 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,5); }
#line 2392 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 141:
#line 396 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,6); }
#line 2398 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 142:
#line 397 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,10); }
#line 2404 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 143:
#line 398 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,12); }
#line 2410 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 144:
#line 399 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,13); }
#line 2416 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 145:
#line 400 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,17); }
#line 2422 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 146:
#line 401 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,19); }
#line 2428 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 147:
#line 402 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_pred((yyvsp[-1].string_value),0,28); }
#line 2434 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 154:
#line 412 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(UNI_OPTION); }
#line 2440 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 155:
#line 413 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(WIDE_OPTION); }
#line 2446 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 156:
#line 414 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ANY_OPTION); }
#line 2452 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 157:
#line 415 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ALL_OPTION); }
#line 2458 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 158:
#line 416 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(BALLOT_OPTION); }
#line 2464 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 159:
#line 417 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GLOBAL_OPTION); }
#line 2470 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 160:
#line 418 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(CTA_OPTION); }
#line 2476 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 161:
#line 419 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(SYS_OPTION); }
#line 2482 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 162:
#line 420 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GEOM_MODIFIER_1D); }
#line 2488 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 163:
#line 421 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GEOM_MODIFIER_2D); }
#line 2494 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 164:
#line 422 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GEOM_MODIFIER_3D); }
#line 2500 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 165:
#line 423 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(SAT_OPTION); }
#line 2506 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 166:
#line 424 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(FTZ_OPTION); }
#line 2512 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 167:
#line 425 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(NEG_OPTION); }
#line 2518 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 168:
#line 426 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(APPROX_OPTION); }
#line 2524 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 169:
#line 427 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(FULL_OPTION); }
#line 2530 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 170:
#line 428 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(EXIT_OPTION); }
#line 2536 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 171:
#line 429 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ABS_OPTION); }
#line 2542 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 173:
#line 431 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(TO_OPTION); }
#line 2548 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 174:
#line 432 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(HALF_OPTION); }
#line 2554 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 175:
#line 433 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(CA_OPTION); }
#line 2560 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 176:
#line 434 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(CG_OPTION); }
#line 2566 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 177:
#line 435 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(CS_OPTION); }
#line 2572 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 178:
#line 436 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LU_OPTION); }
#line 2578 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 179:
#line 437 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(CV_OPTION); }
#line 2584 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 180:
#line 438 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(WB_OPTION); }
#line 2590 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 181:
#line 439 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(WT_OPTION); }
#line 2596 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 182:
#line 442 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_AND); }
#line 2602 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 183:
#line 443 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_OR); }
#line 2608 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 184:
#line 444 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_XOR); }
#line 2614 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 185:
#line 445 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_CAS); }
#line 2620 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 186:
#line 446 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_EXCH); }
#line 2626 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 187:
#line 447 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_ADD); }
#line 2632 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 188:
#line 448 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_INC); }
#line 2638 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 189:
#line 449 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_DEC); }
#line 2644 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 190:
#line 450 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_MIN); }
#line 2650 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 191:
#line 451 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(ATOMIC_MAX); }
#line 2656 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 194:
#line 457 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RN_OPTION); }
#line 2662 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 195:
#line 458 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RZ_OPTION); }
#line 2668 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 196:
#line 459 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RM_OPTION); }
#line 2674 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 197:
#line 460 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RP_OPTION); }
#line 2680 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 198:
#line 463 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RNI_OPTION); }
#line 2686 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 199:
#line 464 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RZI_OPTION); }
#line 2692 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 200:
#line 465 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RMI_OPTION); }
#line 2698 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 201:
#line 466 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(RPI_OPTION); }
#line 2704 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 202:
#line 469 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(EQ_OPTION); }
#line 2710 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 203:
#line 470 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(NE_OPTION); }
#line 2716 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 204:
#line 471 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LT_OPTION); }
#line 2722 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 205:
#line 472 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LE_OPTION); }
#line 2728 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 206:
#line 473 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GT_OPTION); }
#line 2734 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 207:
#line 474 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GE_OPTION); }
#line 2740 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 208:
#line 475 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LO_OPTION); }
#line 2746 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 209:
#line 476 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LS_OPTION); }
#line 2752 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 210:
#line 477 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(HI_OPTION); }
#line 2758 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 211:
#line 478 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(HS_OPTION); }
#line 2764 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 212:
#line 479 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(EQU_OPTION); }
#line 2770 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 213:
#line 480 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(NEU_OPTION); }
#line 2776 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 214:
#line 481 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LTU_OPTION); }
#line 2782 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 215:
#line 482 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(LEU_OPTION); }
#line 2788 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 216:
#line 483 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GTU_OPTION); }
#line 2794 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 217:
#line 484 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(GEU_OPTION); }
#line 2800 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 218:
#line 485 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(NUM_OPTION); }
#line 2806 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 219:
#line 486 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_option(NAN_OPTION); }
#line 2812 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 222:
#line 492 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[0].string_value) ); }
#line 2818 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 223:
#line 493 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_neg_pred_operand( (yyvsp[0].string_value) ); }
#line 2824 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 224:
#line 494 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[0].string_value) ); change_operand_neg(); }
#line 2830 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 229:
#line 499 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { change_operand_neg(); }
#line 2836 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 231:
#line 501 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[-2].string_value),(yyvsp[0].int_value)); }
#line 2842 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 232:
#line 502 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[-1].string_value) ); change_operand_lohi(1);}
#line 2848 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 233:
#line 503 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[-1].string_value) ); change_operand_lohi(1); change_operand_neg();}
#line 2854 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 234:
#line 504 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[-1].string_value) ); change_operand_lohi(2);}
#line 2860 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 235:
#line 505 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand( (yyvsp[-1].string_value) ); change_operand_lohi(2); change_operand_neg();}
#line 2866 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 236:
#line 506 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-2].string_value),(yyvsp[0].string_value)); change_double_operand_type(-1);}
#line 2872 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 237:
#line 507 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(-1); change_operand_lohi(1);}
#line 2878 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 238:
#line 508 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(-1); change_operand_lohi(2);}
#line 2884 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 239:
#line 509 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-2].string_value),(yyvsp[0].string_value)); change_double_operand_type(-3);}
#line 2890 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 240:
#line 510 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(-3); change_operand_lohi(1);}
#line 2896 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 241:
#line 511 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(-3); change_operand_lohi(2);}
#line 2902 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 242:
#line 514 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_2vector_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); }
#line 2908 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 243:
#line 515 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_3vector_operand((yyvsp[-5].string_value),(yyvsp[-3].string_value),(yyvsp[-1].string_value)); }
#line 2914 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 244:
#line 516 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_4vector_operand((yyvsp[-7].string_value),(yyvsp[-5].string_value),(yyvsp[-3].string_value),(yyvsp[-1].string_value)); }
#line 2920 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 245:
#line 517 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_1vector_operand((yyvsp[-1].string_value)); }
#line 2926 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 246:
#line 520 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_scalar_operand((yyvsp[-1].string_value)); }
#line 2932 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 248:
#line 525 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_builtin_operand((yyvsp[-1].int_value),(yyvsp[0].int_value)); }
#line 2938 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 249:
#line 526 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_builtin_operand((yyvsp[0].int_value),-1); }
#line 2944 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 250:
#line 529 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_memory_operand(); }
#line 2950 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 251:
#line 530 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_memory_operand(); change_memory_addr_space((yyvsp[-3].string_value)); }
#line 2956 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 252:
#line 531 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { change_memory_addr_space((yyvsp[-3].string_value)); }
#line 2962 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 253:
#line 532 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { change_memory_addr_space((yyvsp[-3].string_value)); add_memory_operand();}
#line 2968 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 254:
#line 533 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { change_operand_neg(); }
#line 2974 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 255:
#line 536 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-2].string_value),(yyvsp[0].string_value)); change_double_operand_type(1); }
#line 2980 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 256:
#line 537 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(1); change_operand_lohi(1); }
#line 2986 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 257:
#line 538 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-3].string_value),(yyvsp[-1].string_value)); change_double_operand_type(1); change_operand_lohi(2); }
#line 2992 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 258:
#line 539 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-3].string_value),(yyvsp[0].string_value)); change_double_operand_type(2); }
#line 2998 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 259:
#line 540 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-4].string_value),(yyvsp[-1].string_value)); change_double_operand_type(2); change_operand_lohi(1); }
#line 3004 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 260:
#line 541 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_double_operand((yyvsp[-4].string_value),(yyvsp[-1].string_value)); change_double_operand_type(2); change_operand_lohi(2); }
#line 3010 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 261:
#line 542 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[-3].string_value),(yyvsp[0].int_value)); change_double_operand_type(3); }
#line 3016 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 262:
#line 545 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_literal_int((yyvsp[0].int_value)); }
#line 3022 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 263:
#line 546 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_literal_float((yyvsp[0].float_value)); }
#line 3028 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 264:
#line 547 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_literal_double((yyvsp[0].double_value)); }
#line 3034 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 265:
#line 550 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[0].string_value),0); }
#line 3040 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 266:
#line 551 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[-1].string_value),0); change_operand_lohi(1);}
#line 3046 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 267:
#line 552 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[-1].string_value),0); change_operand_lohi(2); }
#line 3052 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 268:
#line 553 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand((yyvsp[-2].string_value),(yyvsp[0].int_value)); }
#line 3058 "ptx.tab.c" /* yacc.c:1646  */
    break;

  case 269:
#line 554 "../src/cuda-sim/ptx.y" /* yacc.c:1646  */
    { add_address_operand2((yyvsp[0].int_value)); }
#line 3064 "ptx.tab.c" /* yacc.c:1646  */
    break;


#line 3068 "ptx.tab.c" /* yacc.c:1646  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 557 "../src/cuda-sim/ptx.y" /* yacc.c:1906  */


extern int ptx_lineno;
extern const char *g_filename;

void syntax_not_implemented()
{
	printf("Parse error (%s:%u): this syntax is not (yet) implemented:\n",g_filename,ptx_lineno);
	ptx_error(NULL);
	abort();
}
