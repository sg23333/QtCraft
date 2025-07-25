#include "inventory.h"

Inventory::Inventory() : m_selected_slot(0)
{
    m_items.resize(INVENTORY_SLOTS);
    // 为测试预先添加一些物品
    m_items[0] = {BlockType::Stone, 64};
    m_items[1] = {BlockType::Dirt, 64};
    m_items[2] = {BlockType::Grass, 64};
    m_items[3] = {BlockType::Water, 64};
}

void Inventory::nextSlot()
{
    m_selected_slot = (m_selected_slot + 1) % INVENTORY_SLOTS;
}

void Inventory::prevSlot()
{
    m_selected_slot = (m_selected_slot - 1 + INVENTORY_SLOTS) % INVENTORY_SLOTS;
}

void Inventory::setSlot(int slotIndex)
{
    if (slotIndex >= 0 && slotIndex < INVENTORY_SLOTS) {
        m_selected_slot = slotIndex;
    }
}

int Inventory::getSelectedSlot() const
{
    return m_selected_slot;
}

BlockType Inventory::getSelectedBlockType() const
{
    return m_items[m_selected_slot].type;
}

void Inventory::addItem(BlockType type, int count)
{
    // 简化的添加逻辑，找到第一个可用的槽位
    for(int i = 0; i < INVENTORY_SLOTS; ++i) {
        if(m_items[i].type == BlockType::Air) {
            m_items[i] = {type, count};
            return;
        }
    }
    qWarning() << "Inventory is full!";
}
const InventoryItem& Inventory::getItem(int slotIndex) const
{
    return m_items[slotIndex];
}
