/*
 *  UMP_Transcoder.h
 *  Functions to convert UMP <-> MIDI 1.0
 *  @280726
 *
 * Copyright (c) 2022 - 2026 Benoit BOUCHEZ / KissBox
 * License : MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __UMP_TRANSCODER_H__
#define __UMP_TRANSCODER_H__

#include <stdint.h>

// This value covers most of MIDI applications. Redefine it if needed
#ifndef MAX_SYSEX_SIZE
#define MAX_SYSEX_SIZE		256
#endif

//! Control structure to decode UMP SYSEX 7-bit packets into MIDI 1.0 SYSEX
typedef struct {
	unsigned char UMPStarted;		// Avoid processing SYSEX Continue or SYSEX End if a SYSEX Start has not been received
	unsigned int SYSEXSize;	// Size of rebuilt SYSEX packet
	uint8_t SYSEXBuffer[MAX_SYSEX_SIZE];
} TSYSEX7_Decoder_Control;

#ifdef __cplusplus
extern "C" {
#endif

//! Transform a MIDI 1.0 message into a single UMP message
//! \param MIDIBytes : pointer to MIDI 1.0 message to convert (includes SYSEX up to 8 bytes including F0 and F7)
//! \param MIDI1Length : number of bytes in the MIDI 1.0 message
//! \param UMPMessage : array of 4 uint32_t to receive the converted message
//! \return true if the MIDI message can be converted in a single UMP packet. False means message can not be converted into single UMP (or message is not recognized)
unsigned char TranscodeMIDI1_UMP (uint8_t* MIDIBytes, unsigned int MIDI1Length, uint32_t* UMPMessage);

//! Transform a MIDI 1.0 SYSEX packet into a UMP stream. The function must be called as long it returns true to get the UMP sequence
//! \param PtrSYSEX Must be zeroed before the first call for each new SYSEX to process. Contains position of the next byte in the SYSEX payload
//! \return true if a UMP packet has been generated (packet can be sent). False means no more data to be converted to UMP (or this is not a valid SYSEX)
unsigned char TranscodeSYSEX_UMP(uint8_t* MIDIBytes, unsigned int MIDI1Length, unsigned int* PtrSYSEX, uint32_t* UMPMessage);

//! Transform UMP message into MIDI 1.0 equivalent
//! If the UMP message is MIDI 2.0, the function tries to convert it to MIDI 1.0 equivalent using MMA rules from UMP
//! \param MIDIMsg array of bytes to receive the MIDI 1.0 message from UMP. 
//! MIDIMsg array MUST be at least 12 bytes long, as this function decodes MT=3 Single SYSEX packet or can generate multiple MIDI messages from MIDI 2.0
//! \return number of bytes in the MIDI message(s)
unsigned int TranscodeUMP_MIDI1 (uint32_t* SourceUMP, uint8_t* MIDIMsg);

//! Process UMP MT=3 packet to rebuild MIDI 1.0 SYSEX 
//! \return 0 if no SYSEX data is available, size of SYSEX packet if a SYSEX has been successfully rebuilt
unsigned int RebuildSYSEXFromUMP(uint32_t* SourceUMP, TSYSEX7_Decoder_Control* Decoder);

#ifdef __cplusplus
}
#endif

#endif