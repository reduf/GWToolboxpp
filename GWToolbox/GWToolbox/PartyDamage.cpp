#include "PartyDamage.h"

#include <sstream>

#include <GWCA\GWCA.h>
#include <GWCA\Managers\ChatMgr.h>
#include <GWCA\Managers\StoCMgr.h>
#include <GWCA\Managers\PartyMgr.h>

#include "GuiUtils.h"
#include "Config.h"

PartyDamage::PartyDamage() {
	total = 0;
	send_timer = TBTimer::init();

	GW::StoC().AddGameServerEvent<GW::Packet::StoC::P151>(
		std::bind(&PartyDamage::DamagePacketCallback, this, std::placeholders::_1));

	GW::StoC().AddGameServerEvent<GW::Packet::StoC::P230>(
		std::bind(&PartyDamage::MapLoadedCallback, this, std::placeholders::_1));

	for (int i = 0; i < MAX_PLAYERS; ++i) {
		damage[i].damage= 0;
		damage[i].recent_damage = 0;
		damage[i].last_damage = TBTimer::init();
	}
}

PartyDamage::~PartyDamage() {
	inifile->Reset();
	delete inifile;
}

bool PartyDamage::MapLoadedCallback(GW::Packet::StoC::P230* packet) {
	switch (GW::Map().GetInstanceType()) {
	case GW::Constants::InstanceType::Outpost:
		in_explorable = false;
		break;
	case GW::Constants::InstanceType::Explorable:
		party_index.clear();
		if (!in_explorable) {
			in_explorable = true;
			ResetDamage();
		}
		break;
	case GW::Constants::InstanceType::Loading:
	default:
		break;
	}
	return false;
}

bool PartyDamage::DamagePacketCallback(GW::Packet::StoC::P151* packet) {

	// ignore non-damage packets
	switch (packet->type) {
	case GW::Packet::StoC::P151_Type::damage:
	case GW::Packet::StoC::P151_Type::critical:
	case GW::Packet::StoC::P151_Type::armorignoring:
		break;
	default:
		return false;
	}

	// ignore heals
	if (packet->value >= 0) return false;

	GW::AgentArray agents = GW::Agents().GetAgentArray();

	// get cause agent
	GW::Agent* cause = agents[packet->cause_id];
	
	if (cause == nullptr) return false;
	if (cause->Allegiance != 0x1) return false;
	auto cause_it = party_index.find(cause->Id);
	if (cause_it == party_index.end()) return false;  // ignore damage done by non-party members

	// get target agent
	GW::Agent* target = agents[packet->target_id];
	if (target == nullptr) return false;
	if (target->LoginNumber != 0) return false; // ignore player-inflicted damage
										        // such as Life bond or sacrifice
	if (target->Allegiance == 0x1) return false; // ignore damage inflicted to allies in general
	// warning: note damage to allied spirits, minions or stones may still trigger
	// you can do damage like that by standing in bugged dart traps in eye of the north
	// or maybe with some skills that damage minions/spirits

	long dmg;
	if (target->MaxHP > 0 && target->MaxHP < 100000) {
		dmg = std::lround(-packet->value * target->MaxHP);
		hp_map[target->PlayerNumber] = target->MaxHP;
	} else {
		auto it = hp_map.find(target->PlayerNumber);
		if (it == hp_map.end()) {
			// max hp not found, approximate with hp/lvl formula
			dmg = std::lround(-packet->value * (target->Level * 20 + 100));
		} else {
			long maxhp = it->second;
			dmg = std::lround(-packet->value * it->second);
		}
	}

	int index = cause_it->second;
	if (index >= MAX_PLAYERS) return false; // something went very wrong.
	if (damage[index].damage == 0) {
		damage[index].agent_id = packet->cause_id;
		if (cause->LoginNumber > 0) {
			damage[index].name = GW::Agents().GetPlayerNameByLoginNumber(cause->LoginNumber);
		} else {
			damage[index].name = L"<A Hero>";
		}
		damage[index].primary = static_cast<GW::Constants::Profession>(cause->Primary);
		damage[index].secondary = static_cast<GW::Constants::Profession>(cause->Secondary);
	}

	damage[index].damage += dmg;
	total += dmg;

	if (visible) {
		damage[index].recent_damage += dmg;
		damage[index].last_damage = TBTimer::init();
	}
	return false;
}

void PartyDamage::Update() {
	if (!send_queue.empty() && TBTimer::diff(send_timer) > 600) {
		send_timer = TBTimer::init();
		if (GW::Map().GetInstanceType() != GW::Constants::InstanceType::Loading
			&& GW::Agents().GetPlayer()) {
			GW::Chat().SendChat(send_queue.front().c_str(), L'#');
			send_queue.pop();
		}
	}

	if (party_index.empty()) {
		CreatePartyIndexMap();
	}

	// reset recent if needed
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (TBTimer::diff(damage[i].last_damage) > RECENT_MAX_TIME) {
			damage[i].recent_damage = 0;
		}
	}
}

void PartyDamage::CreatePartyIndexMap() {
	if (!GW::Partymgr().GetIsPartyLoaded()) return;
	
	GW::PartyInfo* info = GW::Partymgr().GetPartyInfo();
	if (info == nullptr) return;

	GW::PlayerArray players = GW::Agents().GetPlayerArray();
	if (!players.valid()) return;

	int index = 0;
	for (GW::PlayerPartyMember player : info->players) {
		long id = players[player.loginnumber].AgentID;
		party_index[id] = index++;

		for (GW::HeroPartyMember hero : info->heroes) {
			if (hero.ownerplayerid == player.loginnumber) {
				party_index[hero.agentid] = index++;
			}
		}
	}
}

void PartyDamage::Draw(IDirect3DDevice9* device) {
	if (!visible) return;
	
	int line_height = GuiUtils::GetPartyHealthbarHeight();

	int size = GW::Partymgr().GetPartySize();
	if (size > MAX_PLAYERS) size = MAX_PLAYERS;

	long max_recent = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (max_recent < damage[i].recent_damage) {
			max_recent = damage[i].recent_damage;
		}
	}

	long max = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (max < damage[i].damage) {
			max = damage[i].damage;
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.3f));
	ImGui::Begin(Name(), &visible, ImGuiWindowFlags_NoTitleBar);
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[GuiUtils::FontSize::f10]); // should be 9
	float x = ImGui::GetWindowPos().x;
	float y = ImGui::GetWindowPos().y;
	float width = ImGui::GetWindowWidth();
	const int BUF_SIZE = 16;
	char buf[BUF_SIZE];
	for (int i = 0; i < size; ++i) {
		float part_of_max = 0;
		if (max > 0) {
			part_of_max = (float)(damage[i].damage) / max;
		}
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImVec2(x + width * (1.0f - part_of_max), y + i * line_height),
			ImVec2(x + width, y + (i + 1) * line_height),
			IM_COL32(205, 102, 51, 102));

		float part_of_recent = 0;
		if (max_recent > 0) {
			part_of_recent = (float)(damage[i].recent_damage) / max_recent;
		}
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImVec2(x + width * (1.0f - part_of_recent), y + (i + 1) * line_height - 6),
			ImVec2(x + width, y + (i + 1) * line_height),
			IM_COL32(102, 153, 230, 205));

		if (damage[i].damage < 1000) {
			sprintf_s(buf, BUF_SIZE, "%d", damage[i].damage);
		} else if (damage[i].damage < 1000 * 10) {
			sprintf_s(buf, BUF_SIZE, "%.2f k", (float)damage[i].damage / 1000);
		} else if (damage[i].damage < 1000 * 1000) {
			sprintf_s(buf, BUF_SIZE, "%.1f k", (float)damage[i].damage / 1000);
		} else {
			sprintf_s(buf, BUF_SIZE, "%.2f m", (float)damage[i].damage / (1000 * 1000));
		}
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(x + ImGui::GetStyle().WindowPadding.x, y + i * line_height), 
			IM_COL32(255, 255, 255, 255), buf);

		float perc_of_total = GetPercentageOfTotal(damage[i].damage);
		sprintf_s(buf, BUF_SIZE, "%.1f %%", perc_of_total);
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(x + width / 2, y + i * line_height),
			IM_COL32(255, 255, 255, 255), buf);
	}
	ImGui::PopFont();
	ImGui::End();
	ImGui::PopStyleColor(); // window bg
	ImGui::PopStyleVar(2);
}

float PartyDamage::GetPartOfTotal(long dmg) const {
	if (total == 0) return 0;
	return (float)dmg / total;
}

void PartyDamage::WritePartyDamage() {
	std::vector<size_t> idx(MAX_PLAYERS);
	for (size_t i = 0; i < MAX_PLAYERS; ++i) idx[i] = i;
	sort(idx.begin(), idx.end(), [this](size_t i1, size_t i2) {
		return damage[i1].damage > damage[i2].damage;
	});

	for (size_t i = 0; i < idx.size(); ++i) {
		WriteDamageOf(idx[i], i + 1);
	}
	send_queue.push(L"Total ~ 100 % ~ " + std::to_wstring(total));
}

void PartyDamage::WriteDamageOf(int index, int rank) {
	if (index >= MAX_PLAYERS) return;
	if (index < 0) return;
	if (damage[index].damage <= 0) return;

	if (rank == 0) {
		rank = 1; // start at 1, add 1 for each player with higher damage
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			if (i == index) continue;
			if (damage[i].agent_id == 0) continue;
			if (damage[i].damage > damage[index].damage) ++rank;
		}
	}

	const int size = 130;
	wchar_t buff[size];
	swprintf_s(buff, size, L"#%2d ~ %3.2f %% ~ %ls/%ls %ls ~ %d",
		rank,
		GetPercentageOfTotal(damage[index].damage),
		GW::Constants::GetWProfessionAcronym(damage[index].primary).c_str(),
		GW::Constants::GetWProfessionAcronym(damage[index].secondary).c_str(),
		damage[index].name.c_str(),
		damage[index].damage);

	send_queue.push(buff);
}


void PartyDamage::WriteOwnDamage() {
	GW::Agent* me = GW::Agents().GetPlayer();
	if (me == nullptr) return;

	auto cause_it = party_index.find(me->Id);
	if (cause_it == party_index.end()) return;

	int index = cause_it->second;
	WriteDamageOf(index);
}

void PartyDamage::ResetDamage() {
	total = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		damage[i].Reset();
	}
}

void PartyDamage::LoadSettings(CSimpleIni* ini) {
	ToolboxModule::LoadSettingVisible(ini);
	if (inifile == nullptr) inifile = new CSimpleIni(false, false, false);
	inifile->LoadFile(GuiUtils::getPath(inifilename).c_str());
	CSimpleIni::TNamesDepend keys;
	inifile->GetAllKeys(inisection, keys);
	for (CSimpleIni::Entry key : keys) {
		try {
			long lkey = std::stol(key.pItem);
			if (lkey <= 0) continue;
			long lval = inifile->GetLongValue(inisection, key.pItem, 0);
			if (lval <= 0) continue;
			hp_map[lkey] = lval;
		} catch (...) {
			// no big deal
		}
	}
}

void PartyDamage::SaveSettings(CSimpleIni* ini) const {
	ToolboxModule::SaveSettingVisible(ini);
	for (const std::pair<DWORD, long>& item : hp_map) {
		std::string key = std::to_string(item.first);
		inifile->SetLongValue(inisection, key.c_str(), item.second, 0, false, true);
	}
	inifile->SaveFile(GuiUtils::getPath(inifilename).c_str());
}

void PartyDamage::DrawSettings() {

}
