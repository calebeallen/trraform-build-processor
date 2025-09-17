# Test script pushing dummy data to redis

# CHUNK: 1d23
# PLOT: 8693 <-- hex

import redis
import os
from dotenv import load_dotenv

REDIS_UPDATE_QUEUE = "up:q:0"
REDIS_UPDATE_NEEDS_UPDATE_PREFIX = "up:nu:"
REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX = "up:nu:f:"
REDIS_FLAG_UPDATE_METADATA_FIELDS_ONLY = "mfo"
REDIS_FLAG_SET_DEFAULT_PLOT = "sdp"
REDIS_FLAG_SET_DEFAULT_BUILD = "sdb"
REDIS_FLAG_NO_IMAGE_UPDATE = "niu"

class UpdateFlags:
    def __init__(
        self,
        metadata_only = False,
        default_plot = False,
        default_build = False,
        no_img_update = False
    ):
        self.metadata_only = metadata_only
        self.default_plot = default_plot
        self.default_build = default_build
        self.no_img_update = no_img_update
    
    def __str__():
        flags = []
        if self.metadata_only:
            flags.append(REDIS_FLAG_UPDATE_METADATA_FIELDS_ONLY)
        if self.default_plot:
            flags.append(REDIS_FLAG_SET_DEFAULT_PLOT)
        if self.default_build:
            flags.append(REDIS_FLAG_SET_DEFAULT_BUILD)
        if self.no_img_update:
            flags.append(REDIS_FLAG_NO_IMAGE_UPDATE)

        return " ".join(flags)
    

load_dotenv(dotenv_path="../.env")

r = redis.Redis( 
    host='redis-16216.c15.us-east-1-4.ec2.redns.redis-cloud.com', 
    port=16216, 
    username='default', 
    password=os.getenv("REDIS_PASSWORD"), 
    db=0
)

# Push chunk to queue
r.lpush(REDIS_UPDATE_QUEUE, "l2_1d23")

# Add plot to chunk's needs update list
r.sadd(REDIS_UPDATE_NEEDS_UPDATE_PREFIX + "l2_1d23", "8693")


# Store plot's update flags
# flags = UpdateFlags()
# r.set(REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + "8693", )

