#include <wx/string.h>
#include <string>
#include "Smashladder.h"

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
