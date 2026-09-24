// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    constexpr char bytes[] = {'A', '\0', static_cast<char>(0x80),
                              static_cast<char>(0xff), '!'};
    std::ofstream output(argv[1], std::ios::binary);
    output.write(bytes, sizeof(bytes));
    return output ? 0 : 2;
}
