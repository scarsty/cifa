// Keep base-10000 blocks and remainders integer, matching the Lua benchmark.
// Use a double operand for division: Lua / produces a float even for integers.
// All modulo operands here are nonnegative, so Cifa % matches Lua %.
// 判断大数数组是否为 0
int big_is_zero(a) {
    int len = size(a);
    if (len == 0) return 1;
    if (len == 1 && a[0] == 0) return 1;
    return 0;
}

// 大数加法: res = a + b
auto big_add(a, b) {
    res = {};
    int len_a = size(a);
    int len_b = size(b);
    int max_len = len_a;
    if (len_b > max_len) max_len = len_b;
    res.reserve(max_len + 1);

    int carry = 0;
    for (int i = 0; i < max_len; i++) {
        int val_a = 0;
        int val_b = 0;
        if (i < len_a) val_a = a[i];
        if (i < len_b) val_b = b[i];

        int sum = val_a + val_b + carry;
        res.push_back(sum % 10000);
        carry = floor(sum / 10000.0);
    }
    while (carry > 0) {
        res.push_back(carry % 10000);
        carry = floor(carry / 10000.0);
    }
    return res;
}

// 大数减法: res = a - b (前提要求 a >= b)
auto big_sub(a, b) {
    res = {};
    int len_a = size(a);
    int len_b = size(b);
    res.reserve(len_a);
    int borrow = 0;

    for (int i = 0; i < len_a; i++) {
        int val_a = a[i];
        int val_b = 0;
        if (i < len_b) val_b = b[i];

        int diff = val_a - val_b - borrow;
        if (diff < 0) {
            diff += 10000;
            borrow = 1;
        } else {
            borrow = 0;
        }
        res.push_back(diff);
    }

    // 剔除高位前导 0
    while (size(res) > 1 && res[size(res) - 1] == 0) {
        res.pop_back();
    }
    return res;
}

// 大数乘单个整数: res = a * factor
auto big_mul_int(a, factor) {
    res = {};
    int carry = 0;
    int len = size(a);
    res.reserve(len + 1);
    for (int i = 0; i < len; i++) {
        int val = carry + a[i] * factor;
        res.push_back(val % 10000);
        carry = floor(val / 10000.0);
    }
    while (carry > 0) {
        res.push_back(carry % 10000);
        carry = floor(carry / 10000.0);
    }
    return res;
}

// 大数除以单个整数: res = a / divisor
auto big_div_int(a, divisor) {
    res = {};
    int len = size(a);
    res.reserve(len);
    if (len == 0) {
        res.push_back(0);
        return res;
    }
    int rem = 0;
    tmp = {};
    tmp.reserve(len);
    for (int i = len - 1; i >= 0; i--) {
        int cur = rem * 10000 + a[i];
        int q = floor((double)cur / divisor);
        rem = cur % divisor;
        tmp.push_back(q);
    }
    int tmp_len = size(tmp);
    int start = 0;
    while (start < tmp_len - 1 && tmp[start] == 0) {
        start++;
    }
    for (int i = tmp_len - 1; i >= start; i--) {
        res.push_back(tmp[i]);
    }
    return res;
}

// 计算 arctan(1/x) * base
auto calc_arctan(x, base_val, max_iters) {
    term = big_div_int(base_val, x);
    sum_val = term;
    int x_sq = x * x;

    for (int k = 1; k < max_iters; k++) {
        term = big_div_int(term, x_sq);
        if (big_is_zero(term)) break; // 精度衰减至 0 时提前退出

        int divisor = 2 * k + 1;
        term_div = big_div_int(term, divisor);

        if (k % 2 == 1) {
            sum_val = big_sub(sum_val, term_div);
        } else {
            sum_val = big_add(sum_val, term_div);
        }
    }
    return sum_val;
}

// 格式化输出字符串
string format_pi(pi_arr, target_digits) {
    int len = size(pi_arr);
    if (len == 0) return "0.0000";

    string str = to_string(pi_arr[len - 1]) + ".";
    int printed_digits = 0;

    for (int i = len - 2; i >= 0 && printed_digits < target_digits; i--) {
        int v = pi_arr[i];
        if (v < 10) str += "000" + to_string(v);
        else if (v < 100) str += "00" + to_string(v);
        else if (v < 1000) str += "0" + to_string(v);
        else str += to_string(v);
        printed_digits += 4;
    }
    return str;
}

int target_digits = 500;
// 125 (500/4) + 3 (缓冲区) = 128 个 0 块
int num_blocks = floor(target_digits / 4.0) + 3;

base_val = {};
base_val.reserve(num_blocks + 1);
for (int i = 0; i < num_blocks; i++) {
    base_val.push_back(0);
}
base_val.push_back(1);
// 360 次迭代足以保证 25^-360 < 10^-500 的精度收敛
int iters = 360;
atan5 = calc_arctan(5, base_val, iters);
atan239 = calc_arctan(239, base_val, iters);

term1 = big_mul_int(atan5, 16);
term2 = big_mul_int(atan239, 4);
pi_big = big_sub(term1, term2);
string pi_str = format_pi(pi_big, target_digits);
println("高精度计算 PI (前 " + to_string(target_digits) + " 位) :");
println(pi_str);
return pi_str;
