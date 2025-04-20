?+incr_reg
PSH @0
LDA @?0
ADD $1
SAR $?0
POP $0
?-

loop:

// ?incr_reg 1
// LDA @1
// SAM $128,01
// 
// ?incr_reg 2
// LDA @2
// SAM $128,02

LDA $1
SAM $128,00
JAL loop
