# A preset's control is independent of the optimization level name.
optimization "pressure_off" {
    inherits = "O3";
    f.unroll-register-pressure = false;
}
