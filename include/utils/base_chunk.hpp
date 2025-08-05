
#include "d_chunk.hpp"
#include "l_chunk.hpp"

class BaseChunk : public DChunk {

public:
    void savePointCloud();
    void process() override;
    void update() override;

};