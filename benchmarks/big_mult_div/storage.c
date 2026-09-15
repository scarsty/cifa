string scalar_benchmark(mode, rounds) {
    int checksum = 0;
    for (int n = 0; n < rounds; n++) {
        int value = 9007199254740993;
        for (int i = 0; i < 128; i++) { value++; }
        checksum += value % 1000003;
    }
    return to_string(checksum);
}
string scalar_no_scope_benchmark(mode, rounds) {
    int checksum = 0;
    for (int n = 0; n < rounds; n++) {
        int value = 9007199254740993;
        for (int i = 0; i < 128; i++) value++;
        checksum += value % 1000003;
    }
    return to_string(checksum);
}
string copy_benchmark(mode, rounds) {
    source = {};
    source.reserve(128);
    for (int i = 0; i < 128; i++) source.push_back(9007199254740993 + i);
    result = {};
    int checksum = 0;
    for (int n = 0; n < rounds; n++) {
        result = {};
        result.reserve(128);
        for (int i = 0; i < 128; i++) result.push_back(source[i]);
        checksum += result[0] % 1000003 + result[127] % 1000003;
    }
    string output = to_string(checksum);
    for (int i = 0; i < 128; i++) output += ":" + to_string(result[i]);
    return output;
}
string append_benchmark(mode, rounds) {
    int first = 9007199254740993;
    if (mode == 6) first = 19;
    result = {};
    int checksum = 0;
    for (int n = 0; n < rounds; n++) {
        result = {};
        result.reserve(128);
        int value = first;
        for (int i = 0; i < 128; i++) {
            result.push_back(value);
            value++;
        }
        checksum += result[0] % 1000003 + result[127] % 1000003;
    }
    string output = to_string(checksum);
    for (int i = 0; i < 128; i++) output += ":" + to_string(result[i]);
    return output;
}
