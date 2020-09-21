#include "stdafx.h"
#include "csgo_net_chan.h"

#include "WrpConsole.h"

#include "addresses.h"
#include <SourceInterfaces.h>
#include <csgo/bitbuf/demofilebitbuf.h>

#include <build/protobuf/csgo/netmessages.pb.h>

#include <shared/AfxDetours.h>

#include <Windows.h>
#include <deps/release/Detours/src/detours.h>

int g_i_MirvPov = 0;

struct csgo_bf_read {
	char const* m_pDebugName;
	bool m_bOverflow;
	int m_nDataBits;
	size_t m_nDataBytes;
	unsigned int m_nInBufWord;
	int m_nBitsAvail;
	unsigned int const* m_pDataIn;
	unsigned int const* m_pBufferEnd;
	unsigned int const* m_pData;
};


typedef const SOURCESDK::QAngle& (__fastcall *csgo_C_CSPlayer_EyeAngles_t)(SOURCESDK::C_BaseEntity_csgo* This, void* Edx);
csgo_C_CSPlayer_EyeAngles_t Truecsgo_C_CSPlayer_EyeAngles;

const SOURCESDK::QAngle& __fastcall Mycsgo_C_CSPlayer_EyeAngles(SOURCESDK::C_BaseEntity_csgo * This, void * Edx)
{
	if (g_i_MirvPov)
	{
		if (This->entindex() == g_i_MirvPov)
		{
			DWORD ofs = AFXADDR_GET(csgo_C_CSPlayer_ofs_m_angEyeAngles);
			return *((SOURCESDK::QAngle*)((char *)This + ofs));
		}
	}

	return Truecsgo_C_CSPlayer_EyeAngles(This, Edx);
}

typedef void csgo_CNetChan_t;
typedef int(__fastcall* csgo_CNetChan_ProcessMessages_t)(csgo_CNetChan_t* This, void* edx, csgo_bf_read * pReadBuf, bool bWasReliable);
csgo_CNetChan_ProcessMessages_t Truecsgo_CNetChan_ProcessMessages = 0;

int __fastcall Mycsgo_CNetChan_ProcessMessages(csgo_CNetChan_t* This, void* Edx, csgo_bf_read* pReadBuf, bool bWasReliable)
{
	if (g_i_MirvPov)
	{
		SOURCESDK::CSGO::CBitRead readBuf(pReadBuf->m_pData, pReadBuf->m_nDataBytes);

		while (0 < readBuf.GetNumBytesLeft())
		{
			int packet_cmd = readBuf.ReadVarInt32();
			int packet_size = readBuf.ReadVarInt32();

			if (packet_size < readBuf.GetNumBytesLeft())
			{
				switch (packet_cmd)
				{
				case svc_ServerInfo:
					{
						CSVCMsg_ServerInfo msg;
						msg.ParseFromArray(readBuf.GetBasePointer() + readBuf.GetNumBytesRead(), packet_size);
						if (msg.has_is_hltv())
						{
							msg.set_is_hltv(false);

							WrpConVarRef cvarClPredict("cl_predict"); // GOTV would have this on 0, so force it too.
							cvarClPredict.SetDirectHack(0);
						}
						if (msg.has_player_slot())
						{
							msg.set_player_slot(g_i_MirvPov - 1);
						}
						msg.SerializePartialToArray(const_cast<unsigned char*>(readBuf.GetBasePointer()) + readBuf.GetNumBytesRead(), packet_size);
					}
					break;
				case svc_SetView:
					{
						CSVCMsg_SetView msg;
						msg.ParseFromArray(readBuf.GetBasePointer() + readBuf.GetNumBytesRead(), packet_size);
						if (msg.has_entity_index())
						{
							msg.set_entity_index(g_i_MirvPov);
						}
						msg.SerializePartialToArray(const_cast<unsigned char*>(readBuf.GetBasePointer()) + readBuf.GetNumBytesRead(), packet_size);
					}
					break;
				}

				readBuf.SeekRelative(packet_size * 8);
			}
			else
				break;
		}
	}

	return Truecsgo_CNetChan_ProcessMessages(This, Edx, pReadBuf, bWasReliable);
}

bool csgo_CNetChan_ProcessMessages_Install(void)
{
	static bool firstResult = false;
	static bool firstRun = true;
	if (!firstRun) return firstResult;
	firstRun = false;

	if (AFXADDR_GET(csgo_CNetChan_ProcessMessages))
	{
		LONG error = NO_ERROR;

		Truecsgo_CNetChan_ProcessMessages = (csgo_CNetChan_ProcessMessages_t)AFXADDR_GET(csgo_CNetChan_ProcessMessages);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)Truecsgo_CNetChan_ProcessMessages, Mycsgo_CNetChan_ProcessMessages);
		error = DetourTransactionCommit();

		firstResult = NO_ERROR == error;
	}

	return firstResult;
}


bool csgo_C_CSPlayer_EyeAngles_Install(void)
{
	static bool firstResult = false;
	static bool firstRun = true;
	if (!firstRun) return firstResult;
	firstRun = false;

	if (AFXADDR_GET(csgo_C_CSPlayer_vtable))
	{
		AfxDetourPtr((PVOID*)&(((DWORD*)AFXADDR_GET(csgo_C_CSPlayer_vtable))[169]), Mycsgo_C_CSPlayer_EyeAngles, (PVOID*)&Truecsgo_C_CSPlayer_EyeAngles);

		firstResult = true;
	}

	return firstResult;
}

extern bool csgo_C_CSPlayer_UpdateClientSideAnimation_Install(void);

CON_COMMAND(mirv_pov, "Forces a POV on a GOTV demo.")
{
	if (!(AFXADDR_GET(csgo_C_CSPlayer_ofs_m_angEyeAngles) && csgo_CNetChan_ProcessMessages_Install() && csgo_C_CSPlayer_EyeAngles_Install() /*&& csgo_C_CSPlayer_UpdateClientSideAnimation_Install()*/))
	{
		Tier0_Warning("Not supported for your engine / missing hooks,!\n");
		return;
	}

	int argC = args->ArgC();

	if (2 <= argC)
	{
		g_i_MirvPov = atoi(args->ArgV(1));
		return;
	}

	Tier0_Msg(
		"mirv_pov <iPlayerEntityIndex> - Needs to be set before loading demo / connecting! Forces POV on a GOTV to the given player entity index, set 0 to disable.\n"
		"Current value: %i\n"
		, g_i_MirvPov
	);
}