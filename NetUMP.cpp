/*
 *  NetUMP.cpp
 *  Generic class for NetUMP session initiator/listener
 *
 * Copyright (c) 2023-2025 Benoit BOUCHEZ / KissBox
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

/* Release notes
06/06/2023 :
 - changed PrepareTimerEvent to 1 in InitiateSession to start session immediately (was delayed of 1 second without reason)

22/11/2023
  - update for V0.7.6 protocol version

16/04/2024
  - added SetCallback method to declare callback and instance outside from constructor

27/05/2024
  - update to V0.7.9 protocol version

12/09/2024
  - update to V0.8.1 protocol version
  - removed DataSize parameter in the callback (UMP size is given by MT field)

03/02/2025
  - merged changes made by oscaracena (https://github.com/oscaracena) - Branch 664cb0d
	- added callback function for session events
  - added initialization of PeerEndpointNameSize in RunSession() to mute a compiler warning (variable maybe uninitialized)

15/02/2025
  - removed unused private member variable (UMP_FIFO_FROM_NET)
*/

#include "NetUMP.h"
#include "SystemSleep.h"
#include <stdio.h>

// Session status
#define SESSION_CLOSED			0	// No action
#define SESSION_CLOSE			1	// Session should close in emergency
#define SESSION_INVITE			2	// Sending invitation to remote partner
#define SESSION_WAIT_INVITE		4	// Wait to be invited by remote station
#define SESSION_OPENED			8	// Session is opened, just generate background traffic now

//! Size of UMP messages in words for each possible MT
static unsigned int UMPSize [16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};

//! Maximum number of milliseconds allowed between two incoming messages before connection is closed automatically
#define TIMEOUT_RESET		30000

CNetUMPHandler::CNetUMPHandler (TUMPDataCallback CallbackFunc, void* UserInstance)
{
	UMPSocket = INVALID_SOCKET;
	SessionState=SESSION_CLOSED;

	RemoteIP = 0;
	RemoteUDPPort = 0;
	LocalUDPPort = 0;
	SocketLocked=true;
	strcpy((char*)&this->EndpointName[0], "NetUMP");
	strcpy((char*)&this->ProductInstanceID[0], "DefaultID");

	SessionPartnerIP=0;
	SessionPartnerPort=0;
	IsInitiatorNode=true;
	TimeOutRemote=TIMEOUT_RESET;
	InviteCount=0;

	ConnectionLost = false;
	PeerClosedSession = false;

	LastReceivedUMPCounter = 0;
	PINGDelayCounter = 0;
	PINGIdCounter = 0;

	ResetFECMemory();
	SelectErrorCorrectionMode (ERROR_CORRECTION_FEC);
	//SelectErrorCorrectionMode (ERROR_CORRECTION_NONE);

	// Reset FIFO with host
	UMP_FIFO_TO_NET.ReadPtr = 0;
	UMP_FIFO_TO_NET.WritePtr = 0;

	// Reset timer
	TimeCounter=0;
	TimerRunning=false;
	TimerEvent=false;
	EventTime=0;

	UMPCallback=CallbackFunc;
	ClientInstance=UserInstance;

	ConnectionCallback = 0;
	DisconnectCallback = 0;
}  // CNetUMPHandler::CNetUMPHandler
// -----------------------------------------------------

CNetUMPHandler::~CNetUMPHandler (void)
{
	CloseSession();
	CloseSockets();
}  // CNetUMPHandler::~CNetUMPHandler
// -----------------------------------------------------

void CNetUMPHandler::CloseSockets(void)
{
	// Close the UDP sockets
	if (UMPSocket!=INVALID_SOCKET)
		CloseSocket(&UMPSocket);
}  // CNetUMPHandler::CloseSockets
//---------------------------------------------------------------------------

void CNetUMPHandler::SetEndpointName (char* Name)
{
	if (strlen(Name) == 0) return;
	if (strlen(Name) >= MAX_UMP_ENDPOINT_NAME_LEN-1) return;
	strcpy ((char*)&this->EndpointName[0], Name);
}  // CNetUMPHandler::SetEndpointName
//---------------------------------------------------------------------------

void CNetUMPHandler::SetProductInstanceID(char* PIID)
{
	if (strlen(PIID) == 0) return;
	if (strlen(PIID) >= MAX_UMP_PRODUCT_INSTANCE_ID_LEN) return;
	strcpy((char*)&this->ProductInstanceID[0], PIID);
}  // CNetUMPHandler::SetProductInstanceID
//---------------------------------------------------------------------------

int CNetUMPHandler::InitiateSession(unsigned int DestIP,
						unsigned short DestPort,
						unsigned short LocalPort,
						bool IsInitiator)
{
	bool SocketOK;

	// Close the UDP socket, just in case it was still opened...
	CloseSockets();

	this->RemoteIP=DestIP;
	this->RemoteUDPPort=DestPort;
	this->LocalUDPPort = LocalPort;

	SocketOK=CreateUDPSocket (&UMPSocket, LocalPort, false);
	if (SocketOK == false) return -1;

	ConnectionLost = false;
	this->InviteCount=0;
	this->TimeOutRemote=TIMEOUT_RESET;
	this->UMPSequenceCounter = 0;
	this->PINGDelayCounter = 0;
	this->TimerRunning = false;

	this->IsInitiatorNode=IsInitiator;
	if (IsInitiator==false)
	{  // Do not invite, wait from remote node to start session
		SessionState=SESSION_WAIT_INVITE;
	}
	else
	{ // Initiate session by inviting remote node
		SessionState=SESSION_INVITE;
        SessionPartnerIP = RemoteIP;
		SessionPartnerPort = RemoteUDPPort;
	}
	this->SocketLocked=false;		// Must be last instruction after session initialization
	PrepareTimerEvent(1);	// This will produce invitation immediately

	return 0;
}  // CNetUMPHandler::InitiateSession
//---------------------------------------------------------------------------

void CNetUMPHandler::CloseSession (void)
{
	if (SessionState==SESSION_OPENED)
	{
		SessionState=SESSION_CLOSED;
		SendBYECommand(BYE_USER_TERMINATED, SessionPartnerIP, SessionPartnerPort);
		SystemSleepMillis(50);		// Give time to send the message before closing the socket

		if (DisconnectCallback != 0)
			DisconnectCallback();
	}
}  // CNetUMPHandler::CloseSession
//---------------------------------------------------------------------------

void CNetUMPHandler::RunSession (void)
{
#if defined (__TARGET_MAC__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_LINUX__)
	socklen_t fromlen;
#endif
#if defined (__TARGET_WIN__)
	int fromlen;
#endif
	int RecvSize;
	sockaddr_in SenderData;
	unsigned char ReceptionBuffer[1024];
	bool InvitationAccepted;
	unsigned int UMPCommandSize;
	uint32_t UMPCommand[65];		// Maximum length of UMP Command is 64 words + command header
	sockaddr_in AdrEmit;
	bool InvitationReceived;
	bool BYEReceived;
	bool PingReceived;
	unsigned int SenderIP=0;
	unsigned short SenderPort=0;
	uint32_t Ping_ID=0;
	TUMP_PING_PACKET_NO_SIGNATURE* PingPacket;
	int PtrParse;
	unsigned int PayloadSize;
	int PeerEndpointNamePtr = 0;
	unsigned int PeerEndpointNameSize = 0;

	// Do not process if communication layers are not ready
	if (SocketLocked) return;

	// Check if timer elapsed
	if (TimerRunning)
	{
		if (EventTime>0)
			EventTime--;
		if (EventTime==0)
		{
			TimerRunning=false;
			TimerEvent=true;
		}
	}

	// If no resync from remote node after 2 minutes and we are session initiator, then try to invite again the remote device
	if (SessionState == SESSION_OPENED)
	{
		if (TimeOutRemote > 0)
			TimeOutRemote--;

		if (TimeOutRemote == 0)
		{  // No messages received from remote partner after timeout
			ConnectionLost = true;

			// We send a BYE to inform remote partner that connection is now closed
			SendBYECommand (BYE_TIMEOUT, SessionPartnerIP, SessionPartnerPort);

			if (IsInitiatorNode)
			{
				SessionState = SESSION_CLOSED;
				RestartSessionInitiator();
				// TODO : use the same option as for receiving a BYE to decide if we allow the initiator to restart or the session must be closed
			}
			else
			{  // If we are not session initiator, just wait to be invited again
				SessionState=SESSION_WAIT_INVITE;
			}

			// Inform client application that we have closed connection
			if (DisconnectCallback != 0)
				DisconnectCallback();
		}  // Session has timed out
	}  // Session is opened

	// Init state decoder
	InvitationReceived = false;
	BYEReceived = false;
	InvitationAccepted = false;
	PingReceived = false;

	// Check if something has been received
	if (DataAvail(UMPSocket, 0))
	{
		fromlen=sizeof(sockaddr_in);
		RecvSize=(int)recvfrom(UMPSocket, (char*)&ReceptionBuffer, sizeof(ReceptionBuffer), 0, (sockaddr*)&SenderData, &fromlen);

		if (RecvSize>0)
		{
			// Check UMP header ("MIDI")
			if ((ReceptionBuffer[0]=='M')&&(ReceptionBuffer[1]=='I')&&(ReceptionBuffer[2]=='D')&&(ReceptionBuffer[3]=='I'))
			{
				SenderIP=htonl(SenderData.sin_addr.s_addr);
				SenderPort = htons (SenderData.sin_port);

				// Parse the received NetUMP packets (a single UDP packets can contain multiple NetUMP packets)
				PtrParse = 4;		// Jump over MIDI signature

				//printf ("New UDP packet size : %d\n", RecvSize);
				while (PtrParse<RecvSize)
				{
					PayloadSize = ReceptionBuffer[PtrParse+1];
					PayloadSize*=4;		// Payload size is given in 32 bits words, turn it into byte

					//printf ("Payload size : %d\n", PayloadSize);
                    //printf ("PtrParse : %d\n", PtrParse);

					switch (ReceptionBuffer[PtrParse])
					{
						case UMP_DATA_COMMAND :
						// Check that message comes from the remote partner
	#if defined (__TARGET_WIN__)
							if (htonl(SenderData.sin_addr.S_un.S_addr) == SessionPartnerIP)
	#endif
	#if defined (__TARGET_MAC__)
							if (htonl(SenderData.sin_addr.s_addr) == SessionPartnerIP)
	#endif
	#if defined (__TARGET_LINUX__)
							if (htonl(SenderData.sin_addr.s_addr) == SessionPartnerIP)
	#endif
							{
								if (htons(SenderData.sin_port) == SessionPartnerPort)
								{
									if (SessionState == SESSION_OPENED)
									{
										TimeOutRemote = TIMEOUT_RESET;
										ProcessIncomingUMP(&ReceptionBuffer[PtrParse]);
									}
								}
							}
							break;
						case INVITATION_COMMAND :
							InvitationReceived = true;
							// Size is given in 32-bit words, convert it into bytes
							PeerEndpointNameSize = ReceptionBuffer[PtrParse + 2] * 4;
							PeerEndpointNamePtr = PtrParse + 4;
							break;
						case BYE_COMMAND :
							BYEReceived = true;
							break;
						case INVITATION_ACCEPTED_COMMAND :
							PeerEndpointNameSize = ReceptionBuffer[PtrParse + 2] * 4;
							PeerEndpointNamePtr = PtrParse + 4;
							InvitationAccepted = true;
							break;
						case PING_COMMAND :
							// TODO : receiving a PING from a remote station means it is alive
							// So if the session is opened and *sender is the remote partner*, we reset timeout counter
							PingReceived = true;
							PingPacket = (TUMP_PING_PACKET_NO_SIGNATURE*)&ReceptionBuffer[PtrParse];
							Ping_ID = htonl(PingPacket->ID);
							break;
						case PING_REPLY_COMMAND :
							// TODO : Reset timeout counter if we receive a PING Reply only with the packet ID matching the one we sent
							if (SessionState == SESSION_OPENED)
								TimeOutRemote = TIMEOUT_RESET;
							break;
						case SESSION_RESET_COMMAND :
							// TODO
							// Reset sequence numbers
							// Flush FEC buffers
							// We should report the reste to application layer (send All Notes Off...)
							break;
						case SESSION_RESET_REPLY_COMMAND :
							// TODO 
							// If we did not send a SESSION RESET and we receive a REPLY, we should send a SESSION RESET command
							// If this message is received out of an active session, send BYE with SESSION_NOT_ESTABLISHED
							break;

#ifdef __DEBUG__
						default :
							printf ("Hummmm...\n");
#endif
					}  // switch

					PtrParse+=4;			    // Jump over command header (32 bits)
					PtrParse+=PayloadSize;		// Jump to next NetUMP message in UDP packet
				}  // Loop over all NetUMP commands
			}  // MIDI signature found
		}  // Receives size > 0
	}  // Packet received on socket

	// TODO : should we move this processing inside the parsing loop ?
	// In theory, there is no reason to receive multiple session packets or session packets mixed with UMP messages in the same UDP telegram
	if (InvitationReceived)
	{
		// If we are a session listener AND session is not yet opened, send INVITATION ACCEPTED and open the session
		if (IsInitiatorNode==false)
		{  // Session listener
			if (SessionState==SESSION_WAIT_INVITE)
			{
				TimeOutRemote = TIMEOUT_RESET;
				SessionState = SESSION_OPENED;
				this->SessionPartnerIP=SenderIP;
				this->SessionPartnerPort = SenderPort;
				SendInvitationAcceptedCommand ();
				ResetFECMemory();

				if (ConnectionCallback != 0)
					ConnectionCallback((const char*)(ReceptionBuffer + PeerEndpointNamePtr), PeerEndpointNameSize);
			}
		}
		else
		{  // Session initiator : for now, we don't accept to be invited (TODO : we can acccept an invitation if it comes from the declared partner)
			SendBYECommand (BYE_TOO_MANY_SESSIONS, SenderIP, SenderPort);
		}
	}  // Invitation received

	if (PingReceived)
	{
		SendPINGReplyCommand (Ping_ID);
	}

	if (BYEReceived)
	{
		if ((SenderIP == SessionPartnerIP)&&(SenderPort == SessionPartnerPort))
		{
			SendBYEReplyCommand (SessionPartnerIP, SessionPartnerPort);
			if (IsInitiatorNode == false)
			{
				SessionState = SESSION_WAIT_INVITE;
				SessionPartnerIP = 0;
				SessionPartnerPort = 0;
			}
			else
			{
				SessionState = SESSION_CLOSED;
				// TODO : make an option to decide if session must close if a BYE is received or if it shall invite again the partner
				RestartSessionInitiator ();		// This make the driver automatically invite again a partner which has sent a BYE
			}
			ConnectionLost = true;		// This will report information to user interface

			if (DisconnectCallback != 0)
				DisconnectCallback();
		}
		else
		{
			SendBYEReplyCommand (SenderIP, SenderPort);
		}
	}  // Bye received

	// *** State machine manager ***
	if (SessionState==SESSION_CLOSED)
	{
		return;
	}

	// We must call GenerateUMPCommand even if session is not opened in order to flush the FIFO
	// Otherwise, all UMP data are sent in bursts when session opens...
	UMPCommandSize = GenerateUMPCommand(&UMPCommand[0]);

	if (SessionState==SESSION_OPENED)
	{  // Send UMP data if something in the FIFO
		if (UMPCommandSize>0)
		{
			// Send message on network
			memset (&AdrEmit, 0, sizeof(sockaddr_in));
			AdrEmit.sin_family=AF_INET;
			AdrEmit.sin_addr.s_addr=htonl(SessionPartnerIP);
			AdrEmit.sin_port=htons(SessionPartnerPort);
			sendto(UMPSocket, (const char*)&UMPCommand[0], UMPCommandSize*4, 0, (const sockaddr*)&AdrEmit, sizeof(sockaddr_in));
		}

		// Send PING message if nothing has been sent since more than 10 seconds
		PINGDelayCounter++;
		if (PINGDelayCounter>10000)
		{
			PINGDelayCounter = 0;
			PINGIdCounter++;

			SendPINGCommand (PINGIdCounter);
		}
		return;
	}

	// We are inviting remote node
	if (SessionState==SESSION_INVITE)
	{
		if (InvitationAccepted)
		{
			SessionPartnerIP = SenderIP;		// TODO : what happens if we receive accidentally an INVITATION ACCEPTED from another device while we are inviting one ?
			SessionState=SESSION_OPENED;
			ResetFECMemory();

			if (ConnectionCallback != 0)
				ConnectionCallback((const char*)(ReceptionBuffer + PeerEndpointNamePtr), PeerEndpointNameSize);
			return;
		}

		if (TimerRunning==false)
		{
			if (TimerEvent)
			{  // Previous attempt has timed out
				/*
				if (InviteCount>12)
				{  // No answer received from remote station after 12 attempts : stop invitation and go back to SESSION_INVITE_CONTROL
					RestartSession();
					return;
				}
				else
				*/
				{
					this->SendInvitationCommand();
					PrepareTimerEvent(1000);  // Wait one second before sending a new invitation
					InviteCount++;
					return;
				}
			}
		}
		//else {  /* We wait for an event : nothing to do */ }
		return;
	}  // Session state is SESSION_INVITE

	if (SessionState==SESSION_WAIT_INVITE)
	{
		return;
	}
}  // CNetUMPHandler::RunSession
//---------------------------------------------------------------------------

void CNetUMPHandler::PrepareTimerEvent (unsigned int TimeToWait)
{
	TimerRunning=false;			// Lock the timer until preparation is done
	TimerEvent=false;			// Signal no event
	EventTime=TimeToWait;
	TimerRunning=true;			// Restart the timer
}  // CNetUMPHandler::PrepareTimerEvent
//---------------------------------------------------------------------------

void CNetUMPHandler::RestartSessionInitiator (void)
{
	if (this->IsInitiatorNode == false) return;
	//if (this->SessionState != SESSION_CLOSED) return;

	UMPSequenceCounter = 0;
	SessionState=SESSION_INVITE;
    PrepareTimerEvent(1000);
	TimeOutRemote=TIMEOUT_RESET;
	// Do not reset SessionPartnerIP and SessionPartnerPort as it will block the initiator process
}  // CNetUMPHandler::RestartSessionInitiator
//--------------------------------------------------------------------------

int CNetUMPHandler::GetSessionStatus (void)
{
	if (SessionState==SESSION_CLOSED) return 0;
	if (SessionState==SESSION_OPENED) return 3;
	if (SessionState==SESSION_INVITE) return 1;
	return 2;
}  // CNetUMPHandler::::GetSessionStatus
//--------------------------------------------------------------------------

bool CNetUMPHandler::ReadAndResetConnectionLost (void)
{
	if (ConnectionLost==false) return false;

	ConnectionLost=false;
	return true;
}  // CNetUMPHandler::ReadAndResetConnectionLost
//--------------------------------------------------------------------------

bool CNetUMPHandler::RemotePeerClosedSession (void)
{
	bool ReadValue;

	ReadValue = PeerClosedSession;
	PeerClosedSession = false;

	return ReadValue;
}  // CNetUMPHandler::RemotePeerClosedSession
//--------------------------------------------------------------------------

bool CNetUMPHandler::SendUMPMessage (uint32_t* UMPData)
{
	unsigned int TmpWrite;
	unsigned int WordCounter;
	unsigned int MT;
	unsigned int MsgSize;

	if (SessionState!=SESSION_OPENED) return false;		// Avoid filling the FIFO when nothing can be sent
	MT = UMPData[0]>>28;
	MsgSize = UMPSize[MT];

	TmpWrite = UMP_FIFO_TO_NET.WritePtr;		// Snapshot

	for (WordCounter=0; WordCounter<MsgSize; WordCounter++)
	{
		UMP_FIFO_TO_NET.FIFO[TmpWrite] = UMPData[WordCounter];
		TmpWrite++;
		if (TmpWrite>=UMP_FIFO_SIZE)
			TmpWrite = 0;

		// Check if FIFO is not full
		if (TmpWrite == UMP_FIFO_TO_NET.ReadPtr) return false;
	}

	// Update write pointer only when the whole block has been copied
	UMP_FIFO_TO_NET.WritePtr = TmpWrite;

	return true;
}  // CNetUMPHandler::SendUMPMessage
//--------------------------------------------------------------------------

unsigned int CNetUMPHandler::GenerateUMPCommand (uint32_t* UMPCommand)
{
	unsigned int UMPBlockEnd;			// Index of last UMP word in FIFO to transmit
	unsigned CtrWordPayload;
	unsigned int TempPtr;
	unsigned int FECIndex;
	unsigned int NewFECSlot;
	uint32_t NewUMPCommand[65];
	unsigned int NewCommandWordCount;
	bool MessageLimit;
	uint32_t NewUMP;
	unsigned int NewLength;

    UMPBlockEnd=UMP_FIFO_TO_NET.WritePtr;			// Snapshot of current position of last MIDI message

	// Check first if we have any UMP message waiting in the FIFO. If not, return 0 to signal nothing to transmit
    if (UMPBlockEnd==UMP_FIFO_TO_NET.ReadPtr) return 0;

	// Prepare the new UMP command packet into local buffer. Packet must be 64 words max
	NewCommandWordCount = 0;

	MessageLimit = false;
	TempPtr=UMP_FIFO_TO_NET.ReadPtr;

	while ((TempPtr!=UMPBlockEnd)&&(MessageLimit==false))
    {
		// Read first word of new UMP message to know its length depending on MT
		NewUMP = UMP_FIFO_TO_NET.FIFO[TempPtr];
		NewLength = UMPSize[NewUMP>>28];		// Get size from MT field

		if (NewCommandWordCount+NewLength<65)
		{
			NewUMPCommand[NewCommandWordCount+1] = htonl (NewUMP);		// Store first word
			NewCommandWordCount+=1;
			TempPtr+=1;
			if (TempPtr>=UMP_FIFO_SIZE)
				TempPtr=0;

			// if UMP is more than 1 word, copy the other words in the buffer
			if (NewLength>1)
			{
				for (unsigned int WordCount=1; WordCount<NewLength; WordCount++)
				{
					NewUMPCommand[NewCommandWordCount+1]=htonl(UMP_FIFO_TO_NET.FIFO[TempPtr]);
					NewCommandWordCount+=1;
					TempPtr+=1;
					if (TempPtr>=UMP_FIFO_SIZE)
						TempPtr=0;
				}
			}
		}
		else MessageLimit=true;
	}
    UMP_FIFO_TO_NET.ReadPtr=TempPtr;		// Update pointer when we have read messages from queue
	// Make header for the new UMP packet
	NewUMPCommand[0] = htonl(0xFF000000 + (NewCommandWordCount<<16) + UMPSequenceCounter);
	NewCommandWordCount+=1;		// Add header

	UMPSequenceCounter++;  // Increment for next message

	// *** Prepare message to be sent on network ***
	UMPCommand[0] = htonl (UMP_SIGNATURE);

	// Prepare Forward Error Correction packets before we add the newly computed packet
	if (ErrorCorrectionMode == ERROR_CORRECTION_FEC)
	{
		// Store new message into FEC memory (the slot will become last one when we shift, so the newest UMP packet will be placed at the end)
		NewFECSlot = NextFECSlot;
		for (unsigned int WordCount=0; WordCount<NewCommandWordCount+1; WordCount++)
		{
			FECMemory[NewFECSlot].Packet[WordCount] = NewUMPCommand[WordCount];
		}
		FECMemory[NewFECSlot].Filled = true;
		FECMemory[NewFECSlot].Size = NewCommandWordCount;

		// Shift FEC pointer to next slot (slot containing oldest data)
		NextFECSlot++;		// Points now to the slot containing the oldest UMP packets
		if (NextFECSlot>=NUM_FEC_ENTRIES) NextFECSlot = 0;

		// Copy all packets in the FEC memory into the transmission buffer
		// If Filled field is false, it means that no FEC packet has been created at this position for now, so we don't add it to the UMP
		CtrWordPayload = 1;			// Count the signature field we have added before
		FECIndex = NextFECSlot;
		for (int FECPollCounter=0; FECPollCounter<NUM_FEC_ENTRIES; FECPollCounter++)
		{
			if (FECMemory[FECIndex].Filled)
			{
				for (unsigned int WordCount=0; WordCount<FECMemory[FECIndex].Size; WordCount++)
				{
					UMPCommand[CtrWordPayload] = FECMemory[FECIndex].Packet[WordCount];
					CtrWordPayload++;
				}
			}
			FECIndex++;		// Switch to next slot
			if (FECIndex>=NUM_FEC_ENTRIES) FECIndex = 0;
		}

		return CtrWordPayload;
	}
	else
	{  // No error correction : just copy the new packet into the transmission buffer
		for (unsigned int WordCount=0; WordCount<NewCommandWordCount; WordCount++)
		{
			UMPCommand[WordCount+1] = NewUMPCommand[WordCount];
		}
		return NewCommandWordCount+1;		// Add the signature length
	}
}  // CNetUMPHandler::GenerateUMPCommand
//--------------------------------------------------------------------------

void CNetUMPHandler::ProcessIncomingUMP (unsigned char* Buffer)
{
	unsigned int PayloadLength;
	unsigned int WordCounter = 0;
	unsigned int ByteCounter;
	uint32_t UMPByte[4];
	uint32_t UMPMsg[4];
	unsigned int MT;
	unsigned int MessageSize;
	uint16_t PacketNumber;

	// Byte 0 : 0xFF
	// Byte 1 : payload length (in 32-bit words)
	// Byte 2/3 : packet counter

	PayloadLength = Buffer[1];
	PacketNumber = (Buffer[2]<<8)+(Buffer[3]);
	//printf ("Packet number %d\n", PacketNumber);

	// If packet counter is one of the last we have already received, it means this is a Forward Error Correction packet
	// It must then be ignored
	for (unsigned int FECCounter=0; FECCounter<NUM_FEC_ENTRIES; FECCounter++)
	{
		if (PacketNumber == ReceivedSequenceCounters[FECCounter])
		{
            //printf ("Recovery packet number : %d\n", PacketNumber);
            return;
        }
	}

	// Packet number is not in the list : this is a new packet
	LastReceivedUMPCounter = PacketNumber;

	// Update the received packet list (shift the data, then place the received packet number as the most recent one)
	for (unsigned int FECCounter=0; FECCounter<NUM_FEC_ENTRIES-1; FECCounter++)
	{
		ReceivedSequenceCounters[FECCounter] = ReceivedSequenceCounters[FECCounter+1];
	}
	ReceivedSequenceCounters[NUM_FEC_ENTRIES-1] = PacketNumber;		// Place the new packet number at the end of the list (most recent received one)

	// Parse all UMP packets that follows, until we reach the number of words from the header
	ByteCounter = 4;
	while (WordCounter<PayloadLength)
	{
		// Reconstruct first word, so we can know the size of the UMP packet
		UMPByte[0] = Buffer[ByteCounter++];
		UMPByte[1] = Buffer[ByteCounter++];
		UMPByte[2] = Buffer[ByteCounter++];
		UMPByte[3] = Buffer[ByteCounter++];
		WordCounter++;
		UMPMsg[0] = (UMPByte[0]<<24)+(UMPByte[1]<<16)+(UMPByte[2]<<8)+UMPByte[3];

		MT = UMPMsg[0]>>28;
		MessageSize = UMPSize[MT];

		if (MessageSize == 1)
		{  // UMP message is 32 bits long : send it now
			if (UMPCallback!=0)
				UMPCallback (ClientInstance, &UMPMsg[0]);
		}
		else
		{  // UMP message is 64, 96 or 128 bits long, decode the rest of the message
			// Reconstruct second word
			UMPByte[0] = Buffer[ByteCounter++];
			UMPByte[1] = Buffer[ByteCounter++];
			UMPByte[2] = Buffer[ByteCounter++];
			UMPByte[3] = Buffer[ByteCounter++];
			WordCounter++;
			UMPMsg[1] = (UMPByte[0]<<24)+(UMPByte[1]<<16)+(UMPByte[2]<<8)+UMPByte[3];

			if (MessageSize == 2)
			{  // UMP message is 64 bits long : send it now
				if (UMPCallback)
					UMPCallback (ClientInstance, &UMPMsg[0]);
			}
			else
			{  // Message is 96 or 128 bits long
				// Reconstruct third word
				UMPByte[0] = Buffer[ByteCounter++];
				UMPByte[1] = Buffer[ByteCounter++];
				UMPByte[2] = Buffer[ByteCounter++];
				UMPByte[3] = Buffer[ByteCounter++];
				WordCounter++;
				UMPMsg[2] = (UMPByte[0]<<24)+(UMPByte[1]<<16)+(UMPByte[2]<<8)+UMPByte[3];

				if (MessageSize == 3)
				{  // UMP message is 96 bits long : send it now
					if (UMPCallback)
						UMPCallback (ClientInstance, &UMPMsg[0]);
				}
				else
				{  // UMP message is 128 bits long : decode the last word and send the whole message
					UMPByte[0] = Buffer[ByteCounter++];
					UMPByte[1] = Buffer[ByteCounter++];
					UMPByte[2] = Buffer[ByteCounter++];
					UMPByte[3] = Buffer[ByteCounter++];
					WordCounter++;
					UMPMsg[3] = (UMPByte[0]<<24)+(UMPByte[1]<<16)+(UMPByte[2]<<8)+UMPByte[3];

					if (UMPCallback)
						UMPCallback (ClientInstance, &UMPMsg[0]);
				}
			}
		}
	}
}  // CNetUMPHandler::ProcessIncomingUMP
//--------------------------------------------------------------------------

void CNetUMPHandler::ResetFECMemory (void)
{
	UMPSequenceCounter = 0;
	NextFECSlot = 0;

	for (int Slot=0; Slot<NUM_FEC_ENTRIES; Slot++)
	{
		FECMemory[Slot].Filled = false;
		FECMemory[Slot].Size = 0;
		ReceivedSequenceCounters[Slot] = 0xFFFF;
	}
}  // CNetUMPHandler::ResetFECMemory
//--------------------------------------------------------------------------

void CNetUMPHandler::SelectErrorCorrectionMode (unsigned int CorrectionMethod)
{
	ErrorCorrectionMode = CorrectionMethod;
}  // CNetUMPHandler::SelectErrorCorrectionMode
//--------------------------------------------------------------------------

void CNetUMPHandler::SetCallback(TUMPDataCallback CallbackFunc, void* UserInstance)
{
	bool SocketState = this->SocketLocked;

	this->SocketLocked = true;		// Block processing to avoid callbacks while we configure them

	this->ClientInstance = UserInstance;
	this->UMPCallback = CallbackFunc;

	// Restore lock state
	this->SocketLocked = SocketState;
}  // CNetUMPHandler::SetCallback
//--------------------------------------------------------------------------

void CNetUMPHandler::SetConnectionCallback(void(*CallbackFunc)(const char* EndpointName, unsigned int Size))
{
	this->ConnectionCallback = CallbackFunc;
}  // CNetUMPHandler::SetConnectionCallback
//--------------------------------------------------------------------------

void CNetUMPHandler::SetDisconnectCallback(void(*CallbackFunc)())
{
	this->DisconnectCallback = CallbackFunc;
}  // CNetUMPHandler::SetDisconnectCallback
//--------------------------------------------------------------------------