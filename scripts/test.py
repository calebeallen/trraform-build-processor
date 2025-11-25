# Test script pushing dummy data to redis

# CHUNK: 1d23
# PLOT: 8693 <-- hex

import struct
import redis
import os
from dotenv import load_dotenv

REDIS_UPDATE_QUEUE = "up:q:0"
REDIS_UPDATE_NEEDS_UPDATE_PREFIX = "up:nu:"
REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX = "up:nu:f:"
REDIS_FLAG_UPDATE_METADATA_FIELDS_ONLY = "mfo"
REDIS_FLAG_SET_DEFAULT_JSON = "sdj";
REDIS_FLAG_SET_DEFAULT_BUILD = "sdb";
REDIS_FLAG_NO_IMAGE_UPDATE = "niu";

class UpdateFlags:
    def __init__(
        self,
        default_json = False,
        default_build = False,
        no_img_update = False
    ):
        self.default_json = default_json
        self.default_build = default_build
        self.no_img_update = no_img_update
    
    def __str__(self):
        flags = []
        if self.default_json:
            flags.append(REDIS_FLAG_SET_DEFAULT_JSON)
        if self.default_build:
            flags.append(REDIS_FLAG_SET_DEFAULT_BUILD)
        if self.no_img_update:
            flags.append(REDIS_FLAG_NO_IMAGE_UPDATE)

        return " ".join(flags)
    

load_dotenv()

r = redis.Redis( 
    host='redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com', 
    port=16216, 
    username='default', 
    password=os.getenv("REDIS_PASSWORD"), 
    db=0
)

# Push chunk to queue

chunk_map = {} # from chunk to plot idx

with open("../static/cmap_l2.dat", "rb") as file:
    bin = file.read()
    n = len(bin) // 4
    vals = list(struct.unpack(f'<{n}I', bin))
    
    j = 1
    for i in range(0, n, 2):
        p_id = vals[i]
        if p_id not in chunk_map:
            chunk_map[p_id] = []

        chunk_map[p_id].append(j)
        j += 1

print(chunk_map[0])

def flag_chunk(chunk_id):

    chunk_id_str = f"l2_{hex(chunk_id)[2:]}"

    # Add plot to chunk's needs update list
    needs_update = [hex(plot_id)[2:] for plot_id in chunk_map[chunk_id]]

    r.sadd(REDIS_UPDATE_NEEDS_UPDATE_PREFIX + chunk_id_str, *needs_update)

    # Store plot's update flagsREDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX
    flags = UpdateFlags()
    # r.sadd(REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + "8693", "HELLO");

    r.lpush(REDIS_UPDATE_QUEUE, chunk_id_str)

for i in range(1):
    flag_chunk(i)

