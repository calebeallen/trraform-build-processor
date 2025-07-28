#include "worker.hpp"

void worker(){

    // connect to redis

    while(true){

        // for each queue level, check get item
            // try reconnect redis if needed
            // if item, break and process item
            // if no item in any level, sleep

        // pull chunk, decode into parts
        // if chunk layer 2 (std res) 
            // pull all builds that need update
            // check metadata for default flag
            // if default flag
                // set build data to default plot
            // replace build data into chunk
            // for each build that needs update, call create image data

        // call update chunk
        // if chunk not layer 0
            // chunk.save()

    }

}

