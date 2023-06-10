#ifndef _DATA_TYPE_H_
#define _DATA_TYPE_H_

//#include "framework.h"
#include <stdlib.h> 

#ifndef null
#define null 0
#endif
#ifndef NULL
#define NULL 0
#endif


#ifndef FALSE
#define FALSE             0
#endif
#ifndef TRUE
#define TRUE              1
#endif
#ifndef U8
typedef		unsigned char		U8;
#endif

#ifndef S8
typedef		signed char			S8;
#endif

#ifndef U16
typedef		unsigned short		U16;
#endif

#ifndef S16
typedef		signed short		S16;
#endif

#ifndef U32
typedef		unsigned int		U32;
#endif

#ifndef S32
typedef		signed int			S32;
#endif

#ifndef S64
typedef		signed long long	S64;
#endif

#ifndef U64
typedef		unsigned long long 	U64;
#endif

#ifndef FP32
typedef		float				FP32;
#endif

#ifndef FP64
typedef		double				FP64;
#endif

#ifndef Pixel_t
typedef unsigned short Pixel_t;
#endif

#ifndef UINT32
typedef unsigned int      UINT32;
#endif


#ifndef INT
typedef int               INT;
#endif

#ifndef INT32
typedef int               INT32;
#endif

typedef unsigned char  INT8U;                    /* Unsigned  8 bit quantity                           */
typedef signed   char  INT8S;                    /* Signed    8 bit quantity                           */
typedef unsigned short INT16U;                   /* Unsigned 16 bit quantity                           */
typedef signed   short INT16S;                   /* Signed   16 bit quantity                           */
typedef unsigned int   INT32U;                   /* Unsigned 32 bit quantity                           */
typedef signed   int   INT32S;                   /* Signed   32 bit quantity                           */
typedef unsigned long long   INT64U; 
typedef signed long long   INT64S; 

typedef float          FP32;                     /* Single precision floating point                    */
typedef double         FP64;                     /* Double precision floating point                    */


typedef struct
{
	U16 star;
	U16 end;
}PosLaction;
typedef struct
{
	int a0;
	int a1;
	int a2;
	int a3;
	int a4;
	int a5;
	int a6;
	int a7;
	int a8;
	int a9;
	int a10;
	int a11;
	int a12;
	int a13;
	int a14;
	int a15;
}bytes_64Bytes;
typedef struct
{
	bytes_64Bytes a0;
	bytes_64Bytes a1;
	bytes_64Bytes a2;
	bytes_64Bytes a3;	
}bytes_256Bytes;
typedef struct
{
	bytes_64Bytes a0;
	bytes_64Bytes a1;
	bytes_64Bytes a2;
	bytes_64Bytes a3;
	bytes_64Bytes a4;
	bytes_64Bytes a5;
	bytes_64Bytes a6;
	bytes_64Bytes a7;
	bytes_64Bytes a8;
	bytes_64Bytes a9;
	bytes_64Bytes a10;
	bytes_64Bytes a11;
	bytes_64Bytes a12;
	bytes_64Bytes a13;
	bytes_64Bytes a14;
	bytes_64Bytes a15;
}bytes_1024Bytes;
#endif



