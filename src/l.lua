-- KEYS[1] = queue key (list)
-- ARGV[1] = set key prefix (string), e.g. "set:prefix:"

local popped = redis.call('RPOP', KEYS[1])
if not popped then
  return nil  -- nothing to pop
end

local set_key = ARGV[1] .. popped
local members = redis.call('SMEMBERS', set_key)
redis.call('DEL', set_key)

return {popped, members}




-- ARGV[1] = a (member to add)
-- ARGV[2] = b (id that also names the set suffix)
-- ARGV[3] = c (prefix, used for both set and queue)
-- ARGV[4] = e (queue suffix)

local a = ARGV[1]
local b = ARGV[2]
local c = ARGV[3]
local e = ARGV[4]

local set_key = c .. b
local existed = redis.call('EXISTS', set_key)   -- 1 if key existed already
local added = redis.call('SADD', set_key, a)    -- >0 if new member

if existed == 0 and added > 0 then
  local queue_key = c .. e
  redis.call('RPUSH', queue_key, b)
end

return added  -- number of elements actually added to the set (0 or 1)
