
#include "d_chunk.hpp"
#include "l_chunk.hpp"

class BaseChunk : public DChunk {

protected:
    void savePointCloud();

public:
    void update() override;

};