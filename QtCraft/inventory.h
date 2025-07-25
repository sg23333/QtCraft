#ifndef INVENTORY_H
#define INVENTORY_H

#include "block.h"
#include <vector>
#include <QDebug>

const int INVENTORY_SLOTS = 9;

struct InventoryItem {
    BlockType type = BlockType::Air;
    int count = 0;
};

class Inventory
{
public:
    Inventory();

    void nextSlot();
    void prevSlot();
    void setSlot(int slotIndex);
    int getSelectedSlot() const;

    BlockType getSelectedBlockType() const;
    void addItem(BlockType type, int count = 1);

private:
    std::vector<InventoryItem> m_items;
    int m_selected_slot;
};

#endif // INVENTORY_H
