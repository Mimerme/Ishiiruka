#include <iostream>
#include "..\Core\NetPlayServer.h"
#include "pch.h""

using namespace std;

int main() {
	cout << "Starting Netplay..." << endl;
	new NetPlayServer(0, true, "stun.dolphin-emu.org", 6262);
}