/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "pch.hpp"

#include "creatures/combat/spells.hpp"
#include "creatures/monsters/monster.hpp"
#include "creatures/npcs/npc.hpp"
#include "creatures/players/imbuements/imbuements.hpp"
#include "creatures/players/player.hpp"
#include "creatures/players/grouping/guild.hpp"
#include "game/zones/zone.hpp"
#include "game/game.hpp"
#include "game/movement/teleport.hpp"
#include "items/weapons/weapons.hpp"
#include "lua/functions/core/core_functions.hpp"
#include "lua/functions/creatures/creature_functions.hpp"
#include "lua/functions/events/events_functions.hpp"
#include "lua/functions/items/item_functions.hpp"
#include "lua/functions/lua_functions_loader.hpp"
#include "lua/functions/map/map_functions.hpp"
#include "lua/functions/core/game/zone_functions.hpp"

class LuaScriptInterface;

void LuaFunctionsLoader::load(lua_State* L) {
	if (!L) {
		g_game().dieSafely("Invalid lua state, cannot load lua functions.");
	}

	luaL_openlibs(L);

	CoreFunctions::init(L);
	CreatureFunctions::init(L);
	EventFunctions::init(L);
	ItemFunctions::init(L);
	MapFunctions::init(L);
	ZoneFunctions::init(L);
}

std::string LuaFunctionsLoader::getErrorDesc(ErrorCode_t code) {
	switch (code) {
		case LUA_ERROR_PLAYER_NOT_FOUND:
			return "Player not found";
		case LUA_ERROR_CREATURE_NOT_FOUND:
			return "Creature not found";
		case LUA_ERROR_NPC_NOT_FOUND:
			return "Npc not found";
		case LUA_ERROR_NPC_TYPE_NOT_FOUND:
			return "Npc type not found";
		case LUA_ERROR_MONSTER_NOT_FOUND:
			return "Monster not found";
		case LUA_ERROR_MONSTER_TYPE_NOT_FOUND:
			return "Monster type not found";
		case LUA_ERROR_ITEM_NOT_FOUND:
			return "Item not found";
		case LUA_ERROR_THING_NOT_FOUND:
			return "Thing not found";
		case LUA_ERROR_TILE_NOT_FOUND:
			return "Tile not found";
		case LUA_ERROR_HOUSE_NOT_FOUND:
			return "House not found";
		case LUA_ERROR_COMBAT_NOT_FOUND:
			return "Combat not found";
		case LUA_ERROR_CONDITION_NOT_FOUND:
			return "Condition not found";
		case LUA_ERROR_AREA_NOT_FOUND:
			return "Area not found";
		case LUA_ERROR_CONTAINER_NOT_FOUND:
			return "Container not found";
		case LUA_ERROR_VARIANT_NOT_FOUND:
			return "Variant not found";
		case LUA_ERROR_VARIANT_UNKNOWN:
			return "Unknown variant type";
		case LUA_ERROR_SPELL_NOT_FOUND:
			return "Spell not found";
		case LUA_ERROR_ACTION_NOT_FOUND:
			return "Action not found";
		case LUA_ERROR_TALK_ACTION_NOT_FOUND:
			return "TalkAction not found";
		case LUA_ERROR_ZONE_NOT_FOUND:
			return "Zone not found";
		default:
			return "Bad error code";
	}
}

int LuaFunctionsLoader::protectedCall(lua_State* L, int nargs, int nresults) {
	int error_index = lua_gettop(L) - nargs;
	lua_pushcfunction(L, luaErrorHandler);
	lua_insert(L, error_index);

	int ret = lua_pcall(L, nargs, nresults, error_index);
	lua_remove(L, error_index);
	return ret;
}

void LuaFunctionsLoader::reportError(const char* function, const std::string &error_desc, bool stack_trace /* = false*/) {
	int32_t scriptId;
	int32_t callbackId;
	bool timerEvent;
	LuaScriptInterface* scriptInterface;
	getScriptEnv()->getEventInfo(scriptId, scriptInterface, callbackId, timerEvent);

	g_logger().error("Lua script error: \nscriptInterface: [{}]\nscriptId: [{}]"
					 "\ntimerEvent: [{}]\n callbackId:[{}]\nfunction: [{}]\nerror [{}]",
					 scriptInterface ? scriptInterface->getInterfaceName() : "", scriptId ? scriptInterface->getFileById(scriptId) : "", timerEvent ? "in a timer event called from:" : "", callbackId ? scriptInterface->getFileById(callbackId) : "", function ? scriptInterface->getInterfaceName() : "", (stack_trace && scriptInterface) ? scriptInterface->getStackTrace(error_desc) : error_desc);
}

int LuaFunctionsLoader::luaErrorHandler(lua_State* L) {
	const std::string &errorMessage = popString(L);
	auto interface = getScriptEnv()->getScriptInterface();
	assert(interface); // This fires if the ScriptEnvironment hasn't been setup
	pushString(L, interface->getStackTrace(errorMessage));
	return 1;
}

void LuaFunctionsLoader::pushVariant(lua_State* L, const LuaVariant &var) {
	lua_createtable(L, 0, 4);
	setField(L, "type", var.type);
	switch (var.type) {
		case VARIANT_NUMBER:
			setField(L, "number", var.number);
			break;
		case VARIANT_STRING:
			setField(L, "string", var.text);
			break;
		case VARIANT_TARGETPOSITION:
		case VARIANT_POSITION: {
			pushPosition(L, var.pos);
			lua_setfield(L, -2, "pos");
			break;
		}
		default:
			break;
	}
	setField(L, "instantName", var.instantName);
	setField(L, "runeName", var.runeName);
	setMetatable(L, -1, "Variant");
}

void LuaFunctionsLoader::pushThing(lua_State* L, std::shared_ptr<Thing> thing) {
	if (!thing) {
		lua_createtable(L, 0, 4);
		setField(L, "uid", 0);
		setField(L, "itemid", 0);
		setField(L, "actionid", 0);
		setField(L, "type", 0);
		return;
	}

	if (std::shared_ptr<Item> item = thing->getItem()) {
		pushUserdata<Item>(L, item);
		setItemMetatable(L, -1, item);
	} else if (std::shared_ptr<Creature> creature = thing->getCreature()) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else {
		lua_pushnil(L);
	}
}

void LuaFunctionsLoader::pushCylinder(lua_State* L, std::shared_ptr<Cylinder> cylinder) {
	if (std::shared_ptr<Creature> creature = cylinder->getCreature()) {
		pushUserdata<Creature>(L, creature);
		setCreatureMetatable(L, -1, creature);
	} else if (std::shared_ptr<Item> parentItem = cylinder->getItem()) {
		pushUserdata<Item>(L, parentItem);
		setItemMetatable(L, -1, parentItem);
	} else if (std::shared_ptr<Tile> tile = cylinder->getTile()) {
		pushUserdata<Tile>(L, tile);
		setMetatable(L, -1, "Tile");
	} else if (cylinder == VirtualCylinder::virtualCylinder) {
		pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
}

void LuaFunctionsLoader::pushString(lua_State* L, const std::string &value) {
	lua_pushlstring(L, value.c_str(), value.length());
}

void LuaFunctionsLoader::pushCallback(lua_State* L, int32_t callback) {
	lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
}

std::string LuaFunctionsLoader::popString(lua_State* L) {
	if (lua_gettop(L) == 0) {
		return std::string();
	}

	std::string str(getString(L, -1));
	lua_pop(L, 1);
	return str;
}

int32_t LuaFunctionsLoader::popCallback(lua_State* L) {
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

// Metatables
void LuaFunctionsLoader::setMetatable(lua_State* L, int32_t index, const std::string &name) {
	luaL_getmetatable(L, name.c_str());
	lua_setmetatable(L, index - 1);
}

void LuaFunctionsLoader::setWeakMetatable(lua_State* L, int32_t index, const std::string &name) {
	static std::set<std::string> weakObjectTypes;
	const std::string &weakName = name + "_weak";

	auto result = weakObjectTypes.emplace(name);
	if (result.second) {
		luaL_getmetatable(L, name.c_str());
		int childMetatable = lua_gettop(L);

		luaL_newmetatable(L, weakName.c_str());
		int metatable = lua_gettop(L);

		for (static const std::vector<std::string> methodKeys = { "__index", "__metatable", "__eq" };
			 const std::string &metaKey : methodKeys) {
			lua_getfield(L, childMetatable, metaKey.c_str());
			lua_setfield(L, metatable, metaKey.c_str());
		}

		for (static const std::vector<int> methodIndexes = { 'h', 'p', 't' };
			 int metaIndex : methodIndexes) {
			lua_rawgeti(L, childMetatable, metaIndex);
			lua_rawseti(L, metatable, metaIndex);
		}

		lua_pushnil(L);
		lua_setfield(L, metatable, "__gc");

		lua_remove(L, childMetatable);
	} else {
		luaL_getmetatable(L, weakName.c_str());
	}
	lua_setmetatable(L, index - 1);
}

void LuaFunctionsLoader::setItemMetatable(lua_State* L, int32_t index, std::shared_ptr<Item> item) {
	if (item && item->getContainer()) {
		luaL_getmetatable(L, "Container");
	} else if (item && item->getTeleport()) {
		luaL_getmetatable(L, "Teleport");
	} else {
		luaL_getmetatable(L, "Item");
	}
	lua_setmetatable(L, index - 1);
}

void LuaFunctionsLoader::setCreatureMetatable(lua_State* L, int32_t index, std::shared_ptr<Creature> creature) {
	if (creature && creature->getPlayer()) {
		luaL_getmetatable(L, "Player");
	} else if (creature && creature->getMonster()) {
		luaL_getmetatable(L, "Monster");
	} else {
		luaL_getmetatable(L, "Npc");
	}
	lua_setmetatable(L, index - 1);
}

CombatDamage LuaFunctionsLoader::getCombatDamage(lua_State* L) {
	CombatDamage damage;
	damage.primary.value = getNumber<int32_t>(L, -4);
	damage.primary.type = getNumber<CombatType_t>(L, -3);
	damage.secondary.value = getNumber<int32_t>(L, -2);
	damage.secondary.type = getNumber<CombatType_t>(L, -1);

	lua_pop(L, 4);
	return damage;
}

// Get
std::string LuaFunctionsLoader::getFormatedLoggerMessage(lua_State* L) {
	std::string format = getString(L, 1);
	int n = lua_gettop(L);
	fmt::dynamic_format_arg_store<fmt::format_context> args;

	for (int i = 2; i <= n; i++) {
		if (isString(L, i)) {
			args.push_back(lua_tostring(L, i));
		} else if (isNumber(L, i)) {
			args.push_back(lua_tonumber(L, i));
		} else if (isBoolean(L, i)) {
			args.push_back(lua_toboolean(L, i) ? "true" : "false");
		} else if (isUserdata(L, i)) {
			LuaData_t userType = getUserdataType(L, i);
			args.push_back(getUserdataTypeName(userType));
		} else if (isTable(L, i)) {
			args.push_back("table");
		} else if (isNil(L, i)) {
			args.push_back("nil");
		} else if (isFunction(L, i)) {
			args.push_back("function");
		} else {
			g_logger().warn("[{}] invalid param type", __FUNCTION__);
		}
	}

	try {
		return fmt::vformat(format, args);
	} catch (const fmt::format_error &e) {
		g_logger().error("[{}] format error: {}", __FUNCTION__, e.what());
	}

	return {};
}

std::string LuaFunctionsLoader::getString(lua_State* L, int32_t arg) {
	size_t len;
	const char* c_str = lua_tolstring(L, arg, &len);
	if (!c_str || len == 0) {
		return std::string();
	}
	return std::string(c_str, len);
}

Position LuaFunctionsLoader::getPosition(lua_State* L, int32_t arg, int32_t &stackpos) {
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_getfield(L, arg, "stackpos");
	if (lua_isnil(L, -1) == 1) {
		stackpos = 0;
	} else {
		stackpos = getNumber<int32_t>(L, -1);
	}

	lua_pop(L, 4);
	return position;
}

Position LuaFunctionsLoader::getPosition(lua_State* L, int32_t arg) {
	Position position;
	position.x = getField<uint16_t>(L, arg, "x");
	position.y = getField<uint16_t>(L, arg, "y");
	position.z = getField<uint8_t>(L, arg, "z");

	lua_pop(L, 3);
	return position;
}

Outfit_t LuaFunctionsLoader::getOutfit(lua_State* L, int32_t arg) {
	Outfit_t outfit;
	outfit.lookMountFeet = getField<uint8_t>(L, arg, "lookMountFeet");
	outfit.lookMountLegs = getField<uint8_t>(L, arg, "lookMountLegs");
	outfit.lookMountBody = getField<uint8_t>(L, arg, "lookMountBody");
	outfit.lookMountHead = getField<uint8_t>(L, arg, "lookMountHead");
	outfit.lookFamiliarsType = getField<uint16_t>(L, arg, "lookFamiliarsType");
	outfit.lookMount = getField<uint16_t>(L, arg, "lookMount");
	outfit.lookAddons = getField<uint8_t>(L, arg, "lookAddons");

	outfit.lookFeet = getField<uint8_t>(L, arg, "lookFeet");
	outfit.lookLegs = getField<uint8_t>(L, arg, "lookLegs");
	outfit.lookBody = getField<uint8_t>(L, arg, "lookBody");
	outfit.lookHead = getField<uint8_t>(L, arg, "lookHead");

	outfit.lookTypeEx = getField<uint16_t>(L, arg, "lookTypeEx");
	outfit.lookType = getField<uint16_t>(L, arg, "lookType");

	lua_pop(L, 13);
	return outfit;
}

LuaVariant LuaFunctionsLoader::getVariant(lua_State* L, int32_t arg) {
	LuaVariant var;
	var.instantName = getFieldString(L, arg, "instantName");
	var.runeName = getFieldString(L, arg, "runeName");
	switch (var.type = getField<LuaVariantType_t>(L, arg, "type")) {
		case VARIANT_NUMBER: {
			var.number = getField<uint32_t>(L, arg, "number");
			lua_pop(L, 4);
			break;
		}

		case VARIANT_STRING: {
			var.text = getFieldString(L, arg, "string");
			lua_pop(L, 4);
			break;
		}

		case VARIANT_POSITION:
		case VARIANT_TARGETPOSITION: {
			lua_getfield(L, arg, "pos");
			var.pos = getPosition(L, lua_gettop(L));
			lua_pop(L, 4);
			break;
		}

		default: {
			var.type = VARIANT_NONE;
			lua_pop(L, 3);
			break;
		}
	}
	return var;
}

std::shared_ptr<Thing> LuaFunctionsLoader::getThing(lua_State* L, int32_t arg) {
	std::shared_ptr<Thing> thing;
	if (lua_getmetatable(L, arg) != 0) {
		lua_rawgeti(L, -1, 't');
		switch (getNumber<LuaData_t>(L, -1)) {
			case LuaData_t::Item:
				thing = getUserdataShared<Item>(L, arg);
				break;
			case LuaData_t::Container:
				thing = getUserdataShared<Container>(L, arg);
				break;
			case LuaData_t::Teleport:
				thing = getUserdataShared<Teleport>(L, arg);
				break;
			case LuaData_t::Player:
				thing = getUserdataShared<Player>(L, arg);
				break;
			case LuaData_t::Monster:
				thing = getUserdataShared<Monster>(L, arg);
				break;
			case LuaData_t::Npc:
				thing = getUserdataShared<Npc>(L, arg);
				break;
			default:
				thing = nullptr;
				break;
		}
		lua_pop(L, 2);
	} else {
		thing = getScriptEnv()->getThingByUID(getNumber<uint32_t>(L, arg));
	}
	return thing;
}

std::shared_ptr<Creature> LuaFunctionsLoader::getCreature(lua_State* L, int32_t arg) {
	if (isUserdata(L, arg)) {
		return getUserdataShared<Creature>(L, arg);
	}
	return g_game().getCreatureByID(getNumber<uint32_t>(L, arg));
}

std::shared_ptr<Player> LuaFunctionsLoader::getPlayer(lua_State* L, int32_t arg, bool allowOffline /* = false */) {
	if (isUserdata(L, arg)) {
		return getUserdataShared<Player>(L, arg);
	} else if (isNumber(L, arg)) {
		return g_game().getPlayerByID(getNumber<uint64_t>(L, arg), allowOffline);
	} else if (isString(L, arg)) {
		return g_game().getPlayerByName(getString(L, arg), allowOffline);
	}
	g_logger().warn("LuaFunctionsLoader::getPlayer: Invalid argument.");
	return nullptr;
}

std::shared_ptr<Guild> LuaFunctionsLoader::getGuild(lua_State* L, int32_t arg, bool allowOffline /* = false */) {
	if (isUserdata(L, arg)) {
		return getUserdataShared<Guild>(L, arg);
	} else if (isNumber(L, arg)) {
		return g_game().getGuild(getNumber<uint64_t>(L, arg), allowOffline);
	} else if (isString(L, arg)) {
		return g_game().getGuildByName(getString(L, arg), allowOffline);
	}
	g_logger().warn("LuaFunctionsLoader::getGuild: Invalid argument.");
	return nullptr;
}

std::string LuaFunctionsLoader::getFieldString(lua_State* L, int32_t arg, const std::string &key) {
	lua_getfield(L, arg, key.c_str());
	return getString(L, -1);
}

LuaData_t LuaFunctionsLoader::getUserdataType(lua_State* L, int32_t arg) {
	if (lua_getmetatable(L, arg) == 0) {
		return LuaData_t::Unknown;
	}
	lua_rawgeti(L, -1, 't');

	LuaData_t type = getNumber<LuaData_t>(L, -1);
	lua_pop(L, 2);

	return type;
}

std::string LuaFunctionsLoader::getUserdataTypeName(LuaData_t userType) {
	return magic_enum::enum_name(userType).data();
}

// Push
void LuaFunctionsLoader::pushBoolean(lua_State* L, bool value) {
	lua_pushboolean(L, value ? 1 : 0);
}

void LuaFunctionsLoader::pushCombatDamage(lua_State* L, const CombatDamage &damage) {
	lua_pushnumber(L, damage.primary.value);
	lua_pushnumber(L, damage.primary.type);
	lua_pushnumber(L, damage.secondary.value);
	lua_pushnumber(L, damage.secondary.type);
	lua_pushnumber(L, damage.origin);
}

void LuaFunctionsLoader::pushInstantSpell(lua_State* L, const InstantSpell &spell) {
	lua_createtable(L, 0, 6);

	setField(L, "name", spell.getName());
	setField(L, "words", spell.getWords());
	setField(L, "level", spell.getLevel());
	setField(L, "mlevel", spell.getMagicLevel());
	setField(L, "mana", spell.getMana());
	setField(L, "manapercent", spell.getManaPercent());

	setMetatable(L, -1, "Spell");
}

void LuaFunctionsLoader::pushPosition(lua_State* L, const Position &position, int32_t stackpos /* = 0*/) {
	lua_createtable(L, 0, 4);

	setField(L, "x", position.x);
	setField(L, "y", position.y);
	setField(L, "z", position.z);
	setField(L, "stackpos", stackpos);

	setMetatable(L, -1, "Position");
}

void LuaFunctionsLoader::pushOutfit(lua_State* L, const Outfit_t &outfit) {
	lua_createtable(L, 0, 13);
	setField(L, "lookType", outfit.lookType);
	setField(L, "lookTypeEx", outfit.lookTypeEx);
	setField(L, "lookHead", outfit.lookHead);
	setField(L, "lookBody", outfit.lookBody);
	setField(L, "lookLegs", outfit.lookLegs);
	setField(L, "lookFeet", outfit.lookFeet);
	setField(L, "lookAddons", outfit.lookAddons);
	setField(L, "lookMount", outfit.lookMount);
	setField(L, "lookMountHead", outfit.lookMountHead);
	setField(L, "lookMountBody", outfit.lookMountBody);
	setField(L, "lookMountLegs", outfit.lookMountLegs);
	setField(L, "lookMountFeet", outfit.lookMountFeet);
	setField(L, "lookFamiliarsType", outfit.lookFamiliarsType);
}

void LuaFunctionsLoader::registerClass(lua_State* L, const std::string &className, const std::string &baseClass, lua_CFunction newFunction /* = nullptr*/) {
	// className = {}
	lua_newtable(L);
	lua_pushvalue(L, -1);
	lua_setglobal(L, className.c_str());
	int methods = lua_gettop(L);

	// methodsTable = {}
	lua_newtable(L);
	int methodsTable = lua_gettop(L);

	if (newFunction) {
		// className.__call = newFunction
		lua_pushcfunction(L, newFunction);
		lua_setfield(L, methodsTable, "__call");
	}

	uint32_t parents = 0;
	if (!baseClass.empty()) {
		lua_getglobal(L, baseClass.c_str());
		lua_rawgeti(L, -1, 'p');
		parents = getNumber<uint32_t>(L, -1) + 1;
		lua_pop(L, 1);
		lua_setfield(L, methodsTable, "__index");
	}

	// setmetatable(className, methodsTable)
	lua_setmetatable(L, methods);

	// className.metatable = {}
	luaL_newmetatable(L, className.c_str());
	int metatable = lua_gettop(L);

	// className.metatable.__metatable = className
	lua_pushvalue(L, methods);
	lua_setfield(L, metatable, "__metatable");

	// className.metatable.__index = className
	lua_pushvalue(L, methods);
	lua_setfield(L, metatable, "__index");

	// className.metatable['h'] = hash
	lua_pushnumber(L, std::hash<std::string>()(className));
	lua_rawseti(L, metatable, 'h');

	// className.metatable['p'] = parents
	lua_pushnumber(L, parents);
	lua_rawseti(L, metatable, 'p');

	// className.metatable['t'] = type
	auto userTypeEnum = magic_enum::enum_cast<LuaData_t>(className);
	if (userTypeEnum.has_value()) {
		lua_pushnumber(L, static_cast<lua_Number>(userTypeEnum.value()));
	} else {
		lua_pushnumber(L, static_cast<lua_Number>(LuaData_t::Unknown));
	}
	lua_rawseti(L, metatable, 't');

	// pop className, className.metatable
	lua_pop(L, 2);
}

void LuaFunctionsLoader::registerMethod(lua_State* L, const std::string &globalName, const std::string &methodName, lua_CFunction func) {
	// globalName.methodName = func
	lua_getglobal(L, globalName.c_str());
	lua_pushcfunction(L, func);
	lua_setfield(L, -2, methodName.c_str());

	// pop globalName
	lua_pop(L, 1);
}

void LuaFunctionsLoader::registerTable(lua_State* L, const std::string &tableName) {
	// _G[tableName] = {}
	lua_newtable(L);
	lua_setglobal(L, tableName.c_str());
}

void LuaFunctionsLoader::registerMetaMethod(lua_State* L, const std::string &className, const std::string &methodName, lua_CFunction func) {
	// className.metatable.methodName = func
	luaL_getmetatable(L, className.c_str());
	lua_pushcfunction(L, func);
	lua_setfield(L, -2, methodName.c_str());

	// pop className.metatable
	lua_pop(L, 1);
}

void LuaFunctionsLoader::registerVariable(lua_State* L, const std::string &tableName, const std::string &name, lua_Number value) {
	// tableName.name = value
	lua_getglobal(L, tableName.c_str());
	setField(L, name.c_str(), value);

	// pop tableName
	lua_pop(L, 1);
}

void LuaFunctionsLoader::registerGlobalBoolean(lua_State* L, const std::string &name, bool value) {
	// _G[name] = value
	pushBoolean(L, value);
	lua_setglobal(L, name.c_str());
}

void LuaFunctionsLoader::registerGlobalMethod(lua_State* L, const std::string &functionName, lua_CFunction func) {
	// _G[functionName] = func
	lua_pushcfunction(L, func);
	lua_setglobal(L, functionName.c_str());
}

void LuaFunctionsLoader::registerGlobalVariable(lua_State* L, const std::string &name, lua_Number value) {
	// _G[name] = value
	lua_pushnumber(L, value);
	lua_setglobal(L, name.c_str());
}

void LuaFunctionsLoader::registerGlobalString(lua_State* L, const std::string &variable, const std::string &name) {
	// Example: registerGlobalString(L, "VARIABLE_NAME", "variable string");
	pushString(L, name);
	lua_setglobal(L, variable.c_str());
}

std::string LuaFunctionsLoader::escapeString(const std::string &string) {
	std::string s = string;
	replaceString(s, "\\", "\\\\");
	replaceString(s, "\"", "\\\"");
	replaceString(s, "'", "\\'");
	replaceString(s, "[[", "\\[[");
	return s;
}

int LuaFunctionsLoader::luaUserdataCompare(lua_State* L) {
	pushBoolean(L, getUserdata<void>(L, 1) == getUserdata<void>(L, 2));
	return 1;
}

void LuaFunctionsLoader::registerSharedClass(lua_State* L, const std::string &className, const std::string &baseClass, lua_CFunction newFunction) {
	registerClass(L, className, baseClass, newFunction);
	registerMetaMethod(L, className, "__gc", luaGarbageCollection);
}

int LuaFunctionsLoader::luaGarbageCollection(lua_State* L) {
	auto objPtr = static_cast<std::shared_ptr<SharedObject>*>(lua_touserdata(L, 1));
	if (objPtr) {
		objPtr->reset();
	}
	return 0;
}
