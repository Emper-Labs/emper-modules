#include <emper/Emper_Engine.h>

#include <string>

namespace emper::module::cgol
{

using Cell = emper::i8;

struct CellCoordinate
{
    emper::i32 x;
    emper::i32 y;
};

struct Pattern
{
    std::string name;

    emper::i32 width  = 0;
    emper::i32 height = 0;

    std::vector<CellCoordinate> cells;
};

Pattern loadRLE(const std::string& path);


class GameOfLife
    : public emper::interfaces::module::ISystem
    , public emper::interfaces::behavior::IRenderable
{
public:

    GameOfLife(
        std::size_t width,
        std::size_t height
    )
        : m_width(width)
        , m_height(height)
        , m_current(width * height)
        , m_next(width * height)
    {
    }


    void tick(emper::f32 dt);

    void render(
        emper::interfaces::backend::IRenderer& renderer
    ) override;

    void randomize(float probability = 0.15f);


    void load(
    const Pattern& pattern,
    emper::i32 offsetX = 0,
    emper::i32 offsetY = 0
    );

    void clear();

private:

    void step();


    int countNeighbors(
        std::size_t x,
        std::size_t y
    ) const;


private:

    std::size_t m_width;
    std::size_t m_height;

    std::vector<Cell> m_current;
    std::vector<Cell> m_next;

    std::size_t m_generation = 0;
    float m_accumulator = 0.0f;
};


};