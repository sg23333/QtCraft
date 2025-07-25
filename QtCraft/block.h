#ifndef BLOCK_H
#define BLOCK_H

#include <cstdint>

// 方块类型枚举，用于替代魔法数字
enum class BlockType : uint8_t {
    Air = 0,    // 空气
    Stone = 1,  // 石头
    Dirt = 2,   // 泥土
    Grass = 3,  // 草方块
    Water = 4   // 水
};

// 纹理图集中各个纹理的信息
namespace Texture {
const int Stone = 0;
const int Dirt = 1;
const int GrassTop = 2;
const int GrassSide = 3;
const int Water = 4;
const float AtlasWidth = 5.0f; // 纹理图集包含5个不同的纹理
const float TileWidth = 1.0f / AtlasWidth;
}

#endif // BLOCK_H
