local floor = math.floor
local function size(a) return #a end
local function fmod(a,b) return a % b end
local function push_back(a,v) a[#a+1] = v end
local function big_mul_int(a, factor)
    local res = {}
    local carry = 0
    local len = size(a)
    for i = 0, len - 1 do
        local val = carry + a[i + 1] * factor
        push_back(res, fmod(val, 10000))
        carry = floor(val / 10000)
    end
    while carry > 0 do
        push_back(res, fmod(carry, 10000))
        carry = floor(carry / 10000)
    end
    return res
end

local function big_div_int(a, divisor)
    local res = {}
    local len = size(a)
    if len == 0 then
        push_back(res, 0)
        return res
    end
    local rem = 0
    local tmp = {}
    for i = len - 1, 0, -1 do
        local cur = rem * 10000 + a[i + 1]
        local q = floor(cur / divisor)
        rem = fmod(cur, divisor)
        push_back(tmp, q)
    end
    local tmp_len = size(tmp)
    local start = 1
    while start < tmp_len and tmp[start] == 0 do
        start = start + 1
    end
    for i = tmp_len, start, -1 do
        push_back(res, tmp[i])
    end
    return res
end

local function storage_benchmark(mode, rounds)
    local first = 9007199254740993
    if mode == 6 then first = 19 end
    local checksum = 0
    if mode == 3 or mode == 7 then
        for n = 1, rounds do
            local value = first
            for i = 1, 128 do value = value + 1 end
            checksum = checksum + value % 1000003
        end
        return tostring(checksum)
    end
    local source = {}
    if mode == 4 then
        for i = 0, 127 do source[#source + 1] = first + i end
    end
    local result = {}
    for n = 1, rounds do
        result = {}
        if mode == 4 then
            for i = 1, 128 do result[#result + 1] = source[i] end
        else
            local value = first
            for i = 1, 128 do
                result[#result + 1] = value
                value = value + 1
            end
        end
        checksum = checksum + result[1] % 1000003 + result[128] % 1000003
    end
    local output = tostring(checksum)
    for i = 1, 128 do output = output .. ":" .. tostring(result[i]) end
    return output
end

local function benchmark(mode, rounds)
    if mode >= 3 then return storage_benchmark(mode, rounds) end
    local a = {}
    for i = 0, 127 do a[#a+1] = (i * 73 + 19) % 10000 end
    local result = {}
    local checksum = 0
    for n = 1, rounds do
        if mode == 0 then result = big_mul_int(a, 239)
        elseif mode == 1 then result = big_div_int(a, 239)
        else result = big_div_int(big_mul_int(a, 239), 239) end
        checksum = checksum + result[1] + result[#result]
    end
    local output = tostring(checksum)
    for i = 1, #result do output = output .. ":" .. tostring(result[i]) end
    return output
end
