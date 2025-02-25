/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "pch.hpp"

#include "io/iomapserialize.hpp"
#include "io/iologindata.hpp"
#include "game/game.hpp"
#include "items/bed.hpp"

void IOMapSerialize::loadHouseItems(Map* map) {
	int64_t start = OTSYS_TIME();

	DBResult_ptr result = Database::getInstance().storeQuery("SELECT `data` FROM `tile_store`");
	if (!result) {
		return;
	}

	do {
		unsigned long attrSize;
		const char* attr = result->getStream("data", attrSize);

		PropStream propStream;
		propStream.init(attr, attrSize);

		uint16_t x, y;
		uint8_t z;
		if (!propStream.read<uint16_t>(x) || !propStream.read<uint16_t>(y) || !propStream.read<uint8_t>(z)) {
			continue;
		}

		std::shared_ptr<Tile> tile = map->getTile(x, y, z);
		if (!tile) {
			continue;
		}

		uint32_t item_count;
		if (!propStream.read<uint32_t>(item_count)) {
			continue;
		}

		while (item_count--) {
			loadItem(propStream, tile, true);
		}
	} while (result->next());
	g_logger().info("Loaded house items in {} seconds", (OTSYS_TIME() - start) / (1000.));
}
bool IOMapSerialize::saveHouseItems() {
	bool success = DBTransaction::executeWithinTransaction([]() {
		return SaveHouseItemsGuard();
	});

	if (!success) {
		g_logger().error("[{}] Error occurred saving houses", __FUNCTION__);
	}

	return success;
}

bool IOMapSerialize::SaveHouseItemsGuard() {
	int64_t start = OTSYS_TIME();
	Database &db = Database::getInstance();
	std::ostringstream query;

	// clear old tile data
	if (!db.executeQuery("DELETE FROM `tile_store`")) {
		return false;
	}

	DBInsert stmt("INSERT INTO `tile_store` (`house_id`, `data`) VALUES ");

	PropWriteStream stream;
	for (const auto &[key, house] : g_game().map.houses.getHouses()) {
		// save house items
		for (std::shared_ptr<HouseTile> tile : house->getTiles()) {
			saveTile(stream, tile);

			size_t attributesSize;
			const char* attributes = stream.getStream(attributesSize);
			if (attributesSize > 0) {
				query << house->getId() << ',' << db.escapeBlob(attributes, attributesSize);
				if (!stmt.addRow(query)) {
					return false;
				}
				stream.clear();
			}
		}
	}

	if (!stmt.execute()) {
		return false;
	}

	g_logger().info("Saved house items in {} seconds", (OTSYS_TIME() - start) / (1000.));
	return true;
}

bool IOMapSerialize::loadContainer(PropStream &propStream, std::shared_ptr<Container> container) {
	while (container->serializationCount > 0) {
		if (!loadItem(propStream, container)) {
			g_logger().warn("Deserialization error for container item: {}", container->getID());
			return false;
		}
		container->serializationCount--;
	}

	uint8_t endAttr;
	if (!propStream.read<uint8_t>(endAttr) || endAttr != 0) {
		g_logger().warn("Deserialization error for container item: {}", container->getID());
		return false;
	}
	return true;
}

uint32_t NEW_BEDS_START_ID = 30000;

bool IOMapSerialize::loadItem(PropStream &propStream, std::shared_ptr<Cylinder> parent, bool isHouseItem /*= false*/) {
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return false;
	}

	std::shared_ptr<Tile> tile = nullptr;
	if (parent->getParent() == nullptr) {
		tile = parent->getTile();
	}

	const ItemType &iType = Item::items[id];
	if (isHouseItem && iType.isBed() && id < NEW_BEDS_START_ID) {
		return false;
	}
	if (iType.moveable || !tile || iType.isCarpet() || iType.isBed()) {
		// create a new item
		std::shared_ptr<Item> item = Item::CreateItem(id);
		if (item) {
			if (item->unserializeAttr(propStream)) {
				std::shared_ptr<Container> container = item->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				parent->internalAddThing(item);
				item->startDecaying();
			} else {
				g_logger().warn("Deserialization error in {}", id);

				return false;
			}
		}
	} else {
		// Stationary items like doors/beds/blackboards/bookcases
		std::shared_ptr<Item> item = nullptr;
		if (const TileItemVector* items = tile->getItemList()) {
			for (auto &findItem : *items) {
				if (findItem->getID() == id) {
					item = findItem;
					break;
				} else if (iType.m_transformOnUse && findItem->getID() == iType.m_transformOnUse) {
					item = findItem;
					break;
				} else if (iType.isDoor() && findItem->getDoor()) {
					item = findItem;
					break;
				} else if (iType.isBed() && findItem->getBed()) {
					item = findItem;
					break;
				}
			}
		}

		if (item) {
			if (item->unserializeAttr(propStream)) {
				std::shared_ptr<Container> container = item->getContainer();
				if (container && !loadContainer(propStream, container)) {
					return false;
				}

				g_game().transformItem(item, id);
			} else {
				g_logger().warn("Deserialization error in {}", id);
			}
		} else {
			// The map changed since the last save, just read the attributes
			auto dummy = Item::CreateItem(id);
			if (dummy) {
				dummy->unserializeAttr(propStream);
				std::shared_ptr<Container> container = dummy->getContainer();
				if (container) {
					if (!loadContainer(propStream, container)) {
						return false;
					}
				} else if (std::shared_ptr<BedItem> bedItem = std::dynamic_pointer_cast<BedItem>(dummy)) {
					uint32_t sleeperGUID = bedItem->getSleeper();
					if (sleeperGUID != 0) {
						g_game().removeBedSleeper(sleeperGUID);
					}
				}
			}
		}
	}
	return true;
}

void IOMapSerialize::saveItem(PropWriteStream &stream, std::shared_ptr<Item> item) {
	std::shared_ptr<Container> container = item->getContainer();

	// Write ID & props
	stream.write<uint16_t>(item->getID());
	item->serializeAttr(stream);

	if (container) {
		// Hack our way into the attributes
		stream.write<uint8_t>(ATTR_CONTAINER_ITEMS);
		stream.write<uint32_t>(container->size());
		for (auto it = container->getReversedItems(), end = container->getReversedEnd(); it != end; ++it) {
			saveItem(stream, *it);
		}
	}

	stream.write<uint8_t>(0x00); // attr end
}

void IOMapSerialize::saveTile(PropWriteStream &stream, std::shared_ptr<Tile> tile) {
	const TileItemVector* tileItems = tile->getItemList();
	if (!tileItems) {
		return;
	}

	std::forward_list<std::shared_ptr<Item>> items;
	uint16_t count = 0;
	for (auto &item : *tileItems) {
		if (!item->isSavedToHouses()) {
			continue;
		}

		items.push_front(item);
		++count;
	}

	if (!items.empty()) {
		const Position &tilePosition = tile->getPosition();
		stream.write<uint16_t>(tilePosition.x);
		stream.write<uint16_t>(tilePosition.y);
		stream.write<uint8_t>(tilePosition.z);

		stream.write<uint32_t>(count);
		for (std::shared_ptr<Item> item : items) {
			saveItem(stream, item);
		}
	}
}

bool IOMapSerialize::loadHouseInfo() {
	Database &db = Database::getInstance();

	DBResult_ptr result = db.storeQuery("SELECT `id`, `owner`, `new_owner`, `paid`, `warnings` FROM `houses`");
	if (!result) {
		return false;
	}

	do {
		auto houseId = result->getNumber<uint32_t>("id");
		const auto &house = g_game().map.houses.getHouse(houseId);
		if (house) {
			uint32_t owner = result->getNumber<uint32_t>("owner");
			int32_t newOwner = result->getNumber<int32_t>("new_owner");
			// Transfer house owner
			auto isTransferOnRestart = g_configManager().getBoolean(TOGGLE_HOUSE_TRANSFER_ON_SERVER_RESTART);
			if (isTransferOnRestart && newOwner >= 0) {
				g_game().setTransferPlayerHouseItems(houseId, owner);
				if (newOwner == 0) {
					g_logger().debug("Removing house id '{}' owner", houseId);
					house->setOwner(0);
				} else {
					g_logger().debug("Setting house id '{}' owner to player GUID '{}'", houseId, newOwner);
					house->setOwner(newOwner);
				}
			} else {
				house->setOwner(owner, false);
			}
			house->setPaidUntil(result->getNumber<time_t>("paid"));
			house->setPayRentWarnings(result->getNumber<uint32_t>("warnings"));
		}
	} while (result->next());

	result = db.storeQuery("SELECT `house_id`, `listid`, `list` FROM `house_lists`");
	if (result) {
		do {
			const auto &house = g_game().map.houses.getHouse(result->getNumber<uint32_t>("house_id"));
			if (house) {
				house->setAccessList(result->getNumber<uint32_t>("listid"), result->getString("list"));
			}
		} while (result->next());
	}
	return true;
}

bool IOMapSerialize::saveHouseInfo() {
	bool success = DBTransaction::executeWithinTransaction([]() {
		return SaveHouseInfoGuard();
	});

	if (!success) {
		g_logger().error("[{}] Error occurred saving houses info", __FUNCTION__);
	}

	return success;
}

bool IOMapSerialize::SaveHouseInfoGuard() {
	Database &db = Database::getInstance();

	if (!db.executeQuery("DELETE FROM `house_lists`")) {
		return false;
	}

	std::ostringstream query;
	for (const auto &[key, house] : g_game().map.houses.getHouses()) {
		query << "SELECT `id` FROM `houses` WHERE `id` = " << house->getId();
		DBResult_ptr result = db.storeQuery(query.str());
		if (result) {
			query.str(std::string());
			query << "UPDATE `houses` SET `owner` = " << house->getOwner() << ", `paid` = " << house->getPaidUntil() << ", `warnings` = " << house->getPayRentWarnings() << ", `name` = " << db.escapeString(house->getName()) << ", `town_id` = " << house->getTownId() << ", `rent` = " << house->getRent() << ", `size` = " << house->getSize() << ", `beds` = " << house->getBedCount() << " WHERE `id` = " << house->getId();
		} else {
			query.str(std::string());
			query << "INSERT INTO `houses` (`id`, `owner`, `paid`, `warnings`, `name`, `town_id`, `rent`, `size`, `beds`) VALUES (" << house->getId() << ',' << house->getOwner() << ',' << house->getPaidUntil() << ',' << house->getPayRentWarnings() << ',' << db.escapeString(house->getName()) << ',' << house->getTownId() << ',' << house->getRent() << ',' << house->getSize() << ',' << house->getBedCount() << ')';
		}

		db.executeQuery(query.str());
		query.str(std::string());
	}

	DBInsert stmt("INSERT INTO `house_lists` (`house_id` , `listid` , `list`) VALUES ");

	for (const auto &[key, house] : g_game().map.houses.getHouses()) {
		std::string listText;
		if (house->getAccessList(GUEST_LIST, listText) && !listText.empty()) {
			query << house->getId() << ',' << GUEST_LIST << ',' << db.escapeString(listText);
			if (!stmt.addRow(query)) {
				return false;
			}

			listText.clear();
		}

		if (house->getAccessList(SUBOWNER_LIST, listText) && !listText.empty()) {
			query << house->getId() << ',' << SUBOWNER_LIST << ',' << db.escapeString(listText);
			if (!stmt.addRow(query)) {
				return false;
			}

			listText.clear();
		}

		for (std::shared_ptr<Door> door : house->getDoors()) {
			if (door->getAccessList(listText) && !listText.empty()) {
				query << house->getId() << ',' << door->getDoorId() << ',' << db.escapeString(listText);
				if (!stmt.addRow(query)) {
					return false;
				}

				listText.clear();
			}
		}
	}

	if (!stmt.execute()) {
		return false;
	}

	return true;
}
