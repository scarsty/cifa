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

string benchmark(mode, rounds) {
    a = {};
    a.reserve(128);
    for (int i = 0; i < 128; i++) a.push_back((i * 73 + 19) % 10000);
    result = {};
    int checksum = 0;
    for (int n = 0; n < rounds; n++) {
        if (mode == 0) result = big_mul_int(a, 239);
        else if (mode == 1) result = big_div_int(a, 239);
        else result = big_div_int(big_mul_int(a, 239), 239);
        checksum += result[0] + result[size(result) - 1];
    }
    string output = to_string(checksum);
    for (int i = 0; i < size(result); i++) output += ":" + to_string(result[i]);
    return output;
}
