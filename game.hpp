#ifndef GAME_H
#define GAME_H

#include "network.hpp"
#include <string>

#define GRID_SIDE 8
#define NUM_TREASURES 8
#define TREASURE_DIR "objetos/"
#define SIZE_TOLERANCE 50 * 1024 * 1024 // 50 MB de tolerância


class Coordinate
{
public:
    uint8_t x;
    uint8_t y;

    Coordinate();
    Coordinate(uint8_t x, uint8_t y);

    bool operator==(const Coordinate &other) const;
};

enum cell_type
{
    EMPTY,
    PLAYER,
    VISITED,
    TREASURE,
};

struct Treasure_Position
{
    Coordinate position;
    bool found;
};

class Map
{
public:
    cell_type grid[GRID_SIDE][GRID_SIDE];
    Treasure_Position treasures[NUM_TREASURES];
    Coordinate player_position;

    Map(bool is_player);

    void print();
    bool move_player(message_type movement);
};

class Treasure
{
public:
    std::string filename;
    uint8_t *filename_data;
    FILE *file;
    uint64_t size;
    uint8_t *data;

    Treasure(const std::string &name, bool write);
    ~Treasure();
};

#endif