local warmup_runs = 1
local measured_runs = 6
local runs_per_sample = 32

local function size(value)
    return #value
end

local function floor(value)
    return math.floor(value)
end

local function fmod(left, right)
    return left % right
end

local function to_string(value)
    return tostring(value)
end

local function push_back(values, value)
    values[#values + 1] = value
end

local function pop_back(values)
    values[#values] = nil
end

local function big_is_zero(a)
    local len = size(a)
    if len == 0 then return 1 end
    if len == 1 and a[1] == 0 then return 1 end
    return 0
end

local function big_add(a, b)
    local res = {}
    local len_a = size(a)
    local len_b = size(b)
    local max_len = len_a
    if len_b > max_len then max_len = len_b end
    local carry = 0
    for i = 0, max_len - 1 do
        if i >= max_len and carry <= 0 then break end
        local val_a = 0
        local val_b = 0
        if i < len_a then val_a = a[i + 1] end
        if i < len_b then val_b = b[i + 1] end
        local sum = val_a + val_b + carry
        push_back(res, sum % 10000)
        carry = sum // 10000
    end
    while carry > 0 do
        push_back(res, carry % 10000)
        carry = carry // 10000
    end
    return res
end

local function big_sub(a, b)
    local res = {}
    local len_a = size(a)
    local len_b = size(b)
    local borrow = 0
    for i = 0, len_a - 1 do
        local val_a = a[i + 1]
        local val_b = 0
        if i < len_b then val_b = b[i + 1] end
        local diff = val_a - val_b - borrow
        if diff < 0 then
            diff = diff + 10000
            borrow = 1
        else
            borrow = 0
        end
        push_back(res, diff)
    end
    while size(res) > 1 and res[size(res)] == 0 do
        pop_back(res)
    end
    return res
end

local function big_mul_int(a, factor)
    local res = {}
    local carry = 0
    local len = size(a)
    for i = 0, len - 1 do
        local val = carry
        if i < len then val = val + a[i + 1] * factor end
        push_back(res, val % 10000)
        carry = val // 10000
    end
    while carry > 0 do
        push_back(res, carry % 10000)
        carry = carry // 10000
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
        local q = cur // divisor
        rem = cur % divisor
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

local function calc_arctan(x, base_val, max_iters)
    local term = big_div_int(base_val, x)
    local sum_val = term
    local x_sq = x * x
    for k = 1, max_iters - 1 do
        term = big_div_int(term, x_sq)
        if big_is_zero(term) == 1 then
            break
        end
        local divisor = 2 * k + 1
        local term_div = big_div_int(term, divisor)
        if floor(fmod(k, 2)) == 1 then
            sum_val = big_sub(sum_val, term_div)
        else
            sum_val = big_add(sum_val, term_div)
        end
    end
    return sum_val
end

local function format_pi(pi_arr, target_digits)
    local len = size(pi_arr)
    if len == 0 then return "0.0000" end
    local str = to_string(pi_arr[len]) .. "."
    local printed_digits = 0
    for i = len - 1, 1, -1 do
        local v = pi_arr[i]
        if v < 10 then str = str .. "000" .. to_string(v)
        elseif v < 100 then str = str .. "00" .. to_string(v)
        elseif v < 1000 then str = str .. "0" .. to_string(v)
        else str = str .. to_string(v)
        end
        printed_digits = printed_digits + 4
        if printed_digits >= target_digits then break end
    end
    return str
end

local function calculate_pi()
    local target_digits = 500
    local num_blocks = target_digits // 4 + 3
    local base_val = {}
    for _ = 1, num_blocks do
        push_back(base_val, 0)
    end
    push_back(base_val, 1)
    local iters = 360
    local atan5 = calc_arctan(5, base_val, iters)
    local atan239 = calc_arctan(239, base_val, iters)
    local term1 = big_mul_int(atan5, 16)
    local term2 = big_mul_int(atan239, 4)
    local pi_big = big_sub(term1, term2)
    return format_pi(pi_big, target_digits)
end

local function fnv1a32(value)
    local hash = 2166136261
    local prime = 16777619
    for index = 1, #value do
        hash = (hash ~ string.byte(value, index)) * prime
    end
    return string.format("%08x", hash & 0xffffffff)
end

local function assert_blocks(actual, expected, label)
    assert(size(actual) == size(expected), label .. " length")
    for index = 1, size(expected) do
        assert(actual[index] == expected[index], label .. " block " .. index)
    end
end

assert_blocks(big_div_int({0, 1}, 5), {2000}, "big_div_int")
assert_blocks(big_add({9999}, {2}), {1, 1}, "big_add")
assert_blocks(big_sub({0, 1}, {1}), {9999}, "big_sub")
assert_blocks(big_mul_int({9999}, 2), {9998, 1}, "big_mul_int")

local expected
for _ = 1, warmup_runs do
    expected = calculate_pi()
end

local samples = {}
for index = 1, measured_runs do
    local start = os.clock()
    for _ = 1, runs_per_sample do
        local value = calculate_pi()
        assert(value == expected, "PI result changed between samples")
    end
    samples[index] = (os.clock() - start) * 1000 / runs_per_sample
end

table.sort(samples)
local sum = 0
for _, sample in ipairs(samples) do
    sum = sum + sample
end
io.write(string.format("Lua 5.4 execute: median_ms=%.4f, mean_ms=%.4f, samples_ms=", samples[math.floor(#samples / 2) + 1], sum / #samples))
for _, sample in ipairs(samples) do
    io.write(string.format(" %.4f", sample))
end
io.write("\n")
io.write(string.format("Lua PI result: length=%d, fnv1a32=%s\n", #expected, fnv1a32(expected)))
local output = assert(io.open("build/lua_pi_result.txt", "wb"))
output:write(expected)
output:close()
