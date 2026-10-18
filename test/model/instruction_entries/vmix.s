        .set noreorder
        .text
        vmix    $2, $3, $4              # 4a032080
        vblend  $7, $5, $6              # 4a0531c0
        vmix    $7, $5, 0x12345         # 3c010001 34212345 4a0509c0
        vrot    $2, $3, 5               # 4a20188a
        vrot    $2, $3, 40              # 4a201891
