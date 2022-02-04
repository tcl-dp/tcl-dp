#include "generic/dp.h"
#include <errno.h>
#include <string.h>

/*
 * You will find below a description of the filter functions.
 *
 * PROTOTYPE: int Filter (char *inBuf, int inLength, char **outBuf,
 *                        int *outLength, void **data, Tcl_Interp *interp, int mode)
 *
 * ARGUMENTS: We provide below the "usual" interpretation of the parameters. For
 * the treatment of special cases, please refer to the mode parameter.
 *
 * (in) inBuf: If defined, it points to the input buffer. It is assumed that the
 * filter consumes the entire input. If it is not possible to generate output
 * for all input, part of it can be buffered internally.
 *
 * (in) inLength: The length of the input buffer.
 *
 * (out) outBuf: When the filter generates output, it allocates an output
 * buffer, and stores all the available output in it. The address of the 
 * output buffer has to be stored in *outBuf. If no output is generated
 * (i.e. outLength is 0), this parameter is not used. Except when *output
 * contains the internal parameters of the filter functions (i.e. when mode 
 * is DP_FILTER_GET), the filter function should not manage this space after
 * allocating it. The responsability for doing so belongs to the plugin channel
 * code. When mode is DP_FILTER_GET the string has to be managed by the filter
 * function; this is done to avoid unnecessary reconstruction of the parameter
 * string.
 *
 * (out) outLength: The filter must store here the length of the output
 * buffer. If there is no output, *outLength has to be set to 0.
 *
 * (in) data: The filter function can store a pointer to its local
 * datastructures in *data. The plugin channel code will initializa *data with
 * NULL. The argument for infilter will be different from the argument for the
 * outfilter. The filter function code has the responsability to manage data
 * referred to by *data. In particular, the filter should free all the allocated
 * space when mode is DP_FILTER_CLOSE.
 *
 * (in) interp: Pointer to the Tcl interpreter in which the filter was
 * created. It can be used to create filters that evaluate Tcl expressions (see,
 * for example. the tclfilter function).
 *
 * (int) mode: This parameter can have four distinct values. They are discussed
 * below: 
 *
 *	DP_FILTER_NORMAL: In this mode the interpretation of parameters is the
 * one given above. The filter can either generate output or not. This mode
 * corresponds to setting the "-buffering" argument of the channel to "full" or
 * "line". 
 *
 *	DP_FILTER_FLUSH: In this mode the interpretation of parameters is the
 * one given above. The filter is encouraged to generate output, even if this
 * would not be the case. The filter can ignore this argument and behave like
 * mode was DP_FILTER_NORMAL. This mode corresponds to setting the "-buffering"
 * argument of the channel to "none".
 *
 *	DP_FILTER_CLOSE: In this mode the interpretation of parameters is the
 * one given above. This value of the parameter signals that the filter
 * channel is about to be closed, and this is the last call to the filter. The
 * filter should deallocate all its internal data structures, and should
 * generate all the output it can. NOTE: the filter should never try to
 * deallocate any *outBuf pointer that was returned to DP. These buffers are
 * managed and freed at the DP level.
 *
 *	DP_FILTER_EOF: In this mode the interpretation of parameters is the
 * one given above. This value of the parameter signals that the underlying
 * channel reached eof, no more input is expected. It is important to
 * distinguish this case when the filter function buffers data; when the
 * channel is closed the output that would be generated based on the buffered data
 * will be lost if the channel is used for input (see the uuencode code for an
 * example).
 *
 *	DP_FILTER_SET: All parameters are ignored, except for inBuf and
 * inLength. In this case the input buffer is a string that is passed "as is"
 * by TCL from the "fconfigure <channel> -[inset | outset] <string>" command.
 * The filter should use the string to set its internal variables. For example,
 * this mechanism could be used to set a different pgp key for each channel.
 *
 *	DP_FILTER_GET: All parameters are ignored, except for outBuf. The filter
 * should store in *outBuf the address of a string that describes its internal
 * state. The string should end with a null character ('\0'). If the string is
 * not static, the filter has to take care of allocating/freeing it. The string
 * is used by DP only once, immediately after the filter returns. In order for
 * the "fconfigure <channel>" command to work with no other arguments, the
 * filter should return a non-empty string even if it has no internal state (see
 * the examples below.)
 *
 *
 * RETURN VALUE: If no error is detected, the return value must be zero. A
 * non-zero return value signals an error. The user should use POSIX error codes
 * to differentiate the possible error cases.
 */

typedef struct {
  char *xorString;         /* Pointer to the encoding string. */
  int   xorStringLength;   /* The length of the encoding string. */
  int   counter;           /* Position in the encoding string of the next */
			   /* byte that will be used for encoding.        */
} xorFilterData;


/*
 *-----------------------------------------------------------------------------
 *
 * Plug1to2 --
 *
 *	Given an input string X, this is filtered by replacing each character of
 *	the string with two copies of itself. 
 *	Example: Plug1to2("abc") = "aabbcc".
 *	Remark:  Plug2to1(Plug1to2(X)) = X.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Plug1to2  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    static char internal[] = "{no internal arguments}";
    int i;
    char *iTmp, *oTmp;


    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(inLength == 0) {
            *outBuf = 0;
            return 0;
        }

        if ((*outBuf = (char *)ckalloc(2 * inLength)) == NULL) {
            return ENOMEM;
        }

        for (i = inLength, oTmp = *outBuf, iTmp = inBuf; i; i--, oTmp++, iTmp++) {
            *oTmp++ = *iTmp;
            *oTmp = *iTmp;
        }

        *outLength = 2 * inLength;

        break;


    case DP_FILTER_CLOSE:

        *outLength = 0;

        break;

    case DP_FILTER_SET:

        /* No argument can be set. */
        return EINVAL;
        
    case DP_FILTER_GET:
        
        *outBuf = internal;

        break;

    default:
        
        return EINVAL;

    }

    return 0;

}


/*
 *-----------------------------------------------------------------------------
 *
 * Plug2to1 --
 *
 *	Given an input string X, this is filtered by selecting only the
 *	characters that have even indices.
 *	Example: Plug2to1("abcdef") = "ace"
 *	Remark:  Plug2to1(Plug1to2(X)) = X.
 *
 * Results:
 *
 *      0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Plug2to1 (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    static char internal[] = "{no internal arguments}";
    int i;
    char *iTmp, *oTmp;

    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(inLength == 0) {
            *outBuf = 0;
            return 0;
        }
        if ((*outBuf = (char *)ckalloc(inLength / 2)) == NULL) {
            return ENOMEM;
        };
        
        for (i = inLength / 2, oTmp = *outBuf, iTmp = inBuf; i; i--, iTmp++) {
            *oTmp++ = *iTmp++;
        }
        
        *outLength = inLength / 2;

        break;

    case DP_FILTER_CLOSE:

        *outLength = 0;

        break;

    case DP_FILTER_SET:

        /* No argument can be set. */
        return EINVAL;

    case DP_FILTER_GET:

        *outBuf = internal;

        break;

    default:

        return EINVAL;

    } 

    return 0;
}



/*
 *-----------------------------------------------------------------------------
 *
 * Identity --
 *
 *	Identity filter: given a string X Identity(X) = X.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Identity (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    static char internal[] = "{no internal arguments}";

    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(inLength == 0) {
            *outBuf = 0;
            return 0;
        }

        if ((*outBuf = (char *)ckalloc(inLength)) == NULL) {
            return ENOMEM;
        };
        
        memcpy(*outBuf, inBuf, inLength);
        *outLength = inLength;

        break;

    case DP_FILTER_CLOSE:

        *outLength = 0;
        
        break;

    case DP_FILTER_SET:

        /* No argument can be set. */
        return EINVAL;

    case DP_FILTER_GET:

        *outBuf = internal;

        break;

    default:
        
        return EINVAL;
    }

    return 0;
}



/*
 *-----------------------------------------------------------------------------
 *
 * xor --
 *
 *	A parameter string has to be provided before first using the
 *	filter. The input bytes will be xor'd with the bytes in the parameter,
 *	taken in circular order. This order will be preserved between succesive
 *	calls. The same string must be used for both encoding and decoding.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output. Manages dynamic local
 *	datastructure (xoprFilterData).
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Xor  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{


  xorFilterData *fD;


  if(*data == NULL) {
    
      /* No filter specific data was allocated. */
      
      fD = (xorFilterData *)ckalloc(sizeof(xorFilterData));

      if(fD == NULL) {
          return ENOMEM;
      }
      
      *data = (void *)fD;
      
      fD->xorString = NULL;
      fD->xorStringLength = 0;
      fD->counter = 0;

  } else {

      fD = (xorFilterData *)(*data);

  }
  
  switch(mode) {
      
  case DP_FILTER_NORMAL:
  case DP_FILTER_FLUSH:
  case DP_FILTER_EOF:

      if(fD->xorString == NULL) {
          /* Refuse encoding if no parameter string was specified. */
          return EINVAL;
      }

      if(inLength > 0) {
        
          int   i;
          char *s, *d;
          
          *outBuf = (char *)ckalloc(inLength);
          
          if(*outBuf == NULL) {
              return ENOMEM;
          }
          
          for(i = 0, s = inBuf, d = *outBuf; i < inLength; i++, s++, d++) {
              *d = *s ^ (fD->xorString[(fD->counter)++]);
              if((fD->counter) >= (fD->xorStringLength)) {
                  fD->counter = 0;
              }
          }
      } else {
          *outBuf = NULL;
      }

      *outLength = inLength;

      break;


  case DP_FILTER_CLOSE: 
      /* Last call before the channel is closed. Since there is no buffering,
       * only the storage space for xorString has to be freed.
       */

      *outBuf = NULL;
      *outLength = 0;
      
     	if (fD->xorString != NULL) {
	    ckfree(fD->xorString);
	    fD->xorString = NULL;
	}
      ckfree((char *)fD);
      *data = NULL;

      break;

  case DP_FILTER_SET: /* Set the string that will be used to xor data. */

    if (fD->xorString != NULL) {
        return EINVAL;
    }

    fD->xorString = (char *)ckalloc(inLength);
    if (fD->xorString == NULL) {
	return ENOMEM;
    }

    memcpy(fD->xorString, inBuf, inLength);
    fD->xorStringLength = inLength;

    /* Start using the string from the beginning. */
    fD->counter = 0;
    
    break;

  case DP_FILTER_GET: /* Return the string that is used to xor data. */

    if (fD->xorString == NULL) {
	*outBuf = "{xor string not set}";
    } else {
	*outBuf = fD->xorString;
    }
    break;

  default:
    return EINVAL;
  }

  return 0;

}



/*
 *-----------------------------------------------------------------------------
 *
 * packon --
 *
 *	Adds a header of 6 bytes to each packet that is written to or read from
 *      the underlying channel. The header contains a right-aligned nonnegative
 *	integer in base 10; it is padded to the left with non-significant
 *	zeroes. A packet is the amount of data that is written to (read
 *      from) the underlying channel in one step. If there is more data than it
 *	can fit in one of tcl's buffers (usually 4k), then one write (read) will
 *      be translated into several packets. Though the filter will work both for
 *	input and output, it is more likely that the filter will be used for 
 *	automatically attaching the packet length to outgoing (written)
 *	messages. No internal parameters.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
PackOn  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(inLength > 1000000) {
            return EINVAL;
        }

        *outBuf = (char *)ckalloc(inLength + 6);

        if(*outBuf == NULL) {
            return EINVAL;
        }

        sprintf(*outBuf, "%.6d", inLength);
        memcpy((*outBuf) + 6, inBuf, inLength);
        *outLength = inLength + 6;

        break;

    case DP_FILTER_CLOSE:
        /* Last call before the channel is closed. Since there is no
         * buffering, only the storage space for xorString has to be freed.
         */

        *outBuf = NULL;
        *outLength = 0;
   
        break;

    case DP_FILTER_SET: /* No setup is allowed for this filter. */

        return EINVAL;

    case DP_FILTER_GET: /* No filter option can be set here. */

        *outBuf = "{no internal arguments}";
      
        break;

    default:
        return EINVAL;
    }

    return 0;

}


/* This value of UU_LINE was chosen for compatibility with Unix. If you change it,
 * you should keep in mind that if _must_ be divisible by zero. Functions
 * uuencode and uudecode are unix compatible (i.e. you can uuencode or uudecode
 * either with these functions either with the corresponding Unix utilities, and
 * obtain the same result.
 */

#define UU_LINE 45


/* Prototypes of functions that do the actual uuencoding and decoding. */

static void uuencode _ANSI_ARGS_((unsigned char *from, unsigned char *to,
                                  int howMany));
static void uudecode _ANSI_ARGS_((unsigned char *from, unsigned char *to,
                                  int howMany));


/*
 *-----------------------------------------------------------------------------
 *
 * uuencode --
 *
 *	Same functionality as Unix uuencode (see "man 5 uuencode" for
 *      details). No internal parameters. 
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output. Manages dynamic local
 *      datastructure (encodeData).
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Uuencode  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    /* "740" represents the acces rights a file that was encoded with the 
     * dp uuencode function and decoded with the unix uudecode function will
     * have. The corresponding file name will be be uufilter.
     * Maximum care should be taken when modifying the first constant below; its
     * structure is used to recognize the start of the data part of an uuencoded
     * file (for details please refer to the corresponding comment in the code for
     * uudecode). The second constant can be modified freely; this form is added to
     * preserve Unix compatibility.
     */


    char *header  = "begin 740 uufilter\n";
    char *trailer = " \nend\n";
    
    typedef struct {
        char buffer [UU_LINE]; /* Stores incomplete lines between calls. */
        int  used;             /* Number of bytes used in the buffer. */
        int  first;            /* If 1, the header must be output first. */
        int  trailerOutput;    /* Used to avoid double output of trailer. */
        int  firstEOFCall;     /* Identifies first call with mode == DP_FILTER_EOF */
    } encodeData;

    int   headerLength, trailerLength, spaceNeeded;
    char *putHere;

    encodeData *eD;


    if(*data == NULL) {
    
        /* No filter specific data was allocated. */
    
        eD = (encodeData *)ckalloc(sizeof(encodeData));

        if(eD == NULL) {
            return ENOMEM;
        }
    
        *data = (void *)eD;
        eD->used = 0;
        eD->first = 1;
        eD->trailerOutput = 0;
        eD->firstEOFCall  = 1;
    
    } else {

        eD = (encodeData *)(*data);

    }

    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(mode == DP_FILTER_EOF) {
            if((!(eD->firstEOFCall)) || (inLength == 0)) {
                eD->firstEOFCall = 0;
                goto jump_eof;
            }
            eD->firstEOFCall = 0;
        }

        if(inLength == 0) {
            *outLength = 0;
            return 0;
        }

        spaceNeeded = ((4 * UU_LINE) / 3 + 2) * ((eD->used + inLength) / UU_LINE);

        if(eD->first) {

            /* If this is the first call, insert the header first. */

            headerLength = strlen(header);

            *outBuf = putHere = (char *)ckalloc(spaceNeeded + headerLength);

            if(*outBuf == NULL) {
                return EINVAL;
            }

            *outLength = spaceNeeded + headerLength;

            eD->first = 0;

            memcpy(putHere, header, headerLength);

            putHere += headerLength;

        } else {

            *outBuf = putHere = (char *)ckalloc(spaceNeeded);
            
            if(*outBuf == NULL) {
                return EINVAL;
            }

            *outLength = spaceNeeded;

        }

        /* eD->used is not zero when we first get here. */

        /* Until we have at least one full line to encode, we go
         * ahead. Incomplete lines are not encoded at this point; either they
         * will be completed later, or they will encoded and output when the
         *channel is closed.
         */

        while(eD->used + inLength >= UU_LINE) {

            memcpy(eD->buffer + eD->used, inBuf, UU_LINE - eD->used);
            uuencode(eD->buffer, putHere, UU_LINE);
            putHere += 2 + (4 * UU_LINE) / 3;
            inBuf += UU_LINE - eD->used;
            inLength -= UU_LINE -eD->used;
            eD->used = 0;

        }

        memcpy(eD->buffer, inBuf, inLength);
        eD->used = inLength;

        break;

    case DP_FILTER_CLOSE:

        /* If there is an incomplete line whose encoding was postponed, 
         * output it. Then output the trailer. 
         */

        if(eD->first) {

            /* The filtered was called only to close it; there was no other
             * activity. 
             */

            *outBuf = NULL;
            *outLength = 0;
            break;

        }

        /* Continue with the case for DP_FILTER_EOF below! */

        
    jump_eof:
        if(!(eD->trailerOutput)) {
            trailerLength = strlen(trailer);

            if(eD->used == 0) {

                *outBuf = putHere = (char *)ckalloc(trailerLength);
                
                if(*outBuf == NULL) {
                    return ENOMEM;
                }

                *outLength = trailerLength;
                
            } else {
                
                /* Establish how many bytes will be needed to encode the incomplete
                 * line we are trying to output.
                 */
                
                int needed = 2 + (4 * (((eD->used + 2) / 3) * 3)) / 3;
                
                *outBuf = putHere = (char *)ckalloc(needed + trailerLength);
                
                if(*outBuf == NULL) {
                    return ENOMEM;
                }
                
                uuencode(eD->buffer, putHere, eD->used);
                putHere += needed;
                *outLength = trailerLength + needed;
            }
            
            memcpy(putHere, trailer, trailerLength);

            eD->trailerOutput = 1;
        } else {
            if(mode == DP_FILTER_EOF) {
                *outLength = 0;
            }
        }

        if(mode == DP_FILTER_CLOSE) {
            ckfree((void *)eD);
            *data = NULL;
        }

        break;
        
    case DP_FILTER_SET:

        /* No option is accepted. */
        
        return EINVAL;
      

    case DP_FILTER_GET: 
      
        *outBuf = "{no internal arguments}";
      
        break;

    default:

        return EINVAL;
    }

    return 0;

}


/* This function implements the actual uuencode algorithm. For details
 * see "man 5 uuencode" .
 */

static void
uuencode (from, to, howMany)
    unsigned char *from;     /* (in) Where to take the data from. */
    unsigned char *to;       /* (in) Where to put the encoded data to. */
    int            howMany;  /* (in) Number of bytes to encode. */
{
    int i;

    *to++ = howMany + ' ';

    for(i = 0; i < howMany; i += 3) {
        *to++ = (from[i] >> 2) + ' ';
        *to++ = (((from[i] & 0x03) << 4) | (from[i+1] >> 4)) + ' '; 
        *to++ = (((from[i+1] & 0x0f) << 2) | (from[i+2] >> 6)) + ' ';
        *to++ = (from[i+2] & 0x3f) + ' ';
    }

    *to = '\n';

    return;
}



/*
 *-----------------------------------------------------------------------------
 *
 * uudecode --
 *
 *	Same basic functionality as Unix uudecode (see "man 5 uuencode" for
 *      details). No internal parameters.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output. Manages dynamic local
 *      datastructure (decodeData).
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
Uudecode  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{

#define my_min(x, y)       (((x) < (y)) ? (x) : (y))
#define isoctal(x)         (((x) >= '0') && ((x) <= '7'))


    typedef struct {
        char buffer [(4 * UU_LINE) / 3 + 2]; /* Stores incomplete lines between
                                                calls. */ 
        int  used;           /* Number of bytes used in the buffer. */
        int  headerSeen;     /* The standard uuencode header was identified. */
        int  endSeen;        /* A uuencoded line with lenght zero was seen. */
        int  matchOK;        /* The "begin XXX " part of the header was matched. */
        int  justWaitForEOL; /* Self explanatory. */
    }
        decodeData;

    int maxSpaceNeeded;

    decodeData *dD;


    if(*data == NULL) {
    
        /* No filter specific data was allocated. */
    
        dD = (decodeData *)ckalloc(sizeof(decodeData));

        if(dD == NULL) {
            return ENOMEM;
        }
    
        *data = (void *)dD;
        dD->used = 0;
        dD->headerSeen = 0;
        dD->endSeen = 0;
        dD->matchOK = 0;
        dD->justWaitForEOL = 0;
    
    } else {

        dD = (decodeData *)(*data);

    }

    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:
        
        /* Here we identify the standard header of a uuencoded data stream. */

        while(!(dD->headerSeen)) {

            if(dD->justWaitForEOL) {

                /* Either we already identified the "begin XXX " part header, 
                 * either we found a line that does not contain the header for
                 * sure. In both cases consume input until '\n' is found.
                 */

                for(/* empty */; inLength > 0; inBuf++, inLength--) {
                    if(*inBuf == '\n') {

                        inBuf++;
                        inLength--;

                        dD->used = 0;
                        dD->justWaitForEOL = 0;

                        if(dD->matchOK) {
                            /* Header found, the next line contains data. */
                            dD->headerSeen = 1;
                        }

                        break;
                    }
                }

                /* No more input, but no output yet. */
                if(inLength == 0) {

                    *outBuf = NULL;
                    *outLength = 0;

                    return 0;
                }

            } else {

                /* 10 below is strlen("begin ddd "), d is an octal digit. This
                 * is related to the structure of the header defined in the
                 * uuencode function above. Its structure reflects the Unix
                 * standard.
                 */

                int stillNeeded = 10 - dD->used;
                int canGet      = my_min(stillNeeded, inLength);
                
                memcpy(dD->buffer + dD->used, inBuf, canGet);
                
                dD-> used += canGet;
                inBuf     += canGet;
                inLength  -= canGet;
                
                if(stillNeeded == canGet) {

                    /* Try to match expected header pattern: "begin XXX " */

                    if(!strncmp(dD->buffer, "begin ", 6) &&
                       isoctal((dD->buffer)[6]) &&
                       isoctal((dD->buffer)[7]) &&
                       isoctal((dD->buffer)[8]) &&
                       ((dD->buffer)[9] == ' ')) {
                        
                        /* We found the header. */
                        dD->matchOK = 1;
                        dD->justWaitForEOL = 1;

                    } else {
                        
                        /* Bad news; this is not the header. Reset buffer,
                         * wait for next '\n' and test again.
                         */
                        dD->used = 0;
                        dD->justWaitForEOL = 1;
                        
                    }

                } 
            }
        }

          
        /* No output will be generated after the end marker was found. */
        if(dD->endSeen) {

            *outBuf = NULL;
            *outLength = 0;

            break;
        }

        /* Alloc all the space that might be needed in conversion: round up to
         * the next multiple of 4 for the input, and see how much output could
         * that produce.
         */

        maxSpaceNeeded = ((dD->used + inLength + 3) / 4) * 3;
        *outBuf = (char *)ckalloc(maxSpaceNeeded);

        if(*outBuf == NULL) {
            return ENOMEM;
        }

        *outLength = 0;

        while(inLength > 0) {

            int i, eolSeen, lineLength;

            /* This gives the maximum number of characters that could possibly
             * exist in a correctly uuencoded line.
             */
 
            int maxLookAhead = (4 * UU_LINE) / 3 + 2 - dD->used;
        
            for(i = 0, eolSeen = 0; i < my_min(maxLookAhead, inLength); i++) {
                if(inBuf[i] == '\n') {
                    eolSeen = 1;
                    break;
                }
            }
            
            if(eolSeen == 1) {
                
                /* Transfer the rest of the current line. */
                memcpy(dD->buffer + dD->used, inBuf, i + 1);

                dD->used += i + 1;
                inLength -= i + 1;
                inBuf    += i + 1;
                
                lineLength = (dD->buffer)[0] - ' ';
                
                /* Check the length of the line. */
                if(((lineLength + 2) / 3) * 4 != dD->used - 2) {
                    /* The length of the string is specified incorrectly. */

                    /* Reset filter data, so that recovery is possible. */
                    dD->used = 0;

                    return EINVAL;
                }
                
                /* Is this a logically empty line? */
                
                if(lineLength == 0) {
                
                    /* Yes. This is the ending line of the uuencode file. */
                    dD->endSeen = 1;

                    if(*outLength == 0) {
                        ckfree(*outBuf);
                    }

                    break;
                }
                
                /* No. This is a genuine data line. */
                uudecode(dD->buffer + 1, *outBuf + *outLength, dD->used - 2);
                *outLength += lineLength;
                dD->used = 0;
                
            } else {
                
                if(maxLookAhead < inLength) {

                    /* The line is too long; this is an error. */

                    ckfree(*outBuf);

                    /* Reset filter data, so that recovery is possible. */
                    dD->used = 0;

                    *outBuf = 0;
                    *outLength = 0;

                    return EINVAL;

                } else {

                    /* The input is not long enough, copy it to buffer. */
                    memcpy(dD->buffer + dD->used, inBuf, inLength);
                    dD->used += inLength;
                    inLength = 0;
                    inBuf += inLength;

                }

            }

        }

        break;


    case DP_FILTER_CLOSE: 

        /* Last call before the channel is closed. The last line (which must
         * have been incomplete), if it exists in the buffer, is ignored.
         */
        
        *outBuf = NULL;
        *outLength = 0;

        ckfree((void *)dD);
        *data = NULL;

        break;
        
    case DP_FILTER_SET:

        /* No option is accepted. */
        
        return EINVAL;
      

  case DP_FILTER_GET: 
      
      *outBuf = "{no internal arguments}";
      
      break;

  default:

    return EINVAL;
  }

  return 0;

#undef my_min
#undef isoctal
                        

}


/* This function implements the actual uudecode algorithm. For details
 * see "man 5 uuencode" .
 */
static void
uudecode (from, to, howMany)
    unsigned char *from;     /* (in) Where to take the data from. */
    unsigned char *to;       /* (in) Where to put the encoded data to. */
    int            howMany;  /* (in) Number of bytes to decode. */
{
    int i;

    for(i = 0; i < howMany; i += 4) {
        *to++ = ((from[i] - ' ') << 2) | ((from[i+1] - ' ') >> 4);
        *to++ = ((from[i+1] - ' ') << 4) | ((from[i+2] - ' ') >> 2);
        *to++ = ((from[i+2] - ' ') << 6) | (from[i+3] - ' ');
    }

    return;
}




/*
 *-----------------------------------------------------------------------------
 *
 * TclFilter --
 *
 *	Allows the user to use any tcl function to implement a filter. The tcl filter
 *	is assumed to consume the whole input (and buffer it, if necessary). The
 *      tcl procedure must take two arguments, the first is the input string
 *	(possibly with length zero), and the second one indicates the mode. It
 *	can have the values "normal", "flush", "close", "eof". The output value is a
 *	string that will be returned as a result (the output can have length 0).
 *	Arguments: the name of the tcl command must be setup before the first use.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for the filtered output. Manages dynamic local
 *      datastructure (tclData).
 *
 *----------------------------------------------------------------------------- 
 */

int
TclFilter (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{

#define BUFFER_CHUNK  4096

typedef struct {
    char *buffer;    /* Stores the name of the Tcl commmand. */
    char *cmd;       /* Buffer for creating the Tcl comand. */
    int   cmdLength; /* Length of command buffer. */
} tclData;

    char *modeStr;
    int   temp, needed;
    tclData *tD;

    if(*data == NULL) {
    
        /* No filter specific data was allocated. */
    
        tD = (tclData *)ckalloc(sizeof(tclData));
        if(tD == NULL) {
            return ENOMEM;
        }
    
        *data = (void *)tD;

        tD->buffer    = NULL;
        tD->cmd       = NULL;
        tD->cmdLength = 0;
    
    } else {
        tD = (tclData *)(*data);
    }

    switch(mode) {

	case DP_FILTER_NORMAL:

	    modeStr = "normal";
	    break;

	case DP_FILTER_FLUSH:

	    modeStr = "flush";
	    break;

	case DP_FILTER_CLOSE:

	    modeStr = "close";
	    break;

	case DP_FILTER_EOF:

	    modeStr = "eof";
	    break;

	case DP_FILTER_SET:
	    if(tD->buffer != NULL) {
		/* We do not allow for the change of the tcl filter. */
		return EINVAL;
	    }
	    if(inLength == 0) {
		/* We do not allow for null-length procedure name. */
		return EINVAL;
	    }
	    tD->buffer = (char *)ckalloc(inLength + 1);
	    memcpy(tD->buffer, inBuf, inLength);
	    (tD->buffer)[inLength] = '\0';
	    return 0;

	case DP_FILTER_GET:
	    if(tD->buffer == NULL) {
		*outBuf = "{tcl filter name not set}";
	    } else {
		*outBuf = tD->buffer;
	    }
	    return 0;
	default:
	    return EINVAL;
    } 

    /* Refuse to do filtering if the name of the Tcl procedure was not given. */

    if(tD->buffer == NULL) {
	return EINVAL;
    }

    temp = strlen(tD->buffer);

    /* 6 = max{strlen(modeStr)}; 3 = number of spaces; 1 = space for '\0' */
    needed = temp + inLength + 3 + 6 + 1;

    /* Do we have enough place to create the command? */

    if(tD->cmdLength < needed) {

        /* No. */

        if(tD->cmd != NULL) {
            ckfree(tD->cmd);
        }

        tD->cmd = (char *)ckalloc(needed + BUFFER_CHUNK);
        
        if(tD->cmd == NULL) {
            tD->cmdLength = 0;
            return ENOMEM;
        }

        tD->cmdLength = needed + BUFFER_CHUNK;

    }
        
    /* Create the Tcl command. */

    memcpy(tD->cmd, tD->buffer, temp);
    (tD->cmd)[temp] = ' ';
    if(inLength == 0) {
        memcpy(tD->cmd + temp + 1, "\"\"", 2);
        inLength = 2;
    } else {
        memcpy(tD->cmd + temp + 1, inBuf, inLength);
    }
    (tD->cmd)[temp + 1 + inLength] = ' ';
    memcpy(tD->cmd + temp + 1 + inLength + 1, modeStr, strlen(modeStr));
    (tD->cmd)[temp + 1 + inLength + 1 + strlen(modeStr)] = '\0';

    if(Tcl_GlobalEval(interp, tD->cmd) != TCL_OK) {
        return EINVAL;
    }

    /* Reserve the space for the result. */
    needed = strlen(interp->result);

    if(needed > 0) {
        *outBuf = (char *)ckalloc(needed);

        if(*outBuf == NULL) {
            return ENOMEM;
        }
	*outLength = needed;
	memcpy(*outBuf, interp->result, needed);
    }

    if(mode == DP_FILTER_CLOSE) {
        /* This was the last call; free filter data. */
        if(tD->cmd != NULL) {
            ckfree(tD->cmd);
        }

        if(tD->buffer != NULL) {
            ckfree(tD->buffer);
        }
        ckfree((void *)tD);
    }

    return 0;

#undef BUFFER_CHUNK

}

/*
 *-----------------------------------------------------------------------------
 *
 * HexOut
 *
 *	Converts a string containg characters that correspond to hexadecimal
 *	digits, into a sequence of bytes consisting of the binary representation
 *	of the given hexadecimal symbols. Each byte will contain two hexadecimal
 *	codes; the length of the input string has to be even.
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
HexOut  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    int  i, j;
    unsigned char t1, t2;

    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

        if(inLength % 2 != 0) {
            return EINVAL;
        }

	if(inLength == 0) {
	  *outBuf = 0;
	  return 0;
	}

        *outBuf = (char *)ckalloc(inLength / 2);

        if(*outBuf == NULL) {
            return EINVAL;
        }

        *outLength = inLength / 2;

        for(i = 0, j = 0; i < inLength; i += 2, j++) {
            if(isxdigit(inBuf[i]) && isxdigit(inBuf[i+1])) {
                if(isdigit(inBuf[i])) {
                    t1 = inBuf[i] - '0';
                } else {
                    t1 = tolower(inBuf[i]) - 'a' + 10;
                }
                if(isdigit(inBuf[i + 1])) {
                    t2 = inBuf[i + 1] - '0';
                } else {
                    t2 = tolower(inBuf[i + 1]) - 'a' + 10;
                }

                (*outBuf)[j] = (t1 << 4) | t2;

            } else {
                return EINVAL;
            }
        }

        break;

    case DP_FILTER_CLOSE:
 
       *outLength = 0;
       break;

    case DP_FILTER_SET:

        return EINVAL;

    case DP_FILTER_GET:

        *outBuf = "{no internal arguments}";

    default:
        
        return EINVAL;

    }

    return 0;

}

/*
 *-----------------------------------------------------------------------------
 *
 * HexIn
 *
 *	The opposite of HexOut (see above).
 *
 * Results:
 *
 *	0 if everything is OK, a POSIX error code if there was an error.
 *
 * Side effects:
 *
 *	Allocates memory for output.
 *
 *----------------------------------------------------------------------------- 
 */
	/* ARGSUSED */ 
int
HexIn  (inBuf, inLength, outBuf, outLength, data, interp, mode)
	char  *inBuf;	   /* (in)  Data to be filtered. */ 
	int    inLength;   /* (in)  Amount of data to be filtered in bytes. */
	char **outBuf;     /* (out) Address where the filtered data is stored. */
	int   *outLength;  /* (out) Amount of filtered data. */
	void **data;       /* (in)  Place where filter function data is stored. */
	Tcl_Interp *interp;/* (in)  Interpreter in which the filter was created. */
	int    mode;       /* (in)  Distinguishes between normal, flush and */
			   /*       close modes. Ignored in this case.      */
{
    int  i, j;
    unsigned char t1, t2;

    char *TranslationTable = "0123456789abcdef";


    switch(mode) {

    case DP_FILTER_NORMAL:
    case DP_FILTER_FLUSH:
    case DP_FILTER_EOF:

	if(inLength == 0) {
	  *outBuf = 0;
	  return 0;
	}

        *outBuf = (char *)ckalloc(2 * inLength);

        if(*outBuf == NULL) {
            return EINVAL;
        }

        *outLength = 2 * inLength;

        for(i = 0, j = 0; i < inLength; i++, j += 2) {
            (*outBuf)[j]   = TranslationTable[((unsigned char)(inBuf[i])) >> 4];
            (*outBuf)[j + 1] = TranslationTable[inBuf[i] & 0x0F];
        }

        break;

    case DP_FILTER_CLOSE:
 
       *outLength = 0;
       break;

    case DP_FILTER_SET:

        return EINVAL;

    case DP_FILTER_GET:

        *outBuf = "{no internal arguments}";

    default:
        
        return EINVAL;

    }

    return 0;

}

