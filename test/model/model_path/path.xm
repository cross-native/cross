mangling "path-test" {
    entity = concat(
        "_ZP",
        path("", concat(decimal(bytes(text)), text))
    );
    label = concat(
        "_ZQ",
        path("", concat(decimal(bytes(text)), text))
    );
    generic = concat(entity, "G", decimal(count), arguments("", text, text));
}
