/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */
//#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include <iclient.h>
#include <iserver.h>
#include <ISDKTools.h>

#include "extension.h"

ConVar g_SmVoiceAddr("sm_voice_addr", "127.0.0.1", FCVAR_PROTECTED, "Voice server listen ip address.");
ConVar g_SmVoicePort("sm_voice_port", "27020", FCVAR_PROTECTED, "Voice server listen port.", true, 1025.0, true, 65535.0);

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

template <typename T> inline T min(T a, T b) { return a<b?a:b; }

CVoice g_Interface;
SMEXT_LINK(&g_Interface);

ISDKTools *g_pSDKTools = NULL;
IServer *iserver = NULL;
SH_DECL_MANUALHOOK0(GetPlayerSlot, 0, 0, 0, int); // IClient::GetPlayerSlot

double getTime()
{
    struct timespec tv;
    if(clock_gettime(CLOCK_REALTIME, &tv) != 0)
    	return 0;

    return (tv.tv_sec + (tv.tv_nsec / 1000000000.0));
}

void OnGameFrame(bool simulating)
{
	g_Interface.OnGameFrame(simulating);
}

CVoice::CVoice()
{
	m_ListenSocket = -1;

	m_PollFds = 0;
	for(int i = 1; i < 1 + MAX_CLIENTS; i++)
		m_aPollFds[i].fd = -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aClients[i].m_Socket = -1;

	m_AvailableTime = 0.0;

	m_pMode = NULL;
	m_pCodec = NULL;

	m_SV_BroadcastVoiceData = NULL;
}

bool CVoice::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	// Setup engine-specific data.
	Dl_info info;
	void *engineFactory = (void *)g_SMAPI->GetEngineFactory(false);
	if(dladdr(engineFactory, &info) == 0)
	{
		g_SMAPI->Format(error, maxlength, "dladdr(engineFactory) failed.");
		return false;
	}

	void *pEngineSo = dlopen(info.dli_fname, RTLD_NOW);
	if(pEngineSo == NULL)
	{
		g_SMAPI->Format(error, maxlength, "dlopen(%s) failed.", info.dli_fname);
		return false;
	}

	int engineVersion = g_SMAPI->GetSourceEngineBuild();
	int offsPlayerSlot = 0;

	switch (engineVersion)
	{
		case SOURCE_ENGINE_CSGO:
#ifdef _WIN32
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x81\xEC\xD0\x00\x00\x00\x53\x56\x57", 12);
			offsPlayerSlot = 15;
#else
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
			offsPlayerSlot = 16;
#endif
			break;

		case SOURCE_ENGINE_LEFT4DEAD2:
#ifdef _WIN32
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x83\xEC\x70\xA1\x2A\x2A\x2A\x2A\x33\xC5\x89\x45\xFC\xA1\x2A\x2A\x2A\x2A\x53\x56", 23);
			offsPlayerSlot = 14;
#else
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
			offsPlayerSlot = 15;
#endif
			break;

		case SOURCE_ENGINE_NUCLEARDAWN:
#ifdef _WIN32
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\xA1\x2A\x2A\x2A\x2A\x83\xEC\x58\x57\x33\xFF", 14);
			offsPlayerSlot = 14;
#else
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
			offsPlayerSlot = 15;
#endif
			break;

		case SOURCE_ENGINE_INSURGENCY:
#ifdef _WIN32
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\x83\xEC\x74\x68\x2A\x2A\x2A\x2A\x8D\x4D\xE4\xE8", 15);
			offsPlayerSlot = 14;
#else
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
			offsPlayerSlot = 15;
#endif
			break;

		case SOURCE_ENGINE_TF2:
		case SOURCE_ENGINE_CSS:
		case SOURCE_ENGINE_HL2DM:
		case SOURCE_ENGINE_DODS:
		case SOURCE_ENGINE_SDK2013:
#ifdef _WIN32
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->FindPattern(pEngineSo, "\x55\x8B\xEC\xA1\x2A\x2A\x2A\x2A\x83\xEC\x50\x83\x78\x30", 14);
			offsPlayerSlot = 14;
#else
			m_SV_BroadcastVoiceData = (t_SV_BroadcastVoiceData)memutils->ResolveSymbol(pEngineSo, "_Z21SV_BroadcastVoiceDataP7IClientiPcx");
			offsPlayerSlot = 15;
#endif
			break;

		default:
			g_SMAPI->Format(error, maxlength, "Unsupported game.");
			dlclose(pEngineSo);
			return false;
	}
	dlclose(pEngineSo);

	if(!m_SV_BroadcastVoiceData)
	{
		g_SMAPI->Format(error, maxlength, "SV_BroadcastVoiceData sigscan failed.");
		return false;
	}

	SH_MANUALHOOK_RECONFIGURE(GetPlayerSlot, offsPlayerSlot, 0, 0);

	// Init tcp server
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(m_ListenSocket < 0)
	{
		g_SMAPI->Format(error, maxlength, "Failed creating socket.");
		SDK_OnUnload();
		return false;
	}

	int yes = 1;
	if(setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
	{
		g_SMAPI->Format(error, maxlength, "Failed setting SO_REUSEADDR on socket.");
		SDK_OnUnload();
		return false;
	}

	engine->ServerCommand("exec sourcemod/extension.Voice.cfg\n");
	engine->ServerExecute();

	sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	inet_aton(g_SmVoiceAddr.GetString(), &bindAddr.sin_addr);
	bindAddr.sin_port = htons(g_SmVoicePort.GetInt());

	smutils->LogMessage(myself, "Binding to %s:%d!\n", g_SmVoiceAddr.GetString(), g_SmVoicePort.GetInt());

	if(bind(m_ListenSocket, (sockaddr *)&bindAddr, sizeof(sockaddr_in)) < 0)
	{
		g_SMAPI->Format(error, maxlength, "Failed binding to socket (%d '%s').", errno, strerror(errno));
		SDK_OnUnload();
		return false;
	}

	if(listen(m_ListenSocket, MAX_CLIENTS) < 0)
	{
		g_SMAPI->Format(error, maxlength, "Failed listening on socket.");
		SDK_OnUnload();
		return false;
	}

	m_aPollFds[0].fd = m_ListenSocket;
	m_aPollFds[0].events = POLLIN;
	m_PollFds++;

	// Encoder settings
	m_EncoderSettings.SampleRate_Hz = 22050;
	m_EncoderSettings.TargetBitRate_Kbps = 64;
	m_EncoderSettings.FrameSize = 512; // samples
	m_EncoderSettings.PacketSize = 64;
	m_EncoderSettings.Complexity = 10; // 0 - 10
	m_EncoderSettings.FrameTime = (double)m_EncoderSettings.FrameSize / (double)m_EncoderSettings.SampleRate_Hz;

	// Init CELT encoder
	int theError;
	m_pMode = celt_mode_create(m_EncoderSettings.SampleRate_Hz, m_EncoderSettings.FrameSize, &theError);
	if(!m_pMode)
	{
		g_SMAPI->Format(error, maxlength, "celt_mode_create error: %d", theError);
		SDK_OnUnload();
		return false;
	}

	m_pCodec = celt_encoder_create_custom(m_pMode, 1, &theError);
	if(!m_pCodec)
	{
		g_SMAPI->Format(error, maxlength, "celt_encoder_create_custom error: %d", theError);
		SDK_OnUnload();
		return false;
	}

	celt_encoder_ctl(m_pCodec, CELT_RESET_STATE_REQUEST, NULL);
	celt_encoder_ctl(m_pCodec, CELT_SET_BITRATE(m_EncoderSettings.TargetBitRate_Kbps * 1000));
	celt_encoder_ctl(m_pCodec, CELT_SET_COMPLEXITY(m_EncoderSettings.Complexity));

	smutils->AddGameFrameHook(::OnGameFrame);

	return true;
}

bool CVoice::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	ConVar_Register(0, this);

	return true;
}

bool CVoice::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
	return META_REGCVAR(pVar);
}

void CVoice::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	if(g_pSDKTools == NULL)
		smutils->LogError(myself, "SDKTools interface not found");

	iserver = g_pSDKTools->GetIServer();
	if(iserver == NULL)
		smutils->LogError(myself, "Failed to get IServer interface from SDKTools!");
}

void CVoice::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(::OnGameFrame);

	if(m_ListenSocket != -1)
	{
		close(m_ListenSocket);
		m_ListenSocket = -1;
	}

	for(int Client = 0; Client < MAX_CLIENTS; Client++)
	{
		if(m_aClients[Client].m_Socket != -1)
		{
			close(m_aClients[Client].m_Socket);
			m_aClients[Client].m_Socket = -1;
		}
	}

	if(m_pCodec)
		celt_encoder_destroy(m_pCodec);

	if(m_pMode)
		celt_mode_destroy(m_pMode);
}

void CVoice::OnGameFrame(bool simulating)
{
	HandleNetwork();
	HandleVoiceData();
}

void CVoice::HandleNetwork()
{
	if(m_ListenSocket == -1)
		return;

	int PollRes = poll(m_aPollFds, m_PollFds, 0);
	if(PollRes <= 0)
		return;

	// Accept new clients
	if(m_aPollFds[0].revents & POLLIN)
	{
		// Find slot
		int Client;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == -1)
				break;
		}

		// no free slot
		if(Client != MAX_CLIENTS)
		{
			sockaddr_in addr;
			size_t size = sizeof(sockaddr_in);
			int Socket = accept(m_ListenSocket, (sockaddr *)&addr, &size);

			m_aClients[Client].m_Socket = Socket;
			m_aClients[Client].m_BufferWriteIndex = 0;
			m_aClients[Client].m_LastLength = 0;
			m_aClients[Client].m_LastValidData = 0.0;
			m_aClients[Client].m_New = true;

			m_aPollFds[m_PollFds].fd = Socket;
			m_aPollFds[m_PollFds].events = POLLIN | POLLHUP;
			m_aPollFds[m_PollFds].revents = 0;
			m_PollFds++;

			smutils->LogMessage(myself, "Client %d connected!\n", Client);
		}
	}

	bool CompressPollFds = false;
	for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
	{
		int Client = -1;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == m_aPollFds[PollFds].fd)
				break;
		}
		if(Client == -1)
			continue;

		CClient *pClient = &m_aClients[Client];

		// Connection shutdown prematurely ^C
		// Make sure to set SO_LINGER l_onoff = 1, l_linger = 0
		if(m_aPollFds[PollFds].revents & POLLHUP)
		{
			close(pClient->m_Socket);
			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
			smutils->LogMessage(myself, "Client %d disconnected!(2)\n", Client);
			continue;
		}

		// Data available?
		if(!(m_aPollFds[PollFds].revents & POLLIN))
			continue;

		size_t BytesAvailable;
		if(ioctl(pClient->m_Socket, FIONREAD, &BytesAvailable) == -1)
			continue;

		if(pClient->m_New)
		{
			pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
			pClient->m_New = false;
		}

		m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

		// Don't recv() when we can't fit data into the ringbuffer
		unsigned char aBuf[32768];
		if(min(BytesAvailable, sizeof(aBuf)) > m_Buffer.CurrentFree() * sizeof(int16_t))
			continue;

		ssize_t Bytes = recv(pClient->m_Socket, aBuf, sizeof(aBuf), 0);

		if(Bytes <= 0)
		{
			close(pClient->m_Socket);
			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
			smutils->LogMessage(myself, "Client %d disconnected!(1)\n", Client);
			continue;
		}

		// Got data!
		OnDataReceived(pClient, (int16_t *)aBuf, Bytes / sizeof(int16_t));

		pClient->m_LastLength = m_Buffer.CurrentLength();
		pClient->m_BufferWriteIndex = m_Buffer.GetWriteIndex();
	}

	if(CompressPollFds)
	{
		for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
		{
			if(m_aPollFds[PollFds].fd != -1)
				continue;

			for(int PollFds_ = PollFds; PollFds_ < 1 + MAX_CLIENTS; PollFds_++)
				m_aPollFds[PollFds_].fd = m_aPollFds[PollFds_ + 1].fd;

			PollFds--;
			m_PollFds--;
		}
	}
}

void CVoice::OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples)
{
	// Check for empty input
	ssize_t DataStartsAt = -1;
	for(size_t i = 0; i < Samples; i++)
	{
		if(pData[i] == 0)
			continue;

		DataStartsAt = i;
		break;
	}

	// Discard empty data if last vaild data was more than a second ago.
	if(pClient->m_LastValidData + 1.0 < getTime())
	{
		// All empty
		if(DataStartsAt == -1)
			return;

		// Data starts here
		pData += DataStartsAt;
		Samples -= DataStartsAt;
	}

	if(!m_Buffer.Push(pData, Samples))
	{
		smutils->LogError(myself, "Buffer push failed!!! Samples: %u, Free: %u\n", Samples, m_Buffer.CurrentFree());
		return;
	}

	pClient->m_LastValidData = getTime();
}

void CVoice::HandleVoiceData()
{
	int SamplesPerFrame = m_EncoderSettings.FrameSize;
	int PacketSize = m_EncoderSettings.PacketSize;
	int FramesAvailable = m_Buffer.TotalLength() / SamplesPerFrame;
	float TimeAvailable = (float)m_Buffer.TotalLength() / (float)m_EncoderSettings.SampleRate_Hz;

	if(!FramesAvailable)
		return;

	// Before starting playback we want at least 100ms in the buffer
	if(m_AvailableTime < getTime() && TimeAvailable < 0.1)
		return;

	// let the clients have no more than 500ms
	if(m_AvailableTime > getTime() + 0.5)
		return;

	// 5 = max frames per packet
	FramesAvailable = min(FramesAvailable, 5);

	// 0 = SourceTV
	IClient *pClient = iserver->GetClient(0);
	if(!pClient)
		return;

	for(int Frame = 0; Frame < FramesAvailable; Frame++)
	{
		// Get data into buffer from ringbuffer.
		int16_t aBuffer[SamplesPerFrame];

		size_t OldReadIdx = m_Buffer.m_ReadIndex;
		size_t OldCurLength = m_Buffer.CurrentLength();
		size_t OldTotalLength = m_Buffer.TotalLength();

		if(!m_Buffer.Pop(aBuffer, SamplesPerFrame))
		{
			printf("Buffer pop failed!!! Samples: %u, Length: %u\n", SamplesPerFrame, m_Buffer.TotalLength());
			return;
		}

		// Encode it!
		unsigned char aFinal[PacketSize];
		size_t FinalSize = 0;

		FinalSize = celt_encode(m_pCodec, aBuffer, SamplesPerFrame, aFinal, sizeof(aFinal));

		if(FinalSize <= 0)
		{
			smutils->LogError(myself, "Compress returned %d\n", FinalSize);
			return;
		}

		// Check for buffer underruns
		for(int Client = 0; Client < MAX_CLIENTS; Client++)
		{
			CClient *pClient = &m_aClients[Client];
			if(pClient->m_Socket == -1 || pClient->m_New == true)
				continue;

			m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

			if(m_Buffer.CurrentLength() > pClient->m_LastLength)
			{
				pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
				m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);
				pClient->m_LastLength = m_Buffer.CurrentLength();
			}
		}

		SV_BroadcastVoiceData(pClient, FinalSize, aFinal);
	}

	if(m_AvailableTime < getTime())
		m_AvailableTime = getTime();

	m_AvailableTime += (double)FramesAvailable * m_EncoderSettings.FrameTime;
}

void CVoice::SV_BroadcastVoiceData(IClient *pClient, int nBytes, unsigned char *pData)
{
	m_SV_BroadcastVoiceData(pClient, nBytes, pData, 0);
}
