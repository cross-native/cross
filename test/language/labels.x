global i32 dispatch(in bool choose_right) {
    label destination = choose_right ? dispatch::right : dispatch::left;
    goto destination;

left:
    return 11;
right:
    return 22;
}

global label first_dispatch_target = dispatch::left;

global i32 dispatch_left() {
    return dispatch(0);
}

global i32 dispatch_right() {
    return dispatch(1);
}

