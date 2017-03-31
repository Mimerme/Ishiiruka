#include <wx/string.h>
#include <string>
#include "Smashladder.h"
#include "Core/ConfigManager.h"

wxString Smashladder::m_netplay_code = wxString("");
bool Smashladder::m_netplay = false;
bool Smashladder::m_netplay_host = false;
std::string Smashladder::m_netplay_client_string = "SmashLadder Dolphin";
bool Smashladder::initalized = true;
std::string Smashladder::expectedPlayerNames[3] = { "","","" };
int Smashladder::currentPlayerCount = 0;
bool Smashladder::spec = false;
long Smashladder::expectedPlayerCount = 2;
wxString Smashladder::playername = wxString("");

void Smashladder::playerJoinServer(std::string playername, NetPlayServer *server) {
	currentPlayerCount++;
	if (expectedPlayerCount == currentPlayerCount) {
		//Start netplay
		SConfig& instance = SConfig::GetInstance();
		NetSettings settings;
		settings.m_CPUthread = instance.bCPUThread;
		settings.m_CPUcore = instance.iCPUCore;
		settings.m_EnableCheats = instance.bEnableCheats;
		settings.m_SelectedLanguage = instance.SelectedLanguage;
		settings.m_OverrideGCLanguage = instance.bOverrideGCLanguage;
		settings.m_ProgressiveScan = instance.bProgressive;
		settings.m_PAL60 = instance.bPAL60;
		settings.m_DSPHLE = instance.bDSPHLE;
		settings.m_DSPEnableJIT = instance.m_DSPEnableJIT;
		settings.m_WriteToMemcard = false;
		settings.m_OCEnable = instance.m_OCEnable;
		settings.m_OCFactor = instance.m_OCFactor;
		settings.m_EXIDevice[0] = instance.m_EXIDevice[0];
		settings.m_EXIDevice[1] = instance.m_EXIDevice[1];

		//Start netplay
		server->SetNetSettings(settings);
		server->StartGame();
	}
}