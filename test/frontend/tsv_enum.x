// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A macro turns an embedded TSV table into an enumeration and a name table.

static $::meta::bytes quoted($::meta::bytes name) {
    uptr size = $::meta::len(name);
    $::meta::buffer result = $::meta::alloc(size + 2);
    u8 *bytes = $::meta::data(result);
    bytes[0] = '"';
    for (uptr i = 0; i < size; ++i) {
        bytes[i + 1] = $::meta::at(name, i);
    }
    bytes[size + 1] = '"';
    return $::meta::freeze(result, size + 2);
}

// Each `NAME<TAB>VALUE` row becomes `NAME = VALUE,`, a row without a value
// `NAME,`, and with `names` set every row becomes `"NAME",`.
static $::meta::tokens tsv_rows($::meta::bytes text, u32 names) {
    $::meta::tokens rows = $::meta::parse("");
    uptr size = $::meta::len(text);
    for (uptr start = 0; start < size; ) {
        uptr end = start;
        while (end < size && $::meta::at(text, end) != '\n') ++end;
        uptr stop = end;
        if (stop > start && $::meta::at(text, stop - 1) == '\r') --stop;
        uptr tab = start;
        while (tab < stop && $::meta::at(text, tab) != '\t') ++tab;
        if (tab > start) {
            $::meta::bytes name = $::meta::slice(text, start, tab - start);
            if (names) {
                $::meta::tokens literal = $::meta::token("string", quoted(name));
                rows = $::meta::concat(rows, $::quote { $::unquote(literal), });
            } else if (tab < stop) {
                $::meta::tokens enumerator = $::meta::token("identifier", name);
                $::meta::tokens value =
                    $::meta::token("integer", $::meta::slice(text, tab + 1, stop - tab - 1));
                rows = $::meta::concat(rows, $::quote { $::unquote(enumerator) = $::unquote(value), });
            } else {
                $::meta::tokens enumerator = $::meta::token("identifier", name);
                rows = $::meta::concat(rows, $::quote { $::unquote(enumerator), });
            }
        }
        start = end + 1;
    }
    return rows;
}

[[macro]]
static $::meta::tokens item_table($::meta::tokens tag) {
    $::meta::bytes text = $::embed("tsv_enum_items.tsv");
    $::meta::tokens enumerators = tsv_rows(text, 0);
    $::meta::tokens names = tsv_rows(text, 1);
    return $::quote {
        enum $::unquote(tag) [[underlying(u16)]] { $::unquote(enumerators) };
        global const u8 *const item_names[] = { $::unquote(names) };
    };
}

item_table!(item)

$::static_assert(ITEM_SWORD == 1 && ITEM_POTION == 17 && ITEM_SHIELD == 18, "table values");
$::static_assert(sizeof(enum item) == 2, "underlying type");
$::static_assert(sizeof(item_names) / sizeof(item_names[0]) == 3, "one name per row");

global i32 tsv_enum_entry() {
    enum item selected = ITEM_SHIELD;
    if (selected != 18) return 1;
    if (item_names[1][5] != 'P' || item_names[2][11] != 0) return 2;
    return 7;
}
