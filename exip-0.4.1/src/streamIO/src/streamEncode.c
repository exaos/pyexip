/*==================================================================*\
|                EXIP - Embeddable EXI Processor in C                |
|--------------------------------------------------------------------|
|          This work is licensed under BSD 3-Clause License          |
|  The full license terms and conditions are located in LICENSE.txt  |
\===================================================================*/

/**
 * @file streamEncode.c
 * @brief Implements an interface to a higher-level EXI stream encoder - encode basic EXI types
 *
 * @date Oct 26, 2010
 * @author Rumen Kyusakov
 * @version 0.4
 * @par[Revision] $Id: streamEncode.c 205 2012-09-04 11:19:04Z kjussakov $
 */

#include "streamEncode.h"
#include "streamWrite.h"
#include "stringManipulate.h"
#include "ioUtil.h"
#include <math.h>


errorCode encodeNBitUnsignedInteger(EXIStream* strm, unsigned char n, unsigned int int_val)
{
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, (">> %d [0x%X] (%u bits)", int_val, int_val, n));
	if(WITH_COMPRESSION(strm->header.opts.enumOpt) == FALSE && GET_ALIGNMENT(strm->header.opts.enumOpt) == BIT_PACKED)
	{
		return writeNBits(strm, n, int_val);
	}
	else
	{
		unsigned int byte_number = n / 8 + (n % 8 != 0);
		int tmp_byte_buf = 0;
		errorCode tmp_err_code = UNEXPECTED_ERROR;
		unsigned int i = 0;
		for(i = 0; i < byte_number; i++)
		{
			tmp_byte_buf = (int_val >> (i * 8)) & 0xFF;

			tmp_err_code = writeNBits(strm, 8, tmp_byte_buf);
			if(tmp_err_code != ERR_OK)
				return tmp_err_code;
		}
	}
	return ERR_OK;
}

errorCode encodeBoolean(EXIStream* strm, unsigned char bool_val)
{
	//TODO:  when pattern facets are available in the schema datatype - handle it differently
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, (">> 0x%X (bool)", bool_val));
	return writeNextBit(strm, bool_val);
}

errorCode encodeUnsignedInteger(EXIStream* strm, UnsignedInteger int_val)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned int nbits = getBitsNumber(int_val);
	unsigned int nbyte7 = nbits / 7 + (nbits % 7 != 0);
	unsigned int tmp_byte_buf = 0;
	unsigned int i = 0;
	
#if DEBUG_STREAM_IO == ON
	if (nbyte7 > 1)
		DEBUG_MSG(INFO, DEBUG_STREAM_IO, (" Write %lu (unsigned)\n", (long unsigned int)int_val));
#endif
	if(nbyte7 == 0)
		nbyte7 = 1;  // the 0 Unsigned Integer is encoded with one 7bit byte
	for(i = 0; i < nbyte7; i++)
	{
		tmp_byte_buf = (unsigned int) ((int_val >> (i * 7)) & 0x7F);
		if(i == nbyte7 - 1)
		{
			DEBUG_MSG(INFO, DEBUG_STREAM_IO, (">> 0x%.2X", tmp_byte_buf & 0x7f));
			writeNextBit(strm, 0);
		}
		else
		{
			DEBUG_MSG(INFO, DEBUG_STREAM_IO, (">> 0x%.2X", tmp_byte_buf | 0x80));
			writeNextBit(strm, 1);
		}

		tmp_err_code = writeNBits(strm, 7, tmp_byte_buf);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}
	return ERR_OK;
}

errorCode encodeString(EXIStream* strm, const String* string_val)
{
	// Assume no Restricted Character Set is defined
	//TODO: Handle the case when Restricted Character Set is defined

	errorCode tmp_err_code = UNEXPECTED_ERROR;

	DEBUG_MSG(INFO, DEBUG_STREAM_IO, (" Prepare to write string"));
	tmp_err_code = encodeUnsignedInteger(strm, (UnsignedInteger)(string_val->length) );
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return encodeStringOnly(strm, string_val);
}

errorCode encodeStringOnly(EXIStream* strm, const String* string_val)
{
	// Assume no Restricted Character Set is defined
	//TODO: Handle the case when Restricted Character Set is defined

	errorCode tmp_err_code = UNEXPECTED_ERROR;
	uint32_t tmp_val= 0;
	Index i = 0;
	Index readerPosition = 0;
#if DEBUG_STREAM_IO == ON
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, ("\n Write string, len %u: ", (unsigned int) string_val->length));
	printString(string_val);
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, ("\n"));
#endif
	
	for(i = 0; i < string_val->length; i++)
	{
		tmp_err_code = readCharFromString(string_val, &readerPosition, &tmp_val);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
		tmp_err_code = encodeUnsignedInteger(strm, (UnsignedInteger) tmp_val);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	return ERR_OK;
}

errorCode encodeBinary(EXIStream* strm, char* binary_val, Index nbytes)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	Index i = 0;

	tmp_err_code = encodeUnsignedInteger(strm, (UnsignedInteger) nbytes);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	DEBUG_MSG(INFO, DEBUG_STREAM_IO, (" Write %u (binary bytes)\n", (unsigned int) nbytes));
	for(i = 0; i < nbytes; i++)
	{
		tmp_err_code = writeNBits(strm, 8, (unsigned int) binary_val[i]);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, ("\n"));
	return ERR_OK;
}

errorCode encodeIntegerValue(EXIStream* strm, Integer sint_val)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	UnsignedInteger uval;
	unsigned char sign;
	if(sint_val >= 0)
	{
		sign = 0;
		uval = (UnsignedInteger) sint_val;
	}
	else
	{
		sint_val += 1;
		uval = (UnsignedInteger) -sint_val;
		sign = 1;
	}
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, (" Write %ld (signed)", (long int)sint_val));
	tmp_err_code = writeNextBit(strm, sign);
	DEBUG_MSG(INFO, DEBUG_STREAM_IO, ("\n"));
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;
	return encodeUnsignedInteger(strm, uval);
}

errorCode encodeDecimalValue(EXIStream* strm, Decimal dec_val)
{
	// TODO: Review this. Probably incorrect in some cases and not efficient. Depends on decimal floating point support!
	errorCode tmp_err_code = UNEXPECTED_ERROR;
	unsigned char sign = 0;
	UnsignedInteger integr_part = 0;
	UnsignedInteger fract_part_rev = 0;
	unsigned int i = 1;
	unsigned int d = 0;

	if(dec_val >= 0)
		sign = 0;
	else
	{
		dec_val = -dec_val;
		sign = 1;
	}

	tmp_err_code = encodeBoolean(strm, sign);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	integr_part = (UnsignedInteger) dec_val;

	tmp_err_code = encodeUnsignedInteger(strm, integr_part);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	dec_val = dec_val - integr_part;

	while(dec_val - ((UnsignedInteger) dec_val) != 0)
	{
		dec_val = dec_val * 10;
		d = (unsigned int) dec_val;
		fract_part_rev = fract_part_rev + d*i;
		i = i*10;
		dec_val = dec_val - (UnsignedInteger) dec_val;
	}

	tmp_err_code = encodeUnsignedInteger(strm, fract_part_rev);
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode encodeFloatValue(EXIStream* strm, Float fl_val)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;

	DEBUG_MSG(ERROR, DEBUG_STREAM_IO, (">Float value: %ldE%d\n", (long int)fl_val.mantissa, fl_val.exponent));

	tmp_err_code = encodeIntegerValue(strm, (Integer) fl_val.mantissa);	//encode mantissa
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	tmp_err_code = encodeIntegerValue(strm, (Integer) fl_val.exponent);	//encode exponent
	if(tmp_err_code != ERR_OK)
		return tmp_err_code;

	return ERR_OK;
}

errorCode encodeDateTimeValue(EXIStream* strm, EXIPDateTime dt_val)
{
	errorCode tmp_err_code = UNEXPECTED_ERROR;

	// TODO: currently only the xs:dateTime is implemented.
	//       The other types (gYear, gYearMonth, date, dateTime etc.)
	//       must be known here for correct encoding.

	if(IS_PRESENT(dt_val.presenceMask, YEAR_PRESENCE))
	{
		/* Year component */
		tmp_err_code = encodeIntegerValue(strm, (Integer) dt_val.dateTime.tm_year + 100);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	if(IS_PRESENT(dt_val.presenceMask, MON_PRESENCE) || IS_PRESENT(dt_val.presenceMask, MDAY_PRESENCE))
	{
		/* MonthDay component */
		unsigned int monDay = 0;

		if(IS_PRESENT(dt_val.presenceMask, MON_PRESENCE))
			monDay = dt_val.dateTime.tm_mon + 1;
		else
			monDay = 1;

		monDay = monDay * 32;

		if(IS_PRESENT(dt_val.presenceMask, MDAY_PRESENCE))
			monDay += dt_val.dateTime.tm_mday;
		else
			monDay += 1;

		tmp_err_code = encodeNBitUnsignedInteger(strm, 9, monDay);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	if(IS_PRESENT(dt_val.presenceMask, HOUR_PRESENCE) || IS_PRESENT(dt_val.presenceMask, MIN_PRESENCE) || IS_PRESENT(dt_val.presenceMask, SEC_PRESENCE))
	{
		/* Time component */
		unsigned int timeVal = 0;

		if(IS_PRESENT(dt_val.presenceMask, HOUR_PRESENCE))
			timeVal += dt_val.dateTime.tm_hour;

		timeVal = timeVal * 64;

		if(IS_PRESENT(dt_val.presenceMask, MIN_PRESENCE))
			timeVal += dt_val.dateTime.tm_min;

		timeVal = timeVal * 64;

		if(IS_PRESENT(dt_val.presenceMask, SEC_PRESENCE))
			timeVal += dt_val.dateTime.tm_sec;

		tmp_err_code = encodeNBitUnsignedInteger(strm, 17, timeVal);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	if(IS_PRESENT(dt_val.presenceMask, FRACT_PRESENCE))
	{
		/* FractionalSecs component */
		UnsignedInteger fSecs = 0;
		unsigned int tmp;
		unsigned int i = 1;
		unsigned int j = 0;

		tmp = dt_val.fSecs.value;

		while(tmp != 0)
		{
			fSecs = fSecs*i + (tmp % 10);
			tmp = tmp / 10;

			i = 10;
			j++;
		}

		for(i = 0; i < dt_val.fSecs.offset + 1 - j; j++)
		{
			fSecs = fSecs*10;
		}

		tmp_err_code = encodeBoolean(strm, TRUE);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;

		tmp_err_code = encodeUnsignedInteger(strm, fSecs);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}
	else
	{
		tmp_err_code = encodeBoolean(strm, FALSE);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	if(IS_PRESENT(dt_val.presenceMask, TZONE_PRESENCE))
	{
		return NOT_IMPLEMENTED_YET;
	}
	else
	{
		tmp_err_code = encodeBoolean(strm, FALSE);
		if(tmp_err_code != ERR_OK)
			return tmp_err_code;
	}

	return ERR_OK;
}
