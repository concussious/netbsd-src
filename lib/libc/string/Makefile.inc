#	from: @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
#	$NetBSD: Makefile.inc,v 1.93 2024/11/02 02:43:48 riastradh Exp $

# string sources
.PATH: ${ARCHDIR}/string ${.CURDIR}/string

# NB: if a .S version of any of these is added in the arch/*/Makefile
# then the relevant .c file is automatically removed
# Also adding the .c name to NO_SRCS will have the same effect

SRCS+=	bm.c stpcpy.c stpncpy.c \
	strcasecmp.c strncasecmp.c strcasestr.c strcoll.c strdup.c \
	strerror.c strerror_ss.c strlcat.c strlcpy.c strnlen.c \
	strmode.c strsignal.c strtok.c \
	strtok_r.c strxfrm.c __strsignal.c strerror_r.c strndup.c \
	stresep.c memrchr.c

SRCS+=	bcmp.c bcopy.c bzero.c ffs.c ffsl.c ffsll.c memchr.c memcmp.c memset.c
SRCS+=	strcat.c strcmp.c strcpy.c strcspn.c strlen.c
SRCS+=	strncat.c strncmp.c strncpy.c strpbrk.c strsep.c
SRCS+=	strspn.c strstr.c swab.c strnstr.c
SRCS+=	explicit_memset.c consttime_memequal.c

SRCS+=	memccpy.c memcpy.c memmem.c memmove.c mempcpy.c
SRCS+=	strchr.c strrchr.c strchrnul.c
SRCS+=	popcount32.c popcount64.c

# wide char
SRCS+=	wcscat.c wcschr.c wcscmp.c wcscpy.c wcscspn.c wcslcat.c wcslcpy.c \
	wcslen.c wcsncat.c wcsnlen.c wcscasecmp.c wcsdup.c wcsncasecmp.c \
	wcsncmp.c wcsncpy.c wcspbrk.c wcsrchr.c wcsspn.c wcsstr.c wcstok.c \
	wcswcs.c wmemchr.c wmemcmp.c wmemcpy.c wmemmove.c wmempcpy.c wmemset.c
CPPFLAGS.wcscmp.c+=	-I${LIBCDIR}/locale
CPPFLAGS.wcsncmp.c+=	-I${LIBCDIR}/locale
CPPFLAGS.wmemcmp.c+=	-I${LIBCDIR}/locale

# Avoid memset() compilation generating calls to memset() via code
# that appears to implement memset()-like semantics (similar to how
# printf("foo") may be converted to puts("foo")), since that tends
# to recurse and blow the stack.
COPTS.memset.c+=	${${ACTIVE_CC} == "gcc":? -fno-builtin :}

COPTS.strerror_r.c+= 	-Wno-format-nonliteral

.include "${ARCHDIR}/string/Makefile.inc"

MAN+=	bm.3 bcmp.3 bcopy.3 bstring.3 bzero.3 consttime_memequal.3 \
	explicit_memset.3 ffs.3 index.3 \
	memccpy.3 memchr.3 memcmp.3 memcpy.3 memmem.3 memmove.3	memset.3 \
	popcount.3 \
	rindex.3 strcasecmp.3 strcat.3 strchr.3 strcmp.3 strcoll.3 \
	strcpy.3 strcspn.3 strdup.3 strerror.3 string.3 strings.3 strlcpy.3 \
	strlen.3 strmode.3 strncpy.3 strpbrk.3 strrchr.3 strsep.3 \
	strsignal.3 strspn.3 strstr.3 strtok.3 strxfrm.3 \
	swab.3 wcstok.3 wcswidth.3 wmemchr.3 wcsdup.3 wcscasecmp.3

MLINKS+=bm.3 bm_comp.3 bm.3 bm_exec.3 bm.3 bm_free.3
MLINKS+=explicit_memset.3 memset_explicit.3
MLINKS+=ffs.3 ffsl.3
MLINKS+=ffs.3 ffsll.3
MLINKS+=popcount.3 popcountl.3
MLINKS+=popcount.3 popcountll.3
MLINKS+=popcount.3 popcount32.3
MLINKS+=popcount.3 popcount64.3
MLINKS+=strcasecmp.3 strncasecmp.3
MLINKS+=strcat.3 strncat.3
MLINKS+=strcmp.3 strncmp.3
MLINKS+=strcpy.3 stpcpy.3
MLINKS+=strlcpy.3 strlcat.3
MLINKS+=strlen.3 strnlen.3
MLINKS+=strncpy.3 stpncpy.3
MLINKS+=strstr.3 strcasestr.3
MLINKS+=strstr.3 strnstr.3
MLINKS+=strchr.3 strchrnul.3
MLINKS+=memchr.3 memrchr.3
MLINKS+=memcpy.3 mempcpy.3
MLINKS+=strtok.3 strtok_r.3
MLINKS+=strerror.3 strerror_r.3 strerror.3 perror.3 \
	strerror.3 strerror_l.3 \
	strerror.3 sys_errlist.3 strerror.3 sys_nerr.3
MLINKS+=strdup.3 strndup.3
MLINKS+=strsep.3 stresep.3
MLINKS+=wmemchr.3 wmemcmp.3 wmemchr.3 wmemcpy.3 \
	wmemchr.3 wmemmove.3 wmemchr.3 wmempcpy.3 \
	wmemchr.3 wmemset.3 \
	wmemchr.3 wcscat.3 wmemchr.3 wcschr.3 \
	wmemchr.3 wcscmp.3 wmemchr.3 wcscpy.3 \
	wmemchr.3 wcscspn.3 wmemchr.3 wcslcat.3 \
	wmemchr.3 wcslcpy.3 wmemchr.3 wcslen.3 \
	wmemchr.3 wcsncat.3 wmemchr.3 wcsnlen.3 \
	wmemchr.3 wcsncmp.3 wmemchr.3 wcsncpy.3 \
	wmemchr.3 wcspbrk.3 wmemchr.3 wcsrchr.3 \
	wmemchr.3 wcsspn.3 wmemchr.3 wcsstr.3 \
	wmemchr.3 wcswcs.3
MLINKS+=wcscasecmp.3 wcsncasecmp.3
