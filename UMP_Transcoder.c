/*
 *  UMP_Transcoder.c
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

/* 
Release notes 
05/01/25
	- Bug corrected in TranscodeSYSEX_UMP() : function never returns 0 when last packet is generated

28/07/2026
	- Modified bitmask for MIDI 1.0 data fields to 0x7F rather 0xFF (in case UMP packets are corrupted)
	- Added capability to decode MIDI 2.0 -> MIDI 1.0 in TranscodeUMP_MIDI1()
*/

#include "UMP_Transcoder.h"

unsigned char TranscodeMIDI1_UMP (uint8_t* MIDIBytes, unsigned int MIDI1Length, uint32_t* UMPMessage)
{
	unsigned int ByteShift;
	unsigned int ByteCounter;

	// Bytes are ordered in the pointer as standard MIDI (example : B0 40 7F)
	// Channel voice messages are encoded using UMP MT2
	if ((MIDIBytes[0]>=0x80)&&(MIDIBytes[0]<=0xEF))
	{
		if (MIDI1Length==3)
		{
			UMPMessage[0] = 0x20000000+(MIDIBytes[0]<<16)+(MIDIBytes[1]<<8)+(MIDIBytes[2]);
			return 1;
		}
		else if (MIDI1Length==2)
		{
			UMPMessage[0] = 0x20000000+(MIDIBytes[0]<<16)+(MIDIBytes[1]<<8);
			return 1;
		}
		return 0;
	}

	// Send the SYSEX using MT3 (7-bit SYSEX)
	// 0xF0 and 0xF7 are discarded with UMP, so the length is -2
	if (MIDIBytes[0] == 0xF0)
	{
		MIDI1Length -= 2;		// To ignore F0 and F7

		// SYSEX fits in "Complete SYSEX" (Status = 0) -> Message size is <= 6
		if (MIDI1Length <= 6)
		{
			UMPMessage[0] = 0x30000000 + (MIDI1Length << 16) + (MIDIBytes[1] << 8);		// SYSEX of 0 bytes sounds stupid, no ? So we are sure we will have at least one byte between F0 and F7
			if (MIDI1Length >= 2)
				UMPMessage[0] += MIDIBytes[2];		// Add next byte in first word

			// Generate second word depending on number of remaining bytes
			UMPMessage[1] = 0;
			if (MIDI1Length > 2)
			{
				ByteShift = 24;

				for (ByteCounter = 3; ByteCounter <= MIDI1Length; ByteCounter++)
				{
					UMPMessage[1] |= MIDIBytes[ByteCounter] << ByteShift;
					ByteShift -= 8;
				}
			}
			return 1;
		}
	}

	// Other messages are system and realtime MIDI 1.0 messages. They are encoded using UMP MT1
	if (MIDI1Length==1)
	{
		UMPMessage[0] = 0x10000000+(MIDIBytes[0]<<16);
		return 1;
	}
	else if (MIDI1Length==2)
	{
		UMPMessage[0] = 0x10000000+(MIDIBytes[0]<<16)+(MIDIBytes[1]<<8);
		return 1;
	}
	else if (MIDI1Length==3)
	{
		UMPMessage[0] = 0x10000000+(MIDIBytes[0]<<16)+(MIDIBytes[1]<<8)+(MIDIBytes[2]);
		return 1;
	}

	return 0;
}  // TranscodeMIDI1_UMP
//---------------------------------------------------------------------------

// For SYSEX of more than 6 bytes, there will be two UMP packets (Start / End or Start / Continue / End)
unsigned char TranscodeSYSEX_UMP(uint8_t* MIDIBytes, unsigned int MIDI1Length, unsigned int* PtrSYSEX, uint32_t* UMPMessage)
{
	unsigned int ByteCounter;
	int RemainingBytes;

	// Make sure the packet we convert is a SYSEX
	if (MIDIBytes[0] != 0xF0)
		return 0;

	MIDI1Length -= 2;	// F0 and F7 are not counted

	// Short SYSEX (<= 6 bytes) must be processed by TranscodeMIDI1_UMP
	if (MIDI1Length <= 6)
		return 0;

	// If last UMP packet has been generated, report that there is no more SYSEX data to convert
	if (*PtrSYSEX > MIDI1Length)
		return 0;

	// First packet : Start packet with 6 bytes (as shorter packets are not processed here)
	if (*PtrSYSEX == 0)
	{
		UMPMessage[0] = 0x30160000 + (MIDIBytes[1] << 8) + MIDIBytes[2];
		UMPMessage[1] = (MIDIBytes[3] << 24) + (MIDIBytes[4] << 16) + (MIDIBytes[5] << 8) + MIDIBytes[6];
		*PtrSYSEX = 7;	// Prepare for next packet
		return 1;
	}

	// Check if we have enough bytes remaining for Continue message
	ByteCounter = *PtrSYSEX;
	RemainingBytes = MIDI1Length - ByteCounter + 1;
	if (RemainingBytes > 6)
	{
		UMPMessage[0] = 0x30260000 + (MIDIBytes[ByteCounter] << 8) + MIDIBytes[ByteCounter + 1];
		UMPMessage[1] = (MIDIBytes[ByteCounter + 2] << 24) + (MIDIBytes[ByteCounter + 3] << 16) + (MIDIBytes[ByteCounter + 4] << 8) + MIDIBytes[ByteCounter + 5];
		ByteCounter += 6;
		*PtrSYSEX = ByteCounter;
		return 1;
	}
	else
	{	// If 6 bytes or less remaining, generate the Stop message
		UMPMessage[0] = 0x30300000 + (RemainingBytes << 16) + (MIDIBytes[ByteCounter] << 8);

		if (RemainingBytes >= 2)
			UMPMessage[0] += MIDIBytes[ByteCounter + 1];		// Add next byte in first word
	
		// Generate second word depending on number of remaining bytes
		// Can't use ByteCounter for a loop here...

		UMPMessage[1] = 0;

		if (RemainingBytes >= 3)
			UMPMessage[1] = MIDIBytes[ByteCounter + 2] << 24;

		if (RemainingBytes >= 4)
			UMPMessage[1] |= MIDIBytes[ByteCounter + 3] << 16;

		if (RemainingBytes >= 5)
			UMPMessage[1] |= MIDIBytes[ByteCounter + 4] << 8;

		if (RemainingBytes == 6)
			UMPMessage[1] |= MIDIBytes[ByteCounter + 5];

		// Make ByteCounter pass over the SYSEX size so next call to this function will return 0
		ByteCounter += 16;		// To be sure we really pass over the limit for the next call :-)
		*PtrSYSEX = ByteCounter;

		return 1;
	}
}  // TranscodeSYSEX_UMP
//---------------------------------------------------------------------------

unsigned int TranscodeUMP_MIDI1 (uint32_t* SourceUMP, uint8_t* MIDIMsg)
{
	uint8_t Status, Data1, Data2;
	unsigned int SYSEXLen;
	uint32_t Value14;

	// if MT=1, we have realtime MIDI 1.0 message
	if ((SourceUMP[0]&0xF0000000)==0x10000000)
	{
		if ((SourceUMP[0]&0xFF0000)==0xF20000)
		{  // Three bytes message
			MIDIMsg[0] = 0xF2;
			MIDIMsg[1] = (SourceUMP[0]>>8)&0x7F;
			MIDIMsg[2] = SourceUMP[0]&0x7F;
			return 3;
		}
		else if (((SourceUMP[0]&0xFF0000)==0xF10000) || ((SourceUMP[0]&0xFF0000)==0xF30000))
		{  // Two bytes MIDI message
			MIDIMsg[0] = (SourceUMP[0]>>16)&0xFF;
			MIDIMsg[1] = (SourceUMP[0]>>8)&0x7F;
			return 2;
		}
		else
		{  // All other messages are 1 byte
			MIDIMsg[0] = (SourceUMP[0]>>16)&0xFF;
			return 1;
		}
	}

	// if MT=2, we have a MIDI 1.0 channel message
	if ((SourceUMP[0]&0xF0000000)==0x20000000)
	{
		Status = (SourceUMP[0]>>16)&0xFF;
		Data1 = (SourceUMP[0]>>8)&0x7F;
		Data2 = SourceUMP[0]&0x7F;

		if ((Status>=0x80)&&(Status<=0xBF))
		{
			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			MIDIMsg[2] = Data2;
			return 3;
		}
		else if ((Status>=0xC0)&&(Status<=0xDF))
		{
			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			return 2;
		}
		else if ((Status>=0xE0)&&(Status<=0xEF))
		{
			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			MIDIMsg[2] = Data2;
			return 3;
		}
		return 0;		// This should normally never happen
	}

	// if MT=4, we have a MIDI 2.0 channel message
	// Try to convert it to MIDI 1.0 using MMA rules (see UMP specification Appendix D.2)
	if ((SourceUMP[0]&0xF0000000)==0x40000000)
	{
		Status = (SourceUMP[0]>>16)&0xFF;
		Data1 = (SourceUMP[0]>>8)&0x7F;

		// MIDI 2.0 Note Off
		if ((Status>=0x80)&&(Status<=0x8F))
		{  // Direct translation to MIDI 1.0 Note Off
			Data2 = SourceUMP[1]>>25;

			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			MIDIMsg[2] = Data2;

			return 3;
		}

		// MIDI 2.0 Note On
		if ((Status>=0x90)&&(Status<=0x9F))
		{  // Special case to handle : if velocity is 0 after shifting, it must be set to 1 (otherwise MIDI 1.0 considers it as Note Off)

			Data2 = SourceUMP[1]>>25;
			if (Data2==0)
				Data2 = 1;

			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			MIDIMsg[2] = Data2;

			return 3;				
		}

		// MIDI 2.0 Polypressure / Control Change
		if ((Status>=0xA0)&&(Status<=0xBF))
		{  // Direct translation to MIDI 1.0
			Data2 = SourceUMP[1]>>25;

			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data1;
			MIDIMsg[2] = Data2;

			return 3;
		}

		// MIDI 2.0 Channel pressure
		if ((Status>=0xD0)&&(Status<=0xDF))
		{  // Direct translation to MIDI 1.0
			Data2 = SourceUMP[1]>>25;

			MIDIMsg[0] = Status;
			MIDIMsg[1] = Data2;

			return 2;
		}

		// MIDI 2.0 Pitch Bend
		if ((Status>=0xE0)&&(Status<=0xEF))
		{	// Translate 32-bit bend resolution to 14-bit
			Value14 = SourceUMP[1]>>18;

			// On MIDI 1.0, pitch bend is LSB MSB, so we swap
			MIDIMsg[0] = Status;
			MIDIMsg[1] = Value14&0x7F;	// LSB
			MIDIMsg[2] = (Value14>>7)&0x7F;		// MSB

			return 3;
		}		

		// MIDI 2.0 Program Change
		if ((Status>=0xC0)&&(Status<=0xCF))
		{
			if ((SourceUMP[0]&1)==0)
			{  // B bit is not set : only generate a MIDI 1.0 Program Change
				Data2 = (SourceUMP[1]>>24)&0x7F;

				MIDIMsg[0] = Status;
				MIDIMsg[1] = Data2;

				return 2;
			}
			else
			{  // B bit is set : we have to generate three MIDI 1.0 messages : Bank Select MSB, Bank Select LSB, Program Change
				// MIDI Bank Select MSB message
				MIDIMsg[0] = 0xB0 + (Status&0xF);		// Control Change + copy the channel from original message
				MIDIMsg[1] = 0;		// Bank Select MSB
				MIDIMsg[2] = (SourceUMP[1]>>8)&0x7F;

				MIDIMsg[3] = 0xB0 + (Status&0xF);		// Control Change + copy the channel from original message
				MIDIMsg[4] = 32;	// Bank Select LSB
				MIDIMsg[5] = SourceUMP[1] & 0x7F;

				MIDIMsg[6] = Status;		// Program Change
				MIDIMsg[7] = (SourceUMP[1]>>24)&0x7F;
				
				return 8;
			}
		}

		// MIDI 2.0 RPN Controller
		if ((Status>=0x20)&&(Status<=0x2F))
		{
			MIDIMsg[0] = 0xB0 + (Status&0xF);		// Control Change + copy the channel from original message
			MIDIMsg[1] = 101;
			MIDIMsg[2] = (SourceUMP[0]>>8)&0x7F;

			MIDIMsg[3] = MIDIMsg[0];
			MIDIMsg[4] = 100;
			MIDIMsg[5] = SourceUMP[0]&0x7F;

			Value14 = SourceUMP[1]>>18;
			MIDIMsg[6] = MIDIMsg[0];
			MIDIMsg[7] = 6;
			MIDIMsg[8] = Value14>>7;

			MIDIMsg[9] = MIDIMsg[0];
			MIDIMsg[10] = 38;
			MIDIMsg[11] = Value14&0x7F;

			return 12;
		}

		// MIDI 2.0 NRPN Controller
		if ((Status>=0x30)&&(Status<=0x3F))
		{
			MIDIMsg[0] = 0xB0 + (Status&0xF);		// Control Change + copy the channel from original message
			MIDIMsg[1] = 99;
			MIDIMsg[2] = (SourceUMP[0]>>8)&0x7F;

			MIDIMsg[3] = 0xB0 + (Status&0xF);		// Control Change + copy the channel from original message
			MIDIMsg[4] = 98;
			MIDIMsg[5] = SourceUMP[0]&0x7F;

			Value14 = SourceUMP[1]>>18;
			MIDIMsg[6] = MIDIMsg[0];
			MIDIMsg[7] = 6;
			MIDIMsg[8] = Value14>>7;

			MIDIMsg[9] = MIDIMsg[0];
			MIDIMsg[10] = 38;
			MIDIMsg[11] = Value14&0x7F;

			return 12;
		}

		return 0;		// MIDI 2.0 message can not be translated to MIDI 1.0
	}
	
	// Single 7-bit SYSEX packet
	if ((SourceUMP[0] & 0xF0F00000) == 0x30000000)
	{
		SYSEXLen = (SourceUMP[0] >> 16) & 0x0F;
		MIDIMsg[0] = 0xF0;
		// Decode the six bytes even if message is shorter (faster than looping)
		MIDIMsg[1] = (SourceUMP[0] >> 8) & 0x7F;
		MIDIMsg[2] = SourceUMP[0] & 0x7F;
		MIDIMsg[3] = (SourceUMP[1] >> 24) & 0x7F;
		MIDIMsg[4] = (SourceUMP[1] >> 16) & 0x7F;
		MIDIMsg[5] = (SourceUMP[1] >> 8) & 0x7F;
		MIDIMsg[6] = SourceUMP[1] & 0x7F;

		MIDIMsg[SYSEXLen + 1] = 0xF7;

		return SYSEXLen + 2;	// Add F0 and F7
	}

	// in all other cases, the UMP message can not translated to MIDI 1.0
	return 0;
}  // TranscodeUMP_MIDI1
//---------------------------------------------------------------------------

unsigned int RebuildSYSEXFromUMP(uint32_t* SourceUMP, TSYSEX7_Decoder_Control* Decoder)
{
	unsigned int Size;

	if ((SourceUMP[0] & 0xF0000000) != 0x30000000)
		return 0;		// Not a SYSEX7 UMP packet

	// If this is a SYSEX Start
	// - Reinit the decoder (new SYSEX message)
	// - Set UMPStarted flag
	// - Add F0 to output buffer
	// - Read the 6 bytes (maybe 0 to 6 !)
	if ((SourceUMP[0] & 0xF0F00000) == 0x30100000)
	{
		Size = (SourceUMP[0] >> 16) & 0x0F;

		Decoder->SYSEXBuffer[0] = 0xF0;
		Decoder->SYSEXBuffer[1] = (SourceUMP[0] >> 8) & 0x7F;
		Decoder->SYSEXBuffer[2] = SourceUMP[0] & 0x7F;
		Decoder->SYSEXBuffer[3] = (SourceUMP[1] >> 24) & 0x7F;
		Decoder->SYSEXBuffer[4] = (SourceUMP[1] >> 16) & 0x7F;
		Decoder->SYSEXBuffer[5] = (SourceUMP[1] >> 8) & 0x7F;
		Decoder->SYSEXBuffer[6] = SourceUMP[1] & 0x7F;
		Decoder->UMPStarted = 1;
		Decoder->SYSEXSize = Size+1;
	}

	// If this is a SYSEX Continue
	// - Read the 6 bytes (as a SYSEX Continue always contains 6 bytes, otherwise it would be a SYSEX End)
	else if ((SourceUMP[0] & 0xF0F00000) == 0x30200000)
	{
		if (Decoder->UMPStarted == 0)
			return 0;

		if (Decoder->SYSEXSize >= MAX_SYSEX_SIZE - 6)
		{  // SYSEX packet is too big to fit into the decoder buffer : reject message
			Decoder->UMPStarted = 0;
			return 0;
		}

		Size = (SourceUMP[0] >> 16) & 0x0F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize] = (SourceUMP[0] >> 8) & 0x7F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize+1] = SourceUMP[0] & 0x7F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize+2] = (SourceUMP[1] >> 24) & 0x7F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize+3] = (SourceUMP[1] >> 16) & 0x7F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize+4] = (SourceUMP[1] >> 8) & 0x7F;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize+5] = SourceUMP[1] & 0x7F;

		Decoder->SYSEXSize += Size;
	}

	// If this is a SYSEX End
	// - Get the remaining size
	// - Add the F7 to the buffer
	// - Reset UMPStarted flag
	// - Return the size, to inform that packet is ready
	else if ((SourceUMP[0] & 0xF0F00000) == 0x30300000)
	{
		if (Decoder->UMPStarted == 0)
			return 0;

		Size = (SourceUMP[0] >> 16) & 0x0F;

		if (Size>=1)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize] = (SourceUMP[0] >> 8) & 0x7F;

		if (Size>=2)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize+1] = SourceUMP[0] & 0x7F;

		if (Size >= 3)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize + 2] = (SourceUMP[1] >> 24) & 0x7F;

		if (Size >= 4)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize + 3] = (SourceUMP[1] >> 16) & 0x7F;

		if (Size >= 5)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize + 4] = (SourceUMP[1] >> 8) & 0x7F;

		if (Size == 6)
			Decoder->SYSEXBuffer[Decoder->SYSEXSize + 5] = SourceUMP[1] & 0x7F;

		Decoder->SYSEXSize += Size;
		Decoder->UMPStarted = 0;
		Decoder->SYSEXBuffer[Decoder->SYSEXSize] = 0xF7;
		return Decoder->SYSEXSize+1;
	}

	return 0;
}  // RebuildSYSEXFromUMP
//---------------------------------------------------------------------------
