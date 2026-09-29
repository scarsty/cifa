// Dynamic-type variant of the PI workload. The algorithm and precision match
// calc-pi.c; explicit local and parameter types are intentionally omitted.
big_is_zero(a) {
    len = size(a);
    if (len == 0) return 1;
    if (len == 1 && a[0] == 0) return 1;
    return 0;
}

big_add(a, b) {
    res = {};
    len_a = size(a);
    len_b = size(b);
    max_len = len_a;
    if (len_b > max_len) max_len = len_b;
    res.reserve(max_len + 1);

    carry = 0;
    for (i = 0; i < max_len; i++) {
        val_a = 0;
        val_b = 0;
        if (i < len_a) val_a = a[i];
        if (i < len_b) val_b = b[i];

        sum = val_a + val_b + carry;
        res.push_back(sum % 10000);
        carry = sum / 10000;
    }
    while (carry > 0) {
        res.push_back(carry % 10000);
        carry = carry / 10000;
    }
    return res;
}

big_sub(a, b) {
    res = {};
    len_a = size(a);
    len_b = size(b);
    res.reserve(len_a);
    borrow = 0;

    for (i = 0; i < len_a; i++) {
        val_a = a[i];
        val_b = 0;
        if (i < len_b) val_b = b[i];

        diff = val_a - val_b - borrow;
        if (diff < 0) {
            diff += 10000;
            borrow = 1;
        } else {
            borrow = 0;
        }
        res.push_back(diff);
    }

    while (size(res) > 1 && res[size(res) - 1] == 0) {
        res.pop_back();
    }
    return res;
}

big_mul_int(a, factor) {
    res = {};
    carry = 0;
    len = size(a);
    res.reserve(len + 1);
    for (i = 0; i < len; i++) {
        val = carry + a[i] * factor;
        res.push_back(val % 10000);
        carry = val / 10000;
    }
    while (carry > 0) {
        res.push_back(carry % 10000);
        carry = carry / 10000;
    }
    return res;
}

big_div_int(a, divisor) {
    res = {};
    len = size(a);
    res.reserve(len);
    if (len == 0) {
        res.push_back(0);
        return res;
    }
    rem = 0;
    tmp = {};
    tmp.reserve(len);
    for (i = len - 1; i >= 0; i--) {
        cur = rem * 10000 + a[i];
        q = cur / divisor;
        rem = cur % divisor;
        tmp.push_back(q);
    }
    tmp_len = size(tmp);
    start = 0;
    while (start < tmp_len - 1 && tmp[start] == 0) {
        start++;
    }
    for (i = tmp_len - 1; i >= start; i--) {
        res.push_back(tmp[i]);
    }
    return res;
}

calc_arctan(x, base_val, max_iters) {
    term = big_div_int(base_val, x);
    sum_val = term;
    term_div = term;
    x_sq = x * x;

    for (k = 1; k < max_iters; k++) {
        term = big_div_int(term, x_sq);
        if (big_is_zero(term)) break;

        divisor = 2 * k + 1;
        term_div = big_div_int(term, divisor);

        if (k % 2 == 1) {
            sum_val = big_sub(sum_val, term_div);
        } else {
            sum_val = big_add(sum_val, term_div);
        }
    }
    return sum_val;
}

format_pi(pi_arr, target_digits) {
    len = size(pi_arr);
    if (len == 0) return "0.0000";

    str = to_string(pi_arr[len - 1]) + ".";
    printed_digits = 0;

    for (i = len - 2; i >= 0 && printed_digits < target_digits; i--) {
        v = pi_arr[i];
        if (v < 10) str += "000" + to_string(v);
        else if (v < 100) str += "00" + to_string(v);
        else if (v < 1000) str += "0" + to_string(v);
        else str += to_string(v);
        printed_digits += 4;
    }
    return str;
}

target_digits = 500;
num_blocks = target_digits / 4 + 3;

base_val = {};
base_val.reserve(num_blocks + 1);
for (i = 0; i < num_blocks; i++) {
    base_val.push_back(0);
}
base_val.push_back(1);
iters = 360;
atan5 = calc_arctan(5, base_val, iters);
atan239 = calc_arctan(239, base_val, iters);

term1 = big_mul_int(atan5, 16);
term2 = big_mul_int(atan239, 4);
pi_big = big_sub(term1, term2);
pi_str = format_pi(pi_big, target_digits);
println("高精度计算 PI (前 " + to_string(target_digits) + " 位) :");
println(pi_str);
return pi_str;