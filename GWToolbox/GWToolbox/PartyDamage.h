#pragma once

#include <queue>
#include <string>
#include <map>

#include <GWCA\GWCA.h>
#include <GWCA\Constants\Constants.h>
#include <GWCA\Packets\StoC.h>

#include <SimpleIni.h>

#include "Timer.h"
#include "GuiUtils.h"
#include "ToolboxModule.h"

class PartyDamage : public ToolboxModule {
	const char* inifilename = "healthlog.ini";
	const char* inisection = "health";

	static const int MAX_PLAYERS = 12;
	static const int RECENT_MAX_TIME = 7000;

	struct PlayerDamage {
		long damage = 0;
		long recent_damage;
		clock_t last_damage;
		long agent_id = 0;
		std::wstring name;
		GW::Constants::Profession primary = GW::Constants::Profession::None;
		GW::Constants::Profession secondary = GW::Constants::Profession::None;

		void Reset() {
			damage = 0;
			recent_damage = 0;
			agent_id = 0;
			name = L"";
			primary = GW::Constants::Profession::None;
			secondary = GW::Constants::Profession::None;
		}
	};

public:
	const char* Name() const override { return "Damage"; }

	PartyDamage();
	~PartyDamage();

	// Draw user interface. Will be called every frame if the element is visible
	void Draw(IDirect3DDevice9* pDevice) override;

	void Update() override;

	void LoadSettings(CSimpleIni* ini) override;
	void SaveSettings(CSimpleIni* ini) const override;
	void DrawSettings() override;

	void WritePartyDamage();
	void WriteDamageOf(int index, int rank = 0); // party index from 0 to 12
	void WriteOwnDamage();
	void ResetDamage();

private:
	bool DamagePacketCallback(GW::Packet::StoC::P151* packet);
	bool MapLoadedCallback(GW::Packet::StoC::P230* packet);

	void CreatePartyIndexMap();

	float GetPartOfTotal(long dmg) const;
	inline float GetPercentageOfTotal(long dmg) const
	{ return GetPartOfTotal(dmg) * 100.0f; };

	// damage values
	long total;
	PlayerDamage damage[MAX_PLAYERS];
	std::map<DWORD, long> hp_map;
	std::map<DWORD, int> party_index;

	// main routine variables
	bool in_explorable = false;
	clock_t send_timer;
	std::queue<std::wstring> send_queue;

	// ini
	CSimpleIni* inifile = nullptr;
};
