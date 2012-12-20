/*==================================================================*\
|                EXIP - Embeddable EXI Processor in C                |
|--------------------------------------------------------------------|
|          This work is licensed under BSD 3-Clause License          |
|  The full license terms and conditions are located in LICENSE.txt  |
\===================================================================*/

/**
 * @file headerEncode.h
 * @brief Interface for serializing an EXI header
 *
 * @date Aug 23, 2010
 * @author Rumen Kyusakov
 * @version 0.4
 * @par[Revision] $Id: headerEncode.h 205 2012-09-04 11:19:04Z kjussakov $
 */


#ifndef HEADERENCODE_H_
#define HEADERENCODE_H_

#include "../../../include/errorHandle.h"
#include "../../../include/procTypes.h"

/**
 * @brief Encode the header of an EXI stream. The current position in the stream is set to
 * the first bit after the header. The EXIStream.EXIOptions* are set accordingly
 * @param[in, out] strm an empty EXI stream
 * @return Error handling code
 */
errorCode encodeHeader(EXIStream* strm);

#endif /* HEADERENCODE_H_ */
