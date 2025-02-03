/*
 *  NetUMP.h
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

#ifndef __NETUMP_H__
#define __NETUMP_H__

#include "network.h"
#ifdef __TARGET_WIN__
#include <stdint.h>
#endif

#define MAX_UMP_ENDPOINT_NAME_LEN				99
#define MAX_UMP_PRODUCT_INSTANCE_ID_LEN			43

#define UMP_SIGNATURE 0x4D494449

// Callback type definition
// This callback is called from realtime thread. Processing time in the callback shall be kept to a minimum
// The function is called for each UMP packet received
#ifdef __TARGET_MAC__
typedef void (*TUMPDataCallback) (void* UserInstance, uint32_t* DataBlock);
#endif

#ifdef __TARGET_LINUX__
typedef void (*TUMPDataCallback) (void* UserInstance, uint32_t* DataBlock);
#endif

#ifdef __TARGET_WIN__
typedef void (CALLBACK *TUMPDataCallback) (void* UserInstance, uint32_t* DataBlock);
#endif

//! BYE command codes
#define BYE_UNDEFINED				0x00
#define BYE_USER_TERMINATED			0x01
#define BYE_POWER_DOWN				0x02
#define BYE_TOO_MANY_LOST_PACKETS	0x03
#define BYE_TIMEOUT					0x04
#define BYE_SESSION_NOT_ESTABLISHED	0x05
#define BYE_NO_PENDING_SESSION		0x06
#define BYE_PROTOCOL_ERROR			0x07
#define BYE_TOO_MANY_SESSIONS		0x40
#define BYE_INVITATION_AUTH_REJECTED	0x41
#define BYE_USER_DID_NOT_ACCEPT_SESSION			0x42
#define BYE_AUTHENTICATION_FAILED	0x43
#define BYE_USERNAME_NOT_FOUND		0x44
#define BYE_NO_MATCHING_AUTH_METHOD	0x45
#define BYE_INVITATION_CANCELED		0x80

//! Command codes
#define INVITATION_COMMAND								0x01
#define INVITATION_AUTHENTICATE_COMMAND					0x02
#define INVITATION_USER_AUTHENTICATE_COMMAND			0x03
#define INVITATION_ACCEPTED_COMMAND						0x10
#define INVITATION_PENDING_COMMAND						0x11
#define INVITATION_AUTHENTICATION_REQUIRED_COMMAND		0x12
#define INVITATION_USER_AUTHENTICATION_REQUIRED_COMMAND	0x13
#define PING_COMMAND									0x20
#define PING_REPLY_COMMAND								0x21
#define RETRANSMIT_COMMAND								0x80
#define RETRANSMIT_ERROR_COMMAND						0x81
#define SESSION_RESET_COMMAND							0x82
#define SESSION_RESET_REPLY_COMMAND						0x83
#define NAK_COMMAND										0x8F
#define BYE_COMMAND										0xF0
#define BYE_REPLY_COMMAND								0xF1
#define UMP_DATA_COMMAND								0xFF

//! NAK codes
#define NAK_REASON_RESERVED			0x00
#define NAK_REASON_NOT_SUPPORTED	0x01
#define NAK_REASON_NOT_EXPECTED		0x02
#define NAK_REASON_MALFORMED		0x03
#define NAK_BAD_PING_REPLY			0x20

//! Error correction modes
#define ERROR_CORRECTION_NONE		0
#define ERROR_CORRECTION_FEC		1

#define UMP_FIFO_SIZE	1024

typedef struct {
	uint32_t FIFO[UMP_FIFO_SIZE];
	unsigned int ReadPtr;
	unsigned int WritePtr;
} TUMP_FIFO;

//! Number of packets recorded in Forward Error Correction register
#define NUM_FEC_ENTRIES		5

//! Structure to store one full UMP command for FEC
typedef struct {
	bool Filled;
	unsigned int Size;			// Number of 32 bits word in the buffer
	uint32_t Packet[65];		// 64 UMP words plus header - Binary copy of a sent packet
} TFEC_REGISTER;

#pragma pack (push, 1)

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;	// 2..36
	uint8_t CSD1;			// Length in 32-bit words of UMP Endpoint Name
	uint8_t CSD2;			// Capabilities bitmap D0 : Client supports sending Invitation with Authentication, D1 : Client supports sending Invitation with User Authentication
	uint8_t EPName_PIID[MAX_UMP_ENDPOINT_NAME_LEN + MAX_UMP_PRODUCT_INSTANCE_ID_LEN];
} TUMP_INVITATION_PACKET;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;
	uint8_t CSD1;			// Length in 32-bit words of UMP Endpoint Name
	uint8_t CSD2;			// Reserved
	uint8_t EPName_PIID[MAX_UMP_ENDPOINT_NAME_LEN + MAX_UMP_PRODUCT_INSTANCE_ID_LEN];
} TUMP_INVITATION_ACCEPTED_PACKET;

//! The same packet is used for the Session Reset Reply
typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;
	uint16_t Reserved;			// Must be 0
} TUMP_SESSION_RESET_PACKET;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;		// Shall be 0 (no text used)
	uint8_t BYECode;
	uint8_t Reserved;
} TUMP_BYE_PACKET;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;
	uint16_t Reserved;		// Shall be 0
} TUMP_BYE_REPLY_PACKET;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;		// Shall be 1
	uint16_t Reserved;			// Shall be 0
	uint32_t ID;
}  TUMP_PING_PACKET;

typedef struct {
	uint8_t CommandCode;
	uint8_t PayloadLength;		// Shall be 1
	uint16_t Reserved;			// Shall be 0
	uint32_t ID;
}  TUMP_PING_PACKET_NO_SIGNATURE;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;		// Shall be 1
	uint16_t Reserved;			// Shall be 0
	uint32_t ID;
}  TUMP_PING_REPLY_PACKET;

typedef struct {
	uint32_t Signature;
	uint8_t CommandCode;
	uint8_t PayloadLength;		// Shall be 1 (no text used)
	uint8_t NAKReason;
	uint8_t Reserved;
	uint32_t NAKCommandHeader;
} TUMP_NAK_PACKET;

#pragma pack (pop)

class CNetUMPHandler
{
public:
	CNetUMPHandler (TUMPDataCallback CallbackFunc, void* UserInstance);
	~CNetUMPHandler (void);

	//! Record a session name. Shall be called before InitiateSession.
	//! Note that session name is mandatory, so Name shall not be empty. Length is limited to 98 bytes
	// Do not call on activated handler (must be called before InitiateSession is called)
	void SetEndpointName (char* Name);

	void SetProductInstanceID(char* PIID);

	//! Activate network resources and starts communication (tries to open session) with remote node
	// \return 0=session being initiated -1=can not create UDP socket
	int InitiateSession(unsigned int DestIP,
						unsigned short DestPort,
						unsigned short LocalPort,
						bool IsInitiator);

	//! Terminate active NetUMP session if it exists
	void CloseSession(void);

	//! Main processing function to call from high priority thread (audio or multimedia timer) every millisecond
	void RunSession(void);

	//! Restarts session process after it has been closed by a remote partner
	void RestartSessionInitiator (void);

	//! Returns the session status
	/*!
	0 : session is closed
	1 : inviting remote node
	3 : session opened (MIDI data can be exchanged)
	*/
	int GetSessionStatus (void);

	//! Returns true if remote device does not reply anymore to sync / keepalive messages
	//! The flag is reset after this method has been called (so the method returns true only one time when event has occured)
	bool ReadAndResetConnectionLost (void);

	//! Returns true if remote participant has sent a BYE to close the session
	//! The flag is reset after this method has been called (so the method returns true only one time)
	bool RemotePeerClosedSession (void);

	//! Put a next message to be sent in the transmission queue
	bool SendUMPMessage (uint32_t* UMPData);

	//! Select error correction method on transmit - 0 : no error correction (no FEC) / 1 : Forward Error Correction (add older packets before latest UMP data)
	void SelectErrorCorrectionMode (unsigned int CorrectionMethod);

	//! Declares callback and instance parameter for the callback
	void SetCallback(TUMPDataCallback CallbackFunc, void* UserInstance);

	//! Declares callback for connection event
	void SetConnectionCallback(void(*CallbackFunc)(const char* EndpointName, unsigned int size));

	//! Declares callback for disconnection event
	void SetDisconnectCallback(void(*CallbackFunc)());

private:
	// Callback data
	TUMPDataCallback UMPCallback;	// Callback for incoming RTP-MIDI message
	void* ClientInstance;
	TUMP_FIFO UMP_FIFO_TO_NET;
	TUMP_FIFO UMP_FIFO_FROM_NET;

	unsigned char EndpointName [MAX_UMP_ENDPOINT_NAME_LEN];
	unsigned char ProductInstanceID[MAX_UMP_PRODUCT_INSTANCE_ID_LEN];

	unsigned int RemoteIP;			// Address of remote partner to invite
	unsigned short RemoteUDPPort;	// Port number or remote partner to invite (0 if module is used as session listener)
	unsigned short LocalUDPPort;	// Local port number
	bool IsInitiatorNode;			// Handler will invite the remote device

	bool SocketLocked;				// Blocks access to socket from realtime thread if socket is being modified

	uint16_t UMPSequenceCounter;	// Incremented each time a UMP packet is sent
	uint16_t LastReceivedUMPCounter;	// Updated by incoming UMP packet (used to detect lost packet)
	unsigned int PINGDelayCounter;		// Millisecond counter to know how much time elapsed since the last transmitted packet
	unsigned int PINGIdCounter;		// To generate a new ID each time a PING is sent

	TSOCKTYPE UMPSocket;
	int SessionState;
	unsigned int SessionPartnerIP;              // IP address of session partner (only valid if session is opened)
	unsigned short SessionPartnerPort;			// Remote partner UDP port (0 if handler is used as a session listener)

	bool ConnectionLost;				// Set to 1 when connection is lost after a session has opened successfully
	bool PeerClosedSession;				// Set to 1 when we receive a BY message on a opened session

	unsigned int InviteCount;		// Number of invitation messages sent
	int TimeOutRemote;				// Counter to detect loss of remote node (reset when PING is received)

	bool TimerRunning;				// Event timer is running
	bool TimerEvent;				// Event is signalled
	unsigned int EventTime;		// System time to which event will be signalled
	unsigned int TimeCounter;		// Counter in 100us used for clock synchronization

	TFEC_REGISTER FECMemory[NUM_FEC_ENTRIES];		// Storage for the last send UMP Command Packets, used as round-robin
	unsigned int NextFECSlot;						// Pointer for the round-robin FEC
	unsigned int ErrorCorrectionMode;				// See ERROR_CORRECTION_XXX consts
	uint16_t ReceivedSequenceCounters[NUM_FEC_ENTRIES];				// List of the last received counters to detect incoming packet loss

	void(*ConnectionCallback)(const char* EndpointName, unsigned int size);
	void(*DisconnectCallback)();

	//! Release UDP sockets used by the handler
	void CloseSockets(void);

	//! Sends NetUMP invitation (simple invitation, no authentication)
	//! Invitation is sent to declared partner
	void SendInvitationCommand (void);

	//! Send Invitation Accepted (message is sent to the session partner, so we don't need IP parameters here
	void SendInvitationAcceptedCommand (void);

	//! Sends UMP session BYE message (IP parameters are needed as this message can be sent out of a session)
	void SendBYECommand (unsigned char BYEReason, unsigned int DestinationIP, unsigned short DestinationPort);

	//! Sends UMP session BYE reply (IP parameters are needed as this message can be sent out of a session)
	void SendBYEReplyCommand (unsigned int DestinationIP, unsigned short DestinationPort);

	//! Send UMP PING message
	void SendPINGCommand (uint32_t PINGId);

	//! Send UMP PING REPLY command
	void SendPINGReplyCommand (uint32_t PINGId);

	//! Arms timer (internal timer event will be activated after TimeToWait has elapsed
	//! \param TimeToWait number of milliseconds to wait after this method is called until event is signalled
	void PrepareTimerEvent (unsigned int TimeToWait);

	//! Prepare a UMP Command Block to be sent on network. The packet contains FEC if activated
	//! \return 0 if there is no new UMP data to send on the network
	unsigned int GenerateUMPCommand (uint32_t* UMPCommand);

	//! Process an incoming NetUMP packet from network
	void ProcessIncomingUMP (unsigned char* Buffer);

	//! Reset the Forward Error Correction memory
	void ResetFECMemory (void);
};

#endif  // __NETUMP_H__
