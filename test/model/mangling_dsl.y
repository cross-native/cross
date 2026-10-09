mangling "recursive-test" {
    helper text leaf(text value) = lookup(
        value,
        concat("u", decimal(bytes(value)), value),
        "i32", "i",
        "u32", "j"
    );

    helper text type_code(text value) = select(
        starts_with(value, "K"),
        concat(
            "K",
            type_code(slice(value, 1, subtract(length(value), 1)))
        ),
        select(
            starts_with(value, "V"),
            concat(
                "V",
                type_code(slice(value, 1, subtract(length(value), 1)))
            ),
            select(
                starts_with(value, "P"),
                concat(
                    "P",
                    type_code(
                        slice(value, 1, subtract(length(value), 1)
                    ))
                ),
                leaf(value)
            )
        )
    );

    helper text reference(integer value) =
        concat("S", radix(value, 36), "_");

    helper text parameter(text value) = substitute(
        value,
        type_code(value),
        reference(substitution_index)
    );

    entity = concat(
        "R",
        path("", concat(decimal(bytes(text)), text)),
        parameters("", parameter(text))
    );
    label = concat("L", path("_", text, true));
    generic = concat(
        entity,
        "G",
        arguments("", parameter(text), concat("V", text))
    );
}

# Shows the result and parameter spellings that a generic instance passes.
mangling "signature-test" {
    entity = name;
    label = name;
    generic = concat(
        name, "__", result, "__", parameters("_", concat(mode, text))
    );
}
